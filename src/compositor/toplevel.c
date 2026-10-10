/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* Windows: what xdg-shell and XWayland toplevels share, xdg-shell toplevels and popups,
 * opening new windows by the window rules, and maximize, fullscreen and minimize. */
#include "server.h"

struct sh_popup {
    struct sh_server *server;
    struct wlr_xdg_popup *xdg_popup;
    struct wl_listener commit;
    struct wl_listener destroy;
};

static void set_fullscreen_focus(struct sh_toplevel *toplevel, bool fullscreen, bool focus);
static void toplevel_request_minimize(struct wl_listener *listener, void *data);

/* Window operations shared by xdg-shell and XWayland toplevels. */
struct wlr_surface *toplevel_surface(struct sh_toplevel *toplevel) {
#if WLR_HAS_XWAYLAND
    if (toplevel->xsurface)
        return toplevel->xsurface->surface;
#endif
    return toplevel->xdg_toplevel->base->surface;
}
bool toplevel_mapped(struct sh_toplevel *toplevel) {
    struct wlr_surface *surface = toplevel_surface(toplevel);
    return toplevel->scene_tree && surface && surface->mapped;
}
struct wlr_box toplevel_geometry(struct sh_toplevel *toplevel) {
#if WLR_HAS_XWAYLAND
    if (toplevel->xsurface)
        return (struct wlr_box){0, 0, toplevel->xsurface->width, toplevel->xsurface->height};
#endif
    return toplevel->xdg_toplevel->base->geometry;
}
void toplevel_set_activated(struct sh_toplevel *toplevel, bool activated) {
#if WLR_HAS_XWAYLAND
    if (toplevel->xsurface) {
        wlr_xwayland_surface_activate(toplevel->xsurface, activated);
        if (activated)
            wlr_xwayland_surface_restack(toplevel->xsurface, NULL, XCB_STACK_MODE_ABOVE);
        return;
    }
#endif
    wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, activated);
}
/* Positions the window in layout coordinates; X11 clients also learn the position. */
void toplevel_configure(struct sh_toplevel *toplevel, int x, int y, int width, int height) {
    ++toplevel->server->stats.configures;
    wlr_scene_node_set_position(&toplevel->scene_tree->node, x, y);
#if WLR_HAS_XWAYLAND
    if (toplevel->xsurface) {
        if (width > 0 && height > 0)
            wlr_xwayland_surface_configure(toplevel->xsurface, x, y, width, height);
        follow_output(toplevel);
        return;
    }
#endif
    // Every configure makes the client draw again, so an unchanged size is not sent again.
    const struct wlr_xdg_toplevel_configure *scheduled = &toplevel->xdg_toplevel->scheduled;
    if (scheduled->width != width || scheduled->height != height)
        wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, width, height);
    follow_output(toplevel);
}
void toplevel_configure_box(struct sh_toplevel *toplevel, struct wlr_box box) {
    toplevel_configure(toplevel, box.x, box.y, box.width, box.height);
}
/* The window's position and size, as saved to restore it later. A size the client has not
 * committed yet counts, so placing a window again right after restoring it keeps the restore
 * size rather than the old one (X11 windows take their new size at once). */
struct wlr_box toplevel_box(struct sh_toplevel *toplevel) {
    struct wlr_box geometry = toplevel_geometry(toplevel);
    if (toplevel->xdg_toplevel) {
        struct wlr_xdg_surface *base = toplevel->xdg_toplevel->base;
        const struct wlr_xdg_toplevel_configure *scheduled = &toplevel->xdg_toplevel->scheduled;
        bool pending = base->configure_idle || !wl_list_empty(&base->configure_list);
        if (pending && scheduled->width > 0 && scheduled->height > 0) {
            geometry.width = scheduled->width;
            geometry.height = scheduled->height;
        }
    }
    return (struct wlr_box){toplevel->scene_tree->node.x, toplevel->scene_tree->node.y,
                            geometry.width, geometry.height};
}
void toplevel_set_position(struct sh_toplevel *toplevel, int x, int y) {
    wlr_scene_node_set_position(&toplevel->scene_tree->node, x, y);
#if WLR_HAS_XWAYLAND
    if (toplevel->xsurface && toplevel->xsurface->width > 0 && toplevel->xsurface->height > 0)
        wlr_xwayland_surface_configure(toplevel->xsurface, x, y, toplevel->xsurface->width,
                                       toplevel->xsurface->height);
#endif
    follow_output(toplevel);
}
/* Tells the client and the taskbar whether the window is maximized, and which edges touch
 * a neighbour or the screen edge. */
