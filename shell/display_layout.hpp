// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QList>
#include <QPoint>
#include <QRect>
#include <QSize>

// The arithmetic of the display settings window's arrangement: monitors as rectangles in logical
// pixels, which touch along their edges, each one reaching every other through those it touches,
// and never overlap. Two touch where an edge of one lies on an edge of the other and they share
// some of its length; corners alone do not count.
namespace display_layout {
// Where a monitor `size` large goes when dragged to `wanted`: beside one of `others`, touching it,
// overlapping none, and of those places the nearest to `wanted`. Along the edge it shares, it
// comes in line with another monitor's edge or centre (tops, bottoms, a top with a bottom, centres;
// lefts and rights the same) when that is within `threshold`. With no others, `wanted` itself.
QPoint snap(const QList<QRect> &others, QSize size, QPoint wanted, int threshold = 0);
// The rectangles arranged, each moved as little as it can: the first stays where it is, and the
// others snap beside those placed so far, the nearest to them first, on the side of each that they
// were on, so that a row or a column keeps its order and its touching as one of it changes size.
QList<QRect> settle(const QList<QRect> &rects);
// Moved together so that they start at 0, 0.
QList<QRect> normalise(const QList<QRect> &rects);
// Whether they are arranged: none overlaps another, and each reaches the first through those it
// touches.
bool arranged(const QList<QRect> &rects);
// The size in logical pixels of a monitor with a mode `mode` pixels large, at `scale`, turned by
// `transform` (Wayland's, 0 to 7: the odd ones a quarter turn), as wlroots computes it.
QSize logicalSize(QSize mode, double scale, int transform);
} // namespace display_layout
