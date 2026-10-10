// SPDX-License-Identifier: GPL-3.0-or-later
#include "tray_watcher.hpp"
#include "dbus_util.hpp"
#include <QDBusConnectionInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QRegularExpression>

namespace {
constexpr auto serviceName = "org.kde.StatusNotifierWatcher";
constexpr auto objectPath = "/StatusNotifierWatcher";
} // namespace

bool trayValidPath(const QString &path) {
    // Anchored at the very end: "$" alone would let a trailing newline through.
    static const QRegularExpression pattern(QRegularExpression::anchoredPattern("/|(/[A-Za-z0-9_]+)+"));
    return path.size() <= 1024 && pattern.match(path).hasMatch();
}

TrayWatcher::TrayWatcher(QObject *parent) : QObject(parent) {}
TrayWatcher::~TrayWatcher() {
    if (registered_) {
        bus_.unregisterService(serviceName);
        bus_.unregisterObject(objectPath);
    }
}
bool TrayWatcher::start(const QDBusConnection &bus) {
    bus_ = bus;
    if (!bus_.isConnected()) {
        error_ = "no session bus: " + bus_.lastError().message();
        return false;
    }
    if (!bus_.registerObject(objectPath, this,
                             QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals |
                                 QDBusConnection::ExportAllProperties)) {
        error_ = "cannot export the watcher: " + bus_.lastError().message();
        return false;
    }
    if (!bus_.registerService(serviceName)) {
        bus_.unregisterObject(objectPath);
        error_ = "another watcher owns " + QString(serviceName);
        return false;
    }
    registered_ = true;
    owners_ = new QDBusServiceWatcher(this);
    owners_->setConnection(bus_);
    owners_->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(owners_, &QDBusServiceWatcher::serviceUnregistered, this, &TrayWatcher::serviceGone);
    return true;
}
void TrayWatcher::RegisterStatusNotifierItem(const QString &item) {
    QString service = item, path = "/StatusNotifierItem";
    if (item.startsWith('/')) {
        service = message().service();
        path = item;
    }
    if (service.isEmpty() || service.size() > 255 || !trayValidPath(path)) {
        sendErrorReply(QDBusError::InvalidArgs, "not a bus name or an object path: " + item.left(100));
        return;
    }
    const QString key = service + path;
    if (items_.size() >= maxItems && !items_.contains(key)) {
        sendErrorReply(QDBusError::LimitsExceeded, "too many tray items");
        return;
    }
    whenOwned(service, [this, key, service] {
        if (items_.contains(key) || items_.size() >= maxItems)
            return;
        items_ << key;
        owners_->addWatchedService(service);
        Q_EMIT StatusNotifierItemRegistered(key);
    });
}
void TrayWatcher::RegisterStatusNotifierHost(const QString &service) {
    if (service.isEmpty() || service.size() > 255) {
        sendErrorReply(QDBusError::InvalidArgs, "not a bus name: " + service.left(100));
        return;
    }
    whenOwned(service, [this, service] { addHost(service); });
}
void TrayWatcher::addHost(const QString &service) {
    if (hosts_.contains(service) || !owners_)
        return;
    hosts_ << service;
    owners_->addWatchedService(service);
    Q_EMIT StatusNotifierHostRegistered();
}
// A name nobody owns would never be seen leaving, so it would stay listed for good.
void TrayWatcher::whenOwned(const QString &service, std::function<void()> add) {
    setDelayedReply(true);
    const QDBusMessage request = message();
    dbus::whenAnswered(
        bus_.interface()->asyncCall("NameHasOwner", service), this,
        [this, request, service, add = std::move(add)](QDBusPendingCallWatcher *watch) {
            const QDBusPendingReply<bool> owned = *watch;
            if (owned.isError() || !owned.value()) {
                bus_.send(request.createErrorReply(QDBusError::ServiceUnknown,
                                                   service.left(100) + " is not on the bus"));
                return;
            }
            add();
            bus_.send(request.createReply());
        });
}
void TrayWatcher::serviceGone(const QString &service) {
    QStringList gone;
    for (const auto &item : std::as_const(items_))
        if (item.startsWith(service + '/'))
            gone << item;
    for (const auto &item : gone) {
        items_.removeAll(item);
        Q_EMIT StatusNotifierItemUnregistered(item);
    }
    if (hosts_.removeAll(service) > 0)
        Q_EMIT StatusNotifierHostUnregistered();
    owners_->removeWatchedService(service);
}
