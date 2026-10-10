// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-activation-v1-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"
#include "xdg-toplevel-icon-v1-client-protocol.h"
#include "tearing-control-v1-client-protocol.h"
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <poll.h>
#include <string.h>
#include <sys/mman.h>
#include <signal.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

/* A real xdg-shell client: map pixels, wait for a frame, maximize, restore. */
#ifdef __SANITIZE_ADDRESS__
/* A short-lived test client exits without tearing its protocol objects down. */
const char *__asan_default_options(void) { return "detect_leaks=0"; }
#endif

struct buffer {
    struct wl_buffer *object;
    void *pixels;
    size_t size;
    struct buffer *next;
};
struct probe {
    struct wl_display *display;
    bool list_globals;
    struct wl_compositor *compositor;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct zwlr_layer_surface_v1 *panel;
    struct wl_surface *panel_surface;
    struct zwlr_foreign_toplevel_manager_v1 *manager;
    struct zwlr_foreign_toplevel_handle_v1 *handle;
    struct wl_seat *seat;
    struct wl_output *output;
    int output_width, output_height, panel_height;
    bool panel_ready, handle_minimized, handle_active, handle_closed, title_seen;
    struct wl_shm *shm;
    struct xdg_wm_base *shell;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct wl_callback *frame;
    struct buffer *buffers;
    int width, height, stage;
    bool maximized, fullscreen, handle_fullscreen, done, external_control, external_panel;
    const char *close_app_id;
    bool move_on_press; // PAW_PROBE_MOVE: a button press on the window starts an interactive move
    int resize_edges;   // PAW_PROBE_RESIZE=EDGE: ... or a resize (xdg_toplevel_resize_edge, or 0),
                        // and says "resizing" once the compositor has taken the request up
    bool activate; // --activate: activate the matching window instead of closing it
    bool maximize; // --maximize: ask to maximize the matching window instead of closing it
    struct zwlr_foreign_toplevel_handle_v1 *close_target;
    struct xdg_activation_v1 *activation;
    struct zxdg_decoration_manager_v1 *decorations;
    struct zxdg_toplevel_decoration_v1 *decoration; // with PAW_PROBE_SSD
    // PAW_PROBE_SHADOW=N: a translucent margin N pixels wide around the window, outside its
    // geometry, as a client-side frame draws its shadow.
    int margin;
    bool commands; // --commands: an external-control window that obeys lines on standard input
    bool print_keymap, keymap_seen; // --keymap
    // PAW_PROBE_DRAG=source: a button press on the window drags a line of text from it, as
    // a file manager drags a file; =target: the window takes text dropped on it. Either prints
    // what it hears (see the data source's and data device's listeners).
    bool drag_source, drag_target;
    struct wl_data_device_manager *data_manager;
    struct wl_data_device *data_device;
    struct wl_data_offer *offer; // the drag's offer while it is over the window
    struct xdg_toplevel_icon_manager_v1 *icons; // for PAW_PROBE_ICON and "icon" commands
    // For PAW_PROBE_TEARING: the window's tearing-control-v1 hint.
    struct wp_tearing_control_manager_v1 *tearing_manager;
    struct wp_tearing_control_v1 *tearing;
    // PAW_PROBE_TYPED=PATH: what is typed into the window, as text, appended to PATH.
    FILE *typed;
    struct xkb_context *xkb;
    struct xkb_keymap *typed_keymap;
    struct xkb_state *typed_state;
};
static void die(const char *message) {
    fprintf(stderr, "wayland probe: %s\n", message);
    exit(1);
}
static void ping(void *data, struct xdg_wm_base *shell, uint32_t serial) {
    xdg_wm_base_pong(shell, serial);
}
static const struct xdg_wm_base_listener shell_listener = {.ping = ping};
static void handle_title(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
                         const char *title) {
    struct probe *probe = data;
    probe->title_seen = !strcmp(title, "paw protocol probe");
}
static void handle_app_id(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
                          const char *app_id) {
    struct probe *probe = data;
    if (probe->close_app_id && !strcmp(app_id, probe->close_app_id))
        probe->close_target = handle;
}
static void handle_output(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
                          struct wl_output *output) {}
static void handle_state(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle,
                         struct wl_array *states) {
    struct probe *probe = data;
    probe->handle_minimized = probe->handle_active = probe->handle_fullscreen = false;
    uint32_t *state;
    wl_array_for_each(state, states) {
        if (*state == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED)
            probe->handle_minimized = true;
        if (*state == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED)
            probe->handle_active = true;
        if (*state == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN)
            probe->handle_fullscreen = true;
    }
}
static void handle_done(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle) {
    struct probe *probe = data;
    if (probe->stage == 3 && probe->handle_minimized) {
        probe->stage = 4;
        zwlr_foreign_toplevel_handle_v1_unset_minimized(handle);
        zwlr_foreign_toplevel_handle_v1_activate(handle, probe->seat);
    } else if (probe->stage == 4 && probe->handle_active && !probe->handle_minimized) {
        probe->stage = 5;
        zwlr_foreign_toplevel_handle_v1_close(handle);
    }
}
static void handle_closed(void *data, struct zwlr_foreign_toplevel_handle_v1 *handle) {
    struct probe *probe = data;
    probe->handle_closed = true;
    probe->handle = NULL;
    zwlr_foreign_toplevel_handle_v1_destroy(handle);
}
static const struct zwlr_foreign_toplevel_handle_v1_listener handle_listener = {
    .title = handle_title,
    .app_id = handle_app_id,
    .output_enter = handle_output,
    .output_leave = handle_output,
    .state = handle_state,
    .done = handle_done,
    .closed = handle_closed};
