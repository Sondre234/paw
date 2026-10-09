/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* Moving and resizing windows with the pointer: the grab, the magnetic edges that catch a
 * dragged window, and what dropping it does. */
#include "server.h"

static void magnet_hide_guides(struct sh_server *server) {
    for (int i = 0; i < 2; ++i) {
        if (server->guides[i])
            wlr_scene_node_set_enabled(&server->guides[i]->node, false);
    }
}

void reset_cursor_mode(struct sh_server *server) {
    magnet_hide_guides(server);
    snap_preview_hide(server);
    // A window dropped on another output takes up that output's corners.
    if (server->grabbed_toplevel)
        refresh_frame(server->grabbed_toplevel);
    server->cursor_mode = SH_CURSOR_PASSTHROUGH;
    server->grabbed_toplevel = NULL;
    server->grab_retile = false;
    server->grab_output = NULL;
    server->grab_fullscreen = false;
}

/* A window floating only because it was snapped or maximized, or because its output does not
 * tile, joins the tiling of another output it is dropped on; one floated on purpose stays
 * floating. */
static bool joins_tiling(struct sh_server *server, struct sh_toplevel *toplevel,
                         struct wlr_output *output) {
    return toplevel && server->cursor_mode == SH_CURSOR_MOVE && !toplevel->tiled &&
           !toplevel->sticky && (!toplevel->floating || toplevel->placed) && output &&
           output != server->grab_output && output_tiles(server, output);
}

/* Whether dropping the grabbed window with the pointer on `output` puts it into the tiling
 * there: a tile lifted out to be moved, or a window joining another output's tiling. */
bool drop_tiles(struct sh_server *server, struct wlr_output *output) {
    struct sh_toplevel *toplevel = server->grabbed_toplevel;
    return joins_tiling(server, toplevel, output) ||
           (server->grab_retile && toplevel && wants_tiling(toplevel, output));
}

/* Dropping a window dragged out of the tiling splits the tile under the pointer; dropping one
 * at an edge of the screen snaps it there instead (snap.c), at the top maximized below the
 * panels, like Super+Shift+Up. */
void finish_grab(struct sh_server *server) {
    struct sh_toplevel *toplevel = server->grabbed_toplevel;
    if (snap_drop(server))
        return;
    struct wlr_output *output =
        wlr_output_layout_output_at(server->output_layout, server->cursor->x, server->cursor->y);
    // A tile dropped on an output that does not tile floats there.
    if (joins_tiling(server, toplevel, output)) {
        toplevel->floating = toplevel->placed = false;
        server->grab_retile = true;
    }
    if (server->grab_retile && toplevel && wants_tiling(toplevel, output))
        tile_toplevel(toplevel, output, NULL, true);
    server->grab_retile = false;
}

/* Pointer travel that turns a press on a fullscreen window into a drag out of fullscreen. */
#define SH_DRAG_THRESHOLD 8

/* Magnetic edges (windows.magnet). While a floating window is dragged or resized, an edge of it
 * that comes within `distance` of an edge of the output, of the area panels leave free, or of
 * another window (one whose extent beside it overlaps the dragged window's) lands on it. The
 * position always follows from where the pointer is, not from where the window was, so the
 * window stays held until the pointer has moved `distance` away and then follows it again. */
#define MAGNET_LINES 80

/* An edge a window can stick to: a vertical line at x = `at` (or a horizontal one at y = `at`)
 * spanning [from, to] along the other axis, or, with `outer`, the whole of it. */
struct magnet_line {
    int at, from, to;
    bool outer;
};
struct magnet_lines {
    struct magnet_line line[MAGNET_LINES];
    int count;
};

/* The edge of the window that landed, and where. */
struct magnet_hit {
    bool found;
    int delta;
    struct magnet_line line;
};

static void magnet_add(struct magnet_lines *lines, int at, int from, int to, bool outer) {
    if (lines->count < MAGNET_LINES)
        lines->line[lines->count++] = (struct magnet_line){at, from, to, outer};
}

/* The lines to consider for a window occupying `frame` (the frame the eye sees, border
 * included): vertical ones in `x`, horizontal ones in `y`. */
