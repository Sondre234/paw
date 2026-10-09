/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* The glue between windows and the tiling layouts (src/tiling.cpp): which outputs and
 * workspaces tile, windows joining and leaving the tiling, the layout actions, and the layout
 * settings. */
#include "server.h"

struct wlr_output *tiled_output(struct sh_toplevel *toplevel) {
    const char *name = sh_tiling_output(toplevel->server->tiling, toplevel);
    return name ? find_output(toplevel->server, name) : NULL;
}

enum sh_tile_layout toplevel_layout(struct sh_toplevel *toplevel) {
    const char *name = sh_tiling_output(toplevel->server->tiling, toplevel);
    return name ? sh_tiling_layout(toplevel->server->tiling, name, toplevel->workspace)
                : SH_LAYOUT_DWINDLE;
}

/* The tiling setting the config gives `output`: its own, else layout.tiling. */
static bool configured_tiling(struct sh_server *server, struct wlr_output *output) {
    const struct sh_settings *settings = server_settings(server);
    const struct sh_monitor *monitor = monitor_settings(settings, output);
    return monitor && monitor->tiling >= 0 ? monitor->tiling : settings->tiling;
}

/* Whether `output` as a whole tiles its windows automatically: as configured, until it is
 * toggled. */
bool output_default_tiling(struct sh_server *server, struct wlr_output *output) {
    if (!output)
        return false;
    int slot = output_slot(server, output->name);
    if (server->output_workspaces[slot].tiling < 0) {
        bool configured = configured_tiling(server, output);
        server->output_workspaces[slot].tiling = configured;
        server->output_workspaces[slot].configured = configured;
    }
    return server->output_workspaces[slot].tiling;
}

/* Whether `workspace` of `output` tiles: as the output does, or with
 * layout.tiling_per_workspace as that workspace was toggled to. */
bool workspace_tiles(struct sh_server *server, struct wlr_output *output, int workspace) {
    bool tiles = output_default_tiling(server, output);
    if (!output || !server_settings(server)->tiling_per_workspace || workspace < 0 ||
        workspace >= (int)sizeof(server->output_workspaces[0].workspace_tiling))
        return tiles;
    int own = server->output_workspaces[output_slot(server, output->name)].workspace_tiling[workspace];
    return own < 0 ? tiles : own;
}

/* Whether the workspace `output` shows tiles its windows automatically. */
bool output_tiles(struct sh_server *server, struct wlr_output *output) {
    return output && workspace_tiles(server, output, *output_workspace(server, output->name));
}

/* Whether windows tile where `toplevel` is, or lands on `output`: on the workspace it is on,
 * the one `output` shows when it moves there, or the current one when it is sticky. */
bool tiles_for(struct sh_toplevel *toplevel, struct wlr_output *output) {
    if (!output)
        return false;
    if (toplevel->sticky || strcmp(toplevel->output, output->name))
        return output_tiles(toplevel->server, output);
    return workspace_tiles(toplevel->server, output, toplevel->workspace);
}

/* The output whose tiling an untiled window joins: the one it was placed on, else the one it
 * is on. */
struct wlr_output *home_output(struct sh_toplevel *toplevel) {
    struct wlr_output *output = find_output(toplevel->server, toplevel->output);
    return output ? output : toplevel_output(toplevel);
}

/* Whether the window should join the tiling of `output` (by default its home output). */
bool wants_tiling(struct sh_toplevel *toplevel, struct wlr_output *output) {
    return !toplevel->tiled && !toplevel->group_hidden && !toplevel->swallowed && !toplevel->floating && !toplevel->sticky && !toplevel->minimized &&
           tiles_for(toplevel, output ? output : home_output(toplevel));
}

/* Adds a window to the tiling of `output` (by default the one it is on), splitting `target`
 * when that is tiled there, else the tile under the point x, y with `has_point`. */
