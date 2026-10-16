/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The colour picker (pick_color): while it picks, the cursor is a loupe (src/color_picker.c) with
 * the pixels around the pointer magnified, read from a picture of what the output under it shows,
 * drawn on the renderer without the cursor, night light or a client's gamma table, and drawn again
 * while the output draws. A click, Return or Space picks the pixel under the crosshair: it goes to
 * the clipboard as #rrggbb, to the subscribers as `color-picked #rrggbb`, which the shell shows on
 * its on-screen display, and to each `paw msg pick_color` waiting for it. Escape or a right click
 * cancels; the arrows move the pointer a pixel. `get color_picker`. */
#include "server.h"
#include "paw/color_picker.h"
#include <drm_fourcc.h>
#include <wlr/util/transform.h>

/* How often the picture is drawn again while its output draws frames, how often at most the
 * loupe follows the pointer (a mouse may move a thousand times a second), and how many `paw msg
 * pick_color` may wait at once. */
#define REFRESH_MS 100
#define FOLLOW_MS 8
#define WAITING_MAX 16

struct sh_color_picker {
    bool active;
    /* The output the picture is of, the swapchain it is drawn in, the picture and its texture,
     * whether it holds an HDR monitor's colours, and the output's frames as it was drawn. */
    struct sh_output *output;
    struct wlr_swapchain *swapchain;
    struct wlr_buffer *picture;
    struct wlr_texture *texture;
    bool hdr;
    uint64_t drawn_at;
    unsigned pictures; /* drawn since the compositor started, for `get color_picker` */
    struct wl_event_source *timer;
    /* What the loupe shows: the cells around the pixel under the crosshair, that pixel's colour
     * (none off every output) and where it is on its output, in its pixels as they are seen. */
    uint32_t cells[SH_LOUPE_CELLS * SH_LOUPE_CELLS];
    uint32_t color;
    bool has_color;
    int pixel_x, pixel_y;
    /* The loupe's layout and frame (NULL until painted) for each side of the crosshair the
     * card may be on (left, then above, as bits), at a scale and in a look; the side shown, and
     * whether the cursor shows the loupe of the cells as they are. */
    struct {
        struct sh_loupe_layout layout;
        uint32_t *frame;
    } sides[4];
    double scale;
    struct sh_loupe_look look;
    int side;
    bool shown;
    /* When it last followed the pointer, and whether it is behind it, which `catch_up` sees to. */
    int64_t followed_at;
    bool behind;
    struct wl_event_source *catch_up;
    /* Buttons whose press it took: it takes their release too, after it ends as well. */
    uint32_t pressed;
    /* `paw msg pick_color` connections waiting for the colour. */
    int waiting[WAITING_MAX];
    int waiting_count;
    char last[8]; /* the colour picked last, "" for none */
};

static struct sh_color_picker *picker_of(struct sh_server *server) {
    if (!server->color_picker)
        server->color_picker = calloc(1, sizeof(*server->color_picker));
    return server->color_picker;
}

bool color_picker_active(struct sh_server *server) {
    return server->color_picker && server->color_picker->active;
}

static void drop_picture(struct sh_color_picker *picker) {
    if (picker->texture)
        wlr_texture_destroy(picker->texture);
    if (picker->picture)
        wlr_buffer_unlock(picker->picture);
    picker->texture = NULL;
    picker->picture = NULL;
}

static void drop_output(struct sh_color_picker *picker) {
    drop_picture(picker);
    if (picker->swapchain)
        wlr_swapchain_destroy(picker->swapchain);
    picker->swapchain = NULL;
    picker->output = NULL;
}

/* Draws what the scene shows on `output` into a buffer of the picker's, as its next frame would be
 * drawn but for the cursor, night light (which the output's frames apply), a gamma table a client
 * set (wlr-gamma-control) and direct scan-out: the colours the applications drew, as the loupe
 * reads them. An HDR monitor's picture is in its own colours. False when it cannot be drawn. */
