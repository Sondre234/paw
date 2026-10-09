/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* Small pictures of a window, for the taskbar: a capture source that renders the window's
 * capture scene scaled down to fit a size, through an output of its own, smoothing on the
 * renderer as it shrinks. Adapted from wlroots 0.20's capture source for a scene node
 * (types/ext_image_capture_source_v1/scene.c), under wlroots' MIT license, which
 * vendor/tinywl/LICENSE keeps. */
#include "server.h"

/* The renderer samples bilinearly without mipmaps: drawn a tenth of its size in one pass, a
 * window would give each pixel of its picture four of the hundred pixels it covers, and text
 * would shimmer as it changes. So the scene is rendered at no less than half the resolution of
 * the window's own buffers, where every pixel still counts, and then halved on the renderer, two
 * by two pixels averaged into one, until it is within twice the picture's size; a last bilinear
 * pass makes it the picture's size. Each halving is a buffer kept while the source captures. */
#define MAX_STEPS 16
/* The most outputs a window's capture scene gets, one for each of these sources and one for its
 * full-size source: wlroots' scene takes 64 at most. */
#define MAX_OUTPUTS 16

struct scaled_source {
    struct wlr_ext_image_capture_source_v1 base;
    struct wlr_scene *scene; // the window's capture scene
    int max_width, max_height;
    struct wlr_backend backend;
    struct wlr_output output;
    struct wlr_scene_output *scene_output;
    /* The steps after the scene's render, halvings and the last one at the frame's size, and
     * the swapchain each draws into; none when the scene renders at the frame's size. */
    int step_count;
    struct {
        int width, height;
        struct wlr_swapchain *swapchain;
    } steps[MAX_STEPS];
    /* The frame the steps last made, kept for a session that has yet to get it. */
    struct wlr_buffer *last;
    size_t started;    // sessions capturing it
    bool held;         // the client still holds its resource
    bool destroying;
    struct wl_event_source *idle_destroy;
    struct wl_listener scene_destroy, scene_output_destroy, output_frame, resource_destroy;
};

struct scaled_frame_event {
    struct wlr_ext_image_capture_source_v1_frame_event base;
    struct wlr_buffer *buffer;
    struct timespec when;
};

/* What the window's surfaces cover in its capture scene, and how many buffer pixels the densest
 * of them has to a logical pixel. */
struct extents {
    int x1, y1, x2, y2;
    double density;
};

static void add_extents(struct wlr_scene_buffer *buffer, int sx, int sy, void *data) {
    struct extents *extents = data;
    int width = buffer->dst_width, height = buffer->dst_height;
    if ((width <= 0 || height <= 0) && buffer->buffer) {
        width = buffer->buffer->width;
        height = buffer->buffer->height;
        if (buffer->transform & WL_OUTPUT_TRANSFORM_90) {
            int swap = width;
            width = height;
            height = swap;
        }
    }
    if (width <= 0 || height <= 0)
        return;
    extents->x1 = sx < extents->x1 ? sx : extents->x1;
    extents->y1 = sy < extents->y1 ? sy : extents->y1;
    extents->x2 = sx + width > extents->x2 ? sx + width : extents->x2;
    extents->y2 = sy + height > extents->y2 ? sy + height : extents->y2;
    if (!buffer->buffer)
        return;
    bool cropped = !wlr_fbox_empty(&buffer->src_box);
    double across = cropped ? buffer->src_box.width : buffer->buffer->width;
    double down = cropped ? buffer->src_box.height : buffer->buffer->height;
    if (buffer->transform & WL_OUTPUT_TRANSFORM_90) {
        double swap = across;
        across = down;
        down = swap;
    }
    extents->density = fmax(extents->density, fmax(across / width, down / height));
}

static void drop_steps(struct scaled_source *source) {
    for (int i = 0; i < MAX_STEPS; ++i) {
        wlr_swapchain_destroy(source->steps[i].swapchain);
        source->steps[i].swapchain = NULL;
    }
    wlr_buffer_unlock(source->last);
    source->last = NULL;
}

