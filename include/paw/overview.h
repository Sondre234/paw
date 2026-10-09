// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* Layout for the overview (Expose): where each window's thumbnail goes, which one lies in a
 * direction from another, and how the workspace strip is spread along the top. Pure geometry,
 * so it is tested without a compositor. */
#include "paw/backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Places `count` windows, of the sizes in `sizes` (only width and height are read), in `area`
 * as a grid of equal cells `gap` pixels apart. The number of rows is the one whose thumbnails
 * cover the most area. Each thumbnail keeps its window's aspect ratio, is centered in its cell,
 * is never larger than `max_scale` times its window, and the last row is centered. Rectangles
 * go to `out`, in the order of `sizes`. Returns false for an empty area or no windows. */
bool sh_overview_grid(const struct sh_rect *sizes, int count, struct sh_rect area, int gap,
                      double max_scale, struct sh_rect *out);

enum sh_overview_direction {
    SH_OVERVIEW_LEFT,
    SH_OVERVIEW_RIGHT,
    SH_OVERVIEW_UP,
    SH_OVERVIEW_DOWN,
};
/* The rectangle nearest `from` in a direction, preferring the ones level with it; `from` itself
 * when none lies that way. Left and right wrap to the previous and next in order past the ends
 * of a row. */
int sh_overview_neighbour(const struct sh_rect *rects, int count, int from,
                          enum sh_overview_direction direction);

/* Spreads `count` workspace cells, each of `aspect` (width over height) and at most `area`
 * high, along `area`, centered. Cells shrink to fit and never grow past `max_height`. */
bool sh_overview_strip(int count, struct sh_rect area, int gap, double aspect, int max_height,
                       struct sh_rect *out);

/* Whether `text` (as typed) is contained in `haystack`, ignoring case, matching each of its
 * blank-separated words on their own: "fire fox" finds "Firefox - fire". An empty `text`
 * matches everything. */
bool sh_overview_matches(const char *haystack, const char *text);

#ifdef __cplusplus
}
#endif
