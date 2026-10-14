/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The desktop cat's behaviour (see paw/cat.h): a state machine the compositor steps on a timer,
 * with gravity, jumps and runs, and a cat's idle life in between. */
#include "paw/cat.h"
#include <math.h>
#include <stddef.h>

/* Motion, in logical pixels and seconds. */
#define GRAVITY 2600.0
#define FALL_MAX 1700.0
#define HOP_SPEED 320.0 /* the startled hop as what it sits on goes */
#define WALK_SPEED 40.0
#define RUN_SPEED 420.0
#define STEP_MS 16 /* the longest stretch of time one step of the motion covers */
#define GAP_MS 250 /* a longer one, while it was hidden, is not moved through at all */

/* How long things take, in milliseconds. */
enum {
    LAND_MS = 240,
    CROUCH_MS = 140,
    NOTICE_MS = 220,
    POUNCE_MS = 700,
    KNOCK_MS = 6000,
    GROOM_MS = 2600,
    SCRATCH_MS = 1500,
    YAWN_MS = 1400,
    WAKE_MS = 1100,
    LOOK_MS = 3000,
    BLINK_MS = 160,
    BREATH_MS = 2200,
    REST_MS = 350,   /* the pointer resting this long ends a chase */
    CHASE_MS = 10000,
    AWAY_MS = 1200,  /* off the focused monitor this long, it goes there */
};

/* A flick past the cat: the pointer's travel, decaying with a time constant of TRAVEL_MS, beyond
 * FLICK pixels in at least FLICK_MOTIONS motions each within MOTION_MS of the last, while it is
 * within NEAR pixels of the cat. */
#define TRAVEL_MS 120.0
#define FLICK 90.0
#define NEAR 320.0
#define FLICK_MOTIONS 3
#define MOTION_MS 100

static double clamp(double value, double low, double high) {
    return value < low ? low : value > high ? high : value;
}

static uint32_t next_random(struct sh_cat *cat) {
    uint32_t x = cat->random ? cat->random : 0x9e3779b9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    cat->random = x;
    return x;
}

/* A whole number from `low` to `high`, both included. */
static int between(struct sh_cat *cat, int low, int high) {
    return low + (int)(next_random(cat) % (uint32_t)(high - low + 1));
}

const char *sh_cat_state_name(enum sh_cat_state state) {
    static const char *const names[SH_CAT_STATES] = {
        "sit",  "look", "walk", "groom",  "scratch", "yawn",  "sleep", "wake",
        "jump", "fall", "land", "chase", "pounce",  "rush",  "knock",
    };
    return (unsigned)state < SH_CAT_STATES ? names[state] : "sit";
}

int sh_cat_area_at(const struct sh_cat_world *world, double x, double y) {
    int nearest = -1;
    double best = INFINITY;
    for (int i = 0; i < world->area_count; ++i) {
        const struct sh_cat_area *area = &world->areas[i];
        // Its feet stand on the area's bottom edge, which is the area's.
        double dx = x < area->x ? area->x - x : x >= area->x + area->width ? x - (area->x + area->width) + 1 : 0;
        double dy = y - 0.5 < area->y ? area->y - (y - 0.5)
                    : y - 0.5 >= area->y + area->height ? y - 0.5 - (area->y + area->height) + 1
                                                         : 0;
        if (dx == 0 && dy == 0)
            return i;
        if (dx + dy < best) {
            best = dx + dy;
            nearest = i;
        }
    }
    return nearest;
}

static const struct sh_cat_area *area_of(const struct sh_cat *cat, const struct sh_cat_world *world) {
    int index = sh_cat_area_at(world, cat->x, cat->y);
    return index < 0 ? NULL : &world->areas[index];
}

/* `x` kept where the whole cat shows on `area`. */
static double inside_x(const struct sh_cat *cat, const struct sh_cat_area *area, double x) {
    if (!area || area->width < cat->width)
        return x;
    return clamp(x, area->x + cat->width / 2, area->x + area->width - cat->width / 2);
}

