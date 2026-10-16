// SPDX-License-Identifier: GPL-3.0-or-later
#include "paw/heatmap.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int sh_heat_cells(int length) {
    return length > SH_HEAT_CELL ? (length + SH_HEAT_CELL - 1) / SH_HEAT_CELL : 1;
}

static uint32_t saturated(uint32_t a, uint32_t b) {
    return a > UINT32_MAX - b ? UINT32_MAX : a + b;
}

bool sh_heat_grid_resize(struct sh_heat_grid *grid, int columns, int rows) {
    if (columns < 1 || rows < 1)
        return false;
    if (columns == grid->columns && rows == grid->rows && grid->cells[0])
        return true;
    uint32_t *cells[SH_HEAT_KINDS];
    for (int kind = 0; kind < SH_HEAT_KINDS; ++kind) {
        cells[kind] = calloc((size_t)columns * rows, sizeof(**cells));
        if (!cells[kind]) {
            while (kind-- > 0)
                free(cells[kind]);
            return false;
        }
    }
    for (int kind = 0; kind < SH_HEAT_KINDS; ++kind) {
        const uint32_t *old = grid->cells[kind];
        for (int row = 0; old && row < grid->rows; ++row) {
            int to_row = (int)((row + 0.5) * rows / grid->rows);
            for (int column = 0; column < grid->columns; ++column) {
                int to_column = (int)((column + 0.5) * columns / grid->columns);
                uint32_t *cell = &cells[kind][(size_t)to_row * columns + to_column];
                *cell = saturated(*cell, old[(size_t)row * grid->columns + column]);
            }
        }
        free(grid->cells[kind]);
        grid->cells[kind] = cells[kind];
    }
    grid->columns = columns;
    grid->rows = rows;
    return true;
}

void sh_heat_grid_clear(struct sh_heat_grid *grid) {
    for (int kind = 0; kind < SH_HEAT_KINDS; ++kind) {
        if (grid->cells[kind])
            memset(grid->cells[kind], 0, (size_t)grid->columns * grid->rows * sizeof(uint32_t));
    }
}

void sh_heat_grid_free(struct sh_heat_grid *grid) {
    for (int kind = 0; kind < SH_HEAT_KINDS; ++kind)
        free(grid->cells[kind]);
    *grid = (struct sh_heat_grid){0};
}

void sh_heat_add(struct sh_heat_grid *grid, enum sh_heat_kind kind, int column, int row,
                 uint32_t amount) {
    if (!grid->cells[kind])
        return;
    column = column < 0 ? 0 : column >= grid->columns ? grid->columns - 1 : column;
    row = row < 0 ? 0 : row >= grid->rows ? grid->rows - 1 : row;
    uint32_t *cell = &grid->cells[kind][(size_t)row * grid->columns + column];
    *cell = saturated(*cell, amount);
}

uint32_t sh_heat_most(const struct sh_heat_grid *grid, enum sh_heat_kind kind) {
    uint32_t most = 0;
    size_t count = grid->cells[kind] ? (size_t)grid->columns * grid->rows : 0;
    for (size_t i = 0; i < count; ++i)
        most = grid->cells[kind][i] > most ? grid->cells[kind][i] : most;
    return most;
}

/* The farthest a blur reaches, in cells. */
enum { REACH_MAX = 48 };

/* The gaussian of `weights` (by distance, up to `reach`) along a row of `count` values, weighing
 * those inside the row alone: `sums[r]` adds up `weights` 1 to r, for the share of the kernel a
 * cell near an end has. A row of nothing stays as it is. Each shift of the row is added whole,
 * which runs along memory. */