static void manager_toplevel(void *data, struct zwlr_foreign_toplevel_manager_v1 *manager,
                             struct zwlr_foreign_toplevel_handle_v1 *handle) {
    struct probe *probe = data;
    probe->handle = handle;
    zwlr_foreign_toplevel_handle_v1_add_listener(handle, &handle_listener, probe);
}
static void manager_finished(void *data, struct zwlr_foreign_toplevel_manager_v1 *manager) {}
static const struct zwlr_foreign_toplevel_manager_v1_listener manager_listener = {
    .toplevel = manager_toplevel, .finished = manager_finished};
static void output_geometry(void *data, struct wl_output *output, int32_t x, int32_t y, int32_t pw,
                            int32_t ph, int32_t subpixel, const char *make, const char *model,
                            int32_t transform) {}
static void output_mode(void *data, struct wl_output *output, uint32_t flags, int32_t w, int32_t h,
                        int32_t refresh) {
    if (flags & WL_OUTPUT_MODE_CURRENT) {
        ((struct probe *)data)->output_width = w;
        ((struct probe *)data)->output_height = h;
    }
}
static void output_done(void *data, struct wl_output *output) {}
static void output_scale(void *data, struct wl_output *output, int32_t scale) {}
static const struct wl_output_listener output_listener = {
    .geometry = output_geometry, .mode = output_mode, .done = output_done, .scale = output_scale};
static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface,
                   uint32_t version) {
    struct probe *probe = data;
    if (probe->list_globals)
        puts(interface);
    if (!strcmp(interface, "wl_compositor"))
        probe->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    else if (!strcmp(interface, "wl_shm"))
        probe->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    else if (!strcmp(interface, "zwlr_layer_shell_v1"))
        probe->layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 4);
    else if (!strcmp(interface, "zwlr_foreign_toplevel_manager_v1")) {
        probe->manager =
            wl_registry_bind(registry, name, &zwlr_foreign_toplevel_manager_v1_interface, 2);
        zwlr_foreign_toplevel_manager_v1_add_listener(probe->manager, &manager_listener, probe);
    } else if (!strcmp(interface, "wl_seat"))
        probe->seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
    else if (!strcmp(interface, "wl_output") && !probe->output) {
        probe->output = wl_registry_bind(registry, name, &wl_output_interface, 2);
        wl_output_add_listener(probe->output, &output_listener, probe);
    } else if (!strcmp(interface, "xdg_activation_v1")) {
        probe->activation = wl_registry_bind(registry, name, &xdg_activation_v1_interface, 1);
    } else if (!strcmp(interface, "zxdg_decoration_manager_v1")) {
        probe->decorations =
            wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1);
    } else if (!strcmp(interface, "xdg_wm_base")) {
        probe->shell = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(probe->shell, &shell_listener, probe);
    } else if (!strcmp(interface, "wl_data_device_manager") && version >= 3) {
        probe->data_manager =
            wl_registry_bind(registry, name, &wl_data_device_manager_interface, 3);
    } else if (!strcmp(interface, "xdg_toplevel_icon_manager_v1")) {
        probe->icons =
            wl_registry_bind(registry, name, &xdg_toplevel_icon_manager_v1_interface, 1);
    } else if (!strcmp(interface, "wp_tearing_control_manager_v1")) {
        probe->tearing_manager =
            wl_registry_bind(registry, name, &wp_tearing_control_manager_v1_interface, 1);
    }
}
/* A line on standard output, at once: the test reads it while the probe runs. */
__attribute__((format(printf, 1, 2))) static void say(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    vprintf(format, arguments);
    va_end(arguments);
    putchar('\n');
    fflush(stdout);
}
/* The drag's source hears where it is ("drag target MIME", or "-" where nothing takes it), the
 * action agreed on, and how it ends: "drag dropped" then "drag finished", or "drag cancelled". */
static void source_target(void *data, struct wl_data_source *source, const char *mime) {
    say("drag target %s", mime ? mime : "-");
}
static void source_send(void *data, struct wl_data_source *source, const char *mime, int32_t fd) {
    static const char text[] = "paw drag";
    if (write(fd, text, sizeof(text) - 1) < 0)
        die("cannot send the dragged text");
    close(fd);
}
static void source_cancelled(void *data, struct wl_data_source *source) {
    say("drag cancelled");
    wl_data_source_destroy(source);
}
static void source_dropped(void *data, struct wl_data_source *source) {
    say("drag dropped");
}
static void source_finished(void *data, struct wl_data_source *source) {
    say("drag finished");
    wl_data_source_destroy(source);
}
static void source_action(void *data, struct wl_data_source *source, uint32_t action) {
    say("drag action %u", action);
}
static const struct wl_data_source_listener source_listener = {
    .target = source_target, .send = source_send, .cancelled = source_cancelled,
    .dnd_drop_performed = source_dropped, .dnd_finished = source_finished,
    .action = source_action};
static const char drag_mime[] = "text/plain;charset=utf-8";
/* Starts dragging from the window, for the press that `serial` names. */
static void start_drag(struct probe *probe, uint32_t serial) {
    struct wl_data_source *source = wl_data_device_manager_create_data_source(probe->data_manager);
    wl_data_source_add_listener(source, &source_listener, probe);
    wl_data_source_offer(source, drag_mime);
    wl_data_source_set_actions(source, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY |
                                           WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE);
    wl_data_device_start_drag(probe->data_device, source, probe->surface, NULL, serial);
}
static void offer_offer(void *data, struct wl_data_offer *offer, const char *mime) {}
static void offer_source_actions(void *data, struct wl_data_offer *offer, uint32_t actions) {}
static void offer_action(void *data, struct wl_data_offer *offer, uint32_t action) {}
static const struct wl_data_offer_listener offer_listener = {
    .offer = offer_offer, .source_actions = offer_source_actions, .action = offer_action};
