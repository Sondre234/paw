// SPDX-License-Identifier: GPL-3.0-or-later
#include "restyle.hpp"

#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>

void RestyleWait::start(qint64 now) {
    windows_.clear();
    quiet_ = now;
    drawn_ = -1;
}

bool RestyleWait::shown(const void *window, bool loading, qint64 now) {
    auto it = windows_.find(window);
    if (it == windows_.end()) {
        windows_.insert(window, {true, loading});
        return true;
    }
    bool loaded = it->loading && !loading;
    it->loading = loading;
    if (loading)
        quiet_ = now;
    if (loaded)
        it->owes = true;
    return loaded;
}

void RestyleWait::hidden(const void *window) { windows_.remove(window); }

void RestyleWait::swapped(const void *window, qint64 now) {
    auto it = windows_.find(window);
    if (it == windows_.end())
        return;
    it->owes = false;
    quiet_ = now;
}

bool RestyleWait::settled(qint64 now) {
    bool waiting = false;
    for (const auto &window : std::as_const(windows_))
        waiting = waiting || window.owes || window.loading;
    if (waiting)
        drawn_ = -1;
    else if (drawn_ < 0)
        drawn_ = now;
    return drawn_ >= 0 && (now - quiet_ >= quietMs || now - drawn_ >= settleMs);
}

namespace {
// Whether an image shown under `item` is still loading, such as a new wallpaper, decoded off the
// GUI thread: the look is not on the screen until it is, and its window has drawn it.
bool loading(const QQuickItem *item) {
    if (!item || !item->isVisible())
        return false;
    // QQuickImage and AnimatedImage's base; its status 2 is Loading.
    if (item->inherits("QQuickImageBase") && item->property("status").toInt() == 2)
        return true;
    // The frosted glass (shell.blur) waiting for its output's wallpaper, blurred anew.
    if (item->inherits("GlassItem") && item->property("pending").toBool())
        return true;
    const auto children = item->childItems();
    for (const auto *child : children)
        if (loading(child))
            return true;
    return false;
}
} // namespace

Restyle::Restyle(QObject *parent) : QObject(parent) {
    timer_.setInterval(16);
    connect(&timer_, &QTimer::timeout, this, &Restyle::poll);
}

void Restyle::start(int limit) {
    for (const auto &connection : std::as_const(connections_))
        disconnect(connection);
    connections_.clear();
    watched_.clear();
    limit_ = limit;
    started_.start();
    wait_.start(0);
    timer_.start();
    poll();
}

void Restyle::poll() {
    const qint64 now = started_.elapsed();
    const auto windows = QGuiApplication::topLevelWindows();
    for (auto *window : windows) {
        auto *quick = qobject_cast<QQuickWindow *>(window);
        if (!quick)
            continue;
        // A window hidden meanwhile draws nothing more; one shown meanwhile, such as the macOS
        // style's menu bar, draws its first frame.
        if (!quick->isVisible() || !quick->isExposed()) {
            wait_.hidden(quick);
            continue;
        }
        if (!watched_.contains(quick)) {
            watched_.insert(quick);
            // Emitted from the render thread with the threaded render loop: queued to this one.
            connections_ << connect(quick, &QQuickWindow::frameSwapped, this,
                                    [this, quick] { wait_.swapped(quick, started_.elapsed()); });
            connections_ << connect(quick, &QObject::destroyed, this, [this, quick] {
                watched_.remove(quick);
                wait_.hidden(quick);
            });
        }
        // One that has nothing new to draw draws anyway, which says it is done.
        if (wait_.shown(quick, loading(quick->contentItem()), now))
            quick->update();
    }
    if (wait_.settled(now) || now >= limit_)
        finish();
}

void Restyle::finish() {
    timer_.stop();
    for (const auto &connection : std::as_const(connections_))
        disconnect(connection);
    connections_.clear();
    watched_.clear();
    Q_EMIT settled();
}
