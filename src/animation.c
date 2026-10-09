/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "paw/animation.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wlr/types/wlr_scene.h>

/* How much smaller a window is when it starts opening or finishes closing. */
static const float SMALL = 0.94F;
/* After this much time past the end, a timer finishes animations no frame has advanced, e.g.
 * on a hidden workspace or a switched-off output. */
enum { SLACK_MS = 50 };

struct sh_animator {
    struct sh_animator_config config;
    int64_t (*clock)(void *);
    void *clock_data;
    int64_t last_tick;      // when a frame last advanced the animations; 0 while none run
    struct wl_list running; // struct sh_anim
    struct wl_list held;    // struct sh_anim held by a gesture, whatever the time
    struct wl_list tweens;  // struct sh_tween
    struct wl_event_source *timer;
};

/* A node's resting values and the values the animation last gave it. */
struct sh_anim_record {
    struct wlr_scene_node *node;
    bool shaped, painted; // the resting geometry, paint are known
    int x, y, width, height;
    int set_x, set_y, set_width, set_height;
    float paint[4], set_paint[4]; // opacity in [0], or a rectangle's color
};

static int64_t real_clock(void *data) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
static int64_t now_ms(const struct sh_animator *animator) {
    return animator->clock(animator->clock_data);
}

/* A kind's duration after the speed multiplier; 0 means it does not animate. */
static int duration_of(const struct sh_animator *animator, enum sh_anim_kind kind) {
    const struct sh_anim_style *style = &animator->config.styles[kind];
    if (style->duration <= 0)
        return 0;
    double speed = animator->config.speed > 0 ? animator->config.speed : 1;
    return (int)fmax(1, lround(style->duration / speed));
}

static double eased(const struct sh_curve *curve, int64_t elapsed, int duration) {
    return sh_curve_eval(curve, duration > 0 ? (double)elapsed / duration : 1);
}

static struct sh_anim_record *record(struct sh_anim *anim, struct wlr_scene_node *node) {
    for (size_t i = 0; i < anim->count; ++i)
        if (anim->records[i].node == node)
            return &anim->records[i];
    if (anim->count == anim->capacity) {
        size_t capacity = anim->capacity ? 2 * anim->capacity : 8;
        struct sh_anim_record *records = realloc(anim->records, capacity * sizeof(*records));
        if (!records)
            return NULL;
        anim->records = records;
        anim->capacity = capacity;
    }
    struct sh_anim_record *result = &anim->records[anim->count++];
    *result = (struct sh_anim_record){.node = node};
    return result;
}

/* A node's current geometry and paint. Buffers without a destination size are not resized. */
static bool geometry(struct wlr_scene_node *node, int *width, int *height) {
    if (node->type == WLR_SCENE_NODE_RECT) {
        struct wlr_scene_rect *rect = wlr_scene_rect_from_node(node);
        *width = rect->width, *height = rect->height;
    } else {
        struct wlr_scene_buffer *buffer = wlr_scene_buffer_from_node(node);
        *width = buffer->dst_width, *height = buffer->dst_height;
    }
    return *width > 0 && *height > 0;
}
static void paint(struct wlr_scene_node *node, float value[4]) {
    if (node->type == WLR_SCENE_NODE_RECT)
        memcpy(value, wlr_scene_rect_from_node(node)->color, 4 * sizeof(float));
    else
        value[0] = wlr_scene_buffer_from_node(node)->opacity, value[1] = value[2] = value[3] = 0;
}
static void set_geometry(struct wlr_scene_node *node, int x, int y, int width, int height) {
    wlr_scene_node_set_position(node, x, y);
    if (node->type == WLR_SCENE_NODE_RECT)
        wlr_scene_rect_set_size(wlr_scene_rect_from_node(node), width, height);
    else
        wlr_scene_buffer_set_dest_size(wlr_scene_buffer_from_node(node), width, height);
}
static void set_paint(struct wlr_scene_node *node, const float value[4]) {
    if (node->type == WLR_SCENE_NODE_RECT)
        wlr_scene_rect_set_color(wlr_scene_rect_from_node(node), value);
    else
        wlr_scene_buffer_set_opacity(wlr_scene_buffer_from_node(node), value[0]);
}

/* Scales the node about the center and multiplies its opacity. `ox, oy` is the position of
 * its parent in the animated tree. */
