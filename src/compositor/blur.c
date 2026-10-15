/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The blur behind windows (windows.blur): each output's background, the background layer's
 * surfaces over appearance.background, drawn small on the renderer and blurred on the CPU
 * (src/blur.c) once as it changes, never on every frame; and under each window's surfaces,
 * inside its rounded corners, the part of that picture the window covers on the screen, placed
 * again before each frame wherever the window is drawn. */
#include "server.h"
#include "paw/blur.h"
#include <drm_fourcc.h>
#include <wlr/util/transform.h>

/* A background that changed is blurred once it has not changed for SETTLE_MS (a wallpaper
 * fading in, a profile switching), but at least every MOST_MS while it keeps changing. */
#define SETTLE_MS 150
#define MOST_MS 500
/* The blur's opacity under a window: just below 1, which looks the same, so that the scene does
 * not take it for opaque and hide what is under it. A desktop it hid would get no frame
 * callbacks, and could not draw a new wallpaper for the blur to show. */
#define SOLID 0.9999f

static bool no_input(struct wlr_scene_buffer *buffer, double *sx, double *sy) {
    return false;
}

/* Whether anything is blurred: windows.blur, or a rule that blurs some windows without it. */
static bool wanted(struct sh_server *server) {
    const struct sh_settings *settings = server_settings(server);
    return settings->blur || settings->blur_by_rule;
}

/* What a background is drawn over: appearance.background, over black where it is translucent. */
static void under_colour(struct sh_server *server, float colour[4]) {
    const float *background = server_settings(server)->background; // premultiplied
    colour[0] = background[0], colour[1] = background[1], colour[2] = background[2];
    colour[3] = 1;
}

struct drawing {
    struct wlr_renderer *renderer;
    struct wlr_render_pass *pass;
    struct wlr_box box;   // the output's, in the layout
    double across, down;  // picture pixels per logical pixel
    struct wlr_texture *made[16]; // textures made for this drawing, destroyed after it
    size_t made_count;
};

/* Draws one buffer of the background layer, `lx`, `ly` in the layout, into the picture. */
static void draw_buffer(struct wlr_scene_buffer *buffer, int lx, int ly, void *data) {
    struct drawing *drawing = data;
    if (!buffer->buffer || buffer->opacity <= 0)
        return;
    int width = buffer->dst_width, height = buffer->dst_height;
    if (width <= 0 || height <= 0) {
        bool turned = buffer->transform & WL_OUTPUT_TRANSFORM_90;
        width = turned ? buffer->buffer->height : buffer->buffer->width;
        height = turned ? buffer->buffer->width : buffer->buffer->height;
    }
    struct wlr_box node = {lx, ly, width, height}, inside;
    if (!wlr_box_intersection(&inside, &node, &drawing->box))
        return;
    struct wlr_client_buffer *client = wlr_client_buffer_get(buffer->buffer);
    struct wlr_texture *texture = client ? client->texture : NULL;
    if (!texture) {
        if (drawing->made_count == sizeof(drawing->made) / sizeof(*drawing->made))
            return;
        texture = wlr_texture_from_buffer(drawing->renderer, buffer->buffer);
        if (!texture)
            return;
        drawing->made[drawing->made_count++] = texture;
    }
    const struct wlr_box *box = &drawing->box;
    int left = (int)lround((lx - box->x) * drawing->across);
    int top = (int)lround((ly - box->y) * drawing->down);
    int right = (int)lround((lx + width - box->x) * drawing->across);
    int bottom = (int)lround((ly + height - box->y) * drawing->down);
    if (right <= left || bottom <= top)
        return;
    wlr_render_pass_add_texture(drawing->pass,
                                &(struct wlr_render_texture_options){
                                    .texture = texture,
                                    .src_box = buffer->src_box,
                                    .dst_box = {left, top, right - left, bottom - top},
                                    .transform = wlr_output_transform_invert(buffer->transform),
                                    .alpha = &buffer->opacity,
                                    .filter_mode = WLR_SCALE_FILTER_BILINEAR,
                                    .blend_mode = WLR_RENDER_BLEND_MODE_PREMULTIPLIED,
                                });
}

/* The background of `box` drawn into `target`, read back as XRGB8888 pixels; NULL when it cannot
 * be. Drawn at half the output's pixels, or less, the renderer averages each two by two. */
