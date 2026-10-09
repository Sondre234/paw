// SPDX-License-Identifier: GPL-3.0-or-later
// Bluetooth against a stand-in BlueZ on a private bus (a dbus-daemon this test starts and kills)
// standing in for the system bus, never the real one: an adapter, paired devices and others in
// range, from its ObjectManager. BlueZ coming and going, the adapter switched, devices connected,
// found and forgotten, and pairing through the shell's agent, which the stand-in calls as BlueZ
// does: confirming a passkey, typing a PIN or a number, showing a code, saying no, and BlueZ
// taking a question back.
#include "bluez.hpp"
#include "fake_dbus.hpp"
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QSignalSpy>
#include <QTest>
#include <map>
#include <memory>

namespace {
using Interfaces = QMap<QString, QVariantMap>;
using Objects = QMap<QDBusObjectPath, Interfaces>;
constexpr auto service = "org.bluez";
constexpr auto adapterPath = "/org/bluez/hci0";
constexpr auto adapterInterface = "org.bluez.Adapter1";
constexpr auto deviceInterface = "org.bluez.Device1";
constexpr auto batteryInterface = "org.bluez.Battery1";
constexpr auto objectManagerInterface = "org.freedesktop.DBus.ObjectManager";

QString pathOf(const QString &name) { return QString(adapterPath) + "/dev_" + name; }

// BlueZ as far as the shell asks it: an adapter that is on, the headphones Buds (paired,
// connected, at 80 %), the Speaker (paired) and a Phone in range.
class FakeBlueZ {
  public:
    // What the shell asked of BlueZ itself: "register PATH CAPABILITY", "default PATH",
    // "start", "stop", "remove DEVICE"; and what the agent answered: "ok", "ok VALUE" or the error's
    // name.
    QStringList calls, answers;
    // The agent's method a Pair calls first, with its arguments after the device; none pairs at once.
    QString pairAsks;
    QVariantList pairArguments;
    // The error Connect answers with, while set.
    QString refuseConnect;
    QString agentOwner, agentPath;

    explicit FakeBlueZ(fakedbus::Bus &bus) : bus_(bus.connect()) {
        root_ = add("/", objectManagerInterface, {});
        root_->properties.clear();
        root_->methods = [this](const QDBusMessage &call, QDBusConnection &connection) {
            if (call.member() != "GetManagedObjects")
                return false;
            Objects objects;
            for (const auto &[path, object] : objects_)
                if (path != "/")
                    objects[QDBusObjectPath(path)] = object->properties;
            connection.send(call.createReply(QVariant::fromValue(objects)));
            return true;
        };
        auto *manager = add("/org/bluez", "org.bluez.AgentManager1", {});
        manager->methods = [this](const QDBusMessage &call, QDBusConnection &connection) {
            const auto path = call.arguments().value(0).value<QDBusObjectPath>().path();
            if (call.member() == "RegisterAgent") {
                calls << "register " + path + " " + call.arguments().value(1).toString();
                agentOwner = call.service();
                agentPath = path;
            } else if (call.member() == "RequestDefaultAgent") {
                calls << "default " + path;
            } else if (call.member() == "UnregisterAgent") {
                calls << "unregister " + path;
            } else {
                return false;
            }
            connection.send(call.createReply());
            return true;
        };
        auto *adapter = add(adapterPath, adapterInterface,
                            {{"Address", "00:11:22:33:44:55"}, {"Name", "fake"}, {"Alias", "fake"},
                             {"Powered", true}, {"Discovering", false}, {"Pairable", true}});
        adapter->methods = [this, adapter](const QDBusMessage &call, QDBusConnection &connection) {
            if (call.member() == "StartDiscovery" || call.member() == "StopDiscovery") {
                const bool start = call.member() == "StartDiscovery";
                calls << (start ? "start" : "stop");
                connection.send(call.createReply());
                adapter->set(adapterInterface, "Discovering", start);
                return true;
            }
            if (call.member() == "RemoveDevice") {
                const auto path = call.arguments().value(0).value<QDBusObjectPath>().path();
                calls << "remove " + path;
                connection.send(call.createReply());
                remove(path);
                return true;
            }
            return false;
        };
        addDevice("Buds", "audio-headphones", true, true, 0, 80);
        addDevice("Speaker", "audio-card", true, false);
        addDevice("Phone", "phone", false, false, -50);
    }
    ~FakeBlueZ() {
        bus_.unregisterService(service);
        objects_.clear();
        QDBusConnection::disconnectFromBus(bus_.name());
    }
    bool start() { return bus_.registerService(service); }
    void stop() { bus_.unregisterService(service); }
    fakedbus::Object *at(const QString &path) { return objects_.at(path).get(); }