static void device_data_offer(void *data, struct wl_data_device *device,
                              struct wl_data_offer *offer) {
    wl_data_offer_add_listener(offer, &offer_listener, data);
}
/* A target takes the text as a copy wherever on the window it is dragged ("drop offered"), and
 * once dropped reads it ("drop received TEXT"). */
static void device_enter(void *data, struct wl_data_device *device, uint32_t serial,
                         struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y,
                         struct wl_data_offer *offer) {
    struct probe *probe = data;
    if (probe->offer)
        wl_data_offer_destroy(probe->offer);
    probe->offer = offer;
    if (!offer || !probe->drag_target)
        return;
    wl_data_offer_accept(offer, serial, drag_mime);
    wl_data_offer_set_actions(offer, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY,
                              WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
    say("drop offered");
}
static void device_leave(void *data, struct wl_data_device *device) {
    struct probe *probe = data;
    if (probe->offer)
        wl_data_offer_destroy(probe->offer);
    probe->offer = NULL;
}
static void device_motion(void *data, struct wl_data_device *device, uint32_t time, wl_fixed_t x,
                          wl_fixed_t y) {}
static void device_drop(void *data, struct wl_data_device *device) {
    struct probe *probe = data;
    int fds[2];
    if (!probe->offer || !probe->drag_target || pipe(fds) < 0)
        return;
    wl_data_offer_receive(probe->offer, drag_mime, fds[1]);
    close(fds[1]);
    wl_display_flush(wl_proxy_get_display((struct wl_proxy *)device));
    char text[64];
    size_t length = 0;
    ssize_t count;
    while (length < sizeof(text) - 1 && (count = read(fds[0], text + length, sizeof(text) - 1 - length)) > 0)
        length += (size_t)count;
    close(fds[0]);
    text[length] = '\0';
    say("drop received %s", text);
    wl_data_offer_finish(probe->offer);
    wl_data_offer_destroy(probe->offer);
    probe->offer = NULL;
}
static void device_selection(void *data, struct wl_data_device *device,
                             struct wl_data_offer *offer) {
    if (offer)
        wl_data_offer_destroy(offer);
}
static const struct wl_data_device_listener device_listener = {
    .data_offer = device_data_offer, .enter = device_enter, .leave = device_leave,
    .motion = device_motion, .drop = device_drop, .selection = device_selection};
/* Asks to be focused, the way an application does for a message it wants read: a token from
 * xdg-activation, made without an input serial, and then activate with it. */
static void token_done(void *data, struct xdg_activation_token_v1 *token, const char *string) {
    struct probe *probe = data;
    xdg_activation_v1_activate(probe->activation, string, probe->surface);
    xdg_activation_token_v1_destroy(token);
}
static const struct xdg_activation_token_v1_listener token_listener = {.done = token_done};
static void request_activation(struct probe *probe) {
    if (!probe->activation)
        die("xdg_activation_v1 is not advertised");
    struct xdg_activation_token_v1 *token = xdg_activation_v1_get_activation_token(probe->activation);
    xdg_activation_token_v1_add_listener(token, &token_listener, probe);
    xdg_activation_token_v1_set_surface(token, probe->surface);
    xdg_activation_token_v1_commit(token);
}
/* A buffer for an icon: `size` pixels square, each `colour` (0xAARRGGBB, premultiplied as wl_shm's
 * argb8888 is, or xrgb8888 when `opaque`, whose alpha means nothing). Each row is followed by two
 * pixels of padding, 0xdeadbeef, which are not the icon's. */
static struct wl_buffer *icon_buffer(struct probe *probe, int size, uint32_t colour, bool opaque) {
    struct buffer *buffer = calloc(1, sizeof(*buffer));
    if (!buffer)
        die("out of memory");
    int stride = (size + 2) * 4;
    buffer->size = (size_t)stride * size;
    int fd = memfd_create("paw-test-icon", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, buffer->size) != 0)
        die("cannot allocate shm buffer");
    buffer->pixels = mmap(NULL, buffer->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buffer->pixels == MAP_FAILED)
        die("cannot map shm buffer");
    uint32_t *pixels = buffer->pixels;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size + 2; ++x)
            pixels[y * (size + 2) + x] = x < size ? colour : 0xdeadbeef;
    struct wl_shm_pool *pool = wl_shm_create_pool(probe->shm, fd, (int)buffer->size);
    buffer->object = wl_shm_pool_create_buffer(
        pool, 0, size, size, stride, opaque ? WL_SHM_FORMAT_XRGB8888 : WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    buffer->next = probe->buffers; // kept until the probe exits, as the icon may still use it
    probe->buffers = buffer;
    return buffer->object;
}
/* Gives the window an icon through xdg-toplevel-icon-v1, as SPEC says: "none" sets none, else its
 * words each add to one icon: "name:NAME" its name, "SIZE:AARRGGBB" a buffer SIZE pixels square
 * in that colour, "SIZE:AARRGGBB:x" one in xrgb8888, and "empty" nothing, for an icon with
 * neither. The icon applies at the window's next commit. */