static bool draw_picture(struct sh_server *server, struct sh_output *output) {
    struct sh_color_picker *picker = server->color_picker;
    struct wlr_output *wlr_output = output->wlr_output;
    struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(server->scene, wlr_output);
    if (picker->output != output)
        drop_output(picker);
    picker->output = output;
    if (!scene_output || !wlr_output->enabled ||
        !wlr_output_configure_primary_swapchain(wlr_output, NULL, &picker->swapchain)) {
        drop_picture(picker);
        return false;
    }
    // Software cursors, the loupe among them, are left out by a flag set back before any frame
    // of the output could see it, which damages nothing.
    size_t count = (size_t)wl_list_length(&wlr_output->cursors), hidden_count = 0;
    struct wlr_output_cursor *cursor,
        **hidden = count ? calloc(count, sizeof(*hidden)) : NULL;
    wl_list_for_each(cursor, &wlr_output->cursors, link) {
        if (hidden && cursor->enabled && cursor != wlr_output->hardware_cursor) {
            cursor->enabled = false;
            hidden[hidden_count++] = cursor;
        }
    }
    wlr_output_lock_attach_render(wlr_output, true);
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    // A client's gamma table is set aside for this drawing, as crossfade.c does for its pictures;
    // an SDR output's colours are encoded as its frames are without night light, an HDR one's in
    // its own image description.
    bool gamma_changed = scene_output->WLR_PRIVATE.gamma_lut_changed;
    struct wlr_color_transform *gamma = scene_output->WLR_PRIVATE.gamma_lut_color_transform;
    scene_output->WLR_PRIVATE.gamma_lut_changed = false;
    scene_output->WLR_PRIVATE.gamma_lut_color_transform = NULL;
    struct wlr_color_transform *encoding = NULL;
    if (wlr_output->image_description)
        wlr_output_state_set_image_description(&state, wlr_output->image_description);
    else
        encoding = wlr_color_transform_init_linear_to_inverse_eotf(
            WLR_COLOR_TRANSFER_FUNCTION_GAMMA22);
    struct wlr_buffer *picture = NULL;
    if (wlr_scene_output_build_state(scene_output, &state,
                                     &(struct wlr_scene_output_state_options){
                                         .swapchain = picker->swapchain,
                                         .color_transform = encoding}) &&
        (state.committed & WLR_OUTPUT_STATE_BUFFER) && state.buffer)
        picture = wlr_buffer_lock(state.buffer);
    wlr_output_state_finish(&state);
    if (encoding)
        wlr_color_transform_unref(encoding);
    scene_output->WLR_PRIVATE.gamma_lut_changed = gamma_changed;
    scene_output->WLR_PRIVATE.gamma_lut_color_transform = gamma;
    wlr_output_lock_attach_render(wlr_output, false);
    while (hidden_count > 0)
        hidden[--hidden_count]->enabled = true;
    free(hidden);
    drop_picture(picker);
    if (!picture)
        return false;
    picker->picture = picture;
    picker->texture = wlr_texture_from_buffer(server->renderer, picture);
    picker->hdr = output_is_hdr(output);
    picker->drawn_at = output->frames;
    ++picker->pictures;
    return picker->texture != NULL;
}

/* Reads the cells around the pixel under the pointer from the picture, as they are seen on the
 * output: its pixels are the output's frame, turned as the output is. True when they changed. */
