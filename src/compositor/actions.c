/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* Actions as key bindings, button bindings and the control socket name them: run_action hands
 * each to the module that carries it out. Also screenshots, and starting programs. */
#include "server.h"

/* The windows a region screenshot lets the user click, front to back, into `boxes`: those
 * shown, each cut to its output, leaving out any that one window in front covers whole (slurp
 * picks the smallest box under the pointer, which would be one hidden behind a fullscreen
 * window). None while the overview shows its thumbnails in their place. */
static size_t screenshot_windows(struct sh_server *server, struct sh_rect *boxes, size_t size) {
    size_t count = 0;
    if (server->overview.visible || server->xray.visible)
        return 0;
    struct wlr_scene_tree *trees[WINDOW_LAYERS];
    window_layers(server, trees);
    for (size_t i = 0; i < WINDOW_LAYERS; ++i) {
        struct wlr_scene_node *node;
        wl_list_for_each_reverse(node, &trees[i]->children, link) {
            struct sh_node *owner = node->data;
            if (!node->enabled || !owner || owner->kind != SH_NODE_TOPLEVEL ||
                !toplevel_visible(owner->owner) || count == size)
                continue;
            struct sh_toplevel *toplevel = owner->owner;
            struct wlr_box box = toplevel_box(toplevel), screen;
            struct wlr_output *output = find_output(server, toplevel->output);
            if (output) {
                struct wlr_box window = box;
                wlr_output_layout_get_box(server->output_layout, output, &screen);
                wlr_box_intersection(&box, &window, &screen);
            }
            bool covered = wlr_box_empty(&box);
            for (size_t j = 0; j < count && !covered; ++j)
                covered = box.x >= boxes[j].x && box.y >= boxes[j].y &&
                          box.x + box.width <= boxes[j].x + boxes[j].width &&
                          box.y + box.height <= boxes[j].y + boxes[j].height;
            if (!covered)
                boxes[count++] = (struct sh_rect){box.x, box.y, box.width, box.height};
        }
    }
    return count;
}

/* How long a screenshot may keep the cursor hidden should its process not end. */
#define SCREENSHOT_CURSOR_MS 5000

static int screenshot_cursor_timeout(void *data) {
    cursor_show(data, SH_HIDDEN_SCREENSHOT);
    return 0;
}

/* Hides the cursor until the screenshot ends: an output that draws it into its frames (a
 * software cursor, as the headless backend's and some drivers' are) would hand it to grim. Also
 * for one taken without the screenshot action ("cursor hide", as `paw showcase` takes one). */
void screenshot_hide_cursor(struct sh_server *server) {
    cursor_hide(server, SH_HIDDEN_SCREENSHOT);
    if (!server->screenshot.timer)
        server->screenshot.timer = wl_event_loop_add_timer(
            wl_display_get_event_loop(server->wl_display), screenshot_cursor_timeout, server);
    if (server->screenshot.timer)
        wl_event_source_timer_update(server->screenshot.timer, SCREENSHOT_CURSOR_MS);
}

/* "cursor show": the cursor back after a screenshot taken without the screenshot action, unless
 * the action's own is still being taken. */
void screenshot_show_cursor(struct sh_server *server) {
    if (server->screenshot.pid > 0)
        return;
    if (server->screenshot.timer)
        wl_event_source_timer_update(server->screenshot.timer, 0);
    cursor_show(server, SH_HIDDEN_SCREENSHOT);
}

/* A layer surface going. slurp's ("selection") goes once the region is chosen, leaving grim to
 * take it; until then slurp shows a cursor of its own. */
void screenshot_layer_unmapped(struct sh_server *server, const char *namespace) {
    if (server->screenshot.pid > 0 && server->screenshot.mode == SH_SCREENSHOT_REGION &&
        namespace && !strcmp(namespace, "selection"))
        screenshot_hide_cursor(server);
}

/* A child process ended; the screenshot's ends the screenshot, and the cursor shows again. */
void screenshot_exited(struct sh_server *server, int pid) {
    if (pid <= 0 || pid != server->screenshot.pid)
        return;
    server->screenshot.pid = 0;
    if (server->screenshot.timer)
        wl_event_source_timer_update(server->screenshot.timer, 0);
    cursor_show(server, SH_HIDDEN_SCREENSHOT);
}

void screenshot_finish(struct sh_server *server) {
    if (server->screenshot.timer)
        wl_event_source_remove(server->screenshot.timer);
    server->screenshot.timer = NULL;
}

/* Hands the output under the pointer, the focused window's box or the boxes of the windows shown
 * to the configuration side, which runs grim (and slurp) in the background. The cursor is hidden
 * while grim takes the picture. */
