// SPDX-License-Identifier: GPL-3.0-or-later
// Wi-Fi against a stand-in NetworkManager on a private bus (a dbus-daemon this test starts and
// kills) standing in for the system bus, never the real one: a wired device and a Wi-Fi one, the
// access points it sees, the connections active and those NetworkManager knows. Its coming and
// going, the radio, signals changing, connecting to known, open and secured networks (a refused
// password asked again, the connection added for it removed), and disconnecting.
#include "fake_dbus.hpp"
#include "network_manager.hpp"
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QSignalSpy>
#include <QTest>
#include <map>
#include <memory>

namespace {
using Settings = QMap<QString, QVariantMap>;
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
constexpr auto wired = "/org/freedesktop/NetworkManager/Devices/1";
constexpr auto wireless = "/org/freedesktop/NetworkManager/Devices/2";

QVariant objects(const QStringList &paths) {
    QList<QDBusObjectPath> list;
    for (const auto &path : paths)
        list << QDBusObjectPath(path);
    return QVariant::fromValue(list);
}
QVariant object(const QString &path) { return QVariant::fromValue(QDBusObjectPath(path.isEmpty() ? "/" : path)); }
QStringList pathsOf(const QVariant &value) {
    QStringList list;
    for (const auto &path : value.value<QList<QDBusObjectPath>>())
        list << path.path();
    return list;
}

// NetworkManager as far as the shell asks it: a wired device without a cable and a Wi-Fi one
// connected to Home, six access points (Home twice, an open Cafe, Office with WPA3, Corp with
// 802.1X, and a hidden one), and the connections Wired connection 1, Home and Cafe.
class FakeNetworkManager {
  public:
    // What the methods were asked: "activate CONNECTION AP", "add SSID KEYMGMT PSK", "delete
    // CONNECTION", "disconnect DEVICE", "scan DEVICE".
    QStringList calls;
    // An error AddAndActivateConnection answers with, while set.
    QString refuseAdd;

    explicit FakeNetworkManager(fakedbus::Bus &bus) : bus_(bus.connect()) {
        manager = add(managerPath, managerInterface,
                      {{"WirelessEnabled", true},
                       {"WirelessHardwareEnabled", true},
                       {"Devices", objects({wired, wireless})},
                       {"ActiveConnections", objects({})},
                       {"PrimaryConnection", object({})}});
        manager->methods = [this](const QDBusMessage &call, QDBusConnection &connection) {
            return managerCall(call, connection);
        };
        settings = add(settingsPath, settingsInterface, {{"Connections", objects({})}});
        settings->methods = [this](const QDBusMessage &call, QDBusConnection &connection) {
            if (call.member() != "ListConnections")
                return false;
            connection.send(call.createReply(settings->properties[settingsInterface]["Connections"]));
            return true;
        };
        add(wired, deviceInterface,
            {{"Interface", "eth0"}, {"DeviceType", 1u}, {"State", 20u}, {"ActiveConnection", object({})}});
        auto *wifi = add(wireless, deviceInterface,
                         {{"Interface", "wlan0"}, {"DeviceType", 2u}, {"State", 30u}, {"ActiveConnection", object({})}});
        wifi->properties[wirelessInterface] = {{"AccessPoints", objects({})}, {"ActiveAccessPoint", object({})}};
        wifi->methods = [this](const QDBusMessage &call, QDBusConnection &connection) {
            if (call.member() == "Disconnect") {
                calls << QString("disconnect ") + wireless;
                connection.send(call.createReply());
                deactivate(objects_[wireless]->properties[deviceInterface]["ActiveConnection"].value<QDBusObjectPath>().path());
                return true;
            }
            if (call.member() == "RequestScan") {
                calls << QString("scan ") + wireless;
                connection.send(call.createReply());
                return true;
            }
            return false;
        };
        for (const auto &[ssid, strength, flags, rsn] : {std::tuple{"Home", 80, 1u, 0x188u},
                                                         {"Home", 30, 1u, 0x188u},
                                                         {"Cafe", 40, 0u, 0u},
                                                         {"Office", 60, 1u, 0x408u},
                                                         {"Corp", 70, 1u, 0x208u},
                                                         {"", 90, 1u, 0x188u}})
            accessPoint(ssid, strength, flags, rsn);
        addConnection("Wired connection 1", "802-3-ethernet", {});
        home = addConnection("Home", "802-11-wireless", "Home");
        cafe = addConnection("Cafe", "802-11-wireless", "Cafe");
        const auto active = activate(home, points.front(), true);
        setDevice(2, active, points.front());
        manager->set(managerInterface, "PrimaryConnection", object(active));
    }
    ~FakeNetworkManager() {
        bus_.unregisterService(service);
        objects_.clear();
        QDBusConnection::disconnectFromBus(bus_.name());
    }
    bool start() { return bus_.registerService(service); }
    void stop() { bus_.unregisterService(service); }

