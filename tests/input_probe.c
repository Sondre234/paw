// SPDX-License-Identifier: GPL-3.0-or-later
/* A window that prints the input it gets, a line per event, for the input tests: the pointer
 * entering, moving and pressing buttons over it, touchpad gestures through
 * pointer-gestures-unstable-v1, fingers on a touchscreen through wl_touch, and drawing tablets'
 * tools and pads through tablet-v2. It prints "ready" once it has drawn, and runs until it is
 * ended.
 *   pointer enter X Y | pointer leave | pointer motion X Y | pointer button CODE pressed|released
 *   swipe begin FINGERS | swipe update DX DY | swipe end CANCELLED
 *   pinch begin FINGERS | pinch update DX DY SCALE ROTATION | pinch end CANCELLED
 *   hold begin FINGERS | hold end CANCELLED
 *   touch down ID X Y | touch motion ID X Y | touch up ID | touch frame | touch cancel
 *   tool in TYPE | tool out | tool down | tool up | tool motion X Y | tool pressure P |
 *   tool distance D | tool tilt X Y | tool rotation DEGREES | tool slider P |
 *   tool wheel DEGREES CLICKS | tool button CODE pressed|released | tool frame
 *   pad enter | pad leave | pad button N pressed|released
 * (pressure, distance and the slider in 65535ths). Usage: input_probe [--no-gestures]
 * [--no-touch] [--no-tablet] [--layer] [--overlay] [--keys] [--inhibit] [--commands] [TITLE]:
 * without the gestures, touch or tablets it never binds them, as most applications; with --layer
 * it is a panel along the bottom of the output, 60 pixels high, rather than a window, and with
 * --overlay such a panel on the overlay layer, as the shell's overlays are. --keys prints the
 * keyboard's events too:
 *   keyboard enter | keyboard leave | key CODE pressed|released (an evdev code)
 * --inhibit asks for the compositor's shortcuts as it is ready
 * (keyboard-shortcuts-inhibit-unstable-v1), as a virtual machine's window does, and prints what
 * the compositor says of it:
 *   shortcuts active | shortcuts inactive
 * --commands reads lines on standard input: "inhibit" asks for the shortcuts, "release" lets
 * them go (destroying the inhibitor). */
#define _GNU_SOURCE
#include "keyboard-shortcuts-inhibit-unstable-v1-client-protocol.h"
#include "pointer-gestures-unstable-v1-client-protocol.h"
#include "tablet-v2-client-protocol.h"
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
    struct wl_pointer *pointer;
    struct zwp_pointer_gestures_v1 *gestures;
    uint32_t gestures_version;
    bool want_gestures;
    struct wl_touch *touch;
    bool want_touch;
    struct zwp_tablet_manager_v2 *tablets;
    bool want_tablet;
    struct zwlr_layer_shell_v1 *layer_shell;
    struct zwlr_layer_surface_v1 *layer;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    int width, height;
    bool ready;
    bool want_keys;
    struct wl_keyboard *keyboard;
    struct zwp_keyboard_shortcuts_inhibit_manager_v1 *inhibit_manager;
    struct zwp_keyboard_shortcuts_inhibitor_v1 *inhibitor;
    bool inhibit; // --inhibit: ask for the shortcuts once ready
};

static void die(const char *message) {
    fprintf(stderr, "input probe: %s\n", message);
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

static void pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y) {
    say("pointer enter %.0f %.0f", wl_fixed_to_double(x), wl_fixed_to_double(y));
}
static void pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface) {
    say("pointer leave");
}
static void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t x,
                           wl_fixed_t y) {
    say("pointer motion %.0f %.0f", wl_fixed_to_double(x), wl_fixed_to_double(y));
}
static void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time,
                           uint32_t button, uint32_t state) {
    say("pointer button %u %s", button,
        state == WL_POINTER_BUTTON_STATE_PRESSED ? "pressed" : "released");
}
static void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis,
                         wl_fixed_t value) {}