static uint32_t *draw_background(struct sh_server *server, struct wlr_buffer *target,
                                 struct wlr_box box) {
    struct wlr_renderer *renderer = server->renderer;
    struct wlr_render_pass *pass = wlr_renderer_begin_buffer_pass(renderer, target, NULL);
    if (!pass)
        return NULL;
    float colour[4];
    under_colour(server, colour);
    wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
                                       .box = {0, 0, target->width, target->height},
                                       .color = {colour[0], colour[1], colour[2], colour[3]},
                                       .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
                                   });
    struct drawing drawing = {
        .renderer = renderer,
        .pass = pass,
        .box = box,
        .across = (double)target->width / box.width,
        .down = (double)target->height / box.height,
    };
    wlr_scene_node_for_each_buffer(&server->layer_trees[0]->node, draw_buffer, &drawing);
    bool drawn = wlr_render_pass_submit(pass);
    for (size_t i = 0; i < drawing.made_count; ++i)
        wlr_texture_destroy(drawing.made[i]);
    if (!drawn)
        return NULL;
    struct wlr_texture *texture = wlr_texture_from_buffer(renderer, target);
    size_t count = (size_t)target->width * target->height;
    uint32_t *pixels = texture ? malloc(count * sizeof(*pixels)) : NULL;
    bool read = pixels && wlr_texture_read_pixels(
                              texture, &(struct wlr_texture_read_pixels_options){
                                           .data = pixels,
                                           .format = DRM_FORMAT_XRGB8888,
                                           .stride = (uint32_t)target->width * 4,
                                       });
    if (texture)
        wlr_texture_destroy(texture);
    if (!read) {
        free(pixels);
        return NULL;
    }
    return pixels;
}

/* `pixels` drawn over the whole of `target`, as they are. */
static bool upload(struct sh_server *server, struct wlr_buffer *target, uint32_t *pixels) {
    struct wlr_texture *texture =
        wlr_texture_from_pixels(server->renderer, DRM_FORMAT_XRGB8888,
                                (uint32_t)target->width * 4, (uint32_t)target->width,
                                (uint32_t)target->height, pixels);
    if (!texture)
        return false;
    struct wlr_render_pass *pass = wlr_renderer_begin_buffer_pass(server->renderer, target, NULL);
    bool done = false;
    if (pass) {
        wlr_render_pass_add_texture(pass, &(struct wlr_render_texture_options){
                                              .texture = texture,
                                              .filter_mode = WLR_SCALE_FILTER_NEAREST,
                                              .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
                                          });
        done = wlr_render_pass_submit(pass);
    }
    wlr_texture_destroy(texture);
    return done;
}

static void place_blur(struct sh_toplevel *toplevel);

/* Every window's blur placed again, as a picture changed: the scene then draws the frames it
 * needs. */
static void place_all(struct sh_server *server) {
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel->blur)
            place_blur(toplevel);
    }
}

static void drop_picture(struct sh_blur_picture *picture) {
    wlr_buffer_drop(picture->buffer); // windows showing it keep it until they move on
    picture->buffer = NULL;
    picture->width = picture->height = 0;
}

/* Takes `output`'s picture: its background as it shows now, blurred. A picture it cannot take
 * leaves the one it had, if any; one of an output that is off or out of the layout goes. Its
 * box, size, scale and transform are noted either way, so that it is tried again only once they
 * or the background change. */
static void take_picture(struct sh_output *output) {
    struct sh_server *server = output->server;
    struct sh_blur_picture *picture = &output->blur;
    struct wlr_output *wlr_output = output->wlr_output;
    uint64_t started = now_ns();
    picture->due = false;
    struct wlr_box box;
    wlr_output_layout_get_box(server->output_layout, wlr_output, &box);
    picture->tried = true;
    picture->box = box;
    picture->output_width = wlr_output->width;
    picture->output_height = wlr_output->height;
    picture->scale = wlr_output->scale;
    picture->transform = wlr_output->transform;
    if (output->disabled || !wlr_output->enabled || wlr_box_empty(&box)) {
        drop_picture(picture);
        return;
    }
    // Half the output's pixels each way: enough for grain finer than a logical pixel on a dense
    // output, and the renderer averages each two by two of a wallpaper at the output's size.
    int width = (int)fmax(1, ceil(box.width * wlr_output->scale / 2));
    int height = (int)fmax(1, ceil(box.height * wlr_output->scale / 2));
    const struct wlr_drm_format *format =
        wlr_output->swapchain ? &wlr_output->swapchain->format : NULL;
    struct wlr_buffer *target =
        format ? wlr_allocator_create_buffer(server->allocator, width, height, format) : NULL;
    uint32_t *pixels = target ? draw_background(server, target, box) : NULL;
    uint32_t *blurred = pixels ? malloc((size_t)width * height * sizeof(*blurred)) : NULL;
    const struct sh_blur *blur = &server->blur;
    struct sh_blur_look look = {
        .sigma = blur->radius / 2.0 * width / box.width, // CSS's blur radius is two of them
        .noise = blur->noise,
        .brightness = blur->brightness,
        .saturation = blur->saturation,
    };
    bool made = blurred && sh_blur_paint(pixels, width, height, &look, blurred) &&
                upload(server, target, blurred);
    free(pixels);
    free(blurred);
    if (!made) {
        wlr_log(WLR_ERROR, "Cannot blur the background of %s", wlr_output->name);
        if (target)
            wlr_buffer_drop(target);
        return;
    }
    drop_picture(picture);
    picture->buffer = target;
    picture->width = width;
    picture->height = height;
    ++picture->taken;
    picture->took_us = (uint32_t)((now_ns() - started) / 1000);
}