void toplevel_set_states(struct sh_toplevel *toplevel, bool maximized, uint32_t tiled) {
    if (toplevel->foreign)
        wlr_foreign_toplevel_handle_v1_set_maximized(toplevel->foreign, maximized);
#if WLR_HAS_XWAYLAND
    if (toplevel->xsurface) {
        wlr_xwayland_surface_set_maximized(toplevel->xsurface, maximized, maximized);
        return;
    }
#endif
    const struct wlr_xdg_toplevel_configure *scheduled = &toplevel->xdg_toplevel->scheduled;
    if (scheduled->maximized != maximized)
        wlr_xdg_toplevel_set_maximized(toplevel->xdg_toplevel, maximized);
    if (scheduled->tiled != tiled)
        wlr_xdg_toplevel_set_tiled(toplevel->xdg_toplevel, tiled);
}
static void toplevel_set_fullscreen_state(struct sh_toplevel *toplevel, bool fullscreen) {
#if WLR_HAS_XWAYLAND
    if (toplevel->xsurface) {
        wlr_xwayland_surface_set_fullscreen(toplevel->xsurface, fullscreen);
        return;
    }
#endif
    wlr_xdg_toplevel_set_fullscreen(toplevel->xdg_toplevel, fullscreen);
}
/* Answers a denied client request by repeating the current state. */
void toplevel_refresh(struct sh_toplevel *toplevel) {
#if WLR_HAS_XWAYLAND
    if (toplevel->xsurface) {
        if (toplevel_mapped(toplevel))
            toplevel_set_position(toplevel, toplevel->scene_tree->node.x,
                                  toplevel->scene_tree->node.y);
        return;
    }
#endif
    if (toplevel->xdg_toplevel->base->initialized)
        wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
}
void toplevel_close(struct sh_toplevel *toplevel) {
#if WLR_HAS_XWAYLAND
    if (toplevel->xsurface) {
        wlr_xwayland_surface_close(toplevel->xsurface);
        return;
    }
#endif
    wlr_xdg_toplevel_send_close(toplevel->xdg_toplevel);
}
const char *toplevel_title(struct sh_toplevel *toplevel) {
#if WLR_HAS_XWAYLAND
    if (toplevel->xsurface)
        return toplevel->xsurface->title;
#endif
    return toplevel->xdg_toplevel->title;
}
const char *toplevel_app_id(struct sh_toplevel *toplevel) {
#if WLR_HAS_XWAYLAND
    if (toplevel->xsurface)
        return toplevel->xsurface->class;
#endif
    return toplevel->xdg_toplevel->app_id;
}
bool toplevel_accepts_keyboard(struct sh_toplevel *toplevel) {
#if WLR_HAS_XWAYLAND
    if (toplevel->xsurface)
        return wlr_xwayland_surface_icccm_input_model(toplevel->xsurface) !=
               WLR_ICCCM_INPUT_MODEL_NONE;
#endif
    return true;
}

void maximize_toplevel(struct sh_toplevel *toplevel, bool maximized) {
    if (toplevel->tiled) {
        toplevel_refresh(toplevel); // Tiles ignore client maximize requests, as in Hyprland.
        return;
    }
    if (maximized)
        place_maximized(toplevel);
    else
        restore_toplevel(toplevel);
}

/* The actions windows.rules give a window with its app ID and title now; false when none
 * apply. */
bool window_rule(struct sh_toplevel *toplevel, struct sh_window_rule *rule) {
    const struct sh_callbacks *callbacks = toplevel->server->callbacks;
    const char *app_id = toplevel_app_id(toplevel), *title = toplevel_title(toplevel);
    *rule = (struct sh_window_rule){.floating = -1};
    return callbacks->window_rule &&
           callbacks->window_rule(callbacks->userdata, app_id ? app_id : "", title ? title : "",
                                  rule);
}

/* New windows open on the output under the pointer, as in Hyprland. */
static struct wlr_output *new_window_output(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    struct wlr_output *output = pointer_output(server);
    return output ? output : toplevel_output(toplevel);
}

/* Where a new window on `output` joins the tiling, as in Hyprland: it splits the focused tile
 * when that is on the pointer's output, else the tile under the pointer. Returns the output of
 * the tree it joins and sets `target` to the tile it splits, if any. */
static struct wlr_output *new_tile_split(struct sh_toplevel *toplevel, struct wlr_output *output,
                                         struct sh_toplevel **target) {
    struct sh_toplevel *previous = toplevel->server->focused_toplevel;
    bool split_focused = previous && previous != toplevel && previous->tiled &&
                         toplevel_visible(previous) &&
                         (!output || tiled_output(previous) == output);
    *target = split_focused ? previous : NULL;
    return split_focused ? tiled_output(previous) : output;
}

