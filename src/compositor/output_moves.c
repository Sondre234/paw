/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* Windows changing outputs: an output's windows move to another when it goes away and return
 * when it comes back, and workspaces move or swap between outputs. */
#include "server.h"

/* `box` moved from where it was on the output covering `from` to the same place relative to
 * `area`, kept inside it. Sizes stay unless they no longer fit. */
static struct wlr_box translate_box(struct wlr_box box, struct wlr_box from, struct sh_rect area) {
    if (box.width <= 0 || box.height <= 0 || from.width <= 0 || from.height <= 0)
        return box;
    double rx = (double)(box.x - from.x) / from.width, ry = (double)(box.y - from.y) / from.height;
    box.width = fmin(box.width, area.width);
    box.height = fmin(box.height, area.height);
    box.x = area.x + (int)lround(rx * area.width);
    box.y = area.y + (int)lround(ry * area.height);
    box.x = fmax(area.x, fmin(box.x, area.x + area.width - box.width));
    box.y = fmax(area.y, fmin(box.y, area.y + area.height - box.height));
    return box;
}

/* The connected output nearest the box, or NULL when there is none. */
static struct wlr_output *nearest_output(struct sh_server *server, struct wlr_box box) {
    struct wlr_output *best = NULL;
    double best_distance = 0;
    struct sh_output *candidate;
    wl_list_for_each(candidate, &server->outputs, link) {
        struct wlr_box other;
        wlr_output_layout_get_box(server->output_layout, candidate->wlr_output, &other);
        double dx = (other.x + other.width / 2.0) - (box.x + box.width / 2.0);
        double dy = (other.y + other.height / 2.0) - (box.y + box.height / 2.0);
        double distance = dx * dx + dy * dy;
        if (!best || distance < best_distance) {
            best = candidate->wlr_output;
            best_distance = distance;
        }
    }
    return best;
}

/* Puts a floating window at `box`, or a fullscreen one over `output`, so that the
 * output the window follows is the one it was sent to. */
static void place_on_output(struct sh_toplevel *toplevel, struct wlr_output *output,
                            struct wlr_box box) {
    if (toplevel->fullscreen)
        box = fullscreen_box(toplevel, output);
    toplevel_configure_box(toplevel, box);
}

/* Moves a window from `from` (the box of the output it is on) onto `to`, keeping its workspace
 * number and, on an output that tiles, its place in the tiling. */
static void relocate_toplevel(struct sh_toplevel *toplevel, struct wlr_box from,
                              struct wlr_output *to, bool tile, bool keep_workspace) {
    struct sh_server *server = toplevel->server;
    struct sh_rect area = usable_area(server, to);
    int workspace = toplevel->workspace;
    toplevel->restore_box = translate_box(toplevel->restore_box, from, area);
    toplevel->fullscreen_restore = translate_box(toplevel->fullscreen_restore, from, area);
    struct wlr_box box = translate_box(toplevel_box(toplevel), from, area);
    bool was_tiled = toplevel->tiled;
    if (was_tiled)
        untile_toplevel(toplevel, false);
    set_toplevel_output(toplevel, to); // joins the workspace `to` shows
    if (keep_workspace)
        toplevel->workspace = workspace;
    group_follow(toplevel);
    if (was_tiled && tile && wants_tiling(toplevel, to)) {
        tile_toplevel_at(toplevel, to, NULL, false, 0, 0);
    } else if (was_tiled) {
        // It floats where it floated before, moved over above, or where its tile is when it
        // opened tiled; not by restore_toplevel, which places it by where its tile still is.
        struct wlr_box floating = toplevel->restore_box;
        if (floating.width <= 0 || floating.height <= 0) {
            floating.x = box.x;
            floating.y = box.y;
        }
        toplevel->arranged = false;
        toplevel_set_states(toplevel, false, 0);
        toplevel_configure_box(toplevel, floating);
    } else if (toplevel_mapped(toplevel)) {
        place_on_output(toplevel, to, box);
    }
}

/* Moves a window from the output it is on to `to`, onto the workspace `to` shows: a floating
 * window keeps its place relative to the usable area, a maximized or snapped one is arranged
 * there again, and a tile joins the tiling there or floats where that does not tile, as a
 * window that floated only because its workspace did not tile joins one that does. A sticky
 * window stays sticky. Focus stays where it is. */
