// SPDX-License-Identifier: GPL-3.0-or-later
/* Drives the animator on a bare scene graph with a fake clock: no renderer or display. */
#include "paw/animation.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
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

static void count_fold(const struct sh_anim *anim, void *data) {
    ++updates;
}

/* rig.rect's box, folding into a 40 by 40 button below and to the right of it: it fits the button
 * at a fifth of its size. */
static struct sh_fold button_fold(void (*update)(const struct sh_anim *, void *), void *data) {
    return (struct sh_fold){10, 20, 200, 100, 500, 400, 40, 40, update, data};
}

/* Where a rect folding as button_fold has it should be `at` of the way: its middle on the line
 * from its own to the button's, its size shrinking evenly to a fifth. */
static bool folded_at(const struct wlr_scene_rect *rect, int ox, int oy, double at) {
    double scale = 1 - 0.8 * at;
    double x = 110 + 410 * at, y = 70 + 350 * at;
    double width = 200 * scale, height = 100 * scale;
    return fabs(ox + rect->node.x - (x - width / 2)) <= 1 && fabs(oy + rect->node.y - (y - height / 2)) <= 1 &&
           fabs(rect->width - width) <= 1 && fabs(rect->height - height) <= 1;
}

/* A copy of a window shrinks into its button, fading out, and goes. */
static void test_fold_away(void) {
    struct rig rig;
    setup(&rig, config(100, SH_CURVE_LINEAR));
    int before = wl_list_length(&rig.scene->tree.children);
    struct wlr_scene_tree *copy = wlr_scene_tree_create(&rig.scene->tree);
    float color[4] = {0.2F, 0.4F, 0.6F, 1};
    struct wlr_scene_rect *shown = wlr_scene_rect_create(copy, 200, 100, color);
    wlr_scene_node_set_position(&shown->node, 10, 20);
    struct sh_anim *ref = NULL;
    updates = 0;
    struct sh_fold fold = button_fold(count_fold, NULL);
    struct sh_anim *anim = sh_anim_fold_away(rig.animator, copy, &fold, 0, &ref);
    CHECK(anim && ref == anim && sh_animator_running(rig.animator) == 1, "the copy folds");
    CHECK(folded_at(shown, 0, 0, 0) && shown->color[3] > 0.99F, "starts where the window is");
    CHECK(sh_anim_folded(anim, NULL) == 0, "none of the way yet");
    advance(&rig, 50);
    CHECK(folded_at(shown, 0, 0, 0.5), "half way along, at %d,%d %dx%d", shown->node.x,
          shown->node.y, shown->width, shown->height);
    CHECK(fabsf(shown->color[3] - 0.75F) < 0.01F, "still mostly there half way: %f",
          shown->color[3]);
    CHECK(fabs(sh_anim_folded(anim, NULL) - 0.5) < 1e-9, "half way in");
    advance(&rig, 49);
    CHECK(folded_at(shown, 0, 0, 0.99) && shown->color[3] < 0.03F, "at the button, nearly gone");
    CHECK(updates == 3, "the owner redrew at every step: %d", updates);
    advance(&rig, 2);
    CHECK(!ref && sh_animator_running(rig.animator) == 0, "done, and the owner told");
    CHECK(wl_list_length(&rig.scene->tree.children) == before, "the copy is gone");
    // Without animations, or without a box to go to, the copy goes at once.
    struct sh_animator_config c = config(100, SH_CURVE_LINEAR);
    c.styles[SH_ANIM_MINIMIZE].duration = 0;
    sh_animator_configure(rig.animator, &c);
    copy = wlr_scene_tree_create(&rig.scene->tree);
    CHECK(!sh_anim_fold_away(rig.animator, copy, &fold, 0, &ref) && !ref &&
              wl_list_length(&rig.scene->tree.children) == before,
          "off: no fold, no copy");
    sh_animator_configure(rig.animator, &(struct sh_animator_config){.enabled = false});
    CHECK(!sh_anim_fold_away(rig.animator, wlr_scene_tree_create(&rig.scene->tree), &fold, 0, &ref),
          "disabled: no fold");
    sh_animator_configure(rig.animator, &(struct sh_animator_config){.enabled = true, .speed = 1});
    struct sh_animator_config on = config(100, SH_CURVE_LINEAR);
    sh_animator_configure(rig.animator, &on);
    struct sh_fold empty = fold;
    empty.into_width = 0;
    CHECK(!sh_anim_fold_away(rig.animator, wlr_scene_tree_create(&rig.scene->tree), &empty, 0, &ref) &&
              wl_list_length(&rig.scene->tree.children) == before,
          "an empty box: no fold");
    // One that started part of the way takes the time that part takes.
    copy = wlr_scene_tree_create(&rig.scene->tree);
    shown = wlr_scene_rect_create(copy, 200, 100, color);
    wlr_scene_node_set_position(&shown->node, 10, 20);
    sh_anim_fold_away(rig.animator, copy, &fold, 0.75, &ref);
    CHECK(ref && folded_at(shown, 0, 0, 0.75), "starts three quarters of the way");
    advance(&rig, 49);
    CHECK(ref != NULL, "running for half the time");
    advance(&rig, 2);
    CHECK(!ref, "the last quarter takes half the time");
    // A copy still folding when the animator goes goes with it.
    copy = wlr_scene_tree_create(&rig.scene->tree);
    wlr_scene_rect_create(copy, 200, 100, color);
    sh_anim_fold_away(rig.animator, copy, &fold, 0, &ref);
    teardown(&rig);
    CHECK(!ref, "told as the animator goes");
}