static bool read_cells(struct sh_server *server) {
    struct sh_color_picker *picker = server->color_picker;
    uint32_t cells[SH_LOUPE_CELLS * SH_LOUPE_CELLS] = {0};
    bool had = picker->has_color;
    int was_x = picker->pixel_x, was_y = picker->pixel_y;
    picker->has_color = false;
    struct sh_output *output = picker->output;
    struct wlr_box box = {0};
    if (output)
        wlr_output_layout_get_box(server->output_layout, output->wlr_output, &box);
    if (output && picker->texture && !wlr_box_empty(&box)) {
        struct wlr_output *wlr_output = output->wlr_output;
        int width, height;
        wlr_output_transformed_resolution(wlr_output, &width, &height);
        double scale = wlr_output->scale;
        int x = (int)floor((server->cursor->x - box.x) * scale);
        int y = (int)floor((server->cursor->y - box.y) * scale);
        x = x < 0 ? 0 : x >= width ? width - 1 : x;
        y = y < 0 ? 0 : y >= height ? height - 1 : y;
        picker->pixel_x = x;
        picker->pixel_y = y;
        const int half = SH_LOUPE_CELLS / 2;
        struct wlr_box around = {x - half, y - half, SH_LOUPE_CELLS, SH_LOUPE_CELLS}, seen;
        struct wlr_box screen = {0, 0, width, height};
        enum wl_output_transform turn = wlr_output_transform_invert(wlr_output->transform);
        uint32_t region[SH_LOUPE_CELLS * SH_LOUPE_CELLS];
        struct wlr_box drawn;
        if (wlr_box_intersection(&seen, &around, &screen)) {
            // The same pixels in the picture, where the scene drew them (as wlr_scene does).
            wlr_box_transform(&drawn, &seen, turn, width, height);
            bool read = wlr_texture_read_pixels(
                picker->texture, &(struct wlr_texture_read_pixels_options){
                                     .data = region,
                                     .format = DRM_FORMAT_XRGB8888,
                                     .stride = (uint32_t)drawn.width * 4,
                                     .src_box = drawn,
                                 });
            for (int row = 0; read && row < SH_LOUPE_CELLS; ++row) {
                for (int column = 0; column < SH_LOUPE_CELLS; ++column) {
                    struct wlr_box one = {around.x + column, around.y + row, 1, 1};
                    if (!wlr_box_contains_box(&seen, &one))
                        continue;
                    wlr_box_transform(&one, &one, turn, width, height);
                    uint32_t rgb = region[(one.y - drawn.y) * drawn.width + one.x - drawn.x];
                    rgb &= 0xffffff;
                    cells[row * SH_LOUPE_CELLS + column] =
                        0xff000000u | (picker->hdr ? sh_color_from_pq(rgb) : rgb);
                }
            }
            if (!read)
                wlr_log(WLR_ERROR, "Colour picker: cannot read %s's picture", wlr_output->name);
        }
        uint32_t middle = cells[half * SH_LOUPE_CELLS + half];
        picker->has_color = middle != 0;
        picker->color = middle & 0xffffff;
    }
    bool changed = memcmp(cells, picker->cells, sizeof(cells)) || had != picker->has_color ||
                   was_x != picker->pixel_x || was_y != picker->pixel_y;
    memcpy(picker->cells, cells, sizeof(cells));
    return changed;
}

static uint32_t rgb_of(const float color[4]) {
    uint32_t rgb = 0;
    for (int i = 0; i < 3; ++i) {
        float channel = color[i] < 0 ? 0 : color[i] > 1 ? 1 : color[i];
        rgb = rgb << 8 | (uint32_t)lroundf(channel * 255);
    }
    return rgb;
}

/* The shell's colours, which the loupe is drawn in. */
static struct sh_loupe_look loupe_look(struct sh_server *server) {
    const struct sh_settings *settings = server_settings(server);
    struct sh_loupe_look look = {.surface = rgb_of(settings->shell_surface),
                                 .text = rgb_of(settings->shell_text),
                                 .accent = rgb_of(settings->shell_accent),
                                 .text_alpha = settings->shell_text[3]};
    uint32_t s = look.surface;
    look.light = 0.2126 * (s >> 16 & 0xff) + 0.7152 * (s >> 8 & 0xff) + 0.0722 * (s & 0xff) > 128;
    return look;
}

static bool same_look(const struct sh_loupe_look *a, const struct sh_loupe_look *b) {
    return a->surface == b->surface && a->text == b->text && a->accent == b->accent &&
           a->text_alpha == b->text_alpha && a->light == b->light;
}

static void forget_frames(struct sh_color_picker *picker) {
    for (int i = 0; i < 4; ++i) {
        free(picker->sides[i].frame);
        picker->sides[i].frame = NULL;
    }
}

/* The cursor shows the loupe of the cells as they are, the card on the side of the crosshair
 * where it fits on the output. */