static void set_icon(struct probe *probe, const char *spec) {
    if (!probe->icons)
        die("xdg_toplevel_icon_manager_v1 is not advertised");
    if (!strcmp(spec, "none")) {
        xdg_toplevel_icon_manager_v1_set_icon(probe->icons, probe->toplevel, NULL);
        return;
    }
    struct xdg_toplevel_icon_v1 *icon = xdg_toplevel_icon_manager_v1_create_icon(probe->icons);
    char words[512];
    snprintf(words, sizeof(words), "%s", spec);
    for (char *word = strtok(words, " "); word; word = strtok(NULL, " ")) {
        int size;
        unsigned colour;
        char format = '\0';
        if (!strncmp(word, "name:", 5))
            xdg_toplevel_icon_v1_set_name(icon, word + 5);
        else if (sscanf(word, "%d:%x:%c", &size, &colour, &format) >= 2 && size >= 1 &&
                 size <= 1024)
            xdg_toplevel_icon_v1_add_buffer(icon, icon_buffer(probe, size, colour, format == 'x'),
                                            1);
        else if (strcmp(word, "empty"))
            die("an icon takes \"none\", or \"name:NAME\", \"SIZE:AARRGGBB[:x]\" and \"empty\"");
    }
    xdg_toplevel_icon_manager_v1_set_icon(probe->icons, probe->toplevel, icon);
    xdg_toplevel_icon_v1_destroy(icon);
}
/* One line of standard input: "activate", "title TEXT", "app_id TEXT", or "icon SPEC" (see
 * set_icon). */
static void run_command(struct probe *probe, char *line) {
    line[strcspn(line, "\n")] = '\0';
    if (!strcmp(line, "activate"))
        request_activation(probe);
    else if (!strncmp(line, "title ", 6))
        xdg_toplevel_set_title(probe->toplevel, line + 6);
    else if (!strncmp(line, "app_id ", 7))
        xdg_toplevel_set_app_id(probe->toplevel, line + 7);
    else if (!strncmp(line, "icon ", 5)) {
        set_icon(probe, line + 5);
        wl_surface_commit(probe->surface);
    } else
        die("unknown command");
}
/* Dispatches Wayland events and standard input together until the window is closed. */
static void run_commands(struct wl_display *display, struct probe *probe) {
    char pending[256];
    size_t length = 0;
    bool input = true;
    while (!probe->done) {
        while (wl_display_prepare_read(display) != 0)
            wl_display_dispatch_pending(display);
        wl_display_flush(display);
        struct pollfd fds[2] = {{wl_display_get_fd(display), POLLIN, 0}, {input ? 0 : -1, POLLIN, 0}};
        int ready = poll(fds, 2, -1);
        if (ready < 0 || !(fds[0].revents & POLLIN))
            wl_display_cancel_read(display);
        else if (wl_display_read_events(display) < 0)
            die("dispatch failed");
        if (wl_display_dispatch_pending(display) < 0)
            die("dispatch failed");
        if (ready > 0 && (fds[1].revents & (POLLIN | POLLHUP))) {
            ssize_t count = read(0, pending + length, sizeof(pending) - 1 - length);
            if (count <= 0)
                input = false;
            else
                length += (size_t)count;
            char *newline;
            pending[length] = '\0';
            while ((newline = strchr(pending, '\n'))) {
                *newline = '\0';
                run_command(probe, pending);
                length -= (size_t)(newline + 1 - pending);
                memmove(pending, newline + 1, length + 1);
            }
        }
    }
}
static void global_remove(void *data, struct wl_registry *registry, uint32_t name) {}
static const struct wl_registry_listener registry_listener = {global, global_remove};
static void pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y) {}
static void pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface) {}
static void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t x,
                           wl_fixed_t y) {}
static void resize_taken(void *data, struct wl_callback *callback, uint32_t time) {
    wl_callback_destroy(callback);
    say("resizing");
}
static const struct wl_callback_listener resize_taken_listener = {.done = resize_taken};
static void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time,
                           uint32_t button, uint32_t state) {
    struct probe *probe = data;
    if (probe->resize_edges && state == WL_POINTER_BUTTON_STATE_PRESSED) {
        xdg_toplevel_resize(probe->toplevel, probe->seat, serial, probe->resize_edges);
        // The compositor answers a sync after the requests before it.
        wl_callback_add_listener(wl_display_sync(probe->display), &resize_taken_listener, NULL);
    } else if (probe->move_on_press && state == WL_POINTER_BUTTON_STATE_PRESSED)
        xdg_toplevel_move(probe->toplevel, probe->seat, serial);
    else if (probe->drag_source && state == WL_POINTER_BUTTON_STATE_PRESSED)
        start_drag(probe, serial);
}
static void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis,
                         wl_fixed_t value) {}
static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter, .leave = pointer_leave, .motion = pointer_motion,
    .button = pointer_button, .axis = pointer_axis};
/* --keymap: prints the keymap the seat's keyboard gives a new client, as text, or "no keymap". */
static void keyboard_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd,
                            uint32_t size) {
    struct probe *probe = data;
    char *text = format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 && size
                     ? mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0)
                     : MAP_FAILED;
    if (text != MAP_FAILED) {
        fwrite(text, 1, strnlen(text, size), stdout);
        munmap(text, size);
    } else {
        puts("no keymap");
    }
    close(fd);
    probe->keymap_seen = true;
}
static void keyboard_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                           struct wl_surface *surface, struct wl_array *keys) {}
static void keyboard_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                           struct wl_surface *surface) {}
static void keyboard_key(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time,
                         uint32_t key, uint32_t state) {}
static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                               uint32_t depressed, uint32_t latched, uint32_t locked,
                               uint32_t group) {}
static void keyboard_repeat_info(void *data, struct wl_keyboard *keyboard, int32_t rate,
                                 int32_t delay) {}
static const struct wl_keyboard_listener keyboard_listener = {
    .keymap = keyboard_keymap, .enter = keyboard_enter, .leave = keyboard_leave,
    .key = keyboard_key, .modifiers = keyboard_modifiers, .repeat_info = keyboard_repeat_info};
