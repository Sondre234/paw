/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Key bindings with `release` (Hyprland's bindr): the action runs as the key comes up. One of a
 * modifier key, such as Super_L opening the launcher as Windows' Start key does, runs only when
 * the key was tapped: another key, a mouse button, the wheel, a touchpad gesture, a finger or a
 * pen while it is held lets it go, so Super + drag and Super + Q stay what they are. */
#include "server.h"

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

/* A key went down, after the bindings that run as keys go down had their turn: it lets go of a
 * tap waiting, and with a release binding of its own waits to come up. True when the press is the
 * binding's, as for any key but a modifier, whose press goes on to the application. */
bool key_release_press(struct sh_keyboard *keyboard, uint32_t keycode, uint32_t modifiers,
                       bool woke) {
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
        server->key_release.keyboard = keyboard;
        server->key_release.keycode = keycode;
        server->key_release.modifiers = modifiers;
        server->key_release.sym = sym;
        server->key_release.tap = tap;
        return !tap;
    }
    return false;
}

/* A key came up: the release binding waiting for it runs. */
void key_release_up(struct sh_keyboard *keyboard, uint32_t keycode) {
    struct sh_server *server = keyboard->server;
    if (server->key_release.keyboard != keyboard || server->key_release.keycode != keycode)
        return;
    server->key_release.keyboard = NULL;
    // Looked up again: what else ran meanwhile changed the configuration's last binding.
    int argument = 0;
    enum sh_action action = release_binding(server, server->key_release.modifiers,
                                            server->key_release.sym, &argument);
    if (action == SH_NONE || !may_run(server))
        return;
    run_action(server, action, argument);
}

/* Something else happened (a key, a button, the wheel, a gesture): a modifier key's tap waiting
 * will not run. */
void key_release_cancel(struct sh_server *server) {
    if (server->key_release.keyboard && server->key_release.tap)
        server->key_release.keyboard = NULL;
}

/* A keyboard going away takes what waits on its keys with it. */
void key_release_forget(struct sh_keyboard *keyboard) {
    if (keyboard->server->key_release.keyboard == keyboard)
        keyboard->server->key_release.keyboard = NULL;
}
