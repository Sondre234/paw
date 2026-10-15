/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The scrolling layout's view turning as a cube (`layout.scroll.cube`): as an arrangement moves a
 * workspace's view along its strip of columns, its tiles are drawn on the sides of a square that
 * turns a quarter, the screen as it was turning away on one side and the screen as it will be
 * turning to face the eye on the next, rather than gliding over. Like a toy, the turn only
 * draws: each tile's content is posed through the animator (sh_anim_pose), its place, size and
 * client untouched, from cube_after, after the arrangement has given each tile its place, until
 * the turn ends or anything else happens to the tile. src/cube.c does the arithmetic. */
#include "server.h"
#include "paw/cube.h"

/* The turn moves on with every frame, and with this timer between frames. */
enum { TICK_MS = 16 };

struct cube_tile {
    struct sh_toplevel *toplevel; // NULL once let go
    struct wlr_box box;           // where it rests (toplevel_box)
    /* Where its left edge lies along the strip from the left edge of the view the turn goes to,
     * as the turn starts and at its end. */
    double from, to;
    bool posed; // the animator has drawn it posed since
    struct sh_cube_span span; // as last drawn
};

struct sh_cube_turn {
    struct sh_server *server;
    char output[64]; // where it turns, and the workspace that output shows
    int workspace;
    double left, middle; // the view's left edge and its middle, in layout coordinates
    struct sh_cube from; // the square as the turn starts, in the view it goes to
    int64_t start, ticked; // now_ms(): when it started and was last moved on
    int duration;
    struct sh_curve curve;
    struct wl_event_source *timer;
    struct cube_tile tiles[CUBE_TILES];
    int count;
};

/* How long a turn takes now, 0 when it does not turn. */
static int turn_duration(struct sh_server *server) {
    const struct sh_settings *settings = server_settings(server);
    if (!settings->scroll_cube || !settings->animations)
        return 0;
    double speed = settings->animation_speed > 0 ? settings->animation_speed : 1;
    return (int)lround(settings->animation_styles[SH_ANIM_CUBE].duration / speed);
}

/* The tiles of `output`'s workspace a turn draws: shown there, and not fullscreen. */
static bool on_workspace(struct sh_toplevel *toplevel, struct wlr_output *output, int workspace) {
    return toplevel->tiled && toplevel->workspace == workspace && toplevel->content &&
           !toplevel->unmanaged && toplevel_mapped(toplevel) && toplevel->shown &&
           toplevel_visible(toplevel) && tiled_output(toplevel) == output;
}

static struct cube_tile *find_tile(struct sh_cube_turn *turn, struct sh_toplevel *toplevel) {
    for (int i = 0; turn && i < turn->count; ++i)
        if (turn->tiles[i].toplevel == toplevel)
            return &turn->tiles[i];
    return NULL;
}

/* A stretch of a tile's pose changed: its frame and rounded clips follow (lift_box). */
static void pose_stretched(void *data) {
    struct cube_tile *tile = data;
    if (tile->toplevel)
        refresh_frame(tile->toplevel);
}

/* The tile back at rest and out of the turn: hidden again if it rests past its monitor's edge,
 * and its frame drawn as it is, unless it is going (`quiet`), which draws it itself. */
static void let_go(struct cube_tile *tile, bool quiet) {
    struct sh_toplevel *toplevel = tile->toplevel;
    if (!toplevel)
        return;
    tile->toplevel = NULL; // first: the pose's end asks for no frame
    sh_anim_unpose(&toplevel->anim);
    if (quiet)
        return;
    bound_tile(toplevel);
    refresh_frame(toplevel);
}

/* Whether a tile still turns: as it was placed, on its monitor's workspace, and drawn by nothing
 * but the turn. */
