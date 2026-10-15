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

/* One box blur `radius` either way along each of `rows` rows of `columns` values, from `from`
 * into `to`. Past the ends of a row its end values go on. */
static void box_rows(const float *from, float *to, int columns, int rows, int radius) {
    double scale = 1.0 / (2 * radius + 1);
    for (int row = 0; row < rows; ++row) {
        const float *in = from + (size_t)row * columns;
        float *out = to + (size_t)row * columns;
        double sum = 0;
        for (int i = -radius; i <= radius; ++i)
            sum += in[clamp_index(i, columns)];
        for (int i = 0; i < columns; ++i) {
            out[i] = (float)(sum * scale);
            sum += in[clamp_index(i + radius + 1, columns)] - in[clamp_index(i - radius, columns)];
        }
    }
}

/* The same down the columns, all of them a row at a time, which reads the plane in order:
 * `sums` holds `columns` running sums. */
static void box_columns(const float *from, float *to, int columns, int rows, int radius,
                        double *sums) {
    double scale = 1.0 / (2 * radius + 1);
    for (int column = 0; column < columns; ++column)
        sums[column] = 0;
    for (int i = -radius; i <= radius; ++i) {
        const float *in = from + (size_t)clamp_index(i, rows) * columns;
        for (int column = 0; column < columns; ++column)
            sums[column] += in[column];
    }
    for (int row = 0; row < rows; ++row) {
        float *out = to + (size_t)row * columns;
        const float *add = from + (size_t)clamp_index(row + radius + 1, rows) * columns;
        const float *drop = from + (size_t)clamp_index(row - radius, rows) * columns;
        for (int column = 0; column < columns; ++column) {
            out[column] = (float)(sums[column] * scale);
            sums[column] += add[column] - drop[column];
        }
    }
}

/* A plane of `columns` by `rows` blurred in place as a gaussian of `sigma`, with `scratch` as
 * large and `sums` as long as a row. */
static void blur_plane(float *plane, float *scratch, double *sums, int columns, int rows,
                       double sigma) {
    int widths[3];
    sh_blur_boxes(sigma, widths);
    for (int pass = 0; pass < 3; ++pass) {
        int radius = widths[pass] / 2;
        if (radius < 1)
            continue;
        box_rows(plane, scratch, columns, rows, radius);
        box_columns(scratch, plane, columns, rows, radius, sums);
    }
}

static uint32_t channel(float value) {
    return value <= 0 ? 0 : value >= 255 ? 255 : (uint32_t)(value + 0.5f);
}

bool sh_blur_grid(uint32_t *grid, int columns, int rows, const struct sh_blur_look *look) {
    if (columns < 1 || rows < 1)
        return false;
    size_t cells = (size_t)columns * rows;
    float *planes = malloc(4 * cells * sizeof(*planes));
    double *sums = malloc((size_t)columns * sizeof(*sums));
    if (!planes || !sums) {
        free(planes);
        free(sums);
        return false;
    }
    float *red = planes, *green = planes + cells, *blue = planes + 2 * cells;
    float *scratch = planes + 3 * cells;
    for (size_t cell = 0; cell < cells; ++cell) {
        red[cell] = (float)(grid[cell] >> 16 & 0xff);
        green[cell] = (float)(grid[cell] >> 8 & 0xff);
        blue[cell] = (float)(grid[cell] & 0xff);
    }
    for (int plane = 0; plane < 3; ++plane)
        blur_plane(planes + plane * cells, scratch, sums, columns, rows, look->sigma);
    float saturation = look->saturation, brightness = look->brightness;
    float lower = look->noise * 255 / 2; // the grain's average, which it adds back
    for (size_t cell = 0; cell < cells; ++cell) {
        float luma = 0.2126f * red[cell] + 0.7152f * green[cell] + 0.0722f * blue[cell];
        float r = (luma + (red[cell] - luma) * saturation) * brightness - lower;
        float g = (luma + (green[cell] - luma) * saturation) * brightness - lower;
        float b = (luma + (blue[cell] - luma) * saturation) * brightness - lower;
        grid[cell] = 0xff000000u | channel(r) << 16 | channel(g) << 8 | channel(b);
    }
    free(planes);
    free(sums);
    return true;
}

void sh_blur_grain(uint32_t *tile, int size, float noise) {
    float most = noise * 255;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            uint32_t h = (uint32_t)x * 0x9e3779b1u ^ ((uint32_t)y + 0x7f4a7c15u) * 0x85ebca77u;
            h ^= h >> 16;
            h *= 0x7feb352du;
            h ^= h >> 15;
            h *= 0x846ca68bu;
            h ^= h >> 16;
            uint32_t grey = channel((float)(h >> 8) / 16777216.0f * most);
            tile[(size_t)y * size + x] = grey << 16 | grey << 8 | grey; // alpha 0: added
        }
    }
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
