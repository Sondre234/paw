// SPDX-License-Identifier: GPL-3.0-or-later
/* The blur behind windows (paw/blur.h): an output's background blurred into the picture windows
 * show under them, and the part of it a window covers. */
#include "paw/blur.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define MAX_HALVINGS 6

int sh_blur_halvings(double sigma) {
    int halvings = 0;
    while (halvings < MAX_HALVINGS && sigma / (double)(2 << halvings) >= SH_BLUR_GRID_SIGMA)
        ++halvings;
    return halvings;
}

/* Three boxes whose variances add up to the gaussian's, as close as odd widths come: some of
 * the widest odd width below the ideal one, the rest two wider. */
void sh_blur_boxes(double sigma, int widths[3]) {
    if (!(sigma > 0)) {
        widths[0] = widths[1] = widths[2] = 1;
        return;
    }
    double variance = sigma * sigma, ideal = sqrt(12 * variance / 3 + 1);
    int low = (int)floor(ideal);
    if (low % 2 == 0)
        --low;
    if (low < 1)
        low = 1;
    long narrow = lround((12 * variance - 3.0 * low * low - 12.0 * low - 9) / (-4.0 * low - 4));
    for (int i = 0; i < 3; ++i)
        widths[i] = i < narrow ? low : low + 2;
}

static int clamp_index(int i, int length) {
    return i < 0 ? 0 : i >= length ? length - 1 : i;
}

/* One box blur `radius` either way along `count` lines of `length` values, `step` apart along a
 * line and `next` apart from one line to the next, from `from` into `to`. Past the ends of a
 * line its end values go on. */
static void box(const float *from, float *to, int length, int count, size_t step, size_t next,
                int radius) {
    double scale = 1.0 / (2 * radius + 1);
    for (int line = 0; line < count; ++line) {
        const float *in = from + line * next;
        float *out = to + line * next;
        double sum = 0;
        for (int i = -radius; i <= radius; ++i)
            sum += in[clamp_index(i, length) * step];
        for (int i = 0; i < length; ++i) {
            out[i * step] = (float)(sum * scale);
            sum += in[clamp_index(i + radius + 1, length) * step] -
                   in[clamp_index(i - radius, length) * step];
        }
    }
}

/* A plane of `columns` by `rows` blurred in place as a gaussian of `sigma`, with `scratch` as
 * large. */
static void blur_plane(float *plane, float *scratch, int columns, int rows, double sigma) {
    int widths[3];
    sh_blur_boxes(sigma, widths);
    for (int pass = 0; pass < 3; ++pass) {
        int radius = widths[pass] / 2;
        if (radius < 1)
            continue;
        box(plane, scratch, columns, rows, 1, (size_t)columns, radius);
        box(scratch, plane, rows, columns, (size_t)columns, 1, radius);
    }
}