static bool still_turns(struct sh_cube_turn *turn, struct cube_tile *tile) {
    struct sh_toplevel *toplevel = tile->toplevel;
    struct wlr_output *output = find_output(turn->server, turn->output);
    if (!output || !on_workspace(toplevel, output, turn->workspace) || toplevel->fullscreen ||
        turn->server->grabbed_toplevel == toplevel)
        return false;
    struct wlr_box box = toplevel_box(toplevel);
    return wlr_box_equal(&box, &tile->box) && (!tile->posed || sh_anim_posed(&toplevel->anim));
}

static double progress(const struct sh_cube_turn *turn, int64_t now) {
    double t = turn->duration > 0 ? (double)(now - turn->start) / turn->duration : 1;
    return sh_curve_eval(&turn->curve, t);
}

/* Each tile where the square, `e` of the way to rest, has it: the span its frame covers along the
 * strip drawn across the sides it lies on, its height scaled about the view's middle, fainter as
 * it turns away; one wholly on sides turned away is drawn faded out, past the view's edges. Its
 * content is drawn meanwhile, as a gliding tile's is, so that lookups find it where it rests. */
static void draw(struct sh_cube_turn *turn, double e) {
    struct sh_server *server = turn->server;
    struct sh_cube cube = sh_cube_at(&turn->from, e);
    int border = server_settings(server)->border_width;
    for (int i = 0; i < turn->count; ++i) {
        struct cube_tile *tile = &turn->tiles[i];
        struct sh_toplevel *toplevel = tile->toplevel;
        if (!toplevel)
            continue;
        if (!still_turns(turn, tile)) {
            let_go(tile, false);
            continue;
        }
        double at = tile->from + (tile->to - tile->from) * e;
        int width = tile->box.width + 2 * border;
        sh_cube_span(&cube, at - border, at - border + width, &tile->span);
        const struct sh_cube_span *span = &tile->span;
        // Its frame's left edge, `border` left of the window's corner, where the span begins.
        struct sh_pose pose = {
            .dx = turn->left + span->left + border - tile->box.x,
            .sx = fmax(1e-3, (span->right - span->left) / width),
            .sy = span->scale,
            .ax = -border,
            .ay = turn->middle - tile->box.y,
            .fade = span->shown ? span->fade : 1,
        };
        if (!sh_anim_pose(server->animator, &toplevel->anim, toplevel->content, &pose,
                          pose_stretched, tile)) {
            let_go(tile, false); // something else draws it: it glides as any tile does
            continue;
        }
        tile->posed = true;
        wlr_scene_node_set_enabled(&toplevel->content->node, true); // hidden at rest past an edge
        window_cut(toplevel); // the cut at its monitor's edges follows where it is drawn
    }
}

static int timer_fired(void *data) {
    struct sh_server *server = data;
    cube_tick(server);
    if (server->cube)
        wl_event_source_timer_update(server->cube->timer, TICK_MS);
    return 0;
}

/* Moves the turn on to now; at its end, every tile back at rest. */
void cube_tick(struct sh_server *server) {
    struct sh_cube_turn *turn = server->cube;
    if (!turn)
        return;
    int64_t now = now_ms();
    if (turn->ticked && now == turn->ticked)
        return; // another monitor's frame in the same millisecond
    turn->ticked = now;
    if (server->locked || server->overview.open || now - turn->start >= turn->duration ||
        !turn_duration(server) || !find_output(server, turn->output) ||
        *output_workspace(server, turn->output) != turn->workspace) {
        cube_end(server);
        return;
    }
    draw(turn, progress(turn, now));
    int turning = 0;
    for (int i = 0; i < turn->count; ++i)
        turning += turn->tiles[i].toplevel != NULL;
    if (!turning)
        cube_end(server);
}

/* Ends the turn under way, if any: every tile back at rest where it is placed. */
void cube_end(struct sh_server *server) {
    struct sh_cube_turn *turn = server->cube;
    if (!turn)
        return;
    server->cube = NULL;
    for (int i = 0; i < turn->count; ++i)
        let_go(&turn->tiles[i], !server->running);
    if (turn->timer)
        wl_event_source_remove(turn->timer);
    free(turn);
}