void tile_toplevel_at(struct sh_toplevel *toplevel, struct wlr_output *output,
                      struct sh_toplevel *target, bool has_point, double x, double y) {
    struct sh_server *server = toplevel->server;
    if (!output)
        output = home_output(toplevel);
    if (!output || toplevel->tiled)
        return;
    set_toplevel_output(toplevel, output);
    if (!toplevel->arranged)
        toplevel->restore_box =
            toplevel->fullscreen ? toplevel->fullscreen_restore : toplevel_box(toplevel);
    if (toplevel->tile_sized) // Floating later lets the client choose its size.
        toplevel->restore_box.width = toplevel->restore_box.height = 0;
    toplevel->tile_sized = false;
    toplevel->arranged = toplevel->placed = false;
    toplevel->tiled = true;
    restack_toplevel(toplevel);
    if (toplevel->foreign)
        wlr_foreign_toplevel_handle_v1_set_maximized(toplevel->foreign, false);
    sh_tiling_insert(server->tiling, output->name, toplevel->workspace, toplevel, target, has_point,
                     x, y);
    reflow_output(server, output);
    window_objects_changed(server);
}

/* As tile_toplevel_at, at the tile under the pointer with `at_cursor`. */
void tile_toplevel(struct sh_toplevel *toplevel, struct wlr_output *output,
                   struct sh_toplevel *target, bool at_cursor) {
    struct wlr_cursor *cursor = toplevel->server->cursor;
    tile_toplevel_at(toplevel, output, target, at_cursor, cursor->x, cursor->y);
}

/* Takes a window out of the tiling. `restore` returns it to its floating geometry; otherwise
 * it stays where it is, arranged without a rule, until something else places it. */
void untile_toplevel(struct sh_toplevel *toplevel, bool restore) {
    struct sh_server *server = toplevel->server;
    if (!toplevel->tiled)
        return;
    struct wlr_output *output = tiled_output(toplevel);
    sh_tiling_remove(server->tiling, toplevel);
    toplevel->tiled = false;
    restack_toplevel(toplevel);
    toplevel->arranged = !restore;
    toplevel->arrangement = SH_NONE;
    // The floating geometry was saved where the window entered the tiling, maybe elsewhere.
    // A window that opened tiled has no floating size: it floats where its tile is now.
    if (restore && (toplevel->restore_box.width <= 0 || toplevel->restore_box.height <= 0)) {
        struct wlr_box tile = toplevel_box(toplevel);
        toplevel->restore_box.x = tile.x;
        toplevel->restore_box.y = tile.y;
    } else if (restore) {
        toplevel->restore_box = rebase_box(server, toplevel->restore_box, output);
    }
    if (restore && toplevel->fullscreen) {
        toplevel->fullscreen_restore = toplevel->restore_box;
    } else if (restore) {
        toplevel_set_states(toplevel, false, 0);
        toplevel_configure_box(toplevel, toplevel->restore_box);
    }
    if (output && tiles_for(toplevel, output))
        reflow_output(server, output);
    window_objects_changed(server);
}

/* Takes the window out of the tiling, floating where it was before it tiled, or lets it tile
 * again: a sticky window stops being sticky to, and one that is not tiled joins the tiling of
 * its workspace when that tiles, with `at_cursor` at the tile under the pointer. A floating
 * window stays out of the tiling when its workspace starts tiling, a sticky one once it is no
 * longer sticky. */
void set_floating(struct sh_toplevel *toplevel, bool floating, bool at_cursor) {
    if (floating && toplevel->sticky) {
        toplevel->sticky_floating = true;
    } else if (floating) {
        toplevel->floating = true;
        toplevel->placed = false;
        untile_toplevel(toplevel, true);
    } else if (toplevel->sticky) {
        toplevel->sticky_floating = false; // it tiles once it is no longer sticky
        set_sticky(toplevel, false, true);
    } else {
        toplevel->floating = false;
        toplevel->scratchpad = false; // tiled, it leaves the scratchpad
        if (wants_tiling(toplevel, NULL))
            tile_toplevel(toplevel, NULL, NULL, at_cursor);
    }
    window_objects_changed(toplevel->server);
}

/* Tiles of an output disabled in the config join the tiling of the output they are nearest
 * now, or float there if it does not tile; windows that floated only because their output did
 * not tile join the tiling of one that does. Tiles of an unplugged output wait for it:
 * monitors drop off when they sleep. */
