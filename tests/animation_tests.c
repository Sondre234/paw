// SPDX-License-Identifier: GPL-3.0-or-later
/* Drives the animator on a bare scene graph with a fake clock: no renderer or display. */
#include "paw/animation.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>
#include "check.h"

static int64_t clock_now = 1000;
static int64_t fake_clock(void *data) {
    return clock_now;
}

struct rig {
    struct wl_event_loop *loop;
    struct sh_animator *animator;
    struct wlr_scene *scene;
    struct wlr_scene_tree *window, *content;
    struct wlr_scene_rect *rect;
    struct sh_anim anim;
};

static struct sh_animator_config config(int duration, int kind_curve) {
    struct sh_animator_config c = {.enabled = true, .speed = 1, .late_ms = 0};
    for (int i = 0; i < SH_ANIM_KINDS; ++i)
        c.styles[i] = (struct sh_anim_style){duration, {kind_curve, {0}}};
    return c;
}

static void setup(struct rig *rig, struct sh_animator_config c) {
    memset(rig, 0, sizeof(*rig));
    clock_now = 1000;
    rig->loop = wl_event_loop_create();
    rig->animator = sh_animator_create(rig->loop);
    sh_animator_set_clock(rig->animator, fake_clock, NULL);
    sh_animator_configure(rig->animator, &c);
    rig->scene = wlr_scene_create();
    rig->window = wlr_scene_tree_create(&rig->scene->tree);
    rig->content = wlr_scene_tree_create(rig->window);
    float color[4] = {0.2F, 0.4F, 0.6F, 1};
    rig->rect = wlr_scene_rect_create(rig->content, 200, 100, color);
    wlr_scene_node_set_position(&rig->rect->node, 10, 20);
}
static void teardown(struct rig *rig) {
    sh_anim_finish(&rig->anim);
    sh_animator_destroy(rig->animator);
    wlr_scene_node_destroy(&rig->scene->tree.node);
    wl_event_loop_destroy(rig->loop);
}
static void advance(struct rig *rig, int64_t ms) {
    clock_now += ms;
    sh_animator_tick(rig->animator);
}

static void test_glide_reaches_rest(void) {
    struct rig rig;
    setup(&rig, config(100, SH_CURVE_EASE_OUT));
    sh_anim_glide(rig.animator, &rig.anim, rig.content, -300, 50);
    CHECK(rig.content->node.x == -300 && rig.content->node.y == 50, "starts at the old place");
    CHECK(sh_animator_running(rig.animator) == 1, "running");
    advance(&rig, 50);
    CHECK(rig.content->node.x > -300 && rig.content->node.x < 0, "between: %d", rig.content->node.x);
    CHECK(fabs(rig.content->node.x - -300 * 0.125) <= 1, "ease-out at half time: %d",
          rig.content->node.x);
    advance(&rig, 50);
    CHECK(rig.content->node.x == 0 && rig.content->node.y == 0, "lands");
    CHECK(sh_animator_running(rig.animator) == 0, "finished");
    teardown(&rig);
}

static void test_retarget_is_continuous(void) {
    struct rig rig;
    setup(&rig, config(400, SH_CURVE_SPRING));
    sh_anim_glide(rig.animator, &rig.anim, rig.content, 1000, 0);
    advance(&rig, 100);
    int before = rig.content->node.x;
    // The layout changes again: the destination moves 400 further.
    sh_anim_glide(rig.animator, &rig.anim, rig.content, 400, 0);
    int after = rig.content->node.x;
    CHECK(after == before + 400,
          "the glide restarts from the drawn place plus the change: %d -> %d", before, after);
    CHECK(sh_animator_running(rig.animator) == 1, "still one animation");
    advance(&rig, 400);
    CHECK(rig.content->node.x == 0 && sh_animator_running(rig.animator) == 0, "lands after retarget");
    teardown(&rig);
}

static void test_retarget_keeps_velocity(void) {
    struct rig rig;
    setup(&rig, config(400, SH_CURVE_EASE_OUT));
    sh_anim_glide(rig.animator, &rig.anim, rig.content, 2000, 0);
    advance(&rig, 100);
    advance(&rig, 1);
    int a = rig.content->node.x;
    advance(&rig, 1);
    int b = rig.content->node.x;
    int speed_before = b - a; // pixels per millisecond, moving toward 0
    // Retarget by a tiny change: the position should carry on at about the same speed.
    sh_anim_glide(rig.animator, &rig.anim, rig.content, 1, 0);
    int c = rig.content->node.x;
    advance(&rig, 1);
    int d = rig.content->node.x;
    int speed_after = d - c;
    CHECK(speed_before < -5, "moving: %d", speed_before);
    CHECK(abs(speed_after - speed_before) <= 3, "velocity kept: %d then %d", speed_before,
          speed_after);
    teardown(&rig);
}

/* A glide started again in the millisecond it began, as by a placement within another, was never
 * drawn: it goes as one glide of the two offsets would, with no speed of its own. */
static void test_unseen_glide_carries_nothing(void) {
    struct rig twice, once;
    setup(&twice, config(400, SH_CURVE_SPRING));
    sh_anim_glide(twice.animator, &twice.anim, twice.content, 100, 0);
    sh_anim_glide(twice.animator, &twice.anim, twice.content, 50, -30);
    setup(&once, config(400, SH_CURVE_SPRING)); // the same clock, back at the same time
    sh_anim_glide(once.animator, &once.anim, once.content, 150, -30);
    CHECK(twice.content->node.x == 150 && twice.content->node.y == -30, "drawn where both put it");
    for (int i = 0; i < 10; ++i) {
        advance(&twice, 16);
        sh_animator_tick(once.animator);
        CHECK(twice.content->node.x == once.content->node.x &&
                  twice.content->node.y == once.content->node.y,
              "as one glide: %d, %d against %d, %d", twice.content->node.x, twice.content->node.y,
              once.content->node.x, once.content->node.y);
    }
    teardown(&once);
    teardown(&twice);
}

static void test_glide_kind_uses_its_own_style(void) {
    struct rig rig;
    struct sh_animator_config c = config(100, SH_CURVE_LINEAR);
    c.styles[SH_ANIM_FULLSCREEN].duration = 400;
    setup(&rig, c);
    sh_anim_glide_kind(rig.animator, &rig.anim, rig.content, -200, 0, SH_ANIM_FULLSCREEN);
    advance(&rig, 100);
    CHECK(rig.content->node.x < -100 && rig.content->node.x > -200,
          "the fullscreen duration applies: %d", rig.content->node.x);
    advance(&rig, 300);
    CHECK(rig.content->node.x == 0 && sh_animator_running(rig.animator) == 0, "lands");
    // A kind that is off snaps.
    c.styles[SH_ANIM_FULLSCREEN].duration = 0;
    sh_animator_configure(rig.animator, &c);
    sh_anim_glide_kind(rig.animator, &rig.anim, rig.content, -200, 0, SH_ANIM_FULLSCREEN);
    CHECK(rig.content->node.x == 0 && sh_animator_running(rig.animator) == 0, "off: no glide");
    teardown(&rig);
}

static void test_speed_and_disabled(void) {
    struct rig rig;
    struct sh_animator_config c = config(200, SH_CURVE_LINEAR);
    c.speed = 2;
    setup(&rig, c);
    sh_anim_glide(rig.animator, &rig.anim, rig.content, 100, 0);
    advance(&rig, 50);
    CHECK(rig.content->node.x == 50, "twice as fast: half way at a quarter of 200 ms, got %d",
          rig.content->node.x);
    advance(&rig, 50);
    CHECK(rig.content->node.x == 0 && sh_animator_running(rig.animator) == 0, "done at 100 ms");

    // One kind off, another on.
    c = config(100, SH_CURVE_LINEAR);
    c.styles[SH_ANIM_MOVE].duration = 0;
    sh_animator_configure(rig.animator, &c);
    sh_anim_glide(rig.animator, &rig.anim, rig.content, 100, 0);
    CHECK(rig.content->node.x == 0 && sh_animator_running(rig.animator) == 0,
          "a kind with duration 0 does not animate");
    sh_anim_open(rig.animator, &rig.anim, rig.content, 100, 50);
    CHECK(sh_animator_running(rig.animator) == 1, "open still animates");

    // Disabling finishes what runs, and nothing starts afterwards.
    c.enabled = false;
    sh_animator_configure(rig.animator, &c);
    CHECK(sh_animator_running(rig.animator) == 0, "disabled ends animations");
    CHECK(fabsf(rig.rect->color[3] - 1) < 1e-6F && rig.rect->width == 200, "rest values back");
    sh_anim_open(rig.animator, &rig.anim, rig.content, 100, 50);
    sh_anim_glide(rig.animator, &rig.anim, rig.content, 100, 0);
    CHECK(sh_animator_running(rig.animator) == 0 && rig.content->node.x == 0, "stays instant");
    teardown(&rig);
}

static void test_open_restores_rest_values(void) {
    struct rig rig;
    setup(&rig, config(100, SH_CURVE_LINEAR));
    sh_anim_open(rig.animator, &rig.anim, rig.content, 100, 50);
    CHECK(rig.rect->color[3] < 0.01F, "starts transparent: %f", rig.rect->color[3]);
    CHECK(rig.rect->width < 200 && rig.rect->width >= 188, "a little small: %d", rig.rect->width);
    advance(&rig, 50);
    CHECK(rig.rect->color[3] > 0.4F && rig.rect->color[3] < 0.6F, "half faded: %f",
          rig.rect->color[3]);
    advance(&rig, 60);
    CHECK(sh_animator_running(rig.animator) == 0, "ended");
    CHECK(rig.rect->width == 200 && rig.rect->height == 100, "size restored");
    CHECK(rig.rect->node.x == 10 && rig.rect->node.y == 20, "position restored");
    CHECK(fabsf(rig.rect->color[0] - 0.2F) < 1e-6F && fabsf(rig.rect->color[3] - 1) < 1e-6F,
          "color restored");
    teardown(&rig);
}

