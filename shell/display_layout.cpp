// SPDX-License-Identifier: GPL-3.0-or-later
#include "display_layout.hpp"
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <limits>

namespace {
// Past the last pixel: QRect's right() and bottom() are the last pixel itself.
int end(int start, int length) { return start + length; }
int right(const QRect &rect) { return end(rect.x(), rect.width()); }
int bottom(const QRect &rect) { return end(rect.y(), rect.height()); }

bool overlap(const QRect &a, const QRect &b) {
    return a.x() < right(b) && b.x() < right(a) && a.y() < bottom(b) && b.y() < bottom(a);
}

bool touch(const QRect &a, const QRect &b) {
    const bool side =
        (right(a) == b.x() || right(b) == a.x()) && a.y() < bottom(b) && b.y() < bottom(a);
    const bool stacked =
        (bottom(a) == b.y() || bottom(b) == a.y()) && a.x() < right(b) && b.x() < right(a);
    return side || stacked;
}

// How far apart two rectangles are, squared: 0 where they touch or overlap.
long long gap(const QRect &a, const QRect &b) {
    const long long dx = std::max({0, b.x() - right(a), a.x() - right(b)});
    const long long dy = std::max({0, b.y() - bottom(a), a.y() - bottom(b)});
    return dx * dx + dy * dy;
}

// Where along one axis (`across`: x, else y) an edge `length` long starting at `value` comes in
// line with one of `others`, within `threshold` and between `low` and `high`; `value` itself
// where none is that near.
int align(int value, int length, const QList<QRect> &others, bool across, int low, int high,
          int threshold) {
    int best = value, nearest = threshold + 1;
    for (const auto &other : others) {
        const int start = across ? other.x() : other.y();
        const int extent = across ? other.width() : other.height();
        // Starts, ends, a start at the other's end and an end at its start, and centres.
        for (int target : {start, start + extent - length, start + extent, start - length,
                           start + (extent - length) / 2}) {
            const int distance = std::abs(target - value);
            if (target >= low && target <= high && distance < nearest) {
                best = target;
                nearest = distance;
            }
        }
    }
    return best;
}

// The sides of `other` a monitor may go beside.
enum Side { Right = 1, Left = 2, Below = 4, Above = 8, Anywhere = 15 };

// The side of `other` that a monitor at `rect` is on: the one its centre is furthest towards, as
// a share of the two's sizes together.
int sideOf(const QRect &rect, const QRect &other) {
    const double dx = (2.0 * rect.x() + rect.width()) - (2.0 * other.x() + other.width());
    const double dy = (2.0 * rect.y() + rect.height()) - (2.0 * other.y() + other.height());
    if (std::abs(dx) / (rect.width() + other.width()) >=
        std::abs(dy) / (rect.height() + other.height()))
        return dx >= 0 ? Right : Left;
    return dy >= 0 ? Below : Above;
}

// snap, beside each of `others` only on the sides `sides` gives for it; false where none is free.
bool place(const QList<QRect> &others, QSize size, QPoint wanted, int threshold,
           const std::function<int(const QRect &)> &sides, QPoint &best) {
    const int w = size.width(), h = size.height();
    long long nearest = -1;
    auto consider = [&](QPoint at) {
        const QRect placed(at, size);
        if (std::any_of(others.begin(), others.end(),
                        [&](const QRect &other) { return overlap(placed, other); }))
            return;
        const long long dx = at.x() - wanted.x(), dy = at.y() - wanted.y();
        if (nearest < 0 || dx * dx + dy * dy < nearest) {
            nearest = dx * dx + dy * dy;
            best = at;
        }
    };
    for (const auto &other : others) {
        const int allowed = sides(other);
        // Beside it, sharing at least a pixel of its left or right edge.
        for (int side : {Right, Left}) {
            if (!(allowed & side))
                continue;
            const int low = other.y() - h + 1, high = bottom(other) - 1;
            consider(
                {side == Right ? right(other) : other.x() - w,
                 align(std::clamp(wanted.y(), low, high), h, others, false, low, high, threshold)});
        }
        // Over or under it, the same along its top or bottom edge.
        for (int side : {Below, Above}) {
            if (!(allowed & side))
                continue;
            const int low = other.x() - w + 1, high = right(other) - 1;
            consider(
                {align(std::clamp(wanted.x(), low, high), w, others, true, low, high, threshold),
                 side == Below ? bottom(other) : other.y() - h});
        }
    }
    return nearest >= 0;
}
} // namespace

