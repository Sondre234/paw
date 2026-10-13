// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* Short window animations on the plain wlroots scene graph: a window fades in and grows
 * slightly when it opens, a snapshot of its last frame fades out and shrinks when it closes,
 * and tiles glide to their new place when the layout changes.
 *
 * Each animation owns one scene tree and changes only what is below it: the tree's own
 * position (the glide), and the position, destination size, and opacity (or color, for
 * rectangles) of every buffer and rectangle inside it. The scene has no transform for a whole
 * subtree, so scaling moves and resizes each buffer about a common center. Clients and the
 * rest of the compositor keep writing to those nodes while an animation runs; a value that
 * differs from what the animation last set is taken as the node's new resting value. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include "paw/curve.h"

struct wlr_scene_node;
struct wlr_scene_tree;
struct sh_animator;
struct sh_anim_record;

/* One window's running animation, embedded in the window and zeroed with it. */
struct sh_anim {
    struct sh_animator *animator; // set while it runs
    struct wlr_scene_tree *tree;
    struct wl_list link;
    bool owned; // a closing snapshot: the tree and this struct are freed when it ends
    bool fx, glide;
    bool rested; // sh_animator_rest_at put it at rest; sh_animator_resume takes it back
    int64_t fx_start, glide_start; // milliseconds, CLOCK_MONOTONIC
    int fx_duration, glide_duration;
    struct sh_curve fx_curve, glide_curve;
    float scale_from, scale_to, alpha_from, alpha_to;
    double cx, cy; // scale center, in tree coordinates
    /* The glide's offset and velocity (pixels per millisecond) when it started; the offset
     * ends at 0, 0. A retargeted glide starts from the velocity the last one had. */
    double glide_x, glide_y, glide_vx, glide_vy;
    int base_x, base_y; // where the tree rests: 0, 0, except a snapshot sliding away
    /* Held by a gesture (sh_anim_hold): the tree's offset from where it rests and the factor of
     * its nodes' opacity, which stay as they are whatever the time. */
    bool held;
    double held_x, held_y;
    float held_alpha;
    /* Lifted while dragged (sh_anim_lift): drawn up to `lift_scale` times its size about
     * (lift_cx, lift_cy), on top of whatever else runs on it; with bounds (lift_bounds: x, y,
     * width, height; no width for none), only what is inside them grows, and what is outside
     * moves with their edges. `lift` is how far up it is drawn (0 down, 1 up), easing from
     * `lift_from` toward `lift_to` since lift_start, with the velocity it had then (per
     * millisecond); it stays up, what the client draws included, until it is let down. In the
     * animator's lifted list while not down. */
    bool lifted, lift_easing, lift_rested;
    struct wl_list lift_link;
    double lift, lift_from, lift_to, lift_velocity, lift_scale, lift_cx, lift_cy;
    int lift_bounds[4];
    int64_t lift_start;
    int lift_duration;
    struct sh_curve lift_curve;
    void (*lift_update)(void *data);
    void *lift_data;
    struct sh_anim_record *records;
    size_t count, capacity;
};

/* Values that follow a target over time, for what a window shows for focus: its opacity and
 * border color. The owner keeps asking for the current value with the target it wants now;
 * when the target changes, the value moves toward it from where it is. `update` runs on each
 * frame while it moves, and is where the owner redraws with sh_tween_track's answer. */
enum { SH_TWEEN_VALUES = 5 };
struct sh_tween {
    struct sh_animator *animator; // set while it moves
    struct wl_list link;
    bool valid;                   // value and to hold something
    float from[SH_TWEEN_VALUES], to[SH_TWEEN_VALUES], value[SH_TWEEN_VALUES];
    int64_t start;
    int duration;
    struct sh_curve curve;
    void (*update)(void *data);
    void *data;
};

