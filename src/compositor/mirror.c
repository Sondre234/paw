/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Mirroring: a monitor out of the layout showing another's picture (outputs.monitors' mirror),
 * scaled to fit and letterboxed. Each frame the source commits is drawn onto the mirror as one
 * texture, with the source's hardware cursor over it, so the mirror shows exactly what the
 * source does (the magnifier, night light, the lock) and costs one scaled copy per frame. */
#include "server.h"
#include <drm_fourcc.h>
#include <wlr/util/transform.h>

struct sh_mirror {
    struct sh_output *output, *source;
    /* The newest frame the source committed, locked: where its buffer goes on the source (the
     * part of it, `src_box`, and the box it fills, `dst_box`; empty for all of either), the
     * source's size and transform then, and whether the session was locked, which the frame
     * shows then. */
    struct wlr_buffer *buffer;
    struct wlr_fbox src_box;
    struct wlr_box dst_box;
    int width, height;
    enum wl_output_transform transform;
    /* The colours of a source driven in HDR (hdr.c), which an SDR mirror converts: its transfer
     * function and primaries, 0 for SDR's. */
    enum wlr_color_transfer_function transfer_function;
    enum wlr_color_named_primaries primaries;
    bool shows_lock;
    bool fresh; // a frame or a change the mirror has not drawn yet
    /* The source's hardware cursor as last drawn, which no frame of the source holds. */
    struct {
        bool shown;
        double x, y;
        struct wlr_texture *texture;
    } cursor;
    struct wlr_buffer *shown; // the mirror's own last frame, for tests (mirror_capture)
    struct wl_listener source_commit, source_destroy;
};

/* The output `output` mirrors as the settings in force say, while that one is in the layout: a
 * connector name, else the first a "desc:" key matches. A source that mirrors another itself
 * lends its own. NULL for none. */
struct sh_output *mirror_source(struct sh_server *server, struct sh_output *output) {
    const struct sh_monitor *monitor = output_monitor(output);
    if (!monitor || !monitor->enabled || !monitor->mirror[0])
        return NULL;
    struct sh_output *named = NULL, *candidate;
    for (int exact = 1; exact >= 0 && !named; --exact) {
        for_each_connected_output(candidate, server) {
            if (exact ? output_named(candidate, monitor->mirror)
                      : output_key_matches(monitor->mirror, candidate->wlr_output)) {
                named = candidate;
                break;
            }
        }
    }
    if (named && named->mirror)
        named = named->mirror->source;
    if (!named || named == output || named->disabled)
        return NULL;
    return named;
}

static void detach_source(struct sh_mirror *mirror) {
    if (!mirror->source)
        return;
    wl_list_remove(&mirror->source_commit.link);
    wl_list_remove(&mirror->source_destroy.link);
    mirror->source = NULL;
    if (mirror->buffer)
        wlr_buffer_unlock(mirror->buffer);
    mirror->buffer = NULL;
    mirror->fresh = true;
}

/* The source committed: a new frame, its hardware cursor moved, or it turned off. The mirror
 * draws again at its next frame. */
static void source_commit(struct wl_listener *listener, void *data) {
    struct sh_mirror *mirror = wl_container_of(listener, mirror, source_commit);
    const struct wlr_output_event_commit *event = data;
    const struct wlr_output_state *state = event->state;
    if (!event->output->enabled) {
        if (mirror->buffer)
            wlr_buffer_unlock(mirror->buffer);
        mirror->buffer = NULL;
        mirror->fresh = true;
    } else if ((state->committed & WLR_OUTPUT_STATE_BUFFER) && state->buffer) {
        struct wlr_buffer *previous = mirror->buffer;
        mirror->buffer = wlr_buffer_lock(state->buffer);
        if (previous)
            wlr_buffer_unlock(previous);
        mirror->src_box = state->buffer_src_box;
        mirror->dst_box = state->buffer_dst_box;
        mirror->width = event->output->width;
        mirror->height = event->output->height;
        mirror->transform = event->output->transform;
        const struct wlr_output_image_description *colours = event->output->image_description;
        mirror->transfer_function = colours ? colours->transfer_function : 0;
        mirror->primaries = colours ? colours->primaries : 0;
        // The lock is in the scene from the moment the session locks, so a frame committed
        // since shows it.
        mirror->shows_lock = mirror->output->server->locked;
        mirror->fresh = true;
    }
    wlr_output_schedule_frame(mirror->output->wlr_output);
}

