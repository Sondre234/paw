/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* Input devices without hardware, for tests under --headless: keyboards, pointers that move and
 * make touchpad gestures, which no virtual-pointer client can send, touchscreens, and drawing
 * tablets with their pads. They are devices like any other once plugged in. */
#include "server.h"
#include <wlr/interfaces/wlr_pointer.h>
#include <wlr/interfaces/wlr_tablet_pad.h>
#include <wlr/interfaces/wlr_tablet_tool.h>
#include <wlr/interfaces/wlr_touch.h>

/* Splits `arguments` into at most `size` words in `words`, each at most 63 bytes; returns how
 * many there were, or size + 1 when there were more. */
static int split_words(const char *arguments, char words[][64], int size) {
    int count = 0;
    const char *at = arguments;
    while (*at) {
        while (*at == ' ')
            ++at;
        if (!*at)
            break;
        size_t length = strcspn(at, " ");
        if (count == size || length >= 64)
            return size + 1;
        snprintf(words[count++], 64, "%.*s", (int)length, at);
        at += length;
    }
    return count;
}

/* A number from a word: false when the word is not wholly one. */
static bool parse_double(const char *word, double *value) {
    char *end = NULL;
    *value = strtod(word, &end);
    return end != word && !*end && isfinite(*value);
}

static bool parse_unsigned(const char *word, unsigned *value) {
    char *end = NULL;
    unsigned long number = strtoul(word, &end, 10);
    if (end == word || *end || word[0] == '-' || number > UINT32_MAX)
        return false;
    *value = (unsigned)number;
    return true;
}

/* The event time a command gives as its last word, else now. */
static bool event_time(char words[][64], int count, int at, uint32_t *time) {
    unsigned given = 0;
    if (count <= at) {
        *time = (uint32_t)now_ms();
        return true;
    }
    if (count > at + 1 || !parse_unsigned(words[at], &given))
        return false;
    *time = given;
    return true;
}

/* Keyboards without a device, so tests can type, plug and unplug under --headless:
 * "headless_keyboard add NAME", "headless_keyboard key NAME CODE press|release" (an evdev key
 * code, as from linux/input-event-codes.h) and "headless_keyboard remove NAME". They are
 * keyboards like any other, not virtual ones. */
struct sh_headless_keyboard {
    struct wlr_keyboard keyboard;
    struct wl_list link; // sh_server.headless_keyboards
};
static const struct wlr_keyboard_impl headless_keyboard_impl = {.name = "headless-keyboard"};

static struct sh_headless_keyboard *find_headless_keyboard(struct sh_server *server,
                                                           const char *name) {
    struct sh_headless_keyboard *keyboard;
    wl_list_for_each(keyboard, &server->headless_keyboards, link) {
        if (!strcmp(keyboard->keyboard.base.name, name))
            return keyboard;
    }
    return NULL;
}

static void remove_headless_keyboard(struct sh_headless_keyboard *keyboard) {
    wl_list_remove(&keyboard->link);
    wlr_keyboard_finish(&keyboard->keyboard); // releases its keys and unplugs it
    free(keyboard);
}

