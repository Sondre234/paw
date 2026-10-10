/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* Input devices: keyboards (key presses matched against the bindings, and repeat; keymap.c
 * gives them their keymap), pointers and their libinput settings, virtual devices, and the
 * seat's selection, drag-and-drop and pointer constraints. */
#include "server.h"

static bool handle_keybinding(struct sh_keyboard *keyboard, uint32_t keycode, uint32_t modifiers,
                              xkb_keysym_t sym);

/* Input from the user: idle daemons hear of it (ext-idle-notify-v1), the idle steps start
 * counting again and undo what they did (idle.c), and with `wakes` (a key or button pressed, the
 * pointer moved or scrolled) it turns the monitors on when every one is off (output_power.c).
 * True when it turned some on, so that a key that woke them goes no further. */
bool input_activity(struct sh_server *server, bool wakes) {
    wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
    bool woke = idle_activity(server);
    return (wakes && wake_displays(server)) || woke;
}

static void keyboard_handle_modifiers(struct wl_listener *listener, void *data) {
    struct sh_keyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
    if (keyboard->server->syncing_keyboards)
        return; // keymap.c is changing it, not a key, and tells the seat itself

    // The input method's keyboard grab hears them in the application's place (input_method.c).
    if (!input_method_modifiers(keyboard)) {
        wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
        wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
                                           &keyboard->wlr_keyboard->modifiers);
    }
    struct sh_server *server = keyboard->server;
    follow_keyboard_layout(server, keyboard);
    uint32_t held = server->switcher.modifiers;
    if (server->switcher.open && held &&
        (wlr_keyboard_get_modifiers(keyboard->wlr_keyboard) & held) != held)
        switcher_close(server, server->switcher.selected);
}

static int keyboard_repeat(void *data) {
    struct sh_keyboard *keyboard = data;
    struct sh_server *server = keyboard->server;
    if (server->locked && !keyboard->repeat_locked)
        return 0;
    run_action(server, keyboard->repeat_action, keyboard->repeat_argument);
    int rate = keyboard->wlr_keyboard->repeat_info.rate;
    wl_event_source_timer_update(keyboard->repeat_timer, rate > 0 ? 1000 / rate : 0);
    return 0;
}

static void keyboard_handle_key(struct wl_listener *listener, void *data) {
    struct sh_keyboard *keyboard = wl_container_of(listener, keyboard, key);
    struct sh_server *server = keyboard->server;
    struct wlr_keyboard_key_event *event = data;
    struct wlr_seat *seat = server->seat;

    uint32_t keycode = event->keycode + 8;

    const xkb_keysym_t *syms;
    int nsyms = xkb_state_key_get_syms(keyboard->wlr_keyboard->xkb_state, keycode, &syms);

    bool woke = input_activity(server, event->state == WL_KEYBOARD_KEY_STATE_PRESSED);
    bool handled = false;
    uint32_t modifiers = wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
    // Any key pressed or the repeating one released stops the repeat.
    if (keyboard->repeat_timer && (event->state == WL_KEYBOARD_KEY_STATE_PRESSED ||
                                   event->keycode == keyboard->repeat_keycode))
        wl_event_source_timer_update(keyboard->repeat_timer, 0);
    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        // A key that woke the monitors does nothing else: nobody saw what it would do.
        handled = woke;
        for (int i = 0; i < nsyms && !handled; ++i)
            handled = handle_keybinding(keyboard, event->keycode, modifiers, syms[i]);
        if (!handled) {
            xkb_layout_index_t layout =
                xkb_state_key_get_layout(keyboard->wlr_keyboard->xkb_state, keycode);
            const xkb_keysym_t *raw;
            int nraw = xkb_keymap_key_get_syms_by_level(keyboard->wlr_keyboard->keymap, keycode,
                                                        layout, 0, &raw);
            for (int i = 0; i < nraw && !handled; ++i)
                handled = handle_keybinding(keyboard, event->keycode, modifiers, raw[i]);
        }
        if (event->keycode <= KEY_MAX)
            keyboard->consumed[event->keycode] = handled;
    } else if (event->keycode <= KEY_MAX) {
        handled = keyboard->consumed[event->keycode];
        keyboard->consumed[event->keycode] = false;
        if (server->peeking && server->peek_keycode == event->keycode && handled)
            set_peek(server, false);
    }

    // A key no binding takes goes to the input method's keyboard grab (input_method.c), else to
    // the application.
    if (!handled && !input_method_key(keyboard, event)) {
        wlr_seat_set_keyboard(seat, keyboard->wlr_keyboard);
        wlr_seat_keyboard_notify_key(seat, event->time_msec, event->keycode, event->state);
    }
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
    struct sh_keyboard *keyboard = wl_container_of(listener, keyboard, destroy);
    // The keyboard holding the switcher open goes away as if its modifiers were released.
    struct sh_server *server = keyboard->server;
    if (server->switcher.open && server->switcher.modifiers &&
        (wlr_keyboard_get_modifiers(keyboard->wlr_keyboard) & server->switcher.modifiers))
        switcher_close(server, server->switcher.selected);
    if (server->peek_keyboard == keyboard)
        set_peek(server, false);
    if (keyboard->repeat_timer)
        wl_event_source_remove(keyboard->repeat_timer);
    wl_list_remove(&keyboard->modifiers.link);
    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->destroy.link);
    wl_list_remove(&keyboard->link);
    // Without a seat keyboard, applications that start get no keymap until a key is typed: the
    // seat takes another keyboard, a real one if there is one.
    if (wlr_seat_get_keyboard(server->seat) == keyboard->wlr_keyboard) {
        struct sh_keyboard *other, *next = NULL;
        wl_list_for_each(other, &server->keyboards, link) {
            if (!next || (next->is_virtual && !other->is_virtual))
                next = other;
        }
        wlr_seat_set_keyboard(server->seat, next ? next->wlr_keyboard : NULL);
    }
    free(keyboard);
}

