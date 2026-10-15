/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "paw/cube.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const double QUARTER = M_PI / 2;

struct sh_cube sh_cube_move(struct sh_cube now, double distance) {
    now.start -= distance;
    // A negative angle turns to the right, as the view moving right does. Turning the other way,
    // it turns back from where it is; else on, a quarter further while little is left.
    if (distance > 0) {
        bool back = now.angle > 0;
        while (now.angle > 0 || (!back && now.angle > -QUARTER / 2))
            now.angle -= QUARTER, now.start += now.width;
    } else if (distance < 0) {
        bool back = now.angle < 0;
        while (now.angle < 0 || (!back && now.angle < QUARTER / 2))
            now.angle += QUARTER, now.start -= now.width;
    }
    return now;
}

struct sh_cube sh_cube_at(const struct sh_cube *from, double progress) {
    double left = 1 - progress;
    return (struct sh_cube){from->width, from->angle * left, from->start * left};
}

/* The sides facing the eye, from `*first` to `*last`: those less than a quarter turn away. */
static void facing(const struct sh_cube *cube, int *first, int *last) {
    double turned = cube->angle / QUARTER;
    *first = (int)floor(turned - 1) + 1;
    *last = (int)ceil(turned + 1) - 1;
}

/* The side a point `along` the strip lies on, or the nearest of those facing the eye. */
static int side_of(const struct sh_cube *cube, double along, int first, int last) {
    double side = floor((along - cube->start) / cube->width);
    return side < first ? first : side > last ? last : (int)side;
}

/* Where the point `along` the strip is on `side`, or on past it, from the square's middle: across
 * to the right, and away from the eye. */
static void point(const struct sh_cube *cube, int side, double along, double *x, double *z) {
    double away = side * QUARTER - cube->angle, half = cube->width / 2;
    double from_middle = along - cube->start - side * cube->width - half;
    *x = half * sin(away) + from_middle * cos(away);
    *z = -half * cos(away) + from_middle * sin(away);
}

void sh_cube_span(const struct sh_cube *cube, double left, double right,
                  struct sh_cube_span *out) {
    double width = cube->width;
    if (!(width > 0)) {
        *out = (struct sh_cube_span){left, right, 1, 0, true};
        return;
    }
    int first, last;
    facing(cube, &first, &last);
    out->shown = right > cube->start + first * width && left < cube->start + (last + 1) * width;
    // The square seen from in front reaches `reach` either side of its middle, and as near the
    // eye from its middle at its nearest corner: drawn as wide as the view, whatever its turn.
    double half = width / 2;
    double reach = half * (fabs(cos(cube->angle)) + fabs(sin(cube->angle)));
    double zoom = half / reach;
    double left_x, left_z, right_x, right_z;
    point(cube, side_of(cube, left, first, last), left, &left_x, &left_z);
    point(cube, side_of(cube, right, first, last), right, &right_x, &right_z);
    out->left = half + left_x * zoom;
    out->right = half + right_x * zoom;
    double depth = (left_z + right_z) / 2 + reach; // its middle behind the nearest corner
    out->scale = SH_CUBE_EYE * width / (SH_CUBE_EYE * width + depth);
    double across = right_x - left_x, length = hypot(across, right_z - left_z);
    out->fade = length > 0 ? SH_CUBE_FADE * (1 - fabs(across) / length) : 0;
}
