// SPDX-License-Identifier: GPL-3.0-or-later
#include "network_manager.hpp"
#include "dbus_util.hpp"
#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <algorithm>

namespace {
constexpr auto service = "org.freedesktop.NetworkManager";
constexpr auto managerPath = "/org/freedesktop/NetworkManager";
constexpr auto managerInterface = "org.freedesktop.NetworkManager";
constexpr auto deviceInterface = "org.freedesktop.NetworkManager.Device";
constexpr auto wirelessInterface = "org.freedesktop.NetworkManager.Device.Wireless";
constexpr auto accessPointInterface = "org.freedesktop.NetworkManager.AccessPoint";
constexpr auto activeInterface = "org.freedesktop.NetworkManager.Connection.Active";
constexpr auto settingsPath = "/org/freedesktop/NetworkManager/Settings";
constexpr auto settingsInterface = "org.freedesktop.NetworkManager.Settings";
constexpr auto connectionInterface = "org.freedesktop.NetworkManager.Settings.Connection";
constexpr auto propertiesInterface = "org.freedesktop.DBus.Properties";
// NMDeviceType, NMDeviceState and NMActiveConnectionState, as far as they matter here.
constexpr uint deviceWifi = 2;
constexpr uint devicePrepare = 40, deviceActivated = 100;
constexpr uint activeActivated = 2, activeDeactivated = 4;

// A connection's settings, a{sa{sv}}.
using Settings = QMap<QString, QVariantMap>;

QStringList pathList(const QList<QDBusObjectPath> &objects) {
    QStringList list;
    for (const auto &object : objects)
        if (object.path() != "/")
            list << object.path();
    return list;
}
// A value as it is kept: an object path as a string ("" for "/", which names none), a list of them
// as a list of strings, anything else as it came.
QVariant plain(const QVariant &value) {
    if (value.metaType() == QMetaType::fromType<QDBusObjectPath>()) {
        const auto path = value.value<QDBusObjectPath>().path();
        return path == "/" ? QString() : path;
    }
    if (value.metaType() == QMetaType::fromType<QList<QDBusObjectPath>>())
        return pathList(value.value<QList<QDBusObjectPath>>());
    if (value.metaType() == QMetaType::fromType<QDBusArgument>()) {
        const auto argument = value.value<QDBusArgument>();
        if (argument.currentSignature() == "ao")
            return pathList(qdbus_cast<QList<QDBusObjectPath>>(argument));
    }
    return value;
}
QVariantMap settled(const QVariantMap &properties) {
    QVariantMap values;
    for (auto it = properties.constBegin(); it != properties.constEnd(); ++it)
        values[it.key()] = plain(it.value());
    return values;
}
QDBusMessage call(const QString &path, const QString &interface, const QString &method) {
    return QDBusMessage::createMethodCall(service, path, interface, method);
}
} // namespace

NetworkManager::NetworkManager(const QDBusConnection &bus, QObject *parent) : Wifi(parent), bus_(bus) {
    qDBusRegisterMetaType<Settings>();
    // Strengths change often and objects come in bursts; one update follows them all.
    publish_.setSingleShot(true);
    publish_.setInterval(30);
    connect(&publish_, &QTimer::timeout, this, &NetworkManager::publish);
    if (!bus_.isConnected())
        return;
    auto *watcher = new QDBusServiceWatcher(service, bus_, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &, const QString &owner) {
                stop();
                if (!owner.isEmpty())
                    start();
            });
    bus_.connect(service, QString(), propertiesInterface, "PropertiesChanged", this,
                 SLOT(propertiesChanged(QDBusMessage)));
    bus_.connect(service, settingsPath, settingsInterface, "NewConnection", this, SLOT(connectionsChanged()));
    bus_.connect(service, settingsPath, settingsInterface, "ConnectionRemoved", this, SLOT(connectionsChanged()));
    bus_.connect(service, QString(), connectionInterface, "Updated", this, SLOT(connectionsChanged()));
    auto ask = QDBusMessage::createMethodCall("org.freedesktop.DBus", "/org/freedesktop/DBus",
                                              "org.freedesktop.DBus", "NameHasOwner");
    ask << QString(service);
    dbus::whenAnswered(bus_.asyncCall(ask), this, [this](QDBusPendingCallWatcher *done) {
        if (!done->isError() && done->reply().arguments().value(0).toBool() && !running_)
            start();
    });
}
void NetworkManager::start() {
    running_ = true;
    getAll(managerPath, managerInterface, [this](const QVariantMap &properties) {
        manager_ = properties;
        managerRead_ = true;
        syncDevices();
        syncActives();
        publish_.start();
    });
    connectionsChanged();
}
void NetworkManager::stop() {
    ++generation_;
    running_ = managerRead_ = false;
    manager_.clear();
    devices_.clear();
    accessPoints_.clear();
    actives_.clear();
    connections_.clear();
    pending_.reset();
    publish_.start();
}
void NetworkManager::getAll(const QString &path, const QString &interface,
                            std::function<void(const QVariantMap &)> done) {
    auto message = call(path, propertiesInterface, "GetAll");
    message << interface;
    const int generation = generation_;
    dbus::whenAnswered(bus_.asyncCall(message), this,
                       [this, generation, done = std::move(done)](QDBusPendingCallWatcher *answer) {
                           if (generation == generation_)
                               done(answer->isError()
                                        ? QVariantMap()
                                        : settled(dbus::map(answer->reply().arguments().value(0))));
                       });
}

