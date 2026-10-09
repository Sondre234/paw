/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* Snapping a window dragged to an edge of its output (windows.snap), as Windows' Aero Snap and
 * KWin's quick tiling: the zone the pointer is in, the preview of where the window would go,
 * and the drop that puts it there. Also Windows' Win+arrow cycle from the keyboard, and when
 * Snap Assist offers the windows to fill the rest with. */
#include "server.h"
#if WLR_HAS_GLES2_RENDERER
#include <wlr/render/gles2.h>
#endif

const char *snap_zone_name(enum sh_action zone) {
    switch (zone) {
    case SH_SNAP_LEFT:
        return "left";
    case SH_SNAP_RIGHT:
        return "right";
    case SH_MAXIMIZE:
        return "maximize";
    case SH_SNAP_TOP_LEFT:
        return "top_left";
    case SH_SNAP_TOP_RIGHT:
        return "top_right";
    case SH_SNAP_BOTTOM_LEFT:
        return "bottom_left";
    case SH_SNAP_BOTTOM_RIGHT:
        return "bottom_right";
    default:
        return "none";
    }
}

/* The edges of `output` that lead on to another output where the pointer is along them. */
static uint32_t shared_edges(struct sh_server *server, struct wlr_output *output, double x,
                             double y) {
    struct wlr_output_layout *layout = server->output_layout;
    struct wlr_box box;
    wlr_output_layout_get_box(layout, output, &box);
    uint32_t shared = 0;
    if (wlr_output_layout_output_at(layout, box.x - 1, y))
        shared |= SH_EDGE_LEFT;
    if (wlr_output_layout_output_at(layout, box.x + box.width, y))
        shared |= SH_EDGE_RIGHT;
    if (wlr_output_layout_output_at(layout, x, box.y - 1))
        shared |= SH_EDGE_TOP;
    if (wlr_output_layout_output_at(layout, x, box.y + box.height))
        shared |= SH_EDGE_BOTTOM;
    return shared;
}

/* Where dropping the grabbed window now would snap it, and on which output; SH_NONE where it
 * would not. The pointer decides, not the window: clients such as Firefox draw their tab strip
 * above their reported geometry. A window going into the tiling where it is dropped splits the
 * tile there instead, unless it is dropped at the top. */
static enum sh_action snap_zone(struct sh_server *server, struct wlr_output **where) {
    struct sh_toplevel *toplevel = server->grabbed_toplevel;
    const struct sh_settings *settings = server_settings(server);
    *where = NULL;
    if (!toplevel || server->cursor_mode != SH_CURSOR_MOVE || server->grab_fullscreen ||
        !settings->snap || server->locked)
        return SH_NONE;
    double x = server->cursor->x, y = server->cursor->y;
    struct wlr_output *output = wlr_output_layout_output_at(server->output_layout, x, y);
    if (!output)
        return SH_NONE;
    enum sh_action zone =
        sh_snap_zone(usable_area(server, output), x, y, settings->snap_distance,
                     settings->snap_corners, shared_edges(server, output, x, y));
    if (zone != SH_NONE && zone != SH_MAXIMIZE && drop_tiles(server, output))
        zone = SH_NONE;
    if (zone != SH_NONE)
        *where = output;
    return zone;
}

/* The preview: a translucent rectangle of the slot in windows.snap.color, outlined in the same
 * colour more opaque and rounded as a window there is, just under the dragged window. It eases
 * out of the window's frame into the slot as the pointer comes to an edge, from slot to slot as
 * it goes along, and fades out where it is as the pointer leaves; the drop hides it at once. */
static bool preview_create(struct sh_server *server) {
    if (server->snap.preview)
        return true;
    struct wlr_scene_tree *tree = wlr_scene_tree_create(server->guide_layer);
    if (!tree)
        return false;
    const float clear[4] = {0};
    server->snap.fill = wlr_scene_rect_create(tree, 1, 1, clear);
    server->snap.outline = wlr_scene_rect_create(tree, 1, 1, clear);
    if (!server->snap.fill || !server->snap.outline) {
        wlr_scene_node_destroy(&tree->node);
        server->snap.fill = server->snap.outline = NULL;
        return false;
    }
    wlr_scene_node_set_enabled(&tree->node, false);
    server->snap.preview = tree;
    return true;
}

/* Out of sight, among the guides, until the next drag shows it. */
static void preview_put_away(struct sh_server *server) {
    struct wlr_scene_node *node = &server->snap.preview->node;
    wlr_scene_node_set_enabled(node, false);
    if (node->parent != server->guide_layer)
        wlr_scene_node_reparent(node, server->guide_layer);
}