    // A device, announced when BlueZ runs; a battery of -1 has no Battery1.
    void addDevice(const QString &name, const QString &icon, bool paired, bool connected, int rssi = 0,
                   int battery = -1) {
        const auto path = pathOf(name);
        QVariantMap device{{"Address", "AA:BB:CC:" + name.left(2)},
                           {"Name", name},
                           {"Alias", name},
                           {"Icon", icon},
                           {"Paired", paired},
                           {"Trusted", paired},
                           {"Connected", connected},
                           {"Adapter", QVariant::fromValue(QDBusObjectPath(adapterPath))}};
        if (rssi)
            device["RSSI"] = QVariant::fromValue(short(rssi));
        auto *object = add(path, deviceInterface, device);
        if (battery >= 0)
            object->properties[batteryInterface] = {{"Percentage", QVariant::fromValue(uchar(battery))}};
        object->methods = [this, path](const QDBusMessage &call, QDBusConnection &connection) {
            return deviceCall(path, call, connection);
        };
        root_->emitSignal(objectManagerInterface, "InterfacesAdded",
                          {QVariant::fromValue(QDBusObjectPath(path)), QVariant::fromValue(object->properties)});
    }
    // A device's battery that BlueZ comes to know of.
    void addBattery(const QString &name, int percent) {
        const Interfaces battery{{batteryInterface, {{"Percentage", QVariant::fromValue(uchar(percent))}}}};
        at(pathOf(name))->properties[batteryInterface] = battery.value(batteryInterface);
        root_->emitSignal(objectManagerInterface, "InterfacesAdded",
                          {QVariant::fromValue(QDBusObjectPath(pathOf(name))), QVariant::fromValue(battery)});
    }
    void remove(const QString &path) {
        root_->emitSignal(objectManagerInterface, "InterfacesRemoved",
                          {QVariant::fromValue(QDBusObjectPath(path)), QStringList(at(path)->properties.keys())});
        objects_.erase(path);
    }
    // Calls the shell's agent, as BlueZ does, recording its answer.
    void askAgent(const QString &method, const QVariantList &arguments, std::function<void(bool)> done = {}) {
        auto message = QDBusMessage::createMethodCall(agentOwner, agentPath, "org.bluez.Agent1", method);
        message.setArguments(arguments);
        // Owned here, so that an answer coming after the stand-in is gone goes nowhere.
        auto *watch = new QDBusPendingCallWatcher(bus_.asyncCall(message, 30000), &context_);
        QObject::connect(watch, &QDBusPendingCallWatcher::finished, &context_, [this, watch, done] {
            watch->deleteLater();
            const auto reply = watch->reply();
            if (watch->isError())
                answers << watch->error().name();
            else
                answers << ("ok " + fakedbus::text(reply.arguments().value(0))).trimmed();
            if (done)
                done(!watch->isError());
        });
    }
    // Ends a pairing waiting on a code shown: done, or failed.
    void endPairing(bool paired) {
        if (pendingPair_.type() == QDBusMessage::InvalidMessage)
            return;
        const auto call = pendingPair_;
        pendingPair_ = {};
        finishPair(call, paired, "org.bluez.Error.AuthenticationFailed");
    }

  private:
    QDBusConnection bus_;
    std::map<QString, std::unique_ptr<fakedbus::Object>> objects_;
    fakedbus::Object *root_ = nullptr;
    QDBusMessage pendingPair_;
    QObject context_;