/* The tile a new xdg-shell window will get when it maps, sent with its first configure so the
 * first buffer already fits: otherwise it is drawn at its own size, shown there, and drawn
 * again at the tile's size. */
static bool initial_tile_size(struct sh_toplevel *toplevel, int *width, int *height) {
    struct sh_server *server = toplevel->server;
    if (toplevel->xdg_toplevel->requested.fullscreen || toplevel_is_dialog(toplevel))
        return false;
    // A rule that places the window elsewhere or keeps it out of the tiling decides at map.
    struct sh_window_rule rule;
    if (window_rule(toplevel, &rule) && (rule.floating == 1 || rule.workspace || rule.output[0] ||
                                         rule.fullscreen || rule.maximize || rule.sticky))
        return false;
    struct sh_toplevel *target;
    struct wlr_output *output = new_tile_split(toplevel, new_window_output(toplevel), &target);
    if (!output_tiles(server, output))
        return false;
    int workspace = *output_workspace(server, output->name), gap;
    struct sh_rect area = tiling_area(server, output, workspace, 1, &gap), rect;
    if (!sh_tiling_preview(server->tiling, output->name, workspace, toplevel, target, true,
                           server->cursor->x, server->cursor->y, area, gap, &rect))
        return false;
    rect = inside_border(server, rect);
    *width = rect.width;
    *height = rect.height;
    return true;
}

/* Takes the windows on the current workspace of `output`, other than `toplevel`, out of
 * fullscreen. */
static void leave_fullscreen_for(struct sh_toplevel *toplevel, struct wlr_output *output) {
    struct sh_toplevel *other, *tmp;
    wl_list_for_each_safe(other, tmp, &toplevel->server->toplevels, link) {
        if (other != toplevel && other->fullscreen && toplevel_visible(other) &&
            toplevel_output(other) == output)
            set_fullscreen(other, false);
    }
}

/* Puts the windows on `toplevel`'s workspace of `output` that float only because they were
 * maximized by hand back into the tiling, so that the new tile does not open over them. */
static void retile_maximized_for(struct sh_toplevel *toplevel, struct wlr_output *output) {
    struct sh_toplevel *other;
    wl_list_for_each(other, &toplevel->server->toplevels, link) {
        if (other == toplevel || !other->placed || !other->arranged ||
            other->arrangement != SH_MAXIMIZE || other->minimized ||
            other->workspace != toplevel->workspace || toplevel_output(other) != output)
            continue;
        other->floating = other->placed = false;
        if (wants_tiling(other, output))
            tile_toplevel(other, output, NULL, false);
    }
}

#define SH_PLACE_OTHERS 32

