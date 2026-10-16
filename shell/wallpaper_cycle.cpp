// SPDX-License-Identifier: GPL-3.0-or-later
#include "wallpaper_cycle.hpp"
#include <QCollator>
#include <QDir>
#include <algorithm>

WallpaperCycle::WallpaperCycle(QObject *parent) : QObject(parent), random_(std::random_device{}()) {
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, [this] {
        remaining_ = -1;
        Q_EMIT due();
        // A step starts the time over; without one it starts over all the same.
        if (!timer_.isActive())
            restart();
    });
}
void WallpaperCycle::configure(const paw::WallpaperCycleConfig &config, const QString &folder) {
    const auto cleaned = folder.isEmpty() ? folder : QDir::cleanPath(folder);
    const bool order = cleaned != folder_ || config.shuffle != config_.shuffle;
    const bool time = config.interval != config_.interval;
    config_ = config;
    folder_ = cleaned;
    if (order) {
        listed_.clear();
        sequence_.clear();
        anchored_ = false;
    }
    // A reload that keeps the interval keeps the time already gone by.
    if (time)
        restart();
}
QStringList WallpaperCycle::pictures(const QString &folder) {
    if (folder.isEmpty())
        return {};
    const QDir directory(folder);
    auto names = directory.entryList({"*.png", "*.jpg", "*.jpeg", "*.webp", "*.bmp"},
                                     QDir::Files | QDir::Readable, QDir::NoSort);
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(names.begin(), names.end(),
              [&collator](const QString &a, const QString &b) { return collator.compare(a, b) < 0; });
    QStringList paths;
    paths.reserve(names.size());
    for (const auto &name : names)
        paths.push_back(QDir::cleanPath(directory.absoluteFilePath(name)));
    return paths;
}
QStringList WallpaperCycle::round() {
    QStringList order = listed_;
    if (config_.shuffle)
        std::shuffle(order.begin(), order.end(), random_);
    return order;
}
void WallpaperCycle::extend(bool after, qsizetype count) {
    auto deck = round();
    if (config_.shuffle) {
        // The pictures the deck comes next to, at the end of the sequence or at its start.
        const auto near = after ? sequence_.mid(std::max<qsizetype>(0, sequence_.size() - count))
                                : sequence_.first(std::min(count, sequence_.size()));
        // Its own nearest ones are none of them: each that is goes further in, swapped with
        // one that is not.
        const qsizetype nearest = std::min(count, deck.size());
        auto at = [&](qsizetype k) { return after ? k : deck.size() - 1 - k; };
        for (qsizetype k = 0; k < nearest; ++k) {
            if (!near.contains(deck[at(k)]))
                continue;
            for (qsizetype m = nearest; m < deck.size(); ++m)
                if (!near.contains(deck[at(m)])) {
                    deck.swapItemsAt(at(k), at(m));
                    break;
                }
        }
    }
    if (after) {
        sequence_ += deck;
    } else {
        sequence_ = deck + sequence_;
        position_ += deck.size();
    }
}
void WallpaperCycle::anchor(const QStringList &shown, int direction) {
    const auto monitors = shown.size();
    bool any = false;
    if (config_.shuffle) {
        // A deck that starts with the pictures shown, each once in the monitors' order, which
        // count as shown already; the rest follow shuffled.
        QStringList front;
        for (const auto &picture : shown)
            if (listed_.contains(picture) && !front.contains(picture))
                front.push_back(picture);
        QStringList rest = listed_;
        for (const auto &picture : front)
            rest.removeOne(picture);
        std::shuffle(rest.begin(), rest.end(), random_);
        sequence_ = front + rest;
        position_ = front.size() - monitors;
        any = !front.isEmpty();
    } else {
        // By name, the last monitor showing one of the pictures counting as the last shown.
        sequence_ = listed_;
        qsizetype last = -1;
        for (const auto &picture : shown)
            if (const auto index = listed_.indexOf(picture); index >= 0)
                last = index;
        position_ = last + 1 - monitors;
        any = last >= 0;
    }
    // Showing none of them, a step forward starts at the first and one back at the last.
    if (!any && direction < 0)
        position_ = 0;
    anchored_ = true;
}
QStringList WallpaperCycle::step(const QStringList &shown, int direction) {
    restart();
    const auto listed = pictures(folder_);
    if (listed.isEmpty() || shown.isEmpty()) {
        listed_.clear();
        sequence_.clear();
        anchored_ = false;
        return {};
    }
    QStringList current;
    for (const auto &picture : shown)
        current.push_back(picture.isEmpty() ? picture : QDir::cleanPath(picture));
    const auto monitors = current.size();
    if (listed != listed_) {
        listed_ = listed;
        anchored_ = false;
    }
    // Anything but what the last step showed (a picture picked from the panel, a monitor more or
    // less) lays the order out anew from what shows.
    if (!anchored_ || position_ < 0 || position_ + monitors > sequence_.size() ||
        sequence_.mid(position_, monitors) != current)
        anchor(current, direction);
    position_ += direction < 0 ? -monitors : monitors;
    while (position_ < 0)
        extend(false, monitors);
    while (position_ + monitors > sequence_.size())
        extend(true, monitors);
    // What was shown long ago goes, keeping a few rounds to step back through.
    const qsizetype keep = std::max<qsizetype>(512, 4 * listed_.size()) + monitors;
    if (sequence_.size() > keep) {
        if (position_ >= sequence_.size() / 2) {
            const auto drop = std::min(position_, sequence_.size() - keep);
            sequence_.remove(0, drop);
            position_ -= drop;
        } else {
            sequence_.resize(std::max(keep, position_ + monitors));
        }
    }
    return sequence_.mid(position_, monitors);
}
void WallpaperCycle::setPaused(bool paused) {
    if (paused == paused_)
        return;
    paused_ = paused;
    if (paused) {
        if (timer_.isActive()) {
            remaining_ = std::max(0, timer_.remainingTime());
            timer_.stop();
        }
    } else if (config_.interval > 0) {
        timer_.start(remaining_ >= 0 ? remaining_ : config_.interval * minute_);
        remaining_ = -1;
    }
}
void WallpaperCycle::setMinute(int milliseconds) {
    minute_ = std::max(1, milliseconds);
    restart();
}
void WallpaperCycle::restart() {
    timer_.stop();
    remaining_ = -1;
    if (config_.interval > 0 && !paused_)
        timer_.start(config_.interval * minute_);
}
