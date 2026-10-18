// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QHash>
#include <QList>
#include <QPoint>
#include <QSize>
#include <QString>
#include <QStringList>

// The arithmetic of the desktop's icons on their grid (shell.desktop_icons). A cell is a QPoint,
// its column in x and its row in y, from the top left of a grid `grid.width()` columns by
// `grid.height()` rows. Icons fill it down each column before the next, as on a desktop.
namespace desktop_grid {
// Whether `cell` is on the grid.
bool contains(QSize grid, QPoint cell);
// The free cell nearest `wanted` on the grid, none of `taken`: nearest by the distance between
// cells, the earlier down the columns between two as near. `wanted` itself when it is free, and
// when every cell is taken, `wanted` brought onto the grid.
QPoint nearestFree(QSize grid, QPoint wanted, const QList<QPoint> &taken);
// A cell for each of `keys`, in their order: the cell `saved` has for it while that is on the grid
// and no other key has it first, else the first free cell down the columns. Keys with saved cells
// are placed before the others, so that an icon remembered somewhere keeps its place from a new
// one; keys past the grid's last cell go on down columns beyond it.
QList<QPoint> arrange(const QStringList &keys, const QHash<QString, QPoint> &saved, QSize grid);
// The cells of `count` icons dropped together on `from` from elsewhere (a file manager's files):
// the free ones from `from` down its column, then down each column after it from the same row on,
// a block growing down and to the right; once those run out, the free ones nearest `from`. None of
// `taken`.
QList<QPoint> flow(QSize grid, QPoint from, int count, const QList<QPoint> &taken);
// Where icons dragged together land, `offset` cells from `cells`, where they are: each in the cell
// it is dragged to while that is on the grid and none of `others` (the icons left where they are)
// or of those of `cells` before it has it, else the free cell nearest it. The first of `cells`
// should be the icon under the pointer, so that it lands where it is let go.
QList<QPoint> drop(const QList<QPoint> &cells, QPoint offset, const QList<QPoint> &others, QSize grid);
} // namespace desktop_grid
