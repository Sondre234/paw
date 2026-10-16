/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Shaking the pointer to find it (mouse.shake_to_find), as macOS does: a shake (src/shake.c)
 * grows the cursor to three times its size for a moment. Its image, taken from the cursor theme
 * at that size, is given as a buffer at a scale that draws it at the size of the moment, stepped
 * on a timer of its own; meanwhile what would set the cursor's image only notes it
 * (`cursor_grown`), and as the cursor is back to its size the image it would have shows. */
#include "paw/decoration.h"
#include "paw/shake.h"
#include "server.h"
#include <math.h>
#include <string.h>

#define STEP_MS 16

struct sh_cursor_shake {
    struct sh_server *server;
    struct sh_shake shake;
    struct wl_event_source *timer;
    bool drawn;           /* the cursor is drawn grown */
    int size;             /* the cursor's size (profile_themes.c) */
    float theme_scale;    /* the outputs' largest scale, which `large` is loaded at */
    struct wlr_xcursor_manager *large; /* the theme at SH_SHAKE_GROW times the size */
    /* The image drawn: its name, its buffer, the size of its image at the theme's size and
     * scale 1, and the scale it was given at last. */
    char name[64];
    struct wlr_buffer *buffer;
    int width, height, hotspot_x, hotspot_y;
    double base_width;
    double scale;
};

static struct sh_cursor_shake *shake_of(struct sh_server *server) {
    if (!server->cursor_shake) {
        server->cursor_shake = calloc(1, sizeof(*server->cursor_shake));
        if (!server->cursor_shake)
            return NULL;
        server->cursor_shake->server = server;
        server->cursor_shake->size = cursor_theme_size(server);
    }
    return server->cursor_shake;
}

/* Whether the cursor is drawn grown now, so that its image is the shake's to give. */
bool cursor_grown(struct sh_server *server) {
    return server->cursor_shake && server->cursor_shake->drawn;
}

/* The size the cursor is drawn at, in logical pixels (get cursor_size). */
int cursor_drawn_size(struct sh_server *server) {
    struct sh_cursor_shake *shake = shake_of(server);
    if (!shake)
        return 24;
    double scale = shake->drawn ? shake->scale : 1;
    return (int)lround(shake->size * scale);
}

static float largest_output_scale(struct sh_server *server) {
    float scale = 1;
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        if (output->wlr_output->scale > scale)
            scale = output->wlr_output->scale;
    }
    return scale;
}

/* The xcursor named `name` in `manager` at `scale`, else its default arrow. */
static struct wlr_xcursor_image *theme_image(struct wlr_xcursor_manager *manager,
                                             const char *name, float scale) {
    if (!wlr_xcursor_manager_load(manager, scale))
        return NULL;
    struct wlr_xcursor *xcursor = wlr_xcursor_manager_get_xcursor(manager, name, scale);
    if (!xcursor || !xcursor->image_count)
        xcursor = wlr_xcursor_manager_get_xcursor(manager, "default", scale);
    return xcursor && xcursor->image_count ? xcursor->images[0] : NULL;
}

/* The image to grow: the xcursor the cursor would show, or for a client's own surface the
 * default arrow. False when the theme has none. */
static bool load_image(struct sh_cursor_shake *shake) {
    struct sh_server *server = shake->server;
    const char *image = server->cursor_image;
    const char *name = image && strcmp(image, "client") ? image : "default";
    float scale = largest_output_scale(server);
    if (shake->buffer && !strcmp(shake->name, name) && shake->theme_scale == scale)
        return true;
    if (!shake->large) {
        shake->large = wlr_xcursor_manager_create(cursor_theme_name(server),
                                                  (uint32_t)lround(shake->size * SH_SHAKE_GROW));
        if (!shake->large)
            return false;
    }
    struct wlr_xcursor_image *large = theme_image(shake->large, name, scale);
    struct wlr_xcursor_image *base = theme_image(server->cursor_mgr, name, 1);
    if (!large || !base || !large->width || !large->height)
        return false;
    uint32_t *pixels = malloc((size_t)large->width * large->height * sizeof(*pixels));
    if (!pixels)
        return false;
    memcpy(pixels, large->buffer, (size_t)large->width * large->height * sizeof(*pixels));
    struct wlr_buffer *buffer = sh_pixel_buffer(pixels, (int)large->width, (int)large->height);
    if (!buffer)
        return false;
    wlr_buffer_drop(shake->buffer);
    shake->buffer = buffer;
    snprintf(shake->name, sizeof(shake->name), "%s", name);
    shake->theme_scale = scale;
    shake->width = (int)large->width, shake->height = (int)large->height;
    shake->hotspot_x = (int)large->hotspot_x, shake->hotspot_y = (int)large->hotspot_y;
    shake->base_width = base->width;
    shake->scale = 0; // given anew
    return true;
}

