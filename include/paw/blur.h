// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* The blur behind windows (windows.blur), without wlroots: an output's background, captured
 * small, blurred on the CPU into the picture windows show under them, and the part of that
 * picture a window covers on the screen. */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* How a picture is blurred and coloured. */
struct sh_blur_look {
    double sigma;     /* the gaussian's standard deviation, in the picture's pixels */
    float noise;      /* grain: each pixel strays by up to half this share of full brightness */
    float brightness; /* the colours scaled by this */
    float saturation; /* 1 as they are, 0 grey, above 1 more vivid */
};

/* The blur is done on a grid this many times halved from the picture, in each direction, so
 * that the gaussian still spans SH_BLUR_GRID_SIGMA of its cells (or the picture's own pixels,
 * for a small one); a grid of averages, sampled back up smoothly, which the blur leaves no
 * trace of. Up to 6 halvings. */
#define SH_BLUR_GRID_SIGMA 3.0
int sh_blur_halvings(double sigma);

/* The widths (odd) of three box blurs in a row that blur as a gaussian of `sigma` does, all 1
 * for none. */
void sh_blur_boxes(double sigma, int widths[3]);

/* Paints into `out` the picture `in`, both `width` by `height` pixels of 0xXXRRGGBB in rows
 * without padding: blurred as `look` says, its colours saturated and brightened, grain added
 * (the same at the same pixel every time, so that painting again does not make it flicker),
 * opaque. The edges blur as if their pixels went on. False when out of memory, `out` as it
 * was. */
bool sh_blur_paint(const uint32_t *in, int width, int height, const struct sh_blur_look *look,
                   uint32_t *out);

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