/* PAW_PROBE_TYPED: each key pressed in the window, as the text it types under the keymap the
 * window has then (Return as a new line), appended to the file; every new keymap is read. */
static void typed_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd,
                         uint32_t size) {
    struct probe *probe = data;
    char *text = format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 && size
                     ? mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0)
                     : MAP_FAILED;
    close(fd);
    if (text == MAP_FAILED)
        return;
    struct xkb_keymap *keymap = xkb_keymap_new_from_string(
        probe->xkb, text, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(text, size);
    if (!keymap)
        return;
    xkb_state_unref(probe->typed_state);
    xkb_keymap_unref(probe->typed_keymap);
    probe->typed_keymap = keymap;
    probe->typed_state = xkb_state_new(keymap);
}
static void typed_key(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time,
                      uint32_t key, uint32_t state) {
    struct probe *probe = data;
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED || !probe->typed_state)
        return;
    const xkb_keysym_t keysym = xkb_state_key_get_one_sym(probe->typed_state, key + 8);
    char text[16] = "\n";
    if (keysym != XKB_KEY_Return && xkb_keysym_to_utf8(keysym, text, sizeof(text)) <= 0)
        return;
    fputs(text, probe->typed);
    fflush(probe->typed);
}
static void typed_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                            uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
    struct probe *probe = data;
    if (probe->typed_state)
        xkb_state_update_mask(probe->typed_state, depressed, latched, locked, 0, 0, group);
}
static const struct wl_keyboard_listener typed_listener = {
    .keymap = typed_keymap, .enter = keyboard_enter, .leave = keyboard_leave,
    .key = typed_key, .modifiers = typed_modifiers, .repeat_info = keyboard_repeat_info};
static void frame_done(void *data, struct wl_callback *callback, uint32_t time) {
    struct probe *probe = data;
    wl_callback_destroy(callback);
    probe->frame = NULL;
    if (probe->external_control)
        return;
    if (probe->stage == 0) {
        puts("mapped and received frame");
        probe->stage = 1;
        xdg_toplevel_set_maximized(probe->toplevel);
        wl_surface_commit(probe->surface);
    } else if (probe->stage == 1 && probe->maximized) {
        if (probe->width <= 320 || probe->height <= 240)
            die("maximize did not resize window");
        if (probe->height != probe->output_height - probe->panel_height) {
            fprintf(stderr, "wayland probe: maximized height %d, output %d, panel %d\n",
                    probe->height, probe->output_height, probe->panel_height);
            die("maximize covered the reserved panel area");
        }
        puts("maximize respected panel reservation and rendered");
        probe->stage = 2;
        xdg_toplevel_unset_maximized(probe->toplevel);
        wl_surface_commit(probe->surface);
    } else if (probe->stage == 2 && !probe->maximized) {
        if (probe->width != 320 || probe->height != 240)
            die("floating size was not restored");
        puts("floating size restored and rendered");
        if (!probe->handle || !probe->title_seen)
            die("window was not published to taskbar clients");
        probe->stage = 6;
        xdg_toplevel_set_fullscreen(probe->toplevel, NULL);
        wl_surface_commit(probe->surface);
    } else if (probe->stage == 6 && probe->fullscreen) {
        // Fullscreen the client asks for itself covers the panel too.
        if (probe->width != probe->output_width || probe->height != probe->output_height)
            die("fullscreen did not cover the whole output");
        if (!probe->handle_fullscreen)
            die("taskbar handle did not report fullscreen");
        puts("fullscreen covered the output, including the panel, and rendered");
        probe->stage = 7;
        xdg_toplevel_unset_fullscreen(probe->toplevel);
        wl_surface_commit(probe->surface);
    } else if (probe->stage == 7 && !probe->fullscreen) {
        if (probe->width != 320 || probe->height != 240)
            die("floating size was not restored after fullscreen");
        puts("fullscreen exit restored floating size");
        probe->stage = 3;
        zwlr_foreign_toplevel_handle_v1_set_minimized(probe->handle);
    }
}
static const struct wl_callback_listener frame_listener = {.done = frame_done};
/* A buffer of `width` by `height` filled as the probe's window, `margin` pixels of it all
 * around a translucent shadow. */