static void apply_node(struct sh_anim *anim, struct wlr_scene_node *node, int ox, int oy,
                       float scale, float alpha) {
    struct sh_anim_record *r = record(anim, node);
    if (!r)
        return;
    int width, height;
    bool sized = geometry(node, &width, &height);
    if (!r->shaped || node->x != r->set_x || node->y != r->set_y || width != r->set_width ||
        height != r->set_height) {
        r->shaped = true;
        r->x = node->x, r->y = node->y, r->width = width, r->height = height;
    }
    float current[4];
    paint(node, current);
    if (!r->painted || memcmp(current, r->set_paint, sizeof(current))) {
        r->painted = true;
        memcpy(r->paint, current, sizeof(current));
    }
    if (sized) {
        // Both edges are scaled and rounded, so that nodes that adjoin (the slices of a window's
        // shadow, its surfaces) stay adjoined at every scale.
        double left = anim->cx + (ox + r->x - anim->cx) * scale;
        double top = anim->cy + (oy + r->y - anim->cy) * scale;
        double right = anim->cx + (ox + r->x + r->width - anim->cx) * scale;
        double bottom = anim->cy + (oy + r->y + r->height - anim->cy) * scale;
        r->set_x = (int)lround(left) - ox, r->set_y = (int)lround(top) - oy;
        r->set_width = (int)fmax(1, lround(right) - lround(left));
        r->set_height = (int)fmax(1, lround(bottom) - lround(top));
        set_geometry(node, r->set_x, r->set_y, r->set_width, r->set_height);
    } else {
        r->set_x = r->x, r->set_y = r->y, r->set_width = width, r->set_height = height;
    }
    // Opacity is a factor; a premultiplied color fades by multiplying every channel.
    for (int i = 0; i < 4; ++i)
        r->set_paint[i] = r->paint[i] * alpha;
    set_paint(node, r->set_paint);
}

static void restore_node(struct sh_anim *anim, struct wlr_scene_node *node) {
    struct sh_anim_record *r = NULL;
    for (size_t i = 0; i < anim->count && !r; ++i)
        if (anim->records[i].node == node)
            r = &anim->records[i];
    if (!r)
        return;
    int width, height;
    bool sized = geometry(node, &width, &height);
    // Leave values that changed under the animation: they are newer than the record.
    if (r->shaped && sized && node->x == r->set_x && node->y == r->set_y && width == r->set_width &&
        height == r->set_height)
        set_geometry(node, r->x, r->y, r->width, r->height);
    float current[4];
    paint(node, current);
    if (r->painted && !memcmp(current, r->set_paint, sizeof(current)))
        set_paint(node, r->paint);
}

/* Visits every buffer and rectangle below `tree`, with its parent's offset from the root. */
static void walk(struct sh_anim *anim, struct wlr_scene_tree *tree, int ox, int oy, bool restore,
                 float scale, float alpha) {
    struct wlr_scene_node *node;
    wl_list_for_each(node, &tree->children, link) {
        if (node->type == WLR_SCENE_NODE_TREE)
            walk(anim, wlr_scene_tree_from_node(node), ox + node->x, oy + node->y, restore, scale,
                 alpha);
        else if (restore)
            restore_node(anim, node);
        else
            apply_node(anim, node, ox, oy, scale, alpha);
    }
}

/* A held animation where the gesture put it: its tree offset from where it rests and every node
 * at its opacity times the hold's. */
static void apply_hold(struct sh_anim *anim) {
    wlr_scene_node_set_position(&anim->tree->node, anim->base_x + (int)lround(anim->held_x),
                                anim->base_y + (int)lround(anim->held_y));
    walk(anim, anim->tree, 0, 0, false, 1, anim->held_alpha);
}

/* A held window's tree and nodes back at rest; a copy has no rest but its hold. */
static void rest_hold(struct sh_anim *anim) {
    wlr_scene_node_set_position(&anim->tree->node, anim->base_x, anim->base_y);
    if (!anim->owned)
        walk(anim, anim->tree, 0, 0, true, 1, 1);
}

static void stop(struct sh_anim *anim) {
    if (!anim->animator)
        return;
    wl_list_remove(&anim->link);
    anim->animator = NULL;
    anim->fx = anim->glide = anim->held = false;
    if (anim->owned) {
        wlr_scene_node_destroy(&anim->tree->node);
        free(anim->records);
        free(anim);
        return;
    }
    free(anim->records);
    anim->records = NULL;
    anim->count = anim->capacity = 0;
}