void NetworkManager::syncDevices() {
    const auto listed = manager_.value("Devices").toStringList();
    for (auto it = devices_.begin(); it != devices_.end();)
        it = listed.contains(it.key()) ? std::next(it) : devices_.erase(it);
    for (const auto &device : listed)
        if (!devices_.contains(device))
            readDevice(device);
    syncAccessPoints();
}
void NetworkManager::readDevice(const QString &path) {
    devices_[path];
    getAll(path, deviceInterface, [this, path](const QVariantMap &properties) {
        auto it = devices_.find(path);
        if (it == devices_.end())
            return;
        it->device = properties;
        if (properties.value("DeviceType").toUInt() == deviceWifi)
            getAll(path, wirelessInterface, [this, path](const QVariantMap &wireless) {
                auto it = devices_.find(path);
                if (it == devices_.end())
                    return;
                it->wireless = wireless;
                syncAccessPoints();
                publish_.start();
            });
        publish_.start();
    });
}
void NetworkManager::syncAccessPoints() {
    QStringList listed;
    for (const auto &device : std::as_const(devices_))
        listed += device.wireless.value("AccessPoints").toStringList();
    for (auto it = accessPoints_.begin(); it != accessPoints_.end();)
        it = listed.contains(it.key()) ? std::next(it) : accessPoints_.erase(it);
    for (const auto &point : listed)
        if (!accessPoints_.contains(point))
            readAccessPoint(point);
}
void NetworkManager::readAccessPoint(const QString &path) {
    accessPoints_[path];
    getAll(path, accessPointInterface, [this, path](const QVariantMap &properties) {
        auto it = accessPoints_.find(path);
        if (it == accessPoints_.end())
            return;
        *it = properties;
        publish_.start();
    });
}
void NetworkManager::syncActives() {
    const auto listed = manager_.value("ActiveConnections").toStringList();
    for (auto it = actives_.begin(); it != actives_.end();) {
        if (listed.contains(it.key())) {
            ++it;
            continue;
        }
        // The connection asked for went without being active.
        if (pending_ && pending_->active == it.key())
            fail();
        it = actives_.erase(it);
    }
    for (const auto &active : listed)
        if (!actives_.contains(active))
            readActive(active);
}
void NetworkManager::readActive(const QString &path) {
    actives_[path];
    getAll(path, activeInterface, [this, path](const QVariantMap &properties) {
        auto it = actives_.find(path);
        if (it == actives_.end())
            return;
        *it = properties;
        checkPending();
        publish_.start();
    });
}
void NetworkManager::connectionsChanged() {
    if (!running_)
        return;
    const int generation = generation_;
    dbus::whenAnswered(
        bus_.asyncCall(call(settingsPath, settingsInterface, "ListConnections")), this,
        [this, generation](QDBusPendingCallWatcher *answer) {
            if (generation != generation_ || answer->isError())
                return;
            const auto listed = plain(answer->reply().arguments().value(0)).toStringList();
            for (auto it = connections_.begin(); it != connections_.end();)
                it = listed.contains(it.key()) ? std::next(it) : connections_.erase(it);
            // Each again, as Updated may have changed one.
            for (const auto &connection : listed)
                readConnection(connection);
            publish_.start();
        });
}
void NetworkManager::readConnection(const QString &path) {
    const int generation = generation_;
    dbus::whenAnswered(
        bus_.asyncCall(call(path, connectionInterface, "GetSettings")), this,
        [this, generation, path](QDBusPendingCallWatcher *answer) {
            if (generation != generation_ || answer->isError())
                return;
            const auto settings = qdbus_cast<Settings>(answer->reply().arguments().value(0));
            connections_[path] = {
                settings.value("connection").value("type").toString(),
                QString::fromUtf8(settings.value("802-11-wireless").value("ssid").toByteArray())};
            publish_.start();
        });
}