static void server_new_keyboard(struct sh_server *server, struct wlr_input_device *device) {
    struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);

    struct sh_keyboard *keyboard = calloc(1, sizeof(*keyboard));
    if (!keyboard)
        return;
    keyboard->server = server;
    keyboard->wlr_keyboard = wlr_keyboard;

    // A virtual keyboard (wtype and the like) sends its own keymap, which ours would replace.
    bool is_virtual = wlr_input_device_get_virtual_keyboard(device) != NULL;
    keyboard->is_virtual = is_virtual;
    if (!is_virtual && !configure_keyboard(server, wlr_keyboard)) {
        wlr_log(WLR_ERROR, "Failed to configure keyboard");
        free(keyboard);
        return;
    }
    if (is_virtual) { // held keys repeat bindings as on a real keyboard
        const struct sh_settings *settings = server_settings(server);
        wlr_keyboard_set_repeat_info(wlr_keyboard, settings->repeat_rate, settings->repeat_delay);
    }
    keyboard->repeat_timer = wl_event_loop_add_timer(wl_display_get_event_loop(server->wl_display),
                                                     keyboard_repeat, keyboard);

    add_listener(&wlr_keyboard->events.modifiers, &keyboard->modifiers, keyboard_handle_modifiers);
    add_listener(&wlr_keyboard->events.key, &keyboard->key, keyboard_handle_key);
    add_listener(&device->events.destroy, &keyboard->destroy, keyboard_handle_destroy);

    // It becomes the seat keyboard on its first key, once its keymap has arrived.
    if (!is_virtual)
        wlr_seat_set_keyboard(server->seat, keyboard->wlr_keyboard);

    wl_list_insert(&server->keyboards, &keyboard->link);
}

/* mouse.* applies to every pointer, touchpad.* to devices that can tap. Unset settings keep
 * the device's defaults. Only libinput devices (standalone sessions) have any of these. */