/* The glide's offset along one axis at `elapsed` milliseconds, and its velocity. The eased
 * decay carries the offset to 0; a term that is 0 at both ends carries the starting velocity, so
 * a retargeted glide does not jerk. */
static void glide_axis(const struct sh_anim *anim, double offset, double velocity, int64_t elapsed,
                       double *position, double *speed) {
    double d = anim->glide_duration, t = (double)elapsed / d;
    if (t >= 1) {
        *position = *speed = 0;
        return;
    }
    if (t < 0)
        t = 0;
    const double h = 1e-4;
    double slope = (sh_curve_eval(&anim->glide_curve, t + h) -
                    sh_curve_eval(&anim->glide_curve, t > h ? t - h : 0)) /
                   (t > h ? 2 * h : t + h);
    double carry = (1 - t) * (1 - t);
    *position = offset * (1 - sh_curve_eval(&anim->glide_curve, t)) + velocity * d * t * carry;
    *speed = -offset * slope / d + velocity * (carry - 2 * t * (1 - t));
}

/* Returns whether the animation is still running. */
static bool step(struct sh_anim *anim, int64_t now) {
    if (anim->glide) {
        int64_t elapsed = now - anim->glide_start;
        double x, y, vx, vy;
        glide_axis(anim, anim->glide_x, anim->glide_vx, elapsed, &x, &vx);
        glide_axis(anim, anim->glide_y, anim->glide_vy, elapsed, &y, &vy);
        wlr_scene_node_set_position(&anim->tree->node, anim->base_x + (int)lround(x),
                                    anim->base_y + (int)lround(y));
        anim->glide = elapsed < anim->glide_duration;
    }
    if (anim->fx) {
        int64_t elapsed = now - anim->fx_start;
        double e = eased(&anim->fx_curve, elapsed, anim->fx_duration);
        anim->fx = elapsed < anim->fx_duration;
        if (anim->fx)
            walk(anim, anim->tree, 0, 0, false,
                 (float)(anim->scale_from + (anim->scale_to - anim->scale_from) * e),
                 (float)(anim->alpha_from + (anim->alpha_to - anim->alpha_from) * e));
        else if (!anim->owned)
            walk(anim, anim->tree, 0, 0, true, 1, 1);
    }
    return anim->fx || anim->glide;
}

void sh_anim_finish(struct sh_anim *anim) {
    if (!anim->animator)
        return;
    if (anim->glide)
        wlr_scene_node_set_position(&anim->tree->node, anim->base_x, anim->base_y);
    if (anim->fx && !anim->owned)
        walk(anim, anim->tree, 0, 0, true, 1, 1);
    if (anim->held && !anim->owned)
        rest_hold(anim);
    stop(anim);
}

static void tween_value(const struct sh_tween *tween, double e, float out[SH_TWEEN_VALUES]) {
    for (int i = 0; i < SH_TWEEN_VALUES; ++i)
        out[i] = (float)(tween->from[i] + (tween->to[i] - tween->from[i]) * e);
}

void sh_tween_stop(struct sh_tween *tween) {
    if (tween->animator)
        wl_list_remove(&tween->link);
    tween->animator = NULL;
    tween->valid = false;
}

void sh_tween_track(struct sh_animator *animator, struct sh_tween *tween, enum sh_anim_kind kind,
                    bool animate, const float target[SH_TWEEN_VALUES],
                    float current[SH_TWEEN_VALUES], void (*update)(void *), void *data) {
    int duration = animate && animator->config.enabled ? duration_of(animator, kind) : 0;
    bool changed = !tween->valid || memcmp(tween->to, target, sizeof(tween->to));
    if (changed) {
        if (!duration || !tween->valid) {
            sh_tween_stop(tween);
            tween->valid = true;
            memcpy(tween->to, target, sizeof(tween->to));
            memcpy(tween->value, target, sizeof(tween->value));
        } else {
            // From where it is drawn now: a fade reversed halfway goes back from there.
            memcpy(tween->from, tween->value, sizeof(tween->from));
            memcpy(tween->to, target, sizeof(tween->to));
            tween->start = now_ms(animator);
            tween->duration = duration;
            tween->curve = animator->config.styles[kind].curve;
            tween->update = update, tween->data = data;
            if (!tween->animator) {
                tween->animator = animator;
                wl_list_insert(animator->tweens.prev, &tween->link);
            }
            if (!animator->last_tick)
                animator->last_tick = tween->start;
            wl_event_source_timer_update(animator->timer, duration + SLACK_MS);
        }
    }
    memcpy(current, tween->value, sizeof(tween->value));
}

