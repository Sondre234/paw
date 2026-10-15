// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* The scrolling layout's view turning like a cube (`layout.scroll.cube`) as arithmetic: the strip
 * of columns wrapped round a square, seen from above, whose sides are each as wide as the view,
 * and which turns a quarter as the view moves along the strip, the side that showed the view as
 * it was turning away and the one with the view as it will be turning to the eye, while the strip
 * slides along the sides by what the move and the quarter turn differ. Everything is in logical
 * pixels along the strip from the left edge of the view at rest, where the turn ends;
 * src/compositor/cube.c draws the tiles where this says. */
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* How much fainter a span is drawn turned edge on to the eye (sh_cube_span's fade). */
#define SH_CUBE_FADE 0.6
/* How far the eye is from the side of the square nearest it, in sides' widths: the nearer, the
 * smaller what is further off is drawn. */
#define SH_CUBE_EYE 2.0

/* The square as it is turned: side k lies along the strip from start + k × width to
 * start + (k + 1) × width, and faces `k × π/2 - angle` radians away from the eye, so at rest
 * (angle and start 0) side 0 is the view, facing the eye, and a positive angle brings side 1, on
 * the right, round toward it. Turned by a quarter more, with every side one further along, it is
 * the same square drawn the same way. */
struct sh_cube {
    double width; /* a side's: the view's */
    double angle; /* radians */
    double start;
};

/* The square a turn starts from as the view moves `distance` along the strip (positive: right,
 * the columns moving left), from the square as it is drawn now, in the view it turns to (at rest:
 * width, 0, 0): taken along into the view it will turn to, and turned a quarter more, which draws
 * the same, as long as less than an eighth of a turn is left the way the view moves, so that it
 * turns that way. Nothing moves where it is drawn: from rest, the side facing the eye shows the
 * view as it was, a quarter turn from rest. */
struct sh_cube sh_cube_move(struct sh_cube now, double distance);

/* `progress` of the way (0 to 1, eased) from `from` to rest. */
struct sh_cube sh_cube_at(const struct sh_cube *from, double progress);

/* Where the strip from `left` to `right` is drawn: from `left` to `right` along the view from its
 * left edge, straight across the sides it lies on and on past the sides that face the eye along
 * the last one, its height `scale` times its own about the middle of the view, further off the
 * smaller, `fade` fainter as it turns edge on (0 facing the eye). `shown` is false when all of it
 * lies on sides turned away, where it is drawn nowhere; past the sides facing the eye, it is
 * drawn past the view's edges. */
struct sh_cube_span {
    double left, right, scale, fade;
    bool shown;
};
void sh_cube_span(const struct sh_cube *cube, double left, double right,
                  struct sh_cube_span *out);

#ifdef __cplusplus
}
#endif
