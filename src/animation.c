/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "paw/animation.h"
#include "paw/overview_scene.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wlr/types/wlr_scene.h>

/* How much smaller a window is when it starts opening or finishes closing. */
static const float SMALL = 0.94F;
/* While anything runs, a timer advances it this often besides the outputs' frames. A frame comes
 * only where the scene changed, and a glide that moves less than a pixel in one (a slow spring's
 * start, a nudge from rest) changes nothing: frames alone would stall it, and the next tick would
 * find it late and land it unseen, with whatever started meanwhile. What the timer changes damages
 * the outputs it shows on, which then draw; nothing asks an output for a frame otherwise, so one
 * where nothing moves stays still. It also finishes animations no frame advances, as on a hidden
 * workspace or a switched-off output. */
enum { TICK_MS = 16 };
/* How long a window that changed size waits, stretched to its new size, for its client to draw
 * that size once the glide is over; a slower one is then drawn as it is until it does. */
enum { MORPH_WAIT_MS = 300 };
/* How far past its geometry a window changing size may take input at rest: its frame, and the
 * shadow a client-side frame draws around it. */
enum { MORPH_REACH = 128 };
/* The fade of a copy of what a client drew before its size changed. */
static const struct sh_curve FADE_CURVE = {SH_CURVE_EASE_IN_OUT, {0}};

struct sh_animator {
    struct sh_animator_config config;
    int64_t (*clock)(void *);
    void *clock_data;
    int64_t last_tick;      // when a frame last advanced the animations; 0 while none run
    struct wl_list running; // struct sh_anim
    struct wl_list held;    // struct sh_anim held by a gesture, whatever the time
    struct wl_list tweens;  // struct sh_tween
    struct wl_event_source *timer;
    bool ticking; // the timer is set
};

/* A node's resting values and the values the animation last gave it. */
struct sh_anim_record {
    struct wlr_scene_node *node;
    bool shaped, painted; // the resting geometry, paint are known
    int x, y, width, height;
    int set_x, set_y, set_width, set_height;
    float paint[4], set_paint[4]; // opacity in [0], or a rectangle's color
    /* A buffer's opaque region, set aside while it is drawn at another size than its own: the
     * scene takes the region as it is, unscaled, and would leave what lies under a part of it
     * that is not opaque where the buffer is drawn undrawn. */
    bool cleared;
    pixman_region32_t opaque;
};

/* A record no longer needed. */
static void record_finish(struct sh_anim_record *r) {
    if (r->cleared)
        pixman_region32_fini(&r->opaque);
    r->cleared = false;
}

/* The buffer's opaque region set aside while it is drawn at another size; a region the client
 * gave since takes the place of the one set aside. */
static void clear_opaque(struct sh_anim_record *r, struct wlr_scene_buffer *buffer) {
    if (!pixman_region32_not_empty(&buffer->opaque_region))
        return;
    if (!r->cleared)
        pixman_region32_init(&r->opaque);
    r->cleared = true;
    pixman_region32_copy(&r->opaque, &buffer->opaque_region);
    pixman_region32_t empty;
    pixman_region32_init(&empty);
    wlr_scene_buffer_set_opaque_region(buffer, &empty);
    pixman_region32_fini(&empty);
}

/* The opaque region set aside given back, unless the client gave another meanwhile. */
static void restore_opaque(struct sh_anim_record *r, struct wlr_scene_buffer *buffer) {
    if (!r->cleared)
        return;
    if (!pixman_region32_not_empty(&buffer->opaque_region))
        wlr_scene_buffer_set_opaque_region(buffer, &r->opaque);
    record_finish(r);
}

static int64_t real_clock(void *data) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}
static int64_t now_ms(const struct sh_animator *animator) {
    return animator->clock(animator->clock_data);
}

/* How often the timer ticks: every TICK_MS, or sooner when a tick later than half of
 * `late_ms` after the last would be taken for a late frame. */
static int tick_interval(const struct sh_animator *animator) {
    int late = animator->config.late_ms;
    return late > 0 && late / 2 < TICK_MS ? (late / 2 > 1 ? late / 2 : 1) : TICK_MS;
}

/* Sets the timer for the next tick, unless it is set. */
static void keep_ticking(struct sh_animator *animator) {
    if (animator->ticking)
        return;
    animator->ticking = true;
    wl_event_source_timer_update(animator->timer, tick_interval(animator));
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

/* What a walk does to the nodes it visits: the fx's scale about the animation's center and its
 * factor of their opacity, after a morph's stretch about the tree's origin (`sx`, `sy`: 1 but
 * for the client's surfaces and their fading copy). `width` and `height` are the size a morph
 * draws the window at, `fade` the opacity of that copy. */
struct mapping {
    float scale, alpha;
    double sx, sy;
    double width, height;
    float fade;
};
static const struct mapping IDENTITY = {1, 1, 1, 1, 0, 0, 1};

/* Stretches the node, scales it about the center and multiplies its opacity, as `m` says.
 * `ox, oy` is the position of its parent in the animated tree. */
static void apply_node(struct sh_anim *anim, struct wlr_scene_node *node, int ox, int oy,
                       const struct mapping *m) {
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
        double scale = m->scale;
        double left = anim->cx + ((ox + r->x) * m->sx - anim->cx) * scale;
        double top = anim->cy + ((oy + r->y) * m->sy - anim->cy) * scale;
        double right = anim->cx + ((ox + r->x + r->width) * m->sx - anim->cx) * scale;
        double bottom = anim->cy + ((oy + r->y + r->height) * m->sy - anim->cy) * scale;
        r->set_x = (int)lround(left) - ox, r->set_y = (int)lround(top) - oy;
        r->set_width = (int)fmax(1, lround(right) - lround(left));
        r->set_height = (int)fmax(1, lround(bottom) - lround(top));
        set_geometry(node, r->set_x, r->set_y, r->set_width, r->set_height);
    } else {
        r->set_x = r->x, r->set_y = r->y, r->set_width = width, r->set_height = height;
    }
    if (node->type == WLR_SCENE_NODE_BUFFER) {
        struct wlr_scene_buffer *buffer = wlr_scene_buffer_from_node(node);
        if (r->set_width != r->width || r->set_height != r->height)
            clear_opaque(r, buffer);
        else
            restore_opaque(r, buffer);
    }
    // Opacity is a factor; a premultiplied color fades by multiplying every channel.
    for (int i = 0; i < 4; ++i)
        r->set_paint[i] = r->paint[i] * m->alpha;
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
    if (node->type == WLR_SCENE_NODE_BUFFER)
        restore_opaque(r, wlr_scene_buffer_from_node(node));
}