static void show_loupe(struct sh_server *server) {
    struct sh_color_picker *picker = server->color_picker;
    struct wlr_output *wlr_output = picker->output ? picker->output->wlr_output : NULL;
    double scale = wlr_output ? wlr_output->scale : 1;
    struct wlr_box box = {0};
    if (wlr_output)
        wlr_output_layout_get_box(server->output_layout, wlr_output, &box);
    double card_width, card_height;
    sh_loupe_card_size(scale, &card_width, &card_height);
    bool left = !wlr_box_empty(&box) &&
                server->cursor->x + SH_LOUPE_OFFSET + card_width > box.x + box.width;
    bool above = !wlr_box_empty(&box) &&
                 server->cursor->y + SH_LOUPE_OFFSET + card_height > box.y + box.height;
    struct sh_loupe_look look = loupe_look(server);
    if (scale != picker->scale || !same_look(&look, &picker->look)) {
        forget_frames(picker);
        picker->scale = scale;
        picker->look = look;
    }
    picker->side = (left ? 1 : 0) | (above ? 2 : 0);
    const struct sh_loupe_layout *layout = &picker->sides[picker->side].layout;
    uint32_t **frame = &picker->sides[picker->side].frame;
    if (!*frame) {
        sh_loupe_layout(scale, left, above, &picker->sides[picker->side].layout);
        *frame = malloc((size_t)layout->width * layout->height * 4);
        if (!*frame)
            return;
        sh_loupe_paint_frame(*frame, layout, &picker->look);
    }
    size_t size = (size_t)layout->width * layout->height * 4;
    uint32_t *pixels = malloc(size);
    if (!pixels)
        return;
    memcpy(pixels, *frame, size);
    sh_loupe_paint_cells(pixels, layout, &picker->look, picker->cells);
    struct wlr_buffer *buffer = sh_pixel_buffer(pixels, layout->width, layout->height);
    if (!buffer)
        return;
    wlr_cursor_set_buffer(server->cursor, buffer, layout->hot_x, layout->hot_y, (float)scale);
    wlr_buffer_drop(buffer);
    server->cursor_image = "crosshair";
    picker->shown = true;
}

/* The pointer moved, or the screen under it changed: the picture of the output it is on, the
 * cells under it read again, and the loupe shown anew where they changed. */
static void follow(struct sh_server *server, bool draw) {
    struct sh_color_picker *picker = server->color_picker;
    picker->followed_at = now_ms();
    picker->behind = false;
    struct wlr_output *under = pointer_output(server);
    struct sh_output *output = under ? sh_output_for(server, under) : NULL;
    if (output && (draw || output != picker->output || !picker->picture) &&
        !draw_picture(server, output))
        wlr_log(WLR_ERROR, "Colour picker: cannot draw a picture of %s", under->name);
    if (!output)
        drop_output(picker);
    if (read_cells(server) || !picker->shown)
        show_loupe(server);
}

static int catch_up(void *data) {
    struct sh_server *server = data;
    struct sh_color_picker *picker = server->color_picker;
    if (picker && picker->active && picker->behind)
        follow(server, false);
    return 0;
}

/* Follows the pointer now, or once FOLLOW_MS have passed since it last did. */
static void follow_soon(struct sh_server *server) {
    struct sh_color_picker *picker = server->color_picker;
    int64_t since = now_ms() - picker->followed_at;
    if (since >= FOLLOW_MS || !picker->catch_up) {
        follow(server, false);
    } else if (!picker->behind) {
        picker->behind = true;
        wl_event_source_timer_update(picker->catch_up, (int)(FOLLOW_MS - since));
    }
}

static int refresh(void *data) {
    struct sh_server *server = data;
    struct sh_color_picker *picker = server->color_picker;
    if (!picker || !picker->active)
        return 0;
    // Something drew on the output since the picture was drawn: perhaps under the loupe.
    if (picker->output && picker->output->frames != picker->drawn_at)
        follow(server, true);
    wl_event_source_timer_update(picker->timer, REFRESH_MS);
    return 0;
}