void NetworkManager::propertiesChanged(const QDBusMessage &message) {
    const auto arguments = message.arguments();
    if (!running_ || arguments.size() < 2)
        return;
    const auto interface = arguments[0].toString();
    const auto changed = settled(dbus::map(arguments[1]));
    const auto path = message.path();
    auto merge = [&changed](QVariantMap &into) {
        for (auto it = changed.constBegin(); it != changed.constEnd(); ++it)
            into[it.key()] = it.value();
    };
    if (path == managerPath && interface == managerInterface) {
        if (!managerRead_)
            return;
        merge(manager_);
        if (changed.contains("Devices"))
            syncDevices();
        if (changed.contains("ActiveConnections"))
            syncActives();
    } else if (auto device = devices_.find(path); device != devices_.end()) {
        if (interface == deviceInterface) {
            merge(device->device);
        } else if (interface == wirelessInterface) {
            merge(device->wireless);
            if (changed.contains("AccessPoints"))
                syncAccessPoints();
        }
    } else if (auto point = accessPoints_.find(path); point != accessPoints_.end()) {
        if (interface == accessPointInterface)
            merge(*point);
    } else if (auto active = actives_.find(path); active != actives_.end()) {
        if (interface == activeInterface) {
            merge(*active);
            checkPending();
        }
    } else if (path == settingsPath && changed.contains("Connections")) {
        connectionsChanged();
        return;
    } else {
        return;
    }
    publish_.start();
}

void NetworkManager::checkPending() {
    if (!pending_ || pending_->active.isEmpty())
        return;
    const auto it = actives_.constFind(pending_->active);
    if (it == actives_.constEnd() || !it->contains("State"))
        return;
    const uint state = it->value("State").toUInt();
    if (state == activeActivated)
        pending_.reset();
    else if (state == activeDeactivated)
        fail();
}
void NetworkManager::fail(const QString &error) {
    if (!pending_)
        return;
    const auto pending = *pending_;
    pending_.reset();
    // A connection added for a network that would not take it is not kept: a wrong password would
    // be tried again and again.
    if (!pending.added.isEmpty())
        bus_.asyncCall(call(pending.added, connectionInterface, "Delete"));
    const QString why = !error.isEmpty() ? ": " + error
                        : pending.password ? QStringLiteral(": the password may be wrong") : QString();
    connectionFailed(pending.ssid, "Could not connect to " + pending.ssid + why, pending.password);
}
QString NetworkManager::ssidOf(const QString &accessPoint) const {
    return QString::fromUtf8(accessPoints_.value(accessPoint).value("Ssid").toByteArray());
}
QStringList NetworkManager::wifiDevices(const QString &ssid) const {
    QStringList seeing, others;
    for (auto it = devices_.constBegin(); it != devices_.constEnd(); ++it) {
        if (it->device.value("DeviceType").toUInt() != deviceWifi)
            continue;
        const auto points = it->wireless.value("AccessPoints").toStringList();
        const bool sees = std::any_of(points.begin(), points.end(),
                                      [&](const QString &point) { return ssidOf(point) == ssid; });
        (sees && !ssid.isEmpty() ? seeing : others) << it.key();
    }
    return seeing + others;
}

void NetworkManager::publish() {
    State state;
    state.available = running_ && managerRead_;
    state.enabled = manager_.value("WirelessEnabled").toBool();
    state.hardwareEnabled = manager_.value("WirelessHardwareEnabled", true).toBool();
    for (const auto &device : std::as_const(devices_)) {
        if (device.device.value("DeviceType").toUInt() != deviceWifi)
            continue;
        state.hasWifi = true;
        for (const auto &point : device.wireless.value("AccessPoints").toStringList()) {
            const auto properties = accessPoints_.value(point);
            if (properties.isEmpty())
                continue;
            state.accessPoints.push_back({ssidOf(point), properties.value("Strength").toInt(),
                                          security(properties.value("Flags").toUInt(), properties.value("WpaFlags").toUInt(),
                                                   properties.value("RsnFlags").toUInt())});
        }
        const uint deviceState = device.device.value("State").toUInt();
        const auto activePoint = device.wireless.value("ActiveAccessPoint").toString();
        if (deviceState == deviceActivated && !activePoint.isEmpty()) {
            state.ssid = ssidOf(activePoint);
            state.strength = accessPoints_.value(activePoint).value("Strength").toInt();
        } else if (deviceState >= devicePrepare && deviceState < deviceActivated) {
            // The network of the connection it activates.
            const auto active = actives_.value(device.device.value("ActiveConnection").toString());
            const auto ssid = connections_.value(active.value("Connection").toString()).ssid;
            state.activating = !ssid.isEmpty() ? ssid : active.value("Id").toString();
        }
    }
    for (const auto &connection : std::as_const(connections_))
        if (connection.type == "802-11-wireless" && !connection.ssid.isEmpty() && !state.known.contains(connection.ssid))
            state.known << connection.ssid;
    const auto primary = actives_.value(manager_.value("PrimaryConnection").toString());
    if (!primary.isEmpty()) {
        const auto type = primary.value("Type").toString();
        state.primaryType = type == "802-11-wireless" ? "wifi" : type == "802-3-ethernet" ? "ethernet" : "other";
        state.primaryName = state.primaryType == "wifi" && !state.ssid.isEmpty() ? state.ssid : primary.value("Id").toString();
    }
    update(std::move(state));
}

