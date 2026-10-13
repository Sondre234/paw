// SPDX-License-Identifier: GPL-3.0-or-later
#include "restyle.hpp"

#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>

namespace {
// How long no window may draw before the look counts as settled, in milliseconds; and how long
// after every window drew it, its pictures loaded, it counts as settled anyway, for windows that
// go on drawing as the pointer moves over them.
constexpr int quietMs = 50;
constexpr int settleMs = 150;

// Whether an image shown under `item` is still loading, such as a new wallpaper, decoded off the
// GUI thread: the look is not on the screen until it is.
bool loading(const QQuickItem *item) {
    if (!item || !item->isVisible())
        return false;
    // QQuickImage and AnimatedImage's base; its status 2 is Loading.
    if (item->inherits("QQuickImageBase") && item->property("status").toInt() == 2)
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
    waiting_.clear();
    limit_ = limit;
    started_.start();
    quiet_.start();
    drawn_.invalidate();
    timer_.start();
    poll();
}

void Restyle::watch(QQuickWindow *window) {
    watched_.insert(window);
    waiting_.insert(window);
    // Emitted from the render thread with the threaded render loop: queued to this one.
    connections_ << connect(window, &QQuickWindow::frameSwapped, this, [this, window] {
        waiting_.remove(window);
        quiet_.restart();
    });
    connections_ << connect(window, &QObject::destroyed, this, [this, window] {
        watched_.remove(window);
        waiting_.remove(window);
    });
    // One that has nothing new to draw draws anyway, which says it is done.
    window->update();
}

void Restyle::poll() {
    bool busy = false;
    const auto windows = QGuiApplication::topLevelWindows();
    for (auto *window : windows) {
        auto *quick = qobject_cast<QQuickWindow *>(window);
        if (!quick)
            continue;
        // A window hidden meanwhile draws nothing more; one shown meanwhile, such as the macOS
        // style's menu bar, draws its first frame.
        if (!quick->isVisible() || !quick->isExposed()) {
            waiting_.remove(quick);
            continue;
        }
        if (!watched_.contains(quick))
            watch(quick);
        busy = busy || loading(quick->contentItem());
    }
    if (busy || !waiting_.isEmpty()) {
        // A picture shows once it has loaded and its window draws again.
        if (busy)
            quiet_.restart();
        drawn_.invalidate();
    } else if (!drawn_.isValid()) {
        drawn_.start();
    }
    const bool settled =
        drawn_.isValid() && (quiet_.elapsed() >= quietMs || drawn_.elapsed() >= settleMs);
    if (settled || started_.elapsed() >= limit_)
        finish();
}

void Restyle::finish() {
    timer_.stop();
    for (const auto &connection : std::as_const(connections_))
        disconnect(connection);
    connections_.clear();
    watched_.clear();
    waiting_.clear();
    Q_EMIT settled();
}
