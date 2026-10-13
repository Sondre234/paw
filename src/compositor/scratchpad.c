/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* Sway's scratchpad. A window put there floats and hides, listed in the taskbar as minimized.
 * scratchpad_show brings one to the middle of the focused output's current workspace, where it
 * stays in the scratchpad (and hides again on the next scratchpad_show) until it is moved to a
 * workspace or tiled. It drops down as a quake console does: in from behind the bar, and back
 * out behind it as it hides. */
#include "server.h"
#include <stdlib.h>
#if WLR_HAS_GLES2_RENDERER
#include <wlr/render/gles2.h>
#endif

/* The drop-down. A window shown slides in from the edge of the usable area along the bar
 * (src/dropdown.c picks the edge and the way), and a window hidden slides back out there: its
 * `content` moves with sh_anim_drop_in and sh_anim_drop_out while the window itself is where it
 * rests, so input, focus and `get windows` have its place at once. Past the bar's edge nothing of
 * it is drawn: the clips frame.c gives its trees (window_clip) have what lies past the cut taken
 * off, and the border's rounded rect, which no clip reaches, is cut by its size; each frame the
 * animation draws, the cut follows the window (drop_track). A window hidden is minimized at once,
 * focus moving on, but its node stays shown while it slides out, absent for lookups, and hides
 * when the slide ends. Only the GLES2 renderer with the rounded-corners patch clips; elsewhere
 * the window slides uncut, behind the bar, and no further than its monitor's edge where another
 * monitor would show it. */

/* Far enough past anything a window draws: a clip box that holds none of it. */
enum { FAR = 1 << 20 };

/* Whether clips are drawn: PAW_TEST_DROP_CLIP makes tests on pixman cut as GLES2 would. */
static bool can_clip(struct sh_server *server) {
#ifdef PAW_ROUNDED_CORNERS
    if (getenv("PAW_TEST_DROP_CLIP"))
        return true;
#if WLR_HAS_GLES2_RENDERER
    return wlr_renderer_is_gles2(server->renderer);
#endif
#endif
    return false;
}

static bool cutting(struct sh_toplevel *toplevel) {
    return toplevel->drop.running && toplevel->drop.plan.clip;
}

#ifdef PAW_ROUNDED_CORNERS
static struct wlr_scene_tree *clip_tree(struct sh_toplevel *toplevel, enum sh_clip_tree which) {
    switch (which) {
    case SH_CLIP_SHADOW:
        return toplevel->shadow;
    case SH_CLIP_GRADIENT:
        return toplevel->gradient;
    default:
        return toplevel->content;
    }
}

/* The clip frame.c asked for, with what lies past the cut taken off while a drop runs. */
static void apply_clip(struct sh_toplevel *toplevel, enum sh_clip_tree which) {
    struct wlr_scene_tree *tree = clip_tree(toplevel, which);
    const struct sh_clip_wish *wish = &toplevel->clips[which];
    if (!tree)
        return;
    if (!cutting(toplevel)) {
        wlr_scene_tree_set_rounded_clip(tree, wish->set ? &wish->box : NULL, wish->radius);
        if (which == SH_CLIP_CONTENT)
            toplevel->content_clip = *wish;
        return;
    }
    int x, y;
    wlr_scene_node_coords(&tree->node, &x, &y);
    struct sh_drop_box box = {x - FAR, y - FAR, 2 * FAR, 2 * FAR}; // no clip of its own: all
    if (wish->set)
        box = (struct sh_drop_box){x + wish->box.x, y + wish->box.y, wish->box.width,
                                   wish->box.height};
    struct sh_drop_box whole = box;
    struct wlr_box clip = {-FAR, -FAR, 1, 1}; // all of it past the cut
    int radius = wish->radius;
    if (sh_drop_cut(&toplevel->drop.plan, &box)) {
        clip = (struct wlr_box){box.x - x, box.y - y, box.width, box.height};
        // Rounded corners along a cut no bar hides would show there: crossing it, it is square.
        if (toplevel->drop.plan.square && memcmp(&box, &whole, sizeof(box)))
            radius = 0;
    }
    wlr_scene_tree_set_rounded_clip(tree, &clip, radius);
    if (which == SH_CLIP_CONTENT)
        toplevel->content_clip = (struct sh_clip_wish){true, clip, radius};
}

