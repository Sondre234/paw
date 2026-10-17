// SPDX-License-Identifier: GPL-3.0-or-later
#include "bar_resize.hpp"
#include <algorithm>

namespace bar_resize {
using paw::PanelEdge;

PanelEdge inner(PanelEdge edge) {
    switch (edge) {
    case PanelEdge::Top: return PanelEdge::Bottom;
    case PanelEdge::Bottom: return PanelEdge::Top;
    case PanelEdge::Left: return PanelEdge::Right;
    case PanelEdge::Right: return PanelEdge::Left;
    }
    return PanelEdge::Top;
}

int marginIndex(PanelEdge side) {
    switch (side) {
    case PanelEdge::Top: return 0;
    case PanelEdge::Right: return 1;
    case PanelEdge::Bottom: return 2;
    case PanelEdge::Left: return 3;
    }
    return 0;
}

PanelEdge measuredFrom(PanelEdge edge, PanelEdge side) {
    return side == inner(edge) ? edge : side;
}

Size resized(const Size &start, PanelEdge edge, PanelEdge side, int travel,
             std::pair<int, int> heights, std::pair<int, int> margins) {
    Size size = start;
    if (side == inner(edge)) {
        size.height = std::clamp(start.height + travel, heights.first, heights.second);
        return size;
    }
    const int index = marginIndex(side), from = start.margin[index];
    if (side != edge) {
        size.margin[index] = std::clamp(from + travel, margins.first, margins.second);
        return size;
    }
    // The outer edge: the margin takes what the height gives, and stops where either would leave
    // its range (the bounds kept in order for a bar outside one, which the configuration never has).
    const int least = std::max(margins.first - from, start.height - heights.second);
    const int most = std::min(margins.second - from, start.height - heights.first);
    travel = std::clamp(travel, std::min(least, 0), std::max(most, 0));
    size.margin[index] = from + travel;
    size.height = start.height - travel;
    return size;
}
} // namespace bar_resize