void configure_pointer(struct sh_server *server, struct wlr_input_device *device) {
#if WLR_HAS_LIBINPUT_BACKEND
    if (!wlr_input_device_is_libinput(device))
        return;
    struct libinput_device *handle = wlr_libinput_get_device_handle(device);
    const struct sh_settings *settings = server_settings(server);
    bool touchpad = libinput_device_config_tap_get_finger_count(handle) > 0;
    if (libinput_device_config_accel_is_available(handle)) {
        if (settings->pointer_speed_set)
            libinput_device_config_accel_set_speed(handle, settings->pointer_speed);
        if (settings->pointer_accel >= 0)
            libinput_device_config_accel_set_profile(
                handle, settings->pointer_accel ? LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE
                                                : LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT);
    }
    int natural = touchpad && settings->touchpad_natural_scroll >= 0
                      ? settings->touchpad_natural_scroll
                      : settings->mouse_natural_scroll;
    if (natural >= 0 && libinput_device_config_scroll_has_natural_scroll(handle))
        libinput_device_config_scroll_set_natural_scroll_enabled(handle, natural);
    if (touchpad && settings->touchpad_tap >= 0)
        libinput_device_config_tap_set_enabled(handle, settings->touchpad_tap
                                                           ? LIBINPUT_CONFIG_TAP_ENABLED
                                                           : LIBINPUT_CONFIG_TAP_DISABLED);
    if (settings->touchpad_dwt >= 0 && libinput_device_config_dwt_is_available(handle))
        libinput_device_config_dwt_set_enabled(handle, settings->touchpad_dwt
                                                           ? LIBINPUT_CONFIG_DWT_ENABLED
                                                           : LIBINPUT_CONFIG_DWT_DISABLED);
#else
    (void)server;
    (void)device;
#endif
}

static void pointer_destroy(struct wl_listener *listener, void *data) {
    struct sh_pointer *pointer = wl_container_of(listener, pointer, destroy);
    wl_list_remove(&pointer->destroy.link);
    wl_list_remove(&pointer->link);
    free(pointer);
}

static void server_new_pointer(struct sh_server *server, struct wlr_input_device *device) {
    wlr_cursor_attach_input_device(server->cursor, device);
    struct sh_pointer *pointer = calloc(1, sizeof(*pointer));
    if (!pointer)
        return;
    pointer->server = server;
    pointer->device = device;
    add_listener(&device->events.destroy, &pointer->destroy, pointer_destroy);
    wl_list_insert(&server->pointers, &pointer->link);
    configure_pointer(server, device);
}

void server_new_input(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, new_input);
    struct wlr_input_device *device = data;
    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
        server_new_keyboard(server, device);
        break;
    case WLR_INPUT_DEVICE_POINTER:
        server_new_pointer(server, device);
        break;
    case WLR_INPUT_DEVICE_TOUCH:
        server_new_touch(server, device);
        break;
    case WLR_INPUT_DEVICE_TABLET:
        server_new_tablet(server, device);
        break;
    case WLR_INPUT_DEVICE_TABLET_PAD:
        server_new_tablet_pad(server, device);
        break;
    case WLR_INPUT_DEVICE_SWITCH:
        server_new_switch(server, device);
        break;
    default:
        break;
    }
}

/* Virtual input lets tools such as wtype and wlrctl drive the session, e.g. in tests. */
void server_new_virtual_keyboard(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, new_virtual_keyboard);
    struct wlr_virtual_keyboard_v1 *keyboard = data;
    server_new_input(&server->new_input, &keyboard->keyboard.base);
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
    struct sh_headless_keyboard *keyboard = fields >= 2 ? find_headless_keyboard(server, name) : NULL;
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

void server_new_virtual_pointer(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, new_virtual_pointer);
    struct wlr_virtual_pointer_v1_new_pointer_event *event = data;
    struct wlr_input_device *device = &event->new_pointer->pointer.base;
    server_new_input(&server->new_input, device);
    if (event->suggested_output)
        wlr_cursor_map_input_to_output(server->cursor, device, event->suggested_output);
}

void seat_request_set_selection(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, request_set_selection);
    struct wlr_seat_request_set_selection_event *event = data;
    wlr_seat_set_selection(server->seat, event->source, event->serial);
}

void seat_request_set_primary_selection(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, request_set_primary_selection);
    struct wlr_seat_request_set_primary_selection_event *event = data;
    wlr_seat_set_primary_selection(server->seat, event->source, event->serial);
}

/* Drag-and-drop (browser tabs, files into chat windows): only from a real button press. */
void seat_request_start_drag(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, request_start_drag);
    struct wlr_seat_request_start_drag_event *event = data;
    if (!server->locked && server->cursor_mode == SH_CURSOR_PASSTHROUGH &&
        wlr_seat_validate_pointer_grab_serial(server->seat, event->origin, event->serial))
        wlr_seat_start_pointer_drag(server->seat, event->drag, event->serial);
    else
        wlr_data_source_destroy(event->drag->source);
}

