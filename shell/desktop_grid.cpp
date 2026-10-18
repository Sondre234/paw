// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop_grid.hpp"
#include <QSet>
#include <algorithm>
#include <limits>

namespace desktop_grid {
bool contains(QSize grid, QPoint cell) {
    return cell.x() >= 0 && cell.y() >= 0 && cell.x() < grid.width() && cell.y() < grid.height();
}

QPoint nearestFree(QSize grid, QPoint wanted, const QList<QPoint> &taken) {
    const QSet<QPoint> busy(taken.begin(), taken.end());
    if (contains(grid, wanted) && !busy.contains(wanted))
        return wanted;
    QPoint best(-1, -1);
    long long nearest = std::numeric_limits<long long>::max();
    // Down each column before the next, so that the earlier of two as near wins.
    for (int column = 0; column < grid.width(); ++column)
        for (int row = 0; row < grid.height(); ++row) {
            const QPoint cell(column, row);
            if (busy.contains(cell))
                continue;
            const long long dx = cell.x() - wanted.x(), dy = cell.y() - wanted.y();
            if (dx * dx + dy * dy < nearest) {
                nearest = dx * dx + dy * dy;
                best = cell;
            }
        }
    if (best.x() >= 0)
        return best;
    return {std::clamp(wanted.x(), 0, std::max(0, grid.width() - 1)),
            std::clamp(wanted.y(), 0, std::max(0, grid.height() - 1))};
}

QList<QPoint> arrange(const QStringList &keys, const QHash<QString, QPoint> &saved, QSize grid,
                      const QList<QPoint> &blocked) {
    QList<QPoint> cells(keys.size(), QPoint(-1, -1));
    QSet<QPoint> taken(blocked.begin(), blocked.end());
    for (qsizetype i = 0; i < keys.size(); ++i) {
        const auto it = saved.constFind(keys[i]);
        if (it != saved.cend() && contains(grid, *it) && !taken.contains(*it)) {
            cells[i] = *it;
            taken.insert(*it);
        }
    }
    // The next free cell down the columns, past the grid's last once it is full.
    const int rows = std::max(1, grid.height());
    int next = 0;
    for (qsizetype i = 0; i < keys.size(); ++i) {
        if (cells[i].x() >= 0)
            continue;
        QPoint cell;
        do {
            cell = QPoint(next / rows, next % rows);
            ++next;
        } while (taken.contains(cell));
        cells[i] = cell;
        taken.insert(cell);
    }
    return cells;
}

QList<QPoint> flow(QSize grid, QPoint from, int count, const QList<QPoint> &taken) {
    QList<QPoint> busy = taken, cells;
    const QSet<QPoint> held(taken.begin(), taken.end());
    for (int column = std::max(0, from.x()); column < grid.width() && cells.size() < count; ++column)
        for (int row = std::max(0, from.y()); row < grid.height() && cells.size() < count; ++row)
            if (!held.contains(QPoint(column, row))) {
                cells.push_back(QPoint(column, row));
                busy.push_back(cells.back());
            }
    while (cells.size() < count) {
        cells.push_back(nearestFree(grid, from, busy));
        busy.push_back(cells.back());
    }
    return cells;
}

QList<QPoint> block(QPoint corner, QSize size) {
    QList<QPoint> cells;
    for (int column = 0; column < size.width(); ++column)
        for (int row = 0; row < size.height(); ++row)
            cells.push_back(corner + QPoint(column, row));
    return cells;
}

std::optional<QPoint> freeBlock(QSize grid, QPoint wanted, QSize size, const QList<QPoint> &taken) {
    const QSet<QPoint> busy(taken.begin(), taken.end());
    auto free = [&](QPoint corner) {
        if (corner.x() < 0 || corner.y() < 0 || corner.x() + size.width() > grid.width() ||
            corner.y() + size.height() > grid.height())
            return false;
        const auto cells = block(corner, size);
        return std::none_of(cells.cbegin(), cells.cend(), [&](QPoint cell) { return busy.contains(cell); });
    };
    if (free(wanted))
        return wanted;
    std::optional<QPoint> best;
    long long nearest = std::numeric_limits<long long>::max();
    for (int column = 0; column + size.width() <= grid.width(); ++column)
        for (int row = 0; row + size.height() <= grid.height(); ++row) {
            const QPoint corner(column, row);
            const long long dx = column - wanted.x(), dy = row - wanted.y();
            if (dx * dx + dy * dy < nearest && free(corner)) {
                nearest = dx * dx + dy * dy;
                best = corner;
            }
        }
    return best;
}

QList<QPoint> drop(const QList<QPoint> &cells, QPoint offset, const QList<QPoint> &others, QSize grid) {
    QList<QPoint> taken = others, landed;
    for (const auto &cell : cells) {
        const QPoint place = nearestFree(grid, cell + offset, taken);
        landed.push_back(place);
        taken.push_back(place);
    }
    return landed;
}
} // namespace desktop_grid