    fakedbus::Object *manager = nullptr, *settings = nullptr;
    QStringList points;
    QString home, cafe;

    fakedbus::Object *at(const QString &path) { return objects_.at(path).get(); }
    // A new access point the Wi-Fi device sees.
    QString accessPoint(const QString &ssid, int strength, uint flags, uint rsn) {
        const auto path = QString("/org/freedesktop/NetworkManager/AccessPoint/%1").arg(++serial_);
        add(path, accessPointInterface,
            {{"Ssid", ssid.toUtf8()}, {"Strength", QVariant::fromValue(uchar(strength))}, {"Flags", flags},
             {"WpaFlags", 0u}, {"RsnFlags", rsn}, {"Frequency", 2437u}});
        points << path;
        objects_[wireless]->set(wirelessInterface, "AccessPoints", objects(points));
        return path;
    }
    // The Wi-Fi device in `state` (2 for activated, 1 activating, 0 disconnected), its active
    // connection and access point.
    void setDevice(int state, const QString &active, const QString &point) {
        objects_[wireless]->set(deviceInterface, {{"State", state == 2 ? 100u : state == 1 ? 50u : 30u},
                                                  {"ActiveConnection", object(active)}});
        objects_[wireless]->set(wirelessInterface, "ActiveAccessPoint", object(point));
    }
    // An active connection of `connection` on the Wi-Fi device, activated or activating.
    QString activate(const QString &connection, const QString &point, bool activated) {
        const auto path = QString("/org/freedesktop/NetworkManager/ActiveConnection/%1").arg(++serial_);
        const auto settings = connections_.at(connection);
        add(path, activeInterface,
            {{"Id", settings.value("connection").value("id")},
             {"Type", settings.value("connection").value("type")},
             {"State", activated ? 2u : 1u},
             {"Connection", object(connection)},
             {"SpecificObject", object(point)},
             {"Devices", objects({wireless})}});
        auto list = pathsOf(manager->properties[managerInterface]["ActiveConnections"]);
        manager->set(managerInterface, "ActiveConnections", objects(list << path));
        return path;
    }
    // The connection becomes active, the Wi-Fi device's, and the way out.
    void succeed(const QString &active) {
        at(active)->set(activeInterface, "State", 2u);
        const auto point = at(active)->properties[activeInterface]["SpecificObject"].value<QDBusObjectPath>().path();
        setDevice(2, active, point);
        manager->set(managerInterface, "PrimaryConnection", object(active));
    }
    // It fails or is ended: deactivated, then gone.
    void deactivate(const QString &active) {
        if (active.isEmpty() || active == "/" || !objects_.count(active))
            return;
        at(active)->set(activeInterface, "State", 4u);
        auto list = pathsOf(manager->properties[managerInterface]["ActiveConnections"]);
        list.removeAll(active);
        manager->set(managerInterface, "ActiveConnections", objects(list));
        setDevice(0, {}, {});
        manager->set(managerInterface, "PrimaryConnection", object({}));
        objects_.erase(active);
    }
    QString lastActive() const { return lastActive_; }
    QString addConnection(const QString &id, const QString &type, const QString &ssid) {
        const auto path = QString("/org/freedesktop/NetworkManager/Settings/%1").arg(++serial_);
        Settings settings;
        settings["connection"] = {{"id", id}, {"type", type}};
        if (!ssid.isEmpty())
            settings["802-11-wireless"] = {{"ssid", ssid.toUtf8()}};
        return addConnection(path, settings);
    }

