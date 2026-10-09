// SPDX-License-Identifier: GPL-3.0-or-later
#include "paw/tabs.h"

void sh_tabs_span(int width, int count, int index, int *x, int *length) {
    if (count < 1)
        count = 1;
    int usable = width - SH_TABS_GAP * (count - 1);
    if (usable < count)
        usable = count;
    int start = (int)((long long)usable * index / count) + SH_TABS_GAP * index;
    int end = (int)((long long)usable * (index + 1) / count) + SH_TABS_GAP * index;
    *x = start;
    *length = end - start;
}

int sh_tabs_index_at(int width, int count, double x) {
    if (x < 0 || x >= width || count < 1)
        return -1;
    for (int i = count - 1; i >= 0; --i) {
        int start, length;
        sh_tabs_span(width, count, i, &start, &length);
        if (x >= start)
            return i;
    }
    return -1;
}

static uint32_t premultiply(uint32_t rgb, int alpha) {
    uint32_t r = ((rgb >> 16) & 0xff) * alpha / 255, g = ((rgb >> 8) & 0xff) * alpha / 255,
             b = (rgb & 0xff) * alpha / 255;
    return (uint32_t)alpha << 24 | r << 16 | g << 8 | b;
}

void sh_tabs_paint(uint32_t *pixels, int width, int scale, int count, int active, int hovered) {
    if (scale < 1)
        scale = 1;
    int w = width * scale, h = SH_TABS_HEIGHT * scale;
    for (int i = 0; i < w * h; ++i)
        pixels[i] = 0;
    // Rounded ends of a segment, a pixel or two of radius as the strip is thin.
    int radius = 2 * scale;
    for (int tab = 0; tab < count; ++tab) {
        int start, length;
        sh_tabs_span(width, count, tab, &start, &length);
        int left = start * scale, right = (start + length) * scale;
        uint32_t color = tab == active    ? premultiply(0x7aa8ff, 255)
                         : tab == hovered ? premultiply(0x9aa3b5, 230)
                                          : premultiply(0x4a5060, 210);
        for (int y = 0; y < h; ++y) {
            for (int x = left; x < right && x < w; ++x) {
                int dx = x < left + radius ? left + radius - x : x >= right - radius ? x - (right - radius - 1) : 0;
                int dy = y < radius ? radius - y : y >= h - radius ? y - (h - radius - 1) : 0;
                if (dx * dx + dy * dy > radius * radius)
                    continue;
                pixels[y * w + x] = color;
            }
        }
    }
}
