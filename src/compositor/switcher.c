/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* The window switcher: a list of windows, most recently focused first, that the shell draws
 * from the events sent here. */
#include "server.h"

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
        const char *raw_app_id = toplevel_app_id(toplevel), *raw_title = toplevel_title(toplevel);
        snprintf(app_id, sizeof(app_id), "%s", raw_app_id ? raw_app_id : "");
        snprintf(title, sizeof(title), "%s", raw_title ? raw_title : "");
        for (char *c = app_id; *c; ++c)
            *c = *c == '\t' || *c == '\n' || *c == '\r' ? ' ' : *c;
        for (char *c = title; *c; ++c)
            *c = *c == '\t' || *c == '\n' || *c == '\r' ? ' ' : *c;
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
    char line[32];
    int length = snprintf(line, sizeof(line), "switcher-select %d\n", server->switcher.selected);
    send_event(server, line, (size_t)length);
}

/* Focuses the window at `index` in the list (or none, below 0) and closes the switcher. */
void switcher_close(struct sh_server *server, int index) {
    if (!server->switcher.open)
        return;
    struct sh_toplevel *chosen =
        index >= 0 && index < server->switcher.count ? server->switcher.windows[index] : NULL;
    server->switcher.open = false;
    server->switcher.count = 0;
    send_event(server, "switcher-close\n", strlen("switcher-close\n"));
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
#if WLR_HAS_XWAYLAND
        if (toplevel->unmanaged)
            continue;
#endif
        if (toplevel->swallowed)
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
    snprintf(server->switcher.output, sizeof(server->switcher.output), "%s", output->name);
    // The first window is the focused one unless focus is on the bare desktop.
    int next = server->switcher.windows[0] == server->focused_toplevel && count > 1 ? 1 : 0;
    server->switcher.selected = backward ? count - 1 : next;
    switcher_announce(server);
}

/* A window closing while the switcher is open leaves its list. */
void switcher_forget(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    if (!server->switcher.open)
        return;
    for (int i = 0; i < server->switcher.count; ++i) {
        if (server->switcher.windows[i] != toplevel)
            continue;
        memmove(&server->switcher.windows[i], &server->switcher.windows[i + 1],
                (size_t)(server->switcher.count - i - 1) * sizeof(*server->switcher.windows));
        if (--server->switcher.count == 0) {
            switcher_close(server, -1);
            return;
        }
        if (server->switcher.selected > i || server->switcher.selected == server->switcher.count)
            server->switcher.selected--;
        switcher_announce(server);
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
