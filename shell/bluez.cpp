// SPDX-License-Identifier: GPL-3.0-or-later
#include "bluez.hpp"
#include "dbus_util.hpp"
#include <QDBusArgument>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QDBusVirtualObject>

namespace {
constexpr auto service = "org.bluez";
constexpr auto managerPath = "/org/bluez";
constexpr auto adapterInterface = "org.bluez.Adapter1";
constexpr auto deviceInterface = "org.bluez.Device1";
constexpr auto batteryInterface = "org.bluez.Battery1";
constexpr auto agentInterface = "org.bluez.Agent1";
constexpr auto agentManagerInterface = "org.bluez.AgentManager1";
constexpr auto objectManagerInterface = "org.freedesktop.DBus.ObjectManager";
constexpr auto propertiesInterface = "org.freedesktop.DBus.Properties";

// An object's interfaces and their properties, a{sa{sv}}; and BlueZ's objects, a{oa{sa{sv}}}.
using Interfaces = QMap<QString, QVariantMap>;
using Objects = QMap<QDBusObjectPath, Interfaces>;

// Properties as they are kept: an object path as a string, anything else as it came.
QVariantMap settled(const QVariantMap &properties) {
    QVariantMap values;
    for (auto it = properties.constBegin(); it != properties.constEnd(); ++it)
        values[it.key()] = it.value().metaType() == QMetaType::fromType<QDBusObjectPath>()
                               ? QVariant(it.value().value<QDBusObjectPath>().path())
                               : it.value();
    return values;
}
// A passkey as BlueZ shows it: six digits.
QString digits(uint passkey) { return QString("%1").arg(passkey, 6, 10, QChar('0')); }
} // namespace

// The agent's object, which hands BlueZ's calls to the backend.
class BlueZ::Agent : public QDBusVirtualObject {
  public:
    explicit Agent(BlueZ &owner) : owner_(owner) {}
    QString introspect(const QString &) const override {
        return QStringLiteral("<interface name=\"org.bluez.Agent1\"/>");
    }
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &) override {
        return owner_.agentCall(message);
    }

  private:
    BlueZ &owner_;
};

QString BlueZ::agentPath() { return QStringLiteral("/org/paw/BluetoothAgent"); }