/* The border's rounded rect (`frame_hole`), cut short of the cut by its own size while a drop
 * runs: a size frame.c gave it since the drop last cut it is where it rests. Crossing a cut no bar
 * hides, it would draw a line along it and round its corners there, so it is left out. */
static void cut_ring(struct sh_toplevel *toplevel) {
    struct sh_drop *drop = &toplevel->drop;
    struct wlr_scene_rect *ring = toplevel->frame_hole > 0 ? toplevel->border[0] : NULL;
    if (!ring || !toplevel->content) {
        drop->ring = false;
        return;
    }
    struct wlr_box now = {ring->node.x, ring->node.y, ring->width, ring->height};
    if (!drop->ring || !wlr_box_equal(&now, &drop->ring_set))
        drop->ring_rest = now;
    drop->ring = cutting(toplevel);
    struct wlr_box set = drop->ring_rest;
    if (drop->ring) {
        int x, y;
        wlr_scene_node_coords(&toplevel->content->node, &x, &y);
        struct sh_drop_box box = {x + set.x, y + set.y, set.width, set.height}, whole = box;
        set = (struct wlr_box){set.x, set.y, 0, 0};
        if (sh_drop_cut(&drop->plan, &box) &&
            !(drop->plan.square && memcmp(&box, &whole, sizeof(box))))
            set = (struct wlr_box){box.x - x, box.y - y, box.width, box.height};
    }
    drop->ring_set = set;
    wlr_scene_node_set_position(&ring->node, set.x, set.y);
    wlr_scene_rect_set_size(ring, set.width, set.height);
}
#endif

/* The cut where the window is drawn now, or the clips as frame.c asked for them once the drop is
 * over. */
static void follow_cut(struct sh_toplevel *toplevel) {
#ifdef PAW_ROUNDED_CORNERS
    for (int i = 0; i < SH_CLIP_TREES; ++i)
        apply_clip(toplevel, i);
    cut_ring(toplevel);
#endif
}

/* Every clip frame.c gives a window's trees, kept for a drop's cut to be laid over it. */
void window_clip(struct sh_toplevel *toplevel, enum sh_clip_tree which, const struct wlr_box *box,
                 int radius) {
    struct sh_clip_wish *wish = &toplevel->clips[which];
    wish->set = box != NULL;
    wish->box = box ? *box : (struct wlr_box){0};
    wish->radius = radius;
#ifdef PAW_ROUNDED_CORNERS
    apply_clip(toplevel, which);
#endif
}

/* After frame.c laid the border out again: cut the rounded one while a drop runs. */
void drop_refresh(struct sh_toplevel *toplevel) {
#ifdef PAW_ROUNDED_CORNERS
    if (cutting(toplevel) || toplevel->drop.ring)
        cut_ring(toplevel);
#endif
}

/* Asks for the next frame on the window's output: a window wholly past the cut or the monitor's
 * edge damages nothing as it moves, which would leave the animation waiting for a frame. */
static void next_frame(struct sh_toplevel *toplevel) {
    struct wlr_output *output = find_output(toplevel->server, toplevel->output);
    if (output)
        wlr_output_schedule_frame(output);
}

/* The animation's frames: the cut follows the window; and once it is over, a window that slid
 * out of sight hides, back where it rests. */
static void drop_track(void *data, bool done) {
    struct sh_toplevel *toplevel = data;
    struct sh_drop *drop = &toplevel->drop;
    if (done) {
        drop->running = false;
        if (drop->out) {
            toplevel->anim.base_x = toplevel->anim.base_y = 0;
            if (toplevel->content)
                wlr_scene_node_set_position(&toplevel->content->node, 0, 0);
            // Unless it is shown again meanwhile, or lent to a peek at it, which hides it after.
            if (toplevel->scene_tree && !toplevel_visible(toplevel) && !toplevel->peek_lent)
                wlr_scene_node_set_enabled(&toplevel->scene_tree->node, false);
        }
    }
    if (done || cutting(toplevel))
        follow_cut(toplevel);
    if (!done)
        next_frame(toplevel);
}

/* Whether another output than `output` takes in any of `box`. */
static bool other_output_in(struct sh_server *server, struct wlr_output *output,
                            struct sh_drop_box box) {
    struct wlr_box area = {box.x, box.y, box.width, box.height};
    struct sh_output *other;
    wl_list_for_each(other, &server->outputs, link) {
        struct wlr_box its, both;
        wlr_output_layout_get_box(server->output_layout, other->wlr_output, &its);
        if (other->wlr_output != output && wlr_box_intersection(&both, &area, &its))
            return true;
    }
    return false;
}

