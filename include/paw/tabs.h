// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* The tab strip of a window group: one segment per member across the top of the group's window,
 * the visible member's lit. Only arithmetic and pixels; the compositor turns them into a buffer. */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { SH_TABS_HEIGHT = 8, SH_TABS_GAP = 3 };

/* The segment of tab `index` of `count` across `width` logical pixels: where it starts and how
 * wide it is. Segments cover the width with SH_TABS_GAP between them. */
void sh_tabs_span(int width, int count, int index, int *x, int *length);
/* The tab at logical `x`, or -1 outside the strip (gaps belong to the tab before them). */
int sh_tabs_index_at(int width, int count, double x);
/* Fills `pixels` (premultiplied ARGB, width * scale by SH_TABS_HEIGHT * scale) with the strip;
 * `active` is lit and `hovered` (-1 for none) brightened. */
void sh_tabs_paint(uint32_t *pixels, int width, int scale, int count, int active, int hovered);

#ifdef __cplusplus
}
#endif