static void magnet_collect(struct sh_server *server, struct sh_toplevel *toplevel,
                           struct wlr_output *output, struct wlr_box frame,
                           struct magnet_lines *x, struct magnet_lines *y) {
    int distance = server_settings(server)->magnet_distance;
    int border = server_settings(server)->border_width;
    struct wlr_box full;
    wlr_output_layout_get_box(server->output_layout, output, &full);
    struct sh_rect free_area = usable_area(server, output);
    int xs[] = {full.x, full.x + full.width, free_area.x, free_area.x + free_area.width};
    int ys[] = {full.y, full.y + full.height, free_area.y, free_area.y + free_area.height};
    for (size_t i = 0; i < sizeof(xs) / sizeof(*xs); ++i) {
        magnet_add(x, xs[i], 0, 0, true);
        magnet_add(y, ys[i], 0, 0, true);
    }
    int left = frame.x, right = frame.x + frame.width, top = frame.y,
        bottom = frame.y + frame.height;
    struct sh_toplevel *other;
    wl_list_for_each(other, &server->toplevels, link) {
        if (other == toplevel || !toplevel_mapped(other) || !toplevel_visible(other) ||
            other->fullscreen || other->minimized || toplevel_output(other) != output)
            continue;
        struct wlr_box b = toplevel_box(other);
        int o_left = b.x - border, o_right = b.x + b.width + border, o_top = b.y - border,
            o_bottom = b.y + b.height + border;
        if (top - distance <= o_bottom && bottom + distance >= o_top) {
            magnet_add(x, o_left, o_top, o_bottom, false);
            magnet_add(x, o_right, o_top, o_bottom, false);
        }
        if (left - distance <= o_right && right + distance >= o_left) {
            magnet_add(y, o_top, o_left, o_right, false);
            magnet_add(y, o_bottom, o_left, o_right, false);
        }
    }
}

/* Moves the edges `low` and `high` (each when allowed) onto the nearest line within reach. */
static struct magnet_hit magnet_best(const struct magnet_lines *lines, int low, int high,
                                     bool use_low, bool use_high, int distance) {
    struct magnet_hit hit = {0};
    for (int i = 0; i < lines->count; ++i) {
        for (int side = 0; side < 2; ++side) {
            if (!(side ? use_high : use_low))
                continue;
            int delta = lines->line[i].at - (side ? high : low);
            if (abs(delta) > distance || (hit.found && abs(delta) >= abs(hit.delta)))
                continue;
            hit.found = true;
            hit.delta = delta;
            hit.line = lines->line[i];
        }
    }
    return hit;
}

static void magnet_draw(struct sh_server *server, int index, bool show, struct wlr_box box) {
    if (!show) {
        if (server->guides[index])
            wlr_scene_node_set_enabled(&server->guides[index]->node, false);
        return;
    }
    const struct sh_settings *settings = server_settings(server);
    if (!server->guides[index])
        server->guides[index] = wlr_scene_rect_create(server->guide_layer, box.width, box.height,
                                                      settings->magnet_guide_color);
    struct wlr_scene_rect *rect = server->guides[index];
    wlr_scene_rect_set_size(rect, box.width, box.height);
    wlr_scene_rect_set_color(rect, settings->magnet_guide_color);
    wlr_scene_node_set_position(&rect->node, box.x, box.y);
    wlr_scene_node_set_enabled(&rect->node, true);
}

/* Draws the guide lines for edges that landed: `frame` is the window's frame now. */
static void magnet_show(struct sh_server *server, struct wlr_box frame, struct magnet_hit x,
                        struct magnet_hit y) {
    const int thickness = 3; // an edge of the output still shows two of them
    bool guides = server_settings(server)->magnet_guides;
    int left = frame.x, right = frame.x + frame.width, top = frame.y,
        bottom = frame.y + frame.height;
    // A line covers the extent of the windows it joins.
    int from = x.found && !x.line.outer && x.line.from < top ? x.line.from : top;
    int to = x.found && !x.line.outer && x.line.to > bottom ? x.line.to : bottom;
    magnet_draw(server, 0, guides && x.found,
                (struct wlr_box){x.line.at - thickness / 2, from, thickness, to - from});
    from = y.found && !y.line.outer && y.line.from < left ? y.line.from : left;
    to = y.found && !y.line.outer && y.line.to > right ? y.line.to : right;
    magnet_draw(server, 1, guides && y.found,
                (struct wlr_box){from, y.line.at - thickness / 2, to - from, thickness});
}

/* The modifiers held on any keyboard, virtual ones (wtype, tests) included. */
static uint32_t held_modifiers(struct sh_server *server) {
    uint32_t mods = 0;
    struct sh_keyboard *keyboard;
    wl_list_for_each(keyboard, &server->keyboards, link) {
        mods |= wlr_keyboard_get_modifiers(keyboard->wlr_keyboard);
    }
    return mods;
}

/* The output the magnetism works on, or NULL when it is off for this drag. */
static struct wlr_output *magnet_output(struct sh_server *server, struct sh_toplevel *toplevel) {
    const struct sh_settings *settings = server_settings(server);
    uint32_t mods = held_modifiers(server);
    struct wlr_output *output =
        wlr_output_layout_output_at(server->output_layout, server->cursor->x, server->cursor->y);
    if (!settings->magnet || settings->magnet_distance <= 0 || !output || toplevel->tiled ||
        server->grab_retile || server->grab_fullscreen ||
        (settings->magnet_bypass && (mods & settings->magnet_bypass) == settings->magnet_bypass)) {
        magnet_hide_guides(server);
        return NULL;
    }
    return output;
}