static void source_destroy(struct wl_listener *listener, void *data) {
    struct sh_mirror *mirror = wl_container_of(listener, mirror, source_destroy);
    detach_source(mirror);
    wlr_output_schedule_frame(mirror->output->wlr_output);
}

/* Makes `output` show `source`'s picture, or another source's. The caller has taken it out of
 * the layout, and may have changed its mode or transform, so it draws again either way. */
void mirror_start(struct sh_output *output, struct sh_output *source) {
    struct sh_mirror *mirror = output->mirror;
    if (mirror && mirror->source == source) {
        mirror->fresh = true;
        wlr_output_schedule_frame(output->wlr_output);
        return;
    }
    if (!mirror) {
        mirror = calloc(1, sizeof(*mirror));
        if (!mirror)
            return;
        mirror->output = output;
        output->mirror = mirror;
    }
    detach_source(mirror);
    mirror->source = source;
    add_listener(&source->wlr_output->events.commit, &mirror->source_commit, source_commit);
    add_listener(&source->wlr_output->events.destroy, &mirror->source_destroy, source_destroy);
    mirror->fresh = true;
    // The source draws a whole frame for the mirror to start from.
    wlr_output_schedule_frame(source->wlr_output);
    struct wlr_scene_output *scene_output =
        wlr_scene_get_scene_output(source->server->scene, source->wlr_output);
    if (scene_output)
        wlr_damage_ring_add_whole(&scene_output->damage_ring);
    wlr_output_schedule_frame(output->wlr_output);
    wlr_log(WLR_INFO, "%s mirrors %s", output->wlr_output->name, source->wlr_output->name);
}

/* Ends `output`'s mirroring, as it joins the layout, turns off or goes away. */
void mirror_stop(struct sh_output *output) {
    struct sh_mirror *mirror = output->mirror;
    if (!mirror)
        return;
    detach_source(mirror);
    if (mirror->shown)
        wlr_buffer_unlock(mirror->shown);
    free(mirror);
    output->mirror = NULL;
    wlr_log(WLR_INFO, "%s mirrors nothing", output->wlr_output->name);
}

/* `source` goes away: the mirrors showing it let go of it at once. Their own listeners on its
 * destroy signal run after output_destroy, which arranges the outputs, and so asks what each
 * mirror shows, first. */
void mirrors_forget_source(struct sh_output *source) {
    struct sh_server *server = source->server;
    struct sh_output *output;
    for_each_connected_output(output, server) {
        if (output->mirror && output->mirror->source == source) {
            detach_source(output->mirror);
            wlr_output_schedule_frame(output->wlr_output);
        }
    }
}

/* The output a mirror shows, NULL for none (its source gone). */
struct sh_output *mirrored_output(const struct sh_output *output) {
    return output->mirror ? output->mirror->source : NULL;
}

/* Mirrors start or stop as their sources join or leave the layout, and as the settings in force
 * change: an output out of the layout as a mirror whose source left it joins the layout, and
 * one in it whose source is back leaves it again. arrange_outputs calls this last, and arranges
 * the outputs again when it changed something. */
bool refresh_mirrors(struct sh_server *server) {
    bool any = false;
    // Each change may change another's source, as one mirroring a mirror takes that one's.
    for (int pass = 0; pass < 8; ++pass) {
        bool changed = false;
        struct sh_output *output;
        for_each_connected_output(output, server) {
            if ((output->disabled && !output->mirror) || (output->powered_off && !output->mirror))
                continue; // turned off: configured again as it comes back
            // A mirror whose source went (mirrors_forget_source) shows nothing until then.
            if (mirror_source(server, output) != mirrored_output(output) ||
                (output->mirror && !mirrored_output(output))) {
                output->powered_off = false; // a mirror off with a source gone comes on
                configure_output(server, output); // may move it to the other list: leave
                changed = any = true;
                break;
            }
        }
        if (!changed)
            break;
    }
    return any;
}

/* A mirror follows its source's power (output_power.c): off while the source is, and on again
 * with it. */