static struct wl_buffer *make_buffer(struct probe *probe, int width, int height, int margin) {
    struct buffer *buffer = calloc(1, sizeof(*buffer));
    if (!buffer)
        die("out of memory");
    buffer->size = (size_t)width * height * 4;
    int fd = memfd_create("paw-test-buffer", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, buffer->size) != 0)
        die("cannot allocate shm buffer");
    buffer->pixels = mmap(NULL, buffer->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (buffer->pixels == MAP_FAILED)
        die("cannot map shm buffer");
    uint32_t *pixels = buffer->pixels;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            pixels[y * width + x] =
                x < margin || y < margin || x >= width - margin || y >= height - margin
                    ? 0x30000000
                : y < margin + 36 ? 0xff23314a
                                  : 0xff417bc4;
    struct wl_shm_pool *pool = wl_shm_create_pool(probe->shm, fd, (int)buffer->size);
    buffer->object = wl_shm_pool_create_buffer(
        pool, 0, width, height, width * 4, margin ? WL_SHM_FORMAT_ARGB8888 : WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    buffer->next = probe->buffers;
    probe->buffers = buffer;
    return buffer->object;
}
static void panel_configure(void *data, struct zwlr_layer_surface_v1 *panel, uint32_t serial,
                            uint32_t width, uint32_t height) {
    struct probe *probe = data;
    zwlr_layer_surface_v1_ack_configure(panel, serial);
    if (height != 48 || width < 1 || width > 8192)
        die("panel configure geometry invalid");
    struct wl_buffer *buffer = make_buffer(probe, width, height, 0);
    wl_surface_attach(probe->panel_surface, buffer, 0, 0);
    wl_surface_damage_buffer(probe->panel_surface, 0, 0, width, height);
    wl_surface_commit(probe->panel_surface);
    probe->panel_ready = true;
}
static void panel_closed(void *data, struct zwlr_layer_surface_v1 *panel) {
    die("panel closed unexpectedly");
}
static const struct zwlr_layer_surface_v1_listener panel_listener = {.configure = panel_configure,
                                                                     .closed = panel_closed};
static void surface_configure(void *data, struct xdg_surface *surface, uint32_t serial) {
    struct probe *probe = data;
    // Closing, the window has unmapped: a configure sent before the compositor saw that (focus
    // moving to it as other windows close) is no longer one to acknowledge.
    if (probe->done)
        return;
    xdg_surface_ack_configure(surface, serial);
    if (probe->width < 1 || probe->height < 1 || probe->width > 8192 || probe->height > 8192)
        die("unexpected configure dimensions");
    int margin = probe->margin, width = probe->width + 2 * margin;
    int height = probe->height + 2 * margin;
    struct wl_buffer *buffer = make_buffer(probe, width, height, margin);
    if (margin)
        xdg_surface_set_window_geometry(surface, margin, margin, probe->width, probe->height);
    wl_surface_attach(probe->surface, buffer, 0, 0);
    wl_surface_damage_buffer(probe->surface, 0, 0, width, height);
    if (!probe->frame) {
        probe->frame = wl_surface_frame(probe->surface);
        wl_callback_add_listener(probe->frame, &frame_listener, probe);
    }
    wl_surface_commit(probe->surface);
}
static const struct xdg_surface_listener surface_listener = {.configure = surface_configure};
static void toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t width,
                               int32_t height, struct wl_array *states) {
    struct probe *probe = data;
    probe->width = width > 0 ? width : 320;
    probe->height = height > 0 ? height : 240;
    probe->maximized = probe->fullscreen = false;
    uint32_t *state;
    wl_array_for_each(state, states) {
        if (*state == XDG_TOPLEVEL_STATE_MAXIMIZED)
            probe->maximized = true;
        if (*state == XDG_TOPLEVEL_STATE_FULLSCREEN)
            probe->fullscreen = true;
    }
}
static void toplevel_close(void *data, struct xdg_toplevel *toplevel) {
    struct probe *probe = data;
    // PAW_PROBE_REFUSE_CLOSE stays open, as an application asking whether to save would.
    if (getenv("PAW_PROBE_REFUSE_CLOSE")) {
        puts("close refused");
        fflush(stdout);
        return;
    }
    if (!probe->external_control && probe->stage != 5)
        die("unexpected close request");
    puts("taskbar minimize, restore, activate, and close passed");
    probe->done = true;
}
static const struct xdg_toplevel_listener toplevel_listener = {.configure = toplevel_configure,
                                                               .close = toplevel_close};
/* PAW_PROBE_SPAWN_APP_ID (or PAW_PROBE_SPAWN_PROGRAM, a program that takes "wait-close"): on SIGUSR1 start another probe window with that app_id as a child
 * of this process (through a shell that stays in between when PAW_PROBE_SPAWN_SHELL is set),
 * like an application started from a terminal. */
