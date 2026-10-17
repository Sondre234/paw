// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "paw/config.hpp"
#include <array>
#include <utility>

// The arithmetic of the taskbar resized by one of its edges (shell.panel_draggable). The bar is
// along `edge` of its output, and each of its own edges faces one of the output's, its `side`: the
// inner edge the one opposite `edge`, the outer edge `edge` itself, an end the edge beyond it. The
// compositor tells how far the pointer is from an edge of the output (panel_drag resize EDGE), and
// a drag's travel is how much that has grown since the press.
namespace bar_resize {
// The bar's height across it (shell.panel_height) and the margins around it (shell.panel_margin:
// top, right, bottom, left).
struct Size {
    int height = 0;
    std::array<int, 4> margin{};
    bool operator==(const Size &) const = default;
};
// The side the inner edge of a bar along `edge` faces: the opposite edge.
paw::PanelEdge inner(paw::PanelEdge edge);
// shell.panel_margin's index of the margin on `side`.
int marginIndex(paw::PanelEdge side);
// The edge of the output a drag of the bar's `side` is told from: the inner and the outer edge's
// from `edge`, an end's from `side`.
paw::PanelEdge measuredFrom(paw::PanelEdge edge, paw::PanelEdge side);
// `start` with the bar's `side` dragged `travel` pixels away from measuredFrom's edge: the inner
// edge makes the bar taller; the outer edge widens the margin on its side and makes the bar
// shorter by as much, the inner edge staying where it is; an end widens the margin beyond it. The
// height stays within `heights` and a margin within `margins`, the outer edge stopping where
// either would go past.
Size resized(const Size &start, paw::PanelEdge edge, paw::PanelEdge side, int travel,
             std::pair<int, int> heights, std::pair<int, int> margins);
} // namespace bar_resize