static void pointer_frame(void *data, struct wl_pointer *pointer) {}
static void pointer_axis_source(void *data, struct wl_pointer *pointer, uint32_t source) {}
static void pointer_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time,
                              uint32_t axis) {}
static void pointer_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis,
                                  int32_t discrete) {}
static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
    .frame = pointer_frame,
    .axis_source = pointer_axis_source,
    .axis_stop = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete,
};

static void swipe_begin(void *data, struct zwp_pointer_gesture_swipe_v1 *swipe, uint32_t serial,
                        uint32_t time, struct wl_surface *surface, uint32_t fingers) {
    say("swipe begin %u", fingers);
}
static void swipe_update(void *data, struct zwp_pointer_gesture_swipe_v1 *swipe, uint32_t time,
                         wl_fixed_t dx, wl_fixed_t dy) {
    say("swipe update %.1f %.1f", wl_fixed_to_double(dx), wl_fixed_to_double(dy));
}
static void swipe_end(void *data, struct zwp_pointer_gesture_swipe_v1 *swipe, uint32_t serial,
                      uint32_t time, int32_t cancelled) {
    say("swipe end %d", cancelled);
}
static const struct zwp_pointer_gesture_swipe_v1_listener swipe_listener = {
    .begin = swipe_begin, .update = swipe_update, .end = swipe_end};

static void pinch_begin(void *data, struct zwp_pointer_gesture_pinch_v1 *pinch, uint32_t serial,
                        uint32_t time, struct wl_surface *surface, uint32_t fingers) {
    say("pinch begin %u", fingers);
}
static void pinch_update(void *data, struct zwp_pointer_gesture_pinch_v1 *pinch, uint32_t time,
                         wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t scale, wl_fixed_t rotation) {
    say("pinch update %.1f %.1f %.2f %.1f", wl_fixed_to_double(dx), wl_fixed_to_double(dy),
        wl_fixed_to_double(scale), wl_fixed_to_double(rotation));
}
static void pinch_end(void *data, struct zwp_pointer_gesture_pinch_v1 *pinch, uint32_t serial,
                      uint32_t time, int32_t cancelled) {
    say("pinch end %d", cancelled);
}
static const struct zwp_pointer_gesture_pinch_v1_listener pinch_listener = {
    .begin = pinch_begin, .update = pinch_update, .end = pinch_end};

static void hold_begin(void *data, struct zwp_pointer_gesture_hold_v1 *hold, uint32_t serial,
                       uint32_t time, struct wl_surface *surface, uint32_t fingers) {
    say("hold begin %u", fingers);
}
static void hold_end(void *data, struct zwp_pointer_gesture_hold_v1 *hold, uint32_t serial,
                     uint32_t time, int32_t cancelled) {
    say("hold end %d", cancelled);
}
static const struct zwp_pointer_gesture_hold_v1_listener hold_listener = {.begin = hold_begin,
                                                                          .end = hold_end};

static void touch_down(void *data, struct wl_touch *touch, uint32_t serial, uint32_t time,
                       struct wl_surface *surface, int32_t id, wl_fixed_t x, wl_fixed_t y) {
    say("touch down %d %.0f %.0f", id, wl_fixed_to_double(x), wl_fixed_to_double(y));
}
static void touch_up(void *data, struct wl_touch *touch, uint32_t serial, uint32_t time,
                     int32_t id) {
    say("touch up %d", id);
}
static void touch_motion(void *data, struct wl_touch *touch, uint32_t time, int32_t id,
                         wl_fixed_t x, wl_fixed_t y) {
    say("touch motion %d %.0f %.0f", id, wl_fixed_to_double(x), wl_fixed_to_double(y));
}
static void touch_frame(void *data, struct wl_touch *touch) {
    say("touch frame");
}
static void touch_cancel(void *data, struct wl_touch *touch) {
    say("touch cancel");
}
static const struct wl_touch_listener touch_listener = {.down = touch_down,
                                                        .up = touch_up,
                                                        .motion = touch_motion,
                                                        .frame = touch_frame,
                                                        .cancel = touch_cancel};