static const struct sh_anim *mapped_anim;
static int mapped_wrong;
static void check_mapped(const struct sh_anim *anim, void *data) {
    const struct wlr_scene_rect *rect = data;
    int x = 10, y = 20, width = 200, height = 100, radius = 10;
    mapped_wrong += anim != mapped_anim;
    sh_anim_fold_map(anim, &x, &y, &width, &height, &radius);
    mapped_wrong += x != rect->node.x || y != rect->node.y || width != rect->width ||
                    height != rect->height;
    ++updates;
}

/* A window grows out of its button back to rest; one turned back on its way into the button goes
 * back from where it is drawn. */
static void test_unfold(void) {
    struct rig rig;
    setup(&rig, config(100, SH_CURVE_LINEAR));
    mapped_anim = &rig.anim;
    mapped_wrong = updates = 0;
    struct sh_fold fold = button_fold(check_mapped, rig.rect);
    CHECK(sh_anim_unfold(rig.animator, &rig.anim, rig.content, &fold, 1), "it unfolds");
    CHECK(folded_at(rig.rect, 0, 0, 1) && rig.rect->node.x == 500 && rig.rect->width == 40,
          "starts fitting the button: %d %d", rig.rect->node.x, rig.rect->width);
    CHECK(rig.rect->color[3] < 0.01F, "and unseen");
    advance(&rig, 50);
    CHECK(folded_at(rig.rect, 0, 0, 0.5) && fabsf(rig.rect->color[3] - 0.75F) < 0.01F,
          "half way out: %d %f", rig.rect->node.x, rig.rect->color[3]);
    // Input finds it at rest.
    sh_animator_rest_at(rig.animator, 900, 900);
    CHECK(rig.rect->node.x == 10 && rig.rect->width == 200, "at rest for any lookup");
    sh_animator_resume(rig.animator);
    CHECK(folded_at(rig.rect, 0, 0, 0.5), "and drawn on its way again");
    advance(&rig, 51);
    CHECK(rig.rect->node.x == 10 && rig.rect->node.y == 20 && rig.rect->width == 200 &&
              rig.rect->height == 100 && fabsf(rig.rect->color[3] - 1) < 1e-6F &&
              fabsf(rig.rect->color[0] - 0.2F) < 1e-6F,
          "at rest");
    CHECK(sh_animator_running(rig.animator) == 0 && sh_anim_folded(&rig.anim, NULL) < 0, "done");
    CHECK(updates == 4 && mapped_wrong == 0, "the owner redrew what the fold maps, as drawn: %d %d",
          updates, mapped_wrong);

    // Minimizing turned back: the copy going into the button is replaced by the window coming
    // out of it from the very place the copy was drawn.
    struct wlr_scene_tree *copy = wlr_scene_tree_create(&rig.scene->tree);
    float color[4] = {0.2F, 0.4F, 0.6F, 1};
    struct wlr_scene_rect *shown = wlr_scene_rect_create(copy, 200, 100, color);
    wlr_scene_node_set_position(&shown->node, 10, 20);
    struct sh_anim *ref = NULL;
    struct sh_fold plain = button_fold(NULL, NULL);
    sh_anim_fold_away(rig.animator, copy, &plain, 0, &ref);
    advance(&rig, 30);
    struct sh_fold taken;
    double at = sh_anim_folded(ref, &taken);
    CHECK(fabs(at - 0.3) < 1e-9 && taken.into_x == 500, "three tenths of the way in");
    int x = shown->node.x, width = shown->width;
    float alpha = shown->color[3];
    sh_anim_finish(ref);
    CHECK(!ref, "the copy goes");
    sh_anim_unfold(rig.animator, &rig.anim, rig.content, &taken, at);
    CHECK(rig.rect->node.x == x && rig.rect->width == width && rig.rect->color[3] == alpha,
          "the window takes over where the copy was: %d %d %f", rig.rect->node.x,
          rig.rect->width, rig.rect->color[3]);
    // Turned back after 3 tenths, it takes the time a spring would: sqrt(0.3) of the whole.
    advance(&rig, 54);
    CHECK(sh_animator_running(rig.animator) == 1 && rig.rect->width > 190, "nearly back");
    advance(&rig, 2);
    CHECK(sh_animator_running(rig.animator) == 0 && rig.rect->width == 200, "back");

    // A glide goes on beneath the fold, and settling ends the fold alone.
    sh_anim_glide(rig.animator, &rig.anim, rig.content, 300, 0);
    sh_anim_unfold(rig.animator, &rig.anim, rig.content, &plain, 1);
    advance(&rig, 20);
    CHECK(rig.content->node.x > 0 && folded_at(rig.rect, 0, 0, 0.8), "gliding while folded");
    at = sh_anim_settle(&rig.anim);
    CHECK(fabs(at - 0.8) < 1e-9 && rig.rect->width == 200 && fabsf(rig.rect->color[3] - 1) < 1e-6F,
          "settled at rest from 0.8: %f", at);
    CHECK(rig.content->node.x > 0 && sh_animator_running(rig.animator) == 1, "still gliding");
    advance(&rig, 100);
    CHECK(rig.content->node.x == 0 && sh_animator_running(rig.animator) == 0, "glide over");
    CHECK(sh_anim_settle(&rig.anim) == 0, "nothing to settle");

    // A slide (a workspace switch) takes over a fold from rest.
    sh_anim_unfold(rig.animator, &rig.anim, rig.content, &plain, 1);
    advance(&rig, 30);
    sh_anim_slide(rig.animator, &rig.anim, rig.content, 40, 0);
    advance(&rig, 200);
    CHECK(rig.rect->node.x == 10 && rig.rect->width == 200 && fabsf(rig.rect->color[3] - 1) < 1e-6F,
          "rest values survive a fold cut short");
    // Finishing lands it, as does turning animations off; neither starts it.
    sh_anim_unfold(rig.animator, &rig.anim, rig.content, &plain, 1);
    sh_anim_finish(&rig.anim);
    CHECK(rig.rect->width == 200 && fabsf(rig.rect->color[3] - 1) < 1e-6F, "finished at rest");
    CHECK(!sh_anim_unfold(rig.animator, &rig.anim, rig.content, &plain, 0), "at rest already");
    sh_animator_configure(rig.animator, &(struct sh_animator_config){.enabled = false});
    CHECK(!sh_anim_unfold(rig.animator, &rig.anim, rig.content, &plain, 1) &&
              rig.rect->width == 200,
          "off: at rest at once");
    teardown(&rig);
}

