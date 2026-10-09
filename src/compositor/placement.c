/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* Window placement: the output a window belongs to, snapping, maximizing and the one-shot
 * grid, reflowing arranged windows and tiles, and moving and resizing by keyboard. */
#include "server.h"

static struct wlr_output *box_output(struct sh_server *server, struct wlr_box box);

/* Layouts put their gap at the edges as well as between windows. Laying out with gap_inner
 * in an area grown or shrunk by the difference leaves gap_outer at the edges. Maximized
 * windows ignore gaps. */
struct sh_rect gap_area(const struct sh_settings *settings, struct sh_rect area,
                        enum sh_action action) {
    if (action == SH_MAXIMIZE)
        return area;
    int d = settings->gap_outer - settings->gap_inner;
    return (struct sh_rect){area.x + d, area.y + d, area.width - 2 * d, area.height - 2 * d};
}

/* The area the tiling of `workspace` on `output` is arranged in, and in `gap` the gap between
 * its tiles, while it holds its windows and `joining` more. With layout.smart_gaps a lone tile
 * has no gaps and fills the usable area. Every arrangement of a workspace's tiling and the
 * preview of a new tile take them from here, so a lone window stays where it is however it is
 * arranged, and the gaps come and go with its neighbours. */
struct sh_rect tiling_area(struct sh_server *server, struct wlr_output *output, int workspace,
                           int joining, int *gap) {
    const struct sh_settings *settings = server_settings(server);
    if (settings->smart_gaps &&
        sh_tiling_count(server->tiling, output->name, workspace) + joining == 1) {
        *gap = 0;
        return usable_area(server, output);
    }
    *gap = settings->gap_inner;
    return gap_area(settings, usable_area(server, output), SH_TILE);
}

/* Fullscreen windows have no border, nor do maximized ones on an output that does not tile:
 * their top edge is the screen's, so the pointer pushed against it lands on the window's drag
 * strip rather than a border. */
bool frameless(struct sh_toplevel *toplevel, struct wlr_output *output) {
    return toplevel->fullscreen ||
           (toplevel->arranged && toplevel->arrangement == SH_MAXIMIZE && !toplevel->tiled &&
            !tiles_for(toplevel, output));
}

/* Placed windows keep their border inside their slot. */
struct sh_rect inside_border(struct sh_server *server, struct sh_rect rect) {
    int b = server_settings(server)->border_width;
    if (rect.width <= 2 * b || rect.height <= 2 * b)
        return rect;
    return (struct sh_rect){rect.x + b, rect.y + b, rect.width - 2 * b, rect.height - 2 * b};
}

/* Preserve the original floating rectangle across repeated snap operations. */
static void place_toplevel(struct sh_toplevel *toplevel, enum sh_action action,
                           struct sh_rect target) {
    if (!toplevel->arranged)
        toplevel->restore_box = toplevel_box(toplevel);
    toplevel->arranged = true;
    toplevel->arrangement = action;
    struct wlr_box box = {target.x, target.y, target.width, target.height};
    if (!frameless(toplevel, box_output(toplevel->server, box)))
        target = inside_border(toplevel->server, target);
    toplevel_set_states(toplevel, action == SH_MAXIMIZE, action == SH_MAXIMIZE ? 0 : ALL_EDGES);
    toplevel_configure(toplevel, target.x, target.y, target.width, target.height);
}

/* The output a box shares the most area with, or NULL when it is on none. */
static struct wlr_output *box_output(struct sh_server *server, struct wlr_box box) {
    struct wlr_output *best = NULL;
    long best_area = 0;
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        struct wlr_box full, shared;
        wlr_output_layout_get_box(server->output_layout, output->wlr_output, &full);
        if (!wlr_box_intersection(&shared, &box, &full))
            continue;
        long area = (long)shared.width * shared.height;
        if (area > best_area) {
            best = output->wlr_output;
            best_area = area;
        }
    }
    return best;
}

/* A tile belongs to the output whose tiling holds it. Any other window belongs to the output
 * it covers most, not the one under its top-left corner: a window dropped across two
 * outputs, or moved from a larger one, must fit the output it is mostly on. */
struct wlr_output *toplevel_output(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    struct wlr_output *output = toplevel->tiled ? tiled_output(toplevel) : NULL;
    struct wlr_box box = toplevel_box(toplevel);
    if (!output)
        output = box_output(server, box);
    if (!output) {
        // Off every output: the one nearest its centre.
        double x, y;
        wlr_output_layout_closest_point(server->output_layout, NULL, box.x + box.width / 2.0,
                                        box.y + box.height / 2.0, &x, &y);
        output = wlr_output_layout_output_at(server->output_layout, x, y);
    }
    return output ? output : first_output(server);
}

