/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* The window switcher: a list of windows, most recently focused first, that the shell draws
 * from the events sent here, and its peek at the selected window. */
#include "server.h"

/* How long the selection rests before the first peek at it: about Windows' wait, as the
 * taskbar's card waits on a picture, so that a quick Alt+Tab, or Tab pressed on through the
 * list, fades nothing. */
enum { SWITCHER_PEEK_MS = 500 };

/* Whether the peek at a window is the switcher's: at the window it peeked at, and nothing took
 * it over or ended it since (focus, the desktop peek, a taskbar's picture). */
static bool switcher_peeks(struct sh_server *server) {
    return server->switcher.peeked && server->peek_window == server->switcher.peeked &&
           !server->peek_object;
}

/* Peeks at the selected window (effects.c), taking the peek over from whatever peeks now; the
 * switcher's own peek, which no window object ends. With peek.switcher turned off meanwhile (a
 * reload), it ends its peek instead, at the next step the switcher takes. */
static void peek_at_selected(struct sh_server *server) {
    if (!server_settings(server)->effects.peek_switcher) {
        if (switcher_peeks(server))
            end_window_peek(server, true);
        server->switcher.peeked = NULL;
        return;
    }
    if (server->locked || server->peeking)
        return;
    struct sh_toplevel *selected = server->switcher.windows[server->switcher.selected];
    peek_at_window(selected);
    server->peek_object = NULL;
    server->switcher.peeked = selected;
}

static int switcher_peek_due(void *data) {
    struct sh_server *server = data;
    if (server->switcher.open && server->switcher.peek_asked)
        peek_at_selected(server);
    return 0;
}

/* Peeks at the selection `wait` milliseconds from now. */
static void peek_after(struct sh_server *server, int64_t wait) {
    if (!server->switcher.peek_timer)
        server->switcher.peek_timer = wl_event_loop_add_timer(
            wl_display_get_event_loop(server->wl_display), switcher_peek_due, server);
    if (server->switcher.peek_timer)
        wl_event_source_timer_update(server->switcher.peek_timer, wait > 1 ? (int)wait : 1);
}

/* The selection moved, or the shell showed the switcher: while the switcher's peek goes on, it
 * follows the selection at once, the others staying faded, as on the taskbar's card; otherwise,
 * the first time or after something else ended it, the selection rests SWITCHER_PEEK_MS first. */
static void switcher_follow(struct sh_server *server) {
    if (!server->switcher.open || !server->switcher.peek_asked)
        return;
    if (switcher_peeks(server)) {
        peek_at_selected(server);
        return;
    }
    server->switcher.peeked = NULL;
    peek_after(server, server->switcher.selected_at + SWITCHER_PEEK_MS - now_ms());
}

/* "switcher_peek": the shell has drawn the switcher, so its selected window may peek. Without
 * the shell, which draws it, the switcher stays unseen, and so does the peek. False while it is
 * closed. */
bool switcher_ask_peek(struct sh_server *server) {
    if (!server->switcher.open)
        return false;
    if (!server->switcher.peek_asked) {
        server->switcher.peek_asked = true;
        switcher_follow(server);
    }
    return true;
}

void switcher_finish(struct sh_server *server) {
    if (server->switcher.peek_timer)
        wl_event_source_remove(server->switcher.peek_timer);
    server->switcher.peek_timer = NULL;
}

/* Subscribers get "switcher OUTPUT SELECTED COUNT" followed by COUNT lines
 * "switcher-window APP_ID\tTITLE\tOUTPUT\tWORKSPACE\tMINIMIZED\tURGENT\tID" as it opens or its
 * list changes, "switcher-select N" as the selection moves (both counting from 0), and
 * "switcher-close" when it closes. ID is the window's number (the window control's id event), by
 * which the shell finds the window among its taskbar handles for its picture. */
static void switcher_announce(struct sh_server *server) {
    size_t size = 128 + (size_t)server->switcher.count * 1024, length = 0;
    char *text = malloc(size);
    if (!text)
        return;
    length += snprintf(text, size, "switcher %s %d %d\n", server->switcher.output,
                       server->switcher.selected, server->switcher.count);
    for (int i = 0; i < server->switcher.count; ++i) {
        struct sh_toplevel *toplevel = server->switcher.windows[i];
        char app_id[256], title[512];
        copy_field(app_id, sizeof(app_id), toplevel_app_id(toplevel));
        copy_field(title, sizeof(title), toplevel_title(toplevel));
        length +=
            snprintf(text + length, size - length, "switcher-window %s\t%s\t%s\t%d\t%d\t%d\t%u\n",
                     app_id, title, toplevel->output, toplevel->workspace + 1, toplevel->minimized,
                     toplevel->urgent, toplevel->id);
    }
    send_event(server, text, length);
    free(text);
}

static void switcher_select(struct sh_server *server, int selected) {
    int count = server->switcher.count;
    server->switcher.selected = ((selected % count) + count) % count;
    server->switcher.selected_at = now_ms();
    char line[32];
    int length = snprintf(line, sizeof(line), "switcher-select %d\n", server->switcher.selected);
    send_event(server, line, (size_t)length);
    switcher_follow(server);
}

