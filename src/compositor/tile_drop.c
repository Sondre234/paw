/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A tile moved with the pointer: it stays in a tiling, standing for where a drop would put it,
 * so that the other tiles keep its place open and glide aside to open another where the pointer
 * rests on another tile, a moment later, or close up as it leaves the tiling; the snap preview
 * shows the place. Dropped, it tiles in the place it holds; otherwise it leaves the tiling. The
 * scrolling layout keeps no place: its tile leaves the tiling as it is lifted. */
#include "server.h"

/* How long the pointer rests on another place before the tiles open it: long enough that
 * crossing tiles on the way moves nothing, and that clients are not asked for a new size at
 * every pointer motion. */
#define DROP_DWELL_MS 150

/* struct sh_drop_place (server.h): where the pointer would drop the grabbed window, into the
 * tiling of `workspace` on `output` at `drop`, or out of every tiling without `tiles`. */

static bool same_place(const struct sh_drop_place *a, const struct sh_drop_place *b) {
    return a->tiles == b->tiles &&
           (!a->tiles || (a->output == b->output && a->workspace == b->workspace &&
                          a->drop.split == b->drop.split && a->drop.first == b->drop.first));
}

/* The tiling the window is in while it is moved, NULL for none. */
static struct wlr_output *held_in(struct sh_toplevel *toplevel, int *workspace) {
    *workspace = sh_tiling_workspace(toplevel->server->tiling, toplevel);
    return toplevel->tiled ? NULL : tiled_output(toplevel);
}

/* Where a drop with the pointer would put the grabbed window; false when that is where it is. */
static bool place_at_pointer(struct sh_server *server, struct sh_drop_place *place) {
    struct sh_toplevel *toplevel = server->grabbed_toplevel;
    struct wlr_output *output = pointer_output(server);
    int held_workspace;
    struct wlr_output *held = held_in(toplevel, &held_workspace);
    *place = (struct sh_drop_place){0};
    // Into a tiling where a drop tiles it, but where it would snap (the top edge, with
    // windows.snap.maximize_tiles).
    if (!output || server->snap.zone != SH_NONE || !drop_tiles(server, output))
        return held != NULL;
    int workspace = *output_workspace(server, output->name);
    if (sh_tiling_layout(server->tiling, output->name, workspace) == SH_LAYOUT_SCROLL)
        return held != NULL;
    bool here = held == output && held_workspace == workspace;
    int gap;
    struct sh_rect area = tiling_area(server, output, workspace, here ? -1 : 0, &gap);
    *place = (struct sh_drop_place){true, output, workspace, {0}};
    return sh_tiling_drop_target(server->tiling, toplevel, output->name, workspace,
                                 server->cursor->x, server->cursor->y, area, &place->drop);
}

/* Moves the grabbed window to `place`, and the tiles of the tilings it leaves and joins glide to
 * their new places. */
static void move_to(struct sh_server *server, const struct sh_drop_place *place) {
    struct sh_toplevel *toplevel = server->grabbed_toplevel;
    int held_workspace;
    struct wlr_output *held = held_in(toplevel, &held_workspace);
    server->tile_drop.has_slot = false;
    if (place->tiles)
        sh_tiling_drop(server->tiling, toplevel, place->output->name, place->workspace,
                       &place->drop);
    else
        sh_tiling_remove(server->tiling, toplevel);
    if (held && held != place->output)
        reflow_output(server, held);
    if (place->tiles)
        reflow_output(server, place->output);
    snap_follow(server); // the preview follows its place
}

static int dwell_done(void *data) {
    struct sh_server *server = data;
    server->tile_drop.pending = false;
    struct sh_drop_place place;
    if (server->grabbed_toplevel && server->cursor_mode == SH_CURSOR_MOVE &&
        place_at_pointer(server, &place))
        move_to(server, &place);
    return 0;
}

/* A tile is taken to be moved: it leaves the tiling, but for its place there. */
void tile_drop_lift(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    if (!toplevel->tiled)
        return;
    server->tile_drop.has_slot = false;
    if (toplevel_layout(toplevel) == SH_LAYOUT_SCROLL) {
        untile_toplevel(toplevel, false);
        return;
    }
    // As untile_toplevel does, but in the tree still: nothing else moves.
    toplevel->tiled = false;
    bound_tile(toplevel);
    restack_toplevel(toplevel);
    toplevel->arranged = true;
    toplevel->arrangement = SH_NONE;
    window_objects_changed(server);
}