bool take_screenshot(struct sh_server *server, enum sh_screenshot_mode mode, char *error,
                     size_t error_size) {
    const char *output_name = NULL;
    struct sh_rect box = {0}, *boxes = &box;
    size_t count = 0;
    if (mode == SH_SCREENSHOT_REGION) {
        size_t size = wl_list_length(&server->toplevels);
        if (size && !(boxes = calloc(size, sizeof(*boxes)))) {
            snprintf(error, error_size, "out of memory");
            return false;
        }
        count = screenshot_windows(server, boxes, size);
    } else if (mode == SH_SCREENSHOT_OUTPUT) {
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
        count = 1;
    }
    int pid = server->callbacks->screenshot(server->callbacks->userdata, mode, output_name, boxes,
                                            count, error, error_size);
    if (boxes != &box)
        free(boxes);
    if (pid <= 0)
        return false;
    server->screenshot.pid = pid;
    server->screenshot.mode = mode;
    // grim starts at once for an output or a window, and only connects once this returns.
    if (mode != SH_SCREENSHOT_REGION)
        screenshot_hide_cursor(server);
    return true;
}

/* Starts the program of a spawn or terminal action. One that cannot start is reported in the
 * log and across the panel, and in `error`. */
bool launch_program(struct sh_server *server, enum sh_action action, char *error,
                    size_t error_size) {
    if (server->callbacks->launch(server->callbacks->userdata, action, error, error_size)) {
        wrapped_count(server, SH_WRAPPED_LAUNCHES);
        return true;
    }
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
    {SH_SHORTCUTS, .open = "shortcuts"},
    {SH_NOTIFICATION_HISTORY, .open = "notifications"},
    {SH_CLIPBOARD_HISTORY, .open = "clipboard"},
    {SH_EMOJI_PICKER, .open = "emoji"},
    {SH_RUN, .open = "run"},
    {SH_POWER_MENU, .open = "power-menu"},
    {SH_DISPLAY_SETTINGS, .open = "display-settings"},
    {SH_DND_TOGGLE, .line = "dnd toggle\n"},
    {SH_DND_ON, .line = "dnd on\n"},
    {SH_DND_OFF, .line = "dnd off\n"},
    {SH_DESKTOP_CLOCK, .line = "desktop-clock toggle\n"},
    {SH_DESKTOP_ICONS, .line = "desktop-icons toggle\n"},
    {SH_WALLPAPER_NEXT, .line = "wallpaper-cycle next\n"},
    {SH_WALLPAPER_PREV, .line = "wallpaper-cycle prev\n"},
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
    // The exploded view gives way to the overview and the switcher, which would show under it.
    if (server->xray.open && (action == SH_OVERVIEW_TOGGLE || action == SH_SWITCHER_NEXT ||
                              action == SH_SWITCHER_PREV || action == SH_SWITCHER_APP_NEXT ||
                              action == SH_SWITCHER_APP_PREV))
        xray_close(server, NULL);
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
    case SH_MOVE_TO_WORKSPACE_NEXT:
    case SH_MOVE_TO_WORKSPACE_PREV:
    case SH_MOVE_TO_WORKSPACE_EMPTY:
        move_to_workspace_beside(server, action == SH_MOVE_TO_WORKSPACE_NEXT   ? 1
                                         : action == SH_MOVE_TO_WORKSPACE_PREV ? -1
                                                                               : 0);
        break;
    case SH_WORKSPACE_NEXT:
    case SH_WORKSPACE_PREV: {
        struct wlr_output *output = focused_output(server);
        if (!output)
            break;
        // They wrap around, so only a single workspace leaves them nowhere to go.
        if (count <= 1) {
            workspace_nudge(server, output, action == SH_WORKSPACE_NEXT ? 1 : -1);
            break;
        }
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
    case SH_SPLIT_TOGGLE:
    case SH_SPLIT_SWAP:
        layout_action(server, action);
        break;
    case SH_PSEUDO_TOGGLE:
        pseudo_toggle(server);
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
        switcher_open(server, action == SH_SWITCHER_PREV, false, 0, XKB_KEY_NoSymbol);
        break;
    case SH_SWITCHER_APP_NEXT:
    case SH_SWITCHER_APP_PREV:
        switcher_open(server, action == SH_SWITCHER_APP_PREV, true, 0, XKB_KEY_NoSymbol);
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
    case SH_PROFILE_AUTO:
        profile_schedule_resume(server);
        break;
    case SH_KEEP_AWAKE_TOGGLE:
    case SH_KEEP_AWAKE_ON:
    case SH_KEEP_AWAKE_OFF:
        set_keep_awake(server, action == SH_KEEP_AWAKE_TOGGLE ? !server->keep_awake
                               : action == SH_KEEP_AWAKE_ON);
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
    case SH_MINIMIZE:
        if (current && !current->minimized && toplevel_mapped(current))
            minimize_toplevel(current);
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
    case SH_SHAKE:
    case SH_GRAVITY:
    case SH_PARTY:
        toys_start(server, action, argument);
        break;
    case SH_CAT:
        cat_switch(server, argument);
        break;
    case SH_AMBIENT:
        ambient_switch(server, argument);
        break;
    case SH_CURSOR_TRAIL:
        cursor_trail_switch(server, argument);
        break;
    case SH_XRAY:
        xray_toggle(server);
        break;
    case SH_HEATMAP:
        heatmap_action(server, argument);
        break;
    case SH_PICK_COLOR:
        color_picker_toggle(server);
        break;
    default:
        arrange_windows(server, action);
        break;
    }
}
