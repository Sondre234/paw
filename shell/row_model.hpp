// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QAbstractListModel>
#include <algorithm>
#include <vector>

// A list model whose rows are replaced as a whole and kept in place: rows gone are removed, those
// still there changed and moved to their new places, new ones inserted. A Repeater over it keeps
// the delegate of every row that stays, so a row being typed in (a password, a PIN) keeps what was
// typed, and its handlers run to the end, as the list changes and reorders. `Row` has a `key()`
// that names it and an `==`.
template <class Row>
class RowModel : public QAbstractListModel {
  public:
    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : int(rows_.size());
    }
    int count() const { return int(rows_.size()); }
    const std::vector<Row> &all() const { return rows_; }

  protected:
    // The row at a valid index of this model, or null.
    const Row *at(const QModelIndex &index) const {
        return index.isValid() && index.row() < count() ? &rows_[size_t(index.row())] : nullptr;
    }
    // Takes `rows` as they are now, in order; returns whether their number changed.
    bool replace(const std::vector<Row> &rows) {
        const size_t before = rows_.size();
        auto listed = [&rows](const Row &row) {
            return std::any_of(rows.begin(), rows.end(), [&row](const Row &next) { return next.key() == row.key(); });
        };
        for (int i = count() - 1; i >= 0; --i)
            if (!listed(rows_[size_t(i)])) {
                beginRemoveRows({}, i, i);
                rows_.erase(rows_.begin() + i);
                endRemoveRows();
            }
        // Each place in turn: the row already there, moved up from further down, or new.
        for (int i = 0; i < int(rows.size()); ++i) {
            const Row &next = rows[size_t(i)];
            if (i >= count() || rows_[size_t(i)].key() != next.key()) {
                auto it = std::find_if(rows_.begin() + i, rows_.end(),
                                       [&next](const Row &row) { return row.key() == next.key(); });
                if (it == rows_.end()) {
                    beginInsertRows({}, i, i);
                    rows_.insert(rows_.begin() + i, next);
                    endInsertRows();
                    continue;
                }
                const int from = int(it - rows_.begin());
                beginMoveRows({}, from, from, {}, i);
                const Row moved = *it;
                rows_.erase(it);
                rows_.insert(rows_.begin() + i, moved);
                endMoveRows();
            }
            if (!(rows_[size_t(i)] == next)) {
                rows_[size_t(i)] = next;
                Q_EMIT dataChanged(index(i), index(i));
            }
        }
        return rows_.size() != before;
    }

  private:
    std::vector<Row> rows_;
};