/* A tween's owner redrawing the window at rest on each frame (its focus fade) does so before the
 * animations draw it where they have it, so that the frame shows it where the fold has it. */
static struct wlr_scene_rect *redrawn;
static void redraw_at_rest(void *data) {
    wlr_scene_node_set_position(&redrawn->node, 10, 20);
    wlr_scene_rect_set_size(redrawn, 200, 100);
}

static void test_tween_redraws_under_a_fold(void) {
    struct rig rig;
    setup(&rig, config(100, SH_CURVE_LINEAR));
    redrawn = rig.rect;
    struct sh_tween tween = {0};
    float target[SH_TWEEN_VALUES] = {1}, current[SH_TWEEN_VALUES];
    sh_tween_track(rig.animator, &tween, SH_ANIM_FOCUS, true, target, current, redraw_at_rest, NULL);
    target[0] = 0;
    sh_tween_track(rig.animator, &tween, SH_ANIM_FOCUS, true, target, current, redraw_at_rest, NULL);
    struct sh_fold fold = button_fold(NULL, NULL);
    sh_anim_unfold(rig.animator, &rig.anim, rig.content, &fold, 1);
    advance(&rig, 50);
    CHECK(sh_animator_tweens(rig.animator) == 1 && folded_at(rig.rect, 0, 0, 0.5),
          "drawn half way out after its owner redrew it: %d %d", rig.rect->node.x,
          rig.rect->width);
    advance(&rig, 60);
    CHECK(rig.rect->width == 200 && rig.rect->node.x == 10, "at rest after");
    sh_tween_stop(&tween);
    teardown(&rig);
}