/* Sets the timer for the first picture due, or stops it with none. */
static void arm(struct sh_server *server) {
    struct sh_blur *blur = &server->blur;
    if (!blur->timer)
        return;
    int64_t next = INT64_MAX;
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        const struct sh_blur_picture *picture = &output->blur;
        if (!picture->due)
            continue;
        int64_t at = picture->changed_at + SETTLE_MS;
        if (picture->due_since + MOST_MS < at)
            at = picture->due_since + MOST_MS;
        if (at < next)
            next = at;
    }
    if (next == INT64_MAX) {
        wl_event_source_timer_update(blur->timer, 0);
        return;
    }
    int64_t wait = next - now_ms();
    wl_event_source_timer_update(blur->timer, wait < 1 ? 1 : (int)wait);
}

/* `output`'s background changed: a new picture is due once it settles. With `now`, at the next
 * turn of the event loop. */
static void ask(struct sh_output *output, bool now) {
    struct sh_blur_picture *picture = &output->blur;
    int64_t time = now_ms();
    if (!picture->due) {
        picture->due = true;
        picture->due_since = time;
    }
    picture->changed_at = time;
    if (now)
        picture->due_since = picture->changed_at = time - MOST_MS;
    arm(output->server);
}

/* Takes the pictures that are due, all of them with `all`, else those that settled. */
static void take_due(struct sh_server *server, bool all) {
    int64_t now = now_ms();
    bool taken = false;
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        struct sh_blur_picture *picture = &output->blur;
        if (picture->due && (all || now - picture->changed_at >= SETTLE_MS ||
                             now - picture->due_since >= MOST_MS)) {
            take_picture(output);
            taken = true;
        }
    }
    if (taken)
        place_all(server);
    arm(server);
}

static int timer_fired(void *data) {
    take_due(data, false);
    return 0;
}

/* What decides how the pictures look, from the settings: a change takes them all again. */
static struct sh_blur look_of(struct sh_server *server) {
    const struct sh_settings *settings = server_settings(server);
    struct sh_blur look = {
        .on = wanted(server),
        .radius = settings->blur_radius,
        .noise = settings->blur_noise,
        .brightness = settings->blur_brightness,
        .saturation = settings->blur_saturation,
    };
    under_colour(server, look.background);
    return look;
}

/* At startup and after each reload, before the windows' frames are refreshed: the pictures go
 * once nothing is blurred, and are taken again at once when how they look changed. */
void blur_reload(struct sh_server *server) {
    struct sh_blur *blur = &server->blur;
    struct sh_blur look = look_of(server);
    struct sh_output *output;
    if (!look.on) {
        blur->on = false;
        for_each_connected_output(output, server) {
            drop_picture(&output->blur);
            output->blur.due = output->blur.tried = false;
        }
        arm(server);
        return;
    }
    bool changed = !blur->on || blur->radius != look.radius || blur->noise != look.noise ||
                   blur->brightness != look.brightness || blur->saturation != look.saturation ||
                   memcmp(blur->background, look.background, sizeof(look.background));
    look.timer = blur->timer;
    *blur = look;
    if (!blur->timer)
        blur->timer = wl_event_loop_add_timer(wl_display_get_event_loop(server->wl_display),
                                              timer_fired, server);
    if (changed) {
        wl_list_for_each(output, &server->outputs, link) ask(output, true);
    }
}

/* A surface of the background layer on `wlr_output` committed a buffer, mapped, unmapped or
 * went: its picture is taken again once it settles. */
void blur_background_changed(struct sh_server *server, struct wlr_output *wlr_output) {
    if (!server->blur.on || !wlr_output)
        return;
    struct sh_output *output = sh_output_for(server, wlr_output);
    if (output && !output->disabled)
        ask(output, false);
}

/* Takes the pictures due now rather than once they settle: the shell has drawn a new
 * profile's look, which the crossfade is about to show. */