void map_toplevel(struct sh_toplevel *toplevel, bool fullscreen, bool maximized) {
    struct sh_server *server = toplevel->server;
    int offset = 40 + 32 * (wl_list_length(&toplevel->server->toplevels) % 8);
    int x = offset, y = offset;
    bool resize = false;
    struct wlr_box geometry = toplevel_geometry(toplevel);
    int width = geometry.width, height = geometry.height;
    struct sh_window_rule rule;
    bool ruled = window_rule(toplevel, &rule);
    ruled = session_claim(server, toplevel, &rule, ruled);
    ruled = open_dynamic_rules(toplevel, &rule, ruled);
    struct wlr_output *output = new_window_output(toplevel);
    if (ruled && rule.output[0]) {
        struct wlr_output *named = find_output_key(server, rule.output);
        output = named ? named : output;
    }
    // It opens on that output's current workspace, even if it was mapped there before, unless
    // a rule names another.
    toplevel->output[0] = '\0';
    toplevel->workspace = 0;
    toplevel->sticky = false;
    toplevel->above = ruled && rule.above;
    set_toplevel_output(toplevel, output);
    if (ruled && output && rule.workspace > 0 &&
        rule.workspace <= server_settings(server)->workspaces)
        toplevel->workspace = rule.workspace - 1;
    if (output) {
        struct sh_rect area = usable_area(server, output);
        // Keep newly opened applications reachable inside a small nested output.
        int margin = area.width < 80 || area.height < 80 ? 0 : 40;
        if (width > area.width - 2 * margin) {
            width = area.width - 2 * margin;
            resize = true;
        }
        if (height > area.height - 2 * margin) {
            height = area.height - 2 * margin;
            resize = true;
        }
        x = area.x + (offset + width <= area.width ? offset : margin);
        y = area.y + (offset + height <= area.height ? offset : margin);
        // Rules size and place it within the usable area; a tile keeps this as its floating
        // geometry.
        if (ruled && rule.width > 0 && rule.height > 0) {
            width = rule.width < area.width ? rule.width : area.width;
            height = rule.height < area.height ? rule.height : area.height;
            resize = true;
        }
        // Where the windows already there leave room, unless a rule names the place.
        if (!(ruled && rule.position != SH_RULE_POSITION_UNSET) && width > 0 && height > 0) {
            struct sh_rect others[SH_PLACE_OTHERS], place;
            int count = 0, cascade = 0;
            struct sh_toplevel *other;
            wl_list_for_each(other, &server->toplevels, link) {
                ++cascade;
                if (count < SH_PLACE_OTHERS && toplevel_mapped(other) && toplevel_visible(other) &&
                    !other->fullscreen && toplevel_output(other) == output &&
                    other->workspace == toplevel->workspace) {
                    struct wlr_box box = toplevel_box(other);
                    others[count++] = (struct sh_rect){box.x, box.y, box.width, box.height};
                }
            }
            if (sh_place_window(server_settings(server)->placement, (struct sh_rect){area.x, area.y,
                                area.width, area.height}, others, count, width, height, cascade,
                                &place)) {
                x = place.x;
                y = place.y;
            }
        }
        // The first window after a button binding spawned a program opens centered on the
        // click, kept inside the area.
        bool clicked = server->spawn_until && now_ms() < server->spawn_until &&
                       server->spawn_x >= area.x && server->spawn_x < area.x + area.width &&
                       server->spawn_y >= area.y && server->spawn_y < area.y + area.height;
        server->spawn_until = 0;
        if (clicked && !(ruled && rule.position != SH_RULE_POSITION_UNSET)) {
            x = (int)server->spawn_x - width / 2;
            y = (int)server->spawn_y - height / 2;
            x = x + width > area.x + area.width ? area.x + area.width - width : x;
            y = y + height > area.y + area.height ? area.y + area.height - height : y;
            x = x < area.x ? area.x : x;
            y = y < area.y ? area.y : y;
        }
        if (ruled && rule.position == SH_RULE_POSITION_CENTER) {
            x = area.x + (area.width - width) / 2;
            y = area.y + (area.height - height) / 2;
        } else if (ruled && rule.position == SH_RULE_POSITION_AT) {
            x = area.x + rule.x;
            y = area.y + rule.y;
        }
    }
    if (resize && width > 0 && height > 0)
        toplevel_configure(toplevel, x, y, width, height);
    else
        toplevel_set_position(toplevel, x, y);
    wl_list_insert(&toplevel->server->toplevels, &toplevel->link);

    publish_toplevel(toplevel);
    toplevel->floating = toplevel_is_dialog(toplevel);
    if (ruled && rule.floating >= 0)
        toplevel->floating = rule.floating;
    // Sticky floats it on the current workspace of its output, whatever `workspace` says.
    if (ruled && rule.sticky && server_settings(server)->sticky)
        set_sticky(toplevel, true, false);
    // A window started from a terminal takes its place, unless a rule puts it elsewhere.
    struct sh_toplevel *terminal = NULL;
    if (swallow_wanted(toplevel) &&
        !(ruled && (rule.floating == 1 || rule.workspace || rule.output[0] || rule.sticky))) {
        terminal = swallow_host(toplevel, true);
        if (terminal) {
            set_toplevel_output(toplevel, find_output(server, terminal->output));
            toplevel->workspace = terminal->workspace;
        }
    }
    // A window a rule sends to another workspace, or opens without focus, stays out of the way.
    bool visible = toplevel_visible(toplevel);
    bool focus = visible && !(ruled && rule.no_focus);
    fullscreen = fullscreen || (ruled && rule.fullscreen);
    // A new tile would open under a fullscreen one on its workspace, so that one leaves
    // fullscreen first, as in Hyprland. A floating window opens in front of it instead, as
    // focusing it lowers the fullscreen one, which stays fullscreen behind it.
    if (output && !fullscreen && focus && wants_tiling(toplevel, output))
        leave_fullscreen_for(toplevel, output);
    struct sh_toplevel *target = NULL;
    struct wlr_output *tile_output = visible ? new_tile_split(toplevel, output, &target) : output;
    // Opening while a group has focus adds a tab to it, taking the group's slot.
    struct sh_toplevel *host = server->focused_toplevel;
    bool joins = visible && !terminal && host && host != toplevel && host->group && groupable(host) &&
                 groups_enabled(server) && server_settings(server)->group_join_new &&
                 !fullscreen && !toplevel_is_dialog(toplevel) && !toplevel->sticky &&
                 !(ruled && (rule.floating == 1 || rule.workspace || rule.output[0])) &&
                 host->workspace == toplevel->workspace && !strcmp(host->output, toplevel->output);
    if (terminal) {
        swallow_attach(terminal, toplevel);
    } else if (joins) {
        toplevel->group_hidden = true;
        group_join(toplevel, host->group);
        toplevel->floating = host->floating;
        group_show(toplevel);
    } else if (wants_tiling(toplevel, tile_output)) {
        retile_maximized_for(toplevel, tile_output);
        tile_toplevel(toplevel, tile_output, target, visible);
    } else if (toplevel->tile_sized) {
        toplevel->tile_sized = false; // It floats after all: let the client choose its size.
        toplevel_set_states(toplevel, false, 0);
        toplevel_configure(toplevel, x, y, resize ? width : 0, resize ? height : 0);
    }
    restack_toplevel(toplevel); // kept above by a rule, or floating over tiles
    if (focus)
        focus_toplevel(toplevel);
    else
        wlr_scene_node_set_enabled(&toplevel->scene_tree->node, visible);
    if (fullscreen)
        set_fullscreen_focus(toplevel, true, visible);
    else if (ruled && rule.maximize)
        place_by_hand(toplevel, SH_MAXIMIZE);
    else if (maximized)
        maximize_toplevel(toplevel, true);
#if WLR_HAS_XWAYLAND
    // An X11 client may have asked for attention before it mapped; if it opens without focus,
    // that request stands (the policy is applied as if it had come after).
    if (!focus && toplevel->xsurface) {
        const xcb_icccm_wm_hints_t *hints = toplevel->xsurface->hints;
        toplevel->x_hint_urgent = hints && (hints->flags & XCB_ICCCM_WM_HINT_X_URGENCY);
        if (toplevel->x_hint_urgent || toplevel->xsurface->demands_attention)
            activation_requested(toplevel);
    }
#endif
    // The first frame is already committed and shows at once, only faded and a little small.
    toplevel->shown = true;
    struct wlr_box box = toplevel_geometry(toplevel);
    sh_anim_open(server->animator, &toplevel->anim, toplevel->content, box.width / 2.0,
                 box.height / 2.0);
    notify_subscribers(server); // its workspace holds a window now
}

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, map);
    toplevel->fullscreen_cover = toplevel->xdg_toplevel->requested.fullscreen;
    map_toplevel(toplevel, toplevel->xdg_toplevel->requested.fullscreen,
                 toplevel->xdg_toplevel->requested.maximized);
}