static const char *tool_type(uint32_t type) {
    switch (type) {
    case ZWP_TABLET_TOOL_V2_TYPE_PEN:
        return "pen";
    case ZWP_TABLET_TOOL_V2_TYPE_ERASER:
        return "eraser";
    default:
        return "other";
    }
}
static void tool_type_event(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t type) {
    zwp_tablet_tool_v2_set_user_data(tool, (void *)(uintptr_t)type);
}
static void tool_serial(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t high, uint32_t low) {}
static void tool_wacom(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t high, uint32_t low) {}
static void tool_capability(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t capability) {}
static void tool_done(void *data, struct zwp_tablet_tool_v2 *tool) {}
static void tool_removed(void *data, struct zwp_tablet_tool_v2 *tool) {
    zwp_tablet_tool_v2_destroy(tool);
}
static void tool_proximity_in(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t serial,
                              struct zwp_tablet_v2 *tablet, struct wl_surface *surface) {
    say("tool in %s", tool_type((uint32_t)(uintptr_t)zwp_tablet_tool_v2_get_user_data(tool)));
}
static void tool_proximity_out(void *data, struct zwp_tablet_tool_v2 *tool) {
    say("tool out");
}
static void tool_down(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t serial) {
    say("tool down");
}
static void tool_up(void *data, struct zwp_tablet_tool_v2 *tool) {
    say("tool up");
}
static void tool_motion(void *data, struct zwp_tablet_tool_v2 *tool, wl_fixed_t x, wl_fixed_t y) {
    say("tool motion %.0f %.0f", wl_fixed_to_double(x), wl_fixed_to_double(y));
}
static void tool_pressure(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t pressure) {
    say("tool pressure %u", pressure);
}
static void tool_distance(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t distance) {
    say("tool distance %u", distance);
}
static void tool_tilt(void *data, struct zwp_tablet_tool_v2 *tool, wl_fixed_t x, wl_fixed_t y) {
    say("tool tilt %.1f %.1f", wl_fixed_to_double(x), wl_fixed_to_double(y));
}
static void tool_rotation(void *data, struct zwp_tablet_tool_v2 *tool, wl_fixed_t degrees) {
    say("tool rotation %.1f", wl_fixed_to_double(degrees));
}
static void tool_slider(void *data, struct zwp_tablet_tool_v2 *tool, int32_t position) {
    say("tool slider %d", position);
}
static void tool_wheel(void *data, struct zwp_tablet_tool_v2 *tool, wl_fixed_t degrees,
                       int32_t clicks) {
    say("tool wheel %.1f %d", wl_fixed_to_double(degrees), clicks);
}
static void tool_button(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t serial,
                        uint32_t button, uint32_t state) {
    say("tool button %u %s", button,
        state == ZWP_TABLET_TOOL_V2_BUTTON_STATE_PRESSED ? "pressed" : "released");
}
static void tool_frame(void *data, struct zwp_tablet_tool_v2 *tool, uint32_t time) {
    say("tool frame");
}
static const struct zwp_tablet_tool_v2_listener tool_listener = {
    .type = tool_type_event,
    .hardware_serial = tool_serial,
    .hardware_id_wacom = tool_wacom,
    .capability = tool_capability,
    .done = tool_done,
    .removed = tool_removed,
    .proximity_in = tool_proximity_in,
    .proximity_out = tool_proximity_out,
    .down = tool_down,
    .up = tool_up,
    .motion = tool_motion,
    .pressure = tool_pressure,
    .distance = tool_distance,
    .tilt = tool_tilt,
    .rotation = tool_rotation,
    .slider = tool_slider,
    .wheel = tool_wheel,
    .button = tool_button,
    .frame = tool_frame,
};

