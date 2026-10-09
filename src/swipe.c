/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "paw/swipe.h"
#include <math.h>
#include <string.h>

/* Milliseconds from `earlier` to `later`, across the wrap of libinput's 32-bit times. */
static double elapsed(uint32_t later, uint32_t earlier) {
    return (double)(int32_t)(later - earlier);
}

/* How far a position is along the direction (positive that way). */
static double along(enum sh_swipe_direction direction, double x, double y) {
    switch (direction) {
    case SH_SWIPE_LEFT:
        return -x;
    case SH_SWIPE_RIGHT:
        return x;
    case SH_SWIPE_UP:
        return -y;
    case SH_SWIPE_DOWN:
        return y;
    default:
        return 0;
    }
}

static void sample(struct sh_swipe *swipe, uint32_t time) {
    if (swipe->sample_count == SH_SWIPE_SAMPLES) {
        memmove(swipe->samples, swipe->samples + 1,
                (SH_SWIPE_SAMPLES - 1) * sizeof(*swipe->samples));
        --swipe->sample_count;
    }
    swipe->samples[swipe->sample_count].time = time;
    swipe->samples[swipe->sample_count].x = swipe->x;
    swipe->samples[swipe->sample_count].y = swipe->y;
    ++swipe->sample_count;
}

void sh_swipe_begin(struct sh_swipe *swipe, int fingers, double distance, bool invert,
                    uint32_t time) {
    memset(swipe, 0, sizeof(*swipe));
    swipe->fingers = fingers;
    swipe->distance = distance > 1 ? distance : 1;
    swipe->invert = invert;
    sample(swipe, time);
}

bool sh_swipe_update(struct sh_swipe *swipe, double dx, double dy, uint32_t time) {
    if (!isfinite(dx) || !isfinite(dy))
        return false;
    swipe->x += swipe->invert ? -dx : dx;
    swipe->y += swipe->invert ? -dy : dy;
    sample(swipe, time);
    if (swipe->direction != SH_SWIPE_NONE || hypot(swipe->x, swipe->y) < SH_SWIPE_THRESHOLD)
        return false;
    if (fabs(swipe->x) >= fabs(swipe->y))
        swipe->direction = swipe->x < 0 ? SH_SWIPE_LEFT : SH_SWIPE_RIGHT;
    else
        swipe->direction = swipe->y < 0 ? SH_SWIPE_UP : SH_SWIPE_DOWN;
    return true;
}

double sh_swipe_progress(const struct sh_swipe *swipe) {
    return along(swipe->direction, swipe->x, swipe->y) / swipe->distance;
}

double sh_swipe_speed(const struct sh_swipe *swipe, uint32_t time) {
    if (swipe->direction == SH_SWIPE_NONE || swipe->sample_count < 2)
        return 0;
    int newest = swipe->sample_count - 1;
    if (elapsed(time, swipe->samples[newest].time) > SH_SWIPE_SPEED_SPAN)
        return 0; // the fingers rested before they lifted
    int oldest = newest;
    while (oldest > 0 && elapsed(time, swipe->samples[oldest - 1].time) <= SH_SWIPE_SPEED_SPAN)
        --oldest;
    if (oldest == newest)
        --oldest; // one movement in the span: from where the fingers were before it
    double span = elapsed(time, swipe->samples[oldest].time);
    double moved = along(swipe->direction, swipe->samples[newest].x, swipe->samples[newest].y) -
                   along(swipe->direction, swipe->samples[oldest].x, swipe->samples[oldest].y);
    return moved / (span > 1 ? span : 1);
}

bool sh_swipe_finishes(double progress, double speed) {
    if (speed >= SH_SWIPE_FLICK)
        return progress > 0;
    if (speed <= -SH_SWIPE_FLICK)
        return false;
    return progress >= 0.5;
}

enum sh_swipe_direction sh_swipe_opposite(enum sh_swipe_direction direction) {
    switch (direction) {
    case SH_SWIPE_LEFT:
        return SH_SWIPE_RIGHT;
    case SH_SWIPE_RIGHT:
        return SH_SWIPE_LEFT;
    case SH_SWIPE_UP:
        return SH_SWIPE_DOWN;
    case SH_SWIPE_DOWN:
        return SH_SWIPE_UP;
    default:
        return SH_SWIPE_NONE;
    }
}

const char *sh_swipe_direction_name(enum sh_swipe_direction direction) {
    static const char *names[] = {"none", "left", "right", "up", "down"};
    return direction >= SH_SWIPE_NONE && direction <= SH_SWIPE_DOWN ? names[direction] : "none";
}
