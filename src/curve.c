// SPDX-License-Identifier: GPL-3.0-or-later
#include "paw/curve.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* A critically damped spring is 1 - (1 + wt) e^-wt; this w leaves 1% at t = 1, and the curve is
 * scaled so it ends exactly at 1. */
static const double SPRING_W = 6.64;

static double spring_raw(double t) {
    return 1 - (1 + SPRING_W * t) * exp(-SPRING_W * t);
}

/* CSS cubic-bezier: x(s) = progress, solved for s by Newton's method with bisection as the
 * fallback, then y(s). */
static double bezier(const float p[4], double x) {
    double cx = 3 * p[0], bx = 3 * (p[2] - p[0]) - cx, ax = 1 - cx - bx;
    double cy = 3 * p[1], by = 3 * (p[3] - p[1]) - cy, ay = 1 - cy - by;
    double s = x;
    for (int i = 0; i < 8; ++i) {
        double error = ((ax * s + bx) * s + cx) * s - x;
        if (fabs(error) < 1e-7)
            return ((ay * s + by) * s + cy) * s;
        double slope = (3 * ax * s + 2 * bx) * s + cx;
        if (fabs(slope) < 1e-6)
            break;
        s -= error / slope;
    }
    double low = 0, high = 1;
    s = x;
    for (int i = 0; i < 40; ++i) {
        double value = ((ax * s + bx) * s + cx) * s;
        if (fabs(value - x) < 1e-7)
            break;
        if (value < x)
            low = s;
        else
            high = s;
        s = (low + high) / 2;
    }
    return ((ay * s + by) * s + cy) * s;
}

double sh_curve_eval(const struct sh_curve *curve, double t) {
    if (!(t > 0))
        return 0; // also NaN
    if (t >= 1)
        return 1;
    double u = 1 - t;
    switch (curve->kind) {
    case SH_CURVE_LINEAR:
        return t;
    case SH_CURVE_EASE_IN:
        return t * t * t;
    case SH_CURVE_EASE_IN_OUT:
        return t < 0.5 ? 4 * t * t * t : 1 - 4 * u * u * u;
    case SH_CURVE_EASE_OUT_QUINT:
        return 1 - u * u * u * u * u;
    case SH_CURVE_OVERSHOOT: {
        const double c1 = 1.70158, c3 = c1 + 1;
        return 1 - c3 * u * u * u + c1 * u * u; // 1 + c3 (t-1)^3 + c1 (t-1)^2
    }
    case SH_CURVE_SPRING:
        return spring_raw(t) / spring_raw(1);
    case SH_CURVE_BEZIER:
        return bezier(curve->p, t);
    case SH_CURVE_EASE_OUT:
    default:
        return 1 - u * u * u;
    }
}

static const struct {
    const char *name;
    int kind;
} NAMES[] = {{"ease-out", SH_CURVE_EASE_OUT},
             {"linear", SH_CURVE_LINEAR},
             {"ease-in", SH_CURVE_EASE_IN},
             {"ease-in-out", SH_CURVE_EASE_IN_OUT},
             {"ease-out-quint", SH_CURVE_EASE_OUT_QUINT},
             {"overshoot", SH_CURVE_OVERSHOOT},
             {"spring", SH_CURVE_SPRING}};

bool sh_curve_parse(const char *text, struct sh_curve *curve) {
    char word[96];
    size_t length = 0;
    while (*text && isspace((unsigned char)*text))
        ++text;
    for (; *text && length + 1 < sizeof(word); ++text)
        word[length++] = (char)tolower((unsigned char)*text);
    if (*text)
        return false; // too long to be a curve
    while (length && isspace((unsigned char)word[length - 1]))
        --length;
    word[length] = '\0';
    for (size_t i = 0; i < sizeof(NAMES) / sizeof(*NAMES); ++i) {
        if (!strcmp(word, NAMES[i].name)) {
            *curve = (struct sh_curve){.kind = NAMES[i].kind};
            return true;
        }
    }
    double p[4];
    int used = 0;
    if (sscanf(word, "bezier ( %lf , %lf , %lf , %lf ) %n", &p[0], &p[1], &p[2], &p[3], &used) !=
            4 ||
        word[used] != '\0')
        return false;
    for (int i = 0; i < 4; ++i) {
        double low = i % 2 ? -2 : 0, high = i % 2 ? 3 : 1;
        if (!(p[i] >= low && p[i] <= high))
            return false; // also NaN
    }
    *curve = (struct sh_curve){.kind = SH_CURVE_BEZIER,
                               .p = {(float)p[0], (float)p[1], (float)p[2], (float)p[3]}};
    return true;
}