/* Renders a frame of the window, when it changed or a session waits for one. The scene renders
 * at `scale`: the frame's, or half the density of the window's buffers where that is larger, but
 * never past its logical size; output_commit takes the steps planned here from there. */
static void source_render(struct scaled_source *source) {
    struct extents extents = {INT_MAX, INT_MAX, INT_MIN, INT_MIN, 1};
    wlr_scene_node_for_each_buffer(&source->scene->tree.node, add_extents, &extents);
    if (extents.x2 <= extents.x1 || extents.y2 <= extents.y1)
        return;
    int width = extents.x2 - extents.x1, height = extents.y2 - extents.y1;
    // The frame: scaled down to fit, never up. The scene rounds a node's size at a scale as
    // this does.
    float fit = (float)fmin(1, fmin((double)source->max_width / width,
                                    (double)source->max_height / height));
    int frame_width = (int)fmaxf(1, roundf(width * fit));
    int frame_height = (int)fmaxf(1, roundf(height * fit));
    float scale = fmaxf(fit, (float)fmin(1, extents.density / 2));
    int render_width = (int)fmaxf(1, roundf(width * scale));
    int render_height = (int)fmaxf(1, roundf(height * scale));
    source->step_count = 0;
    int step_width = render_width, step_height = render_height;
    while ((step_width > 2 * frame_width || step_height > 2 * frame_height) &&
           source->step_count < MAX_STEPS - 1) {
        step_width = (step_width + 1) / 2;
        step_height = (step_height + 1) / 2;
        source->steps[source->step_count].width = step_width;
        source->steps[source->step_count++].height = step_height;
    }
    if (step_width != frame_width || step_height != frame_height) {
        source->steps[source->step_count].width = frame_width;
        source->steps[source->step_count++].height = frame_height;
    }

    wlr_scene_output_set_position(source->scene_output, extents.x1, extents.y1);
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    wlr_output_state_set_custom_mode(&state, render_width, render_height, 0);
    wlr_output_state_set_scale(&state, scale);
    if (!wlr_scene_output_build_state(source->scene_output, &state, NULL) ||
        !wlr_output_commit_state(&source->output, &state))
        wlr_log(WLR_DEBUG, "Cannot render a window's picture at %dx%d", frame_width,
                frame_height);
    wlr_output_state_finish(&state);
}

static void send_frame_done(struct scaled_source *source) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(source->scene_output, &now);
}

static void source_destroy(struct scaled_source *source);

static void destroy_idle(void *data) {
    struct scaled_source *source = data;
    source->idle_destroy = NULL;
    if (!source->held && !source->started)
        source_destroy(source);
}

/* A source nobody holds or captures any more goes, once what let it go is over. */
static void destroy_unused(struct scaled_source *source) {
    if (source->held || source->started || source->destroying || source->idle_destroy)
        return;
    source->idle_destroy = wl_event_loop_add_idle(source->output.event_loop, destroy_idle, source);
}

static void source_start(struct wlr_ext_image_capture_source_v1 *base, bool with_cursors) {
    struct scaled_source *source = wl_container_of(base, source, base);
    if (source->started++ || !source->scene_output)
        return;
    source_render(source);
    send_frame_done(source);
}

static void source_stop(struct wlr_ext_image_capture_source_v1 *base) {
    struct scaled_source *source = wl_container_of(base, source, base);
    if (--source->started)
        return;
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, false);
    wlr_output_commit_state(&source->output, &state);
    wlr_output_state_finish(&state);
    drop_steps(source);
    destroy_unused(source);
}

/* A session asks for a frame. One owed what changed since its last (`schedule_frame`) gets the
 * frame as it stands even when nothing changes now: one started while another captures, or one
 * that let frames go by. wlroots' source for a scene node waits for the window to draw again. */
static void source_request_frame(struct wlr_ext_image_capture_source_v1 *base,
                                 bool schedule_frame) {
    struct scaled_source *source = wl_container_of(base, source, base);
    if (schedule_frame)
        wlr_output_update_needs_frame(&source->output);
    if (source->output.frame_pending)
        wlr_output_send_frame(&source->output);
}