/* Draws the preview as `drawn` says; once it has faded out, it goes. */
static void preview_draw(struct sh_server *server) {
    if (!server->snap.preview)
        return;
    const float *drawn = server->snap.drawn;
    int width = (int)lround(drawn[2]), height = (int)lround(drawn[3]);
    float alpha = fminf(1, drawn[4]);
    if (alpha <= 0 || width < 1 || height < 1) {
        if (!server->snap.tween.animator)
            preview_put_away(server);
        else
            wlr_scene_node_set_enabled(&server->snap.preview->node, false);
        return;
    }
    const float *color = server_settings(server)->snap_color;
    float strong = color[3] > 0 ? fminf(1, color[3] * 2.5f) / color[3] : 0;
    float fill[4], line[4];
    for (int i = 0; i < 4; ++i) {
        fill[i] = color[i] * alpha;
        line[i] = color[i] * strong * alpha;
    }
    wlr_scene_node_set_position(&server->snap.preview->node, (int)lround(drawn[0]),
                                (int)lround(drawn[1]));
    wlr_scene_rect_set_size(server->snap.fill, width, height);
    wlr_scene_rect_set_color(server->snap.fill, fill);
    // The outline is a hollow rectangle, which only the GLES2 renderer cuts with the
    // rounded-corners patch; the others draw it solid, so they go without.
    bool outlined = false;
#ifdef PAW_ROUNDED_CORNERS
    int radius = server->snap.radius;
    if (radius > width / 2 || radius > height / 2)
        radius = (width < height ? width : height) / 2;
    wlr_scene_rect_set_rounding(server->snap.fill, radius, 0);
#if WLR_HAS_GLES2_RENDERER
    outlined = width > 8 && height > 8 && wlr_renderer_is_gles2(server->renderer);
#endif
    if (outlined) {
        wlr_scene_rect_set_size(server->snap.outline, width, height);
        wlr_scene_rect_set_color(server->snap.outline, line);
        wlr_scene_rect_set_rounding(server->snap.outline, radius, 2);
    }
#endif
    wlr_scene_node_set_enabled(&server->snap.outline->node, outlined);
    wlr_scene_node_set_enabled(&server->snap.preview->node, true);
}

static void preview_update(void *data) {
    struct sh_server *server = data;
    memcpy(server->snap.drawn, server->snap.tween.value, sizeof(server->snap.drawn));
    preview_draw(server);
}

/* Just under the dragged window, among the windows, or over them where it is not among them. */
static void preview_stack(struct sh_server *server) {
    struct wlr_scene_node *node = &server->snap.preview->node;
    struct sh_toplevel *toplevel = server->grabbed_toplevel;
    struct wlr_scene_node *window =
        toplevel && toplevel->scene_tree ? &toplevel->scene_tree->node : NULL;
    if (window && is_window_layer(server, window->parent)) {
        if (node->parent != window->parent)
            wlr_scene_node_reparent(node, window->parent);
        wlr_scene_node_place_below(node, window);
    } else if (node->parent != server->guide_layer) {
        wlr_scene_node_reparent(node, server->guide_layer);
    }
}

/* The preview easing into `slot`, out of the dragged window's frame if it is not shown yet. */
static void preview_show(struct sh_server *server, struct sh_rect slot) {
    if (!preview_create(server))
        return;
    if (!server->snap.preview->node.enabled && !server->snap.tween.animator) {
        int border = server_settings(server)->border_width;
        struct wlr_box box = toplevel_box(server->grabbed_toplevel);
        const float from[SH_TWEEN_VALUES] = {box.x - border, box.y - border,
                                             box.width + 2 * border, box.height + 2 * border, 0};
        sh_tween_track(server->animator, &server->snap.tween, SH_ANIM_MOVE, false, from,
                       server->snap.drawn, preview_update, server);
    }
    const float to[SH_TWEEN_VALUES] = {slot.x, slot.y, slot.width, slot.height, 1};
    sh_tween_track(server->animator, &server->snap.tween, SH_ANIM_MOVE, true, to,
                   server->snap.drawn, preview_update, server);
    preview_stack(server);
    preview_draw(server);
}

/* The preview fading out where it is. */
static void preview_fade(struct sh_server *server) {
    if (!server->snap.preview || !server->snap.tween.valid || server->snap.tween.to[4] <= 0)
        return;
    float to[SH_TWEEN_VALUES];
    memcpy(to, server->snap.tween.to, sizeof(to));
    to[4] = 0;
    sh_tween_track(server->animator, &server->snap.tween, SH_ANIM_CLOSE, true, to,
                   server->snap.drawn, preview_update, server);
    preview_stack(server);
    preview_draw(server);
}

/* The corners of a window snapped there, as frame.c rounds them: windows on a monitor that
 * tiles, and with windows.round = "always" the others, but for one maximized on a monitor
 * that does not tile, which has no frame. */
static int slot_radius(struct sh_server *server, enum sh_action zone, struct wlr_output *output) {
#ifdef PAW_ROUNDED_CORNERS
    const struct sh_settings *settings = server_settings(server);
    bool tiles = output_tiles(server, output);
    if (zone == SH_MAXIMIZE && !tiles)
        return 0;
    return tiles || settings->round_always ? settings->corner_radius : 0;
#else
    return 0;
#endif
}

/* Follows the pointer while a window is moved: the zone it is in, the slot it stands for, and
 * the preview of it. */
