// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* A window's drop shadow: the window's shape blurred, offset and tinted, with the window itself
 * cut out so that a translucent window does not darken itself. The compositor draws it in
 * slices of one image, painted for a window just large enough to hold its corners: the corners
 * as they are and the edges stretched, so that a window of any size shows it without blurring
 * again. Only arithmetic and pixels; the compositor turns them into a buffer and scene nodes. */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct sh_shadow {
    int radius;             /* the window's corner radius, in logical pixels */
    int blur;               /* how soft the edge is, as CSS's blur radius: twice its sigma */
    int offset_x, offset_y; /* where the shadow falls, right and down from the window */
    float color[4];         /* premultiplied RGBA where it is darkest */
};

/* Where the shadow is around a window, in logical pixels: it reaches `left`, `top`, `right` and
 * `bottom` past the window's edges, and its corners (with the window's own) reach the `inner_`
 * distances into the window from them. An image painted for a window of `width` by `height`,
 * just more than the corners need, serves every window at least as large. */
struct sh_shadow_layout {
    int left, top, right, bottom;
    int inner_left, inner_top, inner_right, inner_bottom;
    int width, height;
};
void sh_shadow_layout(const struct sh_shadow *shadow, struct sh_shadow_layout *layout);

/* The window size to paint an image for, to draw the shadow of a window of `width` by `height`:
 * the layout's, but the window's own along a side too short for both corners (that image then
 * serves only windows of that length there). */
void sh_shadow_image_size(const struct sh_shadow_layout *layout, int width, int height,
                          int *image_width, int *image_height);

/* Paints the shadow of a window of `width` by `height` logical pixels at `scale` pixels per
 * logical pixel into `pixels`: premultiplied ARGB, (left + width + right) * scale wide and
 * (top + height + bottom) * scale high by the layout. False when out of memory. */
bool sh_shadow_paint(uint32_t *pixels, const struct sh_shadow *shadow, int width, int height,
                     int scale);

/* A part of the image (logical pixels from its top-left corner) and where it is drawn, from the
 * window's top-left corner, stretched to that size. */
struct sh_shadow_slice {
    int x, y, width, height;
    int to_x, to_y, to_width, to_height;
};
enum { SH_SHADOW_SLICES = 8 };

/* The slices that draw the shadow of a window of `width` by `height` from the image painted for
 * a window of `image_width` by `image_height` (sh_shadow_image_size's): the corners, top-left,
 * top-right, bottom-left and bottom-right, then the top, bottom, left and right edges. A slice
 * that is not needed has no size. */
void sh_shadow_slices(const struct sh_shadow_layout *layout, int image_width, int image_height,
                      int width, int height, struct sh_shadow_slice slices[SH_SHADOW_SLICES]);
/* As sh_shadow_slices, with the shadow reaching further on each side, `scale` (left, top,
 * right, bottom; 1 or more) times as far from the window's edge there: each corner is drawn
 * that much larger about the window's corner, the edges that much thicker. A blurred edge
 * stretched so is as good as one blurred that much more, and a hard one moves that much
 * further off, so a shadow grows softer and falls further with nothing painted again. A window
 * too small for its corners at that scale has them as large as it holds. */
void sh_shadow_slices_scaled(const struct sh_shadow_layout *layout, int image_width,
                             int image_height, int width, int height, const double scale[4],
                             struct sh_shadow_slice slices[SH_SHADOW_SLICES]);

/* Whether a shadow is printed beside the window rather than cast under it: hard, with little or
 * no blur, as an offset print is. */
bool sh_shadow_printed(const struct sh_shadow *shadow);
/* The shadow of a window lifted off the screen as it is dragged, `progress` of the way (0 at
 * rest, 1 lifted), from its resting one. A cast shadow is drawn from the resting one's image,
 * stretched away from the window `scale` times as far on each side (left, top, right, bottom;
 * sh_shadow_slices_scaled): half as soft again, further the way it falls, its opacity times
 * `opacity`, a little lighter. A printed one is painted two fifths further off instead, in whole
 * pixels (`lifted`), as crisp and as dark: stretched, its corners would round. */
void sh_shadow_lift(const struct sh_shadow *shadow, double progress, struct sh_shadow *lifted,
                    double scale[4], float *opacity);

#ifdef __cplusplus
}
#endif