/* Nodes that adjoin, as the slices of a shadow do, stay adjoined at every step of an opening. */
static void test_open_keeps_neighbours_adjoined(void) {
    struct rig rig;
    setup(&rig, config(100, SH_CURVE_LINEAR));
    float color[4] = {0, 0, 0, 1};
    struct wlr_scene_rect *right = wlr_scene_rect_create(rig.content, 7, 100, color);
    wlr_scene_node_set_position(&right->node, 210, 20); // just right of rig.rect's 10..210
    sh_anim_open(rig.animator, &rig.anim, rig.content, 103, 57);
    int gaps = 0;
    for (int step = 0; step < 12; ++step) {
        gaps += rig.rect->node.x + rig.rect->width != right->node.x;
        advance(&rig, 10);
    }
    CHECK(gaps == 0, "%d steps with a gap or an overlap between neighbours", gaps);
    teardown(&rig);
}

static void test_close_leaves_nothing(void) {
    struct rig rig;
    setup(&rig, config(100, SH_CURVE_EASE_OUT));
    int before = wl_list_length(&rig.scene->tree.children);
    sh_anim_close(rig.animator, &rig.window->node, rig.content, 100, 50);
    CHECK(wl_list_length(&rig.scene->tree.children) == before + 1, "a snapshot tree appears");
    CHECK(sh_animator_running(rig.animator) == 1, "closing runs");
    advance(&rig, 60);
    CHECK(sh_animator_running(rig.animator) == 1, "still running");
    advance(&rig, 60);
    CHECK(sh_animator_running(rig.animator) == 0, "ended");
    CHECK(wl_list_length(&rig.scene->tree.children) == before, "and the snapshot is gone");
    // Quitting mid-animation frees the snapshot.
    sh_anim_close(rig.animator, &rig.window->node, rig.content, 100, 50);
    teardown(&rig);
}

static void test_late_frame_skips(void) {
    struct rig rig;
    struct sh_animator_config c = config(300, SH_CURVE_LINEAR);
    c.late_ms = 80;
    setup(&rig, c);
    sh_anim_glide(rig.animator, &rig.anim, rig.content, 300, 0);
    advance(&rig, 16);
    advance(&rig, 16);
    CHECK(rig.content->node.x > 0 && sh_animator_running(rig.animator) == 1, "steady frames animate");
    advance(&rig, 120); // a late frame, but the animation would still have 130 ms to go
    CHECK(rig.content->node.x == 0 && sh_animator_running(rig.animator) == 0,
          "a late frame lands the animation");
    // The next one starts normally: the first frame after idling is never counted as late.
    advance(&rig, 500);
    sh_anim_glide(rig.animator, &rig.anim, rig.content, 300, 0);
    advance(&rig, 16);
    CHECK(sh_animator_running(rig.animator) == 1 && rig.content->node.x > 0, "fresh start");
    teardown(&rig);
}

static void test_rest_for_input(void) {
    struct rig rig;
    setup(&rig, config(400, SH_CURVE_EASE_OUT));
    double sx, sy;
    // The rect rests at (10, 20), 200 by 100; a glide draws it 300 pixels to the right.
    sh_anim_glide(rig.animator, &rig.anim, rig.content, 300, 0);
    CHECK(wlr_scene_node_at(&rig.scene->tree.node, 50, 50, &sx, &sy) == NULL, "not drawn at rest");
    sh_animator_rest(rig.animator);
    struct wlr_scene_node *hit = wlr_scene_node_at(&rig.scene->tree.node, 50, 50, &sx, &sy);
    CHECK(hit == &rig.rect->node, "input sees the final place");
    sh_animator_resume(rig.animator);
    CHECK(rig.content->node.x == 300, "and the drawing continues where it was: %d",
          rig.content->node.x);
    // Closing snapshots are hidden for the lookup and shown after.
    sh_anim_close(rig.animator, &rig.window->node, rig.content, 100, 50);
    struct wlr_scene_node *snapshot = wl_container_of(rig.window->node.link.next, snapshot, link);
    sh_animator_rest(rig.animator);
    CHECK(!snapshot->enabled, "snapshot hidden");
    sh_animator_resume(rig.animator);
    CHECK(snapshot->enabled, "snapshot shown again");
    // An opening window is measured at full size.
    sh_animator_finish_all(rig.animator);
    sh_anim_open(rig.animator, &rig.anim, rig.content, 100, 50);
    sh_animator_rest(rig.animator);
    CHECK(rig.rect->width == 200 && fabsf(rig.rect->color[3] - 1) < 1e-6F, "full size at rest");
    sh_animator_resume(rig.animator);
    CHECK(rig.rect->width < 200 && rig.rect->color[3] < 0.01F, "small and faded again");
    advance(&rig, 500);
    CHECK(rig.rect->width == 200 && fabsf(rig.rect->color[3] - 1) < 1e-6F, "ends normally");
    teardown(&rig);
}

/* A lookup puts only the animations that matter at the point at rest. */
static void test_rest_at_point(void) {
    struct rig rig;
    setup(&rig, config(400, SH_CURVE_EASE_OUT));
    double sx, sy;
    sh_anim_glide(rig.animator, &rig.anim, rig.content, 300, 0); // drawn at 310..510, rests 10..210
    // Over neither the drawn nor the resting place: nothing is touched.
    int before = rig.content->node.x;
    sh_animator_rest_at(rig.animator, 900, 500);
    CHECK(rig.content->node.x == before && !rig.anim.rested, "far from the point: left alone");
    sh_animator_resume(rig.animator);
    CHECK(rig.content->node.x == before, "and resume leaves it alone");
    // Over the resting place: at rest for the lookup, back after.
    sh_animator_rest_at(rig.animator, 50, 50);
    CHECK(rig.content->node.x == 0, "at rest over its resting place");
    CHECK(wlr_scene_node_at(&rig.scene->tree.node, 50, 50, &sx, &sy) == &rig.rect->node, "found");
    sh_animator_resume(rig.animator);
    CHECK(rig.content->node.x == 300 && !rig.anim.rested, "drawing continues: %d",
          rig.content->node.x);
    // Over the drawn place: it must not be found there.
    sh_animator_rest_at(rig.animator, 400, 50);
    CHECK(wlr_scene_node_at(&rig.scene->tree.node, 400, 50, &sx, &sy) == NULL, "not found where drawn");
    sh_animator_resume(rig.animator);
    // A growing window is always put at rest.
    sh_animator_finish_all(rig.animator);
    sh_anim_open(rig.animator, &rig.anim, rig.content, 100, 50);
    sh_animator_rest_at(rig.animator, 900, 500);
    CHECK(rig.rect->width == 200, "opening windows are at full size for any lookup");
    sh_animator_resume(rig.animator);
    teardown(&rig);
}

static int updates;
static void count_update(void *data) {
    ++updates;
}

static void test_tween_follows_target(void) {
    struct rig rig;
    setup(&rig, config(100, SH_CURVE_LINEAR));
    struct sh_tween tween = {0};
    float target[SH_TWEEN_VALUES] = {1, 0, 0, 0, 1}, current[SH_TWEEN_VALUES];
    // The first target is taken as it is: nothing to fade from.
    sh_tween_track(rig.animator, &tween, SH_ANIM_FOCUS, true, target, current, count_update, NULL);
    CHECK(current[0] == 1 && current[4] == 1 && sh_animator_tweens(rig.animator) == 0, "first");
    // A new target fades over the duration, calling update every frame.
    target[0] = 0.5F, target[1] = 1;
    sh_tween_track(rig.animator, &tween, SH_ANIM_FOCUS, true, target, current, count_update, NULL);
    CHECK(current[0] == 1 && current[1] == 0 && sh_animator_tweens(rig.animator) == 1, "starts");
    updates = 0;
    advance(&rig, 50);
    sh_tween_track(rig.animator, &tween, SH_ANIM_FOCUS, true, target, current, count_update, NULL);
    CHECK(updates == 1 && fabsf(current[0] - 0.75F) < 1e-5F && fabsf(current[1] - 0.5F) < 1e-5F,
          "half way: %f %f", current[0], current[1]);
    // Reversed halfway, it goes back from where it is; it does not jump.
    target[0] = 1, target[1] = 0;
    sh_tween_track(rig.animator, &tween, SH_ANIM_FOCUS, true, target, current, count_update, NULL);
    CHECK(fabsf(current[0] - 0.75F) < 1e-5F, "continues from the current value: %f", current[0]);
    advance(&rig, 100);
    sh_tween_track(rig.animator, &tween, SH_ANIM_FOCUS, true, target, current, count_update, NULL);
    CHECK(current[0] == 1 && current[1] == 0 && sh_animator_tweens(rig.animator) == 0, "lands");
    // Not animating (or an animator that is off) takes the target at once.
    target[0] = 0.2F;
    sh_tween_track(rig.animator, &tween, SH_ANIM_FOCUS, false, target, current, count_update, NULL);
    CHECK(current[0] == 0.2F && sh_animator_tweens(rig.animator) == 0, "instant when asked");
    // A late frame lands it, and finish_all does too.
    target[0] = 1;
    sh_tween_track(rig.animator, &tween, SH_ANIM_FOCUS, true, target, current, count_update, NULL);
    sh_animator_finish_all(rig.animator);
    sh_tween_track(rig.animator, &tween, SH_ANIM_FOCUS, true, target, current, count_update, NULL);
    CHECK(current[0] == 1 && sh_animator_tweens(rig.animator) == 0, "finish_all lands");
    // Stopping forgets it without a callback.
    target[0] = 0;
    sh_tween_track(rig.animator, &tween, SH_ANIM_FOCUS, true, target, current, count_update, NULL);
    updates = 0;
    sh_tween_stop(&tween);
    sh_animator_finish_all(rig.animator);
    CHECK(updates == 0 && sh_animator_tweens(rig.animator) == 0, "stopped");
    teardown(&rig);
}

