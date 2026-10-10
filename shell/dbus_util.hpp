// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusPendingCallWatcher>
#include <QFileInfo>
#include <QVariantMap>

// What the shell's D-Bus clients share.
namespace dbus {
// Calls `answered` with the call's watcher once `call` is answered, unless `context` is gone by
// then; the watcher is deleted after.
template <class Answered>
void whenAnswered(const QDBusPendingCall &call, QObject *context, Answered answered) {
    auto *watcher = new QDBusPendingCallWatcher(call, context);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, context,
                     [answered = std::move(answered)](QDBusPendingCallWatcher *done) mutable {
                         done->deleteLater();
                         answered(done);
                     });
}

// A variant holding a{sv}: a QVariantMap, or a D-Bus argument still to read as one.
inline QVariantMap map(const QVariant &value) {
    if (value.metaType() == QMetaType::fromType<QDBusArgument>())
        return qdbus_cast<QVariantMap>(value.value<QDBusArgument>());
    return value.toMap();
}

// Exports `object` on `bus` at `path`, the parts of it `exported` says, and takes the name
// `service` for it. Returns what went wrong, empty once both are done, naming the object `what` and
// whoever else may own the name `rival`; the object is withdrawn again when the name is taken.
inline QString serve(QDBusConnection &bus, const QString &service, const QString &path,
                     QObject *object, QDBusConnection::RegisterOptions exported, const QString &what,
                     const QString &rival) {
    if (!bus.isConnected())
        return "no session bus: " + bus.lastError().message();
    if (!bus.registerObject(path, object, exported))
        return "cannot export " + what + ": " + bus.lastError().message();
    if (!bus.registerService(service)) {
        bus.unregisterObject(path);
        return "another " + rival + " owns " + service;
    }
    return {};
}

// Whether the session has a bus to connect to. Without an address libdbus would start a bus of its
// own ("autolaunch") that no other program knows of.
inline bool haveSessionBus() {
    return !qEnvironmentVariableIsEmpty("DBUS_SESSION_BUS_ADDRESS") ||
           QFileInfo::exists(qEnvironmentVariable("XDG_RUNTIME_DIR") + "/bus");
}
} // namespace dbus
