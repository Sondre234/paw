// SPDX-License-Identifier: GPL-3.0-or-later
// The taskbar resized by each of its edges: the inner one makes it taller, the outer one and the
// ends move it in from the output's edges, each within its setting's range.
#include "bar_resize.hpp"
#include <QTest>

using namespace bar_resize;
using paw::PanelEdge;

namespace {
const std::pair<int, int> heights{24, 100}, margins{0, 200};
// A bar 46 tall, 10 above the bottom and 16 in from either side (top, right, bottom, left).
const Size floating{46, {0, 16, 10, 16}};
} // namespace

class BarResizeTest : public QObject {
    Q_OBJECT
  private Q_SLOTS:
    void measuresFromTheBarsEdgeOrTheEnds() {
        QCOMPARE(inner(PanelEdge::Bottom), PanelEdge::Top);
        QCOMPARE(inner(PanelEdge::Left), PanelEdge::Right);
        // Along the bottom: the inner (top) and outer (bottom) edges from the bottom, the ends
        // from the sides beyond them.
        QCOMPARE(measuredFrom(PanelEdge::Bottom, PanelEdge::Top), PanelEdge::Bottom);
        QCOMPARE(measuredFrom(PanelEdge::Bottom, PanelEdge::Bottom), PanelEdge::Bottom);
        QCOMPARE(measuredFrom(PanelEdge::Bottom, PanelEdge::Left), PanelEdge::Left);
        QCOMPARE(measuredFrom(PanelEdge::Bottom, PanelEdge::Right), PanelEdge::Right);
        // Down the left side: the ends are the top and the bottom.
        QCOMPARE(measuredFrom(PanelEdge::Left, PanelEdge::Right), PanelEdge::Left);
        QCOMPARE(measuredFrom(PanelEdge::Left, PanelEdge::Top), PanelEdge::Top);
        QCOMPARE(marginIndex(PanelEdge::Top), 0);
        QCOMPARE(marginIndex(PanelEdge::Right), 1);
        QCOMPARE(marginIndex(PanelEdge::Bottom), 2);
        QCOMPARE(marginIndex(PanelEdge::Left), 3);
    }
    void innerEdgeSetsTheHeight() {
        QCOMPARE(resized(floating, PanelEdge::Bottom, PanelEdge::Top, 20, heights, margins),
                 (Size{66, {0, 16, 10, 16}}));
        QCOMPARE(resized(floating, PanelEdge::Bottom, PanelEdge::Top, -40, heights, margins).height, 24);
        QCOMPARE(resized(floating, PanelEdge::Bottom, PanelEdge::Top, 400, heights, margins).height, 100);
        // Down a side, its width.
        QCOMPARE(resized(floating, PanelEdge::Left, PanelEdge::Right, 4, heights, margins),
                 (Size{50, {0, 16, 10, 16}}));
    }
    void endsSetTheMarginBeyondThem() {
        // The left end dragged 30 in, the right end 10 out.
        QCOMPARE(resized(floating, PanelEdge::Bottom, PanelEdge::Left, 30, heights, margins),
                 (Size{46, {0, 16, 10, 46}}));
        QCOMPARE(resized(floating, PanelEdge::Bottom, PanelEdge::Right, -10, heights, margins),
                 (Size{46, {0, 6, 10, 16}}));
        // Out past the output's edge it stops there, and no further in than 200.
        QCOMPARE(resized(floating, PanelEdge::Bottom, PanelEdge::Left, -50, heights, margins).margin[3], 0);
        QCOMPARE(resized(floating, PanelEdge::Bottom, PanelEdge::Left, 500, heights, margins).margin[3], 200);
        // Down a side, the top end.
        QCOMPARE(resized(floating, PanelEdge::Left, PanelEdge::Top, 12, heights, margins),
                 (Size{46, {12, 16, 10, 16}}));
    }
    void outerEdgeTradesTheMarginForTheHeight() {
        // Dragged down 6, into the margin below: the bar 6 taller, its top where it was.
        QCOMPARE(resized(floating, PanelEdge::Bottom, PanelEdge::Bottom, -6, heights, margins),
                 (Size{52, {0, 16, 4, 16}}));
        // Up 12, off the edge: 12 shorter.
        QCOMPARE(resized(floating, PanelEdge::Bottom, PanelEdge::Bottom, 12, heights, margins),
                 (Size{34, {0, 16, 22, 16}}));
        // Down past the output's edge, it stops there; up, at the shortest height.
        QCOMPARE(resized(floating, PanelEdge::Bottom, PanelEdge::Bottom, -30, heights, margins),
                 (Size{56, {0, 16, 0, 16}}));
        QCOMPARE(resized(floating, PanelEdge::Bottom, PanelEdge::Bottom, 300, heights, margins),
                 (Size{24, {0, 16, 32, 16}}));
        // At its tallest it goes no further down, whatever margin is left.
        QCOMPARE(resized(Size{96, {0, 0, 50, 0}}, PanelEdge::Bottom, PanelEdge::Bottom, -30, heights,
                         margins),
                 (Size{100, {0, 0, 46, 0}}));
        // Along the top, its outer edge is the top.
        QCOMPARE(resized(Size{40, {8, 0, 0, 0}}, PanelEdge::Top, PanelEdge::Top, 10, heights, margins),
                 (Size{30, {18, 0, 0, 0}}));
    }
};

QTEST_GUILESS_MAIN(BarResizeTest)
#include "bar_resize_test.moc"