void unmap_toplevel(struct sh_toplevel *toplevel) {
    // Back in its place first, should it be peeked at, so that it closes there.
    forget_window_peek(toplevel);
    // The client's buffers go with this commit; the closing animation draws a copy of them.
    struct sh_server *server = toplevel->server;
    if (toplevel->shown && server->running && toplevel_visible(toplevel)) {
        struct wlr_box box = toplevel_geometry(toplevel);
        sh_anim_close(server->animator, &toplevel->scene_tree->node, toplevel->content,
                      box.width / 2.0, box.height / 2.0);
    }
    sh_anim_finish(&toplevel->anim);
    toplevel->shown = false;
    swallow_end(toplevel);
    if (toplevel == toplevel->server->grabbed_toplevel) {
        reset_cursor_mode(toplevel->server);
    }
    forget_decoration(toplevel);
    group_detach(toplevel);

    switcher_forget(toplevel);
    overview_forget(toplevel);
    toplevel->fullscreen = toplevel->fullscreen_cover = false;
    toplevel->scratchpad = false;
    toplevel->urgent = false;
    refresh_frame(toplevel);
    untile_toplevel(toplevel, false);
    bool was_focused = toplevel->server->focused_toplevel == toplevel;
    if (was_focused)
        deactivate_toplevel(toplevel->server);
    unpublish_toplevel(toplevel);
    wl_list_remove(&toplevel->link);
    if (was_focused)
        focus_previous(toplevel->server);
    notify_subscribers(toplevel->server);
    power_window_closed(toplevel->server);
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);
    unmap_toplevel(toplevel);
}

static void xdg_toplevel_commit(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, commit);

    if (toplevel->xdg_toplevel->base->initial_commit) {
        int width = 0, height = 0;
        toplevel->tile_sized = initial_tile_size(toplevel, &width, &height);
        if (toplevel->tile_sized)
            toplevel_set_states(toplevel, false, ALL_EDGES);
        wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, width, height);
        if (toplevel->decoration)
            wlr_xdg_toplevel_decoration_v1_set_mode(toplevel->decoration,
                                                    decoration_mode(toplevel->decoration));
    } else if (toplevel->xdg_toplevel->base->surface->mapped) {
        uint64_t started = now_ns();
        refresh_frame(toplevel);
        ++toplevel->server->stats.commits;
        toplevel->server->stats.commit_ns += now_ns() - started;
    }
}

