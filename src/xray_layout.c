// SPDX-License-Identifier: GPL-3.0-or-later
#include "paw/xray_layout.h"
#include <math.h>

/* The way the stack rises with the pointer where it was as the view opened: up and to the right,
 * as a deck of cards lying on a table looks from its bottom-left. */
static const double RISE_X = 0.62, RISE_Y = -0.5;
/* How much the pointer turns that way, at the end of its travel, and the shortest and longest
 * the way may get: never so short that the sheets pile up on each other. The stack leans away
 * from the pointer, as seen from where it is, so that the edges of the sheets low in it come out
 * on the pointer's side, under it. */
static const double TURN = 0.9, RISE_MIN = 0.35, RISE_MAX = 1.1;
/* A sheet turned away from the eye is drawn narrower along the way it is turned, this much at
 * most. */
static const double SQUASH = 0.18;
/* Each sheet up is this much larger than the one under it, nearer the eye. */
static const double GROWTH = 0.03;
/* The fanned stack is never drawn larger than this share of the output, even for one sheet. */
static const double MAX_SCALE = 0.7;

static double clamp(double value, double low, double high) {
    return value < low ? low : value > high ? high : value;
}

double sh_xray_gap(int count) {
    return count > 1 ? clamp(0.9 / (count - 1), 0.08, 0.3) : 0;
}

/* The way the stack rises for a tilt, kept between the shortest and longest. */
static void rise(double tilt_x, double tilt_y, double *x, double *y) {
    double dx = RISE_X - TURN * clamp(tilt_x, -1, 1), dy = RISE_Y - TURN * clamp(tilt_y, -1, 1);
    double length = hypot(dx, dy);
    if (length < 1e-9) {
        dx = RISE_X;
        dy = RISE_Y;
        length = hypot(dx, dy);
    }
    double wanted = clamp(length, RISE_MIN, RISE_MAX);
    *x = dx * wanted / length;
    *y = dy * wanted / length;
}

void sh_xray_layout(const struct sh_xray_view *view, const double *lift,
                    struct sh_xray_box *sheets) {
    int count = view->count;
    if (count <= 0)
        return;
    double width = view->width > 1 ? view->width : 1, height = view->height > 1 ? view->height : 1;
    double dx, dy;
    rise(view->tilt_x, view->tilt_y, &dx, &dy);
    double length = hypot(dx, dy);
    double squash_x = 1 - SQUASH * fmin(1, dx * dx), squash_y = 1 - SQUASH * fmin(1, dy * dy);
    double gap = sh_xray_gap(count) * clamp(view->spread, SH_XRAY_SPREAD_MIN, SH_XRAY_SPREAD_MAX) *
                 height;
    // Lifted half a gap at least, or a thirtieth of the output where the gaps are small.
    double reach = fmax(0.5 * gap, height / 30) / length;
    // At full size around the bottom sheet's middle first, then fitted into the area as a whole,
    // with room for a lift at the top: a lift moves a sheet without changing the fit, so that
    // the others stay where they are.
    double left = INFINITY, top = INFINITY, right = -INFINITY, bottom = -INFINITY;
    for (int i = 0; i < count; ++i) {
        double grow = 1 + GROWTH * i;
        double w = width * squash_x * grow, h = height * squash_y * grow;
        double cx = i * gap * dx, cy = i * gap * dy;
        sheets[i] = (struct sh_xray_box){cx - w / 2, cy - h / 2, w, h};
        left = fmin(left, fmin(sheets[i].x, sheets[i].x + reach * dx));
        top = fmin(top, fmin(sheets[i].y, sheets[i].y + reach * dy));
        right = fmax(right, fmax(sheets[i].x, sheets[i].x + reach * dx) + w);
        bottom = fmax(bottom, fmax(sheets[i].y, sheets[i].y + reach * dy) + h);
    }
    struct sh_xray_box area = view->area;
    double scale = fmin(MAX_SCALE, fmin(area.width / (right - left), area.height / (bottom - top)));
    if (!(scale > 0))
        scale = 0;
    double middle_x = (left + right) / 2, middle_y = (top + bottom) / 2;
    double to_x = area.x + area.width / 2, to_y = area.y + area.height / 2;
    double t = view->progress;
    for (int i = 0; i < count; ++i) {
        double up = lift ? clamp(lift[i], 0, 1) : 0;
        struct sh_xray_box fanned = {
            to_x + (sheets[i].x + up * reach * dx - middle_x) * scale,
            to_y + (sheets[i].y + up * reach * dy - middle_y) * scale,
            sheets[i].width * scale,
            sheets[i].height * scale,
        };
        sheets[i] = (struct sh_xray_box){fanned.x * t, fanned.y * t,
                                         width + (fanned.width - width) * t,
                                         height + (fanned.height - height) * t};
    }
}

int sh_xray_sheet_at(const struct sh_xray_box *sheets, int count, double x, double y) {
    for (int i = count - 1; i >= 0; --i) {
        const struct sh_xray_box *s = &sheets[i];
        if (x >= s->x && x < s->x + s->width && y >= s->y && y < s->y + s->height)
            return i;
    }
    return -1;
}

void sh_xray_tilt(double dx, double dy, double width, double height, double *tilt_x,
                  double *tilt_y) {
    *tilt_x = width > 0 ? clamp(2 * dx / width, -1, 1) : 0;
    *tilt_y = height > 0 ? clamp(2 * dy / height, -1, 1) : 0;
}

double sh_xray_spread(double spread, double notches) {
    return clamp(spread * pow(1.2, -notches), SH_XRAY_SPREAD_MIN, SH_XRAY_SPREAD_MAX);
}

struct sh_xray_box sh_xray_area(double width, double height, double *legend) {
    double margin = clamp(fmin(width, height) * 0.05, 16, 56);
    double caption = height >= 480 ? 64 : 0;
    *legend = width >= 1000 ? clamp(width * 0.2, 260, 360) : 0;
    struct sh_xray_box area = {margin + *legend, margin, width - *legend - 2 * margin,
                               height - 2 * margin - caption};
    area.width = fmax(area.width, 1);
    area.height = fmax(area.height, 1);
    return area;
}

double sh_xray_ease(double value, double target, double elapsed, double settle, double epsilon) {
    if (settle <= 0)
        return target;
    if (elapsed <= 0)
        return value;
    // A hundredth of the way is left after `settle`.
    double next = target + (value - target) * exp(-elapsed * log(100) / settle);
    return fabs(next - target) <= epsilon ? target : next;
}