/* A saved floating box, moved onto `output` when it was saved on another one: the same
 * place relative to the usable area, kept inside it. A box already on `output` only moves
 * if it sticks out past the edge. */
struct wlr_box rebase_box(struct sh_server *server, struct wlr_box box,
                          struct wlr_output *output) {
    if (!output || box.width <= 0 || box.height <= 0)
        return box;
    struct wlr_output *from = box_output(server, box);
    struct wlr_box full, shared;
    wlr_output_layout_get_box(server->output_layout, output, &full);
    if (from == output && wlr_box_intersection(&shared, &box, &full) &&
        wlr_box_equal(&shared, &box))
        return box;
    struct sh_rect area = usable_area(server, output);
    if (from && from != output) {
        struct sh_rect old = usable_area(server, from);
        box.x += area.x - old.x;
        box.y += area.y - old.y;
    }
    box.width = fmin(box.width, area.width);
    box.height = fmin(box.height, area.height);
    box.x = fmax(area.x, fmin(box.x, area.x + area.width - box.width));
    box.y = fmax(area.y, fmin(box.y, area.y + area.height - box.height));
    return box;
}

void restore_toplevel(struct sh_toplevel *toplevel) {
    if (!toplevel->arranged)
        return;
    struct wlr_output *output = toplevel_output(toplevel);
    toplevel->arranged = false;
    toplevel_set_states(toplevel, false, 0);
    toplevel->restore_box = rebase_box(toplevel->server, toplevel->restore_box, output);
    toplevel_configure_box(toplevel, toplevel->restore_box);
}

void place_maximized(struct sh_toplevel *toplevel) {
    struct wlr_output *output = toplevel_output(toplevel);
    if (output)
        place_toplevel(toplevel, SH_MAXIMIZE, usable_area(toplevel->server, output));
}

/* Windows taking part in the one-shot grid arrangement of `output`. */
static bool in_grid(struct sh_toplevel *toplevel, struct wlr_output *output) {
    return toplevel_visible(toplevel) && !toplevel->fullscreen &&
           toplevel_output(toplevel) == output;
}

/* Snaps or maximizes one window within the usable area of its output. A tiled window placed
 * by hand floats from then on, or, lifted out by a drag, stays out of the tiling. Snapping a
 * window again to the side it is on moves it to the near half of the next output that way. */
void place_by_hand(struct sh_toplevel *toplevel, enum sh_action action) {
    struct sh_server *server = toplevel->server;
    bool again = (action == SH_SNAP_LEFT || action == SH_SNAP_RIGHT) && toplevel->arranged &&
                 !toplevel->tiled && toplevel->arrangement == action;
    if (toplevel->tiled || wants_tiling(toplevel, NULL))
        toplevel->floating = toplevel->placed = true;
    if (toplevel->tiled)
        untile_toplevel(toplevel, false);
    struct wlr_output *output = toplevel_output(toplevel);
    if (!output)
        return;
    if (again) {
        struct wlr_box box = toplevel_box(toplevel);
        struct wlr_output *next = wlr_output_layout_adjacent_output(
            server->output_layout, action == SH_SNAP_LEFT ? WLR_DIRECTION_LEFT : WLR_DIRECTION_RIGHT,
            output, box.x + box.width / 2.0, box.y + box.height / 2.0);
        if (next) {
            output = next;
            action = action == SH_SNAP_LEFT ? SH_SNAP_RIGHT : SH_SNAP_LEFT;
        }
    }
    place_by_hand_on(toplevel, action, output);
}

/* The slot snapping or maximizing puts a window in on `output`, its border included. */
bool placed_slot(struct sh_server *server, enum sh_action action, struct wlr_output *output,
                 struct sh_rect *slot) {
    const struct sh_settings *settings = server_settings(server);
    struct sh_rect area = gap_area(settings, usable_area(server, output), action);
    return sh_placement(action, area, settings->gap_inner, 0, 1, slot);
}

/* Snaps or maximizes one window within the usable area of `output`, as place_by_hand does
 * on the output it is on: a window dropped at an edge goes to the output under the pointer.
 * Snapped into a half or a quarter, it may bring Snap Assist up beside it. */
