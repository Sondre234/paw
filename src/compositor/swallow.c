/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* Window swallowing (windows.swallow). A window started from a terminal, which the process
 * ancestry shows, takes the terminal's slot and hides it; when the window closes, the terminal
 * takes the slot back. */
#include "server.h"

static bool swallow_listed(const char (*names)[64], int count, const char *name) {
    for (int i = 0; name && i < count; ++i) {
        if (!strcasecmp(names[i], name))
            return true;
    }
    return false;
}

static bool swallow_terminal(struct sh_toplevel *toplevel) {
    const struct sh_settings *settings = server_settings(toplevel->server);
    return swallow_listed(settings->swallow_terminals, settings->swallow_terminal_count,
                          toplevel_app_id(toplevel));
}

/* The parent of a process, or 0 when it cannot be read. */
static pid_t process_parent(pid_t pid) {
    char path[64], text[512];
    snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);
    FILE *file = fopen(path, "r");
    if (!file)
        return 0;
    size_t length = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[length] = '\0';
    // "pid (name) state ppid ...": the name may hold spaces and parentheses.
    char *end = strrchr(text, ')');
    char state;
    int parent = 0;
    if (!end || sscanf(end + 1, " %c %d", &state, &parent) != 2)
        return 0;
    return parent;
}

/* A window that can lend its slot to another. */
static bool swallow_hostable(struct sh_toplevel *host) {
    return toplevel_mapped(host) && !host->swallowed && !host->swallow_peer && !host->group &&
           !host->minimized && !host->scratchpad && !host->sticky && !host->unmanaged;
}

/* The window `child` was started from: that of its nearest ancestor process which has one
 * (the focused or else the most recently focused, when a process has several). With
 * `terminals_only` it has to be one of the configured terminals. */
struct sh_toplevel *swallow_host(struct sh_toplevel *child, bool terminals_only) {
    struct sh_server *server = child->server;
    pid_t pid = toplevel_pid(child), self = getpid();
    if (pid <= 1)
        return NULL;
    pid = process_parent(pid);
    for (int depth = 0; pid > 1 && pid != self && depth < 64; ++depth, pid = process_parent(pid)) {
        struct sh_toplevel *best = NULL, *host;
        wl_list_for_each(host, &server->toplevels, link) {
            if (host == child || !swallow_hostable(host) || toplevel_pid(host) != pid ||
                (terminals_only && !swallow_terminal(host)))
                continue;
            if (!best || host == server->focused_toplevel)
                best = host;
        }
        if (best)
            return best;
    }
    return NULL;
}

/* Whether a window opening now may swallow its terminal by itself. */
bool swallow_wanted(struct sh_toplevel *child) {
    const struct sh_settings *settings = server_settings(child->server);
    return settings->swallow && !swallow_terminal(child) && !toplevel_is_dialog(child) &&
           !swallow_listed(settings->swallow_exceptions, settings->swallow_exception_count,
                           toplevel_app_id(child));
}

/* `child` takes the place of `host`, which hides and leaves the taskbar. */
void swallow_attach(struct sh_toplevel *host, struct sh_toplevel *child) {
    struct sh_server *server = host->server;
    if (host->fullscreen)
        set_fullscreen(host, false); // it could not hide, and would return fullscreen
    if (child->tiled)
        untile_toplevel(child, false);
    host->swallowed = true;
    host->swallow_peer = child;
    child->swallow_peer = host;
    hand_over_slot(host, child);
    unpublish_toplevel(host);
    if (server->switcher.open)
        switcher_forget(host);
    overview_forget(host);
    notify_subscribers(server);
}

/* The window `child` is going away: the terminal it swallowed takes its place again. */
static void swallow_restore(struct sh_toplevel *child) {
    struct sh_toplevel *host = child->swallow_peer;
    child->swallow_peer = NULL;
    if (!host)
        return;
    struct sh_server *server = child->server;
    host->swallow_peer = NULL;
    host->swallowed = false;
    if (child->fullscreen)
        set_fullscreen(child, false);
    bool had_focus = server->focused_toplevel == child;
    hand_over_slot(child, host);
    publish_toplevel(host);
    if (had_focus)
        focus_toplevel(host);
    notify_subscribers(server);
}

/* The window `child` stays; the terminal it swallowed comes back beside it. */
void swallow_release(struct sh_toplevel *child) {
    struct sh_toplevel *host = child->swallow_peer;
    child->swallow_peer = NULL;
    if (!host)
        return;
    struct sh_server *server = child->server;
    host->swallow_peer = NULL;
    host->swallowed = false;
    publish_toplevel(host);
    wlr_scene_node_set_enabled(&host->scene_tree->node, toplevel_visible(host));
    struct wlr_output *output = child->tiled ? tiled_output(child) : NULL;
    if (output && wants_tiling(host, output)) {
        tile_toplevel_at(host, output, child, false, 0, 0);
    } else if (!host->tiled) {
        struct wlr_box box = toplevel_box(child);
        box.x += 32, box.y += 32;
        toplevel_configure_box(host, box);
    }
    refresh_frame(host);
    notify_subscribers(server);
}

/* A window that swallowed or was swallowed is unmapping. */
void swallow_end(struct sh_toplevel *toplevel) {
    struct sh_toplevel *peer = toplevel->swallow_peer;
    if (!peer)
        return;
    if (toplevel->swallowed) {
        // The terminal closed: the window that took its place stays where it is.
        peer->swallow_peer = NULL;
        toplevel->swallow_peer = NULL;
        toplevel->swallowed = false;
    } else {
        swallow_restore(toplevel);
    }
}

/* swallow_toggle: the focused window gives its terminal a place again, or, when it has none
 * swallowed, takes the place of the terminal it was started from (else of the terminal that
 * was focused last on its workspace). */
void swallow_toggle(struct sh_server *server, struct sh_toplevel *current) {
    if (!current || server->locked || !toplevel_mapped(current) || !toplevel_visible(current))
        return;
    if (current->swallow_peer) {
        swallow_release(current);
        return;
    }
    struct sh_toplevel *host = swallow_host(current, false), *other;
    if (!host) {
        wl_list_for_each(other, &server->toplevels, link) {
            if (other != current && swallow_hostable(other) && swallow_terminal(other) &&
                toplevel_visible(other) && other->workspace == current->workspace &&
                !strcmp(other->output, current->output)) {
                host = other;
                break;
            }
        }
    }
    if (!host)
        return;
    swallow_attach(host, current);
    focus_toplevel(current);
}
