/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* Actions as key bindings, button bindings and the control socket name them: run_action hands
 * each to the module that carries it out. Also screenshots, and starting programs. */
#include "server.h"

/* Hands the output under the pointer or the focused window's box to the configuration side, which
 * runs grim in the background. */
bool take_screenshot(struct sh_server *server, enum sh_screenshot_mode mode, char *error,
                     size_t error_size) {
    const char *output_name = NULL;
    struct sh_rect box = {0};
    if (mode == SH_SCREENSHOT_OUTPUT) {
        struct wlr_output *output = wlr_output_layout_output_at(
            server->output_layout, server->cursor->x, server->cursor->y);
        if (!output) {
            snprintf(error, error_size, "no output under the pointer");
            return false;
        }
        output_name = output->name;
    } else if (mode == SH_SCREENSHOT_WINDOW) {
        struct sh_toplevel *current = current_toplevel(server);
        if (!current) {
            snprintf(error, error_size, "no focused window");
            return false;
        }
        struct wlr_box geometry = toplevel_box(current);
        box = (struct sh_rect){geometry.x, geometry.y, geometry.width, geometry.height};
    }
    return server->callbacks->screenshot(server->callbacks->userdata, mode, output_name, &box,
                                         error, error_size);
}

/* Starts the program of a spawn or terminal action. One that cannot start is reported in the
 * log and across the panel, and in `error`. */
bool launch_program(struct sh_server *server, enum sh_action action, char *error,
                    size_t error_size) {
    if (server->callbacks->launch(server->callbacks->userdata, action, error, error_size))
        return true;
    report_failure(server, "spawn-error", error);
    return false;
}

/* Actions the shell carries out: each sends it a line, or asks it to open something on the output
 * under the pointer (request_shell). */
static const struct {
    enum sh_action action;
    const char *line, *open;
} shell_actions[] = {
    {SH_LAUNCHER, .open = "launcher"},
    {SH_PALETTE, .open = "palette"},
    {SH_NOTIFICATION_HISTORY, .open = "notifications"},
    {SH_CLIPBOARD_HISTORY, .open = "clipboard"},
    {SH_EMOJI_PICKER, .open = "emoji"},
    {SH_POWER_MENU, .open = "power-menu"},
    {SH_DISPLAY_SETTINGS, .open = "display-settings"},
    {SH_DND_TOGGLE, .line = "dnd toggle\n"},
    {SH_DND_ON, .line = "dnd on\n"},
    {SH_DND_OFF, .line = "dnd off\n"},
    {SH_MEDIA_PLAY_PAUSE, .line = "media play-pause\n"},
    {SH_MEDIA_NEXT, .line = "media next\n"},
    {SH_MEDIA_PREVIOUS, .line = "media previous\n"},
    {SH_MEDIA_STOP, .line = "media stop\n"},
};