static void tablet_name(void *data, struct zwp_tablet_v2 *tablet, const char *name) {}
static void tablet_id(void *data, struct zwp_tablet_v2 *tablet, uint32_t vendor,
                      uint32_t product) {}
static void tablet_path(void *data, struct zwp_tablet_v2 *tablet, const char *path) {}
static void tablet_done(void *data, struct zwp_tablet_v2 *tablet) {}
static void tablet_removed(void *data, struct zwp_tablet_v2 *tablet) {
    zwp_tablet_v2_destroy(tablet);
}
static const struct zwp_tablet_v2_listener tablet_listener = {.name = tablet_name,
                                                              .id = tablet_id,
                                                              .path = tablet_path,
                                                              .done = tablet_done,
                                                              .removed = tablet_removed};

static void pad_group(void *data, struct zwp_tablet_pad_v2 *pad,
                      struct zwp_tablet_pad_group_v2 *group) {
    zwp_tablet_pad_group_v2_destroy(group);
}
static void pad_path(void *data, struct zwp_tablet_pad_v2 *pad, const char *path) {}
static void pad_buttons(void *data, struct zwp_tablet_pad_v2 *pad, uint32_t buttons) {}
static void pad_done(void *data, struct zwp_tablet_pad_v2 *pad) {}
static void pad_button(void *data, struct zwp_tablet_pad_v2 *pad, uint32_t time, uint32_t button,
                       uint32_t state) {
    say("pad button %u %s", button,
        state == ZWP_TABLET_PAD_V2_BUTTON_STATE_PRESSED ? "pressed" : "released");
}
static void pad_enter(void *data, struct zwp_tablet_pad_v2 *pad, uint32_t serial,
                      struct zwp_tablet_v2 *tablet, struct wl_surface *surface) {
    say("pad enter");
}
static void pad_leave(void *data, struct zwp_tablet_pad_v2 *pad, uint32_t serial,
                      struct wl_surface *surface) {
    say("pad leave");
}
static void pad_removed(void *data, struct zwp_tablet_pad_v2 *pad) {
    zwp_tablet_pad_v2_destroy(pad);
}
static const struct zwp_tablet_pad_v2_listener pad_listener = {.group = pad_group,
                                                               .path = pad_path,
                                                               .buttons = pad_buttons,
                                                               .done = pad_done,
                                                               .button = pad_button,
                                                               .enter = pad_enter,
                                                               .leave = pad_leave,
                                                               .removed = pad_removed};

static void tablet_added(void *data, struct zwp_tablet_seat_v2 *seat,
                         struct zwp_tablet_v2 *tablet) {
    zwp_tablet_v2_add_listener(tablet, &tablet_listener, data);
}
static void tool_added(void *data, struct zwp_tablet_seat_v2 *seat,
                       struct zwp_tablet_tool_v2 *tool) {
    zwp_tablet_tool_v2_add_listener(tool, &tool_listener, data);
}
static void pad_added(void *data, struct zwp_tablet_seat_v2 *seat, struct zwp_tablet_pad_v2 *pad) {
    zwp_tablet_pad_v2_add_listener(pad, &pad_listener, data);
}
static const struct zwp_tablet_seat_v2_listener tablet_seat_listener = {
    .tablet_added = tablet_added, .tool_added = tool_added, .pad_added = pad_added};

static void keyboard_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd,
                            uint32_t size) {
    close(fd);
}
static void keyboard_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                           struct wl_surface *surface, struct wl_array *keys) {
    say("keyboard enter");
}
static void keyboard_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial,
                           struct wl_surface *surface) {
    say("keyboard leave");
}
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

static void inhibitor_active(void *data, struct zwp_keyboard_shortcuts_inhibitor_v1 *inhibitor) {
    say("shortcuts active");
}
static void inhibitor_inactive(void *data, struct zwp_keyboard_shortcuts_inhibitor_v1 *inhibitor) {
    say("shortcuts inactive");
}
static const struct zwp_keyboard_shortcuts_inhibitor_v1_listener inhibitor_listener = {
    .active = inhibitor_active, .inactive = inhibitor_inactive};

