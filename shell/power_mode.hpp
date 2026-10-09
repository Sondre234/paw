// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QStringList>
#include <memory>

// The power mode, as Quick Settings' tile shows it: the profile power-profiles-daemon runs the
// machine in ("power-saver", "balanced" or "performance") and those it offers. A backend delivers
// the daemon's state through update() and carries out sendProfile(); `available` is false while
// no daemon runs.
class PowerMode : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString profile READ profile NOTIFY changed)
    // The profiles offered, in the daemon's order (power-saver first).
    Q_PROPERTY(QStringList profiles READ profiles NOTIFY changed)
    // Why performance is held back ("lap-detected", "high-operating-temperature"), "" while it is
    // not.
    Q_PROPERTY(QString degraded READ degraded NOTIFY changed)
  public:
    struct State {
        bool available = false;
        QString profile;
        QStringList profiles;
        QString degraded;
    };
    explicit PowerMode(QObject *parent = nullptr) : QObject(parent) {}
    bool available() const { return state_.available; }
    QString profile() const { return state_.profile; }
    QStringList profiles() const { return state_.profiles; }
    QString degraded() const { return state_.degraded; }
    void update(State state);
    // Switches to one of the profiles offered, shown at once; failed() says when the daemon
    // refuses, and the profile it runs then shows again.
    Q_INVOKABLE void setProfile(const QString &profile);
  Q_SIGNALS:
    void changed();
    void failed(const QString &message);

  protected:
    virtual void sendProfile(const QString &profile) = 0;

  private:
    State state_;
};

// The power-profiles-daemon backend on the system bus, or one that never finds a daemon when
// paw was built without Qt's D-Bus module.
std::unique_ptr<PowerMode> makePowerMode();
