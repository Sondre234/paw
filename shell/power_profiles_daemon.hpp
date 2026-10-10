// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "power_mode.hpp"
#include <QDBusConnection>
#include <QVariantMap>


// power-profiles-daemon on a system bus: org.freedesktop.UPower.PowerProfiles, or
// net.hadess.PowerProfiles, the name daemons before 0.20 answer to. Read whole as the daemon
// appears and followed through PropertiesChanged; unavailable while neither name is owned.
class PowerProfilesDaemon : public PowerMode {
    Q_OBJECT
  public:
    explicit PowerProfilesDaemon(const QDBusConnection &bus, QObject *parent = nullptr);

  protected:
    void sendProfile(const QString &profile) override;

  private Q_SLOTS:
    void propertiesChanged(const QString &interface, const QVariantMap &changed,
                           const QStringList &invalidated);

  private:
    QDBusConnection bus_;
    // Which of the two names answers (0 the newer, 1 the older), -1 for none, and what it said.
    int daemon_ = -1;
    QVariantMap properties_;
    // Asks which name is owned, the newer first, then reads it.
    void find(int from = 0);
    void read();
    void publish();
};
