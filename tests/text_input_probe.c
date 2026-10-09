// SPDX-License-Identifier: GPL-3.0-or-later
/* A window that takes text through text-input-unstable-v3, as GTK and Qt applications do, for the
 * input method tests. As its text input enters the window it enables it, with surrounding text,
 * a content type and the text cursor's rectangle, and prints what it hears, a line per event:
 *   ready | enter | leave | preedit TEXT BEGIN END | commit TEXT | delete BEFORE AFTER | done
 *   key CODE pressed|released (the wl_keyboard's, an evdev code)
 * (TEXT "-" for none). Lines on standard input change what it tells: "enable" and "disable",
 * "surrounding TEXT" (the cursor at its end), "cursor X Y WIDTH HEIGHT", each committed at once.
 * Usage: text_input_probe [--no-enable] [--overlay] [TITLE]; with --no-enable it waits for
 * "enable", and with --overlay it is a search field on the overlay layer, as the shell's start
 * menu is, 400 by 60 pixels at the top of the output and taking the keyboard, rather than a
 * window. */
#define _GNU_SOURCE
#include "text-input-unstable-v3-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#ifdef __SANITIZE_ADDRESS__
/* A short-lived test client exits without tearing its protocol objects down. */
const char *__asan_default_options(void) { return "detect_leaks=0"; }
#endif

struct probe {
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *shell;
    struct wl_seat *seat;
    struct wl_keyboard *keyboard;
    struct zwp_text_input_manager_v3 *manager;
    struct zwp_text_input_v3 *text_input;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct zwlr_layer_surface_v1 *layer;
    int width, height;
    bool ready, entered, enable_on_enter;
    char surrounding[256];
    int cursor[4];
};

static void die(const char *message) {
    fprintf(stderr, "text input probe: %s\n", message);
    exit(1);
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

/* Enables the text input with what the probe tells of its text, or disables it. */
static void enable(struct probe *probe, bool on) {
    if (on) {
        zwp_text_input_v3_enable(probe->text_input);
        size_t length = strlen(probe->surrounding);
        zwp_text_input_v3_set_surrounding_text(probe->text_input, probe->surrounding,
                                               (int32_t)length, (int32_t)length);
        zwp_text_input_v3_set_text_change_cause(probe->text_input,
                                                ZWP_TEXT_INPUT_V3_CHANGE_CAUSE_OTHER);
        zwp_text_input_v3_set_content_type(probe->text_input,
                                           ZWP_TEXT_INPUT_V3_CONTENT_HINT_SPELLCHECK,
                                           ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL);
        zwp_text_input_v3_set_cursor_rectangle(probe->text_input, probe->cursor[0],
                                               probe->cursor[1], probe->cursor[2],
                                               probe->cursor[3]);
    } else {
        zwp_text_input_v3_disable(probe->text_input);
    }
    zwp_text_input_v3_commit(probe->text_input);
}

static void text_enter(void *data, struct zwp_text_input_v3 *text_input,
                       struct wl_surface *surface) {
    struct probe *probe = data;
    probe->entered = true;
    say("enter");
    if (probe->enable_on_enter)
        enable(probe, true);
}
static void text_leave(void *data, struct zwp_text_input_v3 *text_input,
                       struct wl_surface *surface) {
    struct probe *probe = data;
    probe->entered = false;
    say("leave");
}
static void text_preedit(void *data, struct zwp_text_input_v3 *text_input, const char *text,
                         int32_t begin, int32_t end) {
    say("preedit %s %d %d", text ? text : "-", begin, end);
}
static void text_commit(void *data, struct zwp_text_input_v3 *text_input, const char *text) {
    say("commit %s", text ? text : "-");
}
static void text_delete(void *data, struct zwp_text_input_v3 *text_input, uint32_t before,
                        uint32_t after) {
    say("delete %u %u", before, after);
}
static void text_done(void *data, struct zwp_text_input_v3 *text_input, uint32_t serial) {
    say("done");
}
static const struct zwp_text_input_v3_listener text_input_listener = {
    .enter = text_enter,
    .leave = text_leave,
    .preedit_string = text_preedit,
    .commit_string = text_commit,
    .delete_surrounding_text = text_delete,
    .done = text_done,
};

static void keyboard_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd,
                            uint32_t size) {
    close(fd);
}
static void keyboard_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                           struct wl_surface *surface, struct wl_array *keys) {}
static void keyboard_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                           struct wl_surface *surface) {}
static void keyboard_key(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time,
                         uint32_t key, uint32_t state) {
    say("key %u %s", key, state == WL_KEYBOARD_KEY_STATE_PRESSED ? "pressed" : "released");
}
static void keyboard_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                               uint32_t depressed, uint32_t latched, uint32_t locked,
                               uint32_t group) {}
