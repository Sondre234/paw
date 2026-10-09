// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDBusConnection>
#include <QDBusContext>
#include <QObject>
#include <QStringList>
#include <functional>

class QDBusServiceWatcher;

// Whether `path` is a D-Bus object path: "/", or elements of letters, digits and underscores, each
// after a "/".
bool trayValidPath(const QString &path);

// org.kde.StatusNotifierWatcher, the registry of a session's tray items: applications register
// their StatusNotifierItem with it and hosts (trays) learn of them from it. An item registers by
// bus name, its object at /StatusNotifierItem, or by object path, the caller's connection being
// its service (Ayatana's library does this); it is listed as service + path, and dropped when its
// service leaves the bus.
class TrayWatcher : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierWatcher")
    Q_PROPERTY(QStringList RegisteredStatusNotifierItems READ items)
    Q_PROPERTY(bool IsStatusNotifierHostRegistered READ hostRegistered)
    Q_PROPERTY(int ProtocolVersion READ protocolVersion)
  public:
    explicit TrayWatcher(QObject *parent = nullptr);
    ~TrayWatcher() override;
    // Exports the watcher and takes its name; false, with error() set, when there is no bus or
    // another watcher owns the name.
    bool start(const QDBusConnection &bus);
    QString error() const { return error_; }
    QStringList items() const { return items_; }
    bool hostRegistered() const { return !hosts_.isEmpty(); }
    int protocolVersion() const { return 0; }
    // Registers a host in this process, whose name is known to exist.
    void addHost(const QString &service);
    // At most this many items are listed; more registrations are refused.
    static constexpr int maxItems = 128;

  public Q_SLOTS:
    void RegisterStatusNotifierItem(const QString &service);
    void RegisterStatusNotifierHost(const QString &service);

  Q_SIGNALS:
    void StatusNotifierItemRegistered(const QString &item);
    void StatusNotifierItemUnregistered(const QString &item);
    void StatusNotifierHostRegistered();
    void StatusNotifierHostUnregistered();

  private:
    QDBusConnection bus_{QString()};
    QDBusServiceWatcher *owners_ = nullptr;
    QStringList items_, hosts_;
    QString error_;
    bool registered_ = false;
    // Answers the call being handled once `service` turns out to have an owner, then runs `add`.
    void whenOwned(const QString &service, std::function<void()> add);
    void serviceGone(const QString &service);
};
