/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Toys: animations asked for by name, for fun and to point at a window. `shake` wobbles a window
 * from side to side; with `gravity` every window of the focused monitor's workspace falls to the
 * bottom of its usable area, bounces, lands squashed a little, rests a moment and glides back
 * up; a `party` bobs them out of step under rainbow borders while confetti falls over the
 * screen. They only draw: each window's content is posed through the animator (sh_anim_pose)
 * and its border coloured by refresh_frame (toys_border), its place, size and client untouched,
 * and a window is let go, back at rest, as soon as anything else happens to it. src/toys.c does
 * the arithmetic. */
#include "server.h"
#include "paw/toys.h"

/* The toys move on with every frame, and with this timer between frames, so that nothing
 * stalls where a frame moves no pixel. */
enum { TICK_MS = 16 };
/* At most this many windows play; the rest of a crowded workspace looks on. */
enum { TOY_WINDOWS = 64 };
/* How far a shaken window swings at first. */
static const double SHAKE_AMPLITUDE = 22;
/* Gravity: how strong (pixels per millisecond squared: a fall of 800 pixels takes half a
 * second), how much of its speed a window keeps as it bounces, how long the windows rest once
 * the last has landed, and how long they take to glide back up. */
static const double GRAVITY = 0.006, RESTITUTION = 0.35;
enum { REST_MS = 700, RISE_MS = 650 };
/* A window resting on the bottom already sinks at least this share of the usable height, so
 * that tiles fall too. */
static const double SINK = 0.2;
/* A party's border goes once round the rainbow in this long. */
enum { RAINBOW_MS = 1600 };
/* Confetti: how many pieces at most, thrown in over this long, each living this long at most. */
enum { CONFETTI_PIECES = 240, CONFETTI_THROW_MS = 2200, CONFETTI_LIFE_MS = 2600 };
/* Everything that still shows fades out over a party's last stretch. */
enum { PARTY_FADE_MS = 500 };

struct toy_window {
    struct sh_toplevel *toplevel; // NULL once let go
    struct wlr_box box;           // where it was as the toy began (toplevel_box)
    bool posed;                   // the animator has drawn it posed since
    struct sh_pose pose;          // as last asked for
    struct sh_fall fall;          // gravity's, and how far down it is as it starts back up
    double rise_from;
    double phase, hue;            // a party's: its bob's and its colour's place in the round
    bool colored;                 // the party colours its border
    float color[4];
};

struct toy_piece {
    struct sh_confetto confetto;
    struct wlr_scene_rect *rect; // NULL once it has gone
};

struct sh_toys {
    struct sh_server *server;
    enum sh_action kind;
    int64_t start, ticked; // now_ms(): when it began and was last moved on
    int duration;
    char output[64]; // where it plays, and the workspace that output showed
    int workspace;
    struct wl_event_source *timer;
    struct toy_window windows[TOY_WINDOWS];
    int count;
    int64_t rise_at; // gravity: when the windows glide back up, from the start
    /* A party's confetti: a tree over the windows and the panels, over the output's box. */
    struct wlr_scene_tree *confetti;
    struct wlr_box area;
    struct toy_piece pieces[CONFETTI_PIECES];
    int thrown;
    uint32_t seed;
};

/* A stretch of a window's pose changed: its rounded clips follow (lift_box). */
static void pose_stretched(void *data) {
    struct toy_window *window = data;
    if (window->toplevel)
        refresh_frame(window->toplevel);
}

/* The window back at rest and out of the toy; its frame drawn again as it is, unless it is
 * going (`quiet`), which draws it itself. */
static void let_go(struct toy_window *window, bool quiet) {
    struct sh_toplevel *toplevel = window->toplevel;
    if (!toplevel)
        return;
    window->toplevel = NULL; // first: neither the pose's end nor the border asks for it again
    sh_anim_unpose(&toplevel->anim);
    if (!quiet)
        refresh_frame(toplevel);
}