void blur_flush(struct sh_server *server) {
    if (server->blur.on)
        take_due(server, true);
}

/* Where the window is drawn and which picture it shows there: the part of it on the output under
 * its middle (or the nearest one), the part of that output's picture under that part, placed in
 * `content` where it is. Nothing while it is hidden; no blur where it covers no picture. */
static void place_blur(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    struct wlr_scene_buffer *blur = toplevel->blur, *probe = toplevel->blur_probe;
    int cx, cy;
    if (!wlr_scene_node_coords(&toplevel->content->node, &cx, &cy))
        return; // hidden: placed again as it shows
    struct sh_blur_box drawn = {cx + probe->node.x, cy + probe->node.y, probe->dst_width,
                                probe->dst_height};
    double mx = drawn.x + drawn.width / 2.0, my = drawn.y + drawn.height / 2.0;
    struct wlr_output *wlr_output = wlr_output_layout_output_at(server->output_layout, mx, my);
    if (!wlr_output) {
        double nx, ny;
        wlr_output_layout_closest_point(server->output_layout, NULL, mx, my, &nx, &ny);
        wlr_output = wlr_output_layout_output_at(server->output_layout, nx, ny);
    }
    struct sh_output *output = wlr_output ? sh_output_for(server, wlr_output) : NULL;
    const struct sh_blur_picture *picture = output ? &output->blur : NULL;
    struct sh_blur_box part;
    struct sh_blur_area source;
    if (!picture || !picture->buffer ||
        !sh_blur_source(drawn,
                        (struct sh_blur_box){picture->box.x, picture->box.y, picture->box.width,
                                             picture->box.height},
                        picture->width, picture->height, &part, &source)) {
        wlr_scene_node_set_enabled(&blur->node, false);
        return;
    }
    if (blur->buffer != picture->buffer)
        wlr_scene_buffer_set_buffer(blur, picture->buffer);
    wlr_scene_buffer_set_source_box(
        blur, &(struct wlr_fbox){source.x, source.y, source.width, source.height});
    wlr_scene_buffer_set_dest_size(blur, part.width, part.height);
    wlr_scene_node_set_position(&blur->node, part.x - cx, part.y - cy);
    wlr_scene_node_set_enabled(&blur->node, true);
}

/* Before each frame, after the animations have moved the windows: each window's blur follows
 * where it is drawn, and an output whose box, size, scale or transform changed since its picture
 * was taken gets a new one once that settles. */
void blur_frame(struct sh_server *server) {
    if (!server->blur.on)
        return;
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        struct sh_blur_picture *picture = &output->blur;
        struct wlr_output *wlr_output = output->wlr_output;
        if (picture->due || !wlr_output->enabled)
            continue;
        struct wlr_box box;
        wlr_output_layout_get_box(server->output_layout, wlr_output, &box);
        if (!picture->tried || !wlr_box_equal(&box, &picture->box) ||
            wlr_output->width != picture->output_width ||
            wlr_output->height != picture->output_height ||
            wlr_output->scale != picture->scale || wlr_output->transform != picture->transform)
            ask(output, false);
    }
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel->blur)
            place_blur(toplevel);
    }
}

static void remove_blur(struct sh_toplevel *toplevel) {
    if (!toplevel->blur)
        return;
    if (toplevel->anim.placed_apart == &toplevel->blur->node)
        toplevel->anim.placed_apart = NULL;
    wlr_scene_node_destroy(&toplevel->blur->node);
    wlr_scene_node_destroy(&toplevel->blur_probe->node);
    toplevel->blur = toplevel->blur_probe = NULL;
}

/* A hash of the window's app ID and title, which its rules were matched on. */
static uint64_t rule_key(struct sh_toplevel *toplevel) {
    uint64_t hash = 1469598103934665603ULL;
    const char *texts[] = {toplevel_app_id(toplevel), toplevel_title(toplevel)};
    for (int i = 0; i < 2; ++i) {
        for (const char *c = texts[i] ? texts[i] : ""; *c; ++c)
            hash = (hash ^ (unsigned char)*c) * 1099511628211ULL;
        hash = (hash ^ 0x100) * 1099511628211ULL; // the end of one text, apart from the next
    }
    return hash;
}

/* Whether the window is blurred: windows.blur, unless a rule says otherwise. The rules are
 * matched again only as the title, the app ID or the configuration changes, not on every
 * commit. */