/* How the window drops on its output: the way out of sight from its place, and the cut. */
static bool plan_drop(struct sh_toplevel *toplevel, struct sh_drop_plan *plan) {
    struct sh_server *server = toplevel->server;
    struct wlr_output *output = find_output(server, toplevel->output);
    if (!output)
        return false;
    struct wlr_box monitor;
    wlr_output_layout_get_box(server->output_layout, output, &monitor);
    struct sh_rect area = usable_area(server, output);
    struct wlr_box place = toplevel_box(toplevel);
    int b = server_settings(server)->border_width;
    struct sh_drop_box out = {monitor.x, monitor.y, monitor.width, monitor.height};
    struct sh_drop_box usable = {area.x, area.y, area.width, area.height};
    struct sh_drop_box frame = {place.x - b, place.y - b, place.width + 2 * b,
                                place.height + 2 * b};
    int corner = toplevel->corner_radius > 0 ? toplevel->corner_radius + b : 0;
    bool clips = can_clip(server);
    *plan = sh_drop_plan(out, usable, frame, corner, false, clips);
    struct sh_drop_box swept = sh_drop_swept(plan, out, frame);
    if (swept.width > 0 && swept.height > 0 && other_output_in(server, output, swept))
        *plan = sh_drop_plan(out, usable, frame, corner, true, clips);
    return !wlr_box_empty(&monitor);
}

/* Slides the window in, or out once it is minimized; false when nothing slides. A drop under way
 * turns back from where it is drawn while the window rests where it did, else lands first. */
static bool drop(struct sh_toplevel *toplevel, bool out) {
    struct sh_server *server = toplevel->server;
    struct sh_drop *drop = &toplevel->drop;
    struct sh_anim *anim = &toplevel->anim;
    bool turning = drop->running && drop->place_x == toplevel->scene_tree->node.x &&
                   drop->place_y == toplevel->scene_tree->node.y;
    if (drop->running && !turning)
        sh_anim_finish(anim); // somewhere else now: the last one ends as if done
    anim->track = NULL;       // turning back, this drop takes over from the last
    struct sh_drop_plan plan = {0};
    bool started = false;
    if (plan_drop(toplevel, &plan)) {
        // Out straight from where it is drawn, which a tile hidden is, away from its floating
        // place.
        bool across = plan.edge == SH_DROP_TOP || plan.edge == SH_DROP_BOTTOM;
        if (out && across)
            plan.dx = toplevel->content->node.x;
        else if (out)
            plan.dy = toplevel->content->node.y;
        started =
            out ? sh_anim_drop_out(server->animator, anim, toplevel->content, plan.dx, plan.dy)
                : sh_anim_drop_in(server->animator, anim, toplevel->content, plan.dx, plan.dy);
    } else if (turning) {
        anim->base_x = anim->base_y = 0; // what slid out lands where the window rests
        sh_anim_finish(anim);
    }
    drop->running = started;
    drop->out = out;
    drop->plan = plan;
    drop->place_x = toplevel->scene_tree->node.x;
    drop->place_y = toplevel->scene_tree->node.y;
    if (started) {
        anim->track = drop_track;
        anim->track_data = toplevel;
        next_frame(toplevel);
    }
    follow_cut(toplevel);
    return started;
}

/* A scratchpad window come onto the screen drops in, unless it has yet to show at all. */
void scratchpad_drop_in(struct sh_toplevel *toplevel) {
    if (toplevel->shown && toplevel->server->running && toplevel_mapped(toplevel) &&
        toplevel_visible(toplevel))
        drop(toplevel, false);
}

bool scratchpad_enabled(struct sh_server *server) {
    if (server_settings(server)->scratchpad)
        return true;
    if (!server->scratchpad_off_logged)
        wlr_log(WLR_INFO, "Scratchpad actions ignored: features.scratchpad is false");
    server->scratchpad_off_logged = true;
    return false;
}

/* Moves a scratchpad window onto the current workspace of `output`, centred in its floating
 * area and no larger than it. */