void control_headless_keyboard(struct sh_server *server, int fd, const char *arguments) {
    if (!headless_backend(server)) {
        control_reply(fd, "error: headless_keyboard needs --headless\n");
        return;
    }
    char verb[16] = "", name[64] = "", state[16] = "", extra;
    unsigned code = 0;
    int fields = sscanf(arguments, "%15s %63s %u %15s %c", verb, name, &code, state, &extra);
    struct sh_headless_keyboard *keyboard =
        fields >= 2 ? find_headless_keyboard(server, name) : NULL;
    if (!strcmp(verb, "add") && fields == 2) {
        if (keyboard) {
            control_reply(fd, "error: a keyboard with that name exists\n");
            return;
        }
        keyboard = calloc(1, sizeof(*keyboard));
        if (!keyboard) {
            control_reply(fd, "error: out of memory\n");
            return;
        }
        wlr_keyboard_init(&keyboard->keyboard, &headless_keyboard_impl, name);
        wl_list_insert(&server->headless_keyboards, &keyboard->link);
        server_new_input(&server->new_input, &keyboard->keyboard.base);
        control_reply(fd, "ok\n");
        return;
    }
    if ((!strcmp(verb, "remove") && fields == 2) || (!strcmp(verb, "key") && fields == 4)) {
        bool pressed = !strcmp(state, "press");
        if (!keyboard) {
            control_reply(fd, "error: no such keyboard\n");
        } else if (!strcmp(verb, "remove")) {
            remove_headless_keyboard(keyboard);
            control_reply(fd, "ok\n");
        } else if (code > KEY_MAX || (!pressed && strcmp(state, "release"))) {
            control_reply(fd, "error: usage: headless_keyboard key NAME CODE press|release\n");
        } else {
            struct wlr_keyboard_key_event event = {
                .time_msec = (uint32_t)now_ms(),
                .keycode = code,
                .update_state = true,
                .state = pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED,
            };
            wlr_keyboard_notify_key(&keyboard->keyboard, &event);
            control_reply(fd, "ok\n");
        }
        return;
    }
    control_reply(fd, "error: usage: headless_keyboard add NAME | key NAME CODE press|release | "
                      "remove NAME\n");
}

void destroy_headless_keyboards(struct sh_server *server) {
    struct sh_headless_keyboard *keyboard, *temporary;
    wl_list_for_each_safe(keyboard, temporary, &server->headless_keyboards, link)
        remove_headless_keyboard(keyboard);
}

/* Pointers: "headless_pointer add NAME", "headless_pointer remove NAME",
 * "headless_pointer move NAME X Y" (layout coordinates), and gestures, each with an optional
 * event time in milliseconds last, so that a test can say how fast fingers move:
 *   headless_pointer swipe|pinch|hold NAME begin FINGERS [TIME]
 *   headless_pointer swipe NAME update DX DY [TIME]
 *   headless_pointer pinch NAME update DX DY SCALE ROTATION [TIME]
 *   headless_pointer swipe|pinch|hold NAME end|cancel [TIME] */
struct sh_headless_pointer {
    struct wlr_pointer pointer;
    uint32_t fingers; // of the swipe or pinch under way
    struct wl_list link; // sh_server.headless_pointers
};
static const struct wlr_pointer_impl headless_pointer_impl = {.name = "headless-pointer"};

static struct sh_headless_pointer *find_headless_pointer(struct sh_server *server,
                                                         const char *name) {
    struct sh_headless_pointer *pointer;
    wl_list_for_each(pointer, &server->headless_pointers, link) {
        if (!strcmp(pointer->pointer.base.name, name))
            return pointer;
    }
    return NULL;
}

static void remove_headless_pointer(struct sh_headless_pointer *pointer) {
    wl_list_remove(&pointer->link);
    wlr_pointer_finish(&pointer->pointer); // unplugs it
    free(pointer);
}