static void test_workspace_slide(void) {
    struct rig rig;
    setup(&rig, config(100, SH_CURVE_LINEAR));
    wlr_scene_node_set_position(&rig.window->node, 500, 40);
    // Leaving: a copy slides 80 to the left while fading; the window itself may be hidden.
    int before = wl_list_length(&rig.scene->tree.children);
    sh_anim_slide_out(rig.animator, &rig.window->node, rig.content, -80, 0);
    CHECK(wl_list_length(&rig.scene->tree.children) == before + 1, "a copy appears");
    struct wlr_scene_node *copy = wl_container_of(rig.window->node.link.next, copy, link);
    struct wlr_scene_tree *tree = wlr_scene_tree_from_node(copy);
    CHECK(copy->x == 500 && copy->y == 40, "starts where the window is: %d %d", copy->x, copy->y);
    struct wlr_scene_rect *shown = wl_container_of(tree->children.next, shown, node.link);
    CHECK(shown->color[3] > 0.99F, "opaque at first");
    advance(&rig, 50);
    CHECK(copy->x == 460 && copy->y == 40, "half way: %d", copy->x);
    CHECK(fabsf(shown->color[3] - 0.5F) < 0.02F, "half faded: %f", shown->color[3]);
    advance(&rig, 60);
    CHECK(wl_list_length(&rig.scene->tree.children) == before, "the copy is gone");
    CHECK(sh_animator_running(rig.animator) == 0, "done");

    // Arriving: the window's content starts 80 to the right, faded, and settles.
    sh_anim_slide(rig.animator, &rig.anim, rig.content, 80, 0);
    CHECK(rig.content->node.x == 80 && rig.rect->color[3] < 0.01F, "starts offset and faded");
    advance(&rig, 50);
    CHECK(rig.content->node.x == 40 && fabsf(rig.rect->color[3] - 0.5F) < 0.02F, "half: %d",
          rig.content->node.x);
    advance(&rig, 60);
    CHECK(rig.content->node.x == 0 && fabsf(rig.rect->color[3] - 1) < 1e-6F &&
              rig.rect->width == 200 && rig.rect->node.x == 10,
          "settled at rest values");
    // With a zero distance it only fades.
    sh_anim_slide(rig.animator, &rig.anim, rig.content, 0, 0);
    CHECK(rig.content->node.x == 0 && rig.rect->color[3] < 0.01F, "fade only");
    // Opening interrupted by a slide keeps the resting values, not the half-open look.
    sh_animator_finish_all(rig.animator);
    sh_anim_open(rig.animator, &rig.anim, rig.content, 100, 50);
    advance(&rig, 50);
    sh_anim_slide(rig.animator, &rig.anim, rig.content, 30, 0);
    advance(&rig, 200);
    CHECK(rig.rect->width == 200 && rig.rect->height == 100 && rig.rect->node.x == 10 &&
              fabsf(rig.rect->color[3] - 1) < 1e-6F && rig.content->node.x == 0,
          "rest values survive an interrupted open: %d %f", rig.rect->width, rig.rect->color[3]);
    // Off when the workspace kind has no duration.
    struct sh_animator_config c = config(100, SH_CURVE_LINEAR);
    c.styles[SH_ANIM_WORKSPACE].duration = 0;
    sh_animator_configure(rig.animator, &c);
    sh_anim_slide(rig.animator, &rig.anim, rig.content, 80, 0);
    sh_anim_slide_out(rig.animator, &rig.window->node, rig.content, -80, 0);
    CHECK(sh_animator_running(rig.animator) == 0 && rig.content->node.x == 0, "instant when off");
    teardown(&rig);
}

/* A gesture holds a window part of the way through its slide, whatever the time, and lets it go
 * on from there. */
static void test_hold_and_slide_from(void) {
    struct rig rig;
    struct sh_animator_config c = config(100, SH_CURVE_LINEAR);
    c.late_ms = 30;
    setup(&rig, c);
    sh_anim_hold(rig.animator, &rig.anim, rig.content, -40, 0, 0.5F);
    CHECK(rig.content->node.x == -40 && rig.rect->node.x == 10 && rig.rect->width == 200,
          "held offset: %d", rig.content->node.x);
    CHECK(fabsf(rig.rect->color[3] - 0.5F) < 1e-6F && fabsf(rig.rect->color[0] - 0.1F) < 1e-6F,
          "held faded: %f", rig.rect->color[3]);
    CHECK(sh_animator_running(rig.animator) == 0, "a hold is not running");
    advance(&rig, 500); // a late frame finishes what runs, not what is held
    advance(&rig, 500);
    CHECK(rig.content->node.x == -40 && fabsf(rig.rect->color[3] - 0.5F) < 1e-6F,
          "time leaves it where it is held");
    // A client redrawing its rectangle in another colour keeps the hold's opacity.
    float green[4] = {0, 1, 0, 1};
    wlr_scene_rect_set_color(rig.rect, green);
    advance(&rig, 16);
    CHECK(fabsf(rig.rect->color[1] - 0.5F) < 1e-6F, "a new colour held faded: %f",
          rig.rect->color[1]);
    // Held again, it moves at once.
    sh_anim_hold(rig.animator, &rig.anim, rig.content, -60, 0, 0.25F);
    CHECK(rig.content->node.x == -60 && fabsf(rig.rect->color[3] - 0.25F) < 1e-6F, "held further");
    // Hit testing sees it at rest, and it is held again afterwards.
    sh_animator_rest(rig.animator);
    CHECK(rig.content->node.x == 0 && fabsf(rig.rect->color[3] - 1) < 1e-6F, "at rest for input");
    sh_animator_resume(rig.animator);
    CHECK(rig.content->node.x == -60, "held again");
    // Let go, it slides back to rest in half the slide's time, from where it was held.
    sh_anim_slide_from(rig.animator, &rig.anim, rig.content, -60, 0, 0.25F, 0.5);
    CHECK(!rig.anim.held && sh_animator_running(rig.animator) == 1, "running from the hold");
    CHECK(rig.content->node.x == -60 && fabsf(rig.rect->color[3] - 0.25F) < 0.01F,
          "starts where it was held: %d %f", rig.content->node.x, rig.rect->color[3]);
    advance(&rig, 25);
    CHECK(rig.content->node.x == -30 && fabsf(rig.rect->color[3] - 0.625F) < 0.02F,
          "half way: %d %f", rig.content->node.x, rig.rect->color[3]);
    advance(&rig, 26);
    CHECK(rig.content->node.x == 0 && fabsf(rig.rect->color[1] - 1) < 1e-6F &&
              sh_animator_running(rig.animator) == 0,
          "at rest");
    // Finishing a hold puts the window back at rest at once.
    sh_anim_hold(rig.animator, &rig.anim, rig.content, 25, 5, 0.1F);
    sh_anim_finish(&rig.anim);
    CHECK(rig.content->node.x == 0 && rig.content->node.y == 0 &&
              fabsf(rig.rect->color[3] - 1) < 1e-6F && !rig.anim.held,
          "finished from a hold");
    // Holding over a running slide lands it first; turning animations off leaves the hold.
    sh_anim_slide(rig.animator, &rig.anim, rig.content, 80, 0);
    sh_anim_hold(rig.animator, &rig.anim, rig.content, 10, 0, 0.5F);
    CHECK(rig.content->node.x == 10 && sh_animator_running(rig.animator) == 0 &&
              fabsf(rig.rect->color[3] - 0.5F) < 1e-6F,
          "held over a slide: %d %f", rig.content->node.x, rig.rect->color[3]);
    struct sh_animator_config off = c;
    off.enabled = false;
    sh_animator_configure(rig.animator, &off);
    CHECK(rig.content->node.x == 10, "still held with animations off");
    // Without animations, letting go lands at once.
    sh_anim_slide_from(rig.animator, &rig.anim, rig.content, 10, 0, 0.5F, 1);
    CHECK(rig.content->node.x == 0 && fabsf(rig.rect->color[3] - 1) < 1e-6F &&
              sh_animator_running(rig.animator) == 0,
          "lands at once without animations");
    teardown(&rig);
}

/* A hidden window comes into view as a held copy, which slides away and goes. */
static void test_hold_copy(void) {
    struct rig rig;
    setup(&rig, config(100, SH_CURVE_LINEAR));
    wlr_scene_node_set_position(&rig.window->node, 500, 40);
    wlr_scene_node_set_enabled(&rig.window->node, false);
    int before = wl_list_length(&rig.scene->tree.children);
    struct sh_anim *copy = sh_anim_hold_copy(rig.animator, &rig.window->node, rig.content, 70, 0,
                                             0.2F);
    CHECK(copy && wl_list_length(&rig.scene->tree.children) == before + 1, "a copy appears");
    CHECK(copy->tree->node.enabled && copy->tree->node.x == 570 && copy->tree->node.y == 40,
          "held 70 from the hidden window: %d", copy->tree->node.x);
    struct wlr_scene_rect *shown = wl_container_of(copy->tree->children.next, shown, node.link);
    CHECK(fabsf(shown->color[3] - 0.2F) < 1e-6F && shown->node.x == 10, "a faded copy");
    sh_anim_hold(rig.animator, copy, copy->tree, 35, 0, 0.6F);
    CHECK(copy->tree->node.x == 535 && fabsf(shown->color[3] - 0.6F) < 1e-6F, "held nearer");
    advance(&rig, 1000);
    CHECK(copy->tree->node.x == 535, "a held copy stays");
    sh_animator_rest(rig.animator);
    CHECK(!copy->tree->node.enabled, "no copy in the way of input");
    sh_animator_resume(rig.animator);
    CHECK(copy->tree->node.enabled && copy->tree->node.x == 535, "back");
    // Let go, it slides on to 80 from the window, fading out, and goes.
    sh_anim_slide_away(rig.animator, copy, 80, 0, 0, 1);
    CHECK(copy->tree->node.x == 535 && fabsf(shown->color[3] - 0.6F) < 0.01F,
          "starts where it was held: %d", copy->tree->node.x);
    advance(&rig, 50);
    CHECK(abs(copy->tree->node.x - 558) <= 1 && fabsf(shown->color[3] - 0.3F) < 0.02F,
          "half way: %d %f", copy->tree->node.x, shown->color[3]);
    advance(&rig, 60);
    CHECK(wl_list_length(&rig.scene->tree.children) == before, "the copy is gone");
    // Finished, a held copy goes at once; nothing to copy, no copy.
    copy = sh_anim_hold_copy(rig.animator, &rig.window->node, rig.content, 0, 0, 1);
    sh_anim_finish(copy);
    CHECK(wl_list_length(&rig.scene->tree.children) == before, "a finished copy is gone");
    wlr_scene_node_set_enabled(&rig.rect->node, false);
    CHECK(!sh_anim_hold_copy(rig.animator, &rig.window->node, rig.content, 0, 0, 1),
          "nothing to copy");
    wlr_scene_node_set_enabled(&rig.rect->node, true);
    // A copy still held when the animator goes goes with it.
    sh_anim_hold_copy(rig.animator, &rig.window->node, rig.content, 5, 0, 1);
    teardown(&rig);
}

static int lift_updates;
static void count_lift(void *data) {
    ++lift_updates;
}

/* A dragged window is lifted: it grows about the point it was taken by and stays so, what the
 * client draws included, until it is let down again. */
