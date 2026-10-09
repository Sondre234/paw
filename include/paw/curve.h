// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* Easing curves and the per-animation settings built on them. Plain C so both the compositor
 * and the configuration parser use it. */
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum sh_curve_kind {
    SH_CURVE_EASE_OUT, /* cubic: fast at first, settling gently (the default) */
    SH_CURVE_LINEAR,
    SH_CURVE_EASE_IN,
    SH_CURVE_EASE_IN_OUT,
    SH_CURVE_EASE_OUT_QUINT, /* a harder ease-out: nearly there almost at once */
    SH_CURVE_OVERSHOOT,      /* passes 1 slightly, then settles */
    SH_CURVE_SPRING,         /* critically damped spring; moves keep their velocity when retargeted */
    SH_CURVE_BEZIER          /* CSS-style cubic-bezier(x1, y1, x2, y2) */
};

struct sh_curve {
    int kind; /* enum sh_curve_kind */
    float p[4];
};

/* Parses a curve name ("linear", "ease-in", "ease-out", "ease-in-out", "ease-out-quint",
 * "overshoot", "spring") or "bezier(x1, y1, x2, y2)" with x1 and x2 in [0, 1] and y1 and y2 in
 * [-2, 3]. Case and surrounding spaces do not matter. Returns false and leaves `curve`
 * untouched when the text is not one of these. */
bool sh_curve_parse(const char *text, struct sh_curve *curve);
/* Progress in 0..1 to eased progress; 0 gives 0 and 1 gives 1 exactly, and input outside 0..1
 * is clamped. */
double sh_curve_eval(const struct sh_curve *curve, double t);

/* The kinds of animation that have their own duration and curve. */
enum sh_anim_kind {
    SH_ANIM_OPEN,       /* a window appears */
    SH_ANIM_CLOSE,      /* a window goes */
    SH_ANIM_MOVE,       /* a tile or window moves to a new place */
    SH_ANIM_WORKSPACE,  /* the view slides to another workspace */
    SH_ANIM_FULLSCREEN, /* a window enters or leaves fullscreen */
    SH_ANIM_FOCUS,      /* border and opacity change with focus */
    SH_ANIM_KINDS
};

struct sh_anim_style {
    int duration; /* milliseconds before the speed multiplier; 0 turns this kind off */
    struct sh_curve curve;
};

#ifdef __cplusplus
}
#endif
