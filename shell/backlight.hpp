// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QSocketNotifier>
#include <QTimer>

// The screen backlight (/sys/class/backlight): its level as a percentage, and a signal when it
// changes, for the on-screen display. The kernel sends a hotplug message each time a backlight's
// brightness changes, so with `watch` nothing polls; where that socket cannot be opened it
// polls once a second, and only where a backlight exists.
class Backlight : public QObject {
    Q_OBJECT
    // Whether a backlight exists, and its level from 0 to 100 (-1 without one), for Quick
    // Settings' slider.
    Q_PROPERTY(bool present READ present NOTIFY levelChanged)
    Q_PROPERTY(int percent READ percent NOTIFY levelChanged)
  public:
    // `pollMs` above 0 polls at that interval whatever else there is, for tests that write the
    // level themselves.
    explicit Backlight(QString root = "/sys", bool watch = false, int pollMs = 0, QObject *parent = nullptr);
    ~Backlight() override;
    bool present() const { return present_; }
    // 0 to 100; -1 without a backlight.
    int percent() const { return percent_; }
    // Its name under class/backlight, "" without one.
    QString name() const { return name_; }
    bool watching() const { return notifier_ != nullptr; }
    // Whether a kernel hotplug message (key=value pairs separated by NULs) concerns a backlight.
    static bool relevantUevent(const QByteArray &message);
    // Reads the level again; emits changed() when it differs from the last read, but not for the
    // first read, which only establishes what is there.
    void refresh();
    // Sets the level, 0 to 100, shown at once: through logind's Session.SetBrightness, which lets
    // the session's user change it (on the bus $PAW_LOGIN1_BUS names, else the system bus;
    // only when paw is built with Qt's D-Bus module), or, in a sysfs tree other than /sys and
    // without that bus, by writing its brightness file, as tests do. failed() says what went
    // wrong.
    Q_INVOKABLE void setPercent(int percent);
  Q_SIGNALS:
    void changed(int percent);
    // The level, or whether there is a backlight, changed in any way.
    void levelChanged();
    void failed(const QString &message);

  private:
    QString root_, name_;
    QSocketNotifier *notifier_ = nullptr;
    QTimer timer_;
    bool present_ = false, known_ = false;
    int percent_ = -1, max_ = 0;
};