/* Runs a gesture command (words from the verb on); false when it is not a valid one. */
static bool headless_gesture(struct sh_headless_pointer *headless, char words[][64], int count) {
    struct wlr_pointer *pointer = &headless->pointer;
    const char *kind = words[0], *step = words[2];
    uint32_t time = 0;
    unsigned fingers = 0;
    if (!strcmp(step, "begin")) {
        if (count < 4 || !parse_unsigned(words[3], &fingers) || fingers < 1 || fingers > 10 ||
            !event_time(words, count, 4, &time))
            return false;
        headless->fingers = fingers;
        if (!strcmp(kind, "swipe")) {
            struct wlr_pointer_swipe_begin_event event = {pointer, time, fingers};
            wl_signal_emit_mutable(&pointer->events.swipe_begin, &event);
        } else if (!strcmp(kind, "pinch")) {
            struct wlr_pointer_pinch_begin_event event = {pointer, time, fingers};
            wl_signal_emit_mutable(&pointer->events.pinch_begin, &event);
        } else {
            struct wlr_pointer_hold_begin_event event = {pointer, time, fingers};
            wl_signal_emit_mutable(&pointer->events.hold_begin, &event);
        }
        return true;
    }
    if (!strcmp(step, "end") || !strcmp(step, "cancel")) {
        if (!event_time(words, count, 3, &time))
            return false;
        bool cancelled = !strcmp(step, "cancel");
        if (!strcmp(kind, "swipe")) {
            struct wlr_pointer_swipe_end_event event = {pointer, time, cancelled};
            wl_signal_emit_mutable(&pointer->events.swipe_end, &event);
        } else if (!strcmp(kind, "pinch")) {
            struct wlr_pointer_pinch_end_event event = {pointer, time, cancelled};
            wl_signal_emit_mutable(&pointer->events.pinch_end, &event);
        } else {
            struct wlr_pointer_hold_end_event event = {pointer, time, cancelled};
            wl_signal_emit_mutable(&pointer->events.hold_end, &event);
        }
        return true;
    }
    double dx, dy, scale, rotation;
    if (strcmp(step, "update") || count < 5 || !parse_double(words[3], &dx) ||
        !parse_double(words[4], &dy))
        return false;
    if (!strcmp(kind, "swipe")) {
        if (!event_time(words, count, 5, &time))
            return false;
        struct wlr_pointer_swipe_update_event event = {pointer, time, headless->fingers, dx, dy};
        wl_signal_emit_mutable(&pointer->events.swipe_update, &event);
        return true;
    }
    if (strcmp(kind, "pinch") || count < 7 || !parse_double(words[5], &scale) ||
        !parse_double(words[6], &rotation) || !event_time(words, count, 7, &time))
        return false;
    struct wlr_pointer_pinch_update_event event = {pointer, time, headless->fingers, dx, dy,
                                                   scale, rotation};
    wl_signal_emit_mutable(&pointer->events.pinch_update, &event);
    return true;
}

void control_headless_pointer(struct sh_server *server, int fd, const char *arguments) {
    static const char *usage =
        "error: usage: headless_pointer add NAME | remove NAME | move NAME X Y | "
        "swipe|pinch|hold NAME begin FINGERS [TIME] | swipe NAME update DX DY [TIME] | "
        "pinch NAME update DX DY SCALE ROTATION [TIME] | swipe|pinch|hold NAME end|cancel "
        "[TIME]\n";
    if (!headless_backend(server)) {
        control_reply(fd, "error: headless_pointer needs --headless\n");
        return;
    }
    char words[8][64];
    int count = split_words(arguments, words, 8);
    if (count < 2 || count > 8) {
        control_reply(fd, usage);
        return;
    }
    const char *verb = words[0], *name = words[1];
    struct sh_headless_pointer *pointer = find_headless_pointer(server, name);
    if (!strcmp(verb, "add") && count == 2) {
        if (pointer) {
            control_reply(fd, "error: a pointer with that name exists\n");
            return;
        }
        pointer = calloc(1, sizeof(*pointer));
        if (!pointer) {
            control_reply(fd, "error: out of memory\n");
            return;
        }
        wlr_pointer_init(&pointer->pointer, &headless_pointer_impl, name);
        wl_list_insert(&server->headless_pointers, &pointer->link);
        server_new_input(&server->new_input, &pointer->pointer.base);
        control_reply(fd, "ok\n");
        return;
    }
    bool known = !strcmp(verb, "remove") || !strcmp(verb, "move") || !strcmp(verb, "swipe") ||
                 !strcmp(verb, "pinch") || !strcmp(verb, "hold");
    if (!known) {
        control_reply(fd, usage);
        return;
    }
    if (!pointer) {
        control_reply(fd, "error: no such pointer\n");
        return;
    }
    if (!strcmp(verb, "remove") && count == 2) {
        remove_headless_pointer(pointer);
        control_reply(fd, "ok\n");
        return;
    }
    if (!strcmp(verb, "move")) {
        double x, y;
        struct wlr_box layout;
        wlr_output_layout_get_box(server->output_layout, NULL, &layout);
        if (count != 4 || !parse_double(words[2], &x) || !parse_double(words[3], &y) ||
            wlr_box_empty(&layout)) {
            control_reply(fd, usage);
            return;
        }
        struct wlr_pointer_motion_absolute_event event = {
            .pointer = &pointer->pointer,
            .time_msec = (uint32_t)now_ms(),
            .x = (x - layout.x) / layout.width,
            .y = (y - layout.y) / layout.height,
        };
        wl_signal_emit_mutable(&pointer->pointer.events.motion_absolute, &event);
        wl_signal_emit_mutable(&pointer->pointer.events.frame, &pointer->pointer);
        control_reply(fd, "ok\n");
        return;
    }
    if (strcmp(verb, "remove") && count >= 3 && headless_gesture(pointer, words, count)) {
        control_reply(fd, "ok\n");
        return;
    }
    control_reply(fd, usage);
}