void rehome_tiles(struct sh_server *server) {
    if (!first_output(server))
        return;
    bool moved = false;
    struct sh_toplevel *toplevel;
    wl_list_for_each_reverse(toplevel, &server->toplevels, link) {
        if (toplevel->tiled && !tiled_output(toplevel)) {
            untile_toplevel(toplevel, false);
            if (wants_tiling(toplevel, NULL))
                tile_toplevel(toplevel, NULL, NULL, false);
            else
                restore_toplevel(toplevel);
            moved = true;
        } else if (toplevel != server->grabbed_toplevel && toplevel_mapped(toplevel) &&
                   wants_tiling(toplevel, NULL)) {
            tile_toplevel(toplevel, NULL, NULL, false);
            moved = true;
        }
    }
    if (moved)
        refit_fullscreen(server); // Fullscreen tiles follow to their new output.
}

/* Tiles or floats the windows of `output` as their workspaces now say. */
static void apply_tiling(struct sh_server *server, struct wlr_output *output) {
    // Most recently focused first, so the focused window gets the largest tile. Tiles of an
    // unplugged output are left waiting for it.
    // Placing every window after each one joins or leaves would take time quadratic in the
    // number of windows; the reflow after the loop places them once.
    ++server->reflow_held;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel->tiled ? tiled_output(toplevel) != output : home_output(toplevel) != output)
            continue;
        bool enabled = tiles_for(toplevel, output);
        if (enabled && toplevel->placed) // floating only because it was snapped or maximized
            toplevel->floating = toplevel->placed = false;
        if (enabled && wants_tiling(toplevel, output))
            tile_toplevel(toplevel, output, NULL, false);
        else if (!enabled && toplevel->tiled)
            untile_toplevel(toplevel, true);
        else if (!enabled && toplevel->arranged && toplevel->arrangement == SH_NONE)
            restore_toplevel(toplevel); // left the tiling while minimized
    }
    --server->reflow_held;
    reflow_output(server, output); // maximized windows gain or lose their border
    // Floating windows, which no reflow touches, gain or lose their rounded corners.
    wl_list_for_each(toplevel, &server->toplevels, link) refresh_frame(toplevel);
    notify_subscribers(server);
}

/* Turns automatic tiling of `output` on or off: for the windows on all its workspaces, or with
 * layout.tiling_per_workspace for those on the one it shows. */
void set_tiling(struct sh_server *server, struct wlr_output *output, bool enabled) {
    if (!output || output_tiles(server, output) == enabled)
        return;
    int slot = output_slot(server, output->name);
    int workspace = *output_workspace(server, output->name);
    if (server_settings(server)->tiling_per_workspace) {
        server->output_workspaces[slot].workspace_tiling[workspace] = enabled;
        wlr_log(WLR_INFO, "Tiling %s on %s workspace %d", enabled ? "on" : "off", output->name,
                workspace + 1);
    } else {
        server->output_workspaces[slot].tiling = enabled;
        memset(server->output_workspaces[slot].workspace_tiling, -1,
               sizeof(server->output_workspaces[slot].workspace_tiling));
        wlr_log(WLR_INFO, "Tiling %s on %s", enabled ? "on" : "off", output->name);
    }
    apply_tiling(server, output);
}

/* Turns automatic tiling of `output` as a whole on or off; workspaces toggled on their own with
 * layout.tiling_per_workspace keep their state. */
void set_output_tiling(struct sh_server *server, struct wlr_output *output, bool enabled) {
    if (!output || output_default_tiling(server, output) == enabled)
        return;
    server->output_workspaces[output_slot(server, output->name)].tiling = enabled;
    wlr_log(WLR_INFO, "Tiling %s on %s", enabled ? "on" : "off", output->name);
    apply_tiling(server, output);
}

/* The rectangle a tile got in the last arrangement. */
struct tile_lookup {
    void *window;
    struct sh_rect rect;
    bool found;
};
static void find_tile(void *data, void *window, struct sh_rect rect) {
    struct tile_lookup *lookup = data;
    if (window == lookup->window) {
        lookup->rect = rect;
        lookup->found = true;
    }
}