static void test_lift(void) {
    struct rig rig;
    struct sh_animator_config c = config(100, SH_CURVE_LINEAR);
    c.late_ms = 30;
    setup(&rig, c);
    lift_updates = 0;
    // The rect rests at (10, 20), 200 by 100; it is lifted by its top-left corner.
    sh_anim_lift(rig.animator, &rig.anim, rig.content, true, 1.1, 10, 20, count_lift, NULL);
    CHECK(sh_animator_lifted(rig.animator) == 1 && sh_animator_running(rig.animator) == 0,
          "lifted, and nothing else runs");
    CHECK(rig.rect->width == 200 && sh_anim_lift_progress(&rig.anim) == 0, "starts down");
    advance(&rig, 25);
    advance(&rig, 25);
    CHECK(rig.rect->width == 210 && rig.rect->height == 105 && rig.rect->node.x == 10 &&
              rig.rect->node.y == 20,
          "half way up, about its corner: %d by %d at %d, %d", rig.rect->width,
          rig.rect->height, rig.rect->node.x, rig.rect->node.y);
    advance(&rig, 25);
    advance(&rig, 26);
    CHECK(rig.rect->width == 220 && rig.rect->height == 110 && sh_anim_lift_progress(&rig.anim) == 1,
          "up: %d", rig.rect->width);
    int updates = lift_updates;
    CHECK(updates == 4, "told on each frame: %d", updates);
    advance(&rig, 500); // a late frame lands what eases, and this is up already
    advance(&rig, 16);
    CHECK(rig.rect->width == 220 && lift_updates == updates, "stays up, quietly");
    int box[4] = {0, 0, 100, 100};
    sh_anim_lift_box(&rig.anim, box);
    CHECK(box[0] == -1 && box[1] == -2 && box[2] == 110 && box[3] == 110,
          "a box drawn as the nodes are: %d %d %d %d", box[0], box[1], box[2], box[3]);
    // The client draws at another size: that is lifted too.
    wlr_scene_rect_set_size(rig.rect, 300, 100);
    advance(&rig, 16);
    CHECK(rig.rect->width == 330 && rig.rect->height == 110, "a new size lifted: %d",
          rig.rect->width);
    // Input sees it at its own size.
    sh_animator_rest(rig.animator);
    CHECK(rig.rect->width == 300 && rig.rect->height == 100, "at rest for input");
    sh_animator_resume(rig.animator);
    CHECK(rig.rect->width == 330, "lifted again");
    // Let down, it settles in half as long again, from where it is.
    sh_anim_lift(rig.animator, &rig.anim, rig.content, false, 1.1, 10, 20, count_lift, NULL);
    CHECK(rig.rect->width == 330, "no jump as it starts down");
    for (int i = 0; i < 3; ++i)
        advance(&rig, 25);
    CHECK(rig.rect->width == 315, "half way down: %d", rig.rect->width);
    for (int i = 0; i < 3; ++i)
        advance(&rig, 26);
    CHECK(rig.rect->width == 300 && rig.rect->height == 100 && rig.rect->node.x == 10 &&
              sh_animator_lifted(rig.animator) == 0 && !rig.anim.records,
          "down and gone: %d", rig.rect->width);
    // Reversed half way up, it turns back from where it is.
    sh_anim_lift(rig.animator, &rig.anim, rig.content, true, 1.1, 10, 20, NULL, NULL);
    advance(&rig, 25);
    advance(&rig, 25);
    int half = rig.rect->width;
    sh_anim_lift(rig.animator, &rig.anim, rig.content, false, 1.1, 10, 20, NULL, NULL);
    CHECK(rig.rect->width == half && half > 300 && half < 330, "turns where it is: %d", half);
    advance(&rig, 25);
    CHECK(rig.rect->width < half + 3, "and comes back down: %d", rig.rect->width);
    for (int i = 0; i < 8; ++i)
        advance(&rig, 25);
    CHECK(rig.rect->width == 300 && sh_animator_lifted(rig.animator) == 0, "down");
    teardown(&rig);
}

/* A lift goes on through other animations, at once without animations, and ends when the
 * window's animations are finished. */
static void test_lift_with_others(void) {
    struct rig rig;
    setup(&rig, config(100, SH_CURVE_LINEAR));
    // Over an opening window, the two scales multiply.
    sh_anim_open(rig.animator, &rig.anim, rig.content, 110, 70);
    sh_anim_lift(rig.animator, &rig.anim, rig.content, true, 1.1, 110, 70, NULL, NULL);
    advance(&rig, 50);
    CHECK(rig.rect->width == 204, "open and lift: %d", rig.rect->width); // 200 * 0.97 * 1.05
    advance(&rig, 60);
    CHECK(rig.rect->width == 220 && rig.rect->node.x == 0 && sh_animator_running(rig.animator) == 0,
          "opened, still lifted: %d at %d", rig.rect->width, rig.rect->node.x);
    // A glide moves it, lifted.
    sh_anim_glide(rig.animator, &rig.anim, rig.content, 40, 0);
    CHECK(rig.content->node.x == 40 && rig.rect->width == 220, "glides lifted");
    // A late frame or turning animations off lands what runs; the lift stays up.
    sh_animator_finish_all(rig.animator);
    CHECK(rig.content->node.x == 0 && rig.rect->width == 220, "still up after finishing all");
    struct sh_animator_config off = config(100, SH_CURVE_LINEAR);
    off.enabled = false;
    sh_animator_configure(rig.animator, &off);
    CHECK(rig.rect->width == 220, "and with animations off");
    // Without animations it goes down at once, and up again at once.
    sh_anim_lift(rig.animator, &rig.anim, rig.content, false, 1.1, 110, 70, NULL, NULL);
    CHECK(rig.rect->width == 200 && sh_animator_lifted(rig.animator) == 0, "down at once");
    sh_anim_lift(rig.animator, &rig.anim, rig.content, true, 1.1, 110, 70, NULL, NULL);
    CHECK(rig.rect->width == 220 && rig.rect->node.x == 0, "up at once: %d", rig.rect->width);
    // Finishing the window's animations ends it.
    sh_anim_finish(&rig.anim);
    CHECK(rig.rect->width == 200 && rig.rect->node.x == 10 && sh_animator_lifted(rig.animator) == 0,
          "finished: %d", rig.rect->width);
    // Letting down what is not lifted does nothing.
    sh_anim_lift(rig.animator, &rig.anim, rig.content, false, 1.1, 110, 70, NULL, NULL);
    CHECK(sh_animator_lifted(rig.animator) == 0, "nothing to let down");
    // A window held by a gesture is held lifted.
    sh_animator_configure(rig.animator, &(struct sh_animator_config){0});
    struct sh_animator_config on = config(100, SH_CURVE_LINEAR);
    sh_animator_configure(rig.animator, &on);
    sh_anim_lift(rig.animator, &rig.anim, rig.content, true, 1.1, 110, 70, NULL, NULL);
    advance(&rig, 120);
    sh_anim_hold(rig.animator, &rig.anim, rig.content, -30, 0, 0.5F);
    advance(&rig, 16);
    CHECK(rig.content->node.x == -30 && rig.rect->width == 220 &&
              fabsf(rig.rect->color[3] - 0.5F) < 1e-6F,
          "held lifted: %d", rig.rect->width);
    teardown(&rig);
    CHECK(true, "a lifted window's animator goes cleanly");
}

/* With bounds, a lift draws them larger with their edges on whole pixels, and what lies outside
 * them (a border, a shadow) at its own size around them. */
static void test_lift_bounds(void) {
    struct rig rig;
    setup(&rig, config(100, SH_CURVE_LINEAR));
    // A border two pixels wide around the window's rect.
    float red[4] = {1, 0, 0, 1};
    struct wlr_scene_rect *border = wlr_scene_rect_create(rig.content, 204, 104, red);
    wlr_scene_node_set_position(&border->node, 8, 18);
    sh_anim_lift_bounds(&rig.anim, 10, 20, 200, 100);
    sh_anim_lift(rig.animator, &rig.anim, rig.content, true, 1.1, 110, 70, NULL, NULL);
    for (int i = 0; i < 6; ++i)
        advance(&rig, 20);
    CHECK(rig.rect->node.x == 0 && rig.rect->node.y == 15 && rig.rect->width == 220 &&
              rig.rect->height == 110,
          "the window drawn larger: %d, %d, %d by %d", rig.rect->node.x, rig.rect->node.y,
          rig.rect->width, rig.rect->height);
    CHECK(border->node.x == -2 && border->node.y == 13 && border->width == 224 &&
              border->height == 114,
          "its border as wide around it: %d, %d, %d by %d", border->node.x, border->node.y,
          border->width, border->height);
    int box[4] = {8, 18, 204, 104};
    sh_anim_lift_box(&rig.anim, box);
    CHECK(box[0] == -2 && box[1] == 13 && box[2] == 224 && box[3] == 114,
          "a box drawn as the border is: %d %d %d %d", box[0], box[1], box[2], box[3]);
    // Half way, the window's edges are still on whole pixels and the border meets them.
    sh_anim_lift(rig.animator, &rig.anim, rig.content, false, 1.1, 110, 70, NULL, NULL);
    for (int step = 0; step < 8; ++step) {
        advance(&rig, 19);
        CHECK(border->node.x == rig.rect->node.x - 2 && border->width == rig.rect->width + 4 &&
                  border->node.y == rig.rect->node.y - 2 && border->height == rig.rect->height + 4,
              "the border meets the window settling: %d %d %d %d against %d %d %d %d",
              border->node.x, border->node.y, border->width, border->height, rig.rect->node.x,
              rig.rect->node.y, rig.rect->width, rig.rect->height);
    }
    CHECK(rig.rect->width == 200 && border->width == 204 && border->node.x == 8, "down");
    teardown(&rig);
}

/* A gesture holding or sliding a window lets its lift go on over it; finishing the window's
 * animations ends the lift and tells its owner, so that what follows the lift rests too. */