/* Whether a window can play: shown on its monitor's workspace, mapped, not fullscreen. */
static bool playable(struct sh_toplevel *toplevel) {
    return toplevel && toplevel->scene_tree && toplevel->content && !toplevel->unmanaged &&
           toplevel_mapped(toplevel) && toplevel->shown && toplevel_visible(toplevel) &&
           !toplevel->fullscreen && toplevel->output[0];
}

/* Whether a window still plays: as it was when the toy began, its monitor showing the same
 * workspace, and drawn by nothing but the toy. Anything else lets it go. */
static bool still_plays(struct sh_toys *toys, struct toy_window *window) {
    struct sh_toplevel *toplevel = window->toplevel;
    struct sh_server *server = toys->server;
    if (!playable(toplevel) || server->grabbed_toplevel == toplevel ||
        strcmp(toplevel->output, toys->output) ||
        *output_workspace(server, toys->output) != toys->workspace)
        return false;
    struct wlr_box box = toplevel_box(toplevel);
    if (box.x != window->box.x || box.y != window->box.y || box.width != window->box.width ||
        box.height != window->box.height)
        return false;
    return !window->posed || sh_anim_posed(&toplevel->anim);
}

static struct toy_window *add_window(struct sh_toys *toys, struct sh_toplevel *toplevel) {
    if (toys->count >= TOY_WINDOWS)
        return NULL;
    struct toy_window *window = &toys->windows[toys->count++];
    *window = (struct toy_window){.toplevel = toplevel, .box = toplevel_box(toplevel)};
    window->pose = (struct sh_pose){.sx = 1, .sy = 1};
    return window;
}

/* Where each window falls and how it bounces: they pile up from the bottom of the usable area,
 * their borders touching, and rise again together once the last has landed and rested. */
static void plan_gravity(struct sh_toys *toys, struct wlr_output *output) {
    struct sh_rect area = usable_area(toys->server, output);
    int border = server_settings(toys->server)->border_width;
    struct sh_toy_box boxes[TOY_WINDOWS];
    for (int i = 0; i < toys->count; ++i) {
        struct wlr_box box = toys->windows[i].box;
        boxes[i] = (struct sh_toy_box){box.x - border, box.y - border, box.width + 2 * border,
                                       box.height + 2 * border};
    }
    double drops[TOY_WINDOWS];
    sh_pile(boxes, toys->count, area.y + area.height, SINK * area.height, drops);
    double landed = 0;
    for (int i = 0; i < toys->count; ++i) {
        sh_fall_plan(&toys->windows[i].fall, drops[i], GRAVITY, RESTITUTION);
        toys->windows[i].rise_from = drops[i];
        landed = fmax(landed, toys->windows[i].fall.landed);
    }
    toys->rise_at = (int64_t)ceil(landed) + REST_MS;
    toys->duration = (int)(toys->rise_at + RISE_MS);
}

static void plan_party(struct sh_toys *toys, struct wlr_output *output) {
    toys->duration = SH_PARTY_MS;
    for (int i = 0; i < toys->count; ++i) {
        struct toy_window *window = &toys->windows[i];
        window->phase = i * 2.399963; // the golden angle: no two bob in step
        window->hue = (double)i / toys->count;
        window->colored = true;
    }
    wlr_output_layout_get_box(toys->server->output_layout, output, &toys->area);
    toys->seed = (uint32_t)toys->start | 1;
    // Over the windows and the panels, under menus, the overview and the lock.
    toys->confetti = wlr_scene_tree_create(&toys->server->scene->tree);
    if (toys->confetti)
        wlr_scene_node_place_above(&toys->confetti->node, &toys->server->fullscreen_cover->node);
}

static int timer_fired(void *data) {
    struct sh_server *server = data;
    toys_tick(server);
    if (server->toys)
        wl_event_source_timer_update(server->toys->timer, TICK_MS);
    return 0;
}

/* Starts a toy on `target` (shake) or on the windows of `output`'s workspace, ending the one
 * that plays: one plays at a time. */