  private:
    QDBusConnection bus_;
    std::map<QString, std::unique_ptr<fakedbus::Object>> objects_;
    std::map<QString, Settings> connections_;
    int serial_ = 0;
    QString lastActive_;

    fakedbus::Object *add(const QString &path, const QString &interface, const QVariantMap &properties) {
        auto object = std::make_unique<fakedbus::Object>(bus_, path);
        object->properties[interface] = properties;
        auto *raw = object.get();
        objects_[path] = std::move(object);
        return raw;
    }
    QString addConnection(const QString &path, const Settings &values) {
        connections_[path] = values;
        auto *connection = add(path, connectionInterface, {{"Unsaved", false}});
        connection->methods = [this, path](const QDBusMessage &call, QDBusConnection &bus) {
            if (call.member() == "GetSettings") {
                bus.send(call.createReply(QVariant::fromValue(connections_.at(path))));
                return true;
            }
            if (call.member() == "Delete") {
                calls << "delete " + path;
                bus.send(call.createReply());
                removeConnection(path);
                return true;
            }
            return false;
        };
        auto list = pathsOf(settings->properties[settingsInterface]["Connections"]);
        settings->set(settingsInterface, "Connections", objects(list << path));
        settings->emitSignal(settingsInterface, "NewConnection", {object(path)});
        return path;
    }
    void removeConnection(const QString &path) {
        auto list = pathsOf(settings->properties[settingsInterface]["Connections"]);
        list.removeAll(path);
        settings->set(settingsInterface, "Connections", objects(list));
        settings->emitSignal(settingsInterface, "ConnectionRemoved", {object(path)});
        connections_.erase(path);
        objects_.erase(path);
    }
    bool managerCall(const QDBusMessage &call, QDBusConnection &connection) {
        const auto arguments = call.arguments();
        if (call.member() == "ActivateConnection") {
            const auto settings = arguments.value(0).value<QDBusObjectPath>().path();
            const auto point = arguments.value(2).value<QDBusObjectPath>().path();
            calls << "activate " + settings + " " + point;
            lastActive_ = activate(settings, point, false);
            setDevice(1, lastActive_, {});
            connection.send(call.createReply(object(lastActive_)));
            return true;
        }
        if (call.member() == "AddAndActivateConnection") {
            const auto settings = qdbus_cast<Settings>(arguments.value(0));
            const auto security = settings.value("802-11-wireless-security");
            calls << QString("add %1 %2 %3")
                         .arg(QString::fromUtf8(settings.value("802-11-wireless").value("ssid").toByteArray()),
                              security.value("key-mgmt").toString(), security.value("psk").toString())
                         .trimmed();
            if (!refuseAdd.isEmpty()) {
                connection.send(call.createErrorReply("org.freedesktop.NetworkManager.Settings.Connection.InvalidProperty",
                                                      refuseAdd));
                return true;
            }
            const auto path = QString("/org/freedesktop/NetworkManager/Settings/%1").arg(++serial_);
            addConnection(path, settings);
            lastActive_ = activate(path, arguments.value(2).value<QDBusObjectPath>().path(), false);
            setDevice(1, lastActive_, {});
            connection.send(call.createReply(QVariantList{object(path), object(lastActive_)}));
            return true;
        }
        return false;
    }
};
} // namespace

class NetworkManagerDbusTest : public QObject {
    Q_OBJECT
    fakedbus::Bus bus_;

    static QVariantMap network(const Wifi &wifi, const QString &ssid) {
        for (const auto &entry : wifi.networks()->all())
            if (entry.ssid == ssid)
                return {{"strength", entry.strength}, {"security", entry.security}, {"secured", entry.secured},
                        {"known", entry.known}, {"active", entry.active}, {"connecting", entry.connecting}};
        return {};
    }
    static QStringList names(const Wifi &wifi) {
        QStringList list;
        for (const auto &entry : wifi.networks()->all())
            list << entry.ssid;
        return list;
    }

