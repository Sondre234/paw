// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* A window's border in a gradient: the colour at each place of the frame, and the small pieces
 * the compositor draws it in. A strip along each side holds a sample per pixel along it (at most
 * SH_BORDER_SAMPLES) and one across, which the scene stretches over the side, as a thin border
 * hardly changes across its width; a square at each corner is painted pixel by pixel, rounded as
 * the window's corners are. Only arithmetic and pixels; the compositor makes them buffers. */
#include <stdint.h>

#include "paw/backend.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { SH_BORDER_PIECES = 8, SH_BORDER_SAMPLES = 512 };

/* How far along a gradient at `angle` degrees the point (u, v) of a box is, both from 0 at its
 * left or top edge to 1 at its right or bottom one: from 0 where the gradient starts to 1 where
 * it ends. 0 degrees runs from the left edge to the right one, 90 from the top to the bottom,
 * 45 from the top-left corner to the bottom-right one, whatever the box's proportions. */
double sh_gradient_progress(double angle, double u, double v);

/* The colour of `gradient` `t` of the way along it (premultiplied RGBA), between the two stops
 * around it; t is kept between 0 and 1. */
void sh_gradient_color(const struct sh_gradient *gradient, double t, float color[4]);

/* Whether a border in these colours needs pieces: a gradient, rather than one colour. */
bool sh_gradient_varies(const struct sh_gradient *gradient);

/* A piece of a border: where it is drawn, in logical pixels from the top-left corner of the
 * window's geometry, and the size of its buffer in pixels. A piece with nothing to draw has no
 * width. */
struct sh_border_piece {
    int x, y, width, height;
    int buffer_width, buffer_height;
    int corner; /* -1 for a side; 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right */
};

/* The pieces of a border `border` pixels wide around a window of `width` by `height` whose
 * corners are rounded with `radius` (0 for square ones; at most half the window's shorter side),
 * at `scale` pixels per logical pixel: the strips along the top, bottom, left and right sides
 * between the corners, then the corners, each `border` + `radius` square. */
void sh_border_pieces(int width, int height, int border, int radius, int scale,
                      struct sh_border_piece pieces[SH_BORDER_PIECES]);

/* Paints `piece` of that border (premultiplied ARGB, its buffer's size, rows without padding):
 * `from` crossfading into `to` by `mix` (0 is `from` alone, 1 `to` alone), each gradient laid
 * over the border's whole box, the window and its border. A rounded corner follows the window's
 * corner inside and its own outside, with smooth edges, and is clear inside the window. */
void sh_border_paint(uint32_t *pixels, const struct sh_border_piece *piece, int width, int height,
                     int border, int radius, int scale, const struct sh_gradient *from,
                     const struct sh_gradient *to, double mix);

#ifdef __cplusplus
}
#endif
