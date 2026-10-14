// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* The pointer heatmap's arithmetic, which the compositor's overlay and `paw heatmap` share: a
 * grid over an output of cells SH_HEAT_CELL logical pixels square, counting how long the pointer
 * rested in each (milliseconds) and how often it was clicked there; the grid blurred into a smooth
 * field; where a value lies on the scale from cold to hot and the colour there; a field resampled
 * to another size (a terminal's, an image's); and the overlay's pictures, the field's and its
 * legend's, with the few letters and digits the legend needs. Nothing here touches wlroots or the
 * clock. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { SH_HEAT_CELL = 8 };

enum sh_heat_kind {
    SH_HEAT_MOTION, /* milliseconds the pointer rested in a cell */
    SH_HEAT_CLICKS, /* buttons pressed there */
    SH_HEAT_KINDS,
};

/* A grid of `columns` by `rows` cells of each kind, row by row; no cells while it is empty. */
struct sh_heat_grid {
    int columns, rows;
    uint32_t *cells[SH_HEAT_KINDS];
};

/* The cells that cover `length` logical pixels: at least one. */
int sh_heat_cells(int length);

/* Gives `grid` `columns` by `rows` cells, each old cell's counts going to the new cell its middle
 * falls in, so that a monitor changing its mode or scale keeps its heat where it was on the screen
 * and its totals. False, the grid as it was, when out of memory. */
bool sh_heat_grid_resize(struct sh_heat_grid *grid, int columns, int rows);
void sh_heat_grid_clear(struct sh_heat_grid *grid);
void sh_heat_grid_free(struct sh_heat_grid *grid);

/* Adds `amount` to the cell at `column`, `row` (kept inside the grid), stopping at UINT32_MAX
 * rather than wrapping round. */
void sh_heat_add(struct sh_heat_grid *grid, enum sh_heat_kind kind, int column, int row,
                 uint32_t amount);
/* The largest count of `kind` in `grid`, 0 for an empty one. */
uint32_t sh_heat_most(const struct sh_heat_grid *grid, enum sh_heat_kind kind);

/* Blurs `values` (`columns` by `rows`, row by row) with a box `2 * radius + 1` cells across, each
 * way, three times over, which comes close to a gaussian; a cell averages only the cells inside
 * the grid, so that the edges do not cool. `scratch` holds as many floats as the longer side. */
void sh_heat_blur(float *values, int columns, int rows, int radius, float *scratch);
/* How far sh_heat_field blurs: SH_HEAT_BLUR cells each way, a spread of about 20 logical pixels. */
enum { SH_HEAT_BLUR = 2 };
/* The counts of `kind` as `grid`'s size of floats in `field`, blurred. `scratch` holds as many
 * floats as the grid's longer side. */
void sh_heat_field(const struct sh_heat_grid *grid, enum sh_heat_kind kind, float *field,
                   float *scratch);
/* The largest of `count` values, 0 for none. */
float sh_heat_peak(const float *values, size_t count);

/* Where `value` lies on the scale up to `most`: 0 for nothing, 1 at `most`, logarithmic over four
 * decades in between, so that a path the pointer only passed along shows beside a place it rested
 * at for minutes. */
double sh_heat_level(double value, double most);

/* The colour at `level` of the scale: clear at 0, then blue, cyan, green, yellow, red and white
 * at 1, the blue coming in from clear over the first fifth. Straight, each channel 0 to 1. */
struct sh_heat_color {
    double r, g, b, a;
};
struct sh_heat_color sh_heat_color(double level);
/* The same as a premultiplied ARGB pixel with `opacity` laid over its own alpha. */
uint32_t sh_heat_pixel(double level, double opacity);

/* Resamples `from` (`columns` by `rows`) into `to` (`to_columns` by `to_rows`), each axis on its
 * own: a new cell where the grid shrinks is the average of the old cells it covers, in proportion,
 * and where it grows lies on a line between the middles of the two nearest. `scratch` holds
 * `to_columns * rows` floats. */
void sh_heat_resample(const float *from, int columns, int rows, float *to, int to_columns,
                      int to_rows, float *scratch);

/* The overlay's picture of `field` (`columns` by `rows`), its hottest value `most`: a
 * premultiplied ARGB pixel a cell, at `opacity`, which the scene stretches over the output. */
void sh_heat_paint(const float *field, int columns, int rows, float most, double opacity,
                   uint32_t *pixels);

/* The legend's text: digits, '.', ' ' and the letters c, e, h, i, k, l, m, n, o, p, r, s and t,
 * in dots 7 high; anything else is left out. The width of `text` in dots, a dot between glyphs. */
int sh_heat_text_width(const char *text);
/* Paints `text` into `pixels` (premultiplied ARGB, `width` by `height`) with its top-left corner
 * at `x`, `y`, each dot `dot` pixels square, in `color`; what falls outside is cut. */
void sh_heat_text(uint32_t *pixels, int width, int height, int x, int y, int dot, const char *text,
                  uint32_t color);
/* How the legend writes `value` of `kind`: a time for motion, "1 click" or "N clicks". */
void sh_heat_label(char *text, size_t size, enum sh_heat_kind kind, double value);

/* The legend's size in logical pixels: a card naming the kind over the scale's colours, with 0 at
 * the cold end and the hottest cell's count at the hot one. */
enum { SH_HEAT_LEGEND_WIDTH = 232, SH_HEAT_LEGEND_HEIGHT = 76 };
/* Paints the legend for `kind` up to `most` at `scale` pixels per logical pixel into `pixels`
 * (premultiplied ARGB, the legend's size times `scale`). */
void sh_heat_legend(uint32_t *pixels, int scale, enum sh_heat_kind kind, double most);

#ifdef __cplusplus
}
#endif
