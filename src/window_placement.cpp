// SPDX-License-Identifier: GPL-3.0-or-later
// Where new floating windows open: the policy behind windows.placement.
#include "paw/backend.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <tuple>
#include <vector>

namespace {
// The windows that can matter: the newest ones, which is what the caller lists first.
constexpr int max_obstacles = 32;

int64_t overlap(const sh_rect &a, const sh_rect &b) {
    int64_t w = std::min(a.x + a.width, b.x + b.width) - std::max(a.x, b.x);
    int64_t h = std::min(a.y + a.height, b.y + b.height) - std::max(a.y, b.y);
    return w > 0 && h > 0 ? w * h : 0;
}
// The gap between two rectangles that do not overlap: how far apart they are along the axis
// that separates them (the larger, when both do).
int64_t gap(const sh_rect &a, const sh_rect &b) {
    int64_t dx = std::max<int64_t>({0, b.x - (a.x + a.width), a.x - (b.x + b.width)});
    int64_t dy = std::max<int64_t>({0, b.y - (a.y + a.height), a.y - (b.y + b.height)});
    return std::max(dx, dy);
}
// Coordinates along one axis worth trying for a window `size` long inside [low, high]: flush
// against the area's ends and against every other window's edges, and centered in each gap
// between edges.
std::vector<int> candidates(int low, int high, int size, const std::vector<int> &edges) {
    std::vector<int> cuts{low, high};
    for (int edge : edges)
        cuts.push_back(std::clamp(edge, low, high));
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());
    std::vector<int> result;
    auto add = [&](int position) {
        result.push_back(std::clamp(position, low, std::max(low, high - size)));
    };
    for (std::size_t i = 0; i < cuts.size(); ++i) {
        add(cuts[i]);
        add(cuts[i] - size);
        if (i + 1 < cuts.size() && cuts[i + 1] - cuts[i] >= size)
            add((cuts[i] + cuts[i + 1] - size) / 2);
    }
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}
} // namespace

extern "C" bool sh_place_window(sh_place_mode mode, sh_rect area, const sh_rect *others,
                                int other_count, int width, int height, int index,
                                sh_rect *result) {
    if (!result || area.width < 1 || area.height < 1 || width < 1 || height < 1 || index < 0 ||
        other_count < 0 || (other_count > 0 && !others))
        return false;
    *result = {area.x, area.y, width, height};
    if (mode == SH_PLACE_CENTER || mode == SH_PLACE_SMART) {
        result->x = area.x + std::max(0, (area.width - width) / 2);
        result->y = area.y + std::max(0, (area.height - height) / 2);
    }
    if (mode == SH_PLACE_SMART && other_count > 0) {
        std::vector<sh_rect> obstacles;
        std::vector<int> xs, ys;
        for (int i = 0; i < other_count && static_cast<int>(obstacles.size()) < max_obstacles; ++i) {
            if (overlap(area, others[i]) == 0)
                continue;
            obstacles.push_back(others[i]);
            xs.push_back(others[i].x);
            xs.push_back(others[i].x + others[i].width);
            ys.push_back(others[i].y);
            ys.push_back(others[i].y + others[i].height);
        }
        // Least overlap first, then the most room around it, then the nearest to the middle.
        using Score = std::tuple<int64_t, int64_t, int64_t>;
        Score best{INT64_MAX, 0, 0};
        sh_rect found = *result;
        const int64_t cx = area.x + area.width / 2, cy = area.y + area.height / 2;
        for (int y : candidates(area.y, area.y + area.height, height, ys)) {
            for (int x : candidates(area.x, area.x + area.width, width, xs)) {
                sh_rect rect{x, y, width, height};
                int64_t covered = 0;
                for (const auto &other : obstacles)
                    covered += overlap(rect, other);
                if (covered > std::get<0>(best))
                    continue;
                int64_t room = std::min({static_cast<int64_t>(x - area.x),
                                         static_cast<int64_t>(area.x + area.width - (x + width)),
                                         static_cast<int64_t>(y - area.y),
                                         static_cast<int64_t>(area.y + area.height - (y + height))});
                if (covered == 0)
                    for (const auto &other : obstacles)
                        room = std::min(room, gap(rect, other));
                int64_t dx = x + width / 2 - cx, dy = y + height / 2 - cy;
                Score score{covered, -room, dx * dx + dy * dy};
                if (score < best) {
                    best = score;
                    found = rect;
                }
            }
        }
        if (std::get<0>(best) == 0) {
            *result = found;
            return true;
        }
        mode = SH_PLACE_CASCADE; // nowhere is free: step down and right from the last window
    }
    if (mode == SH_PLACE_CASCADE) {
        // Kept inside a small output, and from the very corner of a big one.
        const int margin = area.width < 80 || area.height < 80 ? 0 : 40;
        const int offset = 40 + 32 * (index % 8);
        result->x = area.x + (offset + width <= area.width ? offset : margin);
        result->y = area.y + (offset + height <= area.height ? offset : margin);
    }
    return true;
}
