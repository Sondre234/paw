// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDateTime>
#include <QList>
#include <QString>

// What the shell launched, and when: for each application its desktop id, how many times it was
// launched and when it last was, the most recent first. It is kept in a file of a line per
// application, its id, count and last launch (seconds since the epoch) separated by tabs, written
// whole through a temporary file on every launch and cut to the `limit` most recent.
class LaunchHistory {
  public:
    struct Entry {
        QString id;
        int count = 0;
        QDateTime last;
    };
    static constexpr int limit = 200;
    // Reads `path`; a missing or unreadable file is an empty history, and a line it cannot read
    // is left out.
    explicit LaunchHistory(QString path = {});
    // Notes a launch of `id` at `when` and saves the history; false, with error() saying why,
    // when it could not be saved.
    bool record(const QString &id, const QDateTime &when = QDateTime::currentDateTimeUtc());
    // Every application launched, the most recent first.
    const QList<Entry> &entries() const { return entries_; }
    // `id`'s entry, or nullptr when it was never launched.
    const Entry *find(const QString &id) const;
    QString error() const { return error_; }

  private:
    QString path_, error_;
    QList<Entry> entries_;
};
