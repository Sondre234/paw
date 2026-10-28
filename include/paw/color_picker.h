// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* The colour picker's loupe, which the compositor shows as the cursor while a colour is picked
 * off the screen (src/compositor/color_picker.c): a crosshair over the pixel picked, and beside
 * it a card in the shell's colours on a soft shadow, holding the SH_LOUPE_CELLS by SH_LOUPE_CELLS
 * pixels around that one magnified, the middle one ringed, and under them a swatch of it and its
 * #rrggbb. Painted into premultiplied ARGB pixels at any scale, fractional ones too, in whole
 * pixels a cell. Also what the picker needs besides: a colour written as hex, and the colours of
 * an HDR monitor's frame taken back to sRGB. Nothing here touches wlroots. */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The pixels the loupe shows across and down, the one picked in the middle. */
enum { SH_LOUPE_CELLS = 11 };

/* What it is drawn in: the shell's surfaces, text and accent (0xRRGGBB), the text's opacity, and
 * whether the surfaces are light, which lightens the shadow. */
struct sh_loupe_look {
    uint32_t surface, text, accent;
    double text_alpha;
    bool light;
};

/* Where everything is in the loupe's picture, in its pixels, for a scale and the side of the
 * crosshair the card is on. The picture is `logical_width` by `logical_height` logical pixels,
 * which at that scale are its `width` by `height` exactly wherever the scale allows (a multiple
 * of 1/120 that does not need more than a few logical pixels to come out whole). The crosshair's
 * middle, the pixel picked, is at `hot_x`, `hot_y` logical pixels from the picture's top-left
 * corner, the pixel (`hot_pixel_x`, `hot_pixel_y`) of the picture, as wlroots places a cursor's
 * hotspot. */
struct sh_loupe_layout {
    double scale;
    bool left, above; /* the card left of the crosshair rather than right, above rather than below */
    int width, height;
    int logical_width, logical_height;
    int hot_x, hot_y, hot_pixel_x, hot_pixel_y;
    int card_x, card_y, card_width, card_height, card_radius;
    int grid_x, grid_y, cell, grid_radius;
    int swatch_x, swatch_y, swatch_size, swatch_radius;
    double text_x, text_top, text_cap;
    /* The card's own size in logical pixels, for placing it beside the crosshair on the screen. */
    double card_logical_width, card_logical_height;
};

/* How far the card stands off the crosshair, logical pixels, across and down. */
enum { SH_LOUPE_OFFSET = 22 };

/* The layout at `scale` (pixels per logical pixel) with the card on that side of the crosshair. */
void sh_loupe_layout(double scale, bool left, bool above, struct sh_loupe_layout *layout);

/* The card's logical size at `scale`, the same for every side, for choosing a side before a
 * layout is made. */
void sh_loupe_card_size(double scale, double *width, double *height);

/* Paints what stays the same as the pointer moves into `pixels` (the layout's size): the shadow,
 * the card and the crosshair, the rest clear. */
void sh_loupe_paint_frame(uint32_t *pixels, const struct sh_loupe_layout *layout,
                          const struct sh_loupe_look *look);

/* Paints the pixels around the one picked over a frame: `cells` holds SH_LOUPE_CELLS rows of
 * SH_LOUPE_CELLS colours, 0xFFRRGGBB, or 0 for one off every monitor, drawn as a checkerboard.
 * The middle cell is ringed in the accent, and the label under the grid shows its swatch and hex,
 * or nothing for one off every monitor. */
void sh_loupe_paint_cells(uint32_t *pixels, const struct sh_loupe_layout *layout,
                          const struct sh_loupe_look *look, const uint32_t *cells);

/* `rgb` (0xRRGGBB, anything above ignored) as "#rrggbb", lowercase. */
void sh_color_hex(uint32_t rgb, char text[8]);

/* A pixel of an HDR monitor's frame, its colours in BT.2020 encoded with PQ (as the scene draws
 * SDR white at 203 cd/m²), back in sRGB's primaries with the gamma 2.2 an SDR monitor's frames
 * are encoded with: what the same content shows as there. 0xRRGGBB in, 0xRRGGBB out. */
uint32_t sh_color_from_pq(uint32_t pixel);

/* The label's text: '#', the digits and the letters a to f, drawn in rounded strokes; anything
 * else is left out. The width of `text` with capitals `cap` pixels high. */
double sh_loupe_text_width(const char *text, double cap);
/* Paints `text` into `pixels` (premultiplied ARGB, `width` by `height`), its capitals `cap`
 * pixels high from `top`, starting at `x`, in `rgb` at `alpha`; what falls outside is cut. */
void sh_loupe_text(uint32_t *pixels, int width, int height, double x, double top, double cap,
                   const char *text, uint32_t rgb, double alpha);

#ifdef __cplusplus
}
#endif
