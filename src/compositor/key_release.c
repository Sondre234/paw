/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Key bindings with `release` (Hyprland's bindr): the action runs as the key comes up. One of a
 * modifier key, such as Super_L opening the launcher as Windows' Start key does, runs only when
 * the key was tapped: another key, a mouse button, the wheel, a touchpad gesture, a finger or a
 * pen while it is held lets it go, so Super + drag and Super + Q stay what they are, and so does
 * holding it longer than a tap, or going down while a button or a finger already is (a window
 * dragged by its title). */
#include "server.h"

/* How long a modifier key may be held and still be tapped, as xcape's default: held longer, it
 * was held for something else, or thought better of. */
#define TAP_MS 500

static unsigned flags_of_last(struct sh_server *server) {
    return server->callbacks->binding_flags
               ? server->callbacks->binding_flags(server->callbacks->userdata)
               : 0;
}

/* The release binding of the keysym `sym` with `modifiers` held, as key_released finds it: its
 * action (SH_NONE for none) and argument. Its flags are then binding_flags'. */
static enum sh_action release_binding(struct sh_server *server, uint32_t modifiers,
                                      xkb_keysym_t sym, int *argument) {
    if (!server->callbacks->key_released)
        return SH_NONE;
    return server->callbacks->key_released(server->callbacks->userdata, modifiers, sym, argument);
}

/* Whether a release binding may wait for its key now: not while something takes the keys for
 * itself (the switcher, the overview, the exploded view, an application holding the shortcuts),
 * nor on the lock screen unless it is `locked`. */
static bool may_run(struct sh_server *server) {
    if (server->locked)
        return flags_of_last(server) & SH_BINDING_LOCKED;
    return !server->switcher.open && !server->overview.open && !server->xray.open &&
           !shortcuts_inhibited(server);
}

/* A slot for a binding to wait in, NULL when every one is taken. */
static struct sh_key_release *free_slot(struct sh_server *server) {
    for (int i = 0; i < SH_KEY_RELEASES; ++i)
        if (!server->key_releases[i].keyboard)
            return &server->key_releases[i];
    return NULL;
}

/* Whether a button or a finger is down: a modifier key going down then is no tap. */
static bool pointer_busy(struct sh_server *server) {
    return server->held_buttons || !wl_list_empty(&server->seat->touch_state.touch_points);
}

/* A key went down at `time` (the event's, in milliseconds), after the bindings that run as keys
 * go down had their turn: it lets go of the taps waiting, and with a release binding of its own
 * waits to come up. True when the press is the binding's, as for any key but a modifier, whose press
 * goes on to the application. */
bool key_release_press(struct sh_keyboard *keyboard, uint32_t keycode, uint32_t modifiers,
                       bool woke, uint32_t time) {
    struct sh_server *server = keyboard->server;
    key_release_cancel(server);
    if (woke)
        return false; // a key that woke the monitors does nothing else
    struct xkb_state *state = keyboard->wlr_keyboard->xkb_state;
    uint32_t xkb_keycode = keycode + 8;
    const xkb_keysym_t *syms;
    int nsyms = xkb_state_key_get_syms(state, xkb_keycode, &syms);
    // As for the other bindings, the keysym the layout gives, else the key's first level's.
    const xkb_keysym_t *raw = NULL;
    int nraw = xkb_keymap_key_get_syms_by_level(keyboard->wlr_keyboard->keymap, xkb_keycode,
                                                xkb_state_key_get_layout(state, xkb_keycode), 0,
                                                &raw);
    for (int i = 0; i < nsyms + nraw; ++i) {
        xkb_keysym_t sym = i < nsyms ? syms[i] : raw[i - nsyms];
        int argument = 0;
        if (release_binding(server, modifiers, sym, &argument) == SH_NONE)
            continue;
        if (!may_run(server))
            return false;
        bool tap = keysym_is_modifier(sym);
        struct sh_key_release *slot = free_slot(server);
        if ((tap && pointer_busy(server)) || !slot)
            return false;
        *slot = (struct sh_key_release){keyboard, keycode, modifiers, time, sym, tap};
        return !tap;
    }
    return false;
}

/* A key came up at `time`: the release bindings waiting for it run, a tap's only when it came up
 * soon enough. */
void key_release_up(struct sh_keyboard *keyboard, uint32_t keycode, uint32_t time) {
    struct sh_server *server = keyboard->server;
    for (int i = 0; i < SH_KEY_RELEASES; ++i) {
        struct sh_key_release waiting = server->key_releases[i];
        if (waiting.keyboard != keyboard || waiting.keycode != keycode)
            continue;
        server->key_releases[i].keyboard = NULL;
        // Unsigned, so that the clock's wrap around 49 days apart counts the same.
        if (waiting.tap && time - waiting.time > TAP_MS)
            continue;
        // Looked up again: what else ran meanwhile changed the configuration's last binding.
        int argument = 0;
        enum sh_action action =
            release_binding(server, waiting.modifiers, waiting.sym, &argument);
        if (action != SH_NONE && may_run(server))
            run_action(server, action, argument);
    }
}

/* Something else happened (a key, a button, the wheel, a gesture): a modifier key's tap waiting
 * will not run. */
void key_release_cancel(struct sh_server *server) {
    for (int i = 0; i < SH_KEY_RELEASES; ++i)
        if (server->key_releases[i].tap)
            server->key_releases[i].keyboard = NULL;
}

/* A keyboard going away takes what waits on its keys with it. */
void key_release_forget(struct sh_keyboard *keyboard) {
    for (int i = 0; i < SH_KEY_RELEASES; ++i)
        if (keyboard->server->key_releases[i].keyboard == keyboard)
            keyboard->server->key_releases[i].keyboard = NULL;
}