    fakedbus::Object *add(const QString &path, const QString &interface, const QVariantMap &properties) {
        auto object = std::make_unique<fakedbus::Object>(bus_, path);
        object->properties[interface] = properties;
        auto *raw = object.get();
        objects_[path] = std::move(object);
        return raw;
    }
    void finishPair(const QDBusMessage &call, bool paired, const QString &error) {
        const auto path = call.path();
        if (!objects_.count(path))
            return;
        if (!paired) {
            bus_.send(call.createErrorReply(error, "Pairing did not happen"));
            return;
        }
        at(path)->set(deviceInterface, "Paired", true);
        bus_.send(call.createReply());
    }
    bool deviceCall(const QString &path, const QDBusMessage &call, QDBusConnection &connection) {
        auto *device = at(path);
        if (call.member() == "Connect") {
            if (!refuseConnect.isEmpty()) {
                connection.send(call.createErrorReply("org.bluez.Error.Failed", refuseConnect));
                return true;
            }
            device->set(deviceInterface, "Connected", true);
            connection.send(call.createReply());
            return true;
        }
        if (call.member() == "Disconnect") {
            device->set(deviceInterface, "Connected", false);
            connection.send(call.createReply());
            return true;
        }
        if (call.member() == "CancelPairing") {
            connection.send(call.createReply());
            if (pendingPair_.type() != QDBusMessage::InvalidMessage) {
                const auto pair = pendingPair_;
                pendingPair_ = {};
                finishPair(pair, false, "org.bluez.Error.AuthenticationCanceled");
            }
            return true;
        }
        if (call.member() != "Pair")
            return false;
        if (pairAsks.isEmpty()) {
            finishPair(call, true, {});
            return true;
        }
        const bool shows = pairAsks.startsWith("Display");
        if (shows)
            pendingPair_ = call;
        askAgent(pairAsks, QVariantList{QVariant::fromValue(QDBusObjectPath(path))} + pairArguments,
                 [this, call, shows](bool answered) {
                     if (!shows)
                         finishPair(call, answered, "org.bluez.Error.AuthenticationRejected");
                 });
        return true;
    }
};
} // namespace

class BlueZDbusTest : public QObject {
    Q_OBJECT
    fakedbus::Bus bus_;

    static QStringList names(const BluetoothDevices *devices) {
        QStringList list;
        for (const auto &device : devices->all())
            list << device.name;
        return list;
    }
    static const BluetoothDevice *device(const Bluetooth &bluetooth, const QString &name) {
        for (const auto *list : {bluetooth.paired(), bluetooth.found()})
            for (const auto &device : list->all())
                if (device.name == name)
                    return &device;
        return nullptr;
    }