/* Frees a window after removing the listeners xdg-shell and X11 windows have in common. */
void free_toplevel(struct sh_toplevel *toplevel) {
    sh_anim_finish(&toplevel->anim);
    sh_tween_stop(&toplevel->fade);
    free(toplevel->opacity_rule.app_id);
    free(toplevel->opacity_rule.title);
    free_icon(&toplevel->icon);
    wl_list_remove(&toplevel->destroy.link);
    wl_list_remove(&toplevel->request_move.link);
    wl_list_remove(&toplevel->request_resize.link);
    wl_list_remove(&toplevel->request_maximize.link);
    wl_list_remove(&toplevel->request_fullscreen.link);
    wl_list_remove(&toplevel->request_minimize.link);
    wl_list_remove(&toplevel->title_changed.link);
    wl_list_remove(&toplevel->app_id_changed.link);
    free(toplevel);
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, destroy);
    overview_forget(toplevel);
    // The decoration outlives this listener: it hears the same signal later.
    if (toplevel->decoration) {
        wl_list_remove(&toplevel->decoration_mode.link);
        wl_list_remove(&toplevel->decoration_destroy.link);
    }
    wl_list_remove(&toplevel->map.link);
    wl_list_remove(&toplevel->unmap.link);
    wl_list_remove(&toplevel->commit.link);
    sh_anim_finish(&toplevel->anim);
    wlr_scene_node_destroy(&toplevel->scene_tree->node);
    free_toplevel(toplevel);
}

/* Client decorations often live in subsurfaces (kitty's title bar), so the clicked surface
 * only has to belong to the toplevel, not be its root surface. */
static bool validate_grab_serial(struct sh_toplevel *toplevel, uint32_t serial) {
    struct wlr_seat *seat = toplevel->server->seat;
    struct wlr_surface *focused = seat->pointer_state.focused_surface;
    return wlr_seat_validate_pointer_grab_serial(seat, NULL, serial) && focused &&
           wlr_surface_get_root_surface(focused) == toplevel->xdg_toplevel->base->surface;
}

static void xdg_toplevel_request_move(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, request_move);
    struct wlr_xdg_toplevel_move_event *event = data;
    if (validate_grab_serial(toplevel, event->serial))
        begin_interactive(toplevel, SH_CURSOR_MOVE, 0);
}

static void xdg_toplevel_request_resize(struct wl_listener *listener, void *data) {
    struct wlr_xdg_toplevel_resize_event *event = data;
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, request_resize);
    if (validate_grab_serial(toplevel, event->serial))
        begin_interactive(toplevel, SH_CURSOR_RESIZE, event->edges);
}

static void xdg_toplevel_request_maximize(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, request_maximize);
    if (!toplevel->xdg_toplevel->base->initialized)
        return;
    if (!toplevel->fullscreen)
        maximize_toplevel(toplevel, toplevel->xdg_toplevel->requested.maximized);
    wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
}

void fit_fullscreen(struct sh_toplevel *toplevel) {
    struct wlr_output *output = toplevel_output(toplevel);
    if (!output)
        return;
    toplevel_configure_box(toplevel, fullscreen_box(toplevel, output));
}

void refit_fullscreen(struct sh_server *server) {
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel->fullscreen)
            fit_fullscreen(toplevel);
    }
}

void set_fullscreen(struct sh_toplevel *toplevel, bool fullscreen) {
    set_fullscreen_focus(toplevel, fullscreen, true);
}
/* As set_fullscreen; entering fullscreen focuses the window only with `focus`. */
static void set_fullscreen_focus(struct sh_toplevel *toplevel, bool fullscreen, bool focus) {
    struct sh_server *server = toplevel->server;
    if (!toplevel_mapped(toplevel) || toplevel->fullscreen == fullscreen) {
        toplevel_refresh(toplevel);
        return;
    }
    if (server->grabbed_toplevel == toplevel)
        reset_cursor_mode(server);
    struct wlr_scene_node *node = &toplevel->scene_tree->node;
    int from_x = node->x, from_y = node->y;
    if (fullscreen)
        toplevel->fullscreen_restore = toplevel_box(toplevel);
    toplevel->fullscreen = fullscreen;
    if (!fullscreen)
        toplevel->fullscreen_cover = false;
    toplevel_set_fullscreen_state(toplevel, fullscreen);
    if (toplevel->foreign)
        wlr_foreign_toplevel_handle_v1_set_fullscreen(toplevel->foreign, fullscreen);
    if (fullscreen) {
        fit_fullscreen(toplevel);
    } else {
        toplevel->fullscreen_restore =
            rebase_box(server, toplevel->fullscreen_restore, toplevel_output(toplevel));
        toplevel_configure_box(toplevel, toplevel->fullscreen_restore);
        wlr_scene_node_reparent(&toplevel->scene_tree->node, window_layer(toplevel));
        // The usable area may have changed while this window covered the output.
        struct wlr_output *output =
            toplevel->tiled ? tiled_output(toplevel) : toplevel_output(toplevel);
        if ((toplevel->arranged || toplevel->tiled) && output)
            reflow_output(server, output);
    }
    // The window glides from where it was drawn; the size follows when the client draws it.
    if (toplevel->shown && toplevel_visible(toplevel))
        sh_anim_glide_kind(server->animator, &toplevel->anim, toplevel->content,
                           from_x - node->x, from_y - node->y, SH_ANIM_FULLSCREEN);
    if (server->focused_toplevel == toplevel || (fullscreen && focus))
        focus_toplevel(toplevel);
    refresh_decoration(toplevel);
    refresh_frame(toplevel);
}