void move_toplevel_to_output(struct sh_toplevel *toplevel, struct wlr_output *to) {
    struct sh_server *server = toplevel->server;
    struct wlr_output *from = find_output(server, toplevel->output);
    if (!from)
        from = toplevel_output(toplevel);
    if (toplevel->unmanaged || !to || !from || to == from)
        return;
    if (server->grabbed_toplevel == toplevel)
        reset_cursor_mode(server);
    struct sh_rect area = usable_area(server, from);
    ++server->reflow_held;
    relocate_toplevel(toplevel, (struct wlr_box){area.x, area.y, area.width, area.height}, to,
                      true, false);
    if (toplevel_mapped(toplevel) && wants_tiling(toplevel, to))
        tile_toplevel_at(toplevel, to, NULL, false, 0, 0);
    --server->reflow_held;
    show_workspaces(server);
    reflow_output(server, from);
    reflow_output(server, to);
    refit_fullscreen(server);
    refresh_frame(toplevel);
    wlr_log(WLR_INFO, "Window moved from %s to %s", from->name, to->name);
}

/* An output is going away: its windows move to the nearest one that is left, keeping their
 * workspace numbers, and remember where they came from so they can return with it. Without
 * another output they stay as they are, as they do when a VT switch takes every output. */
void evacuate_output(struct sh_server *server, const char *name, struct wlr_box gone,
                     bool keep_workspaces) {
    if (wlr_box_empty(&gone) || find_output(server, name))
        return;
    struct wlr_output *target = nearest_output(server, gone);
    if (!target)
        return;
    bool remember = server_settings(server)->return_windows;
    size_t count = 0;
    struct sh_toplevel *toplevel,
        **moving = malloc((size_t)wl_list_length(&server->toplevels) * sizeof(*moving));
    if (!moving)
        return;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (!toplevel->unmanaged && !strcmp(toplevel->output, name))
            moving[count++] = toplevel;
    }
    if (count == 0) {
        free(moving);
        return;
    }
    ++server->reflow_held;
    for (size_t i = 0; i < count; ++i) {
        toplevel = moving[i];
        if (remember && !toplevel->home_output[0]) {
            snprintf(toplevel->home_output, sizeof(toplevel->home_output), "%s", name);
            toplevel->home_workspace = toplevel->workspace;
            toplevel->home_tiled = toplevel->tiled;
        }
    }
    for (size_t i = 0; i < count; ++i) {
        toplevel = moving[i];
        // Hidden members of a group follow the one that shows.
        if (toplevel->group_hidden) {
            struct sh_rect area = usable_area(server, target);
            toplevel->restore_box = translate_box(toplevel->restore_box, gone, area);
            continue;
        }
        char home[64];
        int home_workspace = toplevel->home_workspace;
        bool home_tiled = toplevel->home_tiled;
        snprintf(home, sizeof(home), "%s", toplevel->home_output);
        relocate_toplevel(toplevel, gone, target, true, keep_workspaces);
        // Relocating counts as placing by hand; put the note back.
        snprintf(toplevel->home_output, sizeof(toplevel->home_output), "%s", home);
        toplevel->home_workspace = home_workspace;
        toplevel->home_tiled = home_tiled;
    }
    --server->reflow_held;
    free(moving);
    wlr_log(WLR_INFO, "Moved windows of %s to %s", name, target->name);
    show_workspaces(server);
    reflow_output(server, target);
    refit_fullscreen(server);
    refocus_if_hidden(server);
}

struct sh_evacuation {
    struct sh_server *server;
    char name[64];
    struct wlr_box box;
};

static void evacuation_run(void *data) {
    struct sh_evacuation *job = data;
    if (job->server->running)
        evacuate_output(job->server, job->name, job->box, true);
    free(job);
}

/* Called while the output is being destroyed, when the scene and the layout still hold it and
 * moving windows would touch it again: the move waits until it is gone. */