void NetworkManager::sendEnabled(bool enabled) {
    auto message = call(managerPath, propertiesInterface, "Set");
    message << QString(managerInterface) << QStringLiteral("WirelessEnabled") << QVariant::fromValue(QDBusVariant(enabled));
    dbus::whenAnswered(
        bus_.asyncCall(message), this, [this, enabled](QDBusPendingCallWatcher *answer) {
            if (!answer->isError())
                return;
            Q_EMIT failed(QString("Could not turn Wi-Fi %1: ").arg(enabled ? "on" : "off") +
                          answer->error().message());
            // What NetworkManager has shows again.
            publish();
        });
}
void NetworkManager::sendScan() {
    for (const auto &device : wifiDevices()) {
        auto message = call(device, wirelessInterface, "RequestScan");
        message << QVariantMap();
        // NetworkManager refuses a scan while one runs or one ran a moment ago; that is no failure.
        bus_.asyncCall(message);
    }
}
void NetworkManager::sendConnect(const QString &ssid, const QString &security, const QString &password) {
    const auto devices = wifiDevices(ssid);
    if (devices.isEmpty()) {
        connectionFailed(ssid, "Could not connect to " + ssid + ": there is no Wi-Fi device", false);
        return;
    }
    const QString device = devices.first();
    // Its strongest access point there, for NetworkManager to take; "/" lets it pick one.
    QString point = "/";
    int strongest = -1;
    for (const auto &candidate : devices_.value(device).wireless.value("AccessPoints").toStringList())
        if (ssidOf(candidate) == ssid && accessPoints_.value(candidate).value("Strength").toInt() > strongest) {
            point = candidate;
            strongest = accessPoints_.value(candidate).value("Strength").toInt();
        }
    QString known;
    for (auto it = connections_.constBegin(); it != connections_.constEnd(); ++it)
        if (it->type == "802-11-wireless" && it->ssid == ssid)
            known = it.key();
    QDBusMessage message;
    if (!known.isEmpty()) {
        message = call(managerPath, managerInterface, "ActivateConnection");
        message << QVariant::fromValue(QDBusObjectPath(known)) << QVariant::fromValue(QDBusObjectPath(device))
                << QVariant::fromValue(QDBusObjectPath(point));
    } else {
        // What NetworkManager does not fill in from the access point itself.
        Settings settings;
        settings["connection"] = {{"id", ssid}, {"type", "802-11-wireless"}};
        settings["802-11-wireless"] = {{"ssid", ssid.toUtf8()}, {"mode", "infrastructure"}};
        if (security == "wpa-psk" || security == "sae")
            settings["802-11-wireless-security"] = {{"key-mgmt", security}, {"psk", password}};
        else if (security == "owe")
            settings["802-11-wireless-security"] = {{"key-mgmt", "owe"}};
        message = call(managerPath, managerInterface, "AddAndActivateConnection");
        message << QVariant::fromValue(settings) << QVariant::fromValue(QDBusObjectPath(device))
                << QVariant::fromValue(QDBusObjectPath(point));
    }
    pending_ = Pending{ssid, {}, {}, !password.isEmpty()};
    const int generation = generation_;
    const bool adding = known.isEmpty();
    dbus::whenAnswered(bus_.asyncCall(message), this,
                       [this, generation, ssid, adding](QDBusPendingCallWatcher *answer) {
                           if (generation != generation_ || !pending_ || pending_->ssid != ssid)
                               return;
                           if (answer->isError()) {
                               fail(answer->error().message());
                               return;
                           }
                           const auto arguments = answer->reply().arguments();
                           // AddAndActivateConnection answers with the connection added, then the
                           // active one.
                           pending_->added =
                               adding ? plain(arguments.value(0)).toString() : QString();
                           pending_->active = plain(arguments.value(adding ? 1 : 0)).toString();
                           checkPending();
                       });
}
void NetworkManager::sendDisconnect() {
    for (const auto &device : wifiDevices()) {
        const uint state = devices_.value(device).device.value("State").toUInt();
        if (state < devicePrepare || state > deviceActivated)
            continue;
        dbus::whenAnswered(bus_.asyncCall(call(device, deviceInterface, "Disconnect")), this,
                           [this](QDBusPendingCallWatcher *answer) {
                               if (answer->isError())
                                   Q_EMIT failed("Could not disconnect: " +
                                                 answer->error().message());
                           });
    }
}

std::unique_ptr<Wifi> makeWifi() {
    return std::make_unique<NetworkManager>(QDBusConnection::systemBus());
}