namespace display_layout {
QPoint snap(const QList<QRect> &others, QSize size, QPoint wanted, int threshold) {
    if (others.isEmpty())
        return wanted;
    QPoint best;
    if (place(others, size, wanted, threshold, [](const QRect &) { return Anywhere; }, best))
        return best;
    // Hemmed in everywhere it could go: right of the one reaching furthest right, level with it.
    const auto furthest =
        std::max_element(others.begin(), others.end(),
                         [](const QRect &a, const QRect &b) { return right(a) < right(b); });
    return {right(*furthest), furthest->y()};
}

QList<QRect> settle(const QList<QRect> &rects) {
    QList<QRect> result = rects;
    QList<bool> placed(rects.size(), false);
    QList<QRect> done;
    for (qsizetype count = 0; count < rects.size(); ++count) {
        // The next: the nearest to those placed so far where it is now, the first of a tie.
        qsizetype next = -1;
        long long nearest = std::numeric_limits<long long>::max();
        for (qsizetype i = 0; i < rects.size(); ++i) {
            if (placed[i])
                continue;
            long long distance = done.isEmpty() ? 0 : std::numeric_limits<long long>::max();
            for (const auto &other : done)
                distance = std::min(distance, gap(rects[i], other));
            if (distance < nearest) {
                nearest = distance;
                next = i;
            }
        }
        // On the side of each it was on, where it can be: a monitor right of another that grew
        // moves right, not over it.
        const QRect &rect = rects[next];
        QPoint at;
        if (done.isEmpty())
            at = rect.topLeft();
        else if (!place(
                     done, rect.size(), rect.topLeft(), 0,
                     [&rect](const QRect &other) { return sideOf(rect, other); }, at))
            at = snap(done, rect.size(), rect.topLeft());
        result[next].moveTopLeft(at);
        placed[next] = true;
        done.push_back(result[next]);
    }
    return result;
}

QList<QRect> normalise(const QList<QRect> &rects) {
    if (rects.isEmpty())
        return rects;
    int x = std::numeric_limits<int>::max(), y = std::numeric_limits<int>::max();
    for (const auto &rect : rects) {
        x = std::min(x, rect.x());
        y = std::min(y, rect.y());
    }
    QList<QRect> result;
    for (const auto &rect : rects)
        result.push_back(rect.translated(-x, -y));
    return result;
}

bool arranged(const QList<QRect> &rects) {
    for (qsizetype i = 0; i < rects.size(); ++i)
        for (qsizetype j = i + 1; j < rects.size(); ++j)
            if (overlap(rects[i], rects[j]))
                return false;
    // Each reaches the first through those it touches.
    QList<bool> reached(rects.size(), false);
    QList<qsizetype> queue;
    if (!rects.isEmpty()) {
        reached[0] = true;
        queue.push_back(0);
    }
    while (!queue.isEmpty()) {
        const auto at = queue.takeFirst();
        for (qsizetype i = 0; i < rects.size(); ++i)
            if (!reached[i] && touch(rects[at], rects[i])) {
                reached[i] = true;
                queue.push_back(i);
            }
    }
    return std::all_of(reached.begin(), reached.end(), [](bool done) { return done; });
}

QSize logicalSize(QSize mode, double scale, int transform) {
    const bool turned = transform % 2 == 1;
    const int width = turned ? mode.height() : mode.width();
    const int height = turned ? mode.width() : mode.height();
    // wlroots divides each side by the float scale and drops the fraction.
    const float factor = scale > 0 ? static_cast<float>(scale) : 1.0f;
    return {static_cast<int>(width / factor), static_cast<int>(height / factor)};
}
} // namespace display_layout
