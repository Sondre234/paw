// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QString>
#include <QList>
#include <QSocketNotifier>
#include <QTimer>

// Battery and network state as the panel shows them, read from sysfs (/sys/class/power_supply
// and /sys/class/net). Sysfs has no change notification of its own, so with `watch` the kernel's
// hotplug messages (power supply and network changes) and rtnetlink link changes prompt the
// reads and only a slow timer backs them up, and it stops when there is nothing to poll for. When
// the sockets cannot be opened, or without `watch`, it polls every few seconds.
class SystemStatus : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool batteryPresent READ batteryPresent NOTIFY changed)
    Q_PROPERTY(int batteryPercent READ batteryPercent NOTIFY changed)
    // "charging", "discharging", "full", or "unknown".
    Q_PROPERTY(QString batteryState READ batteryState NOTIFY changed)
    // "none" (no interface to show), "disconnected", "ethernet", or "wifi".
    Q_PROPERTY(QString networkState READ networkState NOTIFY changed)
    Q_PROPERTY(QString networkInterface READ networkInterface NOTIFY changed)
    Q_PROPERTY(QString batteryText READ batteryText NOTIFY changed)
    Q_PROPERTY(QString networkText READ networkText NOTIFY changed)
  public:
    // `root` holds the sysfs tree; tests point it at a fake one.
    explicit SystemStatus(QString root = "/sys", QObject *parent = nullptr, bool watch = false);
    ~SystemStatus() override;
    // True when kernel messages, not polling, prompt the reads.
    bool watching() const { return !sockets_.isEmpty(); }
    // The timer's interval in milliseconds, 0 when it is stopped.
    int pollInterval() const { return timer_.isActive() ? timer_.interval() : 0; }
    // Whether a kernel hotplug message (key=value pairs separated by NULs) concerns power
    // supplies or network interfaces.
    static bool relevantUevent(const QByteArray &message);
    bool batteryPresent() const { return batteryPresent_; }
    int batteryPercent() const { return batteryPercent_; }
    QString batteryState() const { return batteryState_; }
    QString networkState() const { return networkState_; }
    QString networkInterface() const { return networkInterface_; }
    // One-line descriptions for tooltips and accessibility.
    QString batteryText() const;
    QString networkText() const;
    // Reads the state again, emitting changed() when anything differs.
    Q_INVOKABLE void refresh();
  Q_SIGNALS:
    void changed();

  private:
    void openNetlink(int protocol, unsigned groups, bool uevents);
    void updatePolling(bool batteryPresent);
    QString root_;
    QTimer timer_, debounce_;
    QList<QSocketNotifier *> sockets_;
    bool batteryPresent_ = false;
    int batteryPercent_ = 0;
    QString batteryState_ = "unknown", networkState_ = "none", networkInterface_;
    QString read(const QString &path) const;
};