void mirrors_follow_power(struct sh_output *source, bool on) {
    struct sh_server *server = source->server;
    struct sh_output *output;
    wl_list_for_each(output, &server->disabled_outputs, link) {
        if (mirrored_output(output) != source || output->powered_off == !on)
            continue;
        if (on) {
            output->powered_off = false;
            configure_output(server, output);
            continue;
        }
        struct wlr_output_state state;
        wlr_output_state_init(&state);
        wlr_output_state_set_enabled(&state, false);
        if (wlr_output_commit_state(output->wlr_output, &state))
            output->powered_off = true;
        wlr_output_state_finish(&state);
    }
}

/* Where a box of the source's picture, in its buffer's coordinates, goes on the mirror's: the
 * source's picture as one sees it, fitted to the mirror as one sees it and centred, in the
 * mirror buffer's coordinates. */
static struct wlr_box mirror_box(struct sh_mirror *mirror, struct wlr_box box, double *scale) {
    struct wlr_output *out = mirror->output->wlr_output;
    int width = mirror->width, height = mirror->height;
    wlr_box_transform(&box, &box, mirror->transform, width, height);
    wlr_output_transform_coords(mirror->transform, &width, &height);
    int mirror_width, mirror_height;
    wlr_output_transformed_resolution(out, &mirror_width, &mirror_height);
    double fit = fmin((double)mirror_width / width, (double)mirror_height / height);
    double x0 = (mirror_width - width * fit) / 2, y0 = (mirror_height - height * fit) / 2;
    struct wlr_box placed = {
        .x = (int)lround(x0 + box.x * fit),
        .y = (int)lround(y0 + box.y * fit),
    };
    placed.width = (int)lround(x0 + (box.x + box.width) * fit) - placed.x;
    placed.height = (int)lround(y0 + (box.y + box.height) * fit) - placed.y;
    wlr_box_transform(&placed, &placed, wlr_output_transform_invert(out->transform),
                      mirror_width, mirror_height);
    if (scale)
        *scale = fit;
    return placed;
}

/* The source's hardware cursor, which its frames do not hold: drawn onto the mirror where it
 * is on the source. False when there is none showing. */
static bool hardware_cursor(struct sh_mirror *mirror, struct wlr_box *box) {
    struct wlr_output *source = mirror->source ? mirror->source->wlr_output : NULL;
    struct wlr_output_cursor *cursor = source ? source->hardware_cursor : NULL;
    if (!cursor || !cursor->enabled || !cursor->visible || !cursor->texture)
        return false;
    // Its box on the source as one sees it, in the source buffer's coordinates as the frame's.
    int width, height;
    wlr_output_transformed_resolution(source, &width, &height);
    *box = (struct wlr_box){(int)cursor->x - cursor->hotspot_x, (int)cursor->y - cursor->hotspot_y,
                            (int)cursor->width, (int)cursor->height};
    wlr_box_transform(box, box, wlr_output_transform_invert(source->transform), width, height);
    return true;
}

/* Draws the mirror's frame from the source's newest one, and commits it; nothing when nothing
 * changed. While the session is locked, a frame the source committed before it locked is not
 * shown: the mirror stays black until the lock reaches it. */