BlueZ::BlueZ(const QDBusConnection &bus, QObject *parent) : Bluetooth(parent), bus_(bus) {
    qDBusRegisterMetaType<Interfaces>();
    qDBusRegisterMetaType<Objects>();
    // Devices come and change in bursts while looking; one update follows them all.
    publish_.setSingleShot(true);
    publish_.setInterval(30);
    connect(&publish_, &QTimer::timeout, this, &BlueZ::publish);
    if (!bus_.isConnected())
        return;
    agent_ = new Agent(*this);
    bus_.registerVirtualObject(agentPath(), agent_);
    watcher_ = new QDBusServiceWatcher(service, bus_, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher_, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &, const QString &owner) {
                stop();
                if (!owner.isEmpty())
                    start();
            });
    bus_.connect(service, "/", objectManagerInterface, "InterfacesAdded", this, SLOT(interfacesAdded(QDBusMessage)));
    bus_.connect(service, "/", objectManagerInterface, "InterfacesRemoved", this,
                 SLOT(interfacesRemoved(QDBusMessage)));
    bus_.connect(service, QString(), propertiesInterface, "PropertiesChanged", this,
                 SLOT(propertiesChanged(QDBusMessage)));
    auto ask = QDBusMessage::createMethodCall("org.freedesktop.DBus", "/org/freedesktop/DBus",
                                              "org.freedesktop.DBus", "NameHasOwner");
    ask << QString(service);
    dbus::whenAnswered(bus_.asyncCall(ask), this, [this](QDBusPendingCallWatcher *done) {
        if (!done->isError() && done->reply().arguments().value(0).toBool() && !running_)
            start();
    });
}
BlueZ::~BlueZ() {
    if (agent_) {
        dropQuestion();
        if (agentRegistered_) {
            auto message = QDBusMessage::createMethodCall(service, managerPath, agentManagerInterface, "UnregisterAgent");
            message << QVariant::fromValue(QDBusObjectPath(agentPath()));
            bus_.send(message);
        }
        bus_.unregisterObject(agentPath());
    }
    delete agent_;
}
void BlueZ::start() {
    running_ = true;
    const int generation = generation_;
    dbus::whenAnswered(
        bus_.asyncCall(QDBusMessage::createMethodCall(service, "/", objectManagerInterface,
                                                      "GetManagedObjects")),
        this, [this, generation](QDBusPendingCallWatcher *done) {
            if (generation != generation_ || done->isError())
                return;
            const auto objects = qdbus_cast<Objects>(done->reply().arguments().value(0));
            for (auto it = objects.constBegin(); it != objects.constEnd(); ++it)
                for (auto interface = it->constBegin(); interface != it->constEnd(); ++interface)
                    objects_[it.key().path()][interface.key()] = settled(interface.value());
            publish_.start();
        });
}
void BlueZ::stop() {
    ++generation_;
    running_ = agentRegistered_ = false;
    objects_.clear();
    question_ = {};
    questionKind_.clear();
    displayed_.clear();
    publish_.start();
}
void BlueZ::call(const QString &path, const QString &interface, const QString &method,
                 const QVariantList &arguments, int timeout, std::function<void(const QDBusError &)> done) {
    auto message = QDBusMessage::createMethodCall(service, path, interface, method);
    message.setArguments(arguments);
    const int generation = generation_;
    dbus::whenAnswered(bus_.asyncCall(message, timeout), this,
                       [this, generation, done = std::move(done)](QDBusPendingCallWatcher *answer) {
                           if (generation == generation_)
                               done(answer->isError() ? answer->error() : QDBusError());
                       });
}

void BlueZ::interfacesAdded(const QDBusMessage &message) {
    const auto arguments = message.arguments();
    if (!running_ || arguments.size() < 2)
        return;
    const auto path = arguments[0].value<QDBusObjectPath>().path();
    const auto interfaces = qdbus_cast<Interfaces>(arguments[1]);
    for (auto it = interfaces.constBegin(); it != interfaces.constEnd(); ++it)
        objects_[path][it.key()] = settled(it.value());
    publish_.start();
}
void BlueZ::interfacesRemoved(const QDBusMessage &message) {
    const auto arguments = message.arguments();
    if (!running_ || arguments.size() < 2)
        return;
    const auto path = arguments[0].value<QDBusObjectPath>().path();
    auto it = objects_.find(path);
    if (it == objects_.end())
        return;
    for (const auto &interface : arguments[1].toStringList())
        it->remove(interface);
    if (it->isEmpty())
        objects_.erase(it);
    publish_.start();
}
void BlueZ::propertiesChanged(const QDBusMessage &message) {
    const auto arguments = message.arguments();
    auto object = objects_.find(message.path());
    // An object is announced with InterfacesAdded first.
    if (!running_ || arguments.size() < 2 || object == objects_.end())
        return;
    auto &properties = (*object)[arguments[0].toString()];
    const auto changed = settled(dbus::map(arguments[1]));
    for (auto it = changed.constBegin(); it != changed.constEnd(); ++it)
        properties[it.key()] = it.value();
    // As a device goes out of range, its signal is no longer known.
    for (const auto &name : arguments.value(2).toStringList())
        properties.remove(name);
    publish_.start();
}