extern char **environ;
static char *spawn_argv[8], **spawn_envp;
static const char *spawn_program;
static void spawn_child(int sig) {
    pid_t pid = fork();
    if (pid == 0) {
        execve(spawn_program, spawn_argv, spawn_envp);
        _exit(127);
    }
}
static void prepare_spawn(const char *self, const char *app_id) {
    size_t count = 0;
    while (environ[count])
        ++count;
    spawn_envp = calloc(count + 3, sizeof(char *));
    size_t used = 0;
    for (size_t i = 0; i < count; ++i)
        if (strncmp(environ[i], "PAW_PROBE_APP_ID=", strlen("PAW_PROBE_APP_ID=")) &&
            strncmp(environ[i], "PAW_PROBE_SPAWN_", strlen("PAW_PROBE_SPAWN_")))
            spawn_envp[used++] = environ[i];
    char *entry = malloc(strlen(app_id) + 32);
    sprintf(entry, "PAW_PROBE_APP_ID=%s", app_id);
    spawn_envp[used++] = entry;
    if (getenv("PAW_PROBE_SPAWN_TITLE")) {
        entry = malloc(strlen(getenv("PAW_PROBE_SPAWN_TITLE")) + 32);
        sprintf(entry, "PAW_PROBE_TITLE=%s", getenv("PAW_PROBE_SPAWN_TITLE"));
        spawn_envp[used++] = entry;
    }
    if (getenv("PAW_PROBE_SPAWN_PROGRAM")) { // another kind of client, e.g. the X11 probe
        spawn_program = getenv("PAW_PROBE_SPAWN_PROGRAM");
        spawn_argv[0] = (char *)spawn_program;
        spawn_argv[1] = "wait-close";
    } else if (getenv("PAW_PROBE_SPAWN_SHELL")) {
        spawn_program = "/bin/sh";
        spawn_argv[0] = "sh";
        spawn_argv[1] = "-c";
        spawn_argv[2] = "\"$0\" --window-only; status=$?; exit $status";
        spawn_argv[3] = (char *)self;
    } else {
        spawn_program = self;
        spawn_argv[0] = (char *)self;
        spawn_argv[1] = "--window-only";
    }
    struct sigaction action = {.sa_handler = spawn_child, .sa_flags = SA_RESTART};
    sigaction(SIGUSR1, &action, NULL);
    signal(SIGCHLD, SIG_IGN);
}
int main(int argc, char **argv) {
    struct probe probe = {.width = 320, .height = 240, .panel_height = 48};
    if (argc == 2 && !strcmp(argv[1], "--external-control"))
        probe.external_control = true;
    else if (argc == 2 && !strcmp(argv[1], "--window-only")) // external control, no panel
        probe.external_control = probe.external_panel = true;
    else if (argc == 2 && !strcmp(argv[1], "--commands")) // window only, told what to do on stdin
        probe.external_control = probe.external_panel = probe.commands = true;
    else if (argc == 2 && !strcmp(argv[1], "--globals"))
        probe.list_globals = true;
    else if (argc == 2 && !strcmp(argv[1], "--keymap"))
        probe.print_keymap = true;
    else if (argc == 3 && !strcmp(argv[1], "--external-panel")) {
        char *end;
        long height = strtol(argv[2], &end, 10);
        if (*end || height < 0 || height > 100)
            die("invalid external panel height");
        probe.external_panel = true;
        probe.panel_height = (int)height;
    } else if (argc == 3 && (!strcmp(argv[1], "--close") || !strcmp(argv[1], "--activate") ||
                             !strcmp(argv[1], "--maximize"))) {
        probe.close_app_id = argv[2];
        probe.activate = !strcmp(argv[1], "--activate");
        probe.maximize = !strcmp(argv[1], "--maximize");
    } else if (argc != 1)
        die("usage: wayland_probe [--globals | --keymap | --external-control | --window-only | "
            "--commands | --external-panel HEIGHT | --close APP_ID | --activate APP_ID | --maximize APP_ID]");
    struct wl_display *display = wl_display_connect(NULL);
    if (!display)
        die("cannot connect to compositor");
    probe.display = display;
    if (getenv("PAW_PROBE_SPAWN_APP_ID") || getenv("PAW_PROBE_SPAWN_PROGRAM")) {
        static char self[4096];
        ssize_t length = readlink("/proc/self/exe", self, sizeof(self) - 1);
        if (length <= 0)
            die("cannot find the probe's own path");
        self[length] = '\0';
        prepare_spawn(self, getenv("PAW_PROBE_SPAWN_APP_ID") ? getenv("PAW_PROBE_SPAWN_APP_ID")
                                                               : "");
    }
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &probe);
    if (wl_display_roundtrip(display) < 0)
        die("registry roundtrip failed");
    if (probe.list_globals)
        return 0;
    if (probe.print_keymap) {
        // The seat sends its keyboard's keymap as soon as a client asks for a keyboard.
        if (!probe.seat)
            die("no seat");
        wl_keyboard_add_listener(wl_seat_get_keyboard(probe.seat), &keyboard_listener, &probe);
        if (wl_display_roundtrip(display) < 0)
            die("keyboard roundtrip failed");
        if (!probe.keymap_seen)
            die("no keymap arrived");
        return 0;
    }
    if (!probe.compositor || !probe.shm || !probe.shell)
        die("required globals missing");
    if (!probe.layer_shell || !probe.manager || !probe.seat || !probe.output)
        die("desktop protocols missing");
    const char *typed = getenv("PAW_PROBE_TYPED");
    if (typed) {
        probe.typed = fopen(typed, "a");
        probe.xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (!probe.typed || !probe.xkb)
            die("cannot follow what is typed");
        wl_keyboard_add_listener(wl_seat_get_keyboard(probe.seat), &typed_listener, &probe);
    }
    if (probe.close_app_id) {
        // Close, activate, or maximize another client's window through the taskbar protocol.
        if (wl_display_roundtrip(display) < 0)
            die("taskbar roundtrip failed");
        if (!probe.close_target)
            die("no taskbar handle with the requested app_id");
        if (probe.activate)
            zwlr_foreign_toplevel_handle_v1_activate(probe.close_target, probe.seat);
        else if (probe.maximize)
            zwlr_foreign_toplevel_handle_v1_set_maximized(probe.close_target);
        else
            zwlr_foreign_toplevel_handle_v1_close(probe.close_target);
        if (wl_display_roundtrip(display) < 0)
            die("taskbar request failed");
        puts(probe.activate   ? "taskbar activate sent"
             : probe.maximize ? "taskbar maximize sent"
                              : "taskbar close sent");
        return 0;
    }
    if (!probe.external_panel) {
        probe.panel_surface = wl_compositor_create_surface(probe.compositor);
        probe.panel = zwlr_layer_shell_v1_get_layer_surface(
            probe.layer_shell, probe.panel_surface, probe.output, ZWLR_LAYER_SHELL_V1_LAYER_TOP,
            "paw-test-panel");
        zwlr_layer_surface_v1_add_listener(probe.panel, &panel_listener, &probe);
        zwlr_layer_surface_v1_set_size(probe.panel, 0, 48);
        zwlr_layer_surface_v1_set_anchor(probe.panel, ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                                          ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                                          ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
        zwlr_layer_surface_v1_set_exclusive_zone(probe.panel, 48);
        wl_surface_commit(probe.panel_surface);
        while (!probe.panel_ready)
            if (wl_display_dispatch(display) < 0)
                die("panel setup failed");
        if (wl_display_roundtrip(display) < 0)
            die("panel mapping failed");
    }
    probe.surface = wl_compositor_create_surface(probe.compositor);
    probe.xdg_surface = xdg_wm_base_get_xdg_surface(probe.shell, probe.surface);
    xdg_surface_add_listener(probe.xdg_surface, &surface_listener, &probe);
    probe.toplevel = xdg_surface_get_toplevel(probe.xdg_surface);
    xdg_toplevel_add_listener(probe.toplevel, &toplevel_listener, &probe);
    // PAW_PROBE_SSD leaves the frame to the compositor, as kitty does.
    if (getenv("PAW_PROBE_SHADOW"))
        probe.margin = atoi(getenv("PAW_PROBE_SHADOW"));
    if (getenv("PAW_PROBE_SSD") && probe.decorations) {
        probe.decoration =
            zxdg_decoration_manager_v1_get_toplevel_decoration(probe.decorations, probe.toplevel);
        zxdg_toplevel_decoration_v1_set_mode(probe.decoration,
                                             ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }
    if (getenv("PAW_PROBE_MOVE") || getenv("PAW_PROBE_RESIZE")) {
        const char *edge = getenv("PAW_PROBE_RESIZE");
        if (edge) { // top, bottom, left, right, or two of them joined by "_"
            probe.resize_edges = (strstr(edge, "top") ? XDG_TOPLEVEL_RESIZE_EDGE_TOP : 0) |
                                 (strstr(edge, "bottom") ? XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM : 0) |
                                 (strstr(edge, "left") ? XDG_TOPLEVEL_RESIZE_EDGE_LEFT : 0) |
                                 (strstr(edge, "right") ? XDG_TOPLEVEL_RESIZE_EDGE_RIGHT : 0);
        }
        probe.move_on_press = true;
    }
    const char *drag = getenv("PAW_PROBE_DRAG");
    probe.drag_source = drag && !strcmp(drag, "source");
    probe.drag_target = drag && !strcmp(drag, "target");
    if (probe.drag_source || probe.drag_target) {
        if (!probe.data_manager)
            die("wl_data_device_manager version 3 is not advertised");
        probe.data_device = wl_data_device_manager_get_data_device(probe.data_manager, probe.seat);
        wl_data_device_add_listener(probe.data_device, &device_listener, &probe);
    }
    if (probe.move_on_press || probe.drag_source)
        wl_pointer_add_listener(wl_seat_get_pointer(probe.seat), &pointer_listener, &probe);
    // Tests telling several probes apart name them through PAW_PROBE_TITLE, and window
    // rule tests through PAW_PROBE_APP_ID.
    const char *title = getenv("PAW_PROBE_TITLE"), *app_id = getenv("PAW_PROBE_APP_ID");
    xdg_toplevel_set_title(probe.toplevel, title && *title ? title : "paw protocol probe");
    xdg_toplevel_set_app_id(probe.toplevel, app_id && *app_id ? app_id : "paw-probe");
    // PAW_PROBE_ICON=SPEC gives the window an icon before it maps (see set_icon).
    if (getenv("PAW_PROBE_ICON"))
        set_icon(&probe, getenv("PAW_PROBE_ICON"));
    // PAW_PROBE_TEARING=async (or vsync) gives the window that tearing-control-v1 hint, as a
    // game asking for its frames at once does.
    const char *tearing = getenv("PAW_PROBE_TEARING");
    if (tearing) {
        if (!probe.tearing_manager)
            die("wp_tearing_control_manager_v1 is not advertised");
        probe.tearing =
            wp_tearing_control_manager_v1_get_tearing_control(probe.tearing_manager, probe.surface);
        wp_tearing_control_v1_set_presentation_hint(
            probe.tearing, !strcmp(tearing, "async") ? WP_TEARING_CONTROL_V1_PRESENTATION_HINT_ASYNC
                                                     : WP_TEARING_CONTROL_V1_PRESENTATION_HINT_VSYNC);
    }
    wl_surface_commit(probe.surface);
    if (probe.commands)
        run_commands(display, &probe);
    while (!probe.done)
        if (wl_display_dispatch(display) < 0)
            die("dispatch failed");
    wl_surface_attach(probe.surface, NULL, 0, 0);
    wl_surface_commit(probe.surface);
    if (wl_display_roundtrip(display) < 0)
        die("unmap failed");
    if (probe.decoration) // it must go before its toplevel
        zxdg_toplevel_decoration_v1_destroy(probe.decoration);
    xdg_toplevel_destroy(probe.toplevel);
    xdg_surface_destroy(probe.xdg_surface);
    wl_surface_destroy(probe.surface);
    if (wl_display_roundtrip(display) < 0)
        die("destroy failed");
    for (struct buffer *buffer = probe.buffers, *next; buffer; buffer = next) {
        next = buffer->next;
        wl_buffer_destroy(buffer->object);
        munmap(buffer->pixels, buffer->size);
        free(buffer);
    }
    if (!probe.handle_closed)
        die("taskbar window handle survived unmapping");
    if (probe.activation)
        xdg_activation_v1_destroy(probe.activation);
    if (probe.icons)
        xdg_toplevel_icon_manager_v1_destroy(probe.icons);
    if (probe.tearing)
        wp_tearing_control_v1_destroy(probe.tearing);
    if (probe.tearing_manager)
        wp_tearing_control_manager_v1_destroy(probe.tearing_manager);
    if (probe.panel) {
        zwlr_layer_surface_v1_destroy(probe.panel);
        wl_surface_destroy(probe.panel_surface);
    }
    zwlr_layer_shell_v1_destroy(probe.layer_shell);
    zwlr_foreign_toplevel_manager_v1_destroy(probe.manager);
    wl_seat_destroy(probe.seat);
    wl_output_destroy(probe.output);
    xdg_wm_base_destroy(probe.shell);
    wl_shm_destroy(probe.shm);
    wl_compositor_destroy(probe.compositor);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);
    puts("unmap and destroy passed");
    return 0;
}