/* Moves every tween to `now`; a finished one lands and is dropped. */
static void step_tweens(struct sh_animator *animator, int64_t now, bool finish) {
    struct sh_tween *tween, *next;
    wl_list_for_each_safe(tween, next, &animator->tweens, link) {
        int64_t elapsed = now - tween->start;
        if (finish || elapsed >= tween->duration) {
            memcpy(tween->value, tween->to, sizeof(tween->value));
            wl_list_remove(&tween->link);
            tween->animator = NULL;
        } else {
            tween_value(tween, eased(&tween->curve, elapsed, tween->duration), tween->value);
        }
        if (tween->update)
            tween->update(tween->data);
    }
}

size_t sh_animator_tweens(const struct sh_animator *animator) {
    return (size_t)wl_list_length(&animator->tweens);
}

static void rest_anim(struct sh_anim *anim) {
    anim->rested = true;
    if (anim->owned)
        wlr_scene_node_set_enabled(&anim->tree->node, false);
    if (anim->glide)
        wlr_scene_node_set_position(&anim->tree->node, anim->base_x, anim->base_y);
    if (anim->fx && !anim->owned)
        walk(anim, anim->tree, 0, 0, true, 1, 1);
    if (anim->held && !anim->owned)
        rest_hold(anim);
}

void sh_animator_rest(struct sh_animator *animator) {
    struct sh_anim *anim;
    wl_list_for_each(anim, &animator->running, link) rest_anim(anim);
    wl_list_for_each(anim, &animator->held, link) rest_anim(anim);
}

/* Whether a buffer or rectangle below `tree`, whose parent is at (ox, oy), has a box that
 * contains the point. A buffer of unknown size might. */
static bool tree_covers(const struct wlr_scene_tree *tree, double ox, double oy, double px,
                        double py) {
    struct wlr_scene_node *node;
    wl_list_for_each(node, &tree->children, link) {
        if (!node->enabled)
            continue;
        double x = ox + node->x, y = oy + node->y;
        if (node->type == WLR_SCENE_NODE_TREE) {
            if (tree_covers(wlr_scene_tree_from_node(node), x, y, px, py))
                return true;
            continue;
        }
        int width, height;
        if (node->type == WLR_SCENE_NODE_RECT) {
            width = wlr_scene_rect_from_node(node)->width;
            height = wlr_scene_rect_from_node(node)->height;
        } else {
            struct wlr_scene_buffer *buffer = wlr_scene_buffer_from_node(node);
            width = buffer->dst_width;
            height = buffer->dst_height;
            if ((width <= 0 || height <= 0) && buffer->buffer) {
                width = buffer->buffer->width;
                height = buffer->buffer->height;
            }
            if (width <= 0 || height <= 0)
                return true;
        }
        if (px >= x && px < x + width && py >= y && py < y + height)
            return true;
    }
    return false;
}

void sh_animator_rest_at(struct sh_animator *animator, double px, double py) {
    struct sh_anim *anim;
    wl_list_for_each(anim, &animator->running, link) {
        int cx, cy;
        bool matters = true;
        // A fade that only changes opacity and a glide keep their boxes' sizes; growing ones
        // and their scale-about-a-center are put at rest without asking.
        bool sizes_change = anim->fx && anim->scale_from != anim->scale_to;
        if (!sizes_change && wlr_scene_node_coords(&anim->tree->node, &cx, &cy)) {
            double parent_x = cx - anim->tree->node.x, parent_y = cy - anim->tree->node.y;
            matters = tree_covers(anim->tree, cx, cy, px, py) ||
                      (!anim->owned &&
                       tree_covers(anim->tree, parent_x + anim->base_x, parent_y + anim->base_y, px,
                                   py));
        }
        if (matters)
            rest_anim(anim);
    }
    wl_list_for_each(anim, &animator->held, link) rest_anim(anim);
}