static void keyboard_repeat_info(void *data, struct wl_keyboard *keyboard, int32_t rate,
                                 int32_t delay) {}
static const struct wl_keyboard_listener keyboard_listener = {.keymap = keyboard_keymap,
                                                              .enter = keyboard_enter,
                                                              .leave = keyboard_leave,
                                                              .key = keyboard_key,
                                                              .modifiers = keyboard_modifiers,
                                                              .repeat_info = keyboard_repeat_info};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities) {
    struct probe *probe = data;
    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !probe->keyboard) {
        probe->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(probe->keyboard, &keyboard_listener, probe);
    }
}
static void seat_name(void *data, struct wl_seat *seat, const char *name) {}
static const struct wl_seat_listener seat_listener = {.capabilities = seat_capabilities,
                                                      .name = seat_name};

static void shell_ping(void *data, struct xdg_wm_base *shell, uint32_t serial) {
    xdg_wm_base_pong(shell, serial);
}
static const struct xdg_wm_base_listener shell_listener = {.ping = shell_ping};

static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface,
                   uint32_t version) {
    struct probe *probe = data;
    if (!strcmp(interface, wl_compositor_interface.name)) {
        probe->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (!strcmp(interface, wl_shm_interface.name)) {
        probe->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (!strcmp(interface, xdg_wm_base_interface.name)) {
        probe->shell = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(probe->shell, &shell_listener, probe);
    } else if (!strcmp(interface, wl_seat_interface.name) && !probe->seat) {
        probe->seat =
            wl_registry_bind(registry, name, &wl_seat_interface, version < 5 ? version : 5);
        wl_seat_add_listener(probe->seat, &seat_listener, probe);
    } else if (!strcmp(interface, zwlr_layer_shell_v1_interface.name)) {
        probe->layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1);
    } else if (!strcmp(interface, zwp_text_input_manager_v3_interface.name)) {
        probe->manager = wl_registry_bind(registry, name, &zwp_text_input_manager_v3_interface, 1);
    }
}
static void global_remove(void *data, struct wl_registry *registry, uint32_t name) {}
static const struct wl_registry_listener registry_listener = {global, global_remove};

/* A buffer of the window's size in one colour. */
static struct wl_buffer *make_buffer(struct probe *probe) {
    size_t size = (size_t)probe->width * probe->height * 4;
    int fd = memfd_create("paw-text-input-probe", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, (off_t)size) != 0)
        die("cannot allocate a buffer");
    uint32_t *pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pixels == MAP_FAILED)
        die("cannot map a buffer");
    for (size_t i = 0; i < size / 4; ++i)
        pixels[i] = 0xfff4f1e8;
    munmap(pixels, size);
    struct wl_shm_pool *pool = wl_shm_create_pool(probe->shm, fd, (int32_t)size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, probe->width, probe->height,
                                                         probe->width * 4, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buffer;
}

static void frame_done(void *data, struct wl_callback *callback, uint32_t time) {
    struct probe *probe = data;
    wl_callback_destroy(callback);
    if (!probe->ready) {
        probe->ready = true;
        say("ready");
    }
}
static const struct wl_callback_listener frame_listener = {.done = frame_done};

static void surface_configure(void *data, struct xdg_surface *surface, uint32_t serial) {
    struct probe *probe = data;
    xdg_surface_ack_configure(surface, serial);
    wl_surface_attach(probe->surface, make_buffer(probe), 0, 0);
    wl_surface_damage_buffer(probe->surface, 0, 0, probe->width, probe->height);
    if (!probe->ready)
        wl_callback_add_listener(wl_surface_frame(probe->surface), &frame_listener, probe);
    wl_surface_commit(probe->surface);
}
static const struct xdg_surface_listener surface_listener = {.configure = surface_configure};

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t width,
                               int32_t height, struct wl_array *states) {
    struct probe *probe = data;
    probe->width = width > 0 ? width : 400;
    probe->height = height > 0 ? height : 300;
}
static void toplevel_close(void *data, struct xdg_toplevel *toplevel) { exit(0); }
static const struct xdg_toplevel_listener toplevel_listener = {.configure = toplevel_configure,
                                                               .close = toplevel_close};

static void layer_configure(void *data, struct zwlr_layer_surface_v1 *layer, uint32_t serial,
                            uint32_t width, uint32_t height) {
    struct probe *probe = data;
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    probe->width = width > 0 && width <= 8192 ? (int)width : 400;
    probe->height = height > 0 && height <= 8192 ? (int)height : 60;
    wl_surface_attach(probe->surface, make_buffer(probe), 0, 0);
    wl_surface_damage_buffer(probe->surface, 0, 0, probe->width, probe->height);
    if (!probe->ready)
        wl_callback_add_listener(wl_surface_frame(probe->surface), &frame_listener, probe);
    wl_surface_commit(probe->surface);
}
static void layer_closed(void *data, struct zwlr_layer_surface_v1 *layer) { exit(0); }
static const struct zwlr_layer_surface_v1_listener layer_listener = {.configure = layer_configure,
                                                                     .closed = layer_closed};