static void blur_row(float *row, int count, const float *weights, const float *sums, int reach,
                     float *scratch) {
    bool any = false;
    for (int i = 0; i < count; ++i) {
        scratch[i] = row[i];
        any = any || row[i] != 0;
    }
    if (!any)
        return;
    memset(row, 0, (size_t)count * sizeof(*row));
    for (int shift = -reach; shift <= reach; ++shift) {
        float w = weights[shift < 0 ? -shift : shift];
        int from = shift < 0 ? -shift : 0, to = shift > 0 ? count - shift : count;
        for (int i = from; i < to; ++i)
            row[i] += w * scratch[i + shift];
    }
    for (int i = 0; i < count; ++i) {
        int left = i < reach ? i : reach, right = count - 1 - i < reach ? count - 1 - i : reach;
        row[i] /= weights[0] + sums[left] + sums[right];
    }
}

void sh_heat_blur(float *values, int columns, int rows, double sigma, float *scratch) {
    if (!(sigma > 0) || columns < 1 || rows < 1)
        return;
    // exp(-r² / 2σ²) falls below a millionth past r = σ √(2 ln 10⁶).
    int reach = (int)ceil(sigma * sqrt(2 * log(1e6)));
    reach = reach > REACH_MAX ? REACH_MAX : reach;
    float weights[REACH_MAX + 1], sums[REACH_MAX + 1] = {0};
    for (int r = 0; r <= reach; ++r) {
        weights[r] = (float)exp(-r * r / (2 * sigma * sigma));
        sums[r] = r ? sums[r - 1] + weights[r] : 0;
    }
    for (int row = 0; row < rows; ++row)
        blur_row(values + (size_t)row * columns, columns, weights, sums, reach, scratch);
    // Down the columns as sums of whole rows, which run along memory too.
    memcpy(scratch, values, (size_t)columns * rows * sizeof(*values));
    for (int row = 0; row < rows; ++row) {
        int up = row < reach ? row : reach, down = rows - 1 - row < reach ? rows - 1 - row : reach;
        float weight = weights[0] + sums[up] + sums[down];
        float *out = values + (size_t)row * columns;
        memset(out, 0, (size_t)columns * sizeof(*out));
        for (int from = row - up; from <= row + down; ++from) {
            float w = weights[from < row ? row - from : from - row] / weight;
            const float *in = scratch + (size_t)from * columns;
            for (int column = 0; column < columns; ++column)
                out[column] += w * in[column];
        }
    }
}

void sh_heat_field(const struct sh_heat_grid *grid, enum sh_heat_kind kind, float *field,
                   float *scratch) {
    size_t count = (size_t)grid->columns * grid->rows;
    for (size_t i = 0; i < count; ++i)
        field[i] = grid->cells[kind] ? (float)grid->cells[kind][i] : 0;
    sh_heat_blur(field, grid->columns, grid->rows, SH_HEAT_BLUR, scratch);
}

float sh_heat_peak(const float *values, size_t count) {
    float most = 0;
    for (size_t i = 0; i < count; ++i)
        most = values[i] > most ? values[i] : most;
    return most;
}

/* How many times smaller than the hottest value the coldest that shows is. */
#define SPAN 10000.0

double sh_heat_level(double value, double most) {
    if (!(most > 0) || !(value > 0))
        return 0;
    double level = log1p(SPAN * value / most) / log1p(SPAN);
    return level > 1 ? 1 : level;
}

struct sh_heat_color sh_heat_color(double level) {
    static const struct {
        double at, r, g, b;
    } stops[] = {
        {0.2, 0.0, 0.25, 1.0},  // blue
        {0.4, 0.0, 0.9, 1.0},   // cyan
        {0.55, 0.1, 0.9, 0.2},  // green
        {0.7, 1.0, 0.9, 0.0},   // yellow
        {0.85, 1.0, 0.15, 0.0}, // red
        {1.0, 1.0, 1.0, 1.0},   // white
    };
    if (!(level > 0))
        return (struct sh_heat_color){stops[0].r, stops[0].g, stops[0].b, 0};
    if (level < stops[0].at)
        return (struct sh_heat_color){stops[0].r, stops[0].g, stops[0].b, level / stops[0].at};
    size_t last = sizeof(stops) / sizeof(*stops) - 1;
    for (size_t i = 1; i <= last; ++i) {
        if (level <= stops[i].at) {
            double t = (level - stops[i - 1].at) / (stops[i].at - stops[i - 1].at);
            return (struct sh_heat_color){stops[i - 1].r + (stops[i].r - stops[i - 1].r) * t,
                                          stops[i - 1].g + (stops[i].g - stops[i - 1].g) * t,
                                          stops[i - 1].b + (stops[i].b - stops[i - 1].b) * t, 1};
        }
    }
    return (struct sh_heat_color){stops[last].r, stops[last].g, stops[last].b, 1};
}