void sh_animator_resume(struct sh_animator *animator) {
    int64_t now = now_ms(animator);
    struct sh_anim *anim;
    wl_list_for_each(anim, &animator->running, link) {
        if (!anim->rested)
            continue;
        anim->rested = false;
        if (anim->owned)
            wlr_scene_node_set_enabled(&anim->tree->node, true);
        step(anim, now);
    }
    wl_list_for_each(anim, &animator->held, link) {
        if (!anim->rested)
            continue;
        anim->rested = false;
        if (anim->owned)
            wlr_scene_node_set_enabled(&anim->tree->node, true);
        apply_hold(anim);
    }
}

void sh_animator_finish_all(struct sh_animator *animator) {
    struct sh_anim *anim, *next;
    wl_list_for_each_safe(anim, next, &animator->running, link) sh_anim_finish(anim);
    step_tweens(animator, 0, true);
    animator->last_tick = 0;
}

void sh_animator_tick(struct sh_animator *animator) {
    int64_t now = now_ms(animator);
    if (animator->config.late_ms > 0 && animator->last_tick &&
        now - animator->last_tick > animator->config.late_ms) {
        // The compositor fell behind: land now rather than jump through the middle.
        sh_animator_finish_all(animator);
        return;
    }
    animator->last_tick = now;
    struct sh_anim *anim, *next;
    wl_list_for_each_safe(anim, next, &animator->running, link) {
        if (!step(anim, now))
            stop(anim);
    }
    // What a client drew since keeps the hold's opacity.
    wl_list_for_each(anim, &animator->held, link) apply_hold(anim);
    step_tweens(animator, now, false);
    if (wl_list_empty(&animator->running) && wl_list_empty(&animator->tweens))
        animator->last_tick = 0;
}

static int timer_fired(void *data) {
    struct sh_animator *animator = data;
    sh_animator_tick(animator);
    if (!wl_list_empty(&animator->running) || !wl_list_empty(&animator->tweens))
        wl_event_source_timer_update(animator->timer, SLACK_MS);
    return 0;
}

static void start(struct sh_animator *animator, struct sh_anim *anim, struct wlr_scene_tree *tree,
                  int duration) {
    if (anim->animator && anim->tree != tree)
        sh_anim_finish(anim);
    anim->tree = tree;
    if (!anim->animator) {
        anim->animator = animator;
        wl_list_insert(animator->running.prev, &anim->link);
    }
    if (!animator->last_tick)
        animator->last_tick = now_ms(animator);
    wl_event_source_timer_update(animator->timer, duration + SLACK_MS);
}

/* The velocity a restarted glide adds to its curve's own so the speed stays the same, kept
 * within what could plausibly matter for a move of `offset` pixels. */
static double carried(double velocity, double curve_velocity, double offset, int duration) {
    double extra = velocity - curve_velocity, limit = 2 * fabs(offset) / duration;
    return extra > limit ? limit : extra < -limit ? -limit : extra;
}

static void glide_begin(struct sh_animator *animator, struct sh_anim *anim,
                        struct wlr_scene_tree *tree, int dx, int dy, enum sh_anim_kind kind,
                        int duration) {
    start(animator, anim, tree, duration);
    int64_t now = now_ms(animator);
    // A glide under way continues from where the window is drawn now, at the speed it has.
    double vx = 0, vy = 0;
    bool moving = anim->glide;
    if (moving) {
        double x, y;
        glide_axis(anim, anim->glide_x, anim->glide_vx, now - anim->glide_start, &x, &vx);
        glide_axis(anim, anim->glide_y, anim->glide_vy, now - anim->glide_start, &y, &vy);
    }
    anim->glide_x = tree->node.x - anim->base_x + dx;
    anim->glide_y = tree->node.y - anim->base_y + dy;
    anim->glide_duration = duration;
    anim->glide_curve = animator->config.styles[kind].curve;
    // The new curve has a speed of its own to start with; carry only what it lacks.
    const double h = 1e-4;
    double start_slope = (sh_curve_eval(&anim->glide_curve, h) - 0) / h / duration;
    anim->glide_vx = moving ? carried(vx, -anim->glide_x * start_slope, anim->glide_x, duration) : 0;
    anim->glide_vy = moving ? carried(vy, -anim->glide_y * start_slope, anim->glide_y, duration) : 0;
    anim->glide = true;
    anim->glide_start = now;
    step(anim, anim->glide_start);
}