static void source_copy_frame(struct wlr_ext_image_capture_source_v1 *base,
                              struct wlr_ext_image_copy_capture_frame_v1 *frame,
                              struct wlr_ext_image_capture_source_v1_frame_event *base_event) {
    struct scaled_source *source = wl_container_of(base, source, base);
    struct scaled_frame_event *event = wl_container_of(base_event, event, base);
    if (wlr_ext_image_copy_capture_frame_v1_copy_buffer(frame, event->buffer,
                                                       source->output.renderer))
        wlr_ext_image_copy_capture_frame_v1_ready(frame, WL_OUTPUT_TRANSFORM_NORMAL, &event->when);
}

static const struct wlr_ext_image_capture_source_v1_interface source_impl = {
    .start = source_start,
    .stop = source_stop,
    .request_frame = source_request_frame,
    .copy_frame = source_copy_frame,
};

static const struct wlr_backend_impl backend_impl = {0};

/* A buffer is taken only as it covers the output whole: the scene's render, or a window's own
 * buffer handed on as it is (direct scan-out) when it has the output's size. */
static bool output_test(struct wlr_output *output, const struct wlr_output_state *state) {
    uint32_t supported = WLR_OUTPUT_STATE_BACKEND_OPTIONAL | WLR_OUTPUT_STATE_BUFFER |
                         WLR_OUTPUT_STATE_ENABLED | WLR_OUTPUT_STATE_MODE;
    if (state->committed & ~supported)
        return false;
    if (!(state->committed & WLR_OUTPUT_STATE_BUFFER))
        return true;
    int width = output->width, height = output->height;
    if ((state->committed & WLR_OUTPUT_STATE_MODE) &&
        state->mode_type == WLR_OUTPUT_STATE_MODE_CUSTOM) {
        width = state->custom_mode.width;
        height = state->custom_mode.height;
    } else if ((state->committed & WLR_OUTPUT_STATE_MODE) && state->mode) {
        width = state->mode->width;
        height = state->mode->height;
    }
    const struct wlr_fbox *from = &state->buffer_src_box;
    const struct wlr_box *to = &state->buffer_dst_box;
    return state->buffer->width == width && state->buffer->height == height &&
           (wlr_fbox_empty(from) || (from->x == 0 && from->y == 0 && from->width == width &&
                                     from->height == height)) &&
           (wlr_box_empty(to) ||
            (to->x == 0 && to->y == 0 && to->width == width && to->height == height));
}

/* Draws `from` into `to`, scaled to its size and sampled bilinearly. */
static bool draw_scaled(struct wlr_renderer *renderer, struct wlr_buffer *from,
                        struct wlr_buffer *to) {
    struct wlr_texture *texture = wlr_texture_from_buffer(renderer, from);
    if (!texture)
        return false;
    struct wlr_render_pass *pass = wlr_renderer_begin_buffer_pass(renderer, to, NULL);
    bool ok = false;
    if (pass) {
        wlr_render_pass_add_texture(pass, &(struct wlr_render_texture_options){
                                              .texture = texture,
                                              .dst_box = {0, 0, to->width, to->height},
                                              .filter_mode = WLR_SCALE_FILTER_BILINEAR,
                                              .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
                                          });
        ok = wlr_render_pass_submit(pass);
    }
    wlr_texture_destroy(texture);
    return ok;
}

/* Takes the scene's render through the steps into a new frame, made the last one. */
static bool take_steps(struct scaled_source *source, struct wlr_buffer *render) {
    struct wlr_output *output = &source->output;
    struct wlr_buffer *from = render;
    for (int i = 0; i < source->step_count; ++i) {
        struct wlr_swapchain **swapchain = &source->steps[i].swapchain;
        if (*swapchain && ((*swapchain)->width != source->steps[i].width ||
                           (*swapchain)->height != source->steps[i].height ||
                           (*swapchain)->format.format != output->swapchain->format.format)) {
            wlr_swapchain_destroy(*swapchain);
            *swapchain = NULL;
        }
        if (!*swapchain)
            *swapchain = wlr_swapchain_create(output->allocator, source->steps[i].width,
                                              source->steps[i].height, &output->swapchain->format);
        struct wlr_buffer *to = *swapchain ? wlr_swapchain_acquire(*swapchain) : NULL;
        bool ok = to && draw_scaled(output->renderer, from, to);
        if (from != render)
            wlr_buffer_unlock(from);
        if (!ok) {
            wlr_buffer_unlock(to);
            return false;
        }
        from = to;
    }
    wlr_buffer_unlock(source->last);
    source->last = from;
    return true;
}