/* The ledge of `window` (any window for UINT32_MAX) among `ledges` that a cat at (x, y) stands
 * on, or NULL. */
static const struct sh_cat_ledge *ledge_at(const struct sh_cat_ledge *ledges, int count,
                                           uint32_t window, double x, double y) {
    for (int i = 0; i < count; ++i) {
        const struct sh_cat_ledge *ledge = &ledges[i];
        if ((window == UINT32_MAX || ledge->window == window) && fabs(ledge->y - y) < 0.5 &&
            x >= ledge->left && x <= ledge->right)
            return ledge;
    }
    return NULL;
}

static bool supported(const struct sh_cat *cat, const struct sh_cat_world *world) {
    return ledge_at(world->tops, world->top_count, cat->on, cat->x, cat->y) != NULL;
}

static void enter(struct sh_cat *cat, enum sh_cat_state state, int64_t now, int duration) {
    cat->state = state;
    cat->since = now;
    cat->until = now + duration;
}

static void sit(struct sh_cat *cat, int64_t now) {
    enter(cat, SH_CAT_SIT, now, between(cat, 2500, 6500));
    cat->drowsy = false;
}

static void land(struct sh_cat *cat, const struct sh_cat_ledge *ledge, int64_t now) {
    cat->y = ledge->y;
    cat->vx = cat->vy = 0;
    cat->grounded = true;
    cat->on = ledge->window;
    enter(cat, SH_CAT_LAND, now, LAND_MS);
}

/* Falls from where it is, starting at a vertical speed of `vy`. */
static void fall(struct sh_cat *cat, double vy, int64_t now) {
    cat->grounded = false;
    cat->aim = 0;
    cat->vx = 0;
    cat->vy = vy;
    enter(cat, SH_CAT_FALL, now, 0);
}

/* Leaps to (x, y) on `window`, after a crouch. */
static void jump(struct sh_cat *cat, double x, double y, uint32_t window, int64_t now) {
    double dx = x - cat->x, dy = y - cat->y;
    double distance = sqrt(dx * dx + dy * dy);
    cat->from_x = cat->x;
    cat->from_y = cat->y;
    cat->target_x = x;
    cat->target_y = y;
    cat->target_window = window;
    cat->jump_ms = (int)clamp(320 + 0.45 * distance, 320, 900);
    // High enough to clear the higher end, more so the further up or along it goes.
    cat->jump_arc = 24 + (dy < 0 ? -dy * 0.6 : 0) + fabs(dx) * 0.08;
    if (fabs(dx) >= 1)
        cat->facing = dx > 0 ? 1 : -1;
    enter(cat, SH_CAT_JUMP, now, CROUCH_MS + cat->jump_ms);
}

/* The point nearest the cat on the parts of `window`'s top that show on the cat's monitor; false
 * without one. */
static bool spot_on(const struct sh_cat *cat, const struct sh_cat_world *world, uint32_t window,
                    double *x, double *y) {
    const struct sh_cat_area *area = area_of(cat, world);
    bool found = false;
    double best = INFINITY;
    for (int i = 0; i < world->ledge_count; ++i) {
        const struct sh_cat_ledge *ledge = &world->ledges[i];
        if (ledge->window != window)
            continue;
        double left = ledge->left, right = ledge->right;
        if (area) { // its part on the cat's monitor
            left = fmax(left, area->x);
            right = fmin(right, area->x + area->width);
        }
        if (right - left < cat->width * 0.75)
            continue;
        double spot = right - left < cat->width
                          ? (left + right) / 2
                          : clamp(cat->x, left + cat->width / 2, right - cat->width / 2);
        double distance = fabs(spot - cat->x) + fabs(ledge->y - cat->y);
        if (distance < best) {
            best = distance;
            *x = spot;
            *y = ledge->y;
            found = true;
        }
    }
    return found;
}