void schedule_evacuation(struct sh_server *server, struct sh_output *output) {
    struct sh_evacuation *job = calloc(1, sizeof(*job));
    if (!job)
        return;
    job->server = server;
    snprintf(job->name, sizeof(job->name), "%s", output->wlr_output->name);
    job->box = output->usable; // windows keep their place relative to the area they could use
    if (wlr_box_empty(&job->box))
        wlr_output_layout_get_box(server->output_layout, output->wlr_output, &job->box);
    if (wlr_box_empty(&job->box))
        job->box = output->previous;
    wl_event_loop_add_idle(wl_display_get_event_loop(server->wl_display), evacuation_run, job);
}

/* Windows that came from an output that is connected again go back to it, to their old
 * workspace and, when they were tiled, into its tiling. */
void return_home_windows(struct sh_server *server) {
    const struct sh_settings *settings = server_settings(server);
    bool any = false;
    ++server->reflow_held;
    struct sh_toplevel *toplevel;
    wl_list_for_each_reverse(toplevel, &server->toplevels, link) {
        if (!toplevel->home_output[0])
            continue;
        if (!settings->return_windows || toplevel->group_hidden) {
            toplevel->home_output[0] = '\0';
            continue;
        }
        struct wlr_output *home = find_output(server, toplevel->home_output);
        if (!home)
            continue; // still away
        if (!strcmp(toplevel->output, toplevel->home_output)) {
            toplevel->home_output[0] = '\0';
            continue;
        }
        struct wlr_output *from = find_output(server, toplevel->output);
        struct wlr_box from_box = {0};
        if (from) {
            struct sh_rect area = usable_area(server, from);
            from_box = (struct wlr_box){area.x, area.y, area.width, area.height};
        } else {
            from_box = toplevel_box(toplevel);
        }
        int workspace = toplevel->home_workspace;
        bool tile = toplevel->home_tiled;
        toplevel->home_output[0] = '\0';
        // relocate_toplevel keeps the workspace number the window has now.
        toplevel->workspace = workspace;
        relocate_toplevel(toplevel, from_box, home, tile, true);
        any = true;
    }
    --server->reflow_held;
    if (!any)
        return;
    show_workspaces(server);
    // The outputs the windows left lose tiles too.
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) reflow_output(server, output->wlr_output);
    refit_fullscreen(server);
    wl_list_for_each(toplevel, &server->toplevels, link) refresh_frame(toplevel);
}

/* The output an output-target words name, seen from `from`: "left" or "right" is the next
 * output that way, "next" or "prev" the one after or before it from left to right, wrapping;
 * anything else a connector name or "desc:" description. NULL when there is none, or it is
 * `from` itself. */
static struct wlr_output *resolve_output_target(struct sh_server *server, struct wlr_output *from,
                                                const char *target) {
    struct wlr_output *found = NULL;
    struct wlr_box box;
    wlr_output_layout_get_box(server->output_layout, from, &box);
    if (!strcmp(target, "left") || !strcmp(target, "right")) {
        found = wlr_output_layout_adjacent_output(
            server->output_layout, !strcmp(target, "left") ? WLR_DIRECTION_LEFT : WLR_DIRECTION_RIGHT,
            from, box.x + box.width / 2.0, box.y + box.height / 2.0);
    } else if (!strcmp(target, "next") || !strcmp(target, "prev")) {
        // The output whose left edge follows (or precedes) ours, else the far end.
        struct wlr_output *best = NULL, *edge = NULL;
        struct wlr_box best_box = {0}, edge_box = {0};
        bool forward = !strcmp(target, "next");
        struct sh_output *candidate;
        wl_list_for_each(candidate, &server->outputs, link) {
            if (candidate->wlr_output == from)
                continue;
            struct wlr_box other;
            wlr_output_layout_get_box(server->output_layout, candidate->wlr_output, &other);
            bool after = other.x > box.x || (other.x == box.x && other.y > box.y);
            bool nearer = !best || (forward ? (other.x < best_box.x ||
                                              (other.x == best_box.x && other.y < best_box.y))
                                            : (other.x > best_box.x ||
                                               (other.x == best_box.x && other.y > best_box.y)));
            if (after == forward && nearer) {
                best = candidate->wlr_output;
                best_box = other;
            }
            bool further = !edge || (forward ? (other.x < edge_box.x ||
                                               (other.x == edge_box.x && other.y < edge_box.y))
                                             : (other.x > edge_box.x ||
                                                (other.x == edge_box.x && other.y > edge_box.y)));
            if (further) {
                edge = candidate->wlr_output;
                edge_box = other;
            }
        }
        found = best ? best : edge;
    } else {
        struct sh_output *candidate;
        wl_list_for_each(candidate, &server->outputs, link) {
            if (candidate->wlr_output != from && output_key_matches(target, candidate->wlr_output)) {
                found = candidate->wlr_output;
                break;
            }
        }
    }
    return found != from ? found : NULL;
}

