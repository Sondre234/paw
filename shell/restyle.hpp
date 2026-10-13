// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QTimer>

class QQuickWindow;

// What Restyle waits for, apart from Qt's windows so that it can be tested on its own. A window
// shown owes a frame once it is first seen, and again once a picture in it has loaded, since it
// draws the picture only then. The look is settled once no window owes a frame or has a picture
// loading, and no window has drawn for `quietMs`, or `settleMs` have passed since that held, for
// windows that keep drawing as the pointer moves over them. Times are milliseconds.
class RestyleWait {
  public:
    static constexpr qint64 quietMs = 50;
    static constexpr qint64 settleMs = 150;
    void start(qint64 now);
    // The window is shown, with a picture in it loading or not. True when it owes a frame it has
    // not been asked for: shown first, or its picture just loaded.
    bool shown(const void *window, bool loading, qint64 now);
    // Hidden, it draws nothing more; shown again, it is new.
    void hidden(const void *window);
    void swapped(const void *window, qint64 now);
    bool settled(qint64 now);

  private:
    struct Window {
        bool owes = true, loading = false;
    };
    QHash<const void *, Window> windows_;
    qint64 quiet_ = 0, drawn_ = -1;
};

// Follows the shell's windows after it took up a new appearance profile until the new look is on
// the screen, as RestyleWait says. Then `settled`, once; after `limit` milliseconds at the latest.
// It looks every 16 ms while it runs, and costs nothing otherwise.
class Restyle : public QObject {
    Q_OBJECT
  public:
    explicit Restyle(QObject *parent = nullptr);
    // Starts following, or again from now when it runs.
    void start(int limit = 900);
    bool running() const { return timer_.isActive(); }
  Q_SIGNALS:
    void settled();

  private:
    void poll();
    void finish();
    QTimer timer_;
    QElapsedTimer started_;
    RestyleWait wait_;
    QSet<QQuickWindow *> watched_; // the windows whose frames it hears of
    QList<QMetaObject::Connection> connections_;
    int limit_ = 900;
};
