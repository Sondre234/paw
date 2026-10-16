/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The cursor hidden while typing (mouse.hide_when_typing), as macOS does, and after the pointer
 * rests a while (mouse.hide_after), until the pointer moves, clicks or scrolls again. */
#include "server.h"

/* A key went to an application (or its input method): unless it is a modifier, a button is held
 * or the pointer moves something, the cursor hides until the pointer is used. */
void cursor_typed(struct sh_server *server, xkb_keysym_t sym) {
    if (!server_settings(server)->hide_cursor_typing || keysym_is_modifier(sym) ||
        server->seat->pointer_state.button_count || server->cursor_mode != SH_CURSOR_PASSTHROUGH)
        return;
    cursor_hide(server, SH_HIDDEN_TYPING);
}

static void arm(struct sh_server *server, int64_t ms) {
    if (server->cursor_rest.timer)
        wl_event_source_timer_update(server->cursor_rest.timer, ms > 0 ? (int)ms : 1);
}

/* The pointer at rest for mouse.hide_after: the cursor hides, unless a button is held. Motion
 * only notes its time, and the timer looks again for the rest of the while from there. */
static int rested(void *data) {
    struct sh_server *server = data;
    int after = server_settings(server)->hide_cursor_after;
    if (after <= 0)
        return 0;
    int64_t left = server->cursor_rest.moved + after * 1000LL - now_ms();
    if (left > 0) {
        arm(server, left);
        return 0;
    }
    if (server->seat->pointer_state.button_count || server->cursor_mode != SH_CURSOR_PASSTHROUGH) {
        arm(server, after * 1000LL); // held still mid-drag: looked at again later
        return 0;
    }
    cursor_hide(server, SH_HIDDEN_RESTING);
    return 0;
}

/* The pointer moved, clicked or scrolled: the cursor shows again, and rests from now. */
void cursor_used(struct sh_server *server) {
    server->cursor_rest.moved = now_ms();
    if (server->cursor_hidden & SH_HIDDEN_RESTING)
        arm(server, server_settings(server)->hide_cursor_after * 1000LL);
    cursor_show(server, SH_HIDDEN_TYPING | SH_HIDDEN_RESTING);
}

/* The settings, at startup and after a reload: a reason turned off shows the cursor it hid. */
void cursor_hide_reload(struct sh_server *server) {
    const struct sh_settings *settings = server_settings(server);
    if (!settings->hide_cursor_typing)
        cursor_show(server, SH_HIDDEN_TYPING);
    if (settings->hide_cursor_after <= 0) {
        cursor_show(server, SH_HIDDEN_RESTING);
        if (server->cursor_rest.timer)
            wl_event_source_timer_update(server->cursor_rest.timer, 0);
        return;
    }
    if (!server->cursor_rest.timer)
        server->cursor_rest.timer = wl_event_loop_add_timer(
            wl_display_get_event_loop(server->wl_display), rested, server);
    if (!server->cursor_rest.moved)
        server->cursor_rest.moved = now_ms();
    if (!(server->cursor_hidden & SH_HIDDEN_RESTING))
        arm(server, server->cursor_rest.moved + settings->hide_cursor_after * 1000LL - now_ms());
}

void cursor_hide_finish(struct sh_server *server) {
    if (server->cursor_rest.timer)
        wl_event_source_remove(server->cursor_rest.timer);
    server->cursor_rest.timer = NULL;
}
