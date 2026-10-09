// SPDX-License-Identifier: GPL-3.0-or-later
#include "launch_history.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTimeZone>
#include <algorithm>

LaunchHistory::LaunchHistory(QString path) : path_(std::move(path)) {
    QFile file(path_);
    if (path_.isEmpty() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    for (const auto &line : QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts)) {
        const auto fields = line.split('\t');
        bool counted = false, timed = false;
        const int count = fields.size() == 3 ? fields[1].toInt(&counted) : 0;
        const qint64 seconds = fields.size() == 3 ? fields[2].toLongLong(&timed) : 0;
        if (!counted || !timed || count <= 0 || fields[0].isEmpty() || find(fields[0]))
            continue;
        entries_.push_back({fields[0], count, QDateTime::fromSecsSinceEpoch(seconds, QTimeZone::UTC)});
    }
    // The file is the shell's own, but it may have been edited by hand.
    std::stable_sort(entries_.begin(), entries_.end(),
                     [](const Entry &a, const Entry &b) { return a.last > b.last; });
    if (entries_.size() > limit)
        entries_.resize(limit);
}

const LaunchHistory::Entry *LaunchHistory::find(const QString &id) const {
    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [&id](const Entry &entry) { return entry.id == id; });
    return it == entries_.end() ? nullptr : &*it;
}

bool LaunchHistory::record(const QString &id, const QDateTime &when) {
    if (id.isEmpty())
        return true;
    Entry entry{id, 1, when.toUTC()};
    auto it = std::find_if(entries_.begin(), entries_.end(),
                           [&id](const Entry &other) { return other.id == id; });
    if (it != entries_.end()) {
        entry.count = it->count + 1;
        entries_.erase(it);
    }
    entries_.push_front(entry);
    if (entries_.size() > limit)
        entries_.resize(limit);
    if (path_.isEmpty())
        return true;
    QDir().mkpath(QFileInfo(path_).path());
    QSaveFile file(path_);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        for (const auto &item : entries_)
            file.write(QString("%1\t%2\t%3\n")
                           .arg(item.id)
                           .arg(item.count)
                           .arg(item.last.toSecsSinceEpoch())
                           .toUtf8());
        if (file.commit()) {
            error_.clear();
            return true;
        }
    }
    error_ = file.errorString();
    return false;
}
