// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* The exploded view (paw xray) as arithmetic: where each sheet of an output's stack of layers is
 * drawn as the stack fans out like a deck of cards seen from the side, how far the pointer turns
 * it and the wheel spreads it, where the room for the shell's text leaves it, and which sheet is
 * under a point. Everything is in logical pixels from the output's top-left corner;
 * src/compositor/xray.c draws the sheets. */
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct sh_xray_box {
    double x, y, width, height;
};

struct sh_xray_view {
    double width, height;    /* the output */
    struct sh_xray_box area; /* what the fanned stack keeps to (sh_xray_area) */
    int count;               /* sheets, the bottom one first */
    /* How the stack is turned, each from -1 to 1 (sh_xray_tilt): 0, 0 rises up and to the right,
     * as it opens. */
    double tilt_x, tilt_y;
    double spread;   /* the gap between sheets, 1 as it opens (sh_xray_spread) */
    double progress; /* 0: every sheet lies flat on the output as it shows it, 1: fanned out */
};

/* The share of an output's height the gap between two sheets fanned out takes at a spread of 1:
 * shared out over the stack, but within a minimum and maximum for a sheet or two and for many. */
double sh_xray_gap(int count);

/* Where each of the view's sheets is drawn, into `sheets` (`count` of them). Fanned out, every
 * sheet is the output scaled down to fit the area with the rest of the stack, foreshortened
 * along the way it is turned and each one up a little larger, nearer the eye; each lies a gap
 * further along the way the stack rises, and is lifted out of it `lift[i]` of the way (0 to 1;
 * NULL for none), half a gap further at least, without moving the others. Part of the way, each
 * is that far from the whole output to its place. */
void sh_xray_layout(const struct sh_xray_view *view, const double *lift,
                    struct sh_xray_box *sheets);

/* The topmost of `count` sheets containing the point, else -1. */
int sh_xray_sheet_at(const struct sh_xray_box *sheets, int count, double x, double y);

/* The tilt for the pointer moved `dx`, `dy` since the view opened on an output of `width` by
 * `height`: half its size either way turns the stack as far as it goes, and the stack's top
 * follows the pointer. */
void sh_xray_tilt(double dx, double dy, double width, double height, double *tilt_x,
                  double *tilt_y);

/* The spread after `notches` of the wheel from `spread` (up, a negative scroll, spreads the
 * stack further apart), kept between SH_XRAY_SPREAD_MIN and SH_XRAY_SPREAD_MAX. */
#define SH_XRAY_SPREAD_MIN 0.25
#define SH_XRAY_SPREAD_MAX 4.0
double sh_xray_spread(double spread, double notches);

/* What the fanned stack keeps to on an output of `width` by `height`: the output less a margin,
 * a band along the bottom for the caption, and a column along the left for the legend of the
 * layers, `legend` wide (0 on an output too narrow for one). */
struct sh_xray_box sh_xray_area(double width, double height, double *legend);

/* `value` eased toward `target` over `elapsed` milliseconds, as a spring settling in about
 * `settle` of them would: exponentially, reaching it once within `epsilon`. */
double sh_xray_ease(double value, double target, double elapsed, double settle, double epsilon);

#ifdef __cplusplus
}
#endif