/* Landing what runs on a window lands its glide and fade, but lets a fold go on. */
static void test_land_keeps_a_fold(void) {
    struct rig rig;
    setup(&rig, config(100, SH_CURVE_LINEAR));
    struct sh_fold fold = button_fold(NULL, NULL);
    sh_anim_glide(rig.animator, &rig.anim, rig.content, 300, 0);
    sh_anim_unfold(rig.animator, &rig.anim, rig.content, &fold, 1);
    advance(&rig, 50);
    sh_anim_land(&rig.anim);
    CHECK(rig.content->node.x == 0 && folded_at(rig.rect, 0, 0, 0.5) &&
              sh_animator_running(rig.animator) == 1,
          "the glide landed, the fold half way still: %d %d", rig.content->node.x, rig.rect->width);
    advance(&rig, 51);
    CHECK(rig.rect->width == 200 && sh_animator_running(rig.animator) == 0, "the fold ends at rest");
    sh_anim_open(rig.animator, &rig.anim, rig.content, 100, 50);
    sh_anim_land(&rig.anim);
    CHECK(rig.rect->width == 200 && fabsf(rig.rect->color[3] - 1) < 1e-6F &&
              sh_animator_running(rig.animator) == 0,
          "an opening lands at once");
    teardown(&rig);
}

int main(void) {
    test_glide_reaches_rest();
    test_retarget_is_continuous();
    test_retarget_keeps_velocity();
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
    test_finish_all();
    test_fold_away();
    test_unfold();
    test_tween_redraws_under_a_fold();
    test_land_keeps_a_fold();
    if (!failures)
        puts("Animator timing, retargeting, speed, disabling, and late frames passed");
    return failures ? 1 : 0;
}