/* A drag holds the keyboard while it lasts, and wlroots lets no focus change through it: a
 * window focused meanwhile (from the taskbar, which brings a window forward under a drag resting
 * on its button, by xdg-activation or from the switcher) is activated and raised, but the
 * keyboard stays with the window the drag began in. Once it ends, the keyboard goes where focus
 * is, and the pointer, which wlroots took from every surface as the drag began, to the surface
 * under it, so that a click without moving first reaches it. */
static void drag_ended(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, drag_end);
    wl_list_remove(&server->drag_end.link);
    if (!server->running)
        return; // clients going away at shutdown
    struct wlr_seat *seat = server->seat;
    struct wlr_surface *focus = NULL;
    if (server->focused_toplevel && toplevel_accepts_keyboard(server->focused_toplevel))
        focus = toplevel_surface(server->focused_toplevel);
    else if (server->focused_layer)
        focus = server->focused_layer->surface->surface;
    if (!server->locked && focus && focus != seat->keyboard_state.focused_surface)
        keyboard_enter(seat, focus);
    process_cursor_motion(server, (uint32_t)now_ms());
}

void seat_start_drag(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, start_drag);
    struct wlr_drag *drag = data;
    wlr_scene_node_set_position(&server->drag_icons->node, server->cursor->x, server->cursor->y);
    // The scene helper removes the icon's node when the icon goes away.
    if (drag->icon)
        wlr_scene_drag_icon_create(server->drag_icons, drag->icon);
    add_listener(&drag->events.destroy, &server->drag_end, drag_ended);
}

/* Pointer constraints (games, remote desktops, pointer lock in browsers) apply to the
 * keyboard-focused surface only, and only while the pointer is over it. */
static void set_active_constraint(struct sh_server *server,
                                  struct wlr_pointer_constraint_v1 *constraint) {
    if (server->active_constraint == constraint)
        return;
    if (server->active_constraint)
        wlr_pointer_constraint_v1_send_deactivated(server->active_constraint);
    server->active_constraint = constraint;
    if (constraint)
        wlr_pointer_constraint_v1_send_activated(constraint);
}

static void constraint_destroy(struct wl_listener *listener, void *data) {
    struct wlr_pointer_constraint_v1 *constraint = data;
    struct sh_server *server = constraint->data;
    wl_list_remove(&listener->link);
    free(listener);
    if (server->active_constraint == constraint)
        server->active_constraint = NULL;
}

void server_new_constraint(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, new_constraint);
    struct wlr_pointer_constraint_v1 *constraint = data;
    struct wl_listener *destroy = calloc(1, sizeof(*destroy));
    if (!destroy)
        return;
    constraint->data = server;
    add_listener(&constraint->events.destroy, destroy, constraint_destroy);
    if (constraint->surface == server->seat->keyboard_state.focused_surface)
        set_active_constraint(server, constraint);
}

void seat_keyboard_focus_change(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, keyboard_focus_change);
    struct wlr_seat_keyboard_focus_change_event *event = data;
    set_active_constraint(server, event->new_surface
                                      ? wlr_pointer_constraints_v1_constraint_for_surface(
                                            server->constraints, event->new_surface, server->seat)
                                      : NULL);
}

#if WLR_HAS_SESSION
// Returns the VT a key switches to, or 0. Ctrl+AltGr+Fn counts as Ctrl+Alt+Fn: some keyboards'
// only Alt key is Right Alt, which AltGr layouts turn into Level3 instead of Alt.
static unsigned vt_for_key(uint32_t modifiers, xkb_keysym_t sym) {
    if (sym >= XKB_KEY_XF86Switch_VT_1 && sym <= XKB_KEY_XF86Switch_VT_12)
        return sym - XKB_KEY_XF86Switch_VT_1 + 1;
    if ((modifiers & WLR_MODIFIER_CTRL) && (modifiers & (WLR_MODIFIER_ALT | WLR_MODIFIER_MOD5)) &&
        sym >= XKB_KEY_F1 && sym <= XKB_KEY_F12)
        return sym - XKB_KEY_F1 + 1;
    return 0;
}
#endif