static void run_command(struct probe *probe, char *command) {
    if (!strcmp(command, "enable") || !strcmp(command, "disable")) {
        enable(probe, command[0] == 'e');
    } else if (!strncmp(command, "surrounding ", 12)) {
        snprintf(probe->surrounding, sizeof(probe->surrounding), "%.255s", command + 12);
        enable(probe, true);
    } else if (!strncmp(command, "cursor ", 7)) {
        if (sscanf(command + 7, "%d %d %d %d", &probe->cursor[0], &probe->cursor[1],
                   &probe->cursor[2], &probe->cursor[3]) != 4)
            die("usage: cursor X Y WIDTH HEIGHT");
        enable(probe, true);
    } else {
        die("unknown command");
    }
}

int main(int argc, char **argv) {
    struct probe probe = {.width = 400,
                          .height = 300,
                          .enable_on_enter = true,
                          .surrounding = "hello",
                          .cursor = {40, 50, 2, 20}};
    const char *title = "text input probe";
    bool overlay = false;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--no-enable"))
            probe.enable_on_enter = false;
        else if (!strcmp(argv[i], "--overlay"))
            overlay = true;
        else
            title = argv[i];
    }
    struct wl_display *display = wl_display_connect(NULL);
    if (!display)
        die("cannot connect to the compositor");
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &probe);
    if (wl_display_roundtrip(display) < 0)
        die("registry roundtrip failed");
    if (!probe.compositor || !probe.shm || !probe.shell || !probe.seat)
        die("required globals missing");
    if (!probe.manager)
        die("zwp_text_input_manager_v3 is not offered");
    probe.text_input = zwp_text_input_manager_v3_get_text_input(probe.manager, probe.seat);
    zwp_text_input_v3_add_listener(probe.text_input, &text_input_listener, &probe);
    probe.surface = wl_compositor_create_surface(probe.compositor);
    if (overlay) {
        if (!probe.layer_shell)
            die("zwlr_layer_shell_v1 is not offered");
        probe.layer = zwlr_layer_shell_v1_get_layer_surface(
            probe.layer_shell, probe.surface, NULL, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, title);
        zwlr_layer_surface_v1_add_listener(probe.layer, &layer_listener, &probe);
        zwlr_layer_surface_v1_set_size(probe.layer, 400, 60);
        zwlr_layer_surface_v1_set_anchor(probe.layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP);
        zwlr_layer_surface_v1_set_keyboard_interactivity(probe.layer, 1);
    } else {
        probe.xdg_surface = xdg_wm_base_get_xdg_surface(probe.shell, probe.surface);
        xdg_surface_add_listener(probe.xdg_surface, &surface_listener, &probe);
        probe.toplevel = xdg_surface_get_toplevel(probe.xdg_surface);
        xdg_toplevel_add_listener(probe.toplevel, &toplevel_listener, &probe);
        xdg_toplevel_set_title(probe.toplevel, title);
        xdg_toplevel_set_app_id(probe.toplevel, "text-input-probe");
    }
    wl_surface_commit(probe.surface);
    char pending[512];
    size_t length = 0;
    bool input = true;
    for (;;) {
        while (wl_display_prepare_read(display) != 0)
            wl_display_dispatch_pending(display);
        wl_display_flush(display);
        struct pollfd fds[2] = {{wl_display_get_fd(display), POLLIN, 0},
                                {input ? 0 : -1, POLLIN, 0}};
        int ready = poll(fds, 2, -1);
        if (ready < 0 || !(fds[0].revents & POLLIN))
            wl_display_cancel_read(display);
        else if (wl_display_read_events(display) < 0)
            die("the connection broke");
        if (wl_display_dispatch_pending(display) < 0)
            die("the connection broke");
        if (ready <= 0 || !(fds[1].revents & (POLLIN | POLLHUP)))
            continue;
        ssize_t count = read(0, pending + length, sizeof(pending) - 1 - length);
        if (count <= 0) {
            input = false;
            continue;
        }
        length += (size_t)count;
        pending[length] = '\0';
        char *newline;
        while ((newline = strchr(pending, '\n'))) {
            *newline = '\0';
            run_command(&probe, pending);
            length -= (size_t)(newline + 1 - pending);
            memmove(pending, newline + 1, length + 1);
        }
    }
}