static void test_lift_through_gestures(void) {
    struct rig rig;
    setup(&rig, config(100, SH_CURVE_LINEAR));
    lift_updates = 0;
    sh_anim_lift(rig.animator, &rig.anim, rig.content, true, 1.1, 10, 20, count_lift, NULL);
    for (int i = 0; i < 6; ++i)
        advance(&rig, 20);
    sh_anim_lift(rig.animator, &rig.anim, rig.content, false, 1.1, 10, 20, count_lift, NULL);
    advance(&rig, 20);
    int settling = rig.rect->width;
    CHECK(settling > 200 && settling < 220, "settling: %d", settling);
    // Gliding as it settles, as a dropped tile does into its place, a swipe holds it part of the
    // way: the glide lands, and it settles on under the hold and tells its owner.
    sh_anim_glide(rig.animator, &rig.anim, rig.content, 40, 0);
    sh_anim_hold(rig.animator, &rig.anim, rig.content, -30, 0, 0.5F);
    CHECK(sh_animator_lifted(rig.animator) == 1 && rig.content->node.x == -30 &&
              rig.rect->width == settling,
          "the lift goes on under the hold: %zu, %d", sh_animator_lifted(rig.animator),
          rig.rect->width);
    int updates = lift_updates;
    for (int i = 0; i < 9; ++i)
        advance(&rig, 20);
    CHECK(sh_animator_lifted(rig.animator) == 0 && rig.rect->width == 200 &&
              rig.content->node.x == -30 && lift_updates > updates,
          "settled under the hold, its owner told: %d, %d updates", rig.rect->width,
          lift_updates - updates);
    // Slid on from a hold, a lift going up goes on too.
    sh_anim_lift(rig.animator, &rig.anim, rig.content, true, 1.1, 10, 20, count_lift, NULL);
    advance(&rig, 20);
    sh_anim_slide_from(rig.animator, &rig.anim, rig.content, -30, 0, 0.5F, 1);
    CHECK(sh_animator_lifted(rig.animator) == 1 && rig.rect->width > 200,
          "the lift goes on over the slide: %d", rig.rect->width);
    for (int i = 0; i < 12; ++i)
        advance(&rig, 20);
    CHECK(rig.rect->width == 220 && rig.content->node.x == 0, "slid home, lifted: %d at %d",
          rig.rect->width, rig.content->node.x);
    // Finished, the lift ends, and its owner hears of it.
    updates = lift_updates;
    sh_anim_finish(&rig.anim);
    CHECK(rig.rect->width == 200 && sh_animator_lifted(rig.animator) == 0 &&
              lift_updates == updates + 1 && sh_anim_lift_progress(&rig.anim) == 0,
          "finished, its owner told once: %d updates", lift_updates - updates);
    teardown(&rig);
}

/* A lookup puts a lifted window at its own size only when it could be found at the point. */
static void test_lift_rest_at_point(void) {
    struct rig rig;
    setup(&rig, config(100, SH_CURVE_LINEAR));
    sh_anim_lift(rig.animator, &rig.anim, rig.content, true, 1.1, 10, 20, NULL, NULL);
    for (int i = 0; i < 6; ++i)
        advance(&rig, 20);
    CHECK(rig.rect->width == 220, "lifted: %d", rig.rect->width);
    // Far from it: left alone, nothing to draw again.
    sh_animator_rest_at(rig.animator, 900, 600);
    CHECK(rig.rect->width == 220 && !rig.anim.lift_rested, "far from the point: left alone");
    sh_animator_resume(rig.animator);
    CHECK(rig.rect->width == 220, "and resume leaves it alone");
    // Over the part only the lift draws, and over the window: at its own size for the lookup.
    double sx, sy;
    sh_animator_rest_at(rig.animator, 225, 60);
    CHECK(rig.rect->width == 200 &&
              wlr_scene_node_at(&rig.scene->tree.node, 225, 60, &sx, &sy) == NULL,
          "at rest where only the lift reaches");
    sh_animator_resume(rig.animator);
    CHECK(rig.rect->width == 220 && !rig.anim.lift_rested, "lifted again");
    sh_animator_rest_at(rig.animator, 50, 50);
    CHECK(rig.rect->width == 200, "at rest over the window");
    sh_animator_resume(rig.animator);
    teardown(&rig);
}

/* A window changing size: `surfaces` holds the client's surface (a rect standing for its buffer,
 * 200 by 100 at the origin), and `frame` the frame the owner draws around the size drawn, as
 * frame.c does from the morph's `resized`. */
struct morph_rig {
    struct rig rig;
    struct wlr_scene_tree *surfaces;
    struct wlr_scene_rect *surface, *frame;
    int heard, heard_width, heard_height; // what `resized` heard last; 0 by 0 for rest
};

static void morph_resized(void *data) {
    struct morph_rig *m = data;
    ++m->heard;
    int width, height;
    if (sh_anim_drawn_size(&m->rig.anim, &width, &height)) {
        m->heard_width = width, m->heard_height = height;
    } else {
        m->heard_width = m->heard_height = 0;
        width = m->surface->width, height = m->surface->height;
    }
    wlr_scene_rect_set_size(m->frame, width + 4, height + 4);
}

static void morph_setup(struct morph_rig *m, struct sh_animator_config c) {
    setup(&m->rig, c);
    float color[4] = {0.2F, 0.4F, 0.6F, 1}, black[4] = {0, 0, 0, 1};
    // The frame below, at the window's size and two pixels past it.
    wlr_scene_rect_set_size(m->rig.rect, 204, 104);
    wlr_scene_rect_set_color(m->rig.rect, black);
    wlr_scene_node_set_position(&m->rig.rect->node, -2, -2);
    m->frame = m->rig.rect;
    m->surfaces = wlr_scene_tree_create(m->rig.content);
    m->surface = wlr_scene_rect_create(m->surfaces, 200, 100, color);
    m->heard = m->heard_width = m->heard_height = 0;
}

/* Glides by (dx, dy) from 200 by 100 to `width` by `height`. */
static void morph_to(struct morph_rig *m, int dx, int dy, int width, int height) {
    struct sh_morph morph = {
        .surfaces = m->surfaces,
        .dx = dx,
        .dy = dy,
        .from_width = 200,
        .from_height = 100,
        .width = width,
        .height = height,
        .live_width = 200,
        .live_height = 100,
        .kind = SH_ANIM_MOVE,
        .resized = morph_resized,
        .data = m,
    };
    sh_anim_morph(m->rig.animator, &m->rig.anim, m->rig.content, &morph);
}

/* The client draws `width` by `height`: the animator hears of it before the scene takes it. */
static void client_draws(struct morph_rig *m, int width, int height, bool answered) {
    sh_anim_morph_commit(m->rig.animator, &m->rig.anim, width, height, answered);
    wlr_scene_rect_set_size(m->surface, width, height);
}

/* The tree just above the surfaces, a copy of what the client drew before; NULL for none. */
static struct wlr_scene_tree *fading_copy(struct morph_rig *m) {
    struct wl_list *next = m->surfaces->node.link.next;
    if (next == &m->rig.content->children)
        return NULL;
    struct wlr_scene_node *node = wl_container_of(next, node, link);
    return node->type == WLR_SCENE_NODE_TREE ? wlr_scene_tree_from_node(node) : NULL;
}

/* The copy's rect, or NULL. */
static struct wlr_scene_rect *fading_rect(struct morph_rig *m) {
    struct wlr_scene_tree *copy = fading_copy(m);
    if (!copy || wl_list_empty(&copy->children))
        return NULL;
    struct wlr_scene_node *node = wl_container_of(copy->children.next, node, link);
    return node->type == WLR_SCENE_NODE_RECT ? wlr_scene_rect_from_node(node) : NULL;
}

static void test_morph_stretches_and_waits(void) {
    struct morph_rig m;
    morph_setup(&m, config(100, SH_CURVE_LINEAR));
    morph_to(&m, -50, -20, 400, 300);
    CHECK(sh_animator_running(m.rig.animator) == 1, "running");
    CHECK(m.rig.content->node.x == -50 && m.rig.content->node.y == -20, "drawn where it was");
    CHECK(m.surface->width == 200 && m.surface->height == 100, "at its old size");
    CHECK(m.heard >= 1 && m.heard_width == 200 && m.heard_height == 100,
          "the frame is drawn for that size: %d by %d", m.heard_width, m.heard_height);
    advance(&m.rig, 50);
    CHECK(m.rig.content->node.x == -25 && m.rig.content->node.y == -10, "half way: %d, %d",
          m.rig.content->node.x, m.rig.content->node.y);
    CHECK(m.surface->width == 300 && m.surface->height == 200, "stretched half way: %d by %d",
          m.surface->width, m.surface->height);
    CHECK(m.frame->width == 304 && m.frame->height == 204, "the frame follows");
    advance(&m.rig, 50);
    CHECK(m.rig.content->node.x == 0 && m.surface->width == 400 && m.surface->height == 300,
          "the glide is over, the client's old picture stretched to the new size");
    CHECK(sh_animator_running(m.rig.animator) == 1, "waiting for the client");
    advance(&m.rig, 250);
    CHECK(sh_animator_running(m.rig.animator) == 1 && m.surface->width == 400, "still waiting");
    // The client had its time: the window eases to the size it drew, rather than jump there.
    advance(&m.rig, 60);
    CHECK(sh_animator_running(m.rig.animator) == 1 && m.surface->width == 400, "giving up");
    advance(&m.rig, 50);
    CHECK(m.surface->width == 300 && m.surface->height == 200, "easing to the client's size: %d",
          m.surface->width);
    int heard = m.heard;
    advance(&m.rig, 60);
    CHECK(sh_animator_running(m.rig.animator) == 0, "done");
    CHECK(m.surface->width == 200 && m.surface->height == 100, "drawn as the client drew it");
    CHECK(m.heard == heard + 1 && m.heard_width == 0, "the frame drawn for it again");
    CHECK(m.frame->width == 204, "the frame at rest: %d", m.frame->width);
    teardown(&m.rig);
}

/* A client that acknowledges the size it was given with what it drew before, and draws the new
 * size on a later commit, as some video players do: the window does not head back to the old
 * size meanwhile, nor squeeze the new picture and pop at the end. */
static void test_morph_answer_after_an_early_ack(void) {
    struct morph_rig m;
    morph_setup(&m, config(100, SH_CURVE_LINEAR));
    morph_to(&m, 0, 0, 400, 300);
    advance(&m.rig, 20);
    client_draws(&m, 200, 100, true); // acknowledged, at the old size
    int last = m.surface->width;
    bool shrank = false;
    for (int i = 0; i < 12; ++i) {
        advance(&m.rig, 10);
        shrank |= m.surface->width < last;
        last = m.surface->width;
    }
    CHECK(!shrank && m.surface->width == 400, "on to the new size: %d", m.surface->width);
    CHECK(sh_animator_running(m.rig.animator) == 1, "waiting for the new picture");
    client_draws(&m, 400, 300, true);
    CHECK(fading_copy(&m) != NULL, "the old picture fades into it");
    advance(&m.rig, 100);
    CHECK(sh_animator_running(m.rig.animator) == 0 && m.surface->width == 400, "at rest");
    teardown(&m.rig);
}