/* The windows a workspace exchange moves, with where each was drawn, for the glide. */
struct sh_exchange {
    struct {
        struct sh_toplevel *toplevel;
        int x, y;
    } *windows;
    size_t count, capacity;
};

static void exchange_note(struct sh_exchange *exchange, struct sh_toplevel *toplevel) {
    if (exchange->count == exchange->capacity) {
        size_t capacity = exchange->capacity ? 2 * exchange->capacity : 32;
        void *windows = realloc(exchange->windows, capacity * sizeof(*exchange->windows));
        if (!windows)
            return;
        exchange->windows = windows;
        exchange->capacity = capacity;
    }
    exchange->windows[exchange->count].toplevel = toplevel;
    exchange->windows[exchange->count].x = toplevel->scene_tree->node.x;
    exchange->windows[exchange->count++].y = toplevel->scene_tree->node.y;
}

/* Whether a window goes along with its workspace: sticky windows and the scratchpad's belong
 * to their output. */
static bool travels_with_workspace(struct sh_toplevel *toplevel) {
    return !toplevel->unmanaged && toplevel->output[0] && !toplevel->sticky &&
           !toplevel->scratchpad;
}

/* Trades what workspace `wa` of output `a` and workspace `wb` of `b` hold: their windows, moved
 * to the other output (a floating window keeps its place relative to the usable area, a tile its
 * place in the tiling, which comes along with its layout, ratio and columns), and their layout
 * state. A tile that lands on an output that does not tile floats, and a floating window that
 * lands on one that does joins the tiling. Nothing is shown or arranged here. */
static void exchange_workspace_slots(struct sh_server *server, struct wlr_output *a, int wa,
                                     struct wlr_output *b, int wb, struct sh_exchange *exchange) {
    struct sh_rect area_a = usable_area(server, a), area_b = usable_area(server, b);
    struct wlr_box box_a = {area_a.x, area_a.y, area_a.width, area_a.height};
    struct wlr_box box_b = {area_b.x, area_b.y, area_b.width, area_b.height};
    size_t first = exchange->count;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (!travels_with_workspace(toplevel))
            continue;
        bool from_a = !strcmp(toplevel->output, a->name) && toplevel->workspace == wa;
        bool from_b = !strcmp(toplevel->output, b->name) && toplevel->workspace == wb;
        if (!from_a && !from_b)
            continue;
        exchange_note(exchange, toplevel);
    }
    size_t last = exchange->count;
    sh_tiling_exchange(server->tiling, a->name, wa, b->name, wb);
    if (server_settings(server)->tiling_per_workspace) { // each workspace's tiling comes along
        bool tiles_a = workspace_tiles(server, a, wa), tiles_b = workspace_tiles(server, b, wb);
        server->output_workspaces[output_slot(server, a->name)].workspace_tiling[wa] = tiles_b;
        server->output_workspaces[output_slot(server, b->name)].workspace_tiling[wb] = tiles_a;
    }
    for (size_t i = first; i < last; ++i) {
        toplevel = exchange->windows[i].toplevel;
        bool from_a = !strcmp(toplevel->output, a->name);
        struct wlr_output *to = from_a ? b : a;
        struct wlr_box from_box = from_a ? box_a : box_b;
        struct sh_rect area = from_a ? area_b : area_a;
        toplevel->restore_box = translate_box(toplevel->restore_box, from_box, area);
        toplevel->fullscreen_restore = translate_box(toplevel->fullscreen_restore, from_box, area);
        struct wlr_box moved = translate_box(toplevel_box(toplevel), from_box, area);
        snprintf(toplevel->output, sizeof(toplevel->output), "%s", to->name);
        toplevel->workspace = from_a ? wb : wa;
        toplevel->home_output[0] = '\0';
        if (toplevel->group_hidden || toplevel->swallowed || !toplevel_mapped(toplevel))
            continue;
        if (toplevel->tiled) {
            if (!tiles_for(toplevel, to)) {
                // A window that opened tiled floats where its tile is, which must be on `to`.
                wlr_scene_node_set_position(&toplevel->scene_tree->node, moved.x, moved.y);
                untile_toplevel(toplevel, true);
            }
            continue;
        }
        place_on_output(toplevel, to, moved);
        if (wants_tiling(toplevel, to))
            tile_toplevel_at(toplevel, to, NULL, false, 0, 0);
    }
}