static void start(struct sh_server *server, enum sh_action kind, struct sh_toplevel *target,
                  struct wlr_output *output) {
    toys_end(server);
    if (server->locked || !output)
        return;
    struct sh_toys *toys = calloc(1, sizeof(*toys));
    if (!toys)
        return;
    toys->server = server;
    toys->kind = kind;
    toys->start = now_ms();
    snprintf(toys->output, sizeof(toys->output), "%s", output->name);
    toys->workspace = *output_workspace(server, output->name);
    if (target) {
        add_window(toys, target);
    } else {
        struct sh_toplevel *toplevel;
        wl_list_for_each(toplevel, &server->toplevels, link) {
            if (playable(toplevel) && !strcmp(toplevel->output, output->name))
                add_window(toys, toplevel);
        }
    }
    if (kind == SH_SHAKE)
        toys->duration = SH_SHAKE_MS;
    else if (kind == SH_GRAVITY)
        plan_gravity(toys, output);
    else
        plan_party(toys, output);
    // A party plays to its end with or without windows; the others need one.
    toys->timer = kind == SH_PARTY || toys->count
                      ? wl_event_loop_add_timer(wl_display_get_event_loop(server->wl_display),
                                                timer_fired, server)
                      : NULL;
    if (!toys->timer) {
        if (toys->confetti)
            wlr_scene_node_destroy(&toys->confetti->node);
        free(toys);
        return;
    }
    server->toys = toys;
    wlr_log(WLR_DEBUG, "Toy %s with %d windows on %s",
            kind == SH_SHAKE ? "shake" : kind == SH_GRAVITY ? "gravity" : "party", toys->count,
            toys->output);
    toys_tick(server); // draws the first frame
    if (server->toys)
        wl_event_source_timer_update(toys->timer, TICK_MS);
}

/* Shakes `toplevel` from side to side: "this one". Nothing happens to a window that is not on
 * the screen, or is fullscreen. */
void toys_shake(struct sh_server *server, struct sh_toplevel *toplevel) {
    if (playable(toplevel))
        start(server, SH_SHAKE, toplevel, find_output(server, toplevel->output));
}

/* The shake, gravity and party actions; a shake's argument is the window's number (its id), 0
 * for the focused window. */
void toys_start(struct sh_server *server, enum sh_action action, int argument) {
    if (action == SH_SHAKE) {
        struct sh_toplevel *toplevel = argument ? NULL : current_toplevel(server), *candidate;
        wl_list_for_each(candidate, &server->toplevels, link) {
            if (argument && candidate->id == (uint32_t)argument)
                toplevel = candidate;
        }
        toys_shake(server, toplevel);
    } else if (action == SH_GRAVITY || action == SH_PARTY) {
        start(server, action, NULL, focused_output(server));
    }
}

/* Throws in the confetti due `t` into the party, and moves every piece on by `dt`, fading the
 * lot out as the party ends; a piece whose life is over goes. */
static void move_confetti(struct sh_toys *toys, double t, double dt) {
    int due = (int)fmin(CONFETTI_PIECES, ceil(CONFETTI_PIECES * t / CONFETTI_THROW_MS));
    for (; toys->thrown < due; ++toys->thrown) {
        struct toy_piece *piece = &toys->pieces[toys->thrown];
        sh_confetti_spawn(&piece->confetto, &toys->seed, toys->area.x, toys->area.y,
                          toys->area.width, fmin(CONFETTI_LIFE_MS, SH_PARTY_MS - t));
        piece->rect = wlr_scene_rect_create(toys->confetti, 1, 1, piece->confetto.color);
    }
    dt = fmin(dt, 4 * TICK_MS); // after a stall (a VT switched away), a few frames' worth
    float fade = (float)fmin(1, (SH_PARTY_MS - t) / PARTY_FADE_MS);
    for (int i = 0; i < toys->thrown; ++i) {
        struct toy_piece *piece = &toys->pieces[i];
        if (!piece->rect)
            continue;
        const struct sh_confetto *confetto = &piece->confetto;
        if (!sh_confetti_step(&piece->confetto, dt)) {
            wlr_scene_node_destroy(&piece->rect->node);
            piece->rect = NULL;
            continue;
        }
        int width = sh_confetti_width(confetto);
        float alpha = sh_confetti_alpha(confetto) * fade, color[4];
        for (int c = 0; c < 3; ++c)
            color[c] = confetto->color[c] * alpha; // premultiplied
        color[3] = alpha;
        wlr_scene_rect_set_size(piece->rect, width, confetto->height);
        wlr_scene_rect_set_color(piece->rect, color);
        wlr_scene_node_set_position(&piece->rect->node, (int)lround(confetto->x - width / 2.0),
                                    (int)lround(confetto->y));
    }
}

