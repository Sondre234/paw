// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* Touchpad swipes the compositor takes for itself (`gestures`): which way the fingers go, how
 * far along a step (a workspace, the overview) they have come, how fast they move, and whether
 * letting go finishes the step or goes back. Nothing here touches wlroots or the clock: the
 * compositor hands over libinput's swipe events with their times, so it is tested on its own. */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The way a swipe goes, as the fingers move (or the other way, with `invert`). */
enum sh_swipe_direction {
    SH_SWIPE_NONE,
    SH_SWIPE_LEFT,
    SH_SWIPE_RIGHT,
    SH_SWIPE_UP,
    SH_SWIPE_DOWN,
};

/* How far the fingers move, in libinput's units (about a pointer's pixels), before a swipe
 * takes the direction they have gone most; until then it has none. */
#define SH_SWIPE_THRESHOLD 16.0
/* A swipe moving at least this fast as the fingers lift, in units per millisecond, is a flick:
 * it finishes the step it heads for, or goes back from one it heads away from, however far it
 * came. */
#define SH_SWIPE_FLICK 0.5
/* The speed at the end is how far the fingers went over this many milliseconds before they
 * lifted; fingers that stopped for that long before lifting have no speed. */
#define SH_SWIPE_SPEED_SPAN 80

enum { SH_SWIPE_SAMPLES = 16 };

struct sh_swipe {
    int fingers;
    bool invert;
    double distance; /* the fingers' travel that makes a whole step */
    double x, y;     /* where the fingers' centre is from where it began (turned with invert) */
    enum sh_swipe_direction direction;
    /* The last positions and their times, oldest first, for the speed. */
    struct {
        uint32_t time;
        double x, y;
    } samples[SH_SWIPE_SAMPLES];
    int sample_count;
};

/* Starts a swipe of `fingers` at `time`, a whole step being `distance` units of travel. */
void sh_swipe_begin(struct sh_swipe *swipe, int fingers, double distance, bool invert,
                    uint32_t time);
/* Moves the fingers by (dx, dy) at `time`. Returns true once: when this movement gives the swipe
 * its direction. */
bool sh_swipe_update(struct sh_swipe *swipe, double dx, double dy, uint32_t time);
/* How far the fingers have gone along the swipe's direction, in steps: 1 is a whole step, and
 * below 0 they have gone back past where they began, the other way along the same axis. 0
 * while the swipe has no direction. */
double sh_swipe_progress(const struct sh_swipe *swipe);
/* How fast the fingers move along the swipe's direction at `time` (the end), in units per
 * millisecond, negative going back. */
double sh_swipe_speed(const struct sh_swipe *swipe, uint32_t time);
/* Whether letting go `progress` steps toward a step (as sh_swipe_progress, but positive toward
 * the step in question), moving `speed` units per millisecond toward it, finishes that step:
 * a flick toward it does from anywhere on its side, a flick away never does, and otherwise it
 * finishes from half way. */
bool sh_swipe_finishes(double progress, double speed);
/* The direction opposite `direction`, along the same axis. */
enum sh_swipe_direction sh_swipe_opposite(enum sh_swipe_direction direction);
/* "left", "right", "up", "down", or "none". */
const char *sh_swipe_direction_name(enum sh_swipe_direction direction);

#ifdef __cplusplus
}
#endif
