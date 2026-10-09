// SPDX-License-Identifier: GPL-3.0-or-later
/* An input method speaking input-method-unstable-v2, as fcitx5 and ibus do, for the input method
 * tests. It prints what the compositor tells it, a line per event:
 *   ready | unavailable | activate | deactivate | surrounding TEXT CURSOR ANCHOR | cause CAUSE
 *   content_type HINT PURPOSE | done
 *   keymap | key CODE pressed|released | modifiers DEPRESSED LATCHED LOCKED GROUP
 *   repeat RATE DELAY   (its keyboard grab's, with evdev codes)
 *   rectangle X Y WIDTH HEIGHT   (where the text cursor is from its popup)
 * and does what lines on standard input say, each committed with the serial of the last done:
 *   commit TEXT | preedit TEXT BEGIN END | delete BEFORE AFTER
 *   grab | release   (the keyboard grab, whose keymap its virtual keyboard takes)
 *   forward CODE     (a key pressed and released on its virtual keyboard, as fcitx5 passes on
 *                     the keys it does not use)
 *   popup WIDTH HEIGHT | unpopup   (a popup surface, a candidate window, of that size) */
#define _GNU_SOURCE
#include "input-method-unstable-v2-client-protocol.h"
#include "virtual-keyboard-client-protocol.h"
#include <poll.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

#ifdef __SANITIZE_ADDRESS__
/* A short-lived test client exits without tearing its protocol objects down. */
const char *__asan_default_options(void) { return "detect_leaks=0"; }
#endif

struct probe {
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct zwp_input_method_manager_v2 *manager;
    struct zwp_input_method_v2 *input_method;
    struct zwp_input_method_keyboard_grab_v2 *grab;
    struct zwp_virtual_keyboard_manager_v1 *virtual_manager;
    struct zwp_virtual_keyboard_v1 *keyboard;
    bool keymap_sent;
    struct wl_surface *popup_surface;
    struct zwp_input_popup_surface_v2 *popup;
    uint32_t serial; // done events heard, which a commit names
};

static void die(const char *message) {
    fprintf(stderr, "input method probe: %s\n", message);
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

static void im_activate(void *data, struct zwp_input_method_v2 *input_method) { say("activate"); }
static void im_deactivate(void *data, struct zwp_input_method_v2 *input_method) {
    say("deactivate");
}
static void im_surrounding(void *data, struct zwp_input_method_v2 *input_method, const char *text,
                           uint32_t cursor, uint32_t anchor) {
    say("surrounding %s %u %u", text, cursor, anchor);
}
static void im_cause(void *data, struct zwp_input_method_v2 *input_method, uint32_t cause) {
    say("cause %u", cause);
}
static void im_content_type(void *data, struct zwp_input_method_v2 *input_method, uint32_t hint,
                            uint32_t purpose) {
    say("content_type %u %u", hint, purpose);
}
static void im_done(void *data, struct zwp_input_method_v2 *input_method) {
    struct probe *probe = data;
    ++probe->serial;
    say("done");
}
static void im_unavailable(void *data, struct zwp_input_method_v2 *input_method) {
    say("unavailable");
}
static const struct zwp_input_method_v2_listener input_method_listener = {
    .activate = im_activate,
    .deactivate = im_deactivate,
    .surrounding_text = im_surrounding,
    .text_change_cause = im_cause,
    .content_type = im_content_type,
    .done = im_done,
    .unavailable = im_unavailable,
};

/* The grab's keymap goes to the virtual keyboard too, as fcitx5 does, so that the keys it passes
 * on mean the same. */
static void grab_keymap(void *data, struct zwp_input_method_keyboard_grab_v2 *grab, uint32_t format,
                        int32_t fd, uint32_t size) {
    struct probe *probe = data;
    say("keymap");
    if (probe->keyboard) {
        zwp_virtual_keyboard_v1_keymap(probe->keyboard, format, fd, size);
        probe->keymap_sent = true;
    }
    close(fd);
}
static void grab_key(void *data, struct zwp_input_method_keyboard_grab_v2 *grab, uint32_t serial,
                     uint32_t time, uint32_t key, uint32_t state) {
    say("key %u %s", key, state == WL_KEYBOARD_KEY_STATE_PRESSED ? "pressed" : "released");
}
static void grab_modifiers(void *data, struct zwp_input_method_keyboard_grab_v2 *grab,
                           uint32_t serial, uint32_t depressed, uint32_t latched, uint32_t locked,
                           uint32_t group) {
    say("modifiers %u %u %u %u", depressed, latched, locked, group);
}
static void grab_repeat(void *data, struct zwp_input_method_keyboard_grab_v2 *grab, int32_t rate,
                        int32_t delay) {
    say("repeat %d %d", rate, delay);
}
static const struct zwp_input_method_keyboard_grab_v2_listener grab_listener = {
    .keymap = grab_keymap,
    .key = grab_key,
    .modifiers = grab_modifiers,
    .repeat_info = grab_repeat,
};

static void popup_rectangle(void *data, struct zwp_input_popup_surface_v2 *popup, int32_t x,
                            int32_t y, int32_t width, int32_t height) {
    say("rectangle %d %d %d %d", x, y, width, height);
}
static const struct zwp_input_popup_surface_v2_listener popup_listener = {.text_input_rectangle =
                                                                              popup_rectangle};

static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface,
                   uint32_t version) {
    struct probe *probe = data;
    if (!strcmp(interface, wl_compositor_interface.name))
        probe->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    else if (!strcmp(interface, wl_shm_interface.name))
        probe->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    else if (!strcmp(interface, wl_seat_interface.name) && !probe->seat)
        probe->seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
    else if (!strcmp(interface, zwp_input_method_manager_v2_interface.name))
        probe->manager =
            wl_registry_bind(registry, name, &zwp_input_method_manager_v2_interface, 1);
    else if (!strcmp(interface, zwp_virtual_keyboard_manager_v1_interface.name))
        probe->virtual_manager =
            wl_registry_bind(registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1);
}
static void global_remove(void *data, struct wl_registry *registry, uint32_t name) {}
static const struct wl_registry_listener registry_listener = {global, global_remove};