/* `m` with what was drawn for `width` by `height` stretched to the size the morph draws. */
static struct mapping stretched(const struct mapping *m, int width, int height) {
    struct mapping inner = *m;
    inner.sx = width > 0 && m->width > 0 ? m->width / width : 1;
    inner.sy = height > 0 && m->height > 0 ? m->height / height : 1;
    return inner;
}

/* Visits every buffer and rectangle below `tree`, with its parent's offset from the root: puts
 * each back at rest with `restore`, else where `m` says. */
static void walk(struct sh_anim *anim, struct wlr_scene_tree *tree, int ox, int oy, bool restore,
                 const struct mapping *m) {
    struct wlr_scene_node *node;
    wl_list_for_each(node, &tree->children, link) {
        if (node->type == WLR_SCENE_NODE_TREE) {
            struct wlr_scene_tree *child = wlr_scene_tree_from_node(node);
            struct mapping inner;
            const struct mapping *next = m;
            if (!restore && anim->morph && child == anim->surfaces) {
                inner = stretched(m, anim->live_width, anim->live_height);
                next = &inner;
            } else if (!restore && anim->morph && child == anim->fading) {
                inner = stretched(m, anim->fading_width, anim->fading_height);
                inner.alpha *= m->fade;
                next = &inner;
            }
            walk(anim, child, ox + node->x, oy + node->y, restore, next);
        } else if (restore) {
            restore_node(anim, node);
        } else {
            apply_node(anim, node, ox, oy, m);
        }
    }
}

/* Drops the records of the nodes below `tree`: they are going, or are read afresh. */
static void forget(struct sh_anim *anim, struct wlr_scene_tree *tree) {
    struct wlr_scene_node *node;
    wl_list_for_each(node, &tree->children, link) {
        if (node->type == WLR_SCENE_NODE_TREE) {
            forget(anim, wlr_scene_tree_from_node(node));
            continue;
        }
        for (size_t i = 0; i < anim->count; ++i) {
            if (anim->records[i].node == node) {
                record_finish(&anim->records[i]);
                anim->records[i] = anim->records[--anim->count];
                break;
            }
        }
    }
}

/* A morph's copy of what the client drew before goes. */
static void drop_fading(struct sh_anim *anim) {
    if (!anim->fading)
        return;
    forget(anim, anim->fading);
    wlr_scene_node_destroy(&anim->fading->node);
    anim->fading = NULL;
}

/* A held animation where the gesture put it: its tree offset from where it rests and every node
 * at its opacity times the hold's. */
static void apply_hold(struct sh_anim *anim) {
    wlr_scene_node_set_position(&anim->tree->node, anim->base_x + (int)lround(anim->held_x),
                                anim->base_y + (int)lround(anim->held_y));
    struct mapping m = IDENTITY;
    m.alpha = anim->held_alpha;
    walk(anim, anim->tree, 0, 0, false, &m);
}

/* A held window's tree and nodes back at rest; a copy has no rest but its hold. */
static void rest_hold(struct sh_anim *anim) {
    wlr_scene_node_set_position(&anim->tree->node, anim->base_x, anim->base_y);
    if (!anim->owned)
        walk(anim, anim->tree, 0, 0, true, NULL);
}

/* Tells the owner tracking the tree that this animation is over for it. */
static void release_track(struct sh_anim *anim) {
    void (*track)(void *, bool) = anim->track;
    anim->track = NULL;
    if (track)
        track(anim->track_data, true);
}

