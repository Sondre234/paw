// SPDX-License-Identifier: GPL-3.0-or-later
// The desktop icons' grid: icons laid down the columns, remembered cells kept, and where icons
// dragged together land.
#include "desktop_grid.hpp"
#include <QTest>

using namespace desktop_grid;

class DesktopGridTest : public QObject {
    Q_OBJECT
  private Q_SLOTS:
    void fillsDownTheColumns() {
        const QStringList keys{"a", "b", "c", "d", "e"};
        QCOMPARE(arrange(keys, {}, QSize(3, 2)),
                 (QList<QPoint>{{0, 0}, {0, 1}, {1, 0}, {1, 1}, {2, 0}}));
        // Past the grid's last cell, on down columns beyond it.
        QCOMPARE(arrange(keys, {}, QSize(2, 2)),
                 (QList<QPoint>{{0, 0}, {0, 1}, {1, 0}, {1, 1}, {2, 0}}));
        QCOMPARE(arrange({}, {}, QSize(4, 4)), QList<QPoint>{});
        // A grid without rows still gives each a cell of its own.
        QCOMPARE(arrange({"a", "b"}, {}, QSize(0, 0)), (QList<QPoint>{{0, 0}, {1, 0}}));
    }
    void keepsRememberedCells() {
        const QStringList keys{"a", "b", "c"};
        // "c" was put at the top left: it keeps it, and the others go round it.
        QCOMPARE(arrange(keys, {{"c", {0, 0}}}, QSize(4, 3)),
                 (QList<QPoint>{{0, 1}, {0, 2}, {0, 0}}));
        // Two remembered in one cell: the first keeps it, the second goes to a free one.
        QCOMPARE(arrange(keys, {{"a", {2, 2}}, {"b", {2, 2}}}, QSize(4, 3)),
                 (QList<QPoint>{{2, 2}, {0, 0}, {0, 1}}));
        // A cell off a grid that has shrunk counts for nothing.
        QCOMPARE(arrange(keys, {{"a", {9, 0}}, {"b", {1, 5}}}, QSize(4, 3)),
                 (QList<QPoint>{{0, 0}, {0, 1}, {0, 2}}));
        // A cell for a key that is gone keeps nobody out.
        QCOMPARE(arrange({"a"}, {{"gone", {0, 0}}}, QSize(4, 3)), (QList<QPoint>{{0, 0}}));
    }
    void findsTheNearestFreeCell() {
        const QSize grid(5, 4);
        QCOMPARE(nearestFree(grid, {2, 2}, {}), QPoint(2, 2));
        // Taken: the one in the column before is as near as those above, below and after it,
        // and earlier.
        QCOMPARE(nearestFree(grid, {2, 2}, {{2, 2}}), QPoint(1, 2));
        QCOMPARE(nearestFree(grid, {2, 2}, {{2, 2}, {1, 2}, {2, 1}}), QPoint(2, 3));
        // Off the grid: the nearest cell on it.
        QCOMPARE(nearestFree(grid, {9, -3}, {}), QPoint(4, 0));
        QCOMPARE(nearestFree(grid, {-2, 2}, {{0, 2}}), QPoint(0, 1));
        // Every cell taken: where it was wanted, brought onto the grid.
        QList<QPoint> all;
        for (int column = 0; column < 2; ++column)
            for (int row = 0; row < 2; ++row)
                all.push_back({column, row});
        QCOMPARE(nearestFree(QSize(2, 2), {5, 5}, all), QPoint(1, 1));
    }
    void dropsDraggedIconsTogether() {
        const QSize grid(6, 5);
        // Three in a column dragged two cells right and one down keep their shape.
        QCOMPARE(drop({{0, 0}, {0, 1}, {0, 2}}, {2, 1}, {}, grid),
                 (QList<QPoint>{{2, 1}, {2, 2}, {2, 3}}));
        // One landing on an icon left behind goes beside it, the others where they are dragged.
        QCOMPARE(drop({{0, 0}, {0, 1}}, {3, 0}, {{3, 1}}, grid),
                 (QList<QPoint>{{3, 0}, {2, 1}}));
        // Dragged past the bottom edge, those off the grid come back onto it, beside each other.
        QCOMPARE(drop({{1, 2}, {1, 3}, {1, 4}}, {0, 2}, {}, grid),
                 (QList<QPoint>{{1, 4}, {0, 4}, {2, 4}}));
        // Moving within itself: each may land where another of them was.
        QCOMPARE(drop({{0, 0}, {0, 1}}, {0, 1}, {}, grid), (QList<QPoint>{{0, 1}, {0, 2}}));
        // Not moved at all, nothing changes.
        QCOMPARE(drop({{4, 4}, {0, 0}}, {0, 0}, {{1, 1}}, grid), (QList<QPoint>{{4, 4}, {0, 0}}));
    }
};

QTEST_GUILESS_MAIN(DesktopGridTest)
#include "desktop_grid_test.moc"