/* Touchscreens: "headless_touch add NAME [OUTPUT]" (OUTPUT: the output the device names, as
 * libinput's WL_OUTPUT does), "headless_touch remove NAME", and fingers, at the place on the
 * screen from 0 to 1 across and down as libinput gives it, each with an optional event time last:
 *   headless_touch down|motion NAME ID X Y [TIME]
 *   headless_touch up|cancel NAME ID [TIME]
 *   headless_touch frame NAME */
struct sh_headless_touch {
    struct wlr_touch touch;
    struct wl_list link; // sh_server.headless_touches
};
static const struct wlr_touch_impl headless_touch_impl = {.name = "headless-touch"};

static struct sh_headless_touch *find_headless_touch(struct sh_server *server, const char *name) {
    struct sh_headless_touch *touch;
    wl_list_for_each(touch, &server->headless_touches, link) {
        if (!strcmp(touch->touch.base.name, name))
            return touch;
    }
    return NULL;
}

static void remove_headless_touch(struct sh_headless_touch *touch) {
    wl_list_remove(&touch->link);
    wlr_touch_finish(&touch->touch); // unplugs it, and frees its output's name
    free(touch);
}

/* Runs a finger's command (words from the verb on); false when it is not a valid one. */
static bool headless_finger(struct sh_headless_touch *headless, char words[][64], int count) {
    struct wlr_touch *touch = &headless->touch;
    const char *verb = words[0];
    uint32_t time = 0;
    if (!strcmp(verb, "frame")) {
        if (count != 2)
            return false;
        wl_signal_emit_mutable(&touch->events.frame, NULL);
        return true;
    }
    char *end = NULL;
    long id = count >= 3 ? strtol(words[2], &end, 10) : -1;
    if (count < 3 || end == words[2] || *end || id < 0 || id > INT32_MAX)
        return false;
    if (!strcmp(verb, "up") || !strcmp(verb, "cancel")) {
        if (!event_time(words, count, 3, &time))
            return false;
        if (!strcmp(verb, "up")) {
            struct wlr_touch_up_event event = {touch, time, (int32_t)id};
            wl_signal_emit_mutable(&touch->events.up, &event);
        } else {
            struct wlr_touch_cancel_event event = {touch, time, (int32_t)id};
            wl_signal_emit_mutable(&touch->events.cancel, &event);
        }
        return true;
    }
    double x, y;
    if (count < 5 || !parse_double(words[3], &x) || !parse_double(words[4], &y) || x < 0 ||
        x > 1 || y < 0 || y > 1 || !event_time(words, count, 5, &time))
        return false;
    if (!strcmp(verb, "down")) {
        struct wlr_touch_down_event event = {touch, time, (int32_t)id, x, y};
        wl_signal_emit_mutable(&touch->events.down, &event);
    } else {
        struct wlr_touch_motion_event event = {touch, time, (int32_t)id, x, y};
        wl_signal_emit_mutable(&touch->events.motion, &event);
    }
    return true;
}