void sh_cat_arrive(struct sh_cat *cat, const struct sh_cat_world *world, int64_t now) {
    if (world->area_count < 1)
        return;
    const struct sh_cat_area *area = &world->areas[world->focused_area >= 0 ? world->focused_area : 0];
    double x = area->x + area->width / 2;
    uint32_t aim = 0;
    for (int i = 0; i < world->ledge_count; ++i) {
        const struct sh_cat_ledge *ledge = &world->ledges[i];
        double left = fmax(ledge->left, area->x), right = fmin(ledge->right, area->x + area->width);
        if (world->focused && ledge->window == world->focused && right - left >= cat->width) {
            x = (left + right) / 2;
            aim = ledge->window;
            break;
        }
    }
    cat->x = x;
    cat->y = area->top + cat->height;
    cat->away_since = 0;
    fall(cat, 0, now);
    cat->aim = aim; // it drops in front of the windows above that one
}

/* Whether the window a fall aims for still has its top below `y`, under the cat. */
static bool aimed_below(const struct sh_cat *cat, const struct sh_cat_world *world, double y) {
    for (int i = 0; i < world->top_count; ++i) {
        const struct sh_cat_ledge *top = &world->tops[i];
        if (top->window == cat->aim && top->y >= y - 0.01 && cat->x >= top->left &&
            cat->x <= top->right)
            return true;
    }
    return false;
}

/* Whether a cat in `state` is free to take up something else. */
static bool calm(enum sh_cat_state state) {
    return state == SH_CAT_SIT || state == SH_CAT_LOOK || state == SH_CAT_WALK ||
           state == SH_CAT_GROOM || state == SH_CAT_SCRATCH || state == SH_CAT_YAWN;
}

/* Runs to the urgent window's top, when it shows on the cat's monitor. */
static bool rush(struct sh_cat *cat, const struct sh_cat_world *world, int64_t now) {
    const struct sh_cat_area *area = area_of(cat, world);
    for (int i = 0; i < world->top_count; ++i) {
        const struct sh_cat_ledge *top = &world->tops[i];
        if (top->window != world->urgent)
            continue;
        double left = top->left, right = top->right;
        if (area && (right <= area->x || left >= area->x + area->width))
            return false;
        if (area) {
            left = fmax(left, area->x);
            right = fmin(right, area->x + area->width);
        }
        double margin = right - left > 3 * cat->width ? cat->width : (right - left) / 2;
        cat->target_x = clamp(cat->x, left + margin, right - margin);
        cat->target_y = top->y;
        cat->target_window = top->window;
        enter(cat, SH_CAT_RUSH, now, 0);
        return true;
    }
    return false;
}

/* What a cat sitting about does next. */
static void choose(struct sh_cat *cat, const struct sh_cat_world *world, int64_t now) {
    int area = sh_cat_area_at(world, cat->x, cat->y);
    if (world->focused_area >= 0 && area != world->focused_area) {
        // Along the window it sits on when that reaches the focused monitor, else it turns up
        // there.
        if (!cat->away_since)
            cat->away_since = now;
        if (now - cat->away_since < AWAY_MS) { // it sits a moment first
            sit(cat, now);
            cat->until = cat->away_since + AWAY_MS;
            return;
        }
        const struct sh_cat_area *focused = &world->areas[world->focused_area];
        const struct sh_cat_ledge *ledge =
            ledge_at(world->ledges, world->ledge_count, cat->on, cat->x, cat->y);
        double left = ledge ? fmax(ledge->left, focused->x) : 0;
        double right = ledge ? fmin(ledge->right, focused->x + focused->width) : 0;
        if (cat->on && ledge && right - left >= cat->width) {
            cat->target_x = cat->x < left ? left + cat->width / 2 : right - cat->width / 2;
            cat->facing = cat->target_x > cat->x ? 1 : -1;
            enter(cat, SH_CAT_WALK, now, 0);
        } else {
            sh_cat_arrive(cat, world, now);
        }
        return;
    } else {
        cat->away_since = 0;
    }
    double x = 0, y = 0;
    if (world->focused && world->focused != cat->on && spot_on(cat, world, world->focused, &x, &y)) {
        jump(cat, x, y, world->focused, now);
        return;
    }
    int roll = between(cat, 0, 99);
    if (roll < 30) {
        // A stroll along the part of its ledge that shows.
        const struct sh_cat_ledge *ledge =
            ledge_at(world->ledges, world->ledge_count, cat->on, cat->x, cat->y);
        const struct sh_cat_area *here = area_of(cat, world);
        if (ledge) {
            double left = ledge->left + cat->width / 2, right = ledge->right - cat->width / 2;
            if (here) {
                left = fmax(left, here->x + cat->width / 2);
                right = fmin(right, here->x + here->width - cat->width / 2);
            }
            left = fmax(left, cat->x - 160);
            right = fmin(right, cat->x + 160);
            if (right - left >= 24) {
                double to = left + (right - left) * between(cat, 0, 1000) / 1000.0;
                if (fabs(to - cat->x) >= 12) {
                    cat->target_x = to;
                    cat->facing = to > cat->x ? 1 : -1;
                    enter(cat, SH_CAT_WALK, now, 0);
                    return;
                }
            }
        }
    } else if (roll < 45) {
        enter(cat, SH_CAT_GROOM, now, GROOM_MS);
        return;
    } else if (roll < 55) {
        enter(cat, SH_CAT_SCRATCH, now, SCRATCH_MS);
        return;
    } else if (roll < 62) {
        enter(cat, SH_CAT_YAWN, now, YAWN_MS);
        return;
    } else if (roll < 75) {
        cat->facing = between(cat, 0, 1) ? 1 : -1;
        enter(cat, SH_CAT_LOOK, now, LOOK_MS);
        return;
    }
    sit(cat, now);
}