/* A window going (unmap_toplevel): out of the turn at once, drawn where it is until then. */
void cube_forget(struct sh_toplevel *toplevel) {
    struct cube_tile *tile = find_tile(toplevel->server->cube, toplevel);
    if (tile)
        let_go(tile, true);
}

void cube_before(struct sh_server *server, struct wlr_output *output, int workspace,
                 struct sh_cube_before *before) {
    before->count = 0;
    before->full = false;
    before->scrolls =
        sh_tiling_scroll_view(server->tiling, output->name, workspace, &before->view);
    if (!before->scrolls || !turn_duration(server))
        return;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (!on_workspace(toplevel, output, workspace))
            continue;
        if (before->count == CUBE_TILES) {
            before->full = true;
            return;
        }
        // Drawn by nothing but a turn, which this arrangement takes on from where it is.
        struct sh_anim *anim = &toplevel->anim;
        bool free = !anim->animator && !anim->lifted && !toplevel->fullscreen &&
                    (!sh_anim_posed(anim) || find_tile(server->cube, toplevel));
        before->tiles[before->count++] = (struct sh_cube_tile_before){
            .toplevel = toplevel, .box = toplevel_box(toplevel), .free = free};
    }
}

/* Whether the tiles placed are those `before` found, each free to turn, at the same size and
 * height: the view moved, or columns along the strip, and nothing else. Writes where each rests.
 * Every tile the layout holds is one of them: one not drawn yet, as a window that opens is
 * before it has drawn, has its own way in. */
static bool only_moved(struct sh_server *server, struct wlr_output *output, int workspace,
                       const struct sh_cube_before *before, struct wlr_box *boxes) {
    if (before->full || sh_tiling_count(server->tiling, output->name, workspace) != before->count)
        return false;
    for (int i = 0; i < before->count; ++i) {
        const struct sh_cube_tile_before *was = &before->tiles[i];
        if (!was->free || !on_workspace(was->toplevel, output, workspace))
            return false;
        boxes[i] = toplevel_box(was->toplevel);
        if (boxes[i].width != was->box.width || boxes[i].height != was->box.height ||
            boxes[i].y != was->box.y)
            return false;
    }
    return true;
}