/* Asks for the compositor's shortcuts on the probe's surface, or lets them go. */
static void inhibit(struct probe *probe, bool on) {
    if (on && !probe->inhibitor) {
        probe->inhibitor = zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts(
            probe->inhibit_manager, probe->surface, probe->seat);
        zwp_keyboard_shortcuts_inhibitor_v1_add_listener(probe->inhibitor, &inhibitor_listener,
                                                         probe);
    } else if (!on && probe->inhibitor) {
        zwp_keyboard_shortcuts_inhibitor_v1_destroy(probe->inhibitor);
        probe->inhibitor = NULL;
    }
}

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities) {
    struct probe *probe = data;
    if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && !probe->keyboard && probe->want_keys) {
        probe->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(probe->keyboard, &keyboard_listener, probe);
    }
    if ((capabilities & WL_SEAT_CAPABILITY_TOUCH) && !probe->touch && probe->want_touch) {
        probe->touch = wl_seat_get_touch(seat);
        wl_touch_add_listener(probe->touch, &touch_listener, probe);
    } else if (!(capabilities & WL_SEAT_CAPABILITY_TOUCH) && probe->touch) {
        wl_touch_release(probe->touch);
        probe->touch = NULL;
    }
    if ((capabilities & WL_SEAT_CAPABILITY_POINTER) && !probe->pointer) {
        probe->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(probe->pointer, &pointer_listener, probe);
        if (probe->gestures && probe->want_gestures) {
            zwp_pointer_gesture_swipe_v1_add_listener(
                zwp_pointer_gestures_v1_get_swipe_gesture(probe->gestures, probe->pointer),
                &swipe_listener, probe);
            zwp_pointer_gesture_pinch_v1_add_listener(
                zwp_pointer_gestures_v1_get_pinch_gesture(probe->gestures, probe->pointer),
                &pinch_listener, probe);
            if (probe->gestures_version >= 3)
                zwp_pointer_gesture_hold_v1_add_listener(
                    zwp_pointer_gestures_v1_get_hold_gesture(probe->gestures, probe->pointer),
                    &hold_listener, probe);
        }
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
        probe->seat = wl_registry_bind(registry, name, &wl_seat_interface, version < 5 ? version : 5);
        wl_seat_add_listener(probe->seat, &seat_listener, probe);
    } else if (!strcmp(interface, zwp_tablet_manager_v2_interface.name)) {
        probe->tablets = wl_registry_bind(registry, name, &zwp_tablet_manager_v2_interface, 1);
    } else if (!strcmp(interface, zwlr_layer_shell_v1_interface.name)) {
        probe->layer_shell = wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, 1);
    } else if (!strcmp(interface, zwp_keyboard_shortcuts_inhibit_manager_v1_interface.name)) {
        probe->inhibit_manager = wl_registry_bind(
            registry, name, &zwp_keyboard_shortcuts_inhibit_manager_v1_interface, 1);
    } else if (!strcmp(interface, zwp_pointer_gestures_v1_interface.name)) {
        probe->gestures_version = version < 3 ? version : 3;
        probe->gestures = wl_registry_bind(registry, name, &zwp_pointer_gestures_v1_interface,
                                           probe->gestures_version);
    }
}
static void global_remove(void *data, struct wl_registry *registry, uint32_t name) {}
static const struct wl_registry_listener registry_listener = {global, global_remove};

/* A buffer of the window's size in one colour. */
static struct wl_buffer *make_buffer(struct probe *probe) {
    size_t size = (size_t)probe->width * probe->height * 4;
    int fd = memfd_create("paw-input-probe", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, (off_t)size) != 0)
        die("cannot allocate a buffer");
    uint32_t *pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pixels == MAP_FAILED)
        die("cannot map a buffer");
    for (size_t i = 0; i < size / 4; ++i)
        pixels[i] = 0xff6a8f3c;
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
        if (probe->inhibit)
            inhibit(probe, true);
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