  private Q_SLOTS:
    void initTestCase() {
        if (!fakedbus::Bus::available())
            QSKIP("dbus-daemon is not installed");
        qDBusRegisterMetaType<Interfaces>();
        qDBusRegisterMetaType<Objects>();
        QVERIFY(bus_.start());
    }
    // Read whole once BlueZ is there, and nothing once it goes; no agent until asked.
    void comesAndGoes() {
        BlueZ bluetooth(bus_.connect());
        QTest::qWait(50);
        QVERIFY(!bluetooth.available());
        FakeBlueZ bluez(bus_);
        QVERIFY(bluez.start());
        QTRY_VERIFY(bluetooth.available());
        QVERIFY(bluetooth.powered() && !bluetooth.discovering());
        QCOMPARE(names(bluetooth.paired()), (QStringList{"Buds", "Speaker"}));
        QCOMPARE(device(bluetooth, "Buds")->battery, 80);
        QCOMPARE(device(bluetooth, "Buds")->icon, QString("audio-headphones"));
        QCOMPARE(device(bluetooth, "Speaker")->battery, -1);
        QCOMPARE(bluetooth.connectedName(), QString("Buds"));
        QCOMPARE(bluetooth.found()->count(), 0);
        QVERIFY(bluez.calls.isEmpty());
        bluez.stop();
        QTRY_VERIFY(!bluetooth.available());
        QCOMPARE(bluetooth.paired()->count(), 0);
    }
    // The adapter switched, and a refusal undone; devices connected and disconnected.
    void controls() {
        FakeBlueZ bluez(bus_);
        QVERIFY(bluez.start());
        BlueZ bluetooth(bus_.connect());
        QTRY_VERIFY(bluetooth.available());
        QSignalSpy failed(&bluetooth, &Bluetooth::failed);
        bluetooth.setPowered(false);
        QTRY_COMPARE(bluez.at(adapterPath)->calls, QStringList{"Set org.bluez.Adapter1.Powered false"});
        QTRY_VERIFY(!bluez.at(adapterPath)->properties[adapterInterface]["Powered"].toBool());
        bluez.at(adapterPath)->setter = [](const QString &, const QString &, const QVariant &) { return false; };
        bluetooth.setPowered(true);
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY2(failed.at(0).at(0).toString().startsWith("Could not turn Bluetooth on: "),
                 qPrintable(failed.at(0).at(0).toString()));
        QTRY_VERIFY(!bluetooth.powered());
        bluez.at(adapterPath)->setter = nullptr;
        bluez.at(adapterPath)->set(adapterInterface, "Powered", true);
        QTRY_VERIFY(bluetooth.powered());
        bluetooth.connectDevice(pathOf("Speaker"));
        QVERIFY(device(bluetooth, "Speaker")->busy);
        QTRY_VERIFY(device(bluetooth, "Speaker")->connected && !device(bluetooth, "Speaker")->busy);
        QCOMPARE(bluez.at(pathOf("Speaker"))->calls, QStringList{"Connect"});
        bluetooth.disconnectDevice(pathOf("Buds"));
        QTRY_VERIFY(!device(bluetooth, "Buds")->connected);
        QCOMPARE(names(bluetooth.paired()), (QStringList{"Speaker", "Buds"}));
        bluez.refuseConnect = "Page Timeout";
        bluetooth.connectDevice(pathOf("Buds"));
        QTRY_COMPARE(failed.count(), 2);
        QCOMPARE(failed.at(1).at(0).toString(), QString("Could not connect to Buds: Page Timeout"));
        QVERIFY(!device(bluetooth, "Buds")->busy);
        // Forgotten, it goes.
        bluetooth.forget(pathOf("Buds"));
        QTRY_COMPARE(names(bluetooth.paired()), QStringList{"Speaker"});
        QCOMPARE(bluez.calls, QStringList{"remove " + pathOf("Buds")});
    }
    // Looking for devices registers the agent, as the default, and lists what is in range with a
    // name, the strongest first, as it comes, goes and changes.
    void looking() {
        FakeBlueZ bluez(bus_);
        QVERIFY(bluez.start());
        BlueZ bluetooth(bus_.connect());
        QTRY_VERIFY(bluetooth.available());
        bluetooth.lookFor(true);
        const auto agent = BlueZ::agentPath();
        QTRY_COMPARE(bluez.calls, (QStringList{"register " + agent + " KeyboardDisplay", "start", "default " + agent}));
        QTRY_VERIFY(bluetooth.discovering());
        QCOMPARE(names(bluetooth.found()), QStringList{"Phone"});
        bluez.addDevice("Watch", "watch", false, false, -40);
        QTRY_COMPARE(names(bluetooth.found()), (QStringList{"Watch", "Phone"}));
        // Out of range, its signal is unknown.
        bluez.at(pathOf("Watch"))->emitSignal(fakedbus::propertiesInterface, "PropertiesChanged",
                                               {QString(deviceInterface), QVariantMap(), QStringList{"RSSI"}});
        QTRY_COMPARE(names(bluetooth.found()), (QStringList{"Phone", "Watch"}));
        // A battery that shows up later.
        bluez.addBattery("Speaker", 55);
        QTRY_COMPARE(device(bluetooth, "Speaker")->battery, 55);
        bluez.remove(pathOf("Phone"));
        QTRY_COMPARE(names(bluetooth.found()), QStringList{"Watch"});
        bluetooth.lookFor(false);
        QTRY_COMPARE(bluez.calls.last(), QString("stop"));
        QTRY_VERIFY(!bluetooth.discovering());
        QCOMPARE(bluetooth.found()->count(), 0);
        // Registered once.
        bluetooth.lookFor(true);
        QTRY_COMPARE(bluez.calls.last(), QString("start"));
        QCOMPARE(bluez.calls.count("register " + agent + " KeyboardDisplay"), 1);
    }
    // A passkey to confirm: yes pairs, trusts and connects.
    void confirm() {
        FakeBlueZ bluez(bus_);
        QVERIFY(bluez.start());
        BlueZ bluetooth(bus_.connect());
        QSignalSpy failed(&bluetooth, &Bluetooth::failed);
        QTRY_VERIFY(bluetooth.available());
        bluetooth.lookFor(true);
        QTRY_VERIFY(bluetooth.found()->count() == 1);
        bluez.pairAsks = "RequestConfirmation";
        bluez.pairArguments = {uint(4321)};
        bluetooth.pair(pathOf("Phone"));
        QTRY_COMPARE(bluetooth.request(), QString("confirm"));
        QCOMPARE(bluetooth.requestName(), QString("Phone"));
        QCOMPARE(bluetooth.requestCode(), QString("004321"));
        QVERIFY(device(bluetooth, "Phone")->busy);
        bluetooth.accept();
        QTRY_COMPARE(bluez.answers, QStringList{"ok"});
        QTRY_VERIFY(device(bluetooth, "Phone") && device(bluetooth, "Phone")->paired);
        QTRY_VERIFY(device(bluetooth, "Phone")->connected && !device(bluetooth, "Phone")->busy);
        QCOMPARE(bluez.at(pathOf("Phone"))->calls,
                 (QStringList{"Pair", "Set org.bluez.Device1.Trusted true", "Connect"}));
        QCOMPARE(failed.count(), 0);
    }
    // A PIN and a number typed, a code shown, the user's no, and BlueZ taking a question back.
    void answers() {
        FakeBlueZ bluez(bus_);
        QVERIFY(bluez.start());
        BlueZ bluetooth(bus_.connect());
        QSignalSpy failed(&bluetooth, &Bluetooth::failed);
        QTRY_VERIFY(bluetooth.available());
        bluez.addDevice("Keyboard", "input-keyboard", false, false, -45);
        bluez.addDevice("Mouse", "input-mouse", false, false, -55);
        bluez.addDevice("Old", "audio-card", false, false, -65);
        bluetooth.lookFor(true);
        QTRY_COMPARE(bluetooth.found()->count(), 4);
        bluez.pairAsks = "RequestPinCode";
        bluetooth.pair(pathOf("Old"));
        QTRY_COMPARE(bluetooth.request(), QString("pin"));
        bluetooth.accept("0000");
        QTRY_COMPARE(bluez.answers, QStringList{"ok 0000"});
        QTRY_VERIFY(device(bluetooth, "Old")->paired);
        bluez.pairAsks = "RequestPasskey";
        bluetooth.pair(pathOf("Mouse"));
        QTRY_COMPARE(bluetooth.request(), QString("passkey"));
        bluetooth.reject();
        QTRY_COMPARE(bluez.answers.size(), 2);
        QCOMPARE(bluez.answers[1], QString("org.bluez.Error.Rejected"));
        QTRY_VERIFY(!device(bluetooth, "Mouse")->busy);
        QVERIFY(!device(bluetooth, "Mouse")->paired);
        QCOMPARE(failed.count(), 0);
        bluetooth.pair(pathOf("Mouse"));
        QTRY_COMPARE(bluetooth.request(), QString("passkey"));
        bluetooth.accept("012345");
        QTRY_COMPARE(bluez.answers.size(), 3);
        QCOMPARE(bluez.answers[2], QString("ok 12345"));
        // A code to type on the keyboard; no stops the pairing, which is then no failure.
        bluez.pairAsks = "DisplayPasskey";
        bluez.pairArguments = {uint(98765), QVariant::fromValue(ushort(0))};
        bluetooth.pair(pathOf("Keyboard"));
        QTRY_COMPARE(bluetooth.request(), QString("display"));
        QCOMPARE(bluetooth.requestCode(), QString("098765"));
        bluetooth.reject();
        QTRY_VERIFY(!device(bluetooth, "Keyboard")->busy);
        QVERIFY(bluez.at(pathOf("Keyboard"))->calls.contains("CancelPairing"));
        QCOMPARE(failed.count(), 0);
        // Typed there, it pairs.
        bluetooth.pair(pathOf("Keyboard"));
        QTRY_COMPARE(bluetooth.request(), QString("display"));
        bluez.endPairing(true);
        QTRY_VERIFY(device(bluetooth, "Keyboard")->paired);
        QCOMPARE(bluetooth.request(), QString());
        // BlueZ takes its question back.
        bluez.addDevice("Tablet", "computer", false, false, -70);
        bluez.pairAsks = "RequestAuthorization";
        bluez.pairArguments = {};
        QTRY_VERIFY(device(bluetooth, "Tablet"));
        bluetooth.pair(pathOf("Tablet"));
        QTRY_COMPARE(bluetooth.request(), QString("authorize"));
        bluez.askAgent("Cancel", {});
        QTRY_COMPARE(bluetooth.request(), QString());
        // A paired device's service is allowed, another's not.
        QStringList authorized;
        for (const char *name : {"Old", "Tablet"})
            bluez.askAgent("AuthorizeService", {QVariant::fromValue(QDBusObjectPath(pathOf(name))), QString("0000110b")},
                           [&authorized, name](bool allowed) { authorized << QString(allowed ? "" : "not ") + name; });
        QTRY_COMPARE(authorized.size(), 2);
        QVERIFY(authorized.contains("Old") && authorized.contains("not Tablet"));
    }
};
QTEST_MAIN(BlueZDbusTest)
#include "bluez_dbus_test.moc"
