// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* Window controls for windows that ask the compositor to decorate them, in one of two styles:
 * three flat buttons (minimize, fullscreen, close from left to right) with thin glyphs on a dark
 * translucent strip over the window's top-right corner, or traffic lights, three coloured
 * circles (close, minimize, fullscreen) at its top-left that show their symbols while the
 * pointer is on one of them and turn grey while the window has no focus. */
#include <stdbool.h>
#include <stdint.h>

struct wlr_buffer;

enum sh_deco_part {
    SH_DECO_NONE,
    SH_DECO_CLOSE,
    SH_DECO_MINIMIZE,
    SH_DECO_FULLSCREEN,
};

enum sh_deco_style { SH_DECO_FLAT, SH_DECO_TRAFFIC_LIGHTS };

/* Logical size of the flat strip and its inset from the window's top-right corner. */
enum { SH_DECO_MARGIN = 6, SH_DECO_WIDTH = 96, SH_DECO_HEIGHT = 28 };

/* Traffic lights: circles SH_LIGHTS_DIAMETER across with their centres SH_LIGHTS_SPACING apart,
 * the first SH_LIGHTS_INSET from the window's left and top edges, drawn in an area of
 * SH_LIGHTS_WIDTH by SH_LIGHTS_HEIGHT at its top-left corner. */
enum {
    SH_LIGHTS_DIAMETER = 12,
    SH_LIGHTS_SPACING = 20,
    SH_LIGHTS_INSET = 14,
    SH_LIGHTS_WIDTH = 68,
    SH_LIGHTS_HEIGHT = 28,
};

/* What the controls show: their style, whether their window has focus, the part under the
 * pointer, and the part held down while the pointer is still on it (SH_DECO_NONE for none). */
struct sh_deco_look {
    enum sh_deco_style style;
    bool focused;
    enum sh_deco_part hovered, pressed;
};

/* The logical size of the controls in `style`. */
void sh_decoration_size(enum sh_deco_style style, int *width, int *height);

/* Paints the controls at `scale` pixels per logical pixel into `pixels`, premultiplied ARGB
 * of their size times `scale`. */
void sh_decoration_paint(uint32_t *pixels, int scale, const struct sh_deco_look *look);

/* The same in a new buffer. Returns NULL when out of memory. */
struct wlr_buffer *sh_decoration_render(int scale, const struct sh_deco_look *look);

/* The part at logical coordinates inside the controls of `style`, or SH_DECO_NONE. The flat
 * strip is shared out between its buttons; a traffic light takes the pointer within a couple
 * of pixels of its circle and halfway to the next. */
enum sh_deco_part sh_decoration_part_at(enum sh_deco_style style, double x, double y);

/* Wraps premultiplied ARGB pixels (malloc'd, `width` by `height`) in a buffer that frees them
 * with its last reference. Returns NULL, freeing them, when out of memory. */
struct wlr_buffer *sh_pixel_buffer(uint32_t *pixels, int width, int height);