/* Moves a tile to the far side of `neighbour`, as Hyprland's dwindle layout does: out of the
 * tiling, then in again splitting the neighbour on the side away from where it came from. */
void move_tile(struct sh_toplevel *toplevel, struct sh_toplevel *neighbour,
               struct wlr_output *output, bool horizontal, int sign) {
    struct sh_server *server = toplevel->server;
    struct tile_lookup lookup = {neighbour, {0}, false};
    sh_tiling_remove(server->tiling, toplevel);
    // Arranging without the window gives the neighbour the box it is split from.
    int gap;
    struct sh_rect area = tiling_area(server, output, toplevel->workspace, 0, &gap);
    sh_tiling_arrange(server->tiling, output->name, toplevel->workspace, area, gap, find_tile,
                      &lookup);
    struct sh_rect r = lookup.rect;
    double x = r.x + r.width / 2.0, y = r.y + r.height / 2.0;
    if (horizontal)
        x = sign < 0 ? r.x : r.x + r.width - 1;
    else
        y = sign < 0 ? r.y : r.y + r.height - 1;
    sh_tiling_insert(server->tiling, output->name, toplevel->workspace, toplevel,
                     lookup.found ? neighbour : NULL, lookup.found, x, y);
    reflow_output(server, output);
}

/* A reload applies tiling settings that changed in the config; outputs toggled since keep
 * their state while the config for them stays the same. */
void configure_layouts(struct sh_server *server) {
    const struct sh_settings *settings = server_settings(server);
    sh_tiling_set_defaults(server->tiling, (enum sh_tile_layout)settings->tile_layout,
                           settings->master_ratio, settings->master_count);
    sh_tiling_set_scroll(server->tiling, (enum sh_scroll_follow)settings->scroll_follow,
                         settings->scroll_width, settings->scroll_step, settings->scroll_presets,
                         settings->scroll_preset_count);
}

/* layout.outputs for one output: the entry by connector name wins over one by description. */
void apply_output_layout(struct sh_server *server, const struct wlr_output *output) {
    const struct sh_settings *settings = server_settings(server);
    const struct sh_output_layout *found = NULL;
    for (int i = 0; i < settings->output_layout_count; ++i) {
        const struct sh_output_layout *entry = &settings->output_layouts[i];
        if (!output_key_matches(entry->name, output))
            continue;
        if (strncmp(entry->name, "desc:", 5) != 0) {
            found = entry;
            break;
        }
        found = found ? found : entry;
    }
    if (found)
        sh_tiling_set_output_defaults(server->tiling, output->name, found->tile_layout,
                                      found->master_ratio, found->master_count);
    else
        sh_tiling_set_output_defaults(server->tiling, output->name, -1, 0, 0);
}

/* Gives every connected output its layout.outputs defaults; the caller reflows. */
static void apply_output_layouts(struct sh_server *server) {
    struct wl_list *lists[] = {&server->outputs, &server->disabled_outputs};
    for (size_t i = 0; i < 2; ++i) {
        struct sh_output *output;
        wl_list_for_each(output, lists[i], link) apply_output_layout(server, output->wlr_output);
    }
}

void reconfigure_tiling(struct sh_server *server) {
    configure_layouts(server);
    sh_tiling_clear_output_defaults(server->tiling);
    apply_output_layouts(server);
    struct sh_output *output;
    bool per_workspace = server_settings(server)->tiling_per_workspace;
    if (per_workspace != server->tiling_per_workspace) {
        // Workspaces toggled on their own start or stop counting.
        server->tiling_per_workspace = per_workspace;
        wl_list_for_each(output, &server->outputs, link) apply_tiling(server, output->wlr_output);
    }
    wl_list_for_each(output, &server->outputs, link) {
        int slot = output_slot(server, output->wlr_output->name);
        if (server->output_workspaces[slot].tiling < 0)
            continue;
        bool configured = configured_tiling(server, output->wlr_output);
        if (configured == server->output_workspaces[slot].configured)
            continue;
        server->output_workspaces[slot].configured = configured;
        set_output_tiling(server, output->wlr_output, configured);
    }
}