/* Moves (x, y) toward (to_x, to_y) by up to `distance`; true once there. */
static bool approach(struct sh_cat *cat, double to_x, double to_y, double distance) {
    double dx = to_x - cat->x, dy = to_y - cat->y;
    double length = sqrt(dx * dx + dy * dy);
    if (fabs(dx) >= 1)
        cat->facing = dx > 0 ? 1 : -1;
    if (length <= distance) {
        cat->walked += length;
        cat->x = to_x;
        cat->y = to_y;
        return true;
    }
    cat->walked += distance;
    cat->x += dx / length * distance;
    cat->y += dy / length * distance;
    return false;
}

/* Milliseconds from `now` to `at`, at least 1. */
static int until(int64_t now, int64_t at) {
    return at - now < 1 ? 1 : at - now > 60000 ? 60000 : (int)(at - now);
}

/* Milliseconds until the next whole `period` since `since`. */
static int next_beat(int64_t now, int64_t since, int period) {
    return period - (int)((now - since) % period);
}

/* One step of `dt` seconds ending at `now`; returns when the next is due. */
static int advance(struct sh_cat *cat, const struct sh_cat_world *world, int64_t now, double dt) {
    const struct sh_cat_area *area = area_of(cat, world);
    // What held it up moved away from under it, or went: a startled hop, then down.
    if (cat->grounded && !supported(cat, world)) {
        cat->drowsy = false;
        fall(cat, -HOP_SPEED, now);
    }
    if (!world->urgent)
        cat->knocked = 0; // a window urgent again later gets pawed at again
    bool urgent = world->urgent && world->urgent != cat->knocked;
    if (calm(cat->state) && !(cat->state == SH_CAT_YAWN && cat->drowsy) &&
        !(urgent && rush(cat, world, now)) && world->idle) {
        enter(cat, SH_CAT_YAWN, now, YAWN_MS);
        cat->drowsy = true;
    }
    if (cat->state == SH_CAT_SLEEP && (!world->idle || urgent))
        enter(cat, SH_CAT_WAKE, now, WAKE_MS);

    switch (cat->state) {
    case SH_CAT_SIT: {
        if (now >= cat->blink_at + BLINK_MS)
            cat->blink_at = now + between(cat, 2500, 5500);
        if (now >= cat->until) {
            choose(cat, world, now);
            if (cat->state != SH_CAT_SIT)
                return 1;
        }
        int64_t blink = now < cat->blink_at ? cat->blink_at : cat->blink_at + BLINK_MS;
        return until(now, cat->until < blink ? cat->until : blink);
    }
    case SH_CAT_LOOK:
    case SH_CAT_LAND:
    case SH_CAT_WAKE:
        if (now >= cat->until)
            sit(cat, now);
        return cat->state == SH_CAT_LAND ? STEP_MS : until(now, cat->until);
    case SH_CAT_GROOM:
    case SH_CAT_SCRATCH:
        if (now >= cat->until) {
            sit(cat, now);
            return until(now, cat->until);
        }
        return next_beat(now, cat->since, cat->state == SH_CAT_GROOM ? 300 : 110);
    case SH_CAT_YAWN:
        if (now >= cat->until) {
            if (cat->drowsy && world->idle)
                enter(cat, SH_CAT_SLEEP, now, 0);
            else
                sit(cat, now);
            return 1;
        }
        return next_beat(now, cat->since, 250);
    case SH_CAT_SLEEP:
        return next_beat(now, cat->since, BREATH_MS);
    case SH_CAT_WALK: {
        double step = WALK_SPEED * dt;
        double ahead = cat->x + (cat->target_x > cat->x ? 1 : -1) * fmin(step, fabs(cat->target_x - cat->x));
        // It stops at the end of what it walks on.
        if (!ledge_at(world->tops, world->top_count, cat->on, ahead, cat->y)) {
            sit(cat, now);
            return 1;
        }
        if (approach(cat, cat->target_x, cat->y, step))
            sit(cat, now);
        return (int)(1000 / WALK_SPEED);
    }
    case SH_CAT_JUMP: {
        int64_t t = now - cat->since;
        if (t < CROUCH_MS)
            return until(now, cat->since + CROUCH_MS);
        cat->grounded = false;
        double p = (double)(t - CROUCH_MS) / cat->jump_ms;
        double dy = cat->target_y - cat->from_y;
        if (p >= 1) {
            cat->x = cat->target_x;
            cat->y = cat->target_y;
            const struct sh_cat_ledge *ledge = ledge_at(world->tops, world->top_count,
                                                        cat->target_window, cat->x, cat->y);
            if (ledge)
                land(cat, ledge, now);
            else // it went while the cat was on its way: on down from there
                fall(cat, (dy + 4 * cat->jump_arc) / cat->jump_ms * 1000, now);
            return STEP_MS;
        }
        cat->x = cat->from_x + (cat->target_x - cat->from_x) * p;
        cat->y = cat->from_y + dy * p - 4 * cat->jump_arc * p * (1 - p);
        return STEP_MS;
    }
    case SH_CAT_FALL: {
        double previous = cat->y;
        cat->vy = fmin(cat->vy + GRAVITY * dt, FALL_MAX);
        cat->x = inside_x(cat, area, cat->x + cat->vx * dt);
        cat->y += cat->vy * dt;
        if (cat->aim && !aimed_below(cat, world, previous))
            cat->aim = 0; // what it aimed for went: anything below will do
        if (cat->vy > 0) {
            // The first ledge it passes on the way down.
            const struct sh_cat_ledge *landing = NULL;
            for (int i = 0; i < world->ledge_count; ++i) {
                const struct sh_cat_ledge *ledge = &world->ledges[i];
                if (ledge->y >= previous - 0.01 && ledge->y <= cat->y && cat->x >= ledge->left &&
                    cat->x <= ledge->right && (!landing || ledge->y < landing->y) &&
                    (!cat->aim || ledge->window == cat->aim || !ledge->window))
                    landing = ledge;
            }
            struct sh_cat_ledge floor = {0, cat->x, cat->x, area ? area->bottom : cat->y};
            if (!landing && area && cat->y >= area->bottom)
                landing = &floor;
            if (landing)
                land(cat, landing, now);
        }
        return STEP_MS;
    }
    case SH_CAT_CHASE:
    case SH_CAT_RUSH: {
        int64_t t = now - cat->since;
        if (t < NOTICE_MS)
            return until(now, cat->since + NOTICE_MS);
        cat->grounded = false;
        if (cat->state == SH_CAT_RUSH) {
            if (world->urgent != cat->target_window) { // it has had attention meanwhile
                fall(cat, 0, now);
                return STEP_MS;
            }
            if (approach(cat, cat->target_x, cat->target_y, RUN_SPEED * dt)) {
                const struct sh_cat_ledge *ledge = ledge_at(world->tops, world->top_count,
                                                            cat->target_window, cat->x, cat->y);
                if (ledge) {
                    cat->grounded = true;
                    cat->on = ledge->window;
                    enter(cat, SH_CAT_KNOCK, now, KNOCK_MS);
                } else {
                    fall(cat, 0, now);
                }
            }
            return STEP_MS;
        }
        // Beside the pointer, on the side it comes from, with its head by the pointer.
        int side = cat->x < cat->pointer_x ? -1 : 1;
        double to_x = inside_x(cat, area, cat->pointer_x + side * (cat->width / 2 + 4));
        double to_y = cat->pointer_y + cat->height / 2;
        if (area)
            to_y = clamp(to_y, area->top + cat->height, area->bottom);
        bool there = approach(cat, to_x, to_y, RUN_SPEED * dt);
        if (there && now - cat->pointer_at >= REST_MS) {
            cat->facing = cat->pointer_x >= cat->x ? 1 : -1;
            // Sits by it where there is something to sit on, else paws at it and drops.
            const struct sh_cat_ledge *ledge = NULL;
            for (int i = 0; i < world->ledge_count && !ledge; ++i) {
                const struct sh_cat_ledge *candidate = &world->ledges[i];
                if (candidate->y >= cat->y - 2 && candidate->y <= cat->y + 8 &&
                    cat->x >= candidate->left && cat->x <= candidate->right)
                    ledge = candidate;
            }
            if (ledge) {
                cat->y = ledge->y;
                cat->grounded = true;
                cat->on = ledge->window;
                enter(cat, SH_CAT_LOOK, now, LOOK_MS);
            } else {
                enter(cat, SH_CAT_POUNCE, now, POUNCE_MS);
            }
        } else if (t >= CHASE_MS) {
            fall(cat, 0, now);
        }
        return STEP_MS;
    }
    case SH_CAT_POUNCE:
        if (now >= cat->until) {
            fall(cat, 0, now);
            return STEP_MS;
        }
        return next_beat(now, cat->since, 140);
    case SH_CAT_KNOCK:
        if (now >= cat->until || world->urgent != cat->target_window) {
            if (world->urgent == cat->target_window)
                cat->knocked = cat->target_window; // it gave up on that one
            sit(cat, now);
            return 1;
        }
        return next_beat(now, cat->since, 260);
    case SH_CAT_STATES:
        break;
    }
    return STEP_MS;
}

