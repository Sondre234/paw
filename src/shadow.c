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
    const double plain[4] = {1, 1, 1, 1};
    sh_shadow_slices_scaled(layout, image_width, image_height, width, height, plain, slices);
}

/* How one side of a stretched shadow is drawn: how far it reaches outside the window, how far
 * its corners reach into it, and how far into it the edge between them runs. */
struct side {
    int outer, inner, margin;
};

/* A side reaching `outer` past the window, with corners `inner` into it, stretched `k` times.
 * Stretched, the image's edge where the window is cut out lands a pixel inside the window,
 * where the window covers it: a renderer sampling the stretched image between pixels would
 * otherwise blend the cut-out into a light seam along the window's edge. */
static struct side stretch(int outer, int inner, double k) {
    if (k <= 1 || outer <= 0)
        return (struct side){(int)lround(k * outer), (int)lround(k * inner), 0};
    double inside = (k * outer + 1) / outer; // the stretch that puts the cut a pixel in
    return (struct side){(int)lround(k * outer), (int)lround(1 + inside * inner), 1};
}

void sh_shadow_slices_scaled(const struct sh_shadow_layout *layout, int image_width,
                             int image_height, int width, int height, const double scale[4],
                             struct sh_shadow_slice slices[SH_SHADOW_SLICES]) {
    int left, right, top, bottom; // how much of the window the corners take
    split(width, layout->inner_left, layout->inner_right, &left, &right);
    split(height, layout->inner_top, layout->inner_bottom, &top, &bottom);
    const struct sh_shadow_layout *l = layout;
    // Scaled about the window's edges, the corners may take no more than the window has.
    double k_left = fmax(1, scale[0]), k_top = fmax(1, scale[1]), k_right = fmax(1, scale[2]),
           k_bottom = fmax(1, scale[3]);
    if (left + right > 0 && k_left * left + k_right * right > width) {
        double most = fmax(1, (double)width / (left + right));
        k_left = fmin(k_left, most), k_right = fmin(k_right, most);
    }
    if (top + bottom > 0 && k_top * top + k_bottom * bottom > height) {
        double most = fmax(1, (double)height / (top + bottom));
        k_top = fmin(k_top, most), k_bottom = fmin(k_bottom, most);
    }
    struct side sl = stretch(l->left, left, k_left), sr = stretch(l->right, right, k_right);
    struct side st = stretch(l->top, top, k_top), sb = stretch(l->bottom, bottom, k_bottom);
    // Where the slices meet, from the window's top-left corner: the shadow's outer edge, the
    // corners' inner edges, and the shadow's far edge, in whole pixels so that they adjoin.
    int x0 = -sl.outer, x1 = sl.inner, x2 = width - sr.inner, x3 = width + sr.outer;
    int y0 = -st.outer, y1 = st.inner, y2 = height - sb.inner, y3 = height + sb.outer;
    if (x2 < x1)
        x1 = x2 = (x1 + x2) / 2;
    if (y2 < y1)
        y1 = y2 = (y1 + y2) / 2;
    // The image's right and bottom parts start where the window it was painted for ends.
    int image_right = l->left + image_width, image_bottom = l->top + image_height;
    int middle_x = l->left + l->inner_left, middle_y = l->top + l->inner_top;
    const struct sh_shadow_slice all[SH_SHADOW_SLICES] = {
        // Corners, as painted.
        {0, 0, l->left + left, l->top + top, x0, y0, x1 - x0, y1 - y0},
        {image_right - right, 0, right + l->right, l->top + top, x2, y0, x3 - x2, y1 - y0},
        {0, image_bottom - bottom, l->left + left, bottom + l->bottom, x0, y2, x1 - x0, y3 - y2},
        {image_right - right, image_bottom - bottom, right + l->right, bottom + l->bottom, x2, y2,
         x3 - x2, y3 - y2},
        // Edges, outside the window (inside it the shadow is cut out), from the middle column
        // or row, stretched along it.
        {middle_x, 0, 1, l->top, x1, y0, x2 - x1, st.margin - y0},
        {middle_x, image_bottom, 1, l->bottom, x1, height - sb.margin, x2 - x1,
         y3 - height + sb.margin},
        {0, middle_y, l->left, 1, x0, y1, sl.margin - x0, y2 - y1},
        {image_right, middle_y, l->right, 1, width - sr.margin, y1, x3 - width + sr.margin,
         y2 - y1},
    };
    for (int i = 0; i < SH_SHADOW_SLICES; ++i) {
        slices[i] = all[i];
        if (slices[i].width <= 0 || slices[i].height <= 0 || slices[i].to_width <= 0 ||
            slices[i].to_height <= 0)
            slices[i] = (struct sh_shadow_slice){0};
    }
}

bool sh_shadow_printed(const struct sh_shadow *shadow) {
    return shadow->blur <= 4;
}

void sh_shadow_lift(const struct sh_shadow *shadow, double progress, struct sh_shadow *lifted,
                    double scale[4], float *opacity) {
    *lifted = *shadow;
    for (int i = 0; i < 4; ++i)
        scale[i] = 1;
    *opacity = 1;
    if (sh_shadow_printed(shadow)) {
        // Printed: two fifths further off, in whole pixels, as crisp and as dark.
        lifted->offset_x += (int)lround(0.4 * shadow->offset_x * progress);
        lifted->offset_y += (int)lround(0.4 * shadow->offset_y * progress);
        return;
    }
    // Cast: softer all round, further the way it falls (down when it falls straight), a little
    // lighter for being spread wider.
    double toward_x = shadow->offset_x > 0 ? 1 : shadow->offset_x < 0 ? -1 : 0;
    double toward_y = shadow->offset_y < 0 ? -1 : 1;
    scale[0] = 1 + (0.5 - 0.15 * toward_x) * progress;
    scale[1] = 1 + (0.5 - 0.15 * toward_y) * progress;
    scale[2] = 1 + (0.5 + 0.15 * toward_x) * progress;
    scale[3] = 1 + (0.5 + 0.15 * toward_y) * progress;
    *opacity = (float)(1 - 0.12 * progress);
}