void control_headless_touch(struct sh_server *server, int fd, const char *arguments) {
    static const char *usage =
        "error: usage: headless_touch add NAME [OUTPUT] | remove NAME | down|motion NAME ID X Y "
        "[TIME] | up|cancel NAME ID [TIME] | frame NAME\n";
    if (!headless_backend(server)) {
        control_reply(fd, "error: headless_touch needs --headless\n");
        return;
    }
    char words[8][64];
    int count = split_words(arguments, words, 8);
    if (count < 2 || count > 8) {
        control_reply(fd, usage);
        return;
    }
    const char *verb = words[0], *name = words[1];
    struct sh_headless_touch *touch = find_headless_touch(server, name);
    if (!strcmp(verb, "add") && count <= 3) {
        if (touch) {
            control_reply(fd, "error: a touchscreen with that name exists\n");
            return;
        }
        touch = calloc(1, sizeof(*touch));
        if (!touch) {
            control_reply(fd, "error: out of memory\n");
            return;
        }
        wlr_touch_init(&touch->touch, &headless_touch_impl, name);
        if (count == 3)
            touch->touch.output_name = strdup(words[2]);
        wl_list_insert(&server->headless_touches, &touch->link);
        server_new_input(&server->new_input, &touch->touch.base);
        control_reply(fd, "ok\n");
        return;
    }
    bool known = !strcmp(verb, "remove") || !strcmp(verb, "down") || !strcmp(verb, "motion") ||
                 !strcmp(verb, "up") || !strcmp(verb, "cancel") || !strcmp(verb, "frame");
    if (!known) {
        control_reply(fd, usage);
        return;
    }
    if (!touch) {
        control_reply(fd, "error: no such touchscreen\n");
        return;
    }
    if (!strcmp(verb, "remove") && count == 2) {
        remove_headless_touch(touch);
        control_reply(fd, "ok\n");
        return;
    }
    if (strcmp(verb, "remove") && headless_finger(touch, words, count)) {
        control_reply(fd, "ok\n");
        return;
    }
    control_reply(fd, usage);
}

/* Drawing tablets: "headless_tablet add NAME" plugs in a tablet with a pen and an eraser, and a
 * pad, NAME-pad, with four buttons, a ring and a strip; "headless_tablet remove NAME" unplugs both.
 * The tools (TOOL: pen or eraser) and the pad act with an optional event time last:
 *   headless_tablet in NAME TOOL X Y [TIME]      the tool comes near, at X, Y from 0 to 1
 *   headless_tablet out NAME TOOL [TIME]
 *   headless_tablet axis NAME TOOL KEY=VALUE...  x, y, pressure, distance, slider (0 to 1, the
 *       slider from -1), tilt=X,Y, rotation and wheel (degrees), time
 *   headless_tablet tip NAME TOOL down|up [TIME]
 *   headless_tablet button NAME TOOL CODE press|release [TIME]
 *   headless_tablet pad NAME button N press|release [TIME]
 *   headless_tablet pad NAME ring|strip N POSITION [TIME] */
struct sh_headless_tablet {
    struct wlr_tablet tablet;
    struct wlr_tablet_pad pad;
    struct wlr_tablet_tool tools[2]; // the pen and the eraser
    double x[2], y[2];               // where each tool last was
    struct wl_list link;             // sh_server.headless_tablets
};
static const struct wlr_tablet_impl headless_tablet_impl = {.name = "headless-tablet"};
static const struct wlr_tablet_pad_impl headless_pad_impl = {.name = "headless-tablet-pad"};

static struct sh_headless_tablet *find_headless_tablet(struct sh_server *server,
                                                       const char *name) {
    struct sh_headless_tablet *tablet;
    wl_list_for_each(tablet, &server->headless_tablets, link) {
        if (!strcmp(tablet->tablet.base.name, name))
            return tablet;
    }
    return NULL;
}