static void stop(struct sh_anim *anim) {
    if (!anim->animator)
        return;
    wl_list_remove(&anim->link);
    anim->animator = NULL;
    drop_fading(anim);
    anim->fx = anim->glide = anim->held = anim->morph = anim->absent = anim->fold = false;
    anim->drawn_width = anim->drawn_height = -1;
    for (size_t i = 0; i < anim->count; ++i)
        record_finish(&anim->records[i]);
    if (anim->owned) {
        if (anim->ref)
            *anim->ref = NULL;
        wlr_scene_node_destroy(&anim->tree->node);
        free(anim->records);
        free(anim);
        return;
    }
    free(anim->records);
    anim->records = NULL;
    anim->count = anim->capacity = 0;
    release_track(anim);
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

/* How opaque a fold is `at` of the way into its box: nearly whole for the first part of the way,
 * then fading out as it gets there, so that it reaches the box but is gone in it. */
static float fold_alpha(double at) {
    at = at < 0 ? 0 : at > 1 ? 1 : at;
    return (float)(1 - at * at);
}

/* A fold's scale `at` of the way: from 1 at rest to fold_scale inside the box. Scaling about the
 * fold's point takes the window's middle along a straight line to the box's. */
static double fold_scale_at(const struct sh_anim *anim, double at) {
    return 1 + (anim->fold_scale - 1) * at;
}

static void fold_apply(struct sh_anim *anim) {
    struct mapping m = IDENTITY;
    m.scale = (float)fold_scale_at(anim, anim->fold_at);
    m.alpha = fold_alpha(anim->fold_at);
    walk(anim, anim->tree, 0, 0, false, &m);
}

/* The fold's owner redraws what follows it; a copy that has gone has nothing left to draw. */
static void fold_updated(struct sh_anim *anim) {
    if (anim->folding.update)
        anim->folding.update(anim, anim->folding.data);
}

/* A window's fold stops where it is, its nodes back at rest. */
static void end_fold(struct sh_anim *anim) {
    if (!anim->fold)
        return;
    anim->fold = false;
    if (anim->owned)
        return;
    walk(anim, anim->tree, 0, 0, true, NULL);
    fold_updated(anim);
}

/* The size a morph draws the window at `elapsed` milliseconds into the glide, and how fast it
 * changes. */
static void morph_size(const struct sh_anim *anim, int64_t elapsed, double size[2],
                       double speed[2]) {
    size[0] = anim->rest_width, size[1] = anim->rest_height;
    speed[0] = speed[1] = 0;
    if (!anim->glide)
        return;
    double offset;
    glide_axis(anim, anim->size_x, anim->size_vx, elapsed, &offset, &speed[0]);
    size[0] += offset;
    glide_axis(anim, anim->size_y, anim->size_vy, elapsed, &offset, &speed[1]);
    size[1] += offset;
}

/* How opaque a morph's copy of what the client drew before is at `now`. */
static float fade_alpha(const struct sh_anim *anim, int64_t now) {
    return (float)(1 - eased(&FADE_CURVE, now - anim->fade_start, anim->fade_duration));
}

static void glide_setup(struct sh_animator *animator, struct sh_anim *anim, int dx, int dy,
                        enum sh_anim_kind kind, int duration, int width, int height, int64_t now);

/* The client had its time to draw the size it was given: the window eases to the size it drew
 * instead, rather than jumping there. */
static void morph_give_up(struct sh_anim *anim, int64_t now) {
    anim->answered = true;
    int duration = anim->animator ? duration_of(anim->animator, anim->morph_kind) : 0;
    if (duration > 0 &&
        (anim->live_width != anim->rest_width || anim->live_height != anim->rest_height))
        glide_setup(anim->animator, anim, 0, 0, anim->morph_kind, duration, anim->live_width,
                    anim->live_height, now);
}

/* Returns whether the animation is still running. */
static bool step(struct sh_anim *anim, int64_t now) {
    if (anim->morph && !anim->answered && now >= anim->wait_until)
        morph_give_up(anim, now);
    struct mapping m = IDENTITY;
    if (anim->morph) {
        double size[2], speed[2];
        morph_size(anim, now - anim->glide_start, size, speed);
        m.width = fmax(1, size[0]), m.height = fmax(1, size[1]);
    }
    if (anim->glide) {
        int64_t elapsed = now - anim->glide_start;
        double x, y, vx, vy;
        glide_axis(anim, anim->glide_x, anim->glide_vx, elapsed, &x, &vx);
        glide_axis(anim, anim->glide_y, anim->glide_vy, elapsed, &y, &vy);
        wlr_scene_node_set_position(&anim->tree->node, anim->base_x + (int)lround(x),
                                    anim->base_y + (int)lround(y));
        anim->glide = elapsed < anim->glide_duration;
    }
    // A morph lasts until the client has drawn its new size (or had its time), the glide is
    // over and what it drew before has faded out.
    bool ended = false;
    if (anim->morph) {
        if (anim->fading && now >= anim->fade_start + anim->fade_duration)
            drop_fading(anim);
        if (!anim->glide && anim->answered && !anim->fading) {
            ended = true;
            anim->morph = false;
        } else {
            // Waiting for the client, the window may draw nothing new for a while: the timer
            // keeps it going meanwhile.
            if (!anim->glide && anim->animator)
                keep_ticking(anim->animator);
            m.fade = anim->fading ? fade_alpha(anim, now) : 1;
            int width = (int)lround(m.width), height = (int)lround(m.height);
            if (width != anim->drawn_width || height != anim->drawn_height) {
                // The frame first, so that the walk below takes in where it is drawn.
                anim->drawn_width = width, anim->drawn_height = height;
                if (anim->resized)
                    anim->resized(anim->resized_data);
            }
        }
    }
    bool faded = anim->fx;
    if (anim->fx) {
        int64_t elapsed = now - anim->fx_start;
        double e = eased(&anim->fx_curve, elapsed, anim->fx_duration);
        anim->fx = elapsed < anim->fx_duration;
        m.scale = (float)(anim->scale_from + (anim->scale_to - anim->scale_from) * e);
        m.alpha = (float)(anim->alpha_from + (anim->alpha_to - anim->alpha_from) * e);
        if (!anim->fx)
            m.scale = m.alpha = 1;
    }
    if (anim->fx || anim->morph)
        walk(anim, anim->tree, 0, 0, false, &m);
    else if ((faded || ended) && !anim->owned)
        walk(anim, anim->tree, 0, 0, true, NULL);
    if (ended) {
        // Back at rest: the owner draws the frame for the window's own size again.
        anim->drawn_width = anim->drawn_height = -1;
        if (anim->resized)
            anim->resized(anim->resized_data);
    }
    // A fold runs alone on the nodes: neither a fade nor a morph runs beside it.
    if (anim->fold) {
        int64_t elapsed = now - anim->fold_start;
        double e = eased(&anim->fold_curve, elapsed, anim->fold_duration);
        anim->fold_at = anim->fold_from + (anim->fold_to - anim->fold_from) * e;
        if (elapsed < anim->fold_duration || anim->owned) {
            fold_apply(anim);
            fold_updated(anim);
        }
        if (elapsed >= anim->fold_duration)
            end_fold(anim);
        anim->fold = elapsed < anim->fold_duration;
    }
    return anim->fx || anim->glide || anim->morph || anim->fold;
}

void sh_anim_finish(struct sh_anim *anim) {
    if (!anim->animator)
        return;
    if (anim->glide)
        wlr_scene_node_set_position(&anim->tree->node, anim->base_x, anim->base_y);
    bool morphed = anim->morph;
    if (morphed) {
        drop_fading(anim);
        anim->morph = false;
    }
    if ((anim->fx || morphed) && !anim->owned)
        walk(anim, anim->tree, 0, 0, true, NULL);
    end_fold(anim);
    if (anim->held && !anim->owned)
        rest_hold(anim);
    void (*resized)(void *) = morphed ? anim->resized : NULL;
    void *data = anim->resized_data;
    stop(anim);
    if (resized)
        resized(data);
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
            keep_ticking(animator);
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
    if (anim->owned || anim->absent)
        wlr_scene_node_set_enabled(&anim->tree->node, false);
    if (anim->glide)
        wlr_scene_node_set_position(&anim->tree->node, anim->base_x, anim->base_y);
    if ((anim->fx || anim->morph || anim->fold) && !anim->owned)
        walk(anim, anim->tree, 0, 0, true, NULL);
    // A morph's copy is no window to point at, and its frame (the window's controls) is where
    // it will be: the next step draws it at the size drawn again.
    if (anim->fading)
        wlr_scene_node_set_enabled(&anim->fading->node, false);
    if (anim->morph) {
        anim->drawn_width = anim->drawn_height = -1;
        if (anim->resized)
            anim->resized(anim->resized_data);
    }
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
        bool sizes_change = (anim->fx && anim->scale_from != anim->scale_to) || anim->fold;
        if (!sizes_change && wlr_scene_node_coords(&anim->tree->node, &cx, &cy)) {
            double parent_x = cx - anim->tree->node.x, parent_y = cy - anim->tree->node.y;
            double rest_x = parent_x + anim->base_x, rest_y = parent_y + anim->base_y;
            matters = tree_covers(anim->tree, cx, cy, px, py) ||
                      (!anim->owned && tree_covers(anim->tree, rest_x, rest_y, px, py));
            // One changing size covers at rest the size its client drew, or the one it was
            // given, and what reaches past them.
            if (anim->morph && !anim->owned && !matters) {
                int width =
                    anim->live_width > anim->rest_width ? anim->live_width : anim->rest_width;
                int height =
                    anim->live_height > anim->rest_height ? anim->live_height : anim->rest_height;
                matters = px >= rest_x - MORPH_REACH && px < rest_x + width + MORPH_REACH &&
                          py >= rest_y - MORPH_REACH && py < rest_y + height + MORPH_REACH;
            }
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
        if (anim->owned || anim->absent)
            wlr_scene_node_set_enabled(&anim->tree->node, true);
        if (anim->fading)
            wlr_scene_node_set_enabled(&anim->fading->node, true);
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
    // The tweens' owners redraw a window's frame at rest first (its focus fade), and the
    // animations then draw that where they have the window.
    step_tweens(animator, now, false);
    struct sh_anim *anim, *next;
    wl_list_for_each_safe(anim, next, &animator->running, link) {
        if (!step(anim, now))
            stop(anim);
        else if (anim->track)
            anim->track(anim->track_data, false);
    }
    // What a client drew since keeps the hold's opacity.
    wl_list_for_each(anim, &animator->held, link) apply_hold(anim);
    if (wl_list_empty(&animator->running) && wl_list_empty(&animator->tweens))
        animator->last_tick = 0;
}

static int timer_fired(void *data) {
    struct sh_animator *animator = data;
    animator->ticking = false;
    sh_animator_tick(animator);
    if (!wl_list_empty(&animator->running) || !wl_list_empty(&animator->tweens))
        keep_ticking(animator);
    return 0;
}

static void start(struct sh_animator *animator, struct sh_anim *anim, struct wlr_scene_tree *tree,
                  int duration) {
    if (anim->animator && anim->tree != tree)
        sh_anim_finish(anim);
    release_track(anim); // another animation takes the tree over
    if (anim->absent) {
        // A slide out of sight taken over: the window is there again, to rest where it does and
        // at its full look, unless the new animation fades it itself.
        anim->absent = false;
        anim->base_x = anim->base_y = 0;
        if (anim->fx)
            walk(anim, anim->tree, 0, 0, true, NULL);
        anim->fx = false;
    }
    anim->tree = tree;
    if (!anim->animator) {
        anim->animator = animator;
        wl_list_insert(animator->running.prev, &anim->link);
    }
    if (!animator->last_tick)
        animator->last_tick = now_ms(animator);
    (void)duration; // the timer ticks until it ends, however long
    keep_ticking(animator);
}

/* The velocity a restarted glide adds to its curve's own so the speed stays the same, kept
 * within what could plausibly matter for a move of `offset` pixels. */
static double carried(double velocity, double curve_velocity, double offset, int duration) {
    double extra = velocity - curve_velocity, limit = 2 * fabs(offset) / duration;
    return extra > limit ? limit : extra < -limit ? -limit : extra;
}

/* Starts (or restarts) the glide by (dx, dy) from where the tree rests, and a morph's size to
 * `width` by `height`. */
/* Sets the glide going again at `now`, by (dx, dy) from where the tree rests, and a morph's size
 * to `width` by `height`, from where they are drawn. */
static void glide_setup(struct sh_animator *animator, struct sh_anim *anim, int dx, int dy,
                        enum sh_anim_kind kind, int duration, int width, int height, int64_t now) {
    struct wlr_scene_tree *tree = anim->tree;
    // A glide under way continues from where the window is drawn now, at the speed it has, and
    // so does the size of one changing size.
    double vx = 0, vy = 0, size[2] = {anim->rest_width, anim->rest_height}, speed[2] = {0, 0};
    bool moving = anim->glide;
    // One started this very millisecond, as by a placement within another, was never drawn:
    // only the speed it carried from before is the window's.
    bool unseen = moving && now == anim->glide_start;
    double carried_v[4] = {anim->glide_vx, anim->glide_vy, anim->size_vx, anim->size_vy};
    if (moving) {
        double x, y;
        glide_axis(anim, anim->glide_x, anim->glide_vx, now - anim->glide_start, &x, &vx);
        glide_axis(anim, anim->glide_y, anim->glide_vy, now - anim->glide_start, &y, &vy);
        if (anim->morph)
            morph_size(anim, now - anim->glide_start, size, speed);
    }
    anim->glide_x = tree->node.x - anim->base_x + dx;
    anim->glide_y = tree->node.y - anim->base_y + dy;
    anim->glide_duration = duration;
    anim->glide_curve = animator->config.styles[kind].curve;
    // The new curve has a speed of its own to start with; carry only what it lacks.
    const double h = 1e-4;
    double start_slope = (sh_curve_eval(&anim->glide_curve, h) - 0) / h / duration;
    anim->rest_width = width, anim->rest_height = height;
    anim->size_x = size[0] - width, anim->size_y = size[1] - height;
    if (unseen) {
        anim->glide_vx = carried(carried_v[0], 0, anim->glide_x, duration);
        anim->glide_vy = carried(carried_v[1], 0, anim->glide_y, duration);
        anim->size_vx = carried(carried_v[2], 0, anim->size_x, duration);
        anim->size_vy = carried(carried_v[3], 0, anim->size_y, duration);
    } else {
        anim->glide_vx =
            moving ? carried(vx, -anim->glide_x * start_slope, anim->glide_x, duration) : 0;
        anim->glide_vy =
            moving ? carried(vy, -anim->glide_y * start_slope, anim->glide_y, duration) : 0;
        anim->size_vx =
            moving ? carried(speed[0], -anim->size_x * start_slope, anim->size_x, duration) : 0;
        anim->size_vy =
            moving ? carried(speed[1], -anim->size_y * start_slope, anim->size_y, duration) : 0;
    }
    anim->glide = true;
    anim->glide_start = now;
    // A client still to draw its new size has the wait past the end of this glide.
    if (anim->morph)
        anim->wait_until = now + duration + MORPH_WAIT_MS;
}

static void glide_begin(struct sh_animator *animator, struct sh_anim *anim,
                        struct wlr_scene_tree *tree, int dx, int dy, enum sh_anim_kind kind,
                        int duration, int width, int height) {
    start(animator, anim, tree, duration);
    int64_t now = now_ms(animator);
    glide_setup(animator, anim, dx, dy, kind, duration, width, height, now);
    step(anim, now);
}

void sh_anim_glide_kind(struct sh_animator *animator, struct sh_anim *anim,
                        struct wlr_scene_tree *tree, int dx, int dy, enum sh_anim_kind kind) {
    int duration = animator->config.enabled ? duration_of(animator, kind) : 0;
    if (!duration || (dx == 0 && dy == 0))
        return;
    glide_begin(animator, anim, tree, dx, dy, kind, duration, anim->rest_width, anim->rest_height);
}

void sh_anim_morph(struct sh_animator *animator, struct sh_anim *anim, struct wlr_scene_tree *tree,
                   const struct sh_morph *morph) {
    int duration = animator->config.enabled ? duration_of(animator, morph->kind) : 0;
    if (!duration || anim->owned || anim->held || !morph->surfaces || morph->width < 1 ||
        morph->height < 1) {
        if (anim->morph)
            sh_anim_finish(anim); // it lands where it rests now
        sh_anim_glide_kind(animator, anim, tree, morph->dx, morph->dy, morph->kind);
        return;
    }
    if (anim->animator && anim->tree != tree)
        sh_anim_finish(anim);
    if (!anim->morph) {
        // From the size its client drew, which it is drawn at now.
        if (morph->from_width < 1 || morph->from_height < 1 || morph->live_width < 1 ||
            morph->live_height < 1) {
            sh_anim_glide_kind(animator, anim, tree, morph->dx, morph->dy, morph->kind);
            return;
        }
        anim->size_x = anim->size_y = anim->size_vx = anim->size_vy = 0;
        anim->rest_width = morph->from_width, anim->rest_height = morph->from_height;
        anim->live_width = morph->live_width, anim->live_height = morph->live_height;
        anim->surfaces = morph->surfaces;
        anim->drawn_width = anim->drawn_height = -1;
        anim->morph = true;
    }
    anim->morph_kind = morph->kind;
    anim->resized = morph->resized, anim->resized_data = morph->data;
    anim->answered = morph->answered;
    anim->asked_width = morph->live_width, anim->asked_height = morph->live_height;
    glide_begin(animator, anim, tree, morph->dx, morph->dy, morph->kind, duration, morph->width,
                morph->height);
}

/* A copy of what the client drew last, just over its surfaces, to fade out over what it draws
 * next, at another size. */
static void fade_begin(struct sh_anim *anim, int64_t now) {
    drop_fading(anim);
    struct wlr_scene_node *surfaces = &anim->surfaces->node;
    struct wlr_scene_tree *copy = wlr_scene_tree_create(surfaces->parent);
    if (!copy)
        return;
    if (!sh_thumb_clone(copy, anim->surfaces, 1, 1, NULL)) {
        wlr_scene_node_destroy(&copy->node);
        return;
    }
    // Drawn at other sizes than its buffers' and fading, no part of it hides what is under it.
    pixman_region32_t empty;
    pixman_region32_init(&empty);
    struct wlr_scene_node *node;
    wl_list_for_each(node, &copy->children, link) {
        if (node->type == WLR_SCENE_NODE_BUFFER)
            wlr_scene_buffer_set_opaque_region(wlr_scene_buffer_from_node(node), &empty);
    }
    pixman_region32_fini(&empty);
    wlr_scene_node_place_above(&copy->node, surfaces);
    wlr_scene_node_set_position(&copy->node, surfaces->x, surfaces->y);
    anim->fading = copy;
    anim->fading_width = anim->live_width, anim->fading_height = anim->live_height;
    // Over what is left of the glide, or half of it when the client answers once it is over.
    int64_t left = anim->glide ? anim->glide_start + anim->glide_duration - now : 0;
    anim->fade_start = now;
    anim->fade_duration = (int)fmax(fmax(left, anim->glide_duration / 2.0), 1);
}

void sh_anim_morph_commit(struct sh_animator *animator, struct sh_anim *anim, int width, int height,
                          bool answered) {
    if (!anim->morph || !anim->animator || anim->rested)
        return;
    int64_t now = now_ms(animator);
    if ((width != anim->live_width || height != anim->live_height) && width > 0 && height > 0) {
        // A copy fading already goes on fading: what it shows is the farthest from what the
        // client draws now, and dropping it, or another copy over it at full opacity, would pop.
        if (!anim->fading) {
            walk(anim, anim->surfaces, 0, 0, true, NULL); // copied as the client drew it
            fade_begin(anim, now);
        }
        anim->live_width = width, anim->live_height = height;
    }
    // A client may acknowledge the size it was given on a commit of what it drew before, and draw
    // the new size on a later one: the size it had when asked counts as its answer only once the
    // wait is over.
    bool other = width != anim->rest_width || height != anim->rest_height;
    bool early = width == anim->asked_width && height == anim->asked_height && other &&
                 now < anim->wait_until;
    if (answered && !early) {
        anim->answered = true;
        // At a size of its own, each time: the window eases on to it from where it is drawn.
        int duration = duration_of(animator, anim->morph_kind);
        if (duration > 0 && width > 0 && height > 0 && other)
            glide_begin(animator, anim, anim->tree, 0, 0, anim->morph_kind, duration, width,
                        height);
    }
    // What the client drew goes back to rest, to be read afresh once the scene has taken in the
    // commit, by the next frame: its new size can be just what the animation drew it at, which
    // the records would not tell from the size they last set.
    walk(anim, anim->surfaces, 0, 0, true, NULL);
    forget(anim, anim->surfaces);
}

bool sh_anim_drawn_size(const struct sh_anim *anim, int *width, int *height) {
    if (!anim->animator || !anim->morph || anim->drawn_width < 1 || anim->drawn_height < 1)
        return false;
    *width = anim->drawn_width;
    *height = anim->drawn_height;
    return true;
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
        walk(anim, anim->tree, 0, 0, true, NULL); // what it shows now is not its resting look
    end_fold(anim);
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
        glide_begin(animator, anim, tree, dx, dy, SH_ANIM_WORKSPACE, duration, anim->rest_width,
                    anim->rest_height);
    fx_begin(animator, anim, SH_ANIM_WORKSPACE, duration, 1, 1, 0, 1, 0, 0);
    step(anim, anim->fx_start);
}

/* A tree just above `window` holding a copy of what is visible under `content` (flattened, as
 * the overview's thumbnails are, at full size), where the window is, owned by a new animation
 * that is neither running nor held; NULL if there is nothing to copy. */
static struct sh_anim *make_copy(struct wlr_scene_node *window, struct wlr_scene_tree *content) {
    struct sh_anim *anim = calloc(1, sizeof(*anim));
    struct wlr_scene_tree *tree = anim ? wlr_scene_tree_create(window->parent) : NULL;
    if (!tree) {
        free(anim);
        return NULL;
    }
    if (!sh_thumb_clone(tree, content, 1, 1, NULL)) {
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
    glide_begin(animator, anim, anim->tree, 0, 0, SH_ANIM_WORKSPACE, duration, anim->rest_width,
                anim->rest_height);
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
        glide_begin(animator, anim, tree, dx, dy, SH_ANIM_WORKSPACE, duration, anim->rest_width,
                    anim->rest_height);
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

/* The scale a fold reaches inside its box and the point it scales about, so that the window
 * shrinks to fit the box, keeping its proportions, as its middle slides on a straight line to the
 * box's; false when either box is empty. It always shrinks some, and never to nothing. */
static bool fold_geometry(const struct sh_fold *fold, float *scale, double *cx, double *cy) {
    if (fold->rest_width <= 0 || fold->rest_height <= 0 || fold->into_width <= 0 ||
        fold->into_height <= 0)
        return false;
    double s = fmin((double)fold->into_width / fold->rest_width,
                    (double)fold->into_height / fold->rest_height);
    s = fmax(0.001, fmin(s, 0.8));
    double rest_x = fold->rest_x + fold->rest_width / 2.0;
    double rest_y = fold->rest_y + fold->rest_height / 2.0;
    double into_x = fold->into_x + fold->into_width / 2.0;
    double into_y = fold->into_y + fold->into_height / 2.0;
    *scale = (float)s;
    *cx = rest_x + (into_x - rest_x) / (1 - s);
    *cy = rest_y + (into_y - rest_y) / (1 - s);
    return true;
}

/* The minimize time for `share` of the way: a fold that turns back early has less to undo, and
 * takes about the time a spring would for that distance. */
static int fold_duration(const struct sh_animator *animator, double share) {
    int duration = animator->config.enabled ? duration_of(animator, SH_ANIM_MINIMIZE) : 0;
    share = fabs(share);
    if (!duration || share <= 0)
        return 0;
    return (int)fmax(1, lround(duration * sqrt(fmin(share, 1))));
}

static void fold_begin(struct sh_animator *animator, struct sh_anim *anim,
                       const struct sh_fold *fold, float scale, double cx, double cy,
                       double from, double to, int duration) {
    anim->fold = true;
    anim->fold_from = anim->fold_at = from;
    anim->fold_to = to;
    anim->fold_start = now_ms(animator);
    anim->fold_duration = duration;
    anim->fold_curve = animator->config.styles[SH_ANIM_MINIMIZE].curve;
    anim->fold_scale = scale;
    anim->folding = *fold;
    anim->cx = cx, anim->cy = cy;
}

struct sh_anim *sh_anim_fold_away(struct sh_animator *animator, struct wlr_scene_tree *copy,
                                  const struct sh_fold *fold, double from, struct sh_anim **ref) {
    from = fmax(0, from);
    int duration = fold_duration(animator, 1 - from);
    float scale;
    double cx, cy;
    struct sh_anim *anim = NULL;
    if (duration && fold_geometry(fold, &scale, &cx, &cy))
        anim = calloc(1, sizeof(*anim));
    if (!anim) {
        wlr_scene_node_destroy(&copy->node);
        return NULL;
    }
    anim->owned = true;
    anim->tree = copy;
    start(animator, anim, copy, duration);
    fold_begin(animator, anim, fold, scale, cx, cy, from, 1, duration);
    anim->ref = ref;
    if (ref)
        *ref = anim;
    step(anim, anim->fold_start);
    return anim;
}

bool sh_anim_unfold(struct sh_animator *animator, struct sh_anim *anim, struct wlr_scene_tree *tree,
                    const struct sh_fold *fold, double from) {
    from = fmin(1, from);
    int duration = fold_duration(animator, from);
    float scale;
    double cx, cy;
    if (anim->owned || !duration || !fold_geometry(fold, &scale, &cx, &cy))
        return false;
    if (anim->held || (anim->animator && anim->tree != tree))
        sh_anim_finish(anim); // a gesture's hold, or another tree's animation, lands
    else
        sh_anim_settle(anim); // a glide goes on beneath the fold
    start(animator, anim, tree, duration);
    fold_begin(animator, anim, fold, scale, cx, cy, from, 0, duration);
    step(anim, anim->fold_start);
    return true;
}

double sh_anim_folded(const struct sh_anim *anim, struct sh_fold *fold) {
    if (!anim->animator || !anim->fold)
        return -1;
    if (fold)
        *fold = anim->folding;
    return anim->fold_at;
}

double sh_anim_settle(struct sh_anim *anim) {
    if (!anim->animator || anim->owned)
        return 0;
    double at = anim->fold ? anim->fold_at : 0;
    if (anim->fx) {
        anim->fx = false;
        walk(anim, anim->tree, 0, 0, true, NULL);
    }
    end_fold(anim);
    if (!anim->glide && !anim->held)
        stop(anim);
    return at;
}

void sh_anim_fold_map(const struct sh_anim *anim, int *x, int *y, int *width, int *height,
                      int *radius) {
    if (!anim->animator || !anim->fold)
        return;
    // As apply_node scales a node: both edges, rounded, so that the box stays on the node's.
    double scale = fold_scale_at(anim, anim->fold_at);
    long left = lround(anim->cx + (*x - anim->cx) * scale);
    long top = lround(anim->cy + (*y - anim->cy) * scale);
    long right = lround(anim->cx + (*x + *width - anim->cx) * scale);
    long bottom = lround(anim->cy + (*y + *height - anim->cy) * scale);
    *x = (int)left, *y = (int)top;
    *width = (int)fmax(1, right - left), *height = (int)fmax(1, bottom - top);
    if (radius)
        *radius = (int)lround(*radius * scale);
}

void sh_anim_spring_back(struct sh_animator *animator, struct sh_anim *anim,
                         struct wlr_scene_tree *tree, double dx, double dy, double vx, double vy,
                         float alpha) {
    if (anim->owned)
        return;
    sh_anim_finish(anim); // from rest, as the hold left it
    int duration = animator->config.enabled ? duration_of(animator, SH_ANIM_MOVE) : 0;
    bool moved = lround(dx) || lround(dy);
    if (!duration || (!moved && alpha >= 1))
        return;
    start(animator, anim, tree, duration);
    int64_t now = now_ms(animator);
    if (moved) {
        anim->glide_x = tree->node.x - anim->base_x + dx;
        anim->glide_y = tree->node.y - anim->base_y + dy;
        anim->glide_duration = duration;
        anim->glide_curve = animator->config.styles[SH_ANIM_MOVE].curve;
        // The curve starts at a speed of its own; the release adds what it lacks.
        const double h = 1e-4;
        double start_slope = sh_curve_eval(&anim->glide_curve, h) / h / duration;
        anim->glide_vx = carried(vx, -anim->glide_x * start_slope, anim->glide_x, duration);
        anim->glide_vy = carried(vy, -anim->glide_y * start_slope, anim->glide_y, duration);
        anim->glide = true;
        anim->glide_start = now;
    }
    if (alpha < 1)
        fx_begin(animator, anim, SH_ANIM_MOVE, duration, 1, 1, alpha, 1, 0, 0);
    step(anim, now);
}

void sh_anim_nudge(struct sh_animator *animator, struct sh_anim *anim, struct wlr_scene_tree *tree,
                   int dx, int dy) {
    int duration = animator->config.enabled ? duration_of(animator, SH_ANIM_WORKSPACE) : 0;
    if (!duration || (!dx && !dy) || anim->owned || anim->held)
        return;
    duration *= 2; // there and back
    int64_t now = now_ms(animator);
    bool gliding = anim->animator && anim->glide && anim->tree == tree;
    if (gliding) {
        double x, y, vx, vy;
        glide_axis(anim, anim->glide_x, anim->glide_vx, now - anim->glide_start, &x, &vx);
        glide_axis(anim, anim->glide_y, anim->glide_vy, now - anim->glide_start, &y, &vy);
        if (vx * dx + vy * dy > 0)
            return; // on its way there already
    }
    start(animator, anim, tree, duration);
    // From where it is drawn: what is left of that offset at the turning point, a third of the
    // way through, plus the push makes (dx, dy) there. A glide with no offset of its own and a
    // starting speed v goes out to v * duration * 4/27 at that point and comes back to rest.
    double x = gliding ? tree->node.x - anim->base_x : 0;
    double y = gliding ? tree->node.y - anim->base_y : 0;
    anim->glide_duration = duration;
    anim->glide_curve = animator->config.styles[SH_ANIM_WORKSPACE].curve;
    double left = 1 - sh_curve_eval(&anim->glide_curve, 1.0 / 3);
    anim->glide_x = x;
    anim->glide_y = y;
    anim->glide_vx = (dx - x * left) * 27 / (4.0 * duration);
    anim->glide_vy = (dy - y * left) * 27 / (4.0 * duration);
    anim->glide = true;
    anim->glide_start = now;
    step(anim, now);
}

/* A drop goes further than anything else moves, the window's own height and more: dropping in
 * takes this many times as long as opening a window, and sliding out (quicker, as leaving
 * should be) as long as closing one. */
static const double DROP_IN_STRETCH = 2, DROP_OUT_STRETCH = 1.5;
/* The share of a drop its fade takes: the start of dropping in, the end of sliding out. */
static const double DROP_FADE = 0.4;
/* The share a drop turning back keeps of the speed a glide would carry (what the new curve lacks
 * of the speed it had, held within twice the distance per duration): it eases round rather than
 * bouncing off, without carrying on far the way it went (all of it would carry a window shown
 * and hidden again a sixth of its height further down before it turned). */
static const double DROP_KEEP = 0.25;

/* The factor of its nodes' opacity the animation draws at `now`; 1 at rest. A fade that has yet
 * to start draws where it starts. */
static float drawn_alpha(const struct sh_anim *anim, int64_t now) {
    if (anim->held)
        return anim->held_alpha;
    if (!anim->animator || !anim->fx)
        return 1;
    double e = eased(&anim->fx_curve, now - anim->fx_start, anim->fx_duration);
    return (float)(anim->alpha_from + (anim->alpha_to - anim->alpha_from) * e);
}

float sh_anim_alpha(const struct sh_anim *anim) {
    return anim->animator ? drawn_alpha(anim, now_ms(anim->animator)) : 1;
}

/* A change of size landed where it is, the glide going on: the client's surfaces drawn at the
 * size they were drawn for, the copy gone, and the frame drawn for them again. */
static void morph_end(struct sh_anim *anim) {
    if (!anim->morph)
        return;
    drop_fading(anim);
    anim->morph = false;
    if (anim->surfaces)
        walk(anim, anim->surfaces, 0, 0, true, NULL);
    anim->drawn_width = anim->drawn_height = -1;
    if (anim->resized)
        anim->resized(anim->resized_data);
}

static bool drop(struct sh_animator *animator, struct sh_anim *anim, struct wlr_scene_tree *tree,
                 int dx, int dy, bool out) {
    if (anim->owned)
        return false;
    enum sh_anim_kind kind = out ? SH_ANIM_CLOSE : SH_ANIM_OPEN;
    int length = animator->config.enabled ? duration_of(animator, kind) : 0;
    if (anim->held || (anim->animator && anim->tree != tree))
        sh_anim_finish(anim); // from rest, as the gesture or what ran elsewhere leaves it
    // A change of size lands; the slide goes on from where the window is.
    morph_end(anim);
    if (!length) {
        anim->base_x = anim->base_y = 0; // a slide out under way lands where the tree rests
        sh_anim_finish(anim);
        return false;
    }
    int duration = (int)fmax(1, lround(length * (out ? DROP_OUT_STRETCH : DROP_IN_STRETCH)));
    int64_t now = now_ms(animator);
    // From where it is drawn and as faded as it is, at the speed it has; dropping in afresh,
    // from out of sight.
    bool moving = anim->animator && anim->glide;
    float alpha = out ? drawn_alpha(anim, now) : moving ? drawn_alpha(anim, now) : 0;
    double vx = 0, vy = 0;
    if (moving) {
        double x, y;
        glide_axis(anim, anim->glide_x, anim->glide_vx, now - anim->glide_start, &x, &vx);
        glide_axis(anim, anim->glide_y, anim->glide_vy, now - anim->glide_start, &y, &vy);
    }
    int from_x = moving || out ? tree->node.x : dx, from_y = moving || out ? tree->node.y : dy;
    start(animator, anim, tree, duration);
    anim->base_x = out ? dx : 0;
    anim->base_y = out ? dy : 0;
    anim->glide_x = from_x - anim->base_x;
    anim->glide_y = from_y - anim->base_y;
    anim->glide_duration = duration;
    anim->glide_curve = animator->config.styles[kind].curve;
    if (out)
        anim->glide_curve.kind |= SH_CURVE_REVERSED;
    // The curve has a speed of its own to start with; carry some of what it lacks.
    const double h = 1e-4;
    double start_slope = sh_curve_eval(&anim->glide_curve, h) / h / duration;
    anim->glide_vx =
        moving ? DROP_KEEP * carried(vx, -anim->glide_x * start_slope, anim->glide_x, duration) : 0;
    anim->glide_vy =
        moving ? DROP_KEEP * carried(vy, -anim->glide_y * start_slope, anim->glide_y, duration) : 0;
    anim->glide = true;
    anim->glide_start = now;
    int fade = (int)fmax(1, lround(duration * DROP_FADE));
    fx_begin(animator, anim, kind, fade, 1, 1, alpha, out ? 0 : 1, 0, 0);
    anim->fx_curve = (struct sh_curve){.kind = SH_CURVE_LINEAR};
    // Sliding out, the fade ends as the slide does, and the tree hides with both done.
    anim->fx_start = out ? now + duration - fade : now;
    anim->absent = out;
    step(anim, now);
    return true;
}

bool sh_anim_drop_in(struct sh_animator *animator, struct sh_anim *anim,
                     struct wlr_scene_tree *tree, int dx, int dy) {
    return drop(animator, anim, tree, dx, dy, false);
}

bool sh_anim_drop_out(struct sh_animator *animator, struct sh_anim *anim,
                      struct wlr_scene_tree *tree, int dx, int dy) {
    return drop(animator, anim, tree, dx, dy, true);
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