struct sh_animator *sh_animator_create(struct wl_event_loop *loop);
/* Ends every animation; closing snapshots are destroyed. Call before the scene goes. */
void sh_animator_destroy(struct sh_animator *animator);
struct sh_animator_config {
    bool enabled;
    float speed; /* multiplies every animation's speed: 2 makes them twice as fast */
    /* A frame later than this many milliseconds finishes what is running instead of
     * stuttering through it; 0 never skips. */
    int late_ms;
    struct sh_anim_style styles[SH_ANIM_KINDS];
};
/* Applies to animations that start afterwards; those running keep the style they began with.
 * Disabling finishes what is running. */
void sh_animator_configure(struct sh_animator *animator, const struct sh_animator_config *config);
/* Replaces the clock (milliseconds, monotonic) for tests; NULL restores the real one. */
void sh_animator_set_clock(struct sh_animator *animator, int64_t (*now)(void *), void *data);
/* Puts every running animation's nodes at their final place, so hit testing and other input
 * sees where windows will be rather than where they are drawn; closing snapshots are hidden.
 * `sh_animator_resume` puts the animations back at the current time. Nothing is rendered in
 * between, so it is invisible; call them around a lookup only. */
void sh_animator_rest(struct sh_animator *animator);
/* As sh_animator_rest for a lookup at (x, y) in layout coordinates: only the animations that
 * could change what is found there are put at rest, since each one costs the scene an update
 * and again on resume. Gliding windows count when they are drawn or will rest over the point;
 * fading and growing ones and closing snapshots count when they might be. */
void sh_animator_rest_at(struct sh_animator *animator, double x, double y);
void sh_animator_resume(struct sh_animator *animator);
/* Jumps every running animation to its end; closing snapshots go. */
void sh_animator_finish_all(struct sh_animator *animator);
/* Advances every animation to the current time; call right before rendering a frame. */
void sh_animator_tick(struct sh_animator *animator);
/* Writes to `current` where the value is now on its way to `target`. A change of target
 * starts a fade of `kind` from the current value, unless `animate` is false, which (or a kind
 * that is off) lands on the target at once. */
void sh_tween_track(struct sh_animator *animator, struct sh_tween *tween, enum sh_anim_kind kind,
                    bool animate, const float target[SH_TWEEN_VALUES],
                    float current[SH_TWEEN_VALUES], void (*update)(void *), void *data);
/* Forgets the tween without calling `update`; call before freeing its owner. */
void sh_tween_stop(struct sh_tween *tween);
size_t sh_animator_tweens(const struct sh_animator *animator);
size_t sh_animator_running(const struct sh_animator *animator);

/* Fades `tree` in while it grows to full size about (cx, cy). */
void sh_anim_open(struct sh_animator *animator, struct sh_anim *anim, struct wlr_scene_tree *tree,
                  double cx, double cy);
/* Offsets `tree` by (dx, dy) from where it now rests and glides it back. */
void sh_anim_glide(struct sh_animator *animator, struct sh_anim *anim, struct wlr_scene_tree *tree,
                   int dx, int dy);
/* As sh_anim_glide, with the duration and curve of `kind`. */
void sh_anim_glide_kind(struct sh_animator *animator, struct sh_anim *anim,
                        struct wlr_scene_tree *tree, int dx, int dy, enum sh_anim_kind kind);
/* A window arriving with its workspace: offsets `tree` by (dx, dy) and glides it back while it
 * fades in. */
void sh_anim_slide(struct sh_animator *animator, struct sh_anim *anim, struct wlr_scene_tree *tree,
                   int dx, int dy);
/* A window leaving with its workspace: a copy of what is visible under `content` slides by
 * (dx, dy) and fades out, just above `window`, which the caller hides. */
void sh_anim_slide_out(struct sh_animator *animator, struct wlr_scene_node *window,
                       struct wlr_scene_tree *content, int dx, int dy);