/* Adjusts the position (x, y) a drag asks for. */
static void magnet_snap(struct sh_server *server, struct sh_toplevel *toplevel, int *x, int *y) {
    struct wlr_output *output = magnet_output(server, toplevel);
    if (!output)
        return;
    int distance = server_settings(server)->magnet_distance;
    int border = server_settings(server)->border_width;
    struct wlr_box box = toplevel_box(toplevel);
    // The frame the eye sees: the border is drawn outside the window's geometry.
    struct wlr_box frame = {*x - border, *y - border, box.width + 2 * border,
                            box.height + 2 * border};
    struct magnet_lines vertical = {0}, horizontal = {0};
    magnet_collect(server, toplevel, output, frame, &vertical, &horizontal);
    struct magnet_hit hx = magnet_best(&vertical, frame.x, frame.x + frame.width, true, true,
                                       distance);
    struct magnet_hit hy = magnet_best(&horizontal, frame.y, frame.y + frame.height, true, true,
                                       distance);
    if (hx.found)
        *x += hx.delta, frame.x += hx.delta;
    if (hy.found)
        *y += hy.delta, frame.y += hy.delta;
    magnet_show(server, frame, hx, hy);
}

/* Adjusts the edges a resize asks for (the sides in `edges` are the ones being moved). */
static void magnet_snap_resize(struct sh_server *server, struct sh_toplevel *toplevel,
                               uint32_t edges, int *left, int *top, int *right, int *bottom) {
    struct wlr_output *output = magnet_output(server, toplevel);
    if (!output)
        return;
    int distance = server_settings(server)->magnet_distance;
    int border = server_settings(server)->border_width;
    struct wlr_box frame = {*left - border, *top - border, *right - *left + 2 * border,
                            *bottom - *top + 2 * border};
    struct magnet_lines vertical = {0}, horizontal = {0};
    magnet_collect(server, toplevel, output, frame, &vertical, &horizontal);
    struct magnet_hit hx = magnet_best(&vertical, frame.x, frame.x + frame.width,
                                       edges & WLR_EDGE_LEFT, edges & WLR_EDGE_RIGHT, distance);
    struct magnet_hit hy = magnet_best(&horizontal, frame.y, frame.y + frame.height,
                                       edges & WLR_EDGE_TOP, edges & WLR_EDGE_BOTTOM, distance);
    // A window keeps a pixel of width and height however the edge is pulled.
    if (hx.found) {
        if (edges & WLR_EDGE_LEFT && *left + hx.delta < *right)
            *left += hx.delta, frame.x += hx.delta, frame.width -= hx.delta;
        else if (edges & WLR_EDGE_RIGHT && *right + hx.delta > *left)
            *right += hx.delta, frame.width += hx.delta;
        else
            hx.found = false;
    }
    if (hy.found) {
        if (edges & WLR_EDGE_TOP && *top + hy.delta < *bottom)
            *top += hy.delta, frame.y += hy.delta, frame.height -= hy.delta;
        else if (edges & WLR_EDGE_BOTTOM && *bottom + hy.delta > *top)
            *bottom += hy.delta, frame.height += hy.delta;
        else
            hy.found = false;
    }
    magnet_show(server, frame, hx, hy);
}

void process_cursor_move(struct sh_server *server) {
    struct sh_toplevel *toplevel = server->grabbed_toplevel;
    if (server->grab_fullscreen) {
        if (hypot(server->cursor->x - server->grab_x, server->cursor->y - server->grab_y) <
            SH_DRAG_THRESHOLD)
            return;
        // Leave fullscreen, then drag the window at the size it had before, like a snapped one.
        reset_cursor_mode(server);
        set_fullscreen(toplevel, false);
        if (!toplevel->arranged && !toplevel->tiled) {
            toplevel->restore_box = toplevel_box(toplevel);
            toplevel->arranged = true;
        }
        begin_interactive(toplevel, SH_CURSOR_MOVE, 0);
    }
    int x = (int)(server->cursor->x - server->grab_x), y = (int)(server->cursor->y - server->grab_y);
    magnet_snap(server, toplevel, &x, &y);
    toplevel_set_position(toplevel, x, y);
    snap_follow(server);
}