/* Answers every `paw msg pick_color` waiting with `text`. */
static void answer(struct sh_color_picker *picker, const char *text) {
    for (int i = 0; i < picker->waiting_count; ++i) {
        control_reply(picker->waiting[i], text);
        close(picker->waiting[i]);
    }
    picker->waiting_count = 0;
}

/* Stops picking: the cursor goes back to what is under the pointer. */
static void end(struct sh_server *server) {
    struct sh_color_picker *picker = server->color_picker;
    picker->active = false;
    picker->shown = false;
    if (picker->timer)
        wl_event_source_timer_update(picker->timer, 0);
    if (picker->catch_up)
        wl_event_source_timer_update(picker->catch_up, 0);
    picker->behind = false;
    drop_output(picker);
    forget_frames(picker);
    set_default_cursor(server);
    process_cursor_motion(server, (uint32_t)now_ms());
}

static void cancel(struct sh_server *server) {
    answer(server->color_picker, "error: cancelled\n");
    wlr_log(WLR_INFO, "Colour picker: cancelled");
    end(server);
}

/* The source of the clipboard's text: a colour as #rrggbb. */
struct text_source {
    struct wlr_data_source base;
    char text[8];
};

static void text_send(struct wlr_data_source *wlr_source, const char *mime_type, int32_t fd) {
    struct text_source *source = wl_container_of(wlr_source, source, base);
    // A pipe's reader may be gone: the write fails rather than SIGPIPE ending the compositor.
    sigset_t pipe, old;
    sigemptyset(&pipe);
    sigaddset(&pipe, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &pipe, &old);
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    ssize_t written = write(fd, source->text, strlen(source->text));
    if (written < 0 && errno == EPIPE)
        sigtimedwait(&pipe, NULL, &(struct timespec){0});
    pthread_sigmask(SIG_SETMASK, &old, NULL);
    close(fd);
}

static void text_destroy(struct wlr_data_source *wlr_source) {
    struct text_source *source = wl_container_of(wlr_source, source, base);
    free(source);
}

static const struct wlr_data_source_impl text_source_impl = {
    .send = text_send,
    .destroy = text_destroy,
};

/* Puts `text` on the clipboard, as an application copying it would. */
static void copy_text(struct sh_server *server, const char *text) {
    struct text_source *source = calloc(1, sizeof(*source));
    if (!source)
        return;
    wlr_data_source_init(&source->base, &text_source_impl);
    snprintf(source->text, sizeof(source->text), "%s", text);
    static const char *const types[] = {"text/plain;charset=utf-8", "text/plain", "UTF8_STRING",
                                        "TEXT", "STRING"};
    for (size_t i = 0; i < sizeof(types) / sizeof(*types); ++i) {
        char **type = wl_array_add(&source->base.mime_types, sizeof(*type));
        if (type)
            *type = strdup(types[i]);
        if (type && !*type)
            source->base.mime_types.size -= sizeof(*type);
    }
    wlr_seat_set_selection(server->seat, &source->base,
                           wl_display_next_serial(server->wl_display));
}

/* Picks the pixel under the crosshair as the screen shows it now, and stops. */
static void pick(struct sh_server *server) {
    struct sh_color_picker *picker = server->color_picker;
    follow(server, true);
    if (!picker->has_color) {
        cancel(server);
        return;
    }
    char hex[8], line[64];
    sh_color_hex(picker->color, hex);
    snprintf(picker->last, sizeof(picker->last), "%s", hex);
    copy_text(server, hex);
    snprintf(line, sizeof(line), "color-picked %s\n", hex);
    send_event(server, line, strlen(line));
    snprintf(line, sizeof(line), "ok\n%s\n", hex);
    answer(picker, line);
    wlr_log(WLR_INFO, "Colour picker: picked %s on %s at %d,%d", hex,
            picker->output ? picker->output->wlr_output->name : "-", picker->pixel_x,
            picker->pixel_y);
    end(server);
}

/* Starts picking, or goes on with the picking under way. False, saying why in `error`, when it
 * cannot start: under the lock, while the pointer moves something, or off every output. */