void sh_cat_init(struct sh_cat *cat, double width, double height, const struct sh_cat_area *area,
                 double x, uint32_t seed, int64_t now) {
    *cat = (struct sh_cat){.facing = 1, .width = width, .height = height, .time = now,
                           .random = seed ? seed : 1};
    cat->x = x;
    cat->y = area ? area->top + height : height;
    cat->blink_at = now + between(cat, 2500, 5500);
    fall(cat, 0, now);
}

int sh_cat_step(struct sh_cat *cat, const struct sh_cat_world *world, int64_t now) {
    if (now < cat->time)
        cat->time = now;
    if (now - cat->time > GAP_MS)
        cat->time = now - STEP_MS;
    int next = 1;
    enum sh_cat_state before;
    do {
        int64_t to = now - cat->time > STEP_MS ? cat->time + STEP_MS : now;
        double dt = (double)(to - cat->time) / 1000;
        cat->time = to;
        before = cat->state;
        next = advance(cat, world, to, dt);
    } while (cat->time < now);
    // What it changed to may already have something to say about the world.
    for (int i = 0; i < 4 && cat->state != before; ++i) {
        before = cat->state;
        next = advance(cat, world, now, 0);
    }
    return next < 1 ? 1 : next;
}

bool sh_cat_pointer(struct sh_cat *cat, double x, double y, int64_t now) {
    if (cat->pointer_known) {
        double dx = x - cat->pointer_x, dy = y - cat->pointer_y;
        double elapsed = now > cat->pointer_at ? (double)(now - cat->pointer_at) : 0;
        cat->travel = cat->travel * exp(-elapsed / TRAVEL_MS) + sqrt(dx * dx + dy * dy);
        cat->motions = elapsed <= MOTION_MS ? cat->motions + 1 : 1;
    }
    cat->pointer_known = true;
    cat->pointer_x = x;
    cat->pointer_y = y;
    cat->pointer_at = now;
    if (cat->travel < FLICK || cat->motions < FLICK_MOTIONS ||
        !(calm(cat->state) || cat->state == SH_CAT_POUNCE) ||
        (cat->state == SH_CAT_YAWN && cat->drowsy))
        return false;
    double dx = x - cat->x, dy = y - (cat->y - cat->height / 2);
    if (dx * dx + dy * dy > NEAR * NEAR)
        return false;
    cat->travel = 0;
    enter(cat, SH_CAT_CHASE, now, CHASE_MS);
    return true;
}