void sh_anim_glide_kind(struct sh_animator *animator, struct sh_anim *anim,
                        struct wlr_scene_tree *tree, int dx, int dy, enum sh_anim_kind kind) {
    int duration = animator->config.enabled ? duration_of(animator, kind) : 0;
    if (!duration || (dx == 0 && dy == 0))
        return;
    glide_begin(animator, anim, tree, dx, dy, kind, duration);
}

void sh_anim_glide(struct sh_animator *animator, struct sh_anim *anim, struct wlr_scene_tree *tree,
                   int dx, int dy) {
    sh_anim_glide_kind(animator, anim, tree, dx, dy, SH_ANIM_MOVE);
}

/* Starts (or restarts) the fade-and-scale of a live window from the resting values. */
static void fx_begin(struct sh_animator *animator, struct sh_anim *anim, enum sh_anim_kind kind,
                     int duration, float scale_from, float scale_to, float alpha_from,
                     float alpha_to, double cx, double cy) {
    if (anim->fx && !anim->owned)
        walk(anim, anim->tree, 0, 0, true, 1, 1); // what it shows now is not its resting look
    anim->fx = true;
    anim->fx_start = now_ms(animator);
    anim->fx_duration = duration;
    anim->fx_curve = animator->config.styles[kind].curve;
    anim->scale_from = scale_from, anim->scale_to = scale_to;
    anim->alpha_from = alpha_from, anim->alpha_to = alpha_to;
    anim->cx = cx, anim->cy = cy;
}

void sh_anim_open(struct sh_animator *animator, struct sh_anim *anim, struct wlr_scene_tree *tree,
                  double cx, double cy) {
    int duration = animator->config.enabled ? duration_of(animator, SH_ANIM_OPEN) : 0;
    if (!duration)
        return;
    start(animator, anim, tree, duration);
    fx_begin(animator, anim, SH_ANIM_OPEN, duration, SMALL, 1, 0, 1, cx, cy);
    step(anim, anim->fx_start);
}

void sh_anim_slide(struct sh_animator *animator, struct sh_anim *anim, struct wlr_scene_tree *tree,
                   int dx, int dy) {
    int duration = animator->config.enabled ? duration_of(animator, SH_ANIM_WORKSPACE) : 0;
    if (!duration)
        return;
    start(animator, anim, tree, duration);
    if (dx || dy)
        glide_begin(animator, anim, tree, dx, dy, SH_ANIM_WORKSPACE, duration);
    fx_begin(animator, anim, SH_ANIM_WORKSPACE, duration, 1, 1, 0, 1, 0, 0);
    step(anim, anim->fx_start);
}

/* Copies what is visible below `tree` into `target`, flattened. */
static void copy(struct wlr_scene_tree *target, struct wlr_scene_tree *tree, int ox, int oy) {
    struct wlr_scene_node *node;
    wl_list_for_each(node, &tree->children, link) {
        if (!node->enabled)
            continue;
        int x = ox + node->x, y = oy + node->y;
        if (node->type == WLR_SCENE_NODE_TREE) {
            copy(target, wlr_scene_tree_from_node(node), x, y);
        } else if (node->type == WLR_SCENE_NODE_RECT) {
            struct wlr_scene_rect *rect = wlr_scene_rect_from_node(node);
            struct wlr_scene_rect *clone =
                wlr_scene_rect_create(target, rect->width, rect->height, rect->color);
            if (clone)
                wlr_scene_node_set_position(&clone->node, x, y);
        } else if (node->type == WLR_SCENE_NODE_BUFFER) {
            struct wlr_scene_buffer *buffer = wlr_scene_buffer_from_node(node);
            if (!buffer->buffer)
                continue;
            // The new node locks the client's buffer, which keeps its texture.
            struct wlr_scene_buffer *clone = wlr_scene_buffer_create(target, buffer->buffer);
            if (!clone)
                continue;
            int width = buffer->dst_width, height = buffer->dst_height;
            if (width <= 0 || height <= 0) {
                bool turned = buffer->transform & WL_OUTPUT_TRANSFORM_90;
                width = turned ? buffer->buffer->height : buffer->buffer->width;
                height = turned ? buffer->buffer->width : buffer->buffer->height;
            }
            wlr_scene_node_set_position(&clone->node, x, y);
            wlr_scene_buffer_set_dest_size(clone, width, height);
            wlr_scene_buffer_set_source_box(clone, &buffer->src_box);
            wlr_scene_buffer_set_transform(clone, buffer->transform);
            wlr_scene_buffer_set_opacity(clone, buffer->opacity);
            wlr_scene_buffer_set_filter_mode(clone, buffer->filter_mode);
            wlr_scene_buffer_set_opaque_region(clone, &buffer->opaque_region);
            wlr_scene_buffer_set_transfer_function(clone, buffer->transfer_function);
            wlr_scene_buffer_set_primaries(clone, buffer->primaries);
            wlr_scene_buffer_set_color_encoding(clone, buffer->color_encoding);
            wlr_scene_buffer_set_color_range(clone, buffer->color_range);
        }
    }
}