static bool blurred(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    const struct sh_settings *settings = server_settings(server);
    if (!settings->blur_rules)
        return settings->blur;
    struct sh_blur_rule *rule = &toplevel->blur_rule;
    uint64_t key = rule_key(toplevel);
    if (rule->generation != server->config_generation || rule->key != key) {
        struct sh_window_rule found;
        rule->value = window_rule(toplevel, &found) ? found.blur : -1;
        rule->generation = server->config_generation;
        rule->key = key;
    }
    return rule->value < 0 ? settings->blur : rule->value > 0;
}

/* From refresh_frame: a mapped window that is not fullscreen gets a blur while anything is
 * blurred and the rules leave it one, at `opacity` (what a peek leaves of it; the window's own
 * opacity is not the blur's), and loses it otherwise. Placed before the next frame. */
void refresh_blur(struct sh_toplevel *toplevel, float opacity) {
    struct sh_server *server = toplevel->server;
    if (!server->blur.on || !toplevel->content || !toplevel->surfaces || toplevel->unmanaged ||
        !toplevel_mapped(toplevel) || toplevel->fullscreen || !blurred(toplevel)) {
        remove_blur(toplevel);
        return;
    }
    if (!toplevel->blur) {
        struct wlr_scene_buffer *probe = wlr_scene_buffer_create(toplevel->content, NULL);
        struct wlr_scene_buffer *blur =
            probe ? wlr_scene_buffer_create(toplevel->content, NULL) : NULL;
        if (!blur) {
            if (probe)
                wlr_scene_node_destroy(&probe->node);
            return;
        }
        probe->point_accepts_input = blur->point_accepts_input = no_input;
        wlr_scene_node_place_below(&probe->node, &toplevel->surfaces->node);
        wlr_scene_node_place_below(&blur->node, &toplevel->surfaces->node);
        wlr_scene_node_set_enabled(&blur->node, false); // until it is placed
        toplevel->blur = blur;
        toplevel->blur_probe = probe;
        toplevel->anim.placed_apart = &blur->node;
    }
    // The probe stands where the frame is (the geometry, at the size drawn while it changes
    // size), for the animations to draw where they draw the rest.
    struct wlr_box g = frame_geometry(toplevel);
    wlr_scene_node_set_position(&toplevel->blur_probe->node, 0, 0);
    wlr_scene_buffer_set_dest_size(toplevel->blur_probe, g.width > 0 ? g.width : 1,
                                   g.height > 0 ? g.height : 1);
    wlr_scene_buffer_set_opacity(toplevel->blur, SOLID * opacity);
}

/* `output` goes, and its picture with it. */
void blur_forget_output(struct sh_output *output) {
    drop_picture(&output->blur);
    output->blur.due = false;
    arm(output->server);
}

void blur_finish(struct sh_server *server) {
    struct sh_output *output;
    for_each_connected_output(output, server) drop_picture(&output->blur);
    if (server->blur.timer)
        wl_event_source_remove(server->blur.timer);
    server->blur.timer = NULL;
    server->blur.on = false;
}

/* `get blur`: a line per output in the layout, "output", its name, the picture's width and
 * height (0 for none), how many pictures were taken, how long the last took in microseconds and
 * whether a new one is due; then a line per window with a blur, "window", its title, whether the
 * blur shows (1) or not (0), the part of the layout it covers (x, y, width, height) and where
 * that lies in the picture (x, y, width, height, rounded), the last two as the window was last
 * drawn, and its opacity in thousandths. */
void describe_blur(struct sh_server *server, int fd, const char *arguments) {
    control_reply(fd, "ok\n");
    char line[1024];
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        const struct sh_blur_picture *picture = &output->blur;
        snprintf(line, sizeof(line), "output\t%s\t%d\t%d\t%u\t%u\t%d\n", output->wlr_output->name,
                 picture->width, picture->height, picture->taken, picture->took_us,
                 picture->due);
        control_reply(fd, line);
    }
    struct sh_toplevel *toplevel;
    wl_list_for_each_reverse(toplevel, &server->toplevels, link) {
        struct wlr_scene_buffer *blur = toplevel->blur;
        if (!blur)
            continue;
        char title[512];
        copy_field(title, sizeof(title), toplevel_title(toplevel));
        int x = 0, y = 0;
        wlr_scene_node_coords(&blur->node, &x, &y);
        const struct wlr_fbox *source = &blur->src_box;
        snprintf(line, sizeof(line), "window\t%s\t%d\t%d\t%d\t%d\t%d\t%ld\t%ld\t%ld\t%ld\t%ld\n",
                 title, blur->node.enabled && blur->buffer, x, y, blur->dst_width,
                 blur->dst_height, lround(source->x), lround(source->y), lround(source->width),
                 lround(source->height), lround(1000 * blur->opacity));
        control_reply(fd, line);
    }
}