void snap_follow(struct sh_server *server) {
    struct wlr_output *output;
    enum sh_action zone = snap_zone(server, &output);
    struct sh_rect slot = {0};
    if (zone != SH_NONE && !placed_slot(server, zone, output, &slot))
        zone = SH_NONE;
    server->snap.zone = zone;
    server->snap.slot = slot;
    if (zone != SH_NONE && server_settings(server)->snap_preview) {
        server->snap.radius = slot_radius(server, zone, output);
        preview_show(server, slot);
    } else {
        preview_fade(server);
    }
}

/* Forgets the zone and takes the preview away at once, as the drag ends. */
void snap_preview_hide(struct sh_server *server) {
    server->snap.zone = SH_NONE;
    server->snap.slot = (struct sh_rect){0};
    sh_tween_stop(&server->snap.tween);
    memset(server->snap.drawn, 0, sizeof(server->snap.drawn));
    if (server->snap.preview)
        preview_put_away(server);
}

/* Snaps the grabbed window as it is dropped, when the pointer is in a zone. A window that
 * floated when the drag began goes back there when it is restored; a tile lifted out goes
 * where it was dropped, at its floating size. Returns whether it snapped. */
bool snap_drop(struct sh_server *server) {
    struct sh_toplevel *toplevel = server->grabbed_toplevel;
    struct wlr_output *output;
    enum sh_action zone = snap_zone(server, &output);
    snap_preview_hide(server);
    if (zone == SH_NONE)
        return false;
    bool lifted = server->grab_retile;
    server->grab_retile = false;
    struct wlr_box before = server->snap.start;
    place_by_hand_on(toplevel, zone, output);
    if (!lifted && toplevel->arranged && before.width > 0 && before.height > 0)
        toplevel->restore_box = before;
    wlr_log(WLR_DEBUG, "Snapped a dropped window: %s on %s", snap_zone_name(zone), output->name);
    return true;
}

/* One step of Windows' Win+arrow for the focused window (see sh_snap_cycle). A fullscreen
 * window leaves fullscreen first; a tile counts as one at its own size, and floats as it snaps,
 * as with snap_left. A window that goes on to the next output takes the pointer along, as
 * move_left does. */
void snap_cycle(struct sh_server *server, enum sh_action direction) {
    struct sh_toplevel *toplevel = current_toplevel(server);
    if (!toplevel || server->locked)
        return;
    if (server->grabbed_toplevel == toplevel)
        reset_cursor_mode(server);
    if (toplevel->fullscreen)
        set_fullscreen(toplevel, false);
    enum sh_action from = toplevel->arranged && !toplevel->tiled ? toplevel->arrangement : SH_NONE;
    enum sh_action to;
    struct wlr_output *output = toplevel_output(toplevel), *next;
    switch (sh_snap_cycle(from, direction, &to)) {
    case SH_SNAP_STEP_STAY:
        break;
    case SH_SNAP_STEP_PLACE:
        if (output)
            place_by_hand_on(toplevel, to, output);
        break;
    case SH_SNAP_STEP_NEXT_OUTPUT: {
        struct wlr_box box = toplevel_box(toplevel);
        next = output ? wlr_output_layout_adjacent_output(
                            server->output_layout,
                            direction == SH_SNAP_CYCLE_LEFT ? WLR_DIRECTION_LEFT
                                                            : WLR_DIRECTION_RIGHT,
                            output, box.x + box.width / 2.0, box.y + box.height / 2.0)
                      : NULL;
        if (!next)
            break;
        place_by_hand_on(toplevel, to, next);
        pointer_follow(toplevel);
        break;
    }
    case SH_SNAP_STEP_RESTORE:
        restore_toplevel(toplevel);
        break;
    case SH_SNAP_STEP_MINIMIZE:
        minimize_toplevel(toplevel);
        break;
    }
}

/* Snap Assist (windows.snap.assist): after `toplevel` snapped into a half or a quarter of its
 * output, by dragging, from the keyboard or as Snap Assist's pick, the output's other windows
 * show in the free slot beside it (sh_snap_assist_slot, the quarters the windows snapped there
 * hold being taken) for one to fill it. Not on an output that tiles, whose tiling places
 * windows itself. */
void snap_assist_offer(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    if (!server_settings(server)->snap_assist || server->locked || !toplevel->arranged ||
        toplevel->tiled || !sh_snap_quarters(toplevel->arrangement))
        return;
    struct wlr_output *output = toplevel_output(toplevel);
    if (!output || output_tiles(server, output))
        return;
    unsigned taken = sh_snap_quarters(toplevel->arrangement);
    struct sh_toplevel *other;
    wl_list_for_each(other, &server->toplevels, link) {
        if (other != toplevel && other->arranged && !other->tiled && toplevel_mapped(other) &&
            toplevel_visible(other) && toplevel_output(other) == output)
            taken |= sh_snap_quarters(other->arrangement);
    }
    enum sh_action slot = sh_snap_assist_slot(toplevel->arrangement, taken);
    struct sh_rect area;
    if (slot != SH_NONE && placed_slot(server, slot, output, &area))
        overview_assist(server, toplevel, output, slot, area, slot_radius(server, slot, output));
}