/* A tree just above `window` holding a copy of what is visible under `content`, where the
 * window is, owned by a new animation that is neither running nor held; NULL if there is nothing
 * to copy. */
static struct sh_anim *make_copy(struct wlr_scene_node *window, struct wlr_scene_tree *content) {
    struct sh_anim *anim = calloc(1, sizeof(*anim));
    struct wlr_scene_tree *tree = anim ? wlr_scene_tree_create(window->parent) : NULL;
    if (!tree) {
        free(anim);
        return NULL;
    }
    copy(tree, content, 0, 0);
    if (wl_list_empty(&tree->children)) {
        wlr_scene_node_destroy(&tree->node);
        free(anim);
        return NULL;
    }
    wlr_scene_node_place_above(&tree->node, window);
    wlr_scene_node_set_position(&tree->node, window->x + content->node.x,
                                window->y + content->node.y);
    anim->owned = true;
    anim->tree = tree;
    return anim;
}

/* make_copy's copy, started. */
static struct sh_anim *snapshot(struct sh_animator *animator, struct wlr_scene_node *window,
                                struct wlr_scene_tree *content, int duration) {
    struct sh_anim *anim = make_copy(window, content);
    if (anim)
        start(animator, anim, anim->tree, duration);
    return anim;
}

void sh_anim_close(struct sh_animator *animator, struct wlr_scene_node *window,
                   struct wlr_scene_tree *content, double cx, double cy) {
    int duration = animator->config.enabled ? duration_of(animator, SH_ANIM_CLOSE) : 0;
    if (!duration || !window->enabled || !window->parent)
        return;
    struct sh_anim *anim = snapshot(animator, window, content, duration);
    if (!anim)
        return;
    fx_begin(animator, anim, SH_ANIM_CLOSE, duration, 1, SMALL, 1, 0, cx, cy);
    step(anim, anim->fx_start);
}

void sh_anim_slide_out(struct sh_animator *animator, struct wlr_scene_node *window,
                       struct wlr_scene_tree *content, int dx, int dy) {
    int duration = animator->config.enabled ? duration_of(animator, SH_ANIM_WORKSPACE) : 0;
    if (!duration || !window->enabled || !window->parent)
        return;
    struct sh_anim *anim = snapshot(animator, window, content, duration);
    if (!anim)
        return;
    // It rests where it slides to, and starts at the offset back from there.
    anim->base_x = anim->tree->node.x + dx;
    anim->base_y = anim->tree->node.y + dy;
    glide_begin(animator, anim, anim->tree, 0, 0, SH_ANIM_WORKSPACE, duration);
    fx_begin(animator, anim, SH_ANIM_WORKSPACE, duration, 1, 1, 1, 0, 0, 0);
    step(anim, anim->fx_start);
}

void sh_anim_hold(struct sh_animator *animator, struct sh_anim *anim, struct wlr_scene_tree *tree,
                  double dx, double dy, float alpha) {
    if (anim->owned && !anim->held)
        return; // a copy that is running is the animator's alone
    if (anim->animator && (!anim->held || anim->tree != tree))
        sh_anim_finish(anim); // what ran lands, and the hold starts from rest
    anim->tree = tree;
    if (!anim->animator) {
        anim->animator = animator;
        anim->held = true;
        wl_list_insert(animator->held.prev, &anim->link);
    }
    anim->held_x = dx, anim->held_y = dy;
    anim->held_alpha = alpha;
    apply_hold(anim);
}

