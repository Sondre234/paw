// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* The desktop cat's behaviour, without wlroots: where it is, what it does and what it does next,
 * given the world src/compositor/cat.c shows it. It sits on the top edge of a window or on the
 * bottom of a monitor's free area (above a taskbar), wanders along it, grooms, yawns and
 * scratches, falls with gravity when what it sits on goes, jumps up to the focused window, chases
 * a pointer flicked past it, runs over to paw at an urgent window, and sleeps while nobody uses
 * the computer. Positions are layout coordinates in logical pixels, (x, y) being the middle of
 * the cat's feet; times are milliseconds on the monotonic clock. */
#include <stdbool.h>
#include <stdint.h>

#include "paw/cat_sprite.h"

#ifdef __cplusplus
extern "C" {
#endif

enum sh_cat_state {
    SH_CAT_SIT,     /* sitting, blinking now and then; every few seconds it picks something */
    SH_CAT_LOOK,    /* sitting sideways, looking one way */
    SH_CAT_WALK,    /* walking along what it sits on, to `target_x` */
    SH_CAT_GROOM,   /* licking a paw */
    SH_CAT_SCRATCH, /* scratching behind an ear */
    SH_CAT_YAWN,    /* yawning, on its way to sleep or just because */
    SH_CAT_SLEEP,   /* curled up, breathing out a "z" now and then */
    SH_CAT_WAKE,    /* stretching awake */
    SH_CAT_JUMP,    /* leaping in an arc to `target_x`, `target_y` on `target_window` */
    SH_CAT_FALL,    /* falling, after a startled hop when what it sat on went */
    SH_CAT_LAND,    /* squashed for a moment as it lands */
    SH_CAT_CHASE,   /* running through the air after the pointer, as oneko does */
    SH_CAT_POUNCE,  /* pawing at the pointer it caught up with, before it drops */
    SH_CAT_RUSH,    /* running to an urgent window's top */
    SH_CAT_KNOCK,   /* patting the urgent window's top until it has attention */
    SH_CAT_STATES,
};

/* A line the cat can stand on, from `left` to `right` at `y`: the top of window number `window`
 * (where its feet go, so the cat shows above it), or the floor of a monitor's free area for
 * window 0. */
struct sh_cat_ledge {
    uint32_t window;
    double left, right, y;
};

/* A monitor: its box, and the top and bottom of its free area (what panels leave). */
struct sh_cat_area {
    double x, y, width, height;
    double top, bottom;
};

/* A window as sh_cat_ledges takes it: its number and box, frame included. */
struct sh_cat_window {
    uint32_t window;
    double x, y, width, height;
};

/* What the cat sees. `tops` holds every window's whole top edge and each monitor's floor, which
 * hold the cat up; `ledges` the parts of them nothing is drawn over, where it lands and goes to.
 * `focused_area` is the monitor with the focus (an index into `areas`, -1 for none); `focused` and
 * `urgent` are window numbers, 0 for none; `idle` says nobody has used the computer for a while. */
struct sh_cat_world {
    const struct sh_cat_area *areas;
    int area_count;
    int focused_area;
    const struct sh_cat_ledge *tops, *ledges;
    int top_count, ledge_count;
    uint32_t focused, urgent;
    bool idle;
};

struct sh_cat {
    enum sh_cat_state state;
    double x, y;
    double vx, vy;        /* pixels per second, while it falls */
    int facing;           /* 1 right, -1 left */
    bool grounded;        /* it stands on `on` */
    uint32_t on;          /* the window it stands on, 0 for a floor */
    double width, height; /* its size, logical pixels */
    int64_t time;         /* the last step */
    int64_t since, until; /* when the state began, and when it ends or picks again */
    /* Where it heads: a walk's, a jump's, a run's end, and the window it lands on there. */
    double target_x, target_y;
    uint32_t target_window;
    double from_x, from_y; /* where a jump began */
    uint32_t aim;          /* the window a fall that drops it in lands on, passing the others */
    int jump_ms;
    double jump_arc;
    double walked;      /* pixels walked, for the steps' frames */
    bool drowsy;        /* its yawn leads to sleep */
    int64_t blink_at;   /* when it next blinks */
    int64_t away_since; /* since when it has been off the focused monitor, 0 while on it */
    uint32_t knocked;   /* the urgent window it last pawed at, which it leaves be after */
    /* The pointer as last seen: where, when it last moved, how far it went lately (decaying) and
     * in how many motions one soon after another, which tell a flick past the cat from an
     * ordinary move and from the pointer put somewhere at once. */
    double pointer_x, pointer_y, travel;
    int64_t pointer_at;
    int motions;
    bool pointer_known;
    uint32_t random;
};

/* What to draw now: the frame, mirrored to face left, how squashed (0 for not at all, positive
 * wider and lower, as on landing), and whether a "z" floats over it, at which of its three
 * heights. */
struct sh_cat_pose {
    enum sh_cat_frame frame;
    bool left;
    double squash;
    bool z;
    int z_step;
};

/* A cat of `width` by `height` that appears at the top of `area`'s free area at `x` and falls
 * from there, its idle choices following `seed`. */
void sh_cat_init(struct sh_cat *cat, double width, double height, const struct sh_cat_area *area,
                 double x, uint32_t seed, int64_t now);

/* Has the cat turn up on the focused monitor (else the first), falling from the top of its free
 * area over the focused window where that shows there. */
void sh_cat_arrive(struct sh_cat *cat, const struct sh_cat_world *world, int64_t now);

/* Steps the cat to `now` in `world`. Returns the milliseconds after which it next changes or
 * decides something (at least 1). */
int sh_cat_step(struct sh_cat *cat, const struct sh_cat_world *world, int64_t now);

/* Notes the pointer at (x, y). True when that makes the cat chase it, or matters to it otherwise,
 * so that it should be stepped at once. */
bool sh_cat_pointer(struct sh_cat *cat, double x, double y, int64_t now);

/* Wakes a sleeping cat; true when it was asleep or falling asleep. */
bool sh_cat_wake(struct sh_cat *cat, int64_t now);

void sh_cat_pose(const struct sh_cat *cat, int64_t now, struct sh_cat_pose *pose);

const char *sh_cat_state_name(enum sh_cat_state state);

/* The index of the area the cat's feet are in, else the nearest one; -1 without areas. */
int sh_cat_area_at(const struct sh_cat_world *world, double x, double y);

/* What a cat of `height` stands on among `windows` (front to back) on `areas`: into `tops`, each
 * window's top edge, where its feet go, held down within each monitor's free area so the cat
 * shows whole, then each area's floor; into `ledges`, the parts of the window tops that no window
 * in front of them covers (where the cat would show), then the floors. Writes at most `max` of
 * each; returns the counts through `top_count` and `ledge_count`. */
void sh_cat_ledges(const struct sh_cat_window *windows, int window_count,
                   const struct sh_cat_area *areas, int area_count, double height,
                   struct sh_cat_ledge *tops, int *top_count, struct sh_cat_ledge *ledges,
                   int *ledge_count, int max);

#ifdef __cplusplus
}
#endif
