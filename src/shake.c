/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Shaking the pointer to find it: the strokes that make a shake, and the cursor's size as it
 * grows and shrinks back (include/paw/shake.h). */
#include "paw/shake.h"
#include <math.h>

static double clamp01(double value) { return value < 0 ? 0 : value > 1 ? 1 : value; }

static double ease_out(double t) {
    double u = 1 - t;
    return 1 - u * u * u;
}

static double ease_in_out(double t) {
    return t < 0.5 ? 4 * t * t * t : 1 - pow(-2 * t + 2, 3) / 2;
}

void sh_shake_reset(struct sh_shake *shake) {
    shake->known = false;
    shake->way = 0;
    shake->count = 0;
}

/* The stroke under way ends at its furthest point, a turn: true when it is one of a shake's. */
static bool good_stroke(const struct sh_shake *shake) {
    double width = fabs(shake->far_x - shake->start_x);
    double height = fabs(shake->far_y - shake->start_y);
    int64_t took = shake->far_at - shake->start_at;
    return width >= SH_SHAKE_STROKE && height < width &&
           width * 1000 >= SH_SHAKE_SPEED * (double)(took > 1 ? took : 1);
}

/* A shake seen at `now`: the cursor grows from the size it is drawn at, or growing or grown
 * already holds longer. */
static void shaken(struct sh_shake *shake, int64_t now) {
    if (!sh_shake_grown(shake, now) || now > shake->shaken_at + SH_SHAKE_HOLD_MS) {
        shake->from = sh_shake_scale(shake, now);
        shake->grown_at = now;
    }
    shake->shaken_at = now;
}

bool sh_shake_motion(struct sh_shake *shake, double x, double y, int64_t now) {
    bool rested = now - shake->seen_at > SH_SHAKE_REST_MS;
    shake->seen_at = now;
    if (!shake->known || rested) {
        shake->known = true;
        shake->way = 0;
        shake->count = 0;
        shake->start_x = shake->far_x = x;
        shake->start_y = shake->far_y = y;
        shake->start_at = shake->far_at = now;
        return false;
    }
    double ahead = (x - shake->far_x) * shake->way;
    if (shake->way == 0) {
        // The first stroke's way is the one the pointer goes past SH_SHAKE_BACK.
        double moved = x - shake->start_x;
        if (fabs(moved) < SH_SHAKE_BACK)
            return false;
        shake->way = moved > 0 ? 1 : -1;
        shake->far_x = x, shake->far_y = y, shake->far_at = now;
        return false;
    }
    if (ahead >= 0) { // on the same way: the furthest point yet
        shake->far_x = x, shake->far_y = y, shake->far_at = now;
        return false;
    }
    if (-ahead < SH_SHAKE_BACK)
        return false;
    // Turned back at the furthest point: the stroke ends there and the next begins.
    bool good = good_stroke(shake);
    if (!good) {
        shake->count = 0;
    } else {
        if (shake->count == SH_SHAKE_TURNS) {
            for (int i = 1; i < SH_SHAKE_TURNS; ++i)
                shake->turns[i - 1] = shake->turns[i];
            --shake->count;
        }
        shake->turns[shake->count++] = shake->far_at;
    }
    shake->start_x = shake->far_x, shake->start_y = shake->far_y;
    shake->start_at = shake->far_at;
    shake->far_x = x, shake->far_y = y, shake->far_at = now;
    shake->way = -shake->way;
    if (!good || shake->count < SH_SHAKE_TURNS ||
        shake->turns[SH_SHAKE_TURNS - 1] - shake->turns[0] > SH_SHAKE_MS)
        return false;
    shaken(shake, now);
    return true;
}

double sh_shake_scale(const struct sh_shake *shake, int64_t now) {
    if (!shake->shaken_at)
        return 1;
    double grow = SH_SHAKE_GROW;
    double growing = clamp01((double)(now - shake->grown_at) / SH_SHAKE_GROW_MS);
    double grown = shake->from + (grow - shake->from) * ease_out(growing);
    int64_t shrink_at = shake->shaken_at + SH_SHAKE_HOLD_MS;
    if (now <= shrink_at)
        return grown;
    double shrinking = clamp01((double)(now - shrink_at) / SH_SHAKE_SHRINK_MS);
    return 1 + (grown - 1) * (1 - ease_in_out(shrinking));
}

bool sh_shake_grown(const struct sh_shake *shake, int64_t now) {
    return shake->shaken_at && now < shake->shaken_at + SH_SHAKE_HOLD_MS + SH_SHAKE_SHRINK_MS;
}