struct sh_anim *sh_anim_hold_copy(struct sh_animator *animator, struct wlr_scene_node *window,
                                  struct wlr_scene_tree *content, double dx, double dy,
                                  float alpha) {
    if (!window->parent)
        return NULL;
    struct sh_anim *anim = make_copy(window, content);
    if (!anim)
        return NULL;
    // It rests where the window is, and the hold moves it from there.
    anim->base_x = anim->tree->node.x;
    anim->base_y = anim->tree->node.y;
    anim->animator = animator;
    anim->held = true;
    wl_list_insert(animator->held.prev, &anim->link);
    anim->held_x = dx, anim->held_y = dy;
    anim->held_alpha = alpha;
    apply_hold(anim);
    return anim;
}

/* The workspace slide's time, cut to `share` of it (a quarter at least), or 0 when it is off. */
static int slide_duration(const struct sh_animator *animator, double share) {
    int duration = animator->config.enabled ? duration_of(animator, SH_ANIM_WORKSPACE) : 0;
    if (!duration)
        return 0;
    share = share < 0.25 ? 0.25 : share > 1 ? 1 : share;
    return (int)fmax(1, lround(duration * share));
}

void sh_anim_slide_from(struct sh_animator *animator, struct sh_anim *anim,
                        struct wlr_scene_tree *tree, int dx, int dy, float alpha, double share) {
    if (anim->owned)
        return;
    sh_anim_finish(anim); // from rest, as the hold or what ran left it
    int duration = slide_duration(animator, share);
    if (!duration || (dx == 0 && dy == 0 && alpha >= 1))
        return;
    start(animator, anim, tree, duration);
    if (dx || dy)
        glide_begin(animator, anim, tree, dx, dy, SH_ANIM_WORKSPACE, duration);
    fx_begin(animator, anim, SH_ANIM_WORKSPACE, duration, 1, 1, alpha, 1, 0, 0);
    step(anim, anim->fx_start);
}

void sh_anim_slide_away(struct sh_animator *animator, struct sh_anim *anim, int dx, int dy,
                        float alpha, double share) {
    if (!anim->animator || !anim->owned)
        return;
    int duration = slide_duration(animator, share);
    if (!duration) {
        stop(anim);
        return;
    }
    double from_x = anim->held ? anim->held_x : 0, from_y = anim->held ? anim->held_y : 0;
    float from_alpha = anim->held ? anim->held_alpha : 1;
    if (anim->held) {
        wl_list_remove(&anim->link);
        anim->animator = NULL;
        anim->held = false;
    }
    start(animator, anim, anim->tree, duration);
    // It rests where it slides to, and starts from where it was held, back from there.
    anim->base_x += dx;
    anim->base_y += dy;
    anim->glide_x = from_x - dx;
    anim->glide_y = from_y - dy;
    anim->glide_vx = anim->glide_vy = 0;
    anim->glide_duration = duration;
    anim->glide_curve = animator->config.styles[SH_ANIM_WORKSPACE].curve;
    anim->glide = true;
    anim->glide_start = now_ms(animator);
    fx_begin(animator, anim, SH_ANIM_WORKSPACE, duration, 1, 1, from_alpha, alpha, 0, 0);
    step(anim, anim->fx_start);
}

struct sh_animator *sh_animator_create(struct wl_event_loop *loop) {
    struct sh_animator *animator = calloc(1, sizeof(*animator));
    if (!animator)
        return NULL;
    wl_list_init(&animator->running);
    wl_list_init(&animator->held);
    wl_list_init(&animator->tweens);
    animator->clock = real_clock;
    animator->config.speed = 1;
    animator->timer = wl_event_loop_add_timer(loop, timer_fired, animator);
    if (!animator->timer) {
        free(animator);
        return NULL;
    }
    return animator;
}

void sh_animator_configure(struct sh_animator *animator, const struct sh_animator_config *config) {
    animator->config = *config;
    if (!(animator->config.speed > 0))
        animator->config.speed = 1;
    if (!animator->config.enabled)
        sh_animator_finish_all(animator);
}

void sh_animator_set_clock(struct sh_animator *animator, int64_t (*now)(void *), void *data) {
    animator->clock = now ? now : real_clock;
    animator->clock_data = data;
}

size_t sh_animator_running(const struct sh_animator *animator) {
    return (size_t)wl_list_length(&animator->running);
}

void sh_animator_destroy(struct sh_animator *animator) {
    if (!animator)
        return;
    sh_animator_finish_all(animator);
    struct sh_anim *anim, *next;
    wl_list_for_each_safe(anim, next, &animator->held, link) sh_anim_finish(anim);
    wl_event_source_remove(animator->timer);
    free(animator);
}