/* The pose of a window `t` into its toy. */
static struct sh_pose pose_at(struct sh_toys *toys, struct toy_window *window, double t) {
    struct sh_pose pose = {.sx = 1, .sy = 1};
    if (toys->kind == SH_SHAKE) {
        pose.dx = sh_shake_offset(t, SHAKE_AMPLITUDE);
    } else if (toys->kind == SH_PARTY) {
        sh_party_bob(t, window->phase, &pose.dx, &pose.dy);
    } else if (t < toys->rise_at) {
        pose.dy = sh_fall_drop(&window->fall, t);
        // Squashed standing on its bottom edge, its border's, and wider by half as much.
        double squash = sh_fall_squash(&window->fall, t);
        if (squash > 0) {
            int border = server_settings(toys->server)->border_width;
            pose.sy = 1 - squash, pose.sx = 1 + squash / 2;
            pose.ax = window->box.width / 2.0, pose.ay = window->box.height + border;
        }
    } else {
        struct sh_curve curve = {SH_CURVE_EASE_IN_OUT, {0}};
        pose.dy = window->rise_from * (1 - sh_curve_eval(&curve, (t - toys->rise_at) / RISE_MS));
    }
    return pose;
}

/* Moves the toy on to now: each window to its pose, letting go of those that no longer play, a
 * party's colours and confetti; at its end, everything back as it was. */
void toys_tick(struct sh_server *server) {
    struct sh_toys *toys = server->toys;
    if (!toys)
        return;
    int64_t now = now_ms();
    if (toys->ticked && now == toys->ticked)
        return; // another monitor's frame in the same millisecond
    double t = (double)(now - toys->start), dt = toys->ticked ? (double)(now - toys->ticked) : 0;
    toys->ticked = now;
    if (server->locked || server->overview.open || t >= toys->duration ||
        !find_output(server, toys->output) ||
        *output_workspace(server, toys->output) != toys->workspace) {
        toys_end(server);
        return;
    }
    int playing = 0;
    for (int i = 0; i < toys->count; ++i) {
        struct toy_window *window = &toys->windows[i];
        if (!window->toplevel)
            continue;
        if (!still_plays(toys, window)) {
            let_go(window, false);
            continue;
        }
        ++playing;
        if (window->colored) {
            sh_rainbow(t / RAINBOW_MS + window->hue, window->color);
            refresh_frame(window->toplevel); // its border in the colour of the moment
        }
        window->pose = pose_at(toys, window, t);
        struct sh_toplevel *toplevel = window->toplevel;
        // One something else draws waits until it is free; one taken over is let go.
        if (sh_anim_pose(server->animator, &toplevel->anim, toplevel->content, &window->pose,
                         pose_stretched, window))
            window->posed = true;
        else if (window->posed)
            let_go(window, false);
    }
    if (toys->confetti)
        move_confetti(toys, t, dt);
    // Shaking or falling with no window left, it is over; a party plays on to its end.
    if (!playing && toys->kind != SH_PARTY)
        toys_end(server);
}

/* A button pressed: windows gravity has pulled down glide back up from where they are, so that
 * what the press reaches, which lookups find where the windows really are, is what shows there
 * next. */
void toys_press(struct sh_server *server) {
    struct sh_toys *toys = server->toys;
    if (!toys || toys->kind != SH_GRAVITY)
        return;
    int64_t t = now_ms() - toys->start;
    if (t >= toys->rise_at)
        return;
    for (int i = 0; i < toys->count; ++i)
        toys->windows[i].rise_from = sh_fall_drop(&toys->windows[i].fall, (double)t);
    toys->rise_at = t;
    toys->duration = (int)(t + RISE_MS);
    toys_tick(server);
}