void place_by_hand_on(struct sh_toplevel *toplevel, enum sh_action action,
                      struct wlr_output *output) {
    if (toplevel->tiled || wants_tiling(toplevel, output))
        toplevel->floating = toplevel->placed = true;
    if (toplevel->tiled)
        untile_toplevel(toplevel, false);
    struct sh_rect target;
    if (!placed_slot(toplevel->server, action, output, &target))
        return;
    place_toplevel(toplevel, action, target);
    snap_assist_offer(toplevel);
}

void arrange_windows(struct sh_server *server, enum sh_action action) {
    struct sh_toplevel *focused = current_toplevel(server);
    if (!focused)
        return;
    if (action == SH_TILE && tiles_for(focused, toplevel_output(focused)))
        return; // Already tiled automatically.
    if (focused->tiled && action == SH_RESTORE)
        return;
    if (focused->fullscreen)
        set_fullscreen(focused, false);
    if (action == SH_RESTORE) {
        restore_toplevel(focused);
        return;
    }
    if (action != SH_TILE) {
        place_by_hand(focused, action);
        return;
    }
    struct wlr_output *output = toplevel_output(focused);
    if (!output)
        return;
    const struct sh_settings *settings = server_settings(server);
    struct sh_rect area = gap_area(settings, usable_area(server, output), action), target;
    int gap = settings->gap_inner;
    int count = 0, index = 0;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) count += in_grid(toplevel, output);
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (in_grid(toplevel, output) && sh_placement(action, area, gap, index++, count, &target))
            place_toplevel(toplevel, action, target);
    }
}

static void place_tiled(void *data, void *window, struct sh_rect rect) {
    struct sh_toplevel *toplevel = window;
    if (toplevel->fullscreen)
        return; // It returns to its tile when it leaves fullscreen.
    toplevel_set_states(toplevel, false, ALL_EDGES);
    rect = inside_border(toplevel->server, rect);
    struct wlr_scene_node *node = &toplevel->scene_tree->node;
    int x = node->x, y = node->y;
    toplevel_configure(toplevel, rect.x, rect.y, rect.width, rect.height);
    // Tiles already on screen glide to their new place; the size follows when the client
    // draws it.
    if (toplevel->shown && toplevel_visible(toplevel))
        sh_anim_glide(toplevel->server->animator, &toplevel->anim, toplevel->content, x - node->x,
                      y - node->y);
}

/* Snapped, maximized, or grid-arranged windows that follow changes to the usable area. */
static bool reflows(struct sh_toplevel *toplevel, int workspace, struct wlr_output *output) {
    return toplevel->workspace == workspace && toplevel->arranged && !toplevel->minimized &&
           !toplevel->group_hidden && !toplevel->swallowed &&
           !toplevel->fullscreen && toplevel_output(toplevel) == output;
}

void reflow_output(struct sh_server *server, struct wlr_output *output) {
    if (server->reflow_held)
        return;
    ++server->stats.reflows;
    uint64_t started = now_ns();
    struct sh_rect area = usable_area(server, output), target;
    const struct sh_settings *settings = server_settings(server);
    for (int workspace = 0; workspace < settings->workspaces; ++workspace) {
        int count = 0, index = 0;
        struct sh_toplevel *toplevel;
        wl_list_for_each(toplevel, &server->toplevels, link) {
            count += reflows(toplevel, workspace, output) && toplevel->arrangement == SH_TILE;
        }
        wl_list_for_each(toplevel, &server->toplevels, link) {
            if (!reflows(toplevel, workspace, output))
                continue;
            enum sh_action action = toplevel->arrangement;
            if (sh_placement(action, gap_area(settings, area, action), settings->gap_inner,
                             action == SH_TILE ? index++ : 0, action == SH_TILE ? count : 1,
                             &target))
                place_toplevel(toplevel, action, target);
        }
        int gap;
        struct sh_rect tiles = tiling_area(server, output, workspace, 0, &gap);
        sh_tiling_arrange(server->tiling, output->name, workspace, tiles, gap, place_tiled, NULL);
    }
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel->fullscreen && toplevel_output(toplevel) == output)
            fit_fullscreen(toplevel); // it follows the panels' exclusive zones
    }
    server->stats.reflow_ns += now_ns() - started;
}

/* The usable area of `output` less the outer gap and the window border: where a floating
 * window moved by the keyboard may go. */
struct sh_rect floating_area(struct sh_server *server, struct wlr_output *output) {
    int gap = server_settings(server)->gap_outer;
    struct sh_rect area = usable_area(server, output);
    area = (struct sh_rect){area.x + gap, area.y + gap, area.width - 2 * gap,
                            area.height - 2 * gap};
    return inside_border(server, area);
}