static uint32_t byte(double value) {
    return (uint32_t)lround((value < 0 ? 0 : value > 1 ? 1 : value) * 255);
}

/* A straight colour at `alpha` as a premultiplied ARGB pixel. */
static uint32_t premultiplied(double r, double g, double b, double alpha) {
    return byte(alpha) << 24 | byte(r * alpha) << 16 | byte(g * alpha) << 8 | byte(b * alpha);
}

uint32_t sh_heat_pixel(double level, double opacity) {
    struct sh_heat_color color = sh_heat_color(level);
    return premultiplied(color.r, color.g, color.b, color.a * opacity);
}

/* One axis of sh_heat_resample: `count` values `stride` apart to `to_count` `to_stride` apart. */
static void resample_line(const float *from, int count, size_t stride, float *to, int to_count,
                          size_t to_stride) {
    double ratio = (double)count / to_count;
    for (int i = 0; i < to_count; ++i) {
        double value = 0;
        if (ratio > 1) {
            double start = i * ratio, end = start + ratio;
            for (int j = (int)start; j < count && j < end; ++j) {
                double covered = fmin(end, j + 1) - fmax(start, j);
                if (covered > 0)
                    value += covered * from[(size_t)j * stride];
            }
            value /= ratio;
        } else {
            double at = (i + 0.5) * ratio - 0.5;
            at = at < 0 ? 0 : at > count - 1 ? count - 1 : at;
            int j = (int)at;
            int next = j + 1 < count ? j + 1 : j;
            value =
                from[(size_t)j * stride] * (1 - (at - j)) + from[(size_t)next * stride] * (at - j);
        }
        to[(size_t)i * to_stride] = (float)value;
    }
}

void sh_heat_resample(const float *from, int columns, int rows, float *to, int to_columns,
                      int to_rows, float *scratch) {
    if (columns < 1 || rows < 1 || to_columns < 1 || to_rows < 1)
        return;
    for (int row = 0; row < rows; ++row)
        resample_line(from + (size_t)row * columns, columns, 1, scratch + (size_t)row * to_columns,
                      to_columns, 1);
    for (int column = 0; column < to_columns; ++column)
        resample_line(scratch + column, rows, (size_t)to_columns, to + column, to_rows,
                      (size_t)to_columns);
}

/* How finely sh_heat_paint tells levels apart: finer than a colour's 256 steps. */
enum { PAINT_LEVELS = 1024 };

void sh_heat_paint(const float *field, int columns, int rows, float most, double opacity,
                   uint32_t *pixels) {
    // The colour of each level once, and a cell's level as sh_heat_level gives it.
    uint32_t colors[PAINT_LEVELS + 1];
    for (int i = 0; i <= PAINT_LEVELS; ++i)
        colors[i] = sh_heat_pixel((double)i / PAINT_LEVELS, opacity);
    float scale = most > 0 ? (float)(SPAN / most) : 0;
    float steps = (float)(PAINT_LEVELS / log1p(SPAN));
    size_t count = (size_t)columns * rows;
    for (size_t i = 0; i < count; ++i) {
        float level = field[i] > 0 ? log1pf(field[i] * scale) * steps + 0.5F : 0;
        pixels[i] = colors[level < PAINT_LEVELS ? (int)level : PAINT_LEVELS];
    }
}

/* The legend's glyphs, 5 dots wide (fewer for '.' and ' ') and 7 high, a row a byte, its dots in
 * the five low bits from the left. */