/* Focuses the window at `index` in the list (or none, below 0) and closes the switcher. */
void switcher_close(struct sh_server *server, int index) {
    if (!server->switcher.open)
        return;
    struct sh_toplevel *chosen =
        index >= 0 && index < server->switcher.count ? server->switcher.windows[index] : NULL;
    bool peeked = switcher_peeks(server);
    server->switcher.open = false;
    server->switcher.count = 0;
    server->switcher.peek_asked = false;
    server->switcher.peeked = NULL;
    if (server->switcher.peek_timer)
        wl_event_source_timer_update(server->switcher.peek_timer, 0);
    send_event(server, "switcher-close\n", strlen("switcher-close\n"));
    // The peek ends with it. The window peeked at, chosen, is in front already: focusing it ends
    // the peek around it as the others come back. Otherwise they fade back first, a window
    // chosen coming up over the one peeked at with them.
    if (peeked && (!chosen || chosen != server->peek_window || server->locked))
        end_window_peek(server, true);
    if (!chosen || server->locked)
        return;
    focus_toplevel(chosen);
    // Focus following the mouse would hand focus back to whatever the pointer is over. Raised,
    // the window is on top wherever the pointer is inside it.
    struct wlr_box box = toplevel_box(chosen);
    if (!wlr_box_contains_point(&box, server->cursor->x, server->cursor->y))
        pointer_follow(chosen);
}

/* Opens the switcher on the focused output, selecting the window focused before the current
 * one (or the least recent one, going `backward`); when open, moves the selection instead. */
void switcher_open(struct sh_server *server, bool backward, uint32_t modifiers,
                   xkb_keysym_t key) {
    if (server->switcher.open) {
        switcher_select(server, server->switcher.selected + (backward ? -1 : 1));
        return;
    }
    struct wlr_output *output = focused_output(server);
    if (server->locked || !output)
        return;
    int count = 0;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel->unmanaged || toplevel->swallowed)
            continue;
        if (count < (int)(sizeof(server->switcher.windows) / sizeof(*server->switcher.windows)))
            server->switcher.windows[count++] = toplevel;
    }
    if (!count)
        return;
    server->switcher.open = true;
    server->switcher.count = count;
    server->switcher.modifiers = modifiers;
    server->switcher.key = key;
    server->switcher.peek_asked = false;
    server->switcher.peeked = NULL;
    server->switcher.selected_at = now_ms();
    snprintf(server->switcher.output, sizeof(server->switcher.output), "%s", output->name);
    // The first window is the focused one unless focus is on the bare desktop.
    int next = server->switcher.windows[0] == server->focused_toplevel && count > 1 ? 1 : 0;
    server->switcher.selected = backward ? count - 1 : next;
    switcher_announce(server);
    wrapped_count(server, SH_WRAPPED_SWITCHER_OPENS);
}

/* A window closing while the switcher is open leaves its list. */
void switcher_forget(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    if (!server->switcher.open)
        return;
    for (int i = 0; i < server->switcher.count; ++i) {
        if (server->switcher.windows[i] != toplevel)
            continue;
        // Its closing ended the switcher's peek at it, if any.
        bool peeked = server->switcher.peeked == toplevel;
        if (peeked)
            server->switcher.peeked = NULL;
        memmove(&server->switcher.windows[i], &server->switcher.windows[i + 1],
                (size_t)(server->switcher.count - i - 1) * sizeof(*server->switcher.windows));
        if (--server->switcher.count == 0) {
            switcher_close(server, -1);
            return;
        }
        bool moved = server->switcher.selected == i;
        if (server->switcher.selected > i || server->switcher.selected == server->switcher.count)
            server->switcher.selected--;
        switcher_announce(server);
        if (!moved)
            return;
        // The selected window gone, the selection is on another. The peek moves on to it once
        // the window has closed, as focus may go there too, which would end a peek begun before.
        server->switcher.selected_at = now_ms();
        if (peeked && server->switcher.peek_asked)
            peek_after(server, 1);
        else
            switcher_follow(server);
        return;
    }
}

/* Keys while the switcher is open: the opening key (with Shift, backward), Tab, and the arrows
 * move the selection, Return confirms, Escape cancels. It keeps every key from the windows. */
void switcher_key(struct sh_server *server, uint32_t modifiers, xkb_keysym_t sym) {
    switch (sym) {
    case XKB_KEY_Escape:
        switcher_close(server, -1);
        return;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        switcher_close(server, server->switcher.selected);
        return;
    case XKB_KEY_ISO_Left_Tab:
    case XKB_KEY_Left:
    case XKB_KEY_Up:
        switcher_select(server, server->switcher.selected - 1);
        return;
    case XKB_KEY_Right:
    case XKB_KEY_Down:
        switcher_select(server, server->switcher.selected + 1);
        return;
    }
    if (sym == XKB_KEY_Tab || xkb_keysym_to_lower(sym) == server->switcher.key)
        switcher_select(server,
                        server->switcher.selected + (modifiers & WLR_MODIFIER_SHIFT ? -1 : 1));
}