/* Ends the toy playing, if any: every window back at rest with its own border, the confetti
 * gone. */
void toys_end(struct sh_server *server) {
    struct sh_toys *toys = server->toys;
    if (!toys)
        return;
    server->toys = NULL; // first: refresh_frame no longer finds the party's colours
    for (int i = 0; i < toys->count; ++i)
        let_go(&toys->windows[i], !server->running);
    if (toys->confetti)
        wlr_scene_node_destroy(&toys->confetti->node);
    if (toys->timer)
        wl_event_source_remove(toys->timer);
    free(toys);
}

/* A window going (unmap_toplevel): out of the toy at once, drawn where it is until then. */
void toys_forget(struct sh_toplevel *toplevel) {
    struct sh_toys *toys = toplevel->server->toys;
    for (int i = 0; toys && i < toys->count; ++i) {
        if (toys->windows[i].toplevel == toplevel)
            let_go(&toys->windows[i], true);
    }
}

/* The colour a party gives the window's border now, in place of its own (refresh_frame); false
 * when none does. */
bool toys_border(struct sh_toplevel *toplevel, float color[4]) {
    struct sh_toys *toys = toplevel->server->toys;
    for (int i = 0; toys && i < toys->count; ++i) {
        struct toy_window *window = &toys->windows[i];
        if (window->toplevel == toplevel && window->colored) {
            memcpy(color, window->color, sizeof(window->color));
            return true;
        }
    }
    return false;
}

/* The confetti's tree, which no lookup should find; NULL without one. */
struct wlr_scene_node *toys_confetti(struct sh_server *server) {
    return server->toys && server->toys->confetti ? &server->toys->confetti->node : NULL;
}

/* `get toys`: a `toy` line with the toy playing (shake, gravity, party, or none), how long it
 * has played and lasts (milliseconds), and its monitor and workspace; a `window` line for each
 * window playing: its number, its title, whether it is drawn posed yet, the offset it is drawn
 * at (x, y), its stretch (thousandths) and its border's colour (#rrggbb, or - for its own); and a
 * `confetti` line with how many pieces show. */
void describe_toys(struct sh_server *server, int fd, const char *arguments) {
    control_reply(fd, "ok\n");
    struct sh_toys *toys = server->toys;
    if (!toys) {
        control_reply(fd, "toy\tnone\t0\t0\t-\t0\nconfetti\t0\n");
        return;
    }
    const char *name = toys->kind == SH_SHAKE ? "shake" : toys->kind == SH_GRAVITY ? "gravity"
                                                                                  : "party";
    char line[768];
    snprintf(line, sizeof(line), "toy\t%s\t%ld\t%d\t%s\t%d\n", name, (long)(now_ms() - toys->start),
             toys->duration, toys->output, toys->workspace + 1);
    control_reply(fd, line);
    for (int i = 0; i < toys->count; ++i) {
        const struct toy_window *window = &toys->windows[i];
        if (!window->toplevel)
            continue;
        bool posed = sh_anim_posed(&window->toplevel->anim);
        const struct sh_pose *pose = &window->pose;
        char title[512], color[16] = "-";
        copy_field(title, sizeof(title), toplevel_title(window->toplevel));
        if (window->colored)
            snprintf(color, sizeof(color), "#%02x%02x%02x", (int)lroundf(window->color[0] * 255),
                     (int)lroundf(window->color[1] * 255), (int)lroundf(window->color[2] * 255));
        snprintf(line, sizeof(line), "window\t%u\t%s\t%d\t%ld\t%ld\t%ld\t%ld\t%s\n",
                 window->toplevel->id, title, posed, posed ? lround(pose->dx) : 0,
                 posed ? lround(pose->dy) : 0, posed ? lround(1000 * pose->sx) : 1000,
                 posed ? lround(1000 * pose->sy) : 1000, color);
        control_reply(fd, line);
    }
    int pieces = 0;
    for (int i = 0; i < toys->thrown; ++i)
        pieces += toys->pieces[i].rect != NULL;
    snprintf(line, sizeof(line), "confetti\t%d\n", pieces);
    control_reply(fd, line);
}