static bool output_commit(struct wlr_output *output, const struct wlr_output_state *state) {
    struct scaled_source *source = wl_container_of(output, source, output);
    if ((state->committed & WLR_OUTPUT_STATE_ENABLED) && !state->enabled)
        return true;
    if (!(state->committed & WLR_OUTPUT_STATE_BUFFER))
        return false;
    // The scene renders a frame without change too, for a session owed one: then the steps
    // need not be taken again.
    bool changed = !(state->committed & WLR_OUTPUT_STATE_DAMAGE) ||
                   !pixman_region32_empty(&state->damage) ||
                   (state->committed & WLR_OUTPUT_STATE_MODE);
    struct wlr_buffer *frame = state->buffer;
    struct wlr_swapchain *swapchain = output->swapchain;
    if (source->step_count) {
        struct wlr_buffer *last = source->last;
        if (changed || !last || last->width != source->steps[source->step_count - 1].width ||
            last->height != source->steps[source->step_count - 1].height) {
            if (!take_steps(source, state->buffer))
                return false;
            changed = true;
        }
        frame = source->last;
        swapchain = source->steps[source->step_count - 1].swapchain;
    } else if (source->last) {
        drop_steps(source);
    }
    if ((uint32_t)frame->width != source->base.width ||
        (uint32_t)frame->height != source->base.height)
        wlr_ext_image_capture_source_v1_set_constraints_from_swapchain(&source->base, swapchain,
                                                                       output->renderer);

    pixman_region32_t damage;
    if (changed)
        pixman_region32_init_rect(&damage, 0, 0, frame->width, frame->height);
    else
        pixman_region32_init(&damage);
    struct scaled_frame_event event = {.base = {.damage = &damage}, .buffer = frame};
    clock_gettime(CLOCK_MONOTONIC, &event.when);
    wl_signal_emit_mutable(&source->base.events.frame, &event.base);
    pixman_region32_fini(&damage);
    return true;
}

static const struct wlr_output_impl output_impl = {
    .test = output_test,
    .commit = output_commit,
};

static void source_destroy(struct scaled_source *source) {
    source->destroying = true;
    if (source->idle_destroy)
        wl_event_source_remove(source->idle_destroy);
    wl_list_remove(&source->scene_destroy.link);
    wl_list_remove(&source->scene_output_destroy.link);
    wl_list_remove(&source->output_frame.link);
    wl_list_remove(&source->resource_destroy.link);
    wlr_ext_image_capture_source_v1_finish(&source->base); // stopping its sessions
    drop_steps(source);
    wlr_scene_output_destroy(source->scene_output);
    wlr_output_finish(&source->output);
    wlr_backend_finish(&source->backend);
    free(source);
}

static void handle_scene_destroy(struct wl_listener *listener, void *data) {
    struct scaled_source *source = wl_container_of(listener, source, scene_destroy);
    source_destroy(source);
}

static void handle_scene_output_destroy(struct wl_listener *listener, void *data) {
    struct scaled_source *source = wl_container_of(listener, source, scene_output_destroy);
    source->scene_output = NULL;
    wl_list_remove(&listener->link);
    wl_list_init(&listener->link);
}

/* The output's frame: rendered when the window changed or a session waits for one. Windows that
 * show nowhere else (minimized, on a workspace not shown) draw at this pace. */
static void handle_output_frame(struct wl_listener *listener, void *data) {
    struct scaled_source *source = wl_container_of(listener, source, output_frame);
    if (!source->scene_output)
        return;
    if (wlr_scene_output_needs_frame(source->scene_output))
        source_render(source);
    send_frame_done(source);
}