/* `box` placed against the side of `area` facing the direction, kept inside `area` across
 * it. */
static struct wlr_box against_edge(struct wlr_box box, struct sh_rect area, bool horizontal,
                                   int sign) {
    int x = sign < 0 ? area.x : fmax(area.x, area.x + area.width - box.width);
    int y = sign < 0 ? area.y : fmax(area.y, area.y + area.height - box.height);
    if (horizontal) {
        box.x = x;
        box.y = fmax(area.y, fmin(box.y, area.y + area.height - box.height));
    } else {
        box.y = y;
        box.x = fmax(area.x, fmin(box.x, area.x + area.width - box.width));
    }
    return box;
}

/* A snapped or grid-arranged window leaves its arrangement to be moved: it gets its floating
 * size back where it is. */
void unarrange_in_place(struct sh_toplevel *toplevel) {
    if (!toplevel->arranged)
        return;
    struct wlr_box box = toplevel_box(toplevel);
    if (toplevel->restore_box.width > 0 && toplevel->restore_box.height > 0) {
        box.width = toplevel->restore_box.width;
        box.height = toplevel->restore_box.height;
    }
    toplevel->arranged = false;
    toplevel_set_states(toplevel, false, 0);
    toplevel_configure_box(toplevel, box);
}

/* Moves the window over to `next`, onto the side facing where it came from: into its tiling
 * when the window tiles there, else floating against that edge. A maximized window stays
 * maximized. */
static void move_to_output(struct sh_toplevel *toplevel, struct wlr_output *next, bool horizontal,
                           int sign) {
    struct sh_server *server = toplevel->server;
    struct sh_rect area = usable_area(server, next);
    struct wlr_box box = toplevel_box(toplevel);
    untile_toplevel(toplevel, false);
    if (wants_tiling(toplevel, next)) {
        double x = fmax(area.x, fmin(box.x + box.width / 2.0, area.x + area.width - 1));
        double y = fmax(area.y, fmin(box.y + box.height / 2.0, area.y + area.height - 1));
        if (horizontal)
            x = sign < 0 ? area.x + area.width - 1 : area.x;
        else
            y = sign < 0 ? area.y + area.height - 1 : area.y;
        tile_toplevel_at(toplevel, next, NULL, true, x, y);
        return;
    }
    if (toplevel->arranged && toplevel->arrangement == SH_MAXIMIZE) {
        place_toplevel(toplevel, SH_MAXIMIZE, area);
        return;
    }
    unarrange_in_place(toplevel);
    box = rebase_box(server, toplevel_box(toplevel), next);
    toplevel_configure_box(toplevel, against_edge(box, floating_area(server, next), horizontal,
                                                  -sign));
}

/* Hyprland's movewindow. A tile trades places with the nearest tile that way; a floating
 * window moves to that edge of its output. From the edge, either moves on to the next output
 * that way, if there is one. Neither ever covers another tile or grows to fill half the
 * output. */