  private Q_SLOTS:
    void initTestCase() {
        if (!fakedbus::Bus::available())
            QSKIP("dbus-daemon is not installed");
        qDBusRegisterMetaType<Settings>();
        QVERIFY(bus_.start());
    }
    // Read whole once NetworkManager is there, and nothing once it goes.
    void comesAndGoes() {
        NetworkManager wifi(bus_.connect());
        QTest::qWait(50);
        QVERIFY(!wifi.available());
        FakeNetworkManager manager(bus_);
        QVERIFY(manager.start());
        QTRY_VERIFY(wifi.available() && wifi.ssid() == "Home");
        QVERIFY(wifi.hasWifi() && wifi.enabled() && wifi.hardwareEnabled());
        QCOMPARE(wifi.strength(), 80);
        QCOMPARE(wifi.primaryType(), QString("wifi"));
        QCOMPARE(wifi.primaryName(), QString("Home"));
        QTRY_COMPARE(names(wifi), (QStringList{"Home", "Corp", "Office", "Cafe"}));
        QVERIFY(network(wifi, "Home").value("known").toBool() && network(wifi, "Home").value("active").toBool());
        QVERIFY(network(wifi, "Cafe").value("known").toBool());
        QCOMPARE(network(wifi, "Office").value("security").toString(), QString("sae"));
        QCOMPARE(network(wifi, "Corp").value("security").toString(), QString("enterprise"));
        QVERIFY(wifi.needsPassword("Office"));
        manager.stop();
        QTRY_VERIFY(!wifi.available());
        QVERIFY(wifi.networks()->count() == 0);
        QCOMPARE(wifi.primaryType(), QString());
    }
    // Signals, access points coming and going, and the way out changing.
    void follows() {
        FakeNetworkManager manager(bus_);
        QVERIFY(manager.start());
        NetworkManager wifi(bus_.connect());
        QTRY_VERIFY(wifi.ssid() == "Home");
        manager.at(manager.points.front())->set(accessPointInterface, "Strength", QVariant::fromValue(uchar(35)));
        QTRY_COMPARE(wifi.strength(), 35);
        // The other Home is the stronger now.
        QCOMPARE(network(wifi, "Home").value("strength").toInt(), 35);
        manager.accessPoint("Library", 99, 0, 0);
        QTRY_COMPARE(names(wifi).value(1), QString("Library"));
        QVERIFY(!network(wifi, "Library").value("secured").toBool());
        manager.at(wireless)->set(wirelessInterface, "AccessPoints", objects(manager.points.mid(0, 3)));
        QTRY_COMPARE(names(wifi), (QStringList{"Home", "Cafe"}));
        manager.manager->set(managerInterface, "WirelessHardwareEnabled", false);
        QTRY_VERIFY(!wifi.hardwareEnabled());
    }
    // The radio, through NetworkManager's WirelessEnabled; a refusal is said and undone.
    void radio() {
        FakeNetworkManager manager(bus_);
        QVERIFY(manager.start());
        NetworkManager wifi(bus_.connect());
        QTRY_VERIFY(wifi.available() && wifi.enabled());
        QSignalSpy failed(&wifi, &Wifi::failed);
        wifi.setEnabled(false);
        QVERIFY(!wifi.enabled());
        QTRY_COMPARE(manager.manager->calls, QStringList{"Set org.freedesktop.NetworkManager.WirelessEnabled false"});
        QTRY_VERIFY(!manager.manager->properties[managerInterface]["WirelessEnabled"].toBool());
        QTest::qWait(50);
        QVERIFY(!wifi.enabled());
        manager.manager->setter = [](const QString &, const QString &, const QVariant &) { return false; };
        wifi.setEnabled(true);
        QVERIFY(wifi.enabled());
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY2(failed.at(0).at(0).toString().startsWith("Could not turn Wi-Fi on: "),
                 qPrintable(failed.at(0).at(0).toString()));
        QTRY_VERIFY(!wifi.enabled());
        wifi.scan(); // the radio is off
        manager.manager->setter = nullptr;
        manager.manager->set(managerInterface, "WirelessEnabled", true);
        QTRY_VERIFY(wifi.enabled());
        wifi.scan();
        QTRY_COMPARE(manager.calls, QStringList{QString("scan ") + wireless});
    }
    // A known network is activated as NetworkManager has it, on its strongest access point.
    void knownNetwork() {
        FakeNetworkManager manager(bus_);
        QVERIFY(manager.start());
        NetworkManager wifi(bus_.connect());
        QTRY_VERIFY(wifi.ssid() == "Home" && wifi.canConnect("Cafe"));
        wifi.connectTo("Cafe");
        QCOMPARE(wifi.connecting(), QString("Cafe"));
        QTRY_COMPARE(manager.calls, QStringList{"activate " + manager.cafe + " " + manager.points[2]});
        QTRY_COMPARE(wifi.ssid(), QString());
        QCOMPARE(wifi.connecting(), QString("Cafe"));
        manager.succeed(manager.lastActive());
        QTRY_COMPARE(wifi.ssid(), QString("Cafe"));
        QCOMPARE(wifi.connecting(), QString());
        QCOMPARE(wifi.primaryName(), QString("Cafe"));
        QCOMPARE(names(wifi).first(), QString("Cafe"));
        // Disconnecting asks the device, and nothing reconnects.
        wifi.disconnectNetwork();
        QTRY_COMPARE(manager.calls.last(), QString("disconnect ") + wireless);
        QTRY_COMPARE(wifi.ssid(), QString());
        QCOMPARE(wifi.primaryType(), QString());
    }
    // A network NetworkManager does not know is added with its key; a refused password is asked
    // for again and the connection added for it removed; the right one connects and is kept.
    void newNetwork() {
        FakeNetworkManager manager(bus_);
        QVERIFY(manager.start());
        NetworkManager wifi(bus_.connect());
        QTRY_VERIFY(wifi.ssid() == "Home" && wifi.needsPassword("Office"));
        QSignalSpy failed(&wifi, &Wifi::failed);
        QSignalSpy rejected(&wifi, &Wifi::passwordRejected);
        wifi.connectTo("Office", "wrong one");
        QTRY_COMPARE(manager.calls, QStringList{"add Office sae wrong one"});
        QTRY_VERIFY(network(wifi, "Office").value("known").toBool());
        const auto added = manager.lastActive();
        manager.deactivate(added);
        QTRY_COMPARE(rejected.count(), 1);
        QCOMPARE(rejected.at(0).at(0).toString(), QString("Office"));
        QCOMPARE(failed.count(), 1);
        QVERIFY2(failed.at(0).at(0).toString().startsWith("Could not connect to Office"),
                 qPrintable(failed.at(0).at(0).toString()));
        QTRY_VERIFY(manager.calls.size() == 2 && manager.calls[1].startsWith("delete "));
        QTRY_VERIFY(!network(wifi, "Office").value("known").toBool());
        QCOMPARE(wifi.connecting(), QString());
        wifi.connectTo("Office", "right one");
        QTRY_COMPARE(manager.calls.size(), 3);
        QCOMPARE(manager.calls[2], QString("add Office sae right one"));
        QTRY_COMPARE(wifi.connecting(), QString("Office"));
        manager.succeed(manager.lastActive());
        QTRY_COMPARE(wifi.ssid(), QString("Office"));
        QVERIFY(network(wifi, "Office").value("known").toBool());
        QVERIFY(!wifi.needsPassword("Office"));
        // An open one takes no key; NetworkManager's refusal is said as it gave it.
        manager.refuseAdd = "802-11-wireless.ssid: property is invalid";
        manager.accessPoint("Library", 50, 0, 0);
        QTRY_VERIFY(wifi.canConnect("Library"));
        wifi.connectTo("Library");
        QTRY_COMPARE(failed.count(), 2);
        QCOMPARE(manager.calls.last(), QString("add Library"));
        QCOMPARE(failed.at(1).at(0).toString(),
                 QString("Could not connect to Library: 802-11-wireless.ssid: property is invalid"));
        QCOMPARE(rejected.count(), 1);
        QCOMPARE(wifi.connecting(), QString());
    }
};
QTEST_MAIN(NetworkManagerDbusTest)
#include "network_manager_dbus_test.moc"