/* Fullscreen the client asks for itself, like a video player's: it covers the panels too. */
void set_client_fullscreen(struct sh_toplevel *toplevel, bool fullscreen) {
    if (fullscreen && !toplevel->fullscreen)
        toplevel->fullscreen_cover = true;
    set_fullscreen(toplevel, fullscreen);
}

static void xdg_toplevel_request_fullscreen(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, request_fullscreen);
    set_client_fullscreen(toplevel, toplevel->xdg_toplevel->requested.fullscreen);
}

void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, new_xdg_toplevel);
    struct wlr_xdg_toplevel *xdg_toplevel = data;

    struct sh_toplevel *toplevel = calloc(1, sizeof(*toplevel));
    toplevel->server = server;
    toplevel->xdg_toplevel = xdg_toplevel;

    // Listen before the scene does, so the surfaces are still shown when the window unmaps
    // and the closing animation can copy them.
    struct wlr_surface *surface = xdg_toplevel->base->surface;
    add_listener(&surface->events.map, &toplevel->map, xdg_toplevel_map);
    add_listener(&surface->events.unmap, &toplevel->unmap, xdg_toplevel_unmap);
    add_listener(&surface->events.commit, &toplevel->commit, xdg_toplevel_commit);
    toplevel->scene_tree = wlr_scene_tree_create(toplevel->server->windows);
    toplevel->content = wlr_scene_tree_create(toplevel->scene_tree);
    wlr_scene_xdg_surface_create(toplevel->content, xdg_toplevel->base);
    toplevel->node = (struct sh_node){SH_NODE_TOPLEVEL, toplevel};
    toplevel->scene_tree->node.data = &toplevel->node;
    toplevel->content->node.data = &toplevel->node;
    xdg_toplevel->base->data = toplevel->scene_tree;
    add_listener(&xdg_toplevel->events.destroy, &toplevel->destroy, xdg_toplevel_destroy);
    add_listener(&xdg_toplevel->events.set_title, &toplevel->title_changed, toplevel_title_changed);
    add_listener(&xdg_toplevel->events.set_app_id, &toplevel->app_id_changed,
                 toplevel_app_id_changed);
    add_listener(&xdg_toplevel->events.request_move, &toplevel->request_move,
                 xdg_toplevel_request_move);
    add_listener(&xdg_toplevel->events.request_resize, &toplevel->request_resize,
                 xdg_toplevel_request_resize);
    add_listener(&xdg_toplevel->events.request_maximize, &toplevel->request_maximize,
                 xdg_toplevel_request_maximize);
    add_listener(&xdg_toplevel->events.request_fullscreen, &toplevel->request_fullscreen,
                 xdg_toplevel_request_fullscreen);
    add_listener(&xdg_toplevel->events.request_minimize, &toplevel->request_minimize,
                 toplevel_request_minimize);
}

static void xdg_popup_commit(struct wl_listener *listener, void *data) {
    struct sh_popup *popup = wl_container_of(listener, popup, commit);

    if (popup->xdg_popup->base->initial_commit) {
        // Keep menus on the output of their window or panel; positioners say how to flip or slide.
        struct wlr_scene_tree *root = popup->xdg_popup->base->data;
        while (root && !root->node.data)
            root = root->node.parent;
        struct sh_server *server = popup->server;
        int root_x = 0, root_y = 0;
        if (root)
            wlr_scene_node_coords(&root->node, &root_x, &root_y);
        else
            root_x = server->cursor->x, root_y = server->cursor->y;
        struct wlr_output *output =
            wlr_output_layout_output_at(server->output_layout, root_x, root_y);
        if (!output)
            output = pointer_output(server);
        if (root && output) {
            struct wlr_box box;
            wlr_output_layout_get_box(server->output_layout, output, &box);
            box.x -= root_x;
            box.y -= root_y;
            wlr_xdg_popup_unconstrain_from_box(popup->xdg_popup, &box);
        }
        wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
    }
}