static void remove_headless_tablet(struct sh_headless_tablet *tablet) {
    wl_list_remove(&tablet->link);
    for (int i = 0; i < 2; ++i)
        wl_signal_emit_mutable(&tablet->tools[i].events.destroy, &tablet->tools[i]);
    wlr_tablet_finish(&tablet->tablet);
    wlr_tablet_pad_finish(&tablet->pad);
    free(tablet);
}

static bool add_headless_tablet(struct sh_server *server, const char *name) {
    struct sh_headless_tablet *tablet = calloc(1, sizeof(*tablet));
    if (!tablet)
        return false;
    wlr_tablet_init(&tablet->tablet, &headless_tablet_impl, name);
    char pad_name[80];
    snprintf(pad_name, sizeof(pad_name), "%s-pad", name);
    wlr_tablet_pad_init(&tablet->pad, &headless_pad_impl, pad_name);
    tablet->pad.button_count = 4;
    tablet->pad.ring_count = tablet->pad.strip_count = 1;
    for (int i = 0; i < 2; ++i) {
        struct wlr_tablet_tool *tool = &tablet->tools[i];
        tool->type = i ? WLR_TABLET_TOOL_TYPE_ERASER : WLR_TABLET_TOOL_TYPE_PEN;
        tool->hardware_serial = 0x100 + (uint64_t)i;
        tool->pressure = tool->tilt = tool->distance = true;
        tool->rotation = tool->slider = tool->wheel = !i;
        wl_signal_init(&tool->events.destroy);
    }
    wl_list_insert(&server->headless_tablets, &tablet->link);
    server_new_input(&server->new_input, &tablet->tablet.base);
    server_new_input(&server->new_input, &tablet->pad.base);
    return true;
}

/* An axis event from KEY=VALUE words. */
static bool headless_axis(struct sh_headless_tablet *tablet, int tool, char words[][64], int count,
                          struct wlr_tablet_tool_axis_event *event) {
    event->time_msec = (uint32_t)now_ms();
    for (int i = 3; i < count; ++i) {
        char *equals = strchr(words[i], '=');
        if (!equals)
            return false;
        *equals = '\0';
        const char *key = words[i], *value = equals + 1;
        double number = 0, second = 0;
        unsigned time = 0;
        if (!strcmp(key, "time")) {
            if (!parse_unsigned(value, &time))
                return false;
            event->time_msec = time;
            continue;
        }
        if (!strcmp(key, "tilt")) {
            char first[64];
            const char *comma = strchr(value, ',');
            if (!comma || (size_t)(comma - value) >= sizeof(first))
                return false;
            snprintf(first, sizeof(first), "%.*s", (int)(comma - value), value);
            if (!parse_double(first, &number) || !parse_double(comma + 1, &second))
                return false;
            event->tilt_x = number, event->tilt_y = second;
            event->updated_axes |= WLR_TABLET_TOOL_AXIS_TILT_X | WLR_TABLET_TOOL_AXIS_TILT_Y;
            continue;
        }
        if (!parse_double(value, &number))
            return false;
        if (!strcmp(key, "x") && number >= 0 && number <= 1) {
            tablet->x[tool] = number;
            event->updated_axes |= WLR_TABLET_TOOL_AXIS_X;
        } else if (!strcmp(key, "y") && number >= 0 && number <= 1) {
            tablet->y[tool] = number;
            event->updated_axes |= WLR_TABLET_TOOL_AXIS_Y;
        } else if (!strcmp(key, "pressure") && number >= 0 && number <= 1) {
            event->pressure = number;
            event->updated_axes |= WLR_TABLET_TOOL_AXIS_PRESSURE;
        } else if (!strcmp(key, "distance") && number >= 0 && number <= 1) {
            event->distance = number;
            event->updated_axes |= WLR_TABLET_TOOL_AXIS_DISTANCE;
        } else if (!strcmp(key, "rotation")) {
            event->rotation = number;
            event->updated_axes |= WLR_TABLET_TOOL_AXIS_ROTATION;
        } else if (!strcmp(key, "slider") && number >= -1 && number <= 1) {
            event->slider = number;
            event->updated_axes |= WLR_TABLET_TOOL_AXIS_SLIDER;
        } else if (!strcmp(key, "wheel")) {
            event->wheel_delta = number;
            event->updated_axes |= WLR_TABLET_TOOL_AXIS_WHEEL;
        } else {
            return false;
        }
    }
    event->x = tablet->x[tool];
    event->y = tablet->y[tool];
    return event->updated_axes != 0;
}