bool sh_cat_wake(struct sh_cat *cat, int64_t now) {
    if (cat->state != SH_CAT_SLEEP && !(cat->state == SH_CAT_YAWN && cat->drowsy))
        return false;
    cat->drowsy = false;
    enter(cat, SH_CAT_WAKE, now, WAKE_MS);
    return true;
}

void sh_cat_pose(const struct sh_cat *cat, int64_t now, struct sh_cat_pose *pose) {
    *pose = (struct sh_cat_pose){.frame = SH_CAT_FRAME_SIT, .left = cat->facing < 0};
    int64_t t = now - cat->since;
    if (t < 0)
        t = 0;
    switch (cat->state) {
    case SH_CAT_SIT:
        pose->frame = now >= cat->blink_at && now < cat->blink_at + BLINK_MS ? SH_CAT_FRAME_BLINK
                                                                             : SH_CAT_FRAME_SIT;
        break;
    case SH_CAT_LOOK:
        pose->frame = SH_CAT_FRAME_LOOK;
        break;
    case SH_CAT_WALK:
        pose->frame = fmod(cat->walked, 12) >= 6 ? SH_CAT_FRAME_WALK_2 : SH_CAT_FRAME_WALK_1;
        break;
    case SH_CAT_GROOM:
        pose->frame = (t / 300) % 2 ? SH_CAT_FRAME_GROOM_2 : SH_CAT_FRAME_GROOM_1;
        break;
    case SH_CAT_SCRATCH:
        pose->frame = (t / 110) % 2 ? SH_CAT_FRAME_SCRATCH_2 : SH_CAT_FRAME_SCRATCH_1;
        break;
    case SH_CAT_YAWN:
        pose->frame = t < 250 || t >= YAWN_MS - 250 ? SH_CAT_FRAME_BLINK : SH_CAT_FRAME_YAWN;
        break;
    case SH_CAT_SLEEP:
        pose->frame = (t / BREATH_MS) % 2 ? SH_CAT_FRAME_SLEEP_2 : SH_CAT_FRAME_SLEEP_1;
        pose->z = true;
        pose->z_step = (int)((t / BREATH_MS) % 3);
        break;
    case SH_CAT_WAKE:
        pose->frame = t < WAKE_MS * 6 / 10 ? SH_CAT_FRAME_YAWN : SH_CAT_FRAME_BLINK;
        break;
    case SH_CAT_JUMP:
        if (t < CROUCH_MS) {
            pose->frame = SH_CAT_FRAME_LOOK;
            pose->squash = 0.18;
        } else {
            double p = (double)(t - CROUCH_MS) / cat->jump_ms;
            pose->frame = p < 0.5 ? SH_CAT_FRAME_RUN_2 : SH_CAT_FRAME_RUN_1;
        }
        break;
    case SH_CAT_FALL:
        pose->frame = SH_CAT_FRAME_FALL;
        break;
    case SH_CAT_LAND: {
        double p = 1 - (double)t / LAND_MS;
        pose->squash = p > 0 ? 0.3 * p * p : 0;
        break;
    }
    case SH_CAT_CHASE:
    case SH_CAT_RUSH:
        pose->frame = t < NOTICE_MS ? SH_CAT_FRAME_ALERT
                      : (t / 90) % 2 ? SH_CAT_FRAME_RUN_2
                                     : SH_CAT_FRAME_RUN_1;
        break;
    case SH_CAT_POUNCE:
        pose->frame = (t / 140) % 2 ? SH_CAT_FRAME_PAW_2 : SH_CAT_FRAME_PAW_1;
        break;
    case SH_CAT_KNOCK:
        pose->frame = (t / 260) % 2 ? SH_CAT_FRAME_PAW_2 : SH_CAT_FRAME_PAW_1;
        break;
    case SH_CAT_STATES:
        break;
    }
}