/* Follows a move with the pointer, after snap_follow: once the pointer has rested a moment on
 * another place for the window, the window moves there. */
void tile_drop_follow(struct sh_server *server) {
    struct sh_toplevel *toplevel = server->grabbed_toplevel;
    if (!toplevel || server->cursor_mode != SH_CURSOR_MOVE || server->grab_fullscreen)
        return;
    int held_workspace;
    struct wlr_output *held = held_in(toplevel, &held_workspace);
    if (held && !server->tile_drop.has_slot) {
        // Its place, as the tiling has it, for the preview; the other tiles stay as they are.
        reflow_output(server, held);
        snap_follow(server);
    }
    struct sh_drop_place place;
    if (!place_at_pointer(server, &place)) {
        server->tile_drop.pending = false;
        if (server->tile_drop.timer)
            wl_event_source_timer_update(server->tile_drop.timer, 0);
        return;
    }
    if (server->tile_drop.pending && same_place(&server->tile_drop.place, &place))
        return;
    if (!server->tile_drop.timer)
        server->tile_drop.timer = wl_event_loop_add_timer(
            wl_display_get_event_loop(server->wl_display), dwell_done, server);
    if (!server->tile_drop.timer)
        return;
    server->tile_drop.place = place;
    server->tile_drop.pending = true;
    wl_event_source_timer_update(server->tile_drop.timer, DROP_DWELL_MS);
}

/* The window is dropped: at once where the pointer is, out of a tiling the drop does not put it
 * in, for finish_grab to tile or place it there. */
void tile_drop_release(struct sh_server *server) {
    struct sh_toplevel *toplevel = server->grabbed_toplevel;
    server->tile_drop.pending = false;
    if (server->tile_drop.timer)
        wl_event_source_timer_update(server->tile_drop.timer, 0);
    if (!toplevel || server->cursor_mode != SH_CURSOR_MOVE)
        return;
    struct sh_drop_place place;
    if (place_at_pointer(server, &place))
        move_to(server, &place);
    int held_workspace;
    struct wlr_output *held = held_in(toplevel, &held_workspace);
    struct wlr_output *output = pointer_output(server);
    bool snapping = server->snap.zone != SH_NONE;
    if (held && (snapping || held != output || !drop_tiles(server, output) ||
                 held_workspace != *output_workspace(server, output->name))) {
        sh_tiling_remove(server->tiling, toplevel);
        reflow_output(server, held);
    }
}

/* The move ends: a window still holding a place in a tiling, as when the move was cut short,
 * leaves it, and the tiles close up. */
void tile_drop_end(struct sh_server *server) {
    struct sh_toplevel *toplevel = server->grabbed_toplevel;
    server->tile_drop.pending = false;
    server->tile_drop.has_slot = false;
    if (server->tile_drop.timer)
        wl_event_source_timer_update(server->tile_drop.timer, 0);
    if (!toplevel || toplevel->tiled || !sh_tiling_output(server->tiling, toplevel))
        return;
    struct wlr_output *held = tiled_output(toplevel);
    sh_tiling_remove(server->tiling, toplevel);
    if (held)
        reflow_output(server, held);
}

/* The tiling places a window: the one moved with the pointer is not configured, its place is
 * noted for the preview. Returns whether it was that one. */
bool tile_drop_place(struct sh_toplevel *toplevel, struct sh_rect rect) {
    struct sh_server *server = toplevel->server;
    if (toplevel != server->grabbed_toplevel || toplevel->tiled)
        return false;
    server->tile_drop.slot = rect;
    server->tile_drop.has_slot = true;
    return true;
}

/* Where the window moved with the pointer would go into a tiling if dropped now, and the
 * tiling's output. */
bool tile_drop_slot(struct sh_server *server, struct sh_rect *slot, struct wlr_output **output) {
    struct sh_toplevel *toplevel = server->grabbed_toplevel;
    if (!toplevel || toplevel->tiled || server->cursor_mode != SH_CURSOR_MOVE ||
        !server->tile_drop.has_slot || !(*output = tiled_output(toplevel)))
        return false;
    *slot = server->tile_drop.slot;
    return true;
}

void tile_drop_finish(struct sh_server *server) {
    if (server->tile_drop.timer)
        wl_event_source_remove(server->tile_drop.timer);
    server->tile_drop.timer = NULL;
}