/* A buffer of that size in one colour. */
static struct wl_buffer *make_buffer(struct probe *probe, int width, int height) {
    size_t size = (size_t)width * height * 4;
    int fd = memfd_create("paw-input-method-probe", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, (off_t)size) != 0)
        die("cannot allocate a buffer");
    uint32_t *pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pixels == MAP_FAILED)
        die("cannot map a buffer");
    for (size_t i = 0; i < size / 4; ++i)
        pixels[i] = 0xff2a5bd7;
    munmap(pixels, size);
    struct wl_shm_pool *pool = wl_shm_create_pool(probe->shm, fd, (int32_t)size);
    struct wl_buffer *buffer =
        wl_shm_pool_create_buffer(pool, 0, width, height, width * 4, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buffer;
}

static uint32_t now_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint32_t)(now.tv_sec * 1000 + now.tv_nsec / 1000000);
}

static void run_command(struct probe *probe, char *command) {
    char text[256];
    int first = 0, second = 0;
    if (!strncmp(command, "commit ", 7)) {
        zwp_input_method_v2_commit_string(probe->input_method, command + 7);
        zwp_input_method_v2_commit(probe->input_method, probe->serial);
    } else if (sscanf(command, "preedit %255s %d %d", text, &first, &second) == 3) {
        zwp_input_method_v2_set_preedit_string(probe->input_method, text, first, second);
        zwp_input_method_v2_commit(probe->input_method, probe->serial);
    } else if (sscanf(command, "delete %d %d", &first, &second) == 2) {
        zwp_input_method_v2_delete_surrounding_text(probe->input_method, (uint32_t)first,
                                                    (uint32_t)second);
        zwp_input_method_v2_commit(probe->input_method, probe->serial);
    } else if (!strcmp(command, "grab") && !probe->grab) {
        probe->grab = zwp_input_method_v2_grab_keyboard(probe->input_method);
        zwp_input_method_keyboard_grab_v2_add_listener(probe->grab, &grab_listener, probe);
    } else if (!strcmp(command, "release") && probe->grab) {
        zwp_input_method_keyboard_grab_v2_release(probe->grab);
        probe->grab = NULL;
    } else if (sscanf(command, "forward %d", &first) == 1) {
        if (!probe->keymap_sent)
            die("forward needs the grab's keymap first");
        zwp_virtual_keyboard_v1_key(probe->keyboard, now_ms(), (uint32_t)first,
                                    WL_KEYBOARD_KEY_STATE_PRESSED);
        zwp_virtual_keyboard_v1_key(probe->keyboard, now_ms(), (uint32_t)first,
                                    WL_KEYBOARD_KEY_STATE_RELEASED);
    } else if (sscanf(command, "popup %d %d", &first, &second) == 2 && !probe->popup) {
        probe->popup_surface = wl_compositor_create_surface(probe->compositor);
        probe->popup =
            zwp_input_method_v2_get_input_popup_surface(probe->input_method, probe->popup_surface);
        zwp_input_popup_surface_v2_add_listener(probe->popup, &popup_listener, probe);
        wl_surface_attach(probe->popup_surface, make_buffer(probe, first, second), 0, 0);
        wl_surface_damage_buffer(probe->popup_surface, 0, 0, first, second);
        wl_surface_commit(probe->popup_surface);
    } else if (!strcmp(command, "unpopup") && probe->popup) {
        zwp_input_popup_surface_v2_destroy(probe->popup);
        wl_surface_destroy(probe->popup_surface);
        probe->popup = NULL;
        probe->popup_surface = NULL;
    } else {
        die("unknown command, or one out of turn");
    }
}

int main(void) {
    struct probe probe = {0};
    struct wl_display *display = wl_display_connect(NULL);
    if (!display)
        die("cannot connect to the compositor");
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &probe);
    if (wl_display_roundtrip(display) < 0)
        die("registry roundtrip failed");
    if (!probe.compositor || !probe.shm || !probe.seat || !probe.virtual_manager)
        die("required globals missing");
    if (!probe.manager)
        die("zwp_input_method_manager_v2 is not offered");
    probe.input_method = zwp_input_method_manager_v2_get_input_method(probe.manager, probe.seat);
    zwp_input_method_v2_add_listener(probe.input_method, &input_method_listener, &probe);
    probe.keyboard =
        zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(probe.virtual_manager, probe.seat);
    if (wl_display_roundtrip(display) < 0)
        die("roundtrip failed");
    say("ready");
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
