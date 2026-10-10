/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* Window groups (features.groups): windows that share one slot, shown one at a time and
 * switched with a tab strip. */
#include "server.h"

/* Window groups. The member showing holds the group's slot (its tile, or its floating place);
 * the others are group_hidden, in no tiling and on no screen, until a tab brings one forward. */
bool groups_enabled(struct sh_server *server) {
    return server_settings(server)->groups;
}

int group_size(struct sh_server *server, unsigned group) {
    int count = 0;
    struct sh_toplevel *member;
    if (group)
        wl_list_for_each(member, &server->toplevels, link) count += member->group == group;
    return count;
}

/* The member of `group` that is showing. */
static struct sh_toplevel *group_shown(struct sh_server *server, unsigned group) {
    struct sh_toplevel *member;
    if (group)
        wl_list_for_each(member, &server->toplevels, link) {
            if (member->group == group && !member->group_hidden)
                return member;
        }
    return NULL;
}

/* The tab `from` is, counting from 0. */
int group_index(struct sh_toplevel *from) {
    int index = 0;
    struct sh_toplevel *member;
    wl_list_for_each(member, &from->server->toplevels, link) {
        index += member->group == from->group && member->group_order < from->group_order;
    }
    return index;
}

/* The member `step` (1 or -1) tabs away from `from`, wrapping; NULL when it is alone. */
static struct sh_toplevel *group_step(struct sh_toplevel *from, int step) {
    struct sh_toplevel *member, *best = NULL, *wrap = NULL;
    wl_list_for_each(member, &from->server->toplevels, link) {
        if (member->group != from->group || member == from)
            continue;
        bool beyond = step > 0 ? member->group_order > from->group_order
                               : member->group_order < from->group_order;
        if (beyond && (!best || (step > 0 ? member->group_order < best->group_order
                                          : member->group_order > best->group_order)))
            best = member;
        if (!wrap || (step > 0 ? member->group_order < wrap->group_order
                               : member->group_order > wrap->group_order))
            wrap = member;
    }
    return best ? best : wrap;
}

/* Hidden members follow the shown one to another workspace or output. */
void group_follow(struct sh_toplevel *toplevel) {
    if (!toplevel->group || toplevel->group_hidden)
        return;
    struct sh_toplevel *member;
    wl_list_for_each(member, &toplevel->server->toplevels, link) {
        if (member->group != toplevel->group || member == toplevel)
            continue;
        member->workspace = toplevel->workspace;
        snprintf(member->output, sizeof(member->output), "%s", toplevel->output);
    }
}

/* `to` takes the slot of `from` (its tile, or its floating place and state). The caller has set
 * which of the two is hidden. */
void hand_over_slot(struct sh_toplevel *from, struct sh_toplevel *to) {
    struct sh_server *server = from->server;
    struct wlr_output *output = from->tiled ? tiled_output(from) : NULL;
    to->workspace = from->workspace;
    snprintf(to->output, sizeof(to->output), "%s", from->output);
    to->floating = from->floating;
    to->placed = from->placed;
    to->above = from->above;
    to->restore_box = from->restore_box;
    to->tile_sized = false;
    wlr_scene_node_set_position(&to->scene_tree->node, from->scene_tree->node.x,
                                from->scene_tree->node.y);
    if (from->tiled) {
        sh_tiling_replace(server->tiling, from, to);
        from->tiled = false;
        from->arranged = false;
        from->arrangement = SH_NONE;
        to->tiled = true;
        to->arranged = false;
        to->arrangement = SH_NONE;
        if (to->foreign)
            wlr_foreign_toplevel_handle_v1_set_maximized(to->foreign, false);
    } else {
        struct wlr_box box = toplevel_box(from);
        to->arranged = from->arranged;
        to->arrangement = from->arrangement;
        toplevel_set_states(to, from->arrangement == SH_MAXIMIZE, 0);
        toplevel_configure_box(to, box);
    }
    wlr_scene_node_set_enabled(&from->scene_tree->node, toplevel_visible(from));
    wlr_scene_node_set_enabled(&to->scene_tree->node, toplevel_visible(to));
    restack_toplevel(from);
    restack_toplevel(to);
    if (output)
        reflow_output(server, output);
    refresh_frame(from);
    refresh_frame(to);
}

/* `to` (a hidden member) takes the slot of `from` (the member showing), which hides. */
static void group_take_slot(struct sh_toplevel *from, struct sh_toplevel *to) {
    from->group_hidden = true;
    to->group_hidden = false;
    hand_over_slot(from, to);
    group_follow(to);
    notify_subscribers(from->server);
}

/* Brings a hidden member forward; the caller focuses it. */
void group_show(struct sh_toplevel *toplevel) {
    struct sh_toplevel *shown = group_shown(toplevel->server, toplevel->group);
    if (!toplevel->group_hidden || !shown || shown == toplevel)
        return;
    if (shown->fullscreen)
        set_fullscreen(shown, false); // it could not hide, and would return fullscreen
    group_take_slot(shown, toplevel);
    // The tabs of the others follow the new count and highlight.
    struct sh_toplevel *member;
    wl_list_for_each(member, &toplevel->server->toplevels, link) {
        if (member->group == toplevel->group)
            refresh_tabs(member);
    }
}

/* Takes a window out of its group. A member showing hands its slot to the next tab; a group
 * left with one window dissolves. The window keeps no slot of its own (it is on its way out,
 * or the caller places it). */