/* The last steps of an exchange: what is on screen, the arrangement of both outputs, and a glide
 * from where each window was drawn. Frees the exchange. */
static void finish_exchange(struct sh_server *server, struct wlr_output *a, struct wlr_output *b,
                            struct sh_exchange *exchange) {
    show_workspaces(server);
    // Floating windows glide from where they were; tiles glide in reflow_output.
    for (size_t i = 0; i < exchange->count; ++i) {
        struct sh_toplevel *toplevel = exchange->windows[i].toplevel;
        if (toplevel->tiled || !toplevel->shown || !toplevel_visible(toplevel))
            continue;
        struct wlr_scene_node *node = &toplevel->scene_tree->node;
        sh_anim_glide_kind(server->animator, &toplevel->anim, toplevel->content,
                           exchange->windows[i].x - node->x, exchange->windows[i].y - node->y,
                           SH_ANIM_MOVE);
    }
    free(exchange->windows);
    reflow_output(server, a);
    reflow_output(server, b);
    refit_fullscreen(server);
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) refresh_frame(toplevel);
    if (server->focused_toplevel) {
        if (!toplevel_visible(server->focused_toplevel)) {
            deactivate_toplevel(server);
            focus_previous(server);
        } else {
            set_active_output(server, server->focused_toplevel->output);
        }
    }
    notify_subscribers(server);
}

/* move_workspace_to_output: the focused output's workspace goes to another output, which shows
 * it; the workspace with the same number there takes its place. */
void move_workspace_to_output(struct sh_server *server, const char *target) {
    struct wlr_output *from = focused_output(server);
    struct wlr_output *to = from ? resolve_output_target(server, from, target) : NULL;
    if (!to) {
        wlr_log(WLR_INFO, "move_workspace_to_output: no output %s from %s", target,
                from ? from->name : "here");
        return;
    }
    int workspace = *output_workspace(server, from->name);
    struct sh_exchange exchange = {0};
    ++server->reflow_held;
    exchange_workspace_slots(server, from, workspace, to, workspace, &exchange);
    --server->reflow_held;
    show_workspace(server, to->name, workspace);
    finish_exchange(server, from, to, &exchange);
    wlr_log(WLR_INFO, "Workspace %d moved from %s to %s", workspace + 1, from->name, to->name);
}

/* swap_workspaces: the focused output and another trade all their workspaces, and with them
 * what they show. */
void swap_output_workspaces(struct sh_server *server, const char *target) {
    struct wlr_output *first = focused_output(server);
    struct wlr_output *second = first ? resolve_output_target(server, first, target) : NULL;
    if (!second) {
        wlr_log(WLR_INFO, "swap_workspaces: no output %s from %s", target,
                first ? first->name : "here");
        return;
    }
    struct sh_exchange exchange = {0};
    ++server->reflow_held;
    for (int workspace = 0; workspace < server_settings(server)->workspaces; ++workspace)
        exchange_workspace_slots(server, first, workspace, second, workspace, &exchange);
    --server->reflow_held;
    int a = output_slot(server, first->name), b = output_slot(server, second->name);
    int current = server->output_workspaces[a].current, previous = server->output_workspaces[a].previous;
    server->output_workspaces[a].current = server->output_workspaces[b].current;
    server->output_workspaces[a].previous = server->output_workspaces[b].previous;
    server->output_workspaces[b].current = current;
    server->output_workspaces[b].previous = previous;
    finish_exchange(server, first, second, &exchange);
    wlr_log(WLR_INFO, "Workspaces of %s and %s swapped", first->name, second->name);
}
