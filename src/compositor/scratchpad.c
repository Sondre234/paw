/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* Sway's scratchpad. A window put there floats and hides, listed in the taskbar as minimized.
 * scratchpad_show brings one to the middle of the focused output's current workspace, where it
 * stays in the scratchpad (and hides again on the next scratchpad_show) until it is moved to a
 * workspace or tiled. */
#include "server.h"

bool scratchpad_enabled(struct sh_server *server) {
    if (server_settings(server)->scratchpad)
        return true;
    if (!server->scratchpad_off_logged)
        wlr_log(WLR_INFO, "Scratchpad actions ignored: features.scratchpad is false");
    server->scratchpad_off_logged = true;
    return false;
}

/* Moves a scratchpad window onto the current workspace of `output`, centred in its floating
 * area and no larger than it. */
void center_scratchpad(struct sh_toplevel *toplevel, struct wlr_output *output) {
    struct sh_server *server = toplevel->server;
    if (!output)
        return;
    snprintf(toplevel->output, sizeof(toplevel->output), "%s", output->name);
    toplevel->workspace = *output_workspace(server, output->name);
    struct sh_rect area = floating_area(server, output);
    struct wlr_box box = toplevel_box(toplevel);
    box.width = box.width > area.width ? area.width : box.width;
    box.height = box.height > area.height ? area.height : box.height;
    box.x = area.x + (area.width - box.width) / 2;
    box.y = area.y + (area.height - box.height) / 2;
    toplevel_configure_box(toplevel, box);
}

/* Floats the window at its own size and hides it in the scratchpad, behind the others. */
void hide_in_scratchpad(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    if (server->grabbed_toplevel == toplevel)
        reset_cursor_mode(server);
    if (toplevel->fullscreen)
        set_fullscreen(toplevel, false);
    if (toplevel->sticky)
        set_sticky(toplevel, false, false); // a hidden window shows on no workspace
    toplevel->floating = true;
    toplevel->placed = false;
    if (toplevel->tiled)
        untile_toplevel(toplevel, true);
    else
        restore_toplevel(toplevel); // snapped or maximized
    toplevel->scratchpad = true;
    toplevel->scratchpad_order = ++server->scratchpad_serial;
    minimize_toplevel(toplevel);
    notify_subscribers(server); // its workspace may be empty now
}

/* Hides the focused scratchpad window; else focuses one shown on the focused output; else
 * shows the hidden one put there longest ago, or failing that takes one shown elsewhere. */
void scratchpad_show(struct sh_server *server) {
    struct sh_toplevel *focused = server->focused_toplevel;
    if (focused && focused->scratchpad) {
        hide_in_scratchpad(focused);
        return;
    }
    struct wlr_output *output = focused_output(server);
    struct sh_toplevel *toplevel, *next = NULL;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel->scratchpad && toplevel_visible(toplevel) &&
            find_output(server, toplevel->output) == output) {
            focus_toplevel(toplevel);
            return;
        }
    }
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (!toplevel->scratchpad)
            continue;
        if (!next || (toplevel->minimized && !next->minimized) ||
            (toplevel->minimized == next->minimized &&
             toplevel->scratchpad_order < next->scratchpad_order))
            next = toplevel;
    }
    if (!next || !output)
        return;
    center_scratchpad(next, output);
    focus_toplevel(next);
    notify_subscribers(server);
}

/* With features.scratchpad turned off, its windows come back to the focused output. */
void empty_scratchpad(struct sh_server *server) {
    struct wlr_output *output = focused_output(server);
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (!toplevel->scratchpad)
            continue;
        toplevel->scratchpad = false;
        if (!toplevel->minimized)
            continue;
        center_scratchpad(toplevel, output);
        toplevel->minimized = false;
        if (toplevel->foreign)
            wlr_foreign_toplevel_handle_v1_set_minimized(toplevel->foreign, false);
    }
}
