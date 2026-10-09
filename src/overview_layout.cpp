// SPDX-License-Identifier: GPL-3.0-or-later
#include "paw/overview.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

namespace {
struct Grid {
    int rows = 0, columns = 0;
    double score = -1;
};

double fit(const sh_rect &size, double cell_width, double cell_height, double max_scale) {
    if (size.width < 1 || size.height < 1)
        return 0;
    return std::min({cell_width / size.width, cell_height / size.height, max_scale});
}
} // namespace

extern "C" bool sh_overview_grid(const sh_rect *sizes, int count, sh_rect area, int gap,
                                 double max_scale, sh_rect *out) {
    if (!sizes || !out || count < 1 || area.width < 1 || area.height < 1 || gap < 0 ||
        !(max_scale > 0))
        return false;
    Grid best;
    for (int rows = 1; rows <= count; ++rows) {
        int columns = (count + rows - 1) / rows;
        if ((rows - 1) * columns >= count)
            continue; // the last row would be empty
        double cell_width = static_cast<double>(area.width - gap * (columns - 1)) / columns;
        double cell_height = static_cast<double>(area.height - gap * (rows - 1)) / rows;
        if (cell_width < 1 || cell_height < 1)
            continue;
        double score = 0;
        for (int i = 0; i < count; ++i) {
            double scale = fit(sizes[i], cell_width, cell_height, max_scale);
            score += scale * scale * std::max(1, sizes[i].width) * std::max(1, sizes[i].height);
        }
        if (score > best.score * 1.0000001)
            best = {rows, columns, score};
    }
    if (!best.rows) {
        // Too little room for the gaps: shrink them away.
        return gap > 0 && sh_overview_grid(sizes, count, area, 0, max_scale, out);
    }
    double cell_width = static_cast<double>(area.width - gap * (best.columns - 1)) / best.columns;
    double cell_height = static_cast<double>(area.height - gap * (best.rows - 1)) / best.rows;
    for (int i = 0; i < count; ++i) {
        int row = i / best.columns, column = i % best.columns;
        int in_row = std::min(best.columns, count - row * best.columns);
        double scale = fit(sizes[i], cell_width, cell_height, max_scale);
        // Rounding must not take a thumbnail past its cell, or neighbours would touch.
        int width = std::max(1, std::min(static_cast<int>(std::lround(sizes[i].width * scale)),
                                         static_cast<int>(cell_width)));
        int height = std::max(1, std::min(static_cast<int>(std::lround(sizes[i].height * scale)),
                                          static_cast<int>(cell_height)));
        double row_width = in_row * cell_width + (in_row - 1) * gap;
        double left = area.x + (area.width - row_width) / 2 + column * (cell_width + gap);
        double top = area.y + row * (cell_height + gap);
        out[i] = {static_cast<int>(std::lround(left + (cell_width - width) / 2)),
                  static_cast<int>(std::lround(top + (cell_height - height) / 2)), width, height};
    }
    // Center the grid vertically when it is shorter than the area.
    int lowest = std::numeric_limits<int>::min(), highest = std::numeric_limits<int>::max();
    for (int i = 0; i < count; ++i) {
        highest = std::min(highest, out[i].y);
        lowest = std::max(lowest, out[i].y + out[i].height);
    }
    int shift = area.y + (area.height - (lowest - highest)) / 2 - highest;
    for (int i = 0; i < count; ++i)
        out[i].y += shift;
    return true;
}

extern "C" int sh_overview_neighbour(const sh_rect *rects, int count, int from,
                                     sh_overview_direction direction) {
    if (!rects || from < 0 || from >= count)
        return from;
    auto cx = [&](int i) { return rects[i].x + rects[i].width / 2.0; };
    auto cy = [&](int i) { return rects[i].y + rects[i].height / 2.0; };
    bool horizontal = direction == SH_OVERVIEW_LEFT || direction == SH_OVERVIEW_RIGHT;
    double sign = direction == SH_OVERVIEW_LEFT || direction == SH_OVERVIEW_UP ? -1 : 1;
    int best = -1;
    double best_score = std::numeric_limits<double>::max();
    for (int i = 0; i < count; ++i) {
        if (i == from)
            continue;
        double along = ((horizontal ? cx(i) - cx(from) : cy(i) - cy(from))) * sign;
        double across = std::abs(horizontal ? cy(i) - cy(from) : cx(i) - cx(from));
        if (along <= 0)
            continue;
        // A window in another row counts as farther than any in this one.
        double score = along + 3 * across;
        if (score < best_score) {
            best = i;
            best_score = score;
        }
    }
    if (best >= 0)
        return best;
    if (horizontal && count > 1)
        return (from + (sign < 0 ? count - 1 : 1)) % count;
    return from;
}

extern "C" bool sh_overview_strip(int count, sh_rect area, int gap, double aspect, int max_height,
                                  sh_rect *out) {
    if (!out || count < 1 || area.width < 1 || area.height < 1 || gap < 0 || !(aspect > 0))
        return false;
    double width = static_cast<double>(area.width - gap * (count - 1)) / count;
    double height = std::min({width / aspect, static_cast<double>(area.height),
                              max_height > 0 ? static_cast<double>(max_height) : 1e9});
    width = height * aspect;
    if (width < 1 || height < 1)
        return false;
    double total = count * width + (count - 1) * gap;
    double left = area.x + (area.width - total) / 2;
    int top = area.y + static_cast<int>(std::lround((area.height - height) / 2));
    for (int i = 0; i < count; ++i) {
        int x0 = static_cast<int>(std::lround(left + i * (width + gap)));
        int x1 = static_cast<int>(std::lround(left + i * (width + gap) + width));
        out[i] = {x0, top, std::max(1, x1 - x0), std::max(1, static_cast<int>(std::lround(height)))};
    }
    return true;
}

extern "C" bool sh_overview_matches(const char *haystack, const char *text) {
    if (!text || !*text)
        return true;
    if (!haystack)
        haystack = "";
    std::string lower(haystack);
    for (char &c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const char *p = text;
    while (*p) {
        while (*p && std::isspace(static_cast<unsigned char>(*p)))
            ++p;
        const char *start = p;
        while (*p && !std::isspace(static_cast<unsigned char>(*p)))
            ++p;
        if (p == start)
            break;
        std::string word(start, p);
        for (char &c : word)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower.find(word) == std::string::npos)
            return false;
    }
    return true;
}
