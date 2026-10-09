// SPDX-License-Identifier: GPL-3.0-or-later
#include "paw/shadow.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>

static int max_int(int a, int b) { return a > b ? a : b; }

/* How far the blur reaches past the shadow's shape, in logical pixels: three sigmas, where
 * what is left is far below one level of eight bits, and a pixel to spare. */
static int reach(int blur) { return blur > 0 ? (3 * blur + 1) / 2 + 1 : 0; }

void sh_shadow_layout(const struct sh_shadow *shadow, struct sh_shadow_layout *layout) {
    int radius = max_int(shadow->radius, 0), spread = reach(shadow->blur);
    int x = shadow->offset_x, y = shadow->offset_y;
    layout->left = max_int(0, spread - x);
    layout->right = max_int(0, spread + x);
    layout->top = max_int(0, spread - y);
    layout->bottom = max_int(0, spread + y);
    // Along an edge the shadow only changes across it, until the blur of a corner's curve
    // (radius in from the side of the offset shape, plus the blur's reach) or the window's
    // own curved corner comes into it.
    layout->inner_left = max_int(radius, x + radius + spread);
    layout->inner_right = max_int(radius, radius + spread - x);
    layout->inner_top = max_int(radius, y + radius + spread);
    layout->inner_bottom = max_int(radius, radius + spread - y);
    // One column and one row more than the corners take, for the edges to be stretched from.
    layout->width = layout->inner_left + layout->inner_right + 1;
    layout->height = layout->inner_top + layout->inner_bottom + 1;
}

void sh_shadow_image_size(const struct sh_shadow_layout *layout, int width, int height,
                          int *image_width, int *image_height) {
    *image_width = width < layout->inner_left + layout->inner_right ? width : layout->width;
    *image_height = height < layout->inner_top + layout->inner_bottom ? height : layout->height;
}

/* Signed distance from (x, y) to a box with corners of `radius`, negative inside: what the
 * renderer's rounded clip measures, so the cut-out meets the window's clipped edge. */
static double rounded_box(double x, double y, double left, double top, double width,
                          double height, double radius) {
    double half_width = width / 2, half_height = height / 2;
    radius = fmin(radius, fmin(half_width, half_height));
    double qx = fabs(x - left - half_width) - half_width + radius;
    double qy = fabs(y - top - half_height) - half_height + radius;
    return hypot(fmax(qx, 0), fmax(qy, 0)) + fmin(fmax(qx, qy), 0) - radius;
}

/* Widths of three box blurs that together come close to a Gaussian of `sigma` pixels (the
 * passes of a box blur add up to a smooth bell, at a cost that does not grow with the blur). */
static void box_widths(double sigma, int widths[3]) {
    double ideal = sqrt(4 * sigma * sigma + 1);
    int lower = (int)floor(ideal);
    if (lower % 2 == 0)
        --lower;
    double lower_count = (12 * sigma * sigma - 3.0 * lower * lower - 12.0 * lower - 9) /
                         (-4.0 * lower - 4);
    long count = lround(lower_count);
    for (int i = 0; i < 3; ++i)
        widths[i] = i < count ? lower : lower + 2;
}

/* Replaces each value of `lines` lines of `length` (`step` apart within a line, `line_step`
 * between lines) by the mean of the `2 * radius + 1` around it, taking 0 past the ends. */
static void box_blur(float *data, int lines, int length, ptrdiff_t line_step, ptrdiff_t step,
                     int radius, float *line) {
    if (radius <= 0)
        return;
    double scale = 1.0 / (2 * radius + 1);
    for (int l = 0; l < lines; ++l) {
        float *values = data + l * line_step;
        for (int i = 0; i < length; ++i)
            line[i] = values[i * step];
        double sum = 0;
        for (int i = 0; i <= radius && i < length; ++i)
            sum += line[i];
        for (int i = 0; i < length; ++i) {
            values[i * step] = (float)(sum * scale);
            if (i + radius + 1 < length)
                sum += line[i + radius + 1];
            if (i - radius >= 0)
                sum -= line[i - radius];
        }
    }
}

