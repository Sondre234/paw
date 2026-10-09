// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "notifications.hpp"
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QVariantMap>

// The org.freedesktop.Notifications interface of the notification spec 1.2, forwarding to a
// NotificationCenter and reporting its closures and actions back to the applications.
class NotificationsAdaptor : public QDBusAbstractAdaptor {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")
  public:
    NotificationsAdaptor(NotificationCenter &center, QObject *parent);
    // The notification a Notify call describes; exposed for tests.
    static Notification parse(const QString &app, const QString &icon, const QString &summary,
                              const QString &body, const QStringList &actions,
                              const QVariantMap &hints, int timeout);
  public Q_SLOTS:
    QStringList GetCapabilities();
    uint Notify(const QString &app_name, uint replaces_id, const QString &app_icon,
                const QString &summary, const QString &body, const QStringList &actions,
                const QVariantMap &hints, int expire_timeout);
    void CloseNotification(uint id);
    QString GetServerInformation(QString &vendor, QString &version, QString &spec_version);
  Q_SIGNALS:
    void NotificationClosed(uint id, uint reason);
    void ActionInvoked(uint id, const QString &action_key);

  private:
    NotificationCenter &center_;
};

// Owns the well-known name on the session bus while it lives.
class NotificationService : public QObject {
    Q_OBJECT
  public:
    NotificationService(NotificationCenter &center, QObject *parent = nullptr);
    ~NotificationService() override;
    // Registers the object and the name; false, with error() set, when there is no session bus
    // or another daemon already owns the name.
    bool start(const QDBusConnection &bus = QDBusConnection::sessionBus());
    QString error() const { return error_; }

  private:
    NotificationCenter &center_;
    QDBusConnection bus_{QString()};
    QString error_;
    bool registered_ = false;
};