/* Each answer at a size of its own retargets the window, not only the first. */
static void test_morph_follows_every_answer(void) {
    struct morph_rig m;
    morph_setup(&m, config(100, SH_CURVE_LINEAR));
    morph_to(&m, 0, 0, 400, 300);
    advance(&m.rig, 30);
    client_draws(&m, 380, 280, true);
    advance(&m.rig, 30);
    client_draws(&m, 360, 260, true);
    int before_end = 0;
    for (int i = 0; i < 60 && sh_animator_running(m.rig.animator); ++i) {
        before_end = m.surface->width;
        advance(&m.rig, 5);
    }
    CHECK(abs(before_end - 360) <= 6, "easing to it, no jump at the end: %d", before_end);
    CHECK(sh_animator_running(m.rig.animator) == 0, "done");
    CHECK(m.surface->width == 360 && m.surface->height == 260, "at the last answer's size: %d",
          m.surface->width);
    CHECK(m.frame->width == 364, "its frame too: %d", m.frame->width);
    teardown(&m.rig);
}

static void test_morph_crossfades_the_answer(void) {
    struct morph_rig m;
    morph_setup(&m, config(100, SH_CURVE_LINEAR));
    int children = wl_list_length(&m.rig.content->children);
    morph_to(&m, 0, 0, 400, 300);
    advance(&m.rig, 20);
    CHECK(m.surface->width == 240 && m.surface->height == 140, "a fifth of the way: %d by %d",
          m.surface->width, m.surface->height);
    client_draws(&m, 400, 300, true);
    CHECK(fading_copy(&m) && wl_list_length(&m.rig.content->children) == children + 1,
          "a copy of the old picture over the new one");
    struct wlr_scene_rect *old = fading_rect(&m);
    CHECK(old && old->width == 200 && old->height == 100, "the old picture as the client drew it");
    advance(&m.rig, 20);
    CHECK(m.surface->width == 280 && m.surface->height == 180,
          "the new picture shrunk to the size drawn: %d by %d", m.surface->width,
          m.surface->height);
    CHECK(old && old->width == 280 && old->height == 180, "the old one stretched to it");
    CHECK(old && old->color[3] < 1 && old->color[3] > 0.5F, "fading: %f", old ? old->color[3] : 0);
    advance(&m.rig, 40);
    CHECK(old && old->color[3] < 0.5F, "mostly gone: %f", old ? old->color[3] : 0);
    advance(&m.rig, 50);
    CHECK(sh_animator_running(m.rig.animator) == 0, "done once the glide and the fade are");
    CHECK(!fading_copy(&m) && wl_list_length(&m.rig.content->children) == children,
          "the copy is gone");
    CHECK(m.surface->width == 400 && m.surface->height == 300, "the new picture at rest");
    CHECK(m.frame->width == 404 && m.heard_width == 0, "and the frame");
    teardown(&m.rig);
}

/* The client answers once the glide is over, at the very size its old picture was stretched
 * to, which the records could take for the animation's own: it rests at that size. */
static void test_morph_late_answer_at_drawn_size(void) {
    struct morph_rig m;
    morph_setup(&m, config(100, SH_CURVE_LINEAR));
    morph_to(&m, 0, 0, 400, 300);
    advance(&m.rig, 150);
    CHECK(m.surface->width == 400 && sh_animator_running(m.rig.animator) == 1, "waiting");
    client_draws(&m, 400, 300, true);
    CHECK(fading_copy(&m) != NULL, "the old picture fades");
    advance(&m.rig, 20);
    CHECK(m.surface->width == 400 && m.surface->height == 300, "the new one at its size");
    advance(&m.rig, 40);
    CHECK(sh_animator_running(m.rig.animator) == 0, "it fades in half the glide's time");
    CHECK(m.surface->width == 400 && m.surface->height == 300, "and rests there: %d by %d",
          m.surface->width, m.surface->height);
    teardown(&m.rig);
}

/* A second change of size while the first fades keeps that fade going, at the opacity it has. */
static void test_morph_keeps_one_fade(void) {
    struct morph_rig m;
    morph_setup(&m, config(100, SH_CURVE_LINEAR));
    morph_to(&m, 0, 0, 400, 300);
    advance(&m.rig, 10);
    client_draws(&m, 300, 200, false);
    struct wlr_scene_tree *copy = fading_copy(&m);
    advance(&m.rig, 40);
    struct wlr_scene_rect *old = fading_rect(&m);
    float alpha = old ? old->color[3] : 0;
    CHECK(copy && alpha < 0.9F && alpha > 0.2F, "half faded: %f", alpha);
    client_draws(&m, 400, 300, true);
    advance(&m.rig, 1);
    CHECK(fading_copy(&m) == copy, "the same copy");
    old = fading_rect(&m);
    CHECK(old && old->color[3] <= alpha, "fading on from where it was: %f after %f",
          old ? old->color[3] : 0, alpha);
    advance(&m.rig, 200);
    CHECK(sh_animator_running(m.rig.animator) == 0 && !fading_copy(&m), "done");
    teardown(&m.rig);
}

/* Drawn at another size than asked, the window eases on to it rather than jumping. */
static void test_morph_answer_of_its_own(void) {
    struct morph_rig m;
    morph_setup(&m, config(100, SH_CURVE_LINEAR));
    morph_to(&m, 0, 0, 400, 300);
    advance(&m.rig, 50);
    int width = m.surface->width;
    client_draws(&m, 380, 250, true);
    advance(&m.rig, 1);
    CHECK(abs(m.surface->width - width) <= 3, "no jump: %d after %d", m.surface->width, width);
    advance(&m.rig, 200);
    CHECK(sh_animator_running(m.rig.animator) == 0, "done");
    CHECK(m.surface->width == 380 && m.surface->height == 250, "at the client's size");
    teardown(&m.rig);
}

/* Reversed half way (maximized, then restored), it goes back from where it is drawn, at the
 * speed it has. */
static void test_morph_reverses_smoothly(void) {
    struct morph_rig m;
    morph_setup(&m, config(400, SH_CURVE_SPRING));
    morph_to(&m, 0, 0, 1200, 900);
    advance(&m.rig, 60);
    int before = m.surface->width;
    advance(&m.rig, 16);
    int speed = m.surface->width - before;
    CHECK(speed > 0, "growing");
    struct sh_morph back = {.surfaces = m.surfaces,
                            .from_width = 1,
                            .from_height = 1,
                            .width = 200,
                            .height = 100,
                            .live_width = 1,
                            .live_height = 1,
                            .kind = SH_ANIM_MOVE,
                            .resized = morph_resized,
                            .data = &m};
    int drawn = m.surface->width;
    sh_anim_morph(m.rig.animator, &m.rig.anim, m.rig.content, &back);
    CHECK(m.surface->width == drawn, "starts where it is drawn, not from the size given: %d, %d",
          m.surface->width, drawn);
    advance(&m.rig, 16);
    int next = m.surface->width - drawn;
    CHECK(next > 0 && next <= 2 * speed, "still growing at first, no faster: %d after %d", next,
          speed);
    advance(&m.rig, 1000);
    CHECK(sh_animator_running(m.rig.animator) == 0 && m.surface->width == 200, "back: %d",
          m.surface->width);
    teardown(&m.rig);
}

static void test_morph_rest_and_finish(void) {
    struct morph_rig m;
    morph_setup(&m, config(100, SH_CURVE_LINEAR));
    morph_to(&m, -100, 0, 400, 300);
    advance(&m.rig, 20);
    client_draws(&m, 400, 300, true);
    advance(&m.rig, 20);
    struct wlr_scene_tree *copy = fading_copy(&m);
    CHECK(copy && copy->node.enabled, "copy shown");
    // Input sees the window where it will be, at the size the client drew, its frame (the
    // window's controls) around that, and no copy.
    sh_animator_rest(m.rig.animator);
    CHECK(m.rig.content->node.x == 0 && m.surface->width == 400, "at rest for input");
    CHECK(m.heard_width == 0 && m.frame->width == 404, "the frame too: %d", m.frame->width);
    CHECK(copy && !copy->node.enabled, "copy hidden");
    sh_animator_resume(m.rig.animator);
    CHECK(m.rig.content->node.x < 0 && m.surface->width < 400 && copy && copy->node.enabled,
          "drawn again");
    CHECK(m.heard_width == m.surface->width && m.frame->width == m.surface->width + 4,
          "the frame around it: %d", m.frame->width);
    // A lookup far from where it is drawn and from where it rests leaves it alone; one where it
    // rests and is not drawn puts it at rest.
    int drawn = m.surface->width, heard = m.heard;
    sh_animator_rest_at(m.rig.animator, 5000, 5000);
    CHECK(m.surface->width == drawn && m.heard == heard && !m.rig.anim.rested,
          "left alone far away");
    sh_animator_resume(m.rig.animator);
    sh_animator_rest_at(m.rig.animator, 395, 295);
    CHECK(m.surface->width == 400 && m.rig.anim.rested, "at rest where it will be");
    sh_animator_resume(m.rig.animator);
    // Finishing lands it, and the frame is drawn at rest.
    sh_anim_finish(&m.rig.anim);
    CHECK(sh_animator_running(m.rig.animator) == 0 && !fading_copy(&m), "landed");
    CHECK(m.surface->width == 400 && m.rig.content->node.x == 0 && m.heard_width == 0 &&
              m.frame->width == 404,
          "at rest");
    teardown(&m.rig);
}