bool sh_shadow_paint(uint32_t *pixels, const struct sh_shadow *shadow, int width, int height,
                     int scale) {
    struct sh_shadow_layout layout;
    sh_shadow_layout(shadow, &layout);
    int s = scale < 1 ? 1 : scale;
    int w = (layout.left + width + layout.right) * s, h = (layout.top + height + layout.bottom) * s;
    float *value = malloc((size_t)w * h * sizeof(*value));
    float *line = malloc((size_t)(w > h ? w : h) * sizeof(*line));
    if (!value || !line) {
        free(value);
        free(line);
        return false;
    }
    // In pixels: the window's box, the shadow's (the same, offset), and their corners.
    double radius = max_int(shadow->radius, 0) * s;
    double window_x = layout.left * s, window_y = layout.top * s;
    double window_width = (double)width * s, window_height = (double)height * s;
    double shape_x = window_x + shadow->offset_x * s, shape_y = window_y + shadow->offset_y * s;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            double d = rounded_box(x + 0.5, y + 0.5, shape_x, shape_y, window_width,
                                   window_height, radius);
            value[(size_t)y * w + x] = (float)fmin(1, fmax(0, 0.5 - d));
        }
    }
    if (shadow->blur > 0) {
        int widths[3];
        box_widths(shadow->blur * s / 2.0, widths);
        for (int pass = 0; pass < 3; ++pass) {
            box_blur(value, h, w, w, 1, widths[pass] / 2, line);
            box_blur(value, w, h, 1, w, widths[pass] / 2, line);
        }
    }
    // The window covers the shadow under it; a translucent one must not show it there.
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            double d = rounded_box(x + 0.5, y + 0.5, window_x, window_y, window_width,
                                   window_height, radius);
            double v = value[(size_t)y * w + x] * fmin(1, fmax(0, 0.5 + d));
            v = fmin(1, fmax(0, v));
            uint32_t channels[4];
            for (int c = 0; c < 4; ++c)
                channels[c] = (uint32_t)lround(shadow->color[c] * v * 255);
            pixels[(size_t)y * w + x] =
                channels[3] << 24 | channels[0] << 16 | channels[1] << 8 | channels[2];
        }
    }
    free(value);
    free(line);
    return true;
}

/* Shares `size` out between the two ends' corners, which take `first` and `second` where it
 * has room for both, and their shares of it where it has not (the image is then painted for
 * that size, so they meet as painted). */
static void split(int size, int first, int second, int *a, int *b) {
    if (size >= first + second) {
        *a = first, *b = second;
        return;
    }
    *a = first + second > 0 ? (int)((long long)size * first / (first + second)) : 0;
    *b = size - *a;
}

void sh_shadow_slices(const struct sh_shadow_layout *layout, int image_width, int image_height,
                      int width, int height, struct sh_shadow_slice slices[SH_SHADOW_SLICES]) {
    int left, right, top, bottom; // how much of the window the corners take
    split(width, layout->inner_left, layout->inner_right, &left, &right);
    split(height, layout->inner_top, layout->inner_bottom, &top, &bottom);
    const struct sh_shadow_layout *l = layout;
    // The image's right and bottom parts start where the window it was painted for ends.
    int image_right = l->left + image_width, image_bottom = l->top + image_height;
    int middle_x = l->left + l->inner_left, middle_y = l->top + l->inner_top;
    int across = width - left - right, down = height - top - bottom;
    const struct sh_shadow_slice all[SH_SHADOW_SLICES] = {
        // Corners, as painted.
        {0, 0, l->left + left, l->top + top, -l->left, -l->top, l->left + left, l->top + top},
        {image_right - right, 0, right + l->right, l->top + top, width - right, -l->top,
         right + l->right, l->top + top},
        {0, image_bottom - bottom, l->left + left, bottom + l->bottom, -l->left, height - bottom,
         l->left + left, bottom + l->bottom},
        {image_right - right, image_bottom - bottom, right + l->right, bottom + l->bottom,
         width - right, height - bottom, right + l->right, bottom + l->bottom},
        // Edges, outside the window (inside it the shadow is cut out), from the middle column
        // or row, stretched along it.
        {middle_x, 0, 1, l->top, left, -l->top, across, l->top},
        {middle_x, image_bottom, 1, l->bottom, left, height, across, l->bottom},
        {0, middle_y, l->left, 1, -l->left, top, l->left, down},
        {image_right, middle_y, l->right, 1, width, top, l->right, down},
    };
    for (int i = 0; i < SH_SHADOW_SLICES; ++i) {
        slices[i] = all[i];
        if (slices[i].width <= 0 || slices[i].height <= 0 || slices[i].to_width <= 0 ||
            slices[i].to_height <= 0)
            slices[i] = (struct sh_shadow_slice){0};
    }
}