/* Draws the cursor at `scale` times its size. */
static bool draw(struct sh_cursor_shake *shake, double scale) {
    if (!load_image(shake))
        return false;
    if (shake->drawn && fabs(scale - shake->scale) < 0.01)
        return true;
    // The buffer's pixels for each logical pixel of the cursor at this size.
    double density = shake->width / (shake->base_width * scale);
    wlr_cursor_set_buffer(shake->server->cursor, shake->buffer,
                          (int32_t)lround(shake->hotspot_x / density),
                          (int32_t)lround(shake->hotspot_y / density), (float)density);
    shake->drawn = true;
    shake->scale = scale;
    return true;
}

/* The cursor back to its size: the image it would have shows, a client's own surface given again
 * as the pointer enters it anew. */
static void restore(struct sh_cursor_shake *shake) {
    struct sh_server *server = shake->server;
    if (shake->timer)
        wl_event_source_timer_update(shake->timer, 0);
    if (!shake->drawn)
        return;
    shake->drawn = false;
    if (server->cursor_hidden)
        return; // cursor_show gives it its image
    const char *image = server->cursor_image;
    if (image && strcmp(image, "client")) {
        show_xcursor(server, image);
        return;
    }
    set_default_cursor(server);
    wlr_seat_pointer_notify_clear_focus(server->seat);
    process_cursor_motion(server, (uint32_t)now_ms());
}

static int step(void *data) {
    struct sh_cursor_shake *shake = data;
    int64_t now = now_ms();
    if (shake->server->cursor_hidden || !sh_shake_grown(&shake->shake, now) ||
        !draw(shake, sh_shake_scale(&shake->shake, now))) {
        restore(shake);
        return 0;
    }
    wl_event_source_timer_update(shake->timer, STEP_MS);
    return 0;
}

/* The pointer moved: a shake grows the cursor, and shaking on keeps it grown. Not while a button
 * is held (a drag, a drawing), the pointer moves a window or is held by an application, nor while
 * the cursor is hidden. */
void cursor_shake_motion(struct sh_server *server) {
    if (!server_settings(server)->shake_to_find)
        return;
    struct sh_cursor_shake *shake = shake_of(server);
    if (!shake)
        return;
    if (server->seat->pointer_state.button_count || server->cursor_mode != SH_CURSOR_PASSTHROUGH ||
        server->active_constraint || server->cursor_hidden) {
        sh_shake_reset(&shake->shake);
        return;
    }
    int64_t now = now_ms();
    if (!sh_shake_motion(&shake->shake, server->cursor->x, server->cursor->y, now) ||
        shake->drawn)
        return;
    if (!shake->timer)
        shake->timer = wl_event_loop_add_timer(wl_display_get_event_loop(server->wl_display),
                                               step, shake);
    if (shake->timer)
        step(shake);
}

/* After a reload: turned off, a grown cursor goes back to its size at once. */
void cursor_shake_reload(struct sh_server *server) {
    if (server->cursor_shake && !server_settings(server)->shake_to_find) {
        memset(&server->cursor_shake->shake, 0, sizeof(server->cursor_shake->shake));
        restore(server->cursor_shake);
    }
}

/* The cursor theme or size changed (profile_themes.c): a grown cursor goes back to its size at
 * once, its image from the new theme, and the next shake grows the new one. */
void cursor_shake_retheme(struct sh_server *server) {
    struct sh_cursor_shake *shake = server->cursor_shake;
    if (!shake)
        return;
    memset(&shake->shake, 0, sizeof(shake->shake));
    restore(shake);
    wlr_buffer_drop(shake->buffer);
    shake->buffer = NULL;
    if (shake->large)
        wlr_xcursor_manager_destroy(shake->large);
    shake->large = NULL;
    shake->size = cursor_theme_size(server);
}

void cursor_shake_finish(struct sh_server *server) {
    struct sh_cursor_shake *shake = server->cursor_shake;
    if (!shake)
        return;
    if (shake->timer)
        wl_event_source_remove(shake->timer);
    wlr_buffer_drop(shake->buffer);
    if (shake->large)
        wlr_xcursor_manager_destroy(shake->large);
    free(shake);
    server->cursor_shake = NULL;
}
