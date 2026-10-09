// SPDX-License-Identifier: GPL-3.0-or-later
/* A virtual pointer (and keyboard, for modifiers) for tests, which stays connected (a button pressed by a pointer that
 * disconnects is released) and reads commands, one line at a time, from standard input. It
 * answers each line with "done" once the compositor has taken it. Commands:
 *   move X Y     jump to X, Y, of the WIDTH x HEIGHT given on the command line
 *   press B      press a button: left, middle or right
 *   release B
 *   click B      press and release
 *   scroll N     a vertical wheel movement of N (15 is one notch)
 *   sweep N      N moves along a zigzag over the whole area, one round trip at the end
 *   key NAME down|up   hold or release a modifier: shift, ctrl, alt or super
 *   sleep MS
 * A line may hold several commands. Usage: pointer_probe WIDTH HEIGHT */
#define _GNU_SOURCE
#include "virtual-keyboard-client-protocol.h"
#include "wlr-virtual-pointer-client-protocol.h"
#include <linux/input-event-codes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

static struct wl_seat *seat;
static struct zwlr_virtual_pointer_manager_v1 *manager;
static struct zwp_virtual_keyboard_manager_v1 *keyboard_manager;
static struct zwp_virtual_keyboard_v1 *keyboard;
static uint32_t depressed; /* the modifiers held on it */

static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface,
                   uint32_t version) {
    if (!strcmp(interface, wl_seat_interface.name))
        seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
    else if (!strcmp(interface, zwp_virtual_keyboard_manager_v1_interface.name))
        keyboard_manager =
            wl_registry_bind(registry, name, &zwp_virtual_keyboard_manager_v1_interface, 1);
    else if (!strcmp(interface, zwlr_virtual_pointer_manager_v1_interface.name))
        manager = wl_registry_bind(registry, name, &zwlr_virtual_pointer_manager_v1_interface,
                                   version < 2 ? version : 2);
}
static void global_remove(void *data, struct wl_registry *registry, uint32_t name) {}
static const struct wl_registry_listener registry_listener = {global, global_remove};

static uint32_t now_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint32_t)(now.tv_sec * 1000 + now.tv_nsec / 1000000);
}

static uint32_t button_code(const char *name) {
    return !strcmp(name, "left") ? BTN_LEFT : !strcmp(name, "middle") ? BTN_MIDDLE : BTN_RIGHT;
}

/* A keymap with only the modifier keys, on the evdev codes of their keys. */
static const char keymap[] =
    "xkb_keymap {\n"
    " xkb_keycodes \"k\" { minimum = 8; maximum = 255;\n"
    "  <LFSH> = 50; <LCTL> = 37; <LALT> = 64; <LWIN> = 133; };\n"
    " xkb_types \"t\" { include \"complete\" };\n"
    " xkb_compat \"c\" { include \"complete\" };\n"
    " xkb_symbols \"s\" {\n"
    "  key <LFSH> { [ Shift_L ] }; key <LCTL> { [ Control_L ] };\n"
    "  key <LALT> { [ Alt_L ] }; key <LWIN> { [ Super_L ] };\n"
    "  modifier_map Shift { <LFSH> }; modifier_map Control { <LCTL> };\n"
    "  modifier_map Mod1 { <LALT> }; modifier_map Mod4 { <LWIN> }; };\n"
    "};\n";

static void make_keyboard(struct wl_display *display) {
    if (!keyboard_manager)
        return;
    keyboard = zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(keyboard_manager, seat);
    int fd = memfd_create("paw-test-keymap", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, sizeof(keymap)) != 0 || write(fd, keymap, sizeof(keymap)) < 0) {
        fprintf(stderr, "cannot make the keymap\n");
        exit(1);
    }
    zwp_virtual_keyboard_v1_keymap(keyboard, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, sizeof(keymap));
    close(fd);
    wl_display_roundtrip(display);
}