void mirror_frame(struct sh_output *output) {
    struct sh_mirror *mirror = output->mirror;
    struct sh_server *server = output->server;
    struct wlr_output *out = output->wlr_output;
    struct wlr_box cursor_box = {0};
    bool cursor = mirror->buffer && hardware_cursor(mirror, &cursor_box);
    struct wlr_output_cursor *source_cursor = cursor ? mirror->source->wlr_output->hardware_cursor : NULL;
    if (cursor != mirror->cursor.shown ||
        (cursor && (source_cursor->x != mirror->cursor.x || source_cursor->y != mirror->cursor.y ||
                    source_cursor->texture != mirror->cursor.texture)))
        mirror->fresh = true;
    if (!mirror->fresh || !out->enabled)
        return;
    bool covered = server->locked && !mirror->shows_lock;
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    struct wlr_texture *texture = NULL;
    bool done = false;
    struct wlr_render_pass *pass = wlr_output_begin_render_pass(out, &state, NULL);
    if (!pass)
        goto out;
    wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
                                       .box = {0, 0, out->width, out->height},
                                       .color = {0, 0, 0, 1},
                                       .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
                                   });
    if (mirror->buffer && !covered)
        texture = wlr_texture_from_buffer(server->renderer, mirror->buffer);
    if (texture) {
        struct wlr_box placed = wlr_box_empty(&mirror->dst_box)
                                    ? (struct wlr_box){0, 0, mirror->width, mirror->height}
                                    : mirror->dst_box;
        // An HDR source's picture brought down to SDR as the scene brings an HDR window's, its
        // reference white to SDR's.
        struct wlr_color_primaries primaries = {0};
        if (mirror->primaries)
            wlr_color_primaries_from_named(&primaries, mirror->primaries);
        // wlroots' defaults: PQ's reference white is 203 cd/m² of 10000, SDR's 80 of 80.
        bool pq = mirror->transfer_function == WLR_COLOR_TRANSFER_FUNCTION_ST2084_PQ;
        float luminance = pq ? (80.0f / 203.0f) * (10000.0f / 80.0f) : 1.0f;
        wlr_render_pass_add_texture(
            pass, &(struct wlr_render_texture_options){
                      .texture = texture,
                      .src_box = mirror->src_box,
                      .dst_box = mirror_box(mirror, placed, NULL),
                      .transform = wlr_output_transform_compose(
                          wlr_output_transform_invert(mirror->transform), out->transform),
                      .filter_mode = WLR_SCALE_FILTER_BILINEAR,
                      .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
                      .transfer_function = mirror->transfer_function,
                      .primaries = mirror->primaries ? &primaries : NULL,
                      .luminance_multiplier = &luminance,
                  });
        if (cursor) {
            double scale;
            struct wlr_box box = mirror_box(mirror, cursor_box, &scale);
            wlr_render_pass_add_texture(
                pass, &(struct wlr_render_texture_options){
                          .texture = source_cursor->texture,
                          .src_box = source_cursor->src_box,
                          .dst_box = box,
                          .transform = wlr_output_transform_compose(
                              wlr_output_transform_invert(source_cursor->transform),
                              out->transform),
                          .filter_mode = WLR_SCALE_FILTER_BILINEAR,
                      });
        }
    }
    if (!wlr_render_pass_submit(pass))
        goto out;
    done = wlr_output_commit_state(out, &state);
    if (done) {
        mirror->fresh = false;
        mirror->cursor.shown = cursor && texture;
        if (mirror->cursor.shown) {
            mirror->cursor.x = source_cursor->x;
            mirror->cursor.y = source_cursor->y;
            mirror->cursor.texture = source_cursor->texture;
        }
        if (mirror->shown)
            wlr_buffer_unlock(mirror->shown);
        mirror->shown = wlr_buffer_lock(state.buffer);
        // A mirror holds the lock up as the outputs in the layout do (lock.c).
        if (server->locked && !covered)
            lock_output_presented(output);
        else
            output->lock_presented = false;
    }
out:
    if (texture)
        wlr_texture_destroy(texture);
    wlr_output_state_finish(&state);
    if (!done)
        wlr_log(WLR_ERROR, "Cannot draw the mirror on %s", out->name);
}

/* Writes the mirror's last frame to `path` as a binary PPM, for tests: the mirror has no
 * wl_output for a screenshot tool to name. */
bool mirror_capture(struct sh_output *output, const char *path, char *error, size_t error_size) {
    struct sh_mirror *mirror = output->mirror;
    if (!mirror || !mirror->shown) {
        snprintf(error, error_size, "%s shows no mirror", output->wlr_output->name);
        return false;
    }
    struct wlr_texture *texture = wlr_texture_from_buffer(output->server->renderer, mirror->shown);
    if (!texture) {
        snprintf(error, error_size, "cannot read the mirror's frame");
        return false;
    }
    uint32_t width = texture->width, height = texture->height;
    uint32_t *pixels = malloc((size_t)width * height * 4);
    bool done = pixels && wlr_texture_read_pixels(
                              texture, &(struct wlr_texture_read_pixels_options){
                                           .data = pixels,
                                           .format = DRM_FORMAT_XRGB8888,
                                           .stride = width * 4,
                                       });
    wlr_texture_destroy(texture);
    FILE *file = done ? fopen(path, "wb") : NULL;
    if (file) {
        fprintf(file, "P6\n%u %u\n255\n", width, height);
        for (size_t i = 0; i < (size_t)width * height; ++i) {
            unsigned char rgb[3] = {pixels[i] >> 16 & 0xff, pixels[i] >> 8 & 0xff, pixels[i] & 0xff};
            fwrite(rgb, 1, 3, file);
        }
        done = fclose(file) == 0;
    } else {
        done = false;
    }
    free(pixels);
    if (!done)
        snprintf(error, error_size, "cannot write %s", path);
    return done;
}