/* Runs a tool's or the pad's command (words from the verb on); false when it is not valid. */
static bool headless_tablet_event(struct sh_headless_tablet *tablet, char words[][64], int count) {
    const char *verb = words[0];
    uint32_t time = 0;
    unsigned number = 0;
    if (!strcmp(verb, "pad")) {
        double position = 0;
        if (count < 5 || !parse_unsigned(words[3], &number))
            return false;
        if (!strcmp(words[2], "button")) {
            bool pressed = !strcmp(words[4], "press");
            if (number >= tablet->pad.button_count || (!pressed && strcmp(words[4], "release")) ||
                !event_time(words, count, 5, &time))
                return false;
            struct wlr_tablet_pad_button_event event = {
                .time_msec = time,
                .button = number,
                .state = pressed ? WLR_BUTTON_PRESSED : WLR_BUTTON_RELEASED};
            wl_signal_emit_mutable(&tablet->pad.events.button, &event);
            return true;
        }
        bool ring = !strcmp(words[2], "ring");
        if ((!ring && strcmp(words[2], "strip")) || number > 0 ||
            !parse_double(words[4], &position) || !event_time(words, count, 5, &time))
            return false;
        if (ring) {
            struct wlr_tablet_pad_ring_event event = {
                .time_msec = time, .source = WLR_TABLET_PAD_RING_SOURCE_FINGER, .ring = number,
                .position = position};
            wl_signal_emit_mutable(&tablet->pad.events.ring, &event);
        } else {
            struct wlr_tablet_pad_strip_event event = {
                .time_msec = time, .source = WLR_TABLET_PAD_STRIP_SOURCE_FINGER, .strip = number,
                .position = position};
            wl_signal_emit_mutable(&tablet->pad.events.strip, &event);
        }
        return true;
    }
    if (count < 3 || (strcmp(words[2], "pen") && strcmp(words[2], "eraser")))
        return false;
    int index = !strcmp(words[2], "eraser");
    struct wlr_tablet_tool *tool = &tablet->tools[index];
    if (!strcmp(verb, "in")) {
        double x, y;
        if (count < 5 || !parse_double(words[3], &x) || !parse_double(words[4], &y) || x < 0 ||
            x > 1 || y < 0 || y > 1 || !event_time(words, count, 5, &time))
            return false;
        tablet->x[index] = x, tablet->y[index] = y;
        struct wlr_tablet_tool_proximity_event event = {
            &tablet->tablet, tool, time, x, y, WLR_TABLET_TOOL_PROXIMITY_IN};
        wl_signal_emit_mutable(&tablet->tablet.events.proximity, &event);
        return true;
    }
    if (!strcmp(verb, "out")) {
        if (!event_time(words, count, 3, &time))
            return false;
        struct wlr_tablet_tool_proximity_event event = {
            &tablet->tablet, tool, time, tablet->x[index], tablet->y[index],
            WLR_TABLET_TOOL_PROXIMITY_OUT};
        wl_signal_emit_mutable(&tablet->tablet.events.proximity, &event);
        return true;
    }
    if (!strcmp(verb, "tip")) {
        bool down = count >= 4 && !strcmp(words[3], "down");
        if (count < 4 || (!down && strcmp(words[3], "up")) || !event_time(words, count, 4, &time))
            return false;
        struct wlr_tablet_tool_tip_event event = {
            &tablet->tablet, tool, time, tablet->x[index], tablet->y[index],
            down ? WLR_TABLET_TOOL_TIP_DOWN : WLR_TABLET_TOOL_TIP_UP};
        wl_signal_emit_mutable(&tablet->tablet.events.tip, &event);
        return true;
    }
    if (!strcmp(verb, "button")) {
        bool pressed = count >= 5 && !strcmp(words[4], "press");
        if (count < 5 || !parse_unsigned(words[3], &number) || number > KEY_MAX ||
            (!pressed && strcmp(words[4], "release")) || !event_time(words, count, 5, &time))
            return false;
        struct wlr_tablet_tool_button_event event = {
            &tablet->tablet, tool, time, number, pressed ? WLR_BUTTON_PRESSED : WLR_BUTTON_RELEASED};
        wl_signal_emit_mutable(&tablet->tablet.events.button, &event);
        return true;
    }
    struct wlr_tablet_tool_axis_event event = {.tablet = &tablet->tablet, .tool = tool};
    if (strcmp(verb, "axis") || !headless_axis(tablet, index, words, count, &event))
        return false;
    wl_signal_emit_mutable(&tablet->tablet.events.axis, &event);
    return true;
}

