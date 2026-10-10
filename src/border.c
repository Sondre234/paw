// SPDX-License-Identifier: GPL-3.0-or-later
/* A window's border in a gradient: its colours, and the pixels of the pieces it is drawn in. */
#include "paw/border.h"
#include <math.h>

/* A gradient's direction, and how far along it a box's farthest corners reach from its centre. */
struct direction {
    double dx, dy, reach;
};

static struct direction direction_of(double angle) {
    double radians = angle * M_PI / 180;
    double dx = cos(radians), dy = sin(radians);
    return (struct direction){dx, dy, fabs(dx) + fabs(dy)};
}

static double progress(struct direction d, double u, double v) {
    double t = 0.5 + ((u - 0.5) * d.dx + (v - 0.5) * d.dy) / d.reach;
    return t < 0 ? 0 : t > 1 ? 1 : t;
}

double sh_gradient_progress(double angle, double u, double v) {
    return progress(direction_of(angle), u, v);
}

void sh_gradient_color(const struct sh_gradient *gradient, double t, float color[4]) {
    int count = gradient->count < 1                   ? 1
                : gradient->count > SH_GRADIENT_STOPS ? SH_GRADIENT_STOPS
                                                      : gradient->count;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    double place = t * (count - 1);
    int first = (int)floor(place);
    if (first >= count - 1)
        first = count > 1 ? count - 2 : 0;
    double along = count > 1 ? place - first : 0;
    const float *a = gradient->stops[first], *b = gradient->stops[count > 1 ? first + 1 : 0];
    for (int i = 0; i < 4; ++i)
        color[i] = (float)(a[i] + (b[i] - a[i]) * along);
}

bool sh_gradient_varies(const struct sh_gradient *gradient) {
    for (int i = 1; i < gradient->count && i < SH_GRADIENT_STOPS; ++i)
        for (int c = 0; c < 4; ++c)
            if (gradient->stops[i][c] != gradient->stops[0][c])
                return true;
    return false;
}

void sh_border_pieces(int width, int height, int border, int radius, int scale,
                      struct sh_border_piece pieces[SH_BORDER_PIECES]) {
    int shorter = width < height ? width : height;
    int r = radius < 0 ? 0 : radius > shorter / 2 ? shorter / 2 : radius;
    int b = border < 0 ? 0 : border, s = scale < 1 ? 1 : scale, c = b + r;
    // Samples along a side: one per pixel, as many as a long side needs at most.
    int along = (width - 2 * r) * s, down = (height - 2 * r) * s;
    along = along > SH_BORDER_SAMPLES ? SH_BORDER_SAMPLES : along;
    down = down > SH_BORDER_SAMPLES ? SH_BORDER_SAMPLES : down;
    const struct sh_border_piece all[SH_BORDER_PIECES] = {
        {r, -b, width - 2 * r, b, along, 1, -1},
        {r, height, width - 2 * r, b, along, 1, -1},
        {-b, r, b, height - 2 * r, 1, down, -1},
        {width, r, b, height - 2 * r, 1, down, -1},
        {-b, -b, c, c, c * s, c * s, 0},
        {width - r, -b, c, c, c * s, c * s, 1},
        {-b, height - r, c, c, c * s, c * s, 2},
        {width - r, height - r, c, c, c * s, c * s, 3},
    };
    for (int i = 0; i < SH_BORDER_PIECES; ++i) {
        pieces[i] = all[i];
        if (b == 0 || pieces[i].width <= 0 || pieces[i].height <= 0 ||
            pieces[i].buffer_width <= 0 || pieces[i].buffer_height <= 0)
            pieces[i].width = pieces[i].height = pieces[i].buffer_width =
                pieces[i].buffer_height = 0;
    }
}

static uint32_t pack(const float color[4], double coverage) {
    uint32_t channels[4];
    for (int i = 0; i < 4; ++i) {
        double value = color[i] * coverage;
        value = value < 0 ? 0 : value > 1 ? 1 : value;
        channels[i] = (uint32_t)lround(value * 255);
    }
    return channels[3] << 24 | channels[0] << 16 | channels[1] << 8 | channels[2];
}

/* How much of the device pixel centred at (x, y) the ring between `inner` and `outer` around
 * the origin covers, smoothed over one pixel at each edge. */
static double ring_coverage(double x, double y, double inner, double outer) {
    double d = sqrt(x * x + y * y);
    double out = outer - d + 0.5, in = inner > 0 ? d - inner + 0.5 : 1;
    out = out < 0 ? 0 : out > 1 ? 1 : out;
    in = in < 0 ? 0 : in > 1 ? 1 : in;
    return out * in;
}

void sh_border_paint(uint32_t *pixels, const struct sh_border_piece *piece, int width, int height,
                     int border, int radius, int scale, const struct sh_gradient *from,
                     const struct sh_gradient *to, double mix) {
    int shorter = width < height ? width : height;
    int r = radius < 0 ? 0 : radius > shorter / 2 ? shorter / 2 : radius;
    int b = border < 0 ? 0 : border, s = scale < 1 ? 1 : scale;
    double box_width = width + 2.0 * b, box_height = height + 2.0 * b;
    // A rounded corner's arc is centred `r` in from the window's corner.
    double centre_x = piece->corner == 1 || piece->corner == 3 ? width - r : r;
    double centre_y = piece->corner >= 2 ? height - r : r;
    struct direction along_from = direction_of(from->angle), along_to = direction_of(to->angle);
    for (int py = 0; py < piece->buffer_height; ++py) {
        for (int px = 0; px < piece->buffer_width; ++px) {
            double x = piece->x + (px + 0.5) * piece->width / piece->buffer_width;
            double y = piece->y + (py + 0.5) * piece->height / piece->buffer_height;
            double u = (x + b) / box_width, v = (y + b) / box_height;
            float a[4], c[4], color[4];
            sh_gradient_color(from, progress(along_from, u, v), a);
            sh_gradient_color(to, progress(along_to, u, v), c);
            for (int i = 0; i < 4; ++i)
                color[i] = (float)(a[i] + (c[i] - a[i]) * mix);
            double coverage = 1;
            if (piece->corner >= 0 && r > 0)
                coverage = ring_coverage((x - centre_x) * s, (y - centre_y) * s, r * s, (r + b) * s);
            pixels[py * piece->buffer_width + px] = pack(color, coverage);
        }
    }
}
