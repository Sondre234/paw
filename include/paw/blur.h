// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* The blur behind windows (windows.blur), without wlroots: an output's background, drawn small
 * on the renderer, blurred on the CPU as a grid of few cells that the renderer stretches back
 * over the picture windows show under them, with grain; and the part of that picture a window
 * covers on the screen. */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* How a grid is blurred and coloured. */
struct sh_blur_look {
    double sigma;     /* the gaussian's standard deviation, in the grid's cells */
    float noise;      /* grain: each pixel strays by up to half this share of full brightness */
    float brightness; /* the colours scaled by this */
    float saturation; /* 1 as they are, 0 grey, above 1 more vivid */
};

/* How many times a picture is halved, each way, into the grid that is blurred: as often as the
 * gaussian of `sigma` of its pixels still spans SH_BLUR_GRID_SIGMA of the grid's cells, up to 6
 * times. A gaussian that wide keeps less than a hundredth of what changes from one cell to the
 * next, so that the grid stretched back smoothly shows no trace of its cells. */
#define SH_BLUR_GRID_SIGMA 1.5
int sh_blur_halvings(double sigma);

/* The widths (odd) of three box blurs in a row that blur as a gaussian of `sigma` does, all 1
 * for none. */
void sh_blur_boxes(double sigma, int widths[3]);

/* Blurs `grid`, `columns` by `rows` pixels of 0xXXRRGGBB in rows without padding, in place as
 * `look` says, its edges as if their cells went on; saturates and brightens its colours; and
 * darkens it by half the grain sh_blur_grain adds back, so that it is as light on the whole.
 * Opaque. False when out of memory, the grid as it was. */
bool sh_blur_grid(uint32_t *grid, int columns, int rows, const struct sh_blur_look *look);

/* A tile of grain to lay over a picture again and again, `size` by `size` premultiplied ARGB
 * pixels with no alpha, which adds them to what is under them: grey of up to `noise` of full
 * brightness, the same at the same place every time, so that a picture made again does not
 * flicker. */
void sh_blur_grain(uint32_t *tile, int size, float noise);

struct sh_blur_box {
    int x, y, width, height;
};
struct sh_blur_area {
    double x, y, width, height;
};

/* Where a window drawn at `window` (layout coordinates) shows a picture of `width` by `height`
 * pixels of an output's box `output`: the part of the window inside that box, in `part`, and
 * where that part lies in the picture, in `source`, never past its edges. False when no part
 * of the window is inside it. */
bool sh_blur_source(struct sh_blur_box window, struct sh_blur_box output, int width, int height,
                    struct sh_blur_box *part, struct sh_blur_area *source);

#ifdef __cplusplus
}
#endif
