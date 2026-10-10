// SPDX-License-Identifier: GPL-3.0-or-later
/* Gradient borders: where along a gradient a point of the frame is, its colours between the
 * stops, the pieces a border is drawn in, and their pixels, rounded corners included. */
#include "paw/border.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "check.h"

static bool near(double a, double b) { return fabs(a - b) < 1e-6; }

static bool same(const float a[4], float r, float g, float b, float alpha) {
    return fabsf(a[0] - r) < 1e-5F && fabsf(a[1] - g) < 1e-5F && fabsf(a[2] - b) < 1e-5F &&
           fabsf(a[3] - alpha) < 1e-5F;
}

/* The channels of a premultiplied ARGB pixel. */
static void channels(uint32_t pixel, int *a, int *r, int *g, int *b) {
    *a = pixel >> 24, *r = (pixel >> 16) & 0xff, *g = (pixel >> 8) & 0xff, *b = pixel & 0xff;
}

int main(void) {
    // Progress: 0 degrees left to right, 90 top to bottom, 45 corner to corner, and the
    // opposite ways past 180.
    CHECK(near(sh_gradient_progress(0, 0, 0.7), 0) && near(sh_gradient_progress(0, 1, 0.2), 1) &&
              near(sh_gradient_progress(0, 0.25, 0.9), 0.25),
          "0 degrees runs from left to right");
    CHECK(near(sh_gradient_progress(90, 0.3, 0), 0) && near(sh_gradient_progress(90, 0.8, 1), 1) &&
              near(sh_gradient_progress(90, 0.1, 0.4), 0.4),
          "90 degrees runs from top to bottom");
    CHECK(near(sh_gradient_progress(45, 0, 0), 0) && near(sh_gradient_progress(45, 1, 1), 1) &&
              near(sh_gradient_progress(45, 1, 0), 0.5) && near(sh_gradient_progress(45, 0, 1), 0.5),
          "45 degrees runs from the top-left corner to the bottom-right one");
    CHECK(near(sh_gradient_progress(180, 0, 0.5), 1) && near(sh_gradient_progress(270, 0.5, 0), 1) &&
              near(sh_gradient_progress(-90, 0.5, 1), 0) && near(sh_gradient_progress(360, 1, 0), 1),
          "past 180 degrees the gradient runs the other way");
    CHECK(near(sh_gradient_progress(30, -1, -1), 0) && near(sh_gradient_progress(30, 2, 2), 1),
          "points outside the box are kept to its ends");

    // Colours between evenly spaced stops, premultiplied.
    struct sh_gradient two = {{{1, 0, 0, 1}, {0, 0, 0.5F, 0.5F}}, 2, 45};
    float color[4];
    sh_gradient_color(&two, 0, color);
    CHECK(same(color, 1, 0, 0, 1), "the first stop at the start");
    sh_gradient_color(&two, 1, color);
    CHECK(same(color, 0, 0, 0.5F, 0.5F), "the last stop at the end");
    sh_gradient_color(&two, 0.5, color);
    CHECK(same(color, 0.5F, 0, 0.25F, 0.75F), "halfway between two stops");
    sh_gradient_color(&two, 1.5, color);
    CHECK(same(color, 0, 0, 0.5F, 0.5F), "past the end is the end");
    struct sh_gradient three = {{{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}}, 3, 0};
    sh_gradient_color(&three, 0.5, color);
    CHECK(same(color, 0, 1, 0, 1), "the middle of three stops at the middle");
    sh_gradient_color(&three, 0.75, color);
    CHECK(same(color, 0, 0.5F, 0.5F, 1), "between the second and third");
    struct sh_gradient one = {{{0.2F, 0.4F, 0.6F, 1}}, 1, 0};
    sh_gradient_color(&one, 0.3, color);
    CHECK(same(color, 0.2F, 0.4F, 0.6F, 1), "one colour everywhere");
    CHECK(sh_gradient_varies(&two) && sh_gradient_varies(&three) && !sh_gradient_varies(&one),
          "only gradients vary");
    struct sh_gradient flat = {{{0.2F, 0.4F, 0.6F, 1}, {0.2F, 0.4F, 0.6F, 1}}, 2, 45};
    CHECK(!sh_gradient_varies(&flat), "a gradient of one colour does not vary");

    // Pieces: strips along the sides, squares at the corners.
    struct sh_border_piece pieces[SH_BORDER_PIECES];
    sh_border_pieces(100, 50, 2, 0, 1, pieces);
    const struct sh_border_piece square[SH_BORDER_PIECES] = {
        {0, -2, 100, 2, 100, 1, -1}, {0, 50, 100, 2, 100, 1, -1}, {-2, 0, 2, 50, 1, 50, -1},
        {100, 0, 2, 50, 1, 50, -1},  {-2, -2, 2, 2, 2, 2, 0},     {100, -2, 2, 2, 2, 2, 1},
        {-2, 50, 2, 2, 2, 2, 2},     {100, 50, 2, 2, 2, 2, 3},
    };
    for (int i = 0; i < SH_BORDER_PIECES; ++i) {
        const struct sh_border_piece *p = &pieces[i], *q = &square[i];
        CHECK(p->x == q->x && p->y == q->y && p->width == q->width && p->height == q->height &&
                  p->buffer_width == q->buffer_width && p->buffer_height == q->buffer_height &&
                  p->corner == q->corner,
              "square piece %d: %d,%d %dx%d in %dx%d", i, p->x, p->y, p->width, p->height,
              p->buffer_width, p->buffer_height);
    }
    sh_border_pieces(100, 50, 2, 10, 2, pieces);
    CHECK(pieces[0].x == 10 && pieces[0].width == 80 && pieces[0].buffer_width == 160 &&
              pieces[2].y == 10 && pieces[2].height == 30 && pieces[2].buffer_height == 60,
          "rounded sides run between the corners, a sample per pixel");
    CHECK(pieces[4].x == -2 && pieces[4].y == -2 && pieces[4].width == 12 &&
              pieces[4].buffer_width == 24 && pieces[7].x == 90 && pieces[7].y == 40,
          "rounded corners are border and radius square");
    sh_border_pieces(4000, 3000, 3, 0, 2, pieces);
    CHECK(pieces[0].buffer_width == SH_BORDER_SAMPLES && pieces[3].buffer_height == SH_BORDER_SAMPLES,
          "a long side takes SH_BORDER_SAMPLES samples at most");
    sh_border_pieces(30, 20, 2, 40, 1, pieces);
    CHECK(pieces[4].width == 12 && pieces[2].width == 0 && pieces[0].width == 10,
          "the radius is at most half the shorter side, leaving no left side to draw");
    sh_border_pieces(100, 50, 0, 10, 1, pieces);
    CHECK(pieces[0].width == 0 && pieces[4].width == 0, "no border, nothing to draw");

    // Pixels: a strip follows the gradient along it, from the frame's box.
    struct sh_gradient red_blue = {{{1, 0, 0, 1}, {0, 0, 1, 1}}, 2, 0};
    sh_border_pieces(100, 50, 2, 0, 1, pieces);
    uint32_t *top = calloc(100, sizeof(*top));
    sh_border_paint(top, &pieces[0], 100, 50, 2, 0, 1, &red_blue, &red_blue, 0);
    int a, r, g, b;
    channels(top[0], &a, &r, &g, &b);
    CHECK(a == 255 && r > 245 && b < 10, "the top's left end is red (%d %d %d %d)", a, r, g, b);
    channels(top[99], &a, &r, &g, &b);
    CHECK(a == 255 && b > 245 && r < 10, "its right end is blue (%d %d %d %d)", a, r, g, b);
    channels(top[49], &a, &r, &g, &b);
    CHECK(abs(r - 127) <= 2 && abs(b - 127) <= 2, "its middle half of each (%d %d)", r, b);
    // Crossfading to another gradient mixes the two.
    struct sh_gradient green = {{{0, 1, 0, 1}}, 1, 0};
    sh_border_paint(top, &pieces[0], 100, 50, 2, 0, 1, &red_blue, &green, 0.25);
    channels(top[0], &a, &r, &g, &b);
    CHECK(abs(r - 187) <= 2 && abs(g - 64) <= 2 && a == 255, "a quarter of the way to green");
    free(top);

    // A rounded corner: clear inside the window and outside its outer arc, solid on the ring.
    sh_border_pieces(100, 50, 2, 10, 1, pieces);
    struct sh_border_piece *corner = &pieces[4];
    uint32_t *pixels = calloc((size_t)corner->buffer_width * corner->buffer_height, sizeof(*pixels));
    struct sh_gradient white = {{{1, 1, 1, 1}}, 1, 0};
    sh_border_paint(pixels, corner, 100, 50, 2, 10, 1, &white, &white, 0);
    int width = corner->buffer_width;
    channels(pixels[11 * width + 11], &a, &r, &g, &b);
    CHECK(a == 0, "inside the window's corner is clear (%d)", a);
    channels(pixels[0], &a, &r, &g, &b);
    CHECK(a == 0, "outside the outer arc is clear (%d)", a);
    channels(pixels[11 * width + 0], &a, &r, &g, &b);
    CHECK(a >= 250 && r >= 250, "the ring along the left side is solid (%d)", a);
    channels(pixels[0 * width + 11], &a, &r, &g, &b);
    CHECK(a >= 250, "and along the top (%d)", a);
    // On the diagonal the ring lies 10 to 12 pixels from the arc's centre at (12, 12).
    channels(pixels[4 * width + 4], &a, &r, &g, &b);
    CHECK(a > 200, "the ring on the diagonal (%d)", a);
    channels(pixels[2 * width + 2], &a, &r, &g, &b);
    CHECK(a < 60, "past it on the diagonal (%d)", a);
    channels(pixels[6 * width + 6], &a, &r, &g, &b);
    CHECK(a < 60, "inside it on the diagonal (%d)", a);
    // Square corners are solid all over.
    sh_border_pieces(100, 50, 2, 0, 1, pieces);
    sh_border_paint(pixels, &pieces[7], 100, 50, 2, 0, 1, &white, &white, 0);
    channels(pixels[0], &a, &r, &g, &b);
    CHECK(a == 255 && r == 255, "a square corner is solid");
    // The bottom-right corner of a 45 degree gradient is its end colour.
    struct sh_gradient diagonal = {{{1, 0, 0, 1}, {0, 0, 1, 1}}, 2, 45};
    sh_border_paint(pixels, &pieces[7], 100, 50, 2, 0, 1, &diagonal, &diagonal, 0);
    channels(pixels[3], &a, &r, &g, &b);
    CHECK(b > 240 && r < 15, "the far corner of a diagonal gradient (%d %d)", r, b);
    free(pixels);

    if (failures)
        return 1;
    puts("border passed");
    return 0;
}