void group_detach(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    unsigned group = toplevel->group;
    if (!group)
        return;
    struct sh_toplevel *heir = toplevel->group_hidden ? NULL : group_step(toplevel, 1);
    bool had_focus = server->focused_toplevel == toplevel;
    if (heir)
        group_take_slot(toplevel, heir);
    toplevel->group = 0;
    toplevel->group_hidden = false;
    if (toplevel->tabs) {
        wlr_scene_node_destroy(&toplevel->tabs->node);
        toplevel->tabs = NULL;
    }
    if (server->tabs_hovered == toplevel)
        server->tabs_hovered = NULL;
    struct sh_toplevel *member, *last = NULL;
    int left = 0;
    wl_list_for_each(member, &server->toplevels, link) {
        if (member->group == group)
            ++left, last = member;
    }
    if (heir && had_focus)
        focus_toplevel(heir);
    if (left == 1 && last) {
        last->group = 0;
        refresh_tabs(last);
    } else if (left > 1) {
        wl_list_for_each(member, &server->toplevels, link) {
            if (member->group == group)
                refresh_tabs(member);
        }
    }
    notify_subscribers(server);
}

/* Whether a window can be part of a group: an ordinary window on a workspace. */
bool groupable(struct sh_toplevel *toplevel) {
    return toplevel && toplevel_mapped(toplevel) && !toplevel->sticky && !toplevel->scratchpad &&
           !toplevel->fullscreen && !toplevel->minimized && !toplevel->group_hidden &&
           !toplevel->unmanaged;
}

/* Makes `toplevel` a member of `group`, last in tab order. */
void group_join(struct sh_toplevel *toplevel, unsigned group) {
    toplevel->group = group;
    toplevel->group_order = ++toplevel->server->group_serial;
}

/* The focused window becomes a group of one, so windows opening next join it, or, in a group,
 * the whole group dissolves: hidden members return to tiles beside the shown one. */
void group_toggle(struct sh_server *server, struct sh_toplevel *current) {
    if (!current)
        return;
    if (!current->group) {
        if (!groupable(current))
            return;
        group_join(current, ++server->group_serial);
        refresh_frame(current);
        notify_subscribers(server);
        return;
    }
    unsigned group = current->group;
    struct sh_toplevel *member, *tmp;
    struct sh_toplevel *shown = group_shown(server, group);
    wl_list_for_each_safe(member, tmp, &server->toplevels, link) {
        if (member->group != group)
            continue;
        bool hidden = member->group_hidden;
        member->group = 0;
        member->group_hidden = false;
        if (hidden && shown) {
            // It comes back beside the shown window: a tile splitting its slot, or floating there.
            wlr_scene_node_set_enabled(&member->scene_tree->node, toplevel_visible(member));
            if (wants_tiling(member, NULL) && shown->tiled)
                tile_toplevel_at(member, tiled_output(shown), shown, false, 0, 0);
            else if (!member->tiled) {
                struct wlr_box box = toplevel_box(shown);
                box.x += 32, box.y += 32;
                toplevel_configure_box(member, box);
            }
        }
        refresh_frame(member);
    }
    notify_subscribers(server);
}

void group_cycle(struct sh_server *server, struct sh_toplevel *current, int step) {
    if (!current || !current->group)
        return;
    struct sh_toplevel *next = group_step(current, step);
    if (!next)
        return;
    focus_toplevel(next); // a hidden member takes the slot as it gets focus
}

/* Takes the focused window out of its group into a slot of its own beside it. */
void ungroup(struct sh_server *server, struct sh_toplevel *current) {
    if (!current || !current->group)
        return;
    struct sh_toplevel *heir = current->group_hidden ? NULL : group_step(current, 1);
    struct wlr_output *output = current->tiled ? tiled_output(current) : NULL;
    struct wlr_box box = toplevel_box(current);
    bool floating = !current->tiled;
    group_detach(current);
    if (!heir || current->group_hidden)
        return;
    // The heir holds the slot now; the window gets a place beside it.
    wlr_scene_node_set_enabled(&current->scene_tree->node, toplevel_visible(current));
    if (output && wants_tiling(current, output)) {
        tile_toplevel_at(current, output, heir, false, 0, 0);
    } else if (floating) {
        box.x += 32, box.y += 32;
        toplevel_configure_box(current, box);
    }
    refresh_frame(current);
    focus_toplevel(current);
}

/* Moves the focused window into the group of the window beside it, that way: it becomes the
 * shown tab in that window's slot. */
void group_merge(struct sh_server *server, enum sh_action action) {
    struct sh_toplevel *current = server->focused_toplevel;
    if (server->locked || !groupable(current))
        return;
    int index = action - SH_GROUP_MERGE_LEFT;
    bool horizontal = index < 2;
    int sign = index % 2 ? 1 : -1;
    struct sh_toplevel *target = toplevel_toward(current, horizontal, sign, false);
    if (!target || target == current || !groupable(target) || (target->group && target->group == current->group))
        return;
    if (!target->group)
        group_join(target, ++server->group_serial);
    unsigned group = target->group;
    // Leave the old group (or slot), then take the target's slot as its shown tab.
    group_detach(current);
    if (current->tiled)
        untile_toplevel(current, false);
    group_join(current, group);
    current->group_hidden = true; // hidden until it takes the slot, so the swap is uniform
    wlr_scene_node_set_enabled(&current->scene_tree->node, false);
    struct sh_toplevel *member;
    group_show(current);
    focus_toplevel(current);
    wl_list_for_each(member, &server->toplevels, link) {
        if (member->group == group)
            refresh_tabs(member);
    }
}

/* With features.groups off every group dissolves. */
void dissolve_groups(struct sh_server *server) {
    struct sh_toplevel *toplevel, *tmp;
    wl_list_for_each_safe(toplevel, tmp, &server->toplevels, link) {
        if (toplevel->group && !toplevel->group_hidden)
            group_toggle(server, toplevel);
    }
    wl_list_for_each_safe(toplevel, tmp, &server->toplevels, link) {
        if (toplevel->group)
            group_toggle(server, toplevel);
    }
}
