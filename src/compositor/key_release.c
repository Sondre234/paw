/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Key bindings with `release` (Hyprland's bindr): the action runs as the key comes up. One of a
 * modifier key, such as Super_L opening the launcher as Windows' Start key does, runs only when
 * the key was tapped: another key, a mouse button, the wheel, a touchpad gesture, a finger or a
 * pen while it is held lets it go, so Super + drag and Super + Q stay what they are, and so does
 * holding it longer than a tap, or going down while a button or a finger already is (a window
 * dragged by its title). A pair (`press` and `release`) runs its press as the key goes down, and
 * then its release as it comes up whatever happened meanwhile, so that push-to-talk always mutes
 * the microphone again. A chord (a binding without a key) is its modifiers held on either side's
 * keys: it goes down with the last of them and comes up with the first, a tap as a modifier key's
 * is. */
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

/* The same for a pair, its press's action, its release kept as `slot`. */
static enum sh_action pair_binding(struct sh_server *server, uint32_t modifiers, xkb_keysym_t sym,
                                   const struct sh_key_release *slot, int *argument) {
    if (!server->callbacks->key_pair)
        return SH_NONE;
    return server->callbacks->key_pair(server->callbacks->userdata, modifiers, sym,
                                       (int)(slot - server->key_releases), argument);
}

/* Whether a release binding may wait for its key now, or a pair's press run: not while something
 * takes the keys for itself (the switcher, the overview, the exploded view, an application
 * holding the shortcuts), nor on the lock screen unless it is `locked`. */
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

/* The keysyms of the key with `keycode` on `keyboard`, as the other bindings take them: those the
 * layout gives, `count` of them, then those of the key's first level, `raw_count`. */
static void key_syms(struct sh_keyboard *keyboard, uint32_t keycode, const xkb_keysym_t **syms,
                     int *count, const xkb_keysym_t **raw, int *raw_count) {
    struct xkb_state *state = keyboard->wlr_keyboard->xkb_state;
    uint32_t xkb_keycode = keycode + 8;
    *count = xkb_state_key_get_syms(state, xkb_keycode, syms);
    *raw_count = xkb_keymap_key_get_syms_by_level(keyboard->wlr_keyboard->keymap, xkb_keycode,
                                                  xkb_state_key_get_layout(state, xkb_keycode),
                                                  0, raw);
}

/* The modifier the key with `keycode` gives among those bindings take (WLR_MODIFIER_LOGO for
 * Super_L), else 0. */
static uint32_t modifier_of(struct sh_keyboard *keyboard, uint32_t keycode) {
    const xkb_keysym_t *syms, *raw;
    int count, raw_count;
    key_syms(keyboard, keycode, &syms, &count, &raw, &raw_count);
    for (int i = 0; i < count + raw_count; ++i) {
        switch (i < count ? syms[i] : raw[i - count]) {
        case XKB_KEY_Shift_L:
        case XKB_KEY_Shift_R:
            return WLR_MODIFIER_SHIFT;
        case XKB_KEY_Control_L:
        case XKB_KEY_Control_R:
            return WLR_MODIFIER_CTRL;
        case XKB_KEY_Alt_L:
        case XKB_KEY_Alt_R:
        case XKB_KEY_Meta_L:
        case XKB_KEY_Meta_R:
            return WLR_MODIFIER_ALT;
        case XKB_KEY_Super_L:
        case XKB_KEY_Super_R:
            return WLR_MODIFIER_LOGO;
        }
    }
    return 0;
}

/* The binding of `sym` (NoSymbol for a chord's) with `modifiers` held, as the key with `keycode`
 * on `keyboard` goes down at `time`: a pair's press runs and its release waits, as does a binding
 * with `release`, `taken` saying whether either does now. False without one. */
static bool key_bound(struct sh_keyboard *keyboard, uint32_t keycode, uint32_t modifiers,
                      xkb_keysym_t sym, uint32_t time, bool *taken) {
    struct sh_server *server = keyboard->server;
    struct sh_key_release *slot = free_slot(server);
    if (!slot)
        return false;
    int argument = 0;
    enum sh_action press = pair_binding(server, modifiers, sym, slot, &argument);
    if (press != SH_NONE) {
        *taken = may_run(server);
        if (*taken) {
            *slot = (struct sh_key_release){keyboard, keycode, modifiers, time, sym, false, true};
            run_action(server, press, argument);
        }
        return true;
    }
    if (release_binding(server, modifiers, sym, &argument) == SH_NONE)
        return false;
    bool tap = sym == XKB_KEY_NoSymbol || keysym_is_modifier(sym);
    *taken = may_run(server) && !(tap && pointer_busy(server));
    if (*taken)
        *slot = (struct sh_key_release){keyboard, keycode, modifiers, time, sym, tap, false};
    return true;
}

/* A key went down at `time` (the event's, in milliseconds), after the bindings that run as keys
 * go down had their turn: it lets go of the taps waiting, and with a release binding or a pair of
 * its own waits to come up. A modifier key without one may make a chord of the modifiers then
 * held, whose binding runs as it goes down and whose release binding or pair waits as a key's.
 * True when the press is the binding's, as for any key but a modifier, whose press goes on to the
 * application. */
bool key_release_press(struct sh_keyboard *keyboard, uint32_t keycode, uint32_t modifiers,
                       bool woke, uint32_t time) {
    struct sh_server *server = keyboard->server;
    key_release_cancel(server);
    if (woke)
        return false; // a key that woke the monitors does nothing else
    const xkb_keysym_t *syms, *raw;
    int count, raw_count;
    key_syms(keyboard, keycode, &syms, &count, &raw, &raw_count);
    bool taken = false;
    for (int i = 0; i < count + raw_count; ++i) {
        xkb_keysym_t sym = i < count ? syms[i] : raw[i - count];
        if (key_bound(keyboard, keycode, modifiers, sym, time, &taken))
            return taken && !keysym_is_modifier(sym);
    }
    uint32_t own = modifier_of(keyboard, keycode);
    if (!own || (modifiers & own))
        return false;
    modifiers |= own;
    // A chord's binding that runs as it goes down, which handle_keybinding never sees.
    int argument = 0;
    enum sh_action action = server->callbacks->key(server->callbacks->userdata, modifiers,
                                                   XKB_KEY_NoSymbol, &argument);
    if (action != SH_NONE && may_run(server))
        run_action(server, action, argument);
    key_bound(keyboard, keycode, modifiers, XKB_KEY_NoSymbol, time, &taken);
    return false;
}

/* The release of the pair waiting in slot `i`, which runs whatever happened since its press. */
static void release_held(struct sh_server *server, int i) {
    int argument = 0;
    enum sh_action action =
        server->callbacks->held_release(server->callbacks->userdata, i, &argument);
    if (action != SH_NONE)
        run_action(server, action, argument);
}

/* A key came up at `time`: the release bindings waiting for it run, a chord's as the first of its
 * modifiers comes up, and a tap's only when it came up soon enough. */
void key_release_up(struct sh_keyboard *keyboard, uint32_t keycode, uint32_t time) {
    struct sh_server *server = keyboard->server;
    uint32_t own = modifier_of(keyboard, keycode);
    for (int i = 0; i < SH_KEY_RELEASES; ++i) {
        struct sh_key_release waiting = server->key_releases[i];
        bool up = waiting.sym == XKB_KEY_NoSymbol ? (waiting.modifiers & own) != 0
                                                  : waiting.keycode == keycode;
        if (waiting.keyboard != keyboard || !up)
            continue;
        server->key_releases[i].keyboard = NULL;
        if (waiting.held) {
            release_held(server, i);
            continue;
        }
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

/* A keyboard going away takes what waits on its keys with it. Its keys came up first
 * (wlr_keyboard_finish lets them up), which ran its pairs' releases. */
void key_release_forget(struct sh_keyboard *keyboard) {
    for (int i = 0; i < SH_KEY_RELEASES; ++i)
        if (keyboard->server->key_releases[i].keyboard == keyboard)
            keyboard->server->key_releases[i].keyboard = NULL;
}

/* The compositor stops with keys still down: their pairs' releases run, while what they act on is
 * still there, and nothing waits for the keys the backend lets up as it goes. */
void key_release_finish(struct sh_server *server) {
    for (int i = 0; i < SH_KEY_RELEASES; ++i) {
        bool held = server->key_releases[i].keyboard && server->key_releases[i].held;
        server->key_releases[i].keyboard = NULL;
        if (held)
            release_held(server, i);
    }
}