/* The sh_binding_flag bits of the binding the key callback returned last. */
static unsigned binding_flags(struct sh_server *server) {
    return server->callbacks->binding_flags
               ? server->callbacks->binding_flags(server->callbacks->userdata)
               : 0;
}

/* A binding with `repeats` runs its action again while its key is held, after the keyboard's
 * repeat delay and at its rate. */
static void start_repeat(struct sh_keyboard *keyboard, uint32_t keycode, enum sh_action action,
                         int argument, unsigned flags) {
    if (!(flags & SH_BINDING_REPEATS) || keyboard->wlr_keyboard->repeat_info.rate <= 0 ||
        !keyboard->repeat_timer)
        return;
    keyboard->repeat_keycode = keycode;
    keyboard->repeat_action = action;
    keyboard->repeat_argument = argument;
    keyboard->repeat_locked = flags & SH_BINDING_LOCKED;
    wl_event_source_timer_update(keyboard->repeat_timer, keyboard->wlr_keyboard->repeat_info.delay);
}

static bool handle_keybinding(struct sh_keyboard *keyboard, uint32_t keycode, uint32_t modifiers,
                              xkb_keysym_t sym) {
    struct sh_server *server = keyboard->server;
#if WLR_HAS_SESSION
    unsigned vt = vt_for_key(modifiers, sym);
    if (server->session && vt) {
        wlr_session_change_vt(server->session, vt);
        return true;
    }
#endif
    if (server->locked) {
        // Every other key belongs to the lock screen: only bindings marked `locked` run, such as
        // the volume keys.
        int argument = 0;
        enum sh_action action =
            server->callbacks->key(server->callbacks->userdata, modifiers, sym, &argument);
        unsigned flags = action != SH_NONE ? binding_flags(server) : 0;
        if (!(flags & SH_BINDING_LOCKED))
            return false;
        run_action(server, action, argument);
        start_repeat(keyboard, keycode, action, argument, flags);
        return true;
    }
    if (server->switcher.open) {
        switcher_key(server, modifiers, sym);
        return true;
    }
    if (server->overview.open) {
        // The overview's own bindings (toggling it again) work as bound; other keys are its.
        int bound_argument = 0;
        enum sh_action bound =
            modifiers & (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO)
                ? server->callbacks->key(server->callbacks->userdata, modifiers, sym,
                                         &bound_argument)
                : SH_NONE;
        // Snap Assist gives way to any other binding, which then runs as usual.
        if (server->overview.assist && bound != SH_NONE && bound != SH_OVERVIEW_CONFIRM &&
            bound != SH_OVERVIEW_CANCEL) {
            overview_close(server, NULL, -1);
        } else {
            if (bound == SH_OVERVIEW_TOGGLE || bound == SH_OVERVIEW_CONFIRM ||
                bound == SH_OVERVIEW_CANCEL)
                run_action(server, bound, bound_argument);
            else
                overview_key(server, modifiers, sym);
            return true;
        }
    }
    // The display mode popup takes the arrows, Return and Escape while it is open.
    if (display_mode_key(server, sym))
        return true;
    int argument = 0;
    enum sh_action action =
        server->callbacks->key(server->callbacks->userdata, modifiers, sym, &argument);
    // An application holding the shortcuts (shortcuts_inhibit.c) has the keys, but for the
    // binding that takes them back.
    if (action != SH_TOGGLE_SHORTCUTS_INHIBIT && shortcuts_inhibited(server))
        return false;
    if (action == SH_NONE)
        return false;
    if (action == SH_PEEK) {
        // Held: the desktop shows until the key comes back up.
        server->peek_keycode = keycode;
        server->peek_keyboard = keyboard;
        set_peek(server, true);
        return true;
    }
    if (action == SH_SWITCHER_NEXT || action == SH_SWITCHER_PREV) {
        // Held, the binding's modifiers keep it open. Shift may come and go to step backward.
        uint32_t held =
            WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO | WLR_MODIFIER_MOD5;
        switcher_open(server, action == SH_SWITCHER_PREV, modifiers & held,
                      xkb_keysym_to_lower(sym));
        return true;
    }
    unsigned flags = binding_flags(server);
    run_action(server, action, argument);
    start_repeat(keyboard, keycode, action, argument, flags);
    return true;
}