/* A buffer of no pixels, as a client's for the scene. */
static void test_buffer_destroy(struct wlr_buffer *buffer) {
    free(buffer);
}
static const struct wlr_buffer_impl test_buffer_impl = {.destroy = test_buffer_destroy};
static struct wlr_scene_buffer *client_buffer(struct wlr_scene_tree *parent, int width, int height,
                                              struct wlr_box opaque) {
    struct wlr_buffer *buffer = calloc(1, sizeof(*buffer));
    wlr_buffer_init(buffer, &test_buffer_impl, width, height);
    struct wlr_scene_buffer *node = wlr_scene_buffer_create(parent, buffer);
    wlr_buffer_drop(buffer); // the node holds it
    wlr_scene_buffer_set_dest_size(node, width, height);
    pixman_region32_t region;
    pixman_region32_init_rect(&region, opaque.x, opaque.y, opaque.width, opaque.height);
    wlr_scene_buffer_set_opaque_region(node, &region);
    pixman_region32_fini(&region);
    return node;
}
static bool opaque_is(struct wlr_scene_buffer *buffer, struct wlr_box box) {
    if (wlr_box_empty(&box))
        return !pixman_region32_not_empty(&buffer->opaque_region);
    pixman_box32_t *extents = pixman_region32_extents(&buffer->opaque_region);
    return pixman_region32_n_rects(&buffer->opaque_region) == 1 && extents->x1 == box.x &&
           extents->y1 == box.y && extents->x2 == box.x + box.width &&
           extents->y2 == box.y + box.height;
}

/* A buffer drawn at another size than its own has no opaque region meanwhile: the scene would
 * take it unscaled, and what lies under its translucent edges (a client-side shadow) would go
 * undrawn. The copy that fades out has none either. */
static void test_morph_sets_opaque_regions_aside(void) {
    struct morph_rig m;
    morph_setup(&m, config(100, SH_CURVE_LINEAR));
    struct wlr_box body = {20, 20, 160, 60}, none = {0};
    struct wlr_scene_buffer *buffer = client_buffer(m.surfaces, 200, 100, body);
    morph_to(&m, 0, 0, 400, 300);
    CHECK(opaque_is(buffer, body), "at its own size, its region stays");
    advance(&m.rig, 50);
    CHECK(buffer->dst_width == 300 && opaque_is(buffer, none), "stretched, none");
    // The client draws its new size, with a region of its own, as the scene gives it.
    struct wlr_box bigger = {20, 20, 360, 260};
    sh_anim_morph_commit(m.rig.animator, &m.rig.anim, 400, 300, true);
    CHECK(opaque_is(buffer, body), "at rest for the commit, its old region is back");
    struct wlr_scene_tree *copy = fading_copy(&m);
    struct wlr_scene_buffer *old = NULL;
    struct wlr_scene_node *node;
    wl_list_for_each(node, copy ? &copy->children : &m.surfaces->children, link) {
        if (copy && node->type == WLR_SCENE_NODE_BUFFER)
            old = wlr_scene_buffer_from_node(node);
    }
    CHECK(old && opaque_is(old, none), "the copy of the old picture hides nothing");
    wlr_scene_rect_set_size(m.surface, 400, 300);
    wlr_scene_buffer_set_dest_size(buffer, 400, 300);
    pixman_region32_t region;
    pixman_region32_init_rect(&region, bigger.x, bigger.y, bigger.width, bigger.height);
    wlr_scene_buffer_set_opaque_region(buffer, &region);
    pixman_region32_fini(&region);
    advance(&m.rig, 10);
    CHECK(buffer->dst_width == 320 && opaque_is(buffer, none), "shrunk to the size drawn, none");
    advance(&m.rig, 200);
    CHECK(sh_animator_running(m.rig.animator) == 0, "done");
    CHECK(buffer->dst_width == 400 && opaque_is(buffer, bigger), "at rest, the client's new one");
    teardown(&m.rig);
}

static void test_morph_off_and_with_open(void) {
    struct morph_rig m;
    struct sh_animator_config c = config(100, SH_CURVE_LINEAR);
    c.enabled = false;
    morph_setup(&m, c);
    morph_to(&m, 30, 0, 400, 300);
    CHECK(sh_animator_running(m.rig.animator) == 0 && m.heard == 0 && m.surface->width == 200 &&
              m.rig.content->node.x == 0,
          "nothing with animations off");
    sh_anim_morph_commit(m.rig.animator, &m.rig.anim, 400, 300, true);
    CHECK(!fading_copy(&m), "nor on the client's answer");
    teardown(&m.rig);
    // Opening and changing size at once: the surfaces are stretched, then scaled with the rest.
    morph_setup(&m, config(100, SH_CURVE_LINEAR));
    sh_anim_open(m.rig.animator, &m.rig.anim, m.rig.content, 0, 0);
    morph_to(&m, 0, 0, 400, 300);
    advance(&m.rig, 50);
    double scale = 0.94 + 0.06 * 0.5;
    CHECK(abs(m.surface->width - (int)lround(300 * scale)) <= 1, "both: %d", m.surface->width);
    CHECK(m.surface->color[3] > 0.4F && m.surface->color[3] < 0.6F, "fading in");
    advance(&m.rig, 400); // the client had its time
    advance(&m.rig, 200);
    CHECK(sh_animator_running(m.rig.animator) == 0 && m.surface->width == 200 &&
              fabsf(m.surface->color[3] - 1) < 1e-6F,
          "both end at rest");
    teardown(&m.rig);
}

/* A window changing size sent to the scratchpad: the change of size lands where it is and the
 * slide goes on from there, its surfaces at the size the client drew and its frame around it. */
static void test_morph_lands_for_a_drop(void) {
    struct morph_rig m;
    morph_setup(&m, config(100, SH_CURVE_LINEAR));
    morph_to(&m, 0, 0, 400, 300);
    advance(&m.rig, 30);
    CHECK(m.surface->width > 200, "stretched");
    sh_anim_drop_out(m.rig.animator, &m.rig.anim, m.rig.content, 0, -500);
    int width, height;
    CHECK(!sh_anim_drawn_size(&m.rig.anim, &width, &height) && !m.rig.anim.morph, "landed");
    CHECK(m.surface->width == 200 && m.heard_width == 0 && m.frame->width == 204,
          "at the client's size, framed for it: %d", m.surface->width);
    CHECK(sh_animator_running(m.rig.animator) == 1 && m.rig.anim.absent, "sliding out");
    advance(&m.rig, 50);
    CHECK(m.surface->width == 200, "the slide changes no size: %d", m.surface->width);
    teardown(&m.rig);
}

static int tracked, tracked_done;
static void count_track(void *data, bool done) {
    ++tracked;
    tracked_done += done;
}

/* The scratchpad's drop-down: in from out of sight in twice the open time, fading over the
 * first 40 % of it; out in 1.5 times the close time, speeding away, fading over the last 40 %. */
static void test_drop_in(void) {
    struct rig rig;
    setup(&rig, config(100, SH_CURVE_EASE_OUT));
    CHECK(sh_anim_drop_in(rig.animator, &rig.anim, rig.content, 0, -500));
    rig.anim.track = count_track;
    tracked = tracked_done = 0;
    CHECK(rig.content->node.y == -500 && rig.rect->color[3] < 0.01F, "out of sight: %d",
          rig.content->node.y);
    CHECK(!rig.anim.absent, "there for lookups");
    advance(&rig, 40);
    // Ease-out over 200 ms: a fifth of the way in time is 1 - 0.8^3 of the way.
    NEAR(rig.content->node.y, -500 * 0.512, 1);
    NEAR(sh_anim_alpha(&rig.anim), 0.5, 0.01);
    NEAR(rig.rect->color[3], 0.5, 0.01);
    advance(&rig, 40);
    CHECK(fabsf(rig.rect->color[3] - 1) < 1e-6F, "faded in by 80 ms: %f", rig.rect->color[3]);
    CHECK(rig.content->node.y < 0, "still sliding: %d", rig.content->node.y);
    CHECK(tracked == 2 && !tracked_done, "tracked each frame: %d", tracked);
    advance(&rig, 120);
    CHECK(rig.content->node.y == 0 && sh_animator_running(rig.animator) == 0, "lands");
    CHECK(tracked == 3 && tracked_done == 1 && !rig.anim.track, "told it is done once");
    teardown(&rig);
}

static void test_drop_out(void) {
    struct rig rig;
    setup(&rig, config(100, SH_CURVE_EASE_OUT));
    double sx, sy;
    CHECK(sh_anim_drop_out(rig.animator, &rig.anim, rig.content, 0, -500));
    rig.anim.track = count_track;
    tracked = tracked_done = 0;
    CHECK(rig.content->node.y == 0 && fabsf(rig.rect->color[3] - 1) < 1e-6F, "from where it rests");
    advance(&rig, 75);
    // Half of 150 ms with the ease-out played backwards: 0.5^3 of the way.
    NEAR(rig.content->node.y, -500 * 0.125, 1);
    CHECK(fabsf(rig.rect->color[3] - 1) < 1e-6F, "not fading yet: %f", rig.rect->color[3]);
    // Drawn where the pointer is, it is not there for it.
    sh_animator_rest_at(rig.animator, 50, 50);
    CHECK(wlr_scene_node_at(&rig.scene->tree.node, 50, 50, &sx, &sy) == NULL, "absent");
    sh_animator_resume(rig.animator);
    CHECK(rig.content->node.enabled && rig.content->node.y < -50, "drawn again after");
    advance(&rig, 45);
    NEAR(rig.content->node.y, -500 * 0.512, 1);
    NEAR(rig.rect->color[3], 0.5, 0.01);
    advance(&rig, 30);
    // Its owner hides the window as it is told the slide is done; the tree is where it went.
    CHECK(sh_animator_running(rig.animator) == 0 && tracked_done == 1, "done");
    CHECK(rig.content->node.y == -500 && !rig.anim.absent, "out of sight: %d", rig.content->node.y);
    CHECK(fabsf(rig.rect->color[3] - 1) < 1e-6F, "its look back at rest");
    teardown(&rig);
}