static bool start(struct sh_server *server, char *error, size_t size) {
    if (server->locked) {
        snprintf(error, size, "the session is locked");
        return false;
    }
    struct sh_color_picker *picker = picker_of(server);
    if (!picker) {
        snprintf(error, size, "out of memory");
        return false;
    }
    if (picker->active)
        return true;
    if (server->cursor_mode != SH_CURSOR_PASSTHROUGH || server->seat->drag) {
        snprintf(error, size, "the pointer is moving something");
        return false;
    }
    struct wlr_output *under = pointer_output(server);
    struct sh_output *output = under ? sh_output_for(server, under) : NULL;
    if (!output || !draw_picture(server, output)) {
        drop_output(picker);
        snprintf(error, size, output ? "cannot read the screen" : "no monitor under the pointer");
        return false;
    }
    if (!picker->timer)
        picker->timer = wl_event_loop_add_timer(wl_display_get_event_loop(server->wl_display),
                                                refresh, server);
    if (picker->timer)
        wl_event_source_timer_update(picker->timer, REFRESH_MS);
    if (!picker->catch_up)
        picker->catch_up = wl_event_loop_add_timer(
            wl_display_get_event_loop(server->wl_display), catch_up, server);
    picker->active = true;
    picker->shown = false;
    // The pointer is the picker's, not the window's under it.
    wlr_seat_pointer_clear_focus(server->seat);
    follow(server, false);
    wlr_log(WLR_INFO, "Colour picker: picking on %s", under->name);
    return true;
}

/* The pick_color action: starts picking, or cancels the picking under way. */
void color_picker_toggle(struct sh_server *server) {
    if (color_picker_active(server)) {
        cancel(server);
        return;
    }
    char error[128];
    if (!start(server, error, sizeof(error)))
        wlr_log(WLR_ERROR, "Colour picker: %s", error);
}

/* `paw msg pick_color`: starts picking, or joins the picking under way, and answers once a colour
 * is picked ("ok" and #rrggbb) or the picking is cancelled ("error: cancelled"), on a copy of the
 * connection's descriptor, which the control socket closes as it would. False, saying why in
 * `error`, when it cannot start, which the caller answers at once. */
bool color_picker_wait(struct sh_server *server, int fd, char *error, size_t size) {
    if (!start(server, error, size))
        return false;
    struct sh_color_picker *picker = server->color_picker;
    int copy = picker->waiting_count < WAITING_MAX ? fcntl(fd, F_DUPFD_CLOEXEC, 0) : -1;
    if (copy < 0) {
        snprintf(error, size, "too many waiting for a colour");
        return false;
    }
    picker->waiting[picker->waiting_count++] = copy;
    return true;
}

/* While picking the pointer's moves are the picker's. */
bool color_picker_motion(struct sh_server *server) {
    if (!color_picker_active(server))
        return false;
    zoom_moved(server);
    follow_soon(server);
    return true;
}

/* While picking a left press picks and a right one cancels; the picker takes the release of
 * every press it took, also once it has stopped. */
bool color_picker_button(struct sh_server *server, const struct wlr_pointer_button_event *event) {
    struct sh_color_picker *picker = server->color_picker;
    if (!picker)
        return false;
    uint32_t bit = event->button >= BTN_MOUSE && event->button < BTN_MOUSE + 32
                       ? 1u << (event->button - BTN_MOUSE)
                       : 0;
    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        if (!(picker->pressed & bit))
            return false;
        picker->pressed &= ~bit;
        return true;
    }
    if (!picker->active)
        return false;
    picker->pressed |= bit;
    if (event->button == BTN_LEFT)
        pick(server);
    else if (event->button == BTN_RIGHT)
        cancel(server);
    return true;
}

/* While picking the wheel scrolls nothing. */
bool color_picker_axis(struct sh_server *server) { return color_picker_active(server); }

/* The pointer a pixel of its output across or down (`dx`, `dy`), onto that pixel's middle. */
static void nudge(struct sh_server *server, int dx, int dy) {
    struct sh_color_picker *picker = server->color_picker;
    if (!picker->output)
        return;
    struct wlr_output *wlr_output = picker->output->wlr_output;
    struct wlr_box box;
    wlr_output_layout_get_box(server->output_layout, wlr_output, &box);
    double scale = wlr_output->scale;
    double x = box.x + (picker->pixel_x + dx + 0.5) / scale;
    double y = box.y + (picker->pixel_y + dy + 0.5) / scale;
    wlr_cursor_warp_closest(server->cursor, NULL, x, y);
    process_cursor_motion(server, (uint32_t)now_ms());
}

