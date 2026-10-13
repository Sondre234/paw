/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Lifting a window moved with the pointer (windows.lift): once the pointer has taken it a few
 * pixels, it grows a touch about where it was taken and its shadow grows softer and further off
 * (frame.c draws it from the lift), easing with animations.move; held by its magnet against an
 * edge it sets down onto it, and it settles back as the move ends, however it ends. */
#include "server.h"

/* How far the pointer moves a window before it lifts, so that a click on its title bar does
 * not. */
#define LIFT_THRESHOLD 4
/* How much larger a lifted window is drawn: a fortieth, and no more than LIFT_MOST pixels along
 * its longer side, so that a large window is nudged rather than blown up. */
static const double LIFT_SCALE = 1.025;
#define LIFT_MOST 24

/* Each frame of the lift: the shadow and the clips follow it. */
static void lift_changed(void *data) {
    refresh_frame(data);
}

static double lift_scale(struct sh_toplevel *toplevel) {
    struct wlr_box box = toplevel_box(toplevel);
    int longer = box.width > box.height ? box.width : box.height;
    if (longer > 0 && longer * (LIFT_SCALE - 1) > LIFT_MOST)
        return 1 + (double)LIFT_MOST / longer;
    return LIFT_SCALE;
}

/* Lifts the grabbed window, or sets it down, about the point the pointer holds it by: of the
 * window where it is, whatever a glide still draws it on the way there. */
static void lift_set(struct sh_server *server, struct sh_toplevel *toplevel, bool up) {
    struct wlr_box geometry = toplevel_geometry(toplevel);
    sh_anim_lift_bounds(&toplevel->anim, 0, 0, geometry.width, geometry.height);
    sh_anim_lift(server->animator, &toplevel->anim, toplevel->content, up, lift_scale(toplevel),
                 server->grab_x, server->grab_y, lift_changed, toplevel);
}

/* A move with the pointer begins: nothing is lifted until the pointer takes the window away. */
void lift_press(struct sh_server *server) {
    server->lift.press_x = server->cursor->x;
    server->lift.press_y = server->cursor->y;
    server->lift.started = server->lift.up = false;
}

/* Follows a move with the pointer, after the window was put where the pointer takes it: lifts
 * it once it has gone LIFT_THRESHOLD, and sets it down onto an edge while its magnet holds it
 * (`held`), where it lifts again as the pointer pulls it away. */
void lift_follow(struct sh_server *server, bool held) {
    struct sh_toplevel *toplevel = server->grabbed_toplevel;
    if (!toplevel || server->cursor_mode != SH_CURSOR_MOVE || server->grab_fullscreen)
        return;
    if (!server->lift.started) {
        if (hypot(server->cursor->x - server->lift.press_x,
                  server->cursor->y - server->lift.press_y) < LIFT_THRESHOLD ||
            !server_settings(server)->lift || !toplevel->shown || !toplevel_mapped(toplevel) ||
            toplevel->fullscreen)
            return;
        server->lift.started = true;
    }
    if (server->lift.up == !held)
        return;
    server->lift.up = !held;
    lift_set(server, toplevel, !held);
}

/* The move ends, dropped or not: the window settles back. */
void lift_drop(struct sh_server *server) {
    struct sh_toplevel *toplevel = server->grabbed_toplevel;
    if (toplevel && server->lift.started && toplevel->scene_tree)
        lift_set(server, toplevel, false);
    server->lift.started = server->lift.up = false;
}