static void xdg_popup_destroy(struct wl_listener *listener, void *data) {
    struct sh_popup *popup = wl_container_of(listener, popup, destroy);

    wl_list_remove(&popup->commit.link);
    wl_list_remove(&popup->destroy.link);

    free(popup);
}

void create_popup(struct sh_server *server, struct wlr_xdg_popup *xdg_popup,
                  struct wlr_scene_tree *parent_tree) {
    struct sh_popup *popup = calloc(1, sizeof(*popup));
    popup->server = server;
    popup->xdg_popup = xdg_popup;
    xdg_popup->base->data = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);
    add_listener(&xdg_popup->base->surface->events.commit, &popup->commit, xdg_popup_commit);
    add_listener(&xdg_popup->events.destroy, &popup->destroy, xdg_popup_destroy);
}

void server_new_xdg_popup(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, new_xdg_popup);
    struct wlr_xdg_popup *popup = data;
    // A layer-shell popup is attached by the layer's new_popup handler instead.
    if (!popup->parent)
        return;
    struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(popup->parent);
    if (parent && parent->data)
        create_popup(server, popup, parent->data);
}

void minimize_toplevel(struct sh_toplevel *toplevel) {
    group_detach(toplevel); // a minimized window keeps no slot to share
    toplevel->minimized = true;
    untile_toplevel(toplevel, false);
    wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
    if (toplevel->foreign)
        wlr_foreign_toplevel_handle_v1_set_minimized(toplevel->foreign, true);
    if (toplevel->server->focused_toplevel == toplevel)
        focus_previous(toplevel->server);
}

/* Fullscreen the client asked for covers the whole output; fullscreen from a binding or the
 * title bar leaves the panels' exclusive zones shown. */
struct wlr_box fullscreen_box(struct sh_toplevel *toplevel, struct wlr_output *output) {
    struct wlr_box box;
    if (toplevel->fullscreen_cover) {
        wlr_output_layout_get_box(toplevel->server->output_layout, output, &box);
        return box;
    }
    struct sh_rect area = usable_area(toplevel->server, output);
    return (struct wlr_box){area.x, area.y, area.width, area.height};
}

struct wlr_scene_tree *fullscreen_tree(struct sh_toplevel *toplevel) {
    return toplevel->fullscreen_cover ? toplevel->server->fullscreen_cover
                                      : toplevel->server->fullscreen;
}

static void toplevel_request_minimize(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, request_minimize);
    if (toplevel_mapped(toplevel))
        minimize_toplevel(toplevel);
}

pid_t toplevel_pid(struct sh_toplevel *toplevel) {
#if WLR_HAS_XWAYLAND
    if (toplevel->xsurface)
        return toplevel->xsurface->pid;
#endif
    pid_t pid = 0;
    wl_client_get_credentials(wl_resource_get_client(toplevel->xdg_toplevel->resource), &pid,
                              NULL, NULL);
    return pid;
}

/* Dialogs and fixed-size windows float, as in Hyprland. */
bool toplevel_is_dialog(struct sh_toplevel *toplevel) {
#if WLR_HAS_XWAYLAND
    if (toplevel->xsurface) {
        const struct wlr_xwayland_surface *xsurface = toplevel->xsurface;
        const xcb_size_hints_t *hints = xsurface->size_hints;
        enum wlr_xwayland_net_wm_window_type types[] = {WLR_XWAYLAND_NET_WM_WINDOW_TYPE_DIALOG,
                                                        WLR_XWAYLAND_NET_WM_WINDOW_TYPE_UTILITY,
                                                        WLR_XWAYLAND_NET_WM_WINDOW_TYPE_SPLASH};
        for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); ++i)
            if (wlr_xwayland_surface_has_window_type(xsurface, types[i]))
                return true;
        // Like xdg-shell below: a window of one fixed size cannot fill a tile.
        uint32_t fixed = XCB_ICCCM_SIZE_HINT_P_MIN_SIZE | XCB_ICCCM_SIZE_HINT_P_MAX_SIZE;
        return xsurface->parent || xsurface->modal ||
               (hints && (hints->flags & fixed) == fixed && hints->min_width > 0 &&
                hints->min_width == hints->max_width && hints->min_height > 0 &&
                hints->min_height == hints->max_height);
    }
#endif
    const struct wlr_xdg_toplevel_state *state = &toplevel->xdg_toplevel->current;
    return toplevel->xdg_toplevel->parent ||
           (state->min_width > 0 && state->min_width == state->max_width && state->min_height > 0 &&
            state->min_height == state->max_height);
}