static void test_drop_turns_back(void) {
    struct rig rig;
    setup(&rig, config(100, SH_CURVE_EASE_OUT));
    sh_anim_drop_in(rig.animator, &rig.anim, rig.content, 0, -500);
    advance(&rig, 30);
    advance(&rig, 1);
    int y = rig.content->node.y;
    float alpha = sh_anim_alpha(&rig.anim);
    CHECK(y > -400 && y < -100 && alpha > 0.2F && alpha < 0.6F, "on its way in: %d, %f", y, alpha);
    // Hidden again half way: it carries on a little, as fast as it went, then turns back.
    CHECK(sh_anim_drop_out(rig.animator, &rig.anim, rig.content, 0, -500));
    CHECK(rig.content->node.y == y, "from where it is drawn: %d, not %d", rig.content->node.y, y);
    NEAR(sh_anim_alpha(&rig.anim), alpha, 0.01);
    advance(&rig, 1);
    CHECK(rig.content->node.y > y, "with the speed it had: %d after %d", rig.content->node.y, y);
    // Some of it: it turns within a few pixels rather than carrying on far.
    int lowest = rig.content->node.y;
    for (int i = 0; i < 60; ++i) {
        advance(&rig, 1);
        lowest = rig.content->node.y > lowest ? rig.content->node.y : lowest;
    }
    CHECK(lowest < y + 25 && rig.content->node.y < lowest, "turned: %d after %d, now %d", lowest, y,
          rig.content->node.y);
    advance(&rig, 39);
    int out = rig.content->node.y;
    CHECK(out < y, "then away: %d", out);
    // And shown again on its way out.
    alpha = sh_anim_alpha(&rig.anim);
    CHECK(sh_anim_drop_in(rig.animator, &rig.anim, rig.content, 0, -500));
    CHECK(rig.content->node.y == out, "from where it is drawn: %d", rig.content->node.y);
    NEAR(sh_anim_alpha(&rig.anim), alpha, 0.01);
    advance(&rig, 200);
    CHECK(rig.content->node.y == 0 && sh_animator_running(rig.animator) == 0 &&
              fabsf(rig.rect->color[3] - 1) < 1e-6F,
          "lands at rest");
    // Another animation taking the tree over tells the owner, as finishing does.
    sh_anim_drop_in(rig.animator, &rig.anim, rig.content, 0, -500);
    rig.anim.track = count_track;
    tracked = tracked_done = 0;
    sh_anim_glide(rig.animator, &rig.anim, rig.content, 0, 40);
    CHECK(tracked_done == 1 && !rig.anim.track, "released as a glide takes over");
    sh_anim_drop_out(rig.animator, &rig.anim, rig.content, 0, -500);
    rig.anim.track = count_track;
    sh_anim_finish(&rig.anim);
    CHECK(tracked_done == 2 && rig.content->node.y == -500 && !rig.anim.absent,
          "finished where it slid to");
    teardown(&rig);
}

/* A window sliding out is not there for lookups, even where it slides to; once another
 * animation takes it over, it is there again where it rests, at its full look. */
static void test_drop_absent(void) {
    struct rig rig;
    setup(&rig, config(100, SH_CURVE_EASE_OUT));
    double sx, sy;
    // Out by 30 only: where it slides to still covers (50, 60), as where it rests does.
    CHECK(sh_anim_drop_out(rig.animator, &rig.anim, rig.content, 0, 30));
    advance(&rig, 120);
    CHECK(rig.rect->color[3] < 0.9F, "fading out: %f", rig.rect->color[3]);
    sh_animator_rest_at(rig.animator, 50, 60);
    CHECK(wlr_scene_node_at(&rig.scene->tree.node, 50, 60, &sx, &sy) == NULL, "absent");
    sh_animator_resume(rig.animator);
    CHECK(rig.content->node.enabled, "drawn again after the lookup");
    sh_anim_glide(rig.animator, &rig.anim, rig.content, 5, 0);
    CHECK(!rig.anim.absent && rig.anim.base_x == 0 && rig.anim.base_y == 0, "there again");
    CHECK(fabsf(rig.rect->color[3] - 1) < 1e-6F, "at its full look: %f", rig.rect->color[3]);
    sh_animator_rest_at(rig.animator, 50, 60);
    CHECK(wlr_scene_node_at(&rig.scene->tree.node, 50, 60, &sx, &sy) == &rig.rect->node,
          "found where it rests");
    sh_animator_resume(rig.animator);
    advance(&rig, 200);
    CHECK(rig.content->node.x == 0 && rig.content->node.y == 0 &&
              fabsf(rig.rect->color[3] - 1) < 1e-6F && sh_animator_running(rig.animator) == 0,
          "lands where it rests: %d, %d", rig.content->node.x, rig.content->node.y);
    teardown(&rig);
}

static void test_drop_off(void) {
    struct rig rig;
    struct sh_animator_config c = config(100, SH_CURVE_EASE_OUT);
    c.styles[SH_ANIM_CLOSE].duration = 0;
    setup(&rig, c);
    CHECK(sh_anim_drop_in(rig.animator, &rig.anim, rig.content, 0, -500));
    advance(&rig, 50);
    // Closing does not animate: the drop under way lands where the window rests.
    CHECK(!sh_anim_drop_out(rig.animator, &rig.anim, rig.content, 0, -500));
    CHECK(rig.content->node.y == 0 && sh_animator_running(rig.animator) == 0 &&
              fabsf(rig.rect->color[3] - 1) < 1e-6F,
          "at rest: %d", rig.content->node.y);
    c.enabled = false;
    sh_animator_configure(rig.animator, &c);
    CHECK(!sh_anim_drop_in(rig.animator, &rig.anim, rig.content, 0, -500) &&
              rig.content->node.y == 0,
          "animations off: nothing moves");
    // Speed scales a drop as it does everything else.
    c = config(100, SH_CURVE_LINEAR);
    c.speed = 2;
    sh_animator_configure(rig.animator, &c);
    sh_anim_drop_in(rig.animator, &rig.anim, rig.content, 0, -500);
    advance(&rig, 50);
    CHECK(rig.content->node.y == -250, "half way at 50 of 100 ms: %d", rig.content->node.y);
    advance(&rig, 50);
    CHECK(rig.content->node.y == 0 && sh_animator_running(rig.animator) == 0, "lands at 100 ms");
    teardown(&rig);
}

/* What the owner of a lifted window changing size does as it hears of the size drawn: the frame
 * around it, and the lift's bounds that size. */
static void lift_morph_resized(void *data) {
    struct morph_rig *m = data;
    morph_resized(m);
    int width = m->surface->width, height = m->surface->height;
    sh_anim_drawn_size(&m->rig.anim, &width, &height);
    sh_anim_lift_bounds(&m->rig.anim, 0, 0, width, height);
}

/* A lift draws on top of a change of size: the size drawn grows, and the frame keeps its width
 * around it; let down, the change of size goes on as it would have. */
static void test_lift_over_morph(void) {
    struct morph_rig m;
    morph_setup(&m, config(100, SH_CURVE_LINEAR));
    sh_anim_lift_bounds(&m.rig.anim, 0, 0, 200, 100);
    sh_anim_lift(m.rig.animator, &m.rig.anim, m.rig.content, true, 1.1, 0, 0, NULL, NULL);
    for (int i = 0; i < 6; ++i)
        advance(&m.rig, 20);
    CHECK(m.surface->width == 220 && m.frame->width == 224 && m.frame->node.x == -2,
          "lifted: %d in %d", m.surface->width, m.frame->width);
    struct sh_morph morph = {
        .surfaces = m.surfaces, .from_width = 200, .from_height = 100, .width = 400,
        .height = 300, .live_width = 200, .live_height = 100, .kind = SH_ANIM_MOVE,
        .resized = lift_morph_resized, .data = &m,
    };
    sh_anim_morph(m.rig.animator, &m.rig.anim, m.rig.content, &morph);
    CHECK(sh_animator_lifted(m.rig.animator) == 1, "still lifted as it changes size");
    advance(&m.rig, 25);
    advance(&m.rig, 25);
    CHECK(m.surface->width == 330 && m.surface->height == 220,
          "half way to 400 by 300, lifted: %d by %d", m.surface->width, m.surface->height);
    CHECK(m.frame->node.x == -2 && m.frame->width == 334 && m.frame->height == 224,
          "the frame as wide around it: %d by %d at %d", m.frame->width, m.frame->height,
          m.frame->node.x);
    sh_anim_lift(m.rig.animator, &m.rig.anim, m.rig.content, false, 1.1, 0, 0, NULL, NULL);
    for (int i = 0; i < 10; ++i)
        advance(&m.rig, 20);
    CHECK(sh_animator_lifted(m.rig.animator) == 0 && m.surface->width == 400 &&
              m.surface->height == 300 && m.frame->width == 404,
          "let down, at the size it changes to: %d by %d", m.surface->width, m.surface->height);
    client_draws(&m, 400, 300, true);
    for (int i = 0; i < 10; ++i)
        advance(&m.rig, 20);
    CHECK(sh_animator_running(m.rig.animator) == 0 && m.surface->width == 400 &&
              m.frame->width == 404,
          "done: %d", m.surface->width);
    teardown(&m.rig);
}

static void test_finish_all(void) {
    struct rig rig;
    setup(&rig, config(300, SH_CURVE_EASE_OUT));
    sh_anim_glide(rig.animator, &rig.anim, rig.content, 300, 0);
    sh_anim_close(rig.animator, &rig.window->node, rig.content, 100, 50);
    CHECK(sh_animator_running(rig.animator) == 2, "two running");
    sh_animator_finish_all(rig.animator);
    CHECK(sh_animator_running(rig.animator) == 0 && rig.content->node.x == 0, "all landed");
    teardown(&rig);
}

int main(void) {
    test_glide_reaches_rest();
    test_retarget_is_continuous();
    test_retarget_keeps_velocity();
    test_unseen_glide_carries_nothing();
    test_glide_kind_uses_its_own_style();
    test_speed_and_disabled();
    test_open_restores_rest_values();
    test_open_keeps_neighbours_adjoined();
    test_close_leaves_nothing();
    test_late_frame_skips();
    test_rest_for_input();
    test_rest_at_point();
    test_tween_follows_target();
    test_workspace_slide();
    test_hold_and_slide_from();
    test_hold_copy();
    test_lift();
    test_lift_with_others();
    test_lift_bounds();
    test_lift_through_gestures();
    test_lift_rest_at_point();
    test_lift_over_morph();
    test_morph_stretches_and_waits();
    test_morph_answer_after_an_early_ack();
    test_morph_follows_every_answer();
    test_morph_crossfades_the_answer();
    test_morph_late_answer_at_drawn_size();
    test_morph_answer_of_its_own();
    test_morph_keeps_one_fade();
    test_morph_reverses_smoothly();
    test_morph_rest_and_finish();
    test_morph_off_and_with_open();
    test_morph_sets_opaque_regions_aside();
    test_morph_lands_for_a_drop();
    test_drop_in();
    test_drop_out();
    test_drop_turns_back();
    test_drop_absent();
    test_drop_off();
    test_finish_all();
    if (!failures)
        puts("Animator timing, retargeting, speed, disabling, and late frames passed");
    return failures ? 1 : 0;
}