static void handle_resource_destroy(struct wl_listener *listener, void *data) {
    struct scaled_source *source = wl_container_of(listener, source, resource_destroy);
    wl_list_remove(&listener->link);
    wl_list_init(&listener->link);
    source->held = false;
    destroy_unused(source);
}

static struct scaled_source *source_create(struct sh_toplevel *toplevel, int max_width,
                                           int max_height) {
    struct sh_server *server = toplevel->server;
    struct scaled_source *source = calloc(1, sizeof(*source));
    if (!source)
        return NULL;
    source->scene = toplevel->capture_scene;
    source->max_width = max_width;
    source->max_height = max_height;
    wlr_ext_image_capture_source_v1_init(&source->base, &source_impl);
    wlr_backend_init(&source->backend, &backend_impl);
    source->backend.buffer_caps = WLR_BUFFER_CAP_DMABUF | WLR_BUFFER_CAP_SHM;
    wlr_output_init(&source->output, &source->backend, &output_impl,
                    wl_display_get_event_loop(server->wl_display), NULL);
    static unsigned long count;
    char name[32];
    snprintf(name, sizeof(name), "PICTURE-%lu", ++count);
    wlr_output_set_name(&source->output, name);
    wl_list_init(&source->scene_output_destroy.link);
    wl_list_init(&source->output_frame.link);
    wl_list_init(&source->resource_destroy.link);
    source->scene_destroy.notify = handle_scene_destroy;
    wl_signal_add(&source->scene->tree.node.events.destroy, &source->scene_destroy);
    if (!wlr_output_init_render(&source->output, server->allocator, server->renderer) ||
        !(source->scene_output = wlr_scene_output_create(source->scene, &source->output))) {
        source_destroy(source);
        return NULL;
    }
    source->scene_output_destroy.notify = handle_scene_output_destroy;
    wl_signal_add(&source->scene_output->events.destroy, &source->scene_output_destroy);
    source->output_frame.notify = handle_output_frame;
    wl_signal_add(&source->output.events.frame, &source->output_frame);
    return source;
}

/* Fills `sources` with the window's scaled capture sources, up to `size` of them, and returns
 * how many it has. */
size_t list_picture_sources(struct sh_toplevel *toplevel, struct sh_picture_source *sources,
                            size_t size) {
    size_t count = 0;
    struct wlr_scene_output *scene_output;
    if (!toplevel->capture_scene)
        return 0;
    wl_list_for_each(scene_output, &toplevel->capture_scene->outputs, link) {
        if (scene_output->output->impl != &output_impl)
            continue; // the full-size source's
        struct scaled_source *source = wl_container_of(scene_output->output, source, output);
        if (count < size)
            sources[count] = (struct sh_picture_source){
                source->max_width, source->max_height, (int)source->base.width,
                (int)source->base.height, source->started};
        ++count;
    }
    return count;
}

/* Gives the client the capture source `id` for a picture of the window within `width` by
 * `height` buffer pixels: a source of its own, which goes once the client neither holds nor
 * captures it, or with the window's capture scene. It is inert when there is no window (NULL),
 * the session is locked, or the window has too many already. */
void create_scaled_capture_source(struct wl_client *client, uint32_t id,
                                  struct sh_toplevel *toplevel, uint32_t width,
                                  uint32_t height) {
    struct scaled_source *source = NULL;
    if (toplevel && !toplevel->server->locked && toplevel->capture_scene &&
        wl_list_length(&toplevel->capture_scene->outputs) < MAX_OUTPUTS)
        source = source_create(toplevel, width < 65536 ? (int)width : 65536,
                               height < 65536 ? (int)height : 65536);
    if (!wlr_ext_image_capture_source_v1_create_resource(source ? &source->base : NULL, client,
                                                         id)) {
        if (source)
            source_destroy(source);
        return;
    }
    if (!source)
        return;
    source->resource_destroy.notify = handle_resource_destroy;
    wl_resource_add_destroy_listener(wl_client_get_object(client, id), &source->resource_destroy);
    source->held = true;
}