/* Keys while picking: Escape cancels; Return, the keypad's Enter and Space pick; the arrows move
 * the pointer a pixel, with Shift ten. True for a key it took, also any bound to nothing, which
 * the windows do not get meanwhile; false for a binding, which runs as usual (pick_color's own
 * cancels). */
bool color_picker_key(struct sh_server *server, uint32_t modifiers, xkb_keysym_t sym) {
    if (!color_picker_active(server))
        return false;
    bool plain = !(modifiers & (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO));
    int step = modifiers & WLR_MODIFIER_SHIFT ? 10 : 1;
    switch (plain ? sym : XKB_KEY_NoSymbol) {
    case XKB_KEY_Escape:
        cancel(server);
        return true;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
    case XKB_KEY_space:
        pick(server);
        return true;
    case XKB_KEY_Left:
        nudge(server, -step, 0);
        return true;
    case XKB_KEY_Right:
        nudge(server, step, 0);
        return true;
    case XKB_KEY_Up:
        nudge(server, 0, -step);
        return true;
    case XKB_KEY_Down:
        nudge(server, 0, step);
        return true;
    default:
        break;
    }
    int argument = 0;
    return server->callbacks->key(server->callbacks->userdata, modifiers, sym, &argument) ==
           SH_NONE;
}

/* The session locks, or the picker's output goes away mid-pick: it cancels. */
void color_picker_cancel(struct sh_server *server) {
    if (color_picker_active(server))
        cancel(server);
}

/* `output` goes away: the picture of it with it. */
void color_picker_forget_output(struct sh_output *output) {
    struct sh_color_picker *picker = output->server->color_picker;
    if (picker && picker->output == output)
        drop_output(picker);
}

/* As the compositor stops: those waiting hear it stopped, without a colour. */
void color_picker_finish(struct sh_server *server) {
    struct sh_color_picker *picker = server->color_picker;
    if (!picker)
        return;
    answer(picker, "error: the compositor is stopping\n");
    if (picker->timer)
        wl_event_source_remove(picker->timer);
    if (picker->catch_up)
        wl_event_source_remove(picker->catch_up);
    drop_output(picker);
    forget_frames(picker);
    free(picker);
    server->color_picker = NULL;
}

/* `get color_picker`: "picking" or "idle"; while picking, the colour under the crosshair
 * (#rrggbb, "-" off every output), the output and the pixel there (x and y in its pixels as they
 * are seen), else "-", "-", -1, -1; the colour picked last ("-" for none); the loupe's size in
 * pixels and its scale; which side of the crosshair the card is on ("right" or "left", then
 * "below" or "above"); and how many pictures of the screen have been drawn. */
void describe_color_picker(struct sh_server *server, int fd, const char *arguments) {
    struct sh_color_picker *picker = server->color_picker;
    bool active = picker && picker->active;
    char hex[8] = "-", line[256];
    if (active && picker->has_color)
        sh_color_hex(picker->color, hex);
    const struct sh_loupe_layout *layout = picker ? &picker->sides[picker->side].layout : NULL;
    snprintf(line, sizeof(line), "ok\n%s\t%s\t%s\t%d\t%d\t%s\t%d\t%d\t%g\t%s\t%s\t%u\n",
             active ? "picking" : "idle", hex,
             active && picker->output ? picker->output->wlr_output->name : "-",
             active ? picker->pixel_x : -1, active ? picker->pixel_y : -1,
             picker && picker->last[0] ? picker->last : "-",
             active && layout ? layout->width : 0, active && layout ? layout->height : 0,
             active && layout ? layout->scale : 0,
             active && layout && layout->left ? "left" : "right",
             active && layout && layout->above ? "above" : "below",
             picker ? picker->pictures : 0);
    control_reply(fd, line);
}
