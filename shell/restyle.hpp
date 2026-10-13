// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QSet>
#include <QTimer>

class QQuickWindow;

// Follows the shell's windows after it took up a new appearance profile until the new look is on
// the screen: every window shown has drawn a frame since, no picture in one is still loading, and
// none has drawn for a moment (a bar of another size takes a few frames to settle as the
// compositor configures it again), or a little longer has passed for windows that keep drawing.
// Then `settled`, once; after `limit` milliseconds at the latest. It looks every 16 ms while it
// runs, and costs nothing otherwise.
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
    void watch(QQuickWindow *window);
    void finish();
    QTimer timer_;
    QElapsedTimer started_, quiet_;
    QElapsedTimer drawn_; // since every window drew, its pictures loaded; invalid until then
    QSet<QQuickWindow *> watched_; // the windows shown since it started
    QSet<QQuickWindow *> waiting_; // those of them that have not drawn since
    QList<QMetaObject::Connection> connections_;
    int limit_ = 900;
};