void process_cursor_resize(struct sh_server *server) {
    struct sh_toplevel *toplevel = server->grabbed_toplevel;
    double border_x = server->cursor->x - server->grab_x;
    double border_y = server->cursor->y - server->grab_y;
    int new_left = server->grab_geobox.x;
    int new_right = server->grab_geobox.x + server->grab_geobox.width;
    int new_top = server->grab_geobox.y;
    int new_bottom = server->grab_geobox.y + server->grab_geobox.height;

    if (server->resize_edges & WLR_EDGE_TOP) {
        new_top = border_y;
        if (new_top >= new_bottom) {
            new_top = new_bottom - 1;
        }
    } else if (server->resize_edges & WLR_EDGE_BOTTOM) {
        new_bottom = border_y;
        if (new_bottom <= new_top) {
            new_bottom = new_top + 1;
        }
    }
    if (server->resize_edges & WLR_EDGE_LEFT) {
        new_left = border_x;
        if (new_left >= new_right) {
            new_left = new_right - 1;
        }
    } else if (server->resize_edges & WLR_EDGE_RIGHT) {
        new_right = border_x;
        if (new_right <= new_left) {
            new_right = new_left + 1;
        }
    }

    if (!toplevel->tiled)
        magnet_snap_resize(server, toplevel, server->resize_edges, &new_left, &new_top,
                           &new_right, &new_bottom);
    if (toplevel->tiled) {
        // Resizing a tile moves the splits beside the dragged edges instead.
        struct sh_rect rect = {new_left, new_top, new_right - new_left, new_bottom - new_top};
        struct wlr_output *output = tiled_output(toplevel);
        if (sh_tiling_resize(server->tiling, toplevel, server->resize_edges, rect) && output)
            reflow_output(server, output);
        return;
    }
    struct wlr_box geo_box = toplevel_geometry(toplevel);
    toplevel_configure(toplevel, new_left - geo_box.x, new_top - geo_box.y, new_right - new_left,
                       new_bottom - new_top);
}

void begin_interactive(struct sh_toplevel *toplevel, enum sh_cursor_mode mode,
                       uint32_t edges) {
    struct sh_server *server = toplevel->server;
    if (toplevel->fullscreen) {
        // Only a move, and it leaves fullscreen once the pointer has travelled a little.
        if (mode != SH_CURSOR_MOVE)
            return;
        server->grabbed_toplevel = toplevel;
        server->cursor_mode = mode;
        server->grab_fullscreen = true;
        server->grab_x = server->cursor->x;
        server->grab_y = server->cursor->y;
        return;
    }

    server->grab_output = toplevel_output(toplevel);
    // Resizing a tile moves its splits; moving one lifts it out until it is dropped.
    bool tiled_resize = toplevel->tiled && mode == SH_CURSOR_RESIZE;
    bool retile = toplevel->tiled && mode == SH_CURSOR_MOVE;
    if (retile)
        untile_toplevel(toplevel, false);
    bool was_arranged = toplevel->arranged;
    if (!tiled_resize) {
        toplevel->arranged = false;
        toplevel_set_states(toplevel, false, 0);
    }
    server->grab_retile = retile;
    server->grabbed_toplevel = toplevel;
    server->cursor_mode = mode;

    if (mode == SH_CURSOR_MOVE && was_arranged) {
        /* Dragging a maximized or snapped window restores its floating size, keeping the
         * pointer at the same relative spot across the width and at most as far down. */
        struct wlr_box geometry = toplevel_geometry(toplevel);
        struct wlr_box restore = toplevel->restore_box;
        if (restore.width <= 0 || restore.height <= 0) { // never floated: keep its size
            restore.width = geometry.width;
            restore.height = geometry.height;
        }
        double from_left = server->cursor->x - toplevel->scene_tree->node.x;
        double from_top = server->cursor->y - toplevel->scene_tree->node.y;
        if (geometry.width > 0)
            from_left = from_left * restore.width / geometry.width;
        if (from_top > restore.height)
            from_top = restore.height / 2.0;
        toplevel_configure(toplevel, server->cursor->x - from_left, server->cursor->y - from_top,
                           restore.width, restore.height);
    }

    if (mode == SH_CURSOR_MOVE) {
        server->grab_x = server->cursor->x - toplevel->scene_tree->node.x;
        server->grab_y = server->cursor->y - toplevel->scene_tree->node.y;
        server->snap.start = toplevel_box(toplevel); // where a snap restores it to
    } else {
        struct wlr_box geo_box = toplevel_geometry(toplevel);

        edges = corner_edges(toplevel, edges);

        double border_x = (toplevel->scene_tree->node.x + geo_box.x) +
                          ((edges & WLR_EDGE_RIGHT) ? geo_box.width : 0);
        double border_y = (toplevel->scene_tree->node.y + geo_box.y) +
                          ((edges & WLR_EDGE_BOTTOM) ? geo_box.height : 0);
        server->grab_x = server->cursor->x - border_x;
        server->grab_y = server->cursor->y - border_y;

        server->grab_geobox = geo_box;
        server->grab_geobox.x += toplevel->scene_tree->node.x;
        server->grab_geobox.y += toplevel->scene_tree->node.y;

        server->resize_edges = edges;
    }
}