void move_window(struct sh_server *server, enum sh_action action) {
    struct sh_toplevel *toplevel = current_toplevel(server);
    if (!toplevel || server->locked || toplevel->fullscreen)
        return;
    if (server->grabbed_toplevel == toplevel)
        reset_cursor_mode(server);
    bool horizontal = action == SH_MOVE_LEFT || action == SH_MOVE_RIGHT;
    int sign = action == SH_MOVE_LEFT || action == SH_MOVE_UP ? -1 : 1;
    struct wlr_output *output = toplevel_output(toplevel);
    if (!output)
        return;
    if (toplevel->tiled) {
        bool strip = toplevel_layout(toplevel) == SH_LAYOUT_SCROLL;
        if (strip && horizontal) {
            // Moves the whole column along the strip; from its end, on to the next output.
            if (sh_tiling_scroll_move(server->tiling, toplevel, sign)) {
                sh_tiling_set_focus(server->tiling, toplevel);
                reflow_output(server, output);
                pointer_follow(toplevel);
                return;
            }
        }
        bool monocle = toplevel_layout(toplevel) == SH_LAYOUT_MONOCLE;
        struct sh_toplevel *neighbour =
            strip ? (horizontal ? NULL
                                : sh_tiling_scroll_step(server->tiling, toplevel, 0, sign))
                  : monocle ? sh_tiling_neighbour(server->tiling, toplevel, sign)
                            : toplevel_toward(toplevel, horizontal, sign, true);
        if (neighbour && toplevel_layout(toplevel) != SH_LAYOUT_DWINDLE) {
            // Outside dwindle, tiles keep their places in the list: trade with the neighbour.
            sh_tiling_swap(server->tiling, toplevel, neighbour);
            reflow_output(server, output);
            pointer_follow(toplevel);
            return;
        }
        if (neighbour) {
            move_tile(toplevel, neighbour, output, horizontal, sign);
            pointer_follow(toplevel);
            return;
        }
    } else if (!toplevel->arranged || toplevel->arrangement != SH_MAXIMIZE) {
        unarrange_in_place(toplevel);
        struct wlr_box box = toplevel_box(toplevel);
        struct wlr_box moved =
            against_edge(box, floating_area(server, output), horizontal, sign);
        if (moved.x != box.x || moved.y != box.y) {
            toplevel_set_position(toplevel, moved.x, moved.y);
            pointer_follow(toplevel);
            return;
        }
    }
    static const enum wlr_direction directions[] = {WLR_DIRECTION_LEFT, WLR_DIRECTION_RIGHT,
                                                    WLR_DIRECTION_UP, WLR_DIRECTION_DOWN};
    struct wlr_box box = toplevel_box(toplevel);
    struct wlr_output *next = wlr_output_layout_adjacent_output(
        server->output_layout, directions[action - SH_MOVE_LEFT], output,
        box.x + box.width / 2.0, box.y + box.height / 2.0);
    if (!next)
        return;
    move_to_output(toplevel, next, horizontal, sign);
    pointer_follow(toplevel);
}

/* The smallest size keyboard resizing shrinks a floating window to, unless the client asks
 * for more. */
#define SH_RESIZE_MIN 64

/* Keyboard resizing by `amount` pixels. A tile moves the split on that side of it that way,
 * growing it, or, touching the output on that side, the split on its other side, shrinking
 * it. A floating window moves its right or bottom edge that way, growing no further than its
 * output's edge and shrinking no smaller than SH_RESIZE_MIN. A snapped window leaves its
 * arrangement at its current size; maximized and fullscreen windows stay as they are, and
 * features.keyboard_resize = false turns this off. */
void resize_window(struct sh_server *server, enum sh_action action, int amount) {
    struct sh_toplevel *toplevel = current_toplevel(server);
    if (!server_settings(server)->keyboard_resize || !toplevel || server->locked ||
        toplevel->fullscreen || amount <= 0 ||
        (toplevel->arranged && toplevel->arrangement == SH_MAXIMIZE))
        return;
    if (server->grabbed_toplevel == toplevel)
        reset_cursor_mode(server);
    static const uint32_t directions[] = {SH_EDGE_LEFT, SH_EDGE_RIGHT, SH_EDGE_TOP,
                                          SH_EDGE_BOTTOM};
    uint32_t direction = directions[action - SH_RESIZE_LEFT];
    if (toplevel->tiled) {
        struct wlr_output *output = tiled_output(toplevel);
        if (output && sh_tiling_resize_by(server->tiling, toplevel, direction, amount))
            reflow_output(server, output);
        return;
    }
    struct wlr_output *output = toplevel_output(toplevel);
    if (!output)
        return;
    struct sh_rect area = floating_area(server, output);
    struct wlr_box box = toplevel_box(toplevel);
    bool horizontal = direction == SH_EDGE_LEFT || direction == SH_EDGE_RIGHT;
    int shift = direction == SH_EDGE_RIGHT || direction == SH_EDGE_BOTTOM ? amount : -amount;
    int start = horizontal ? box.x : box.y, size = horizontal ? box.width : box.height;
    int limit = horizontal ? area.x + area.width : area.y + area.height;
    int least = SH_RESIZE_MIN;
    if (toplevel->xdg_toplevel) {
        const struct wlr_xdg_toplevel_state *state = &toplevel->xdg_toplevel->current;
        least = fmax(least, horizontal ? state->min_width : state->min_height);
    }
    // A window already past the edge, or already below the minimum, gets no worse.
    int end = fmin(start + size + shift, fmax(limit, start + size));
    int resized = fmax(end - start, fmin(least, size));
    if (resized == size)
        return;
    if (toplevel->arranged) {
        toplevel->arranged = false;
        toplevel_set_states(toplevel, false, 0);
    }
    if (horizontal)
        box.width = resized;
    else
        box.height = resized;
    toplevel_configure_box(toplevel, box);
}