void cube_after(struct sh_server *server, struct wlr_output *output, int workspace,
                struct sh_rect area, int gap, const struct sh_cube_before *before) {
    struct sh_cube_turn *turn = server->cube;
    bool here = turn && turn->workspace == workspace && !strcmp(turn->output, output->name);
    int duration = turn_duration(server), view = 0;
    if (!duration || !before->scrolls || server->locked || server->overview.open ||
        server->toys || *output_workspace(server, output->name) != workspace ||
        !sh_tiling_scroll_view(server->tiling, output->name, workspace, &view)) {
        if (here)
            cube_end(server);
        return;
    }
    struct wlr_box boxes[CUBE_TILES];
    bool moved = false;
    if (!only_moved(server, output, workspace, before, boxes)) {
        if (here)
            cube_end(server); // what else happens draws the tiles from where they rest
        return;
    }
    for (int i = 0; i < before->count; ++i)
        moved = moved || boxes[i].x != before->tiles[i].box.x;
    int distance = view - before->view;
    if (!moved || (!distance && !here))
        return; // a turn under way goes on as it was; columns moving alone glide
    // The view the square's sides are as wide as, between the outer gaps.
    double left = area.x + gap / 2.0, width = area.width - gap;
    int64_t now = now_ms();
    double e = 0;
    struct sh_cube square = {width, 0, 0};
    if (here && turn->left == left && turn->from.width == width) {
        e = progress(turn, now);
        square = sh_cube_at(&turn->from, e);
    } else {
        cube_end(server);
        turn = calloc(1, sizeof(*turn));
        if (!turn)
            return;
        turn->server = server;
        snprintf(turn->output, sizeof(turn->output), "%s", output->name);
        turn->workspace = workspace;
        turn->timer = wl_event_loop_add_timer(wl_display_get_event_loop(server->wl_display),
                                              timer_fired, server);
        if (!turn->timer) {
            free(turn);
            return;
        }
        server->cube = turn;
        here = false;
    }
    // Each tile from where it is drawn now along the strip, in the view the turn goes to.
    struct cube_tile tiles[CUBE_TILES];
    for (int i = 0; i < before->count; ++i) {
        const struct sh_cube_tile_before *was = &before->tiles[i];
        struct cube_tile *old = here ? find_tile(turn, was->toplevel) : NULL;
        double from = old ? old->from + (old->to - old->from) * e : was->box.x - left;
        tiles[i] = (struct cube_tile){.toplevel = was->toplevel,
                                      .box = boxes[i],
                                      .from = from - distance,
                                      .to = boxes[i].x - left};
    }
    // The tiles given up (that this arrangement placed nowhere) rest where they are.
    for (int i = 0; here && i < turn->count; ++i) {
        bool kept = false;
        for (int j = 0; j < before->count && !kept; ++j)
            kept = turn->tiles[i].toplevel == tiles[j].toplevel;
        if (!kept)
            let_go(&turn->tiles[i], false);
    }
    memcpy(turn->tiles, tiles, before->count * sizeof(*tiles));
    turn->count = before->count;
    turn->from = sh_cube_move(square, distance);
    turn->left = left;
    turn->middle = area.y + area.height / 2.0;
    turn->start = turn->ticked = now;
    turn->duration = duration;
    turn->curve = server_settings(server)->animation_styles[SH_ANIM_CUBE].curve;
    // The arrangement set each tile gliding from where it was: it turns instead, from there.
    for (int i = 0; i < turn->count; ++i)
        if (turn->tiles[i].toplevel->anim.animator)
            sh_anim_finish(&turn->tiles[i].toplevel->anim);
    draw(turn, 0);
    wlr_log(WLR_DEBUG, "Cube on %s, workspace %d: the view moves %d with %d tiles", turn->output,
            workspace + 1, distance, turn->count);
    wl_event_source_timer_update(turn->timer, TICK_MS);
}

/* `get cube`: a `cube` line with the monitor and workspace that turn (or - and 0), how long the
 * turn has gone on and lasts (milliseconds), and the square's angle (thousandths of a radian) and
 * start; a `tile` line for each tile turning: its number, its title, whether it is drawn posed,
 * shown, the span it is drawn over along the view (x and width, from the view's left edge), its
 * height's scale and its fade (thousandths). */
void describe_cube(struct sh_server *server, int fd, const char *arguments) {
    control_reply(fd, "ok\n");
    struct sh_cube_turn *turn = server->cube;
    if (!turn) {
        control_reply(fd, "cube\t-\t0\t0\t0\t0\t0\n");
        return;
    }
    int64_t now = now_ms();
    struct sh_cube square = sh_cube_at(&turn->from, progress(turn, now));
    char line[768];
    snprintf(line, sizeof(line), "cube\t%s\t%d\t%ld\t%d\t%ld\t%ld\n", turn->output,
             turn->workspace + 1, (long)(now - turn->start), turn->duration,
             lround(1000 * square.angle), lround(square.start));
    control_reply(fd, line);
    for (int i = 0; i < turn->count; ++i) {
        const struct cube_tile *tile = &turn->tiles[i];
        if (!tile->toplevel)
            continue;
        char title[512];
        copy_field(title, sizeof(title), toplevel_title(tile->toplevel));
        const struct sh_cube_span *span = &tile->span;
        snprintf(line, sizeof(line), "tile\t%u\t%s\t%d\t%d\t%ld\t%ld\t%ld\t%ld\n",
                 tile->toplevel->id, title, sh_anim_posed(&tile->toplevel->anim), span->shown,
                 lround(span->left), lround(span->right - span->left), lround(1000 * span->scale),
                 lround(1000 * span->fade));
        control_reply(fd, line);
    }
}