/* The same grain at the same pixel every time: a hash of its place. */
static uint32_t grain(uint32_t x, uint32_t y) {
    uint32_t h = x * 0x9e3779b1u ^ (y + 0x7f4a7c15u) * 0x85ebca77u;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

static uint32_t channel(float value) {
    return value <= 0 ? 0 : value >= 255 ? 255 : (uint32_t)(value + 0.5f);
}

/* Where a picture's pixel lies on the grid along one axis: the cell `low` and the next, and how
 * far toward the next, for each of `length` pixels, the grid `cells` long and `halvings` times
 * halved. */
static void sampling(int length, int cells, int halvings, int *low, float *toward) {
    double size = (double)(1 << halvings);
    for (int i = 0; i < length; ++i) {
        double at = (i + 0.5) / size - 0.5;
        at = at < 0 ? 0 : at > cells - 1 ? cells - 1 : at;
        low[i] = (int)at;
        if (low[i] > cells - 2)
            low[i] = cells > 1 ? cells - 2 : 0;
        toward[i] = cells > 1 ? (float)(at - low[i]) : 0;
    }
}

bool sh_blur_paint(const uint32_t *in, int width, int height, const struct sh_blur_look *look,
                   uint32_t *out) {
    if (width < 1 || height < 1)
        return false;
    int halvings = sh_blur_halvings(look->sigma);
    int size = 1 << halvings;
    int columns = (width + size - 1) / size, rows = (height + size - 1) / size;
    size_t cells = (size_t)columns * rows;
    float *planes = calloc(4 * cells, sizeof(*planes));
    int *low_x = malloc((size_t)width * sizeof(*low_x));
    int *low_y = malloc((size_t)height * sizeof(*low_y));
    float *toward_x = malloc((size_t)width * sizeof(*toward_x));
    float *toward_y = malloc((size_t)height * sizeof(*toward_y));
    if (!planes || !low_x || !low_y || !toward_x || !toward_y) {
        free(planes);
        free(low_x);
        free(low_y);
        free(toward_x);
        free(toward_y);
        return false;
    }
    float *red = planes, *green = planes + cells, *blue = planes + 2 * cells;
    float *scratch = planes + 3 * cells;
    // Each cell the average of the pixels it covers, fewer along the far edges.
    for (int y = 0; y < height; ++y) {
        const uint32_t *row = in + (size_t)y * width;
        size_t base = (size_t)(y >> halvings) * columns;
        for (int x = 0; x < width; ++x) {
            uint32_t pixel = row[x];
            size_t cell = base + (x >> halvings);
            red[cell] += (float)(pixel >> 16 & 0xff);
            green[cell] += (float)(pixel >> 8 & 0xff);
            blue[cell] += (float)(pixel & 0xff);
        }
    }
    for (int row = 0; row < rows; ++row) {
        int tall = height - row * size < size ? height - row * size : size;
        for (int column = 0; column < columns; ++column) {
            int wide = width - column * size < size ? width - column * size : size;
            size_t cell = (size_t)row * columns + column;
            float share = 1.0f / (float)(wide * tall);
            red[cell] *= share;
            green[cell] *= share;
            blue[cell] *= share;
        }
    }
    double sigma = look->sigma / size;
    for (int plane = 0; plane < 3; ++plane)
        blur_plane(planes + plane * cells, scratch, columns, rows, sigma);
    // Back up to the picture's size, bilinearly, coloured, with the grain.
    sampling(width, columns, halvings, low_x, toward_x);
    sampling(height, rows, halvings, low_y, toward_y);
    int right = columns > 1 ? 1 : 0;
    size_t down = rows > 1 ? (size_t)columns : 0;
    float saturation = look->saturation, brightness = look->brightness;
    float noise = look->noise * 255;
    for (int y = 0; y < height; ++y) {
        size_t top = (size_t)low_y[y] * columns;
        float ty = toward_y[y];
        uint32_t *row = out + (size_t)y * width;
        for (int x = 0; x < width; ++x) {
            size_t a = top + low_x[x];
            float tx = toward_x[x];
            float value[3];
            for (int c = 0; c < 3; ++c) {
                const float *p = planes + c * cells;
                float upper = p[a] + (p[a + right] - p[a]) * tx;
                float lower = p[a + down] + (p[a + down + right] - p[a + down]) * tx;
                value[c] = upper + (lower - upper) * ty;
            }
            if (saturation != 1) {
                float luma = 0.2126f * value[0] + 0.7152f * value[1] + 0.0722f * value[2];
                for (int c = 0; c < 3; ++c)
                    value[c] = luma + (value[c] - luma) * saturation;
            }
            float shift = 0;
            if (noise > 0)
                shift = ((float)(grain((uint32_t)x, (uint32_t)y) >> 8) / 16777216.0f - 0.5f) *
                        noise;
            row[x] = 0xff000000u | channel(value[0] * brightness + shift) << 16 |
                     channel(value[1] * brightness + shift) << 8 |
                     channel(value[2] * brightness + shift);
        }
    }
    free(planes);
    free(low_x);
    free(low_y);
    free(toward_x);
    free(toward_y);
    return true;
}

bool sh_blur_source(struct sh_blur_box window, struct sh_blur_box output, int width, int height,
                    struct sh_blur_box *part, struct sh_blur_area *source) {
    if (output.width < 1 || output.height < 1 || width < 1 || height < 1)
        return false;
    int left = window.x > output.x ? window.x : output.x;
    int top = window.y > output.y ? window.y : output.y;
    int window_right = window.x + window.width, output_right = output.x + output.width;
    int window_bottom = window.y + window.height, output_bottom = output.y + output.height;
    int right = window_right < output_right ? window_right : output_right;
    int bottom = window_bottom < output_bottom ? window_bottom : output_bottom;
    if (right <= left || bottom <= top)
        return false;
    *part = (struct sh_blur_box){left, top, right - left, bottom - top};
    double across = (double)width / output.width, down = (double)height / output.height;
    double x = (left - output.x) * across, y = (top - output.y) * down;
    double x2 = (right - output.x) * across, y2 = (bottom - output.y) * down;
    *source = (struct sh_blur_area){x, y, (x2 < width ? x2 : width) - x,
                                    (y2 < height ? y2 : height) - y};
    return true;
}