/* Shared by key bindings and the control socket. */
void run_action(struct sh_server *server, enum sh_action action, int argument) {
    // Snap Assist gives way to any action but those that pick from it or dismiss it.
    if (server->overview.open && server->overview.assist && action != SH_NONE &&
        action != SH_OVERVIEW_CONFIRM && action != SH_OVERVIEW_CANCEL)
        overview_close(server, NULL, -1);
    for (size_t i = 0; i < sizeof(shell_actions) / sizeof(*shell_actions); ++i) {
        if (shell_actions[i].action != action)
            continue;
        if (shell_actions[i].line)
            send_shell_line(server, shell_actions[i].line);
        else
            request_shell(server, shell_actions[i].open);
        return;
    }
    int count = server_settings(server)->workspaces;
    struct sh_toplevel *current = current_toplevel(server);
    switch (action) {
    case SH_NONE:
        break;
    case SH_SPAWN:
    case SH_TERMINAL: {
        char error[256] = "";
        launch_program(server, action, error, sizeof(error));
        break;
    }
    case SH_QUIT:
        session_save_last(server);
        wl_display_terminate(server->wl_display);
        break;
    case SH_RELOAD:
        reload_config(server);
        break;
    case SH_CYCLE: {
        // Raise the least recently focused visible window.
        struct sh_toplevel *toplevel;
        wl_list_for_each_reverse(toplevel, &server->toplevels, link) {
            if (toplevel != current && toplevel_visible(toplevel)) {
                focus_toplevel(toplevel);
                break;
            }
        }
        break;
    }
    case SH_FOCUS_LAST:
        focus_last(server);
        break;
    case SH_FOCUS_URGENT:
        focus_urgent(server);
        break;
    case SH_GROUP_TOGGLE:
        if (groups_enabled(server))
            group_toggle(server, current);
        break;
    case SH_GROUP_NEXT:
    case SH_GROUP_PREV:
        if (groups_enabled(server))
            group_cycle(server, current, action == SH_GROUP_NEXT ? 1 : -1);
        break;
    case SH_UNGROUP:
        if (groups_enabled(server))
            ungroup(server, current);
        break;
    case SH_GROUP_MERGE_LEFT:
    case SH_GROUP_MERGE_RIGHT:
    case SH_GROUP_MERGE_UP:
    case SH_GROUP_MERGE_DOWN:
        if (groups_enabled(server))
            group_merge(server, action);
        break;
    case SH_FULLSCREEN:
        if (current)
            set_fullscreen(current, !current->fullscreen);
        break;
    case SH_CLOSE:
        if (current)
            toplevel_close(current);
        break;
    case SH_WORKSPACE: {
        struct wlr_output *output = focused_output(server);
        int workspace = argument - 1;
        // With back-and-forth, naming the workspace already shown returns to the previous one.
        if (output && server_settings(server)->workspace_back_and_forth &&
            workspace == *output_workspace(server, output->name))
            workspace = server->output_workspaces[output_slot(server, output->name)].previous;
        switch_workspace(server, output, workspace);
        break;
    }
    case SH_WORKSPACE_BACK: {
        struct wlr_output *output = focused_output(server);
        if (output)
            switch_workspace(server, output,
                             server->output_workspaces[output_slot(server, output->name)].previous);
        break;
    }
    case SH_MOVE_TO_WORKSPACE:
        move_to_workspace(server, argument - 1);
        break;
    case SH_WORKSPACE_NEXT:
    case SH_WORKSPACE_PREV: {
        struct wlr_output *output = focused_output(server);
        if (!output)
            break;
        int step = action == SH_WORKSPACE_NEXT ? 1 : count - 1;
        switch_workspace(server, output, (*output_workspace(server, output->name) + step) % count);
        break;
    }
    case SH_TOGGLE_TILING: {
        struct wlr_output *output = focused_output(server);
        set_tiling(server, output, !output_tiles(server, output));
        break;
    }
    case SH_LAYOUT_NEXT:
    case SH_LAYOUT_PREV:
    case SH_SET_LAYOUT_DWINDLE:
    case SH_SET_LAYOUT_MASTER:
    case SH_SET_LAYOUT_SPIRAL:
    case SH_SET_LAYOUT_MONOCLE:
    case SH_SET_LAYOUT_SCROLL:
    case SH_SCROLL_LEFT:
    case SH_SCROLL_RIGHT:
    case SH_COLUMN_WIDEN:
    case SH_COLUMN_NARROW:
    case SH_COLUMN_CYCLE_WIDTH:
    case SH_CONSUME_LEFT:
    case SH_CONSUME_RIGHT:
    case SH_EXPEL:
    case SH_CENTER_COLUMN:
    case SH_PROMOTE:
    case SH_FOCUS_NEXT:
    case SH_FOCUS_PREV:
    case SH_SWAP_NEXT:
    case SH_SWAP_PREV:
    case SH_MASTER_GROW:
    case SH_MASTER_SHRINK:
    case SH_MASTER_MORE:
    case SH_MASTER_LESS:
        layout_action(server, action);
        break;
    case SH_TASKBAR_FOCUS:
        request_taskbar(server);
        break;
    case SH_FOCUS_LEFT:
    case SH_FOCUS_RIGHT:
    case SH_FOCUS_UP:
    case SH_FOCUS_DOWN:
        focus_direction(server, action);
        break;
    case SH_MOVE_LEFT:
    case SH_MOVE_RIGHT:
    case SH_MOVE_UP:
    case SH_MOVE_DOWN:
        move_window(server, action);
        break;
    case SH_MOVE_TO_SCRATCHPAD:
        if (current && scratchpad_enabled(server))
            hide_in_scratchpad(current);
        break;
    case SH_SCRATCHPAD_SHOW:
        if (scratchpad_enabled(server))
            scratchpad_show(server);
        break;
    case SH_RESIZE_LEFT:
    case SH_RESIZE_RIGHT:
    case SH_RESIZE_UP:
    case SH_RESIZE_DOWN:
        resize_window(server, action, argument);
        break;
    case SH_SCREENSHOT: {
        char error[256] = "";
        if (!take_screenshot(server, (enum sh_screenshot_mode)argument, error, sizeof(error)))
            wlr_log(WLR_ERROR, "Screenshot not taken: %s", error);
        break;
    }
    case SH_SWITCHER_NEXT:
    case SH_SWITCHER_PREV:
        switcher_open(server, action == SH_SWITCHER_PREV, 0, XKB_KEY_NoSymbol);
        break;
    case SH_SWITCHER_CONFIRM:
        switcher_close(server, argument > 0 ? argument - 1 : server->switcher.selected);
        break;
    case SH_SWITCHER_CANCEL:
        switcher_close(server, -1);
        break;
    case SH_OVERVIEW_TOGGLE:
        if (server->overview.open)
            overview_close(server, NULL, -1);
        else
            overview_open(server);
        break;
    case SH_OVERVIEW_CONFIRM:
        overview_confirm(server, argument > 0 ? argument - 1 : server->overview.selected);
        break;
    case SH_OVERVIEW_CANCEL:
        overview_close(server, NULL, -1);
        break;
    case SH_PEEK:
    case SH_PEEK_TOGGLE:
        set_peek(server, !server->peeking);
        break;
    case SH_NIGHT_LIGHT_TOGGLE:
    case SH_NIGHT_LIGHT_ON:
    case SH_NIGHT_LIGHT_OFF:
    case SH_NIGHT_LIGHT_AUTO:
        server->night_mode =
            action == SH_NIGHT_LIGHT_TOGGLE
                ? (server->night_kelvin < SH_KELVIN_NEUTRAL ? SH_NIGHT_OFF : SH_NIGHT_ON)
            : action == SH_NIGHT_LIGHT_ON  ? SH_NIGHT_ON
            : action == SH_NIGHT_LIGHT_OFF ? SH_NIGHT_OFF
                                           : SH_NIGHT_AUTO;
        night_light_update(server);
        break;
    case SH_ZOOM_IN:
        zoom_by(server, 1);
        break;
    case SH_ZOOM_OUT:
        zoom_by(server, -1);
        break;
    case SH_ZOOM_RESET:
        zoom_by(server, 0);
        break;
    case SH_SWALLOW_TOGGLE:
        swallow_toggle(server, current);
        break;
    case SH_SWITCH_LAYOUT:
        switch_keyboard_layout(server, argument);
        break;
    case SH_MOVE_WORKSPACE_TO_OUTPUT:
    case SH_SWAP_WORKSPACES: {
        const char *target = server->callbacks->action_target
                                 ? server->callbacks->action_target(server->callbacks->userdata)
                                 : "";
        if (action == SH_MOVE_WORKSPACE_TO_OUTPUT)
            move_workspace_to_output(server, target);
        else
            swap_output_workspaces(server, target);
        break;
    }
    case SH_POWER_OFF:
    case SH_REBOOT:
    case SH_SUSPEND:
    case SH_HIBERNATE:
    case SH_LOGOUT:
    case SH_LOCK:
        power_run(server, action);
        break;
    case SH_DISPLAY_OFF:
    case SH_DISPLAY_ON:
    case SH_DISPLAY_TOGGLE: {
        char error[128];
        if (!display_power(server, action, error, sizeof(error)))
            wlr_log(WLR_ERROR, "%s", error);
        break;
    }
    case SH_DISPLAY_MODE: {
        char error[128];
        if (!display_mode_choose(server, argument, error, sizeof(error)))
            wlr_log(WLR_ERROR, "%s", error);
        break;
    }
    case SH_TOGGLE_STICKY:
        if (current && server_settings(server)->sticky)
            set_sticky(current, !current->sticky, true);
        break;
    case SH_TOGGLE_ABOVE:
        if (current)
            set_above(current, !current->above);
        break;
    case SH_TOGGLE_FLOATING:
        if (current)
            set_floating(current, current->tiled, true);
        break;
    case SH_SNAP_CYCLE_LEFT:
    case SH_SNAP_CYCLE_RIGHT:
    case SH_SNAP_CYCLE_UP:
    case SH_SNAP_CYCLE_DOWN:
        snap_cycle(server, action);
        break;
    case SH_VOLUME_UP:
    case SH_VOLUME_DOWN:
    case SH_VOLUME_MUTE:
    case SH_MIC_MUTE:
    case SH_BRIGHTNESS_UP:
    case SH_BRIGHTNESS_DOWN:
        volume_action(server, action, argument);
        break;
    case SH_MODE:
        set_binding_mode(server, argument);
        break;
    case SH_TOGGLE_SHORTCUTS_INHIBIT:
        toggle_shortcuts_inhibit(server);
        break;
    default:
        arrange_windows(server, action);
        break;
    }
}