static void run(struct wl_display *display, struct zwlr_virtual_pointer_v1 *pointer,
                uint32_t width, uint32_t height, int argc, char **argv) {
    for (int i = 0; i < argc; ++i) {
        const char *command = argv[i];
        if (!strcmp(command, "move") && i + 2 < argc) {
            zwlr_virtual_pointer_v1_motion_absolute(pointer, now_ms(),
                                                    (uint32_t)atoi(argv[i + 1]),
                                                    (uint32_t)atoi(argv[i + 2]), width, height);
            zwlr_virtual_pointer_v1_frame(pointer);
            i += 2;
        } else if ((!strcmp(command, "press") || !strcmp(command, "release") ||
                    !strcmp(command, "click")) && i + 1 < argc) {
            uint32_t code = button_code(argv[++i]);
            bool press = strcmp(command, "release") != 0, release = strcmp(command, "press") != 0;
            if (press) {
                zwlr_virtual_pointer_v1_button(pointer, now_ms(), code,
                                               WL_POINTER_BUTTON_STATE_PRESSED);
                zwlr_virtual_pointer_v1_frame(pointer);
            }
            if (release) {
                zwlr_virtual_pointer_v1_button(pointer, now_ms(), code,
                                               WL_POINTER_BUTTON_STATE_RELEASED);
                zwlr_virtual_pointer_v1_frame(pointer);
            }
        } else if (!strcmp(command, "scroll") && i + 1 < argc) {
            zwlr_virtual_pointer_v1_axis_source(pointer, WL_POINTER_AXIS_SOURCE_WHEEL);
            zwlr_virtual_pointer_v1_axis(pointer, now_ms(), WL_POINTER_AXIS_VERTICAL_SCROLL,
                                         wl_fixed_from_int(atoi(argv[++i])));
            zwlr_virtual_pointer_v1_frame(pointer);
        } else if (!strcmp(command, "sweep") && i + 1 < argc) {
            int steps = atoi(argv[++i]);
            for (int step = 0; step < steps; ++step) {
                uint32_t x = (uint32_t)((step * 37) % (int)width);
                uint32_t y = (uint32_t)((step * 19 + (step / 50) * 7) % (int)height);
                zwlr_virtual_pointer_v1_motion_absolute(pointer, now_ms(), x, y, width, height);
                zwlr_virtual_pointer_v1_frame(pointer);
            }
        } else if (!strcmp(command, "key") && i + 2 < argc) {
            // The virtual keyboard protocol has the client say which modifiers are down.
            const char *name = argv[i + 1];
            uint32_t bit = !strcmp(name, "shift") ? 1 : !strcmp(name, "ctrl") ? 4
                           : !strcmp(name, "alt") ? 8 : 64;
            if (!keyboard)
                make_keyboard(display); // only tests that press keys get a keyboard
            if (keyboard) {
                depressed = !strcmp(argv[i + 2], "down") ? depressed | bit : depressed & ~bit;
                zwp_virtual_keyboard_v1_modifiers(keyboard, depressed, 0, 0, 0);
            }
            i += 2;
        } else if (!strcmp(command, "sleep") && i + 1 < argc) {
            wl_display_roundtrip(display);
            usleep((useconds_t)atoi(argv[++i]) * 1000);
        } else {
            fprintf(stderr, "bad command: %s\n", command);
        }
        wl_display_roundtrip(display);
    }
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: pointer_probe WIDTH HEIGHT\n");
        return 2;
    }
    uint32_t width = (uint32_t)atoi(argv[1]), height = (uint32_t)atoi(argv[2]);
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "no display\n");
        return 1;
    }
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);
    if (!manager) {
        fprintf(stderr, "no virtual pointer manager\n");
        return 1;
    }
    struct zwlr_virtual_pointer_v1 *pointer =
        zwlr_virtual_pointer_manager_v1_create_virtual_pointer(manager, seat);
    wl_display_roundtrip(display);
    puts("ready");
    fflush(stdout);
    char line[512];
    while (fgets(line, sizeof(line), stdin)) {
        char *words[64];
        int count = 0;
        for (char *word = strtok(line, " \t\n"); word && count < 64; word = strtok(NULL, " \t\n"))
            words[count++] = word;
        run(display, pointer, width, height, count, words);
        puts("done");
        fflush(stdout);
    }
    zwlr_virtual_pointer_v1_destroy(pointer);
    wl_display_roundtrip(display);
    wl_display_disconnect(display);
    return 0;
}