/* The tiling layout actions, on the focused output's current workspace. */
void layout_action(struct sh_server *server, enum sh_action action) {
    struct wlr_output *output = focused_output(server);
    if (!output || server->locked)
        return;
    const char *name = output->name;
    int workspace = *output_workspace(server, name);
    struct sh_toplevel *current = current_toplevel(server);
    if (current && (!current->tiled || tiled_output(current) != output ||
                    current->workspace != workspace))
        current = NULL;
    bool changed = true;
    // The master keys resize the focused column in the scrolling layout.
    if (sh_tiling_layout(server->tiling, name, workspace) == SH_LAYOUT_SCROLL) {
        if (action == SH_MASTER_GROW)
            action = SH_COLUMN_WIDEN;
        else if (action == SH_MASTER_SHRINK)
            action = SH_COLUMN_NARROW;
    }
    switch (action) {
    case SH_LAYOUT_NEXT:
    case SH_LAYOUT_PREV:
        sh_tiling_cycle_layout(server->tiling, name, workspace, action == SH_LAYOUT_NEXT ? 1 : -1);
        break;
    case SH_SET_LAYOUT_DWINDLE:
    case SH_SET_LAYOUT_MASTER:
    case SH_SET_LAYOUT_SPIRAL:
    case SH_SET_LAYOUT_MONOCLE:
    case SH_SET_LAYOUT_SCROLL:
        sh_tiling_set_layout(server->tiling, name, workspace,
                             (enum sh_tile_layout)(action - SH_SET_LAYOUT_DWINDLE));
        break;
    case SH_PROMOTE: {
        void *master = sh_tiling_master(server->tiling, name, workspace);
        if (current && master == current)
            master = sh_tiling_neighbour(server->tiling, current, 1);
        changed = current && master && sh_tiling_swap(server->tiling, current, master);
        break;
    }
    case SH_SWAP_NEXT:
    case SH_SWAP_PREV: {
        void *other = current ? sh_tiling_neighbour(server->tiling, current,
                                                     action == SH_SWAP_NEXT ? 1 : -1)
                              : NULL;
        changed = other && sh_tiling_swap(server->tiling, current, other);
        break;
    }
    case SH_FOCUS_NEXT:
    case SH_FOCUS_PREV: {
        struct sh_toplevel *other = current ? sh_tiling_neighbour(server->tiling, current,
                                                                  action == SH_FOCUS_NEXT ? 1 : -1)
                                            : NULL;
        if (other) {
            focus_toplevel(other);
            pointer_follow(other);
        }
        return;
    }
    case SH_MASTER_GROW:
    case SH_MASTER_SHRINK:
        changed = sh_tiling_adjust(server->tiling, name, workspace,
                                   action == SH_MASTER_GROW ? 0.05 : -0.05, 0);
        break;
    case SH_MASTER_MORE:
    case SH_MASTER_LESS:
        changed = sh_tiling_adjust(server->tiling, name, workspace, 0,
                                   action == SH_MASTER_MORE ? 1 : -1);
        break;
    case SH_SCROLL_LEFT:
    case SH_SCROLL_RIGHT: {
        struct sh_toplevel *other = current ? sh_tiling_scroll_step(server->tiling, current,
                                                                    action == SH_SCROLL_RIGHT ? 1 : -1, 0)
                                            : NULL;
        if (other) {
            focus_toplevel(other);
            pointer_follow(other);
        }
        return;
    }
    case SH_COLUMN_WIDEN:
    case SH_COLUMN_NARROW:
    case SH_COLUMN_CYCLE_WIDTH:
    case SH_CONSUME_LEFT:
    case SH_CONSUME_RIGHT:
    case SH_EXPEL:
    case SH_CENTER_COLUMN:
        changed = current && sh_tiling_scroll_action(server->tiling, current, action);
        break;
    default:
        return;
    }
    if (!changed)
        return;
    if (current)
        sh_tiling_set_focus(server->tiling, current); // a new layout finds the view to move
    reflow_output(server, output);
    // Monocle stacks the tiles: keep the focused one on top.
    if (current)
        focus_toplevel(current);
}