int main(int argc, char **argv) {
    struct probe probe = {
        .want_gestures = true, .want_touch = true, .want_tablet = true, .width = 400, .height = 300};
    const char *title = "input probe";
    bool layer = false, commands = false;
    uint32_t level = ZWLR_LAYER_SHELL_V1_LAYER_TOP;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--no-gestures"))
            probe.want_gestures = false;
        else if (!strcmp(argv[i], "--no-touch"))
            probe.want_touch = false;
        else if (!strcmp(argv[i], "--no-tablet"))
            probe.want_tablet = false;
        else if (!strcmp(argv[i], "--layer"))
            layer = true;
        else if (!strcmp(argv[i], "--overlay")) {
            layer = true;
            level = ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY;
        }
        else if (!strcmp(argv[i], "--keys"))
            probe.want_keys = true;
        else if (!strcmp(argv[i], "--inhibit"))
            probe.inhibit = true;
        else if (!strcmp(argv[i], "--commands"))
            commands = true;
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
    if (probe.want_gestures && !probe.gestures)
        die("zwp_pointer_gestures_v1 is not offered");
    if (probe.want_tablet && !probe.tablets)
        die("zwp_tablet_manager_v2 is not offered");
    if ((probe.inhibit || commands) && !probe.inhibit_manager)
        die("zwp_keyboard_shortcuts_inhibit_manager_v1 is not offered");
    if (probe.want_tablet)
        zwp_tablet_seat_v2_add_listener(
            zwp_tablet_manager_v2_get_tablet_seat(probe.tablets, probe.seat),
            &tablet_seat_listener, &probe);
    probe.surface = wl_compositor_create_surface(probe.compositor);
    if (layer) {
        if (!probe.layer_shell)
            die("zwlr_layer_shell_v1 is not offered");
        probe.layer = zwlr_layer_shell_v1_get_layer_surface(
            probe.layer_shell, probe.surface, NULL, level, title);
        zwlr_layer_surface_v1_add_listener(probe.layer, &layer_listener, &probe);
        zwlr_layer_surface_v1_set_size(probe.layer, 0, 60);
        zwlr_layer_surface_v1_set_anchor(probe.layer, ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                                          ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                                          ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
        zwlr_layer_surface_v1_set_exclusive_zone(probe.layer, 60);
    } else {
        probe.xdg_surface = xdg_wm_base_get_xdg_surface(probe.shell, probe.surface);
        xdg_surface_add_listener(probe.xdg_surface, &surface_listener, &probe);
        probe.toplevel = xdg_surface_get_toplevel(probe.xdg_surface);
        xdg_toplevel_add_listener(probe.toplevel, &toplevel_listener, &probe);
        xdg_toplevel_set_title(probe.toplevel, title);
        xdg_toplevel_set_app_id(probe.toplevel, "input-probe");
    }
    wl_surface_commit(probe.surface);
    char pending[256];
    size_t length = 0;
    for (;;) {
        while (wl_display_prepare_read(display) != 0)
            wl_display_dispatch_pending(display);
        wl_display_flush(display);
        struct pollfd fds[2] = {{wl_display_get_fd(display), POLLIN, 0},
                                {commands ? 0 : -1, POLLIN, 0}};
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
            commands = false;
            continue;
        }
        length += (size_t)count;
        pending[length] = '\0';
        char *newline;
        while ((newline = strchr(pending, '\n'))) {
            *newline = '\0';
            if (!strcmp(pending, "inhibit") || !strcmp(pending, "release"))
                inhibit(&probe, !strcmp(pending, "inhibit"));
            else
                die("unknown command");
            length -= (size_t)(newline + 1 - pending);
            memmove(pending, newline + 1, length + 1);
        }
    }
}