QString BlueZ::adapter() const {
    for (auto it = objects_.constBegin(); it != objects_.constEnd(); ++it)
        if (it->contains(adapterInterface))
            return it.key();
    return {};
}
QString BlueZ::nameOf(const QString &path) const {
    const auto device = objects_.value(path).value(deviceInterface);
    for (const char *name : {"Alias", "Name", "Address"})
        if (!device.value(name).toString().isEmpty())
            return device.value(name).toString();
    return path.section('/', -1);
}
void BlueZ::publish() {
    State state;
    const auto adapter = this->adapter();
    state.available = running_ && !adapter.isEmpty();
    if (state.available) {
        const auto properties = objects_.value(adapter).value(adapterInterface);
        state.powered = properties.value("Powered").toBool();
        state.discovering = properties.value("Discovering").toBool();
        for (auto it = objects_.constBegin(); it != objects_.constEnd(); ++it) {
            const auto device = it->value(deviceInterface);
            if (device.isEmpty() || device.value("Adapter").toString() != adapter)
                continue;
            const auto battery = it->value(batteryInterface);
            BluetoothDevice entry;
            entry.path = it.key();
            entry.name = nameOf(it.key());
            entry.named = device.contains("Name");
            entry.icon = device.value("Icon").toString();
            entry.paired = device.value("Paired").toBool();
            entry.connected = device.value("Connected").toBool();
            entry.rssi = device.value("RSSI").toInt();
            entry.battery = battery.contains("Percentage") ? battery.value("Percentage").toInt() : -1;
            state.devices.push_back(entry);
        }
    }
    update(std::move(state));
}

void BlueZ::ensureAgent() {
    if (agentRegistered_ || !running_ || !agent_)
        return;
    agentRegistered_ = true;
    const auto path = QVariant::fromValue(QDBusObjectPath(agentPath()));
    call(managerPath, agentManagerInterface, "RegisterAgent", {path, QStringLiteral("KeyboardDisplay")}, -1,
         [this, path](const QDBusError &error) {
             if (error.isValid() && error.name() != "org.bluez.Error.AlreadyExists") {
                 agentRegistered_ = false;
                 Q_EMIT failed("Could not take part in Bluetooth pairing: " + error.message());
                 return;
             }
             // Asked before another agent of the session, as the one the user sees.
             call(managerPath, agentManagerInterface, "RequestDefaultAgent", {path}, -1, [](const QDBusError &) {});
         });
}
bool BlueZ::agentCall(const QDBusMessage &message) {
    if (message.interface() != agentInterface)
        return false;
    const auto member = message.member();
    const auto arguments = message.arguments();
    const QString device = arguments.value(0).value<QDBusObjectPath>().path();
    // A question waits for the user's answer, which sendAnswer() sends.
    auto wait = [&](const QString &kind, const QString &code) {
        dropQuestion();
        question_ = message;
        questionKind_ = kind;
        ask({kind, device, nameOf(device), code});
    };
    if (member == "RequestPinCode") {
        wait("pin", {});
    } else if (member == "RequestPasskey") {
        wait("passkey", {});
    } else if (member == "RequestConfirmation") {
        wait("confirm", digits(arguments.value(1).toUInt()));
    } else if (member == "RequestAuthorization") {
        wait("authorize", {});
    } else if (member == "DisplayPinCode" || member == "DisplayPasskey") {
        // Shown for the user to type on the device; told again as keys are typed there.
        bus_.send(message.createReply());
        displayed_ = device;
        ask({"display", device, nameOf(device),
             member == "DisplayPinCode" ? arguments.value(1).toString() : digits(arguments.value(1).toUInt())});
    } else if (member == "AuthorizeService") {
        // A paired device's services are its own to use; another's are not.
        const bool paired = objects_.value(device).value(deviceInterface).value("Paired").toBool();
        bus_.send(paired ? message.createReply()
                         : message.createErrorReply("org.bluez.Error.Rejected", "The device is not paired"));
    } else if (member == "Cancel") {
        // BlueZ took its question back: there is nothing to answer.
        bus_.send(message.createReply());
        question_ = {};
        questionKind_.clear();
        displayed_.clear();
        cancelRequest();
    } else if (member == "Release") {
        bus_.send(message.createReply());
        agentRegistered_ = false;
    } else {
        return false;
    }
    return true;
}
void BlueZ::dropQuestion() {
    if (questionKind_.isEmpty())
        return;
    bus_.send(question_.createErrorReply("org.bluez.Error.Canceled", "Another question came"));
    question_ = {};
    questionKind_.clear();
}