void control_headless_tablet(struct sh_server *server, int fd, const char *arguments) {
    static const char *usage =
        "error: usage: headless_tablet add NAME | remove NAME | in NAME pen|eraser X Y [TIME] | "
        "out NAME pen|eraser [TIME] | axis NAME pen|eraser KEY=VALUE... | tip NAME pen|eraser "
        "down|up [TIME] | button NAME pen|eraser CODE press|release [TIME] | pad NAME button N "
        "press|release [TIME] | pad NAME ring|strip N POSITION [TIME]\n";
    if (!headless_backend(server)) {
        control_reply(fd, "error: headless_tablet needs --headless\n");
        return;
    }
    char words[12][64];
    int count = split_words(arguments, words, 12);
    if (count < 2 || count > 12) {
        control_reply(fd, usage);
        return;
    }
    const char *verb = words[0], *name = words[1];
    struct sh_headless_tablet *tablet = find_headless_tablet(server, name);
    if (!strcmp(verb, "add") && count == 2) {
        if (tablet)
            control_reply(fd, "error: a tablet with that name exists\n");
        else if (strlen(name) > 59)
            control_reply(fd, "error: the name is too long\n");
        else
            control_reply(fd, add_headless_tablet(server, name) ? "ok\n" : "error: out of memory\n");
        return;
    }
    bool known = !strcmp(verb, "remove") || !strcmp(verb, "in") || !strcmp(verb, "out") ||
                 !strcmp(verb, "axis") || !strcmp(verb, "tip") || !strcmp(verb, "button") ||
                 !strcmp(verb, "pad");
    if (!known) {
        control_reply(fd, usage);
        return;
    }
    if (!tablet) {
        control_reply(fd, "error: no such tablet\n");
        return;
    }
    if (!strcmp(verb, "remove") && count == 2) {
        remove_headless_tablet(tablet);
        control_reply(fd, "ok\n");
        return;
    }
    if (strcmp(verb, "remove") && headless_tablet_event(tablet, words, count)) {
        control_reply(fd, "ok\n");
        return;
    }
    control_reply(fd, usage);
}

void destroy_headless_inputs(struct sh_server *server) {
    struct sh_headless_pointer *pointer, *temporary;
    wl_list_for_each_safe(pointer, temporary, &server->headless_pointers, link)
        remove_headless_pointer(pointer);
    struct sh_headless_touch *touch, *next;
    wl_list_for_each_safe(touch, next, &server->headless_touches, link)
        remove_headless_touch(touch);
    struct sh_headless_tablet *tablet, *following;
    wl_list_for_each_safe(tablet, following, &server->headless_tablets, link)
        remove_headless_tablet(tablet);
}