static const struct {
    char c;
    int width;
    uint8_t rows[7];
} glyphs[] = {
    {'0', 5, {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e}},
    {'1', 5, {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e}},
    {'2', 5, {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f}},
    {'3', 5, {0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e}},
    {'4', 5, {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02}},
    {'5', 5, {0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e}},
    {'6', 5, {0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e}},
    {'7', 5, {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', 5, {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e}},
    {'9', 5, {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c}},
    {'.', 2, {0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x03}},
    {' ', 3, {0}},
    {'c', 5, {0x00, 0x00, 0x0e, 0x10, 0x10, 0x11, 0x0e}},
    {'e', 5, {0x00, 0x00, 0x0e, 0x11, 0x1f, 0x10, 0x0e}},
    {'h', 5, {0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x11}},
    {'i', 3, {0x02, 0x00, 0x06, 0x02, 0x02, 0x02, 0x07}},
    {'k', 5, {0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12}},
    {'l', 3, {0x06, 0x02, 0x02, 0x02, 0x02, 0x02, 0x07}},
    {'m', 5, {0x00, 0x00, 0x1a, 0x15, 0x15, 0x11, 0x11}},
    {'n', 5, {0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11}},
    {'o', 5, {0x00, 0x00, 0x0e, 0x11, 0x11, 0x11, 0x0e}},
    {'p', 5, {0x00, 0x00, 0x1e, 0x11, 0x1e, 0x10, 0x10}},
    {'r', 5, {0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10}},
    {'s', 5, {0x00, 0x00, 0x0e, 0x10, 0x0e, 0x01, 0x1e}},
    {'t', 5, {0x08, 0x08, 0x1c, 0x08, 0x08, 0x09, 0x06}},
};

static int glyph_of(char c) {
    for (size_t i = 0; i < sizeof(glyphs) / sizeof(*glyphs); ++i) {
        if (glyphs[i].c == c)
            return (int)i;
    }
    return -1;
}

int sh_heat_text_width(const char *text) {
    int width = 0;
    for (; *text; ++text) {
        int glyph = glyph_of(*text);
        if (glyph >= 0)
            width += (width ? 1 : 0) + glyphs[glyph].width;
    }
    return width;
}

/* `color` (premultiplied) laid over the pixel at `x`, `y` of `pixels`, if inside. */
static void blend(uint32_t *pixels, int width, int height, int x, int y, uint32_t color) {
    if (x < 0 || y < 0 || x >= width || y >= height)
        return;
    uint32_t *pixel = &pixels[(size_t)y * width + x];
    uint32_t keep = 255 - (color >> 24), result = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        uint32_t channel =
            ((color >> shift) & 0xff) + (((*pixel >> shift) & 0xff) * keep + 127) / 255;
        result |= (channel > 255 ? 255 : channel) << shift;
    }
    *pixel = result;
}

void sh_heat_text(uint32_t *pixels, int width, int height, int x, int y, int dot, const char *text,
                  uint32_t color) {
    for (; *text; ++text) {
        int glyph = glyph_of(*text);
        if (glyph < 0)
            continue;
        int glyph_width = glyphs[glyph].width;
        for (int row = 0; row < 7; ++row) {
            for (int column = 0; column < glyph_width; ++column) {
                if (!(glyphs[glyph].rows[row] >> (glyph_width - 1 - column) & 1))
                    continue;
                for (int py = 0; py < dot; ++py) {
                    for (int px = 0; px < dot; ++px)
                        blend(pixels, width, height, x + column * dot + px, y + row * dot + py,
                              color);
                }
            }
        }
        x += (glyph_width + 1) * dot;
    }
}

void sh_heat_label(char *text, size_t size, enum sh_heat_kind kind, double value) {
    if (size)
        text[0] = '\0';
    if (!isfinite(value)) // no count or time: the label stays empty
        return;
    if (kind == SH_HEAT_CLICKS) {
        unsigned clicks = (unsigned)lround(value);
        snprintf(text, size, "%u click%s", clicks, clicks == 1 ? "" : "s");
        return;
    }
    static const struct {
        double below, unit;
        const char *format;
    } units[] = {
        {1000, 1, "%.0f ms"},         // below a second
        {10e3, 1000, "%.1f s"},       // seconds to a tenth below ten
        {60e3, 1000, "%.0f s"},       // whole seconds below a minute
        {600e3, 60e3, "%.1f min"},    // minutes to a tenth below ten
        {3600e3, 60e3, "%.0f min"},   // whole minutes below an hour
        {36000e3, 3600e3, "%.1f h"},  // hours to a tenth below ten
        {INFINITY, 3600e3, "%.0f h"}, // whole hours
    };
    for (size_t i = 0; i < sizeof(units) / sizeof(*units); ++i) {
        // Rounding up to the next unit's start ("1000 ms", "60 s") writes it in that unit.
        char probe[32];
        snprintf(probe, sizeof(probe), units[i].format, value / units[i].unit);
        if (atof(probe) * units[i].unit < units[i].below) {
            snprintf(text, size, "%s", probe);
            return;
        }
    }
}

/* The legend's layout in logical pixels: its padding and corner, the dot of its text, and the
 * scale's bar. */
enum {
    LEGEND_PAD = 12,
    LEGEND_RADIUS = 8,
    LEGEND_DOT = 2,
    LEGEND_BAR_Y = LEGEND_PAD + 7 * LEGEND_DOT + 8,
    LEGEND_BAR_HEIGHT = 10,
    LEGEND_TICK = 4,
    LEGEND_LABEL_Y = LEGEND_BAR_Y + LEGEND_BAR_HEIGHT + LEGEND_TICK + 2,
};

void sh_heat_legend(uint32_t *pixels, int scale, enum sh_heat_kind kind, double most) {
    if (scale < 1)
        scale = 1;
    int width = SH_HEAT_LEGEND_WIDTH * scale, height = SH_HEAT_LEGEND_HEIGHT * scale;
    // The card: black, see-through, its corners rounded with smooth edges.
    double radius = LEGEND_RADIUS * scale;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            double dx = fmax(fmax(radius - (x + 0.5), x + 0.5 - (width - radius)), 0);
            double dy = fmax(fmax(radius - (y + 0.5), y + 0.5 - (height - radius)), 0);
            double inside = radius - hypot(dx, dy) + 0.5;
            double coverage = inside < 0 ? 0 : inside > 1 ? 1 : inside;
            pixels[(size_t)y * width + x] = premultiplied(0.08, 0.09, 0.11, 0.7 * coverage);
        }
    }
    const uint32_t text = premultiplied(1, 1, 1, 0.92);
    int dot = LEGEND_DOT * scale;
    sh_heat_text(pixels, width, height, LEGEND_PAD * scale, LEGEND_PAD * scale, dot,
                 kind == SH_HEAT_CLICKS ? "clicks" : "pointer rest", text);
    // The scale from cold to hot, as the overlay draws it, and its ends' ticks.
    int left = LEGEND_PAD * scale, right = width - LEGEND_PAD * scale;
    for (int x = left; x < right; ++x) {
        uint32_t color = sh_heat_pixel((x - left + 0.5) / (right - left), 1);
        for (int y = LEGEND_BAR_Y * scale; y < (LEGEND_BAR_Y + LEGEND_BAR_HEIGHT) * scale; ++y)
            blend(pixels, width, height, x, y, color);
    }
    for (int y = (LEGEND_BAR_Y + LEGEND_BAR_HEIGHT) * scale;
         y < (LEGEND_BAR_Y + LEGEND_BAR_HEIGHT + LEGEND_TICK) * scale; ++y) {
        for (int x = 0; x < scale; ++x) {
            blend(pixels, width, height, left + x, y, text);
            blend(pixels, width, height, right - 1 - x, y, text);
        }
    }
    char label[32];
    sh_heat_label(label, sizeof(label), kind, most);
    sh_heat_text(pixels, width, height, left, LEGEND_LABEL_Y * scale, dot, "0", text);
    sh_heat_text(pixels, width, height, right - sh_heat_text_width(label) * dot,
                 LEGEND_LABEL_Y * scale, dot, label, text);
}