void BlueZ::sendPowered(bool powered) {
    const auto adapter = this->adapter();
    call(adapter, propertiesInterface, "Set",
         {QString(adapterInterface), QStringLiteral("Powered"), QVariant::fromValue(QDBusVariant(powered))}, -1,
         [this, adapter, powered](const QDBusError &error) {
             if (error.isValid()) {
                 Q_EMIT failed(QString("Could not turn Bluetooth %1: ").arg(powered ? "on" : "off") + error.message());
                 // What BlueZ has shows again.
                 publish();
             } else if (powered && looking()) {
                 call(adapter, adapterInterface, "StartDiscovery", {}, -1, [](const QDBusError &) {});
             }
         });
}
void BlueZ::sendDiscovery(bool discovering) {
    if (discovering)
        ensureAgent();
    // Refused while the adapter is off, or already as asked: nothing to tell.
    call(adapter(), adapterInterface, discovering ? "StartDiscovery" : "StopDiscovery", {}, -1,
         [](const QDBusError &) {});
}
void BlueZ::sendConnect(const QString &path) {
    const auto name = nameOf(path);
    call(path, deviceInterface, "Connect", {}, 60000, [this, path, name](const QDBusError &error) {
        finished(path, error.isValid() ? "Could not connect to " + name + ": " + error.message() : QString());
    });
}
void BlueZ::sendDisconnect(const QString &path) {
    const auto name = nameOf(path);
    call(path, deviceInterface, "Disconnect", {}, 30000, [this, path, name](const QDBusError &error) {
        finished(path, error.isValid() ? "Could not disconnect from " + name + ": " + error.message() : QString());
    });
}
void BlueZ::sendPair(const QString &path) {
    ensureAgent();
    refused_ = false;
    const auto name = nameOf(path);
    call(path, deviceInterface, "Pair", {}, 90000, [this, path, name](const QDBusError &error) {
        dropQuestion();
        displayed_.clear();
        cancelRequest();
        if (error.isValid()) {
            // The user's own no is no failure to tell of.
            finished(path, refused_ ? QString() : "Could not pair with " + name + ": " + error.message());
            refused_ = false;
            return;
        }
        // Trusted, it may connect by itself from now on; and connected now.
        call(path, propertiesInterface, "Set",
             {QString(deviceInterface), QStringLiteral("Trusted"), QVariant::fromValue(QDBusVariant(true))}, -1,
             [](const QDBusError &) {});
        call(path, deviceInterface, "Connect", {}, 60000, [this, path, name](const QDBusError &error) {
            finished(path, error.isValid() ? "Paired with " + name + ", but could not connect: " + error.message()
                                           : QString());
        });
    });
}
void BlueZ::sendForget(const QString &path) {
    const auto name = nameOf(path);
    call(adapter(), adapterInterface, "RemoveDevice", {QVariant::fromValue(QDBusObjectPath(path))}, -1,
         [this, name](const QDBusError &error) {
             if (error.isValid())
                 Q_EMIT failed("Could not forget " + name + ": " + error.message());
         });
}
void BlueZ::sendAnswer(bool accepted, const QString &input) {
    if (!accepted)
        refused_ = true;
    if (questionKind_.isEmpty()) {
        // A code shown has nothing to answer; no stops the pairing.
        if (!accepted && !displayed_.isEmpty())
            call(displayed_, deviceInterface, "CancelPairing", {}, -1, [](const QDBusError &) {});
        displayed_.clear();
        return;
    }
    if (!accepted)
        bus_.send(question_.createErrorReply("org.bluez.Error.Rejected", "Refused by the user"));
    else if (questionKind_ == "pin")
        bus_.send(question_.createReply(input));
    else if (questionKind_ == "passkey")
        bus_.send(question_.createReply(input.toUInt()));
    else
        bus_.send(question_.createReply());
    question_ = {};
    questionKind_.clear();
}

std::unique_ptr<Bluetooth> makeBluetooth() {
    return std::make_unique<BlueZ>(QDBusConnection::systemBus());
}