/* Takes [left, right] out of the `count` disjoint spans in `spans`, which hold up to `max`. */
static int subtract(double spans[][2], int count, int max, double left, double right) {
    double kept[64][2];
    int kept_count = 0;
    for (int i = 0; i < count && kept_count < 64; ++i) {
        if (right <= spans[i][0] || left >= spans[i][1]) {
            kept[kept_count][0] = spans[i][0];
            kept[kept_count++][1] = spans[i][1];
            continue;
        }
        if (left > spans[i][0] && kept_count < 64) {
            kept[kept_count][0] = spans[i][0];
            kept[kept_count++][1] = left;
        }
        if (right < spans[i][1] && kept_count < 64) {
            kept[kept_count][0] = right;
            kept[kept_count++][1] = spans[i][1];
        }
    }
    if (kept_count > max)
        kept_count = max;
    for (int i = 0; i < kept_count; ++i) {
        spans[i][0] = kept[i][0];
        spans[i][1] = kept[i][1];
    }
    return kept_count;
}

void sh_cat_ledges(const struct sh_cat_window *windows, int window_count,
                   const struct sh_cat_area *areas, int area_count, double height,
                   struct sh_cat_ledge *tops, int *top_count, struct sh_cat_ledge *ledges,
                   int *ledge_count, int max) {
    struct sh_cat_world world = {.areas = areas, .area_count = area_count};
    *top_count = *ledge_count = 0;
    for (int i = 0; i < window_count && *top_count < max; ++i) {
        const struct sh_cat_window *window = &windows[i];
        if (window->width <= 0 || window->height <= 0)
            continue;
        int index = sh_cat_area_at(&world, window->x + window->width / 2, window->y + 1);
        const struct sh_cat_area *area = index < 0 ? NULL : &areas[index];
        double y = window->y;
        if (area && y < area->top + height)
            y = area->top + height;
        if (area && y > area->bottom)
            continue; // its top is past the bottom of the free area
        if (y > window->y + window->height)
            continue; // too near the top of the free area to stand on
        tops[(*top_count)++] = (struct sh_cat_ledge){window->window, window->x,
                                                     window->x + window->width, y};
        // What shows of it: nothing in front reaches into the band the cat's lower half covers.
        double spans[64][2] = {{window->x, window->x + window->width}};
        int count = 1;
        for (int j = 0; j < i && count > 0; ++j) {
            const struct sh_cat_window *front = &windows[j];
            if (front->y < y && front->y + front->height > y - height / 2)
                count = subtract(spans, count, 64, front->x, front->x + front->width);
        }
        for (int k = 0; k < count && *ledge_count < max; ++k)
            ledges[(*ledge_count)++] =
                (struct sh_cat_ledge){window->window, spans[k][0], spans[k][1], y};
    }
    for (int i = 0; i < area_count; ++i) {
        const struct sh_cat_area *area = &areas[i];
        struct sh_cat_ledge floor = {0, area->x, area->x + area->width, area->bottom};
        if (*top_count < max)
            tops[(*top_count)++] = floor;
        if (*ledge_count < max)
            ledges[(*ledge_count)++] = floor;
    }
}