void center_scratchpad(struct sh_toplevel *toplevel, struct wlr_output *output) {
    struct sh_server *server = toplevel->server;
    if (!output)
        return;
    snprintf(toplevel->output, sizeof(toplevel->output), "%s", output->name);
    toplevel->workspace = *output_workspace(server, output->name);
    struct sh_rect area = floating_area(server, output);
    struct wlr_box box = toplevel_box(toplevel);
    box.width = box.width > area.width ? area.width : box.width;
    box.height = box.height > area.height ? area.height : box.height;
    box.x = area.x + (area.width - box.width) / 2;
    box.y = area.y + (area.height - box.height) / 2;
    toplevel_configure_box(toplevel, box);
}

/* Floats the window at its own size and hides it in the scratchpad, behind the others. Seen
 * going, it slides out the way a window shown there comes in, from where it is drawn: a tile, or
 * a window maximized or fullscreen, does not jump to its floating place first. */
void hide_in_scratchpad(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    bool seen = toplevel->shown && server->running && toplevel_mapped(toplevel) &&
                toplevel_visible(toplevel);
    int drawn_x = 0, drawn_y = 0;
    if (seen) {
        drawn_x = toplevel->scene_tree->node.x + toplevel->content->node.x;
        drawn_y = toplevel->scene_tree->node.y + toplevel->content->node.y;
    }
    if (server->grabbed_toplevel == toplevel)
        reset_cursor_mode(server);
    if (toplevel->fullscreen)
        set_fullscreen(toplevel, false);
    if (toplevel->sticky)
        set_sticky(toplevel, false, false); // a hidden window shows on no workspace
    toplevel->floating = true;
    toplevel->placed = false;
    if (toplevel->tiled)
        untile_toplevel(toplevel, true);
    else
        restore_toplevel(toplevel); // snapped or maximized
    toplevel->scratchpad = true;
    toplevel->scratchpad_order = ++server->scratchpad_serial;
    minimize_toplevel(toplevel);
    if (seen) {
        struct wlr_scene_node *place = &toplevel->scene_tree->node;
        wlr_scene_node_set_position(&toplevel->content->node, drawn_x - place->x,
                                    drawn_y - place->y);
        if (drop(toplevel, true)) {
            // Drawn over the window that took the focus until it is out of sight.
            wlr_scene_node_set_enabled(place, true);
            wlr_scene_node_raise_to_top(place);
        } else {
            wlr_scene_node_set_position(&toplevel->content->node, 0, 0);
        }
    }
    notify_subscribers(server); // its workspace may be empty now
}

/* Hides the focused scratchpad window; else focuses one shown on the focused output; else
 * shows the hidden one put there longest ago, or failing that takes one shown elsewhere. */
void scratchpad_show(struct sh_server *server) {
    struct sh_toplevel *focused = server->focused_toplevel;
    if (focused && focused->scratchpad) {
        hide_in_scratchpad(focused);
        return;
    }
    struct wlr_output *output = focused_output(server);
    struct sh_toplevel *toplevel, *next = NULL;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel->scratchpad && toplevel_visible(toplevel) &&
            find_output(server, toplevel->output) == output) {
            focus_toplevel(toplevel);
            return;
        }
    }
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (!toplevel->scratchpad)
            continue;
        if (!next || (toplevel->minimized && !next->minimized) ||
            (toplevel->minimized == next->minimized &&
             toplevel->scratchpad_order < next->scratchpad_order))
            next = toplevel;
    }
    if (!next || !output)
        return;
    bool hidden = next->minimized;
    center_scratchpad(next, output);
    focus_toplevel(next); // a hidden one drops in as it is shown
    if (!hidden)
        scratchpad_drop_in(next); // and one taken from elsewhere as it arrives
    notify_subscribers(server);
}

/* With features.scratchpad turned off, its windows come back to the focused output, at once:
 * one sliding in or out lands first. */
void empty_scratchpad(struct sh_server *server) {
    struct wlr_output *output = focused_output(server);
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (!toplevel->scratchpad)
            continue;
        if (toplevel->drop.running)
            sh_anim_finish(&toplevel->anim);
        toplevel->scratchpad = false;
        if (!toplevel->minimized)
            continue;
        center_scratchpad(toplevel, output);
        toplevel->minimized = false;
        if (toplevel->foreign)
            wlr_foreign_toplevel_handle_v1_set_minimized(toplevel->foreign, false);
    }
}