/* Gestures: a workspace slide held part of the way while fingers move it. A held animation stays
 * where it is put until it is held elsewhere, slid on from there or finished; neither the clock,
 * a late frame nor turning animations off moves it. Holds `tree` offset by (dx, dy) from where
 * it rests, its nodes' opacity multiplied by `alpha`. Whatever ran on `anim` lands first, but
 * for a lift (sh_anim_lift), which goes on over the hold. */
void sh_anim_hold(struct sh_animator *animator, struct sh_anim *anim, struct wlr_scene_tree *tree,
                  double dx, double dy, float alpha);
/* A copy of what is visible under `content` just above `window`, held as sh_anim_hold holds a
 * window, (dx, dy) from where the window is: a hidden window coming into view with a gesture.
 * NULL when there is nothing to copy. sh_anim_hold moves it (with its own tree), and it goes
 * with sh_anim_finish or at the end of sh_anim_slide_away. */
struct sh_anim *sh_anim_hold_copy(struct sh_animator *animator, struct wlr_scene_node *window,
                                  struct wlr_scene_tree *content, double dx, double dy,
                                  float alpha);
/* As sh_anim_slide, from part of the way: `tree` starts offset by (dx, dy) with its opacity
 * multiplied by `alpha`, and comes to rest in `share` (a quarter to all) of the workspace
 * slide's time. A hold on `anim` ends there; a lift goes on over the slide. */
void sh_anim_slide_from(struct sh_animator *animator, struct sh_anim *anim,
                        struct wlr_scene_tree *tree, int dx, int dy, float alpha, double share);
/* A copy from sh_anim_hold_copy slides on from where it is held to (dx, dy) from its window,
 * its opacity to `alpha`, in `share` of the slide's time, then goes. */
void sh_anim_slide_away(struct sh_animator *animator, struct sh_anim *anim, int dx, int dy,
                        float alpha, double share);
/* Lifts `tree` while its window is dragged (`up`), or lets it down again: its nodes grow to
 * `scale` times their size about (cx, cy) in tree coordinates, on top of whatever else runs on
 * it, easing from where they are drawn with the duration and curve of animations.move (half as
 * long again going down, to settle gently), and stay up, what the client draws included, until
 * let down. A lift reversed half way turns back from where it is, at the speed it has. With
 * animations off it goes up and down at once. `update` is called with `data` on each frame the
 * lift moves, before the nodes are scaled, and as it lands, for what follows it (the window's
 * shadow). */
void sh_anim_lift(struct sh_animator *animator, struct sh_anim *anim, struct wlr_scene_tree *tree,
                  bool up, double scale, double cx, double cy, void (*update)(void *),
                  void *data);
/* The box (in tree coordinates) a lift grows: the window's geometry, around which its border and
 * shadow are drawn. What is inside it is scaled to the box drawn larger, its edges on whole
 * pixels; what is outside moves with its nearest edges at its own size, so that a border keeps
 * its width and meets the window to the pixel. Without bounds (a width of 0) everything scales
 * about the point. Set it again as the window's size changes. */
void sh_anim_lift_bounds(struct sh_anim *anim, int x, int y, int width, int height);
/* How far up the lift is drawn, 0 down to 1 up. */
double sh_anim_lift_progress(const struct sh_anim *anim);
/* Where the lift draws a box of the tree now (x, y, width, height), its edges where the animator
 * puts those of a node there. */
void sh_anim_lift_box(const struct sh_anim *anim, int box[4]);
/* Windows lifted, or still settling. */
size_t sh_animator_lifted(const struct sh_animator *animator);
/* Jumps to the end: every node gets its resting values and the tree its resting position. A
 * lift ends too, and its update is called once it has, for what follows it to rest as well. */
void sh_anim_finish(struct sh_anim *anim);
/* Copies the visible buffers and rectangles under `content` into a new tree just above
 * `window`, which fades out and shrinks about (cx, cy) in `content` coordinates, then goes. */
void sh_anim_close(struct sh_animator *animator, struct wlr_scene_node *window,
                   struct wlr_scene_tree *content, double cx, double cy);
