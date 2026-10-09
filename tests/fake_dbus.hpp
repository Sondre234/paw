// SPDX-License-Identifier: GPL-3.0-or-later
// Stand-ins for the services the shell's D-Bus clients talk to (media players, and the system
// services of Quick Settings), on a private bus a test starts and kills, never the real one. An
// Object answers message by message: its properties by interface through Get, GetAll and Set,
// PropertiesChanged as a test changes them, and the methods a test answers itself, each call
// recorded.
#pragma once
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QFile>
#include <QMap>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <QUuid>
#include <QVariantMap>
#include <functional>

namespace fakedbus {
inline constexpr auto propertiesInterface = "org.freedesktop.DBus.Properties";

// An argument as a call is recorded: an object path by its path, else as Qt converts it.
inline QString text(const QVariant &value) {
    if (value.metaType() == QMetaType::fromType<QDBusObjectPath>())
        return value.value<QDBusObjectPath>().path();
    return value.toString();
}

// A dbus-daemon of the test's own, with no service directories: a name nobody owns stays unowned,
// whatever is installed.
class Bus {
  public:
    ~Bus() {
        if (daemon_.state() != QProcess::NotRunning) {
            daemon_.kill(); // this test's own daemon, by its process
            daemon_.waitForFinished(3000);
        }
    }
    static bool available() { return !QStandardPaths::findExecutable("dbus-daemon").isEmpty(); }
    // Starts it; false when it does not start.
    bool start() {
        QFile config(dir_.filePath("bus.conf"));
        if (!dir_.isValid() || !config.open(QIODevice::WriteOnly))
            return false;
        config.write(("<busconfig><type>session</type><listen>unix:dir=" + dir_.path() +
                      "</listen><auth>EXTERNAL</auth><policy context=\"default\">"
                      "<allow send_destination=\"*\" eavesdrop=\"true\"/><allow eavesdrop=\"true\"/>"
                      "<allow own=\"*\"/></policy></busconfig>")
                         .toUtf8());
        config.close();
        daemon_.start("dbus-daemon", {"--config-file=" + dir_.filePath("bus.conf"), "--nofork",
                                      "--print-address=1"});
        if (!daemon_.waitForStarted() || !daemon_.waitForReadyRead(5000))
            return false;
        address_ = QString::fromUtf8(daemon_.readLine()).trimmed();
        return address_.startsWith("unix:");
    }
    QString address() const { return address_; }
    // A connection of its own, as another process would have: a unique name, and its own
    // objects.
    QDBusConnection connect() {
        return QDBusConnection::connectToBus(address_, "fake-" + QUuid::createUuid().toString(QUuid::Id128));
    }

  private:
    QTemporaryDir dir_;
    QProcess daemon_;
    QString address_;
};

class Object : public QDBusVirtualObject {
  public:
    // Answers a call of an interface other than Properties (through `bus`), returning whether it
    // knew the method; an unknown one is answered with an error.
    using Handler = std::function<bool(const QDBusMessage &call, QDBusConnection &bus)>;
    Object(QDBusConnection bus, QString path) : bus_(std::move(bus)), path_(std::move(path)) {
        bus_.registerVirtualObject(path_, this);
    }
    ~Object() override { bus_.unregisterObject(path_); }
    QString path() const { return path_; }
    QDBusConnection &bus() { return bus_; }
    // The properties of each interface, which Get and GetAll answer with.
    QMap<QString, QVariantMap> properties;
    Handler methods;
    // Every call but Get and GetAll, as "Method argument argument ...", and a Set as
    // "Set Interface.Name value".
    QStringList calls;
    // Whether a Set from the bus is taken, which then changes the property and announces it;
    // false refuses it with an error. Unset, every one is taken.
    std::function<bool(const QString &interface, const QString &name, const QVariant &value)> setter;
    // Told of each Get and GetAll ("" for the name), after the answer is sent.
    std::function<void(const QString &interface, const QString &name)> read;
    // Told of each call recorded in `calls`, as it is recorded.
    std::function<void(const QString &call)> called;
    // Changes properties of one interface and announces them in one PropertiesChanged.
    void set(const QString &interface, const QVariantMap &changes) {
        for (auto it = changes.constBegin(); it != changes.constEnd(); ++it)
            properties[interface][it.key()] = it.value();
        auto signal = QDBusMessage::createSignal(path_, propertiesInterface, "PropertiesChanged");
        signal << interface << changes << QStringList();
        bus_.send(signal);
    }
    void set(const QString &interface, const QString &name, const QVariant &value) {
        set(interface, QVariantMap{{name, value}});
    }
    // Sends a signal of this object.
    void emitSignal(const QString &interface, const QString &name, const QVariantList &arguments) {
        auto signal = QDBusMessage::createSignal(path_, interface, name);
        signal.setArguments(arguments);
        bus_.send(signal);
    }
    QString introspect(const QString &) const override {
        QString xml;
        for (const auto &interface : properties.keys())
            xml += "<interface name=\"" + interface + "\"/>";
        return xml;
    }
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override {
        QDBusConnection bus(connection);
        const auto arguments = message.arguments();
        if (message.interface() == propertiesInterface) {
            const auto interface = arguments.value(0).toString();
            if (message.member() == "GetAll") {
                bus.send(message.createReply(QVariant(properties.value(interface))));
                if (read)
                    read(interface, QString());
                return true;
            }
            if (message.member() == "Get") {
                const auto name = arguments.value(1).toString();
                if (!properties.value(interface).contains(name))
                    bus.send(message.createErrorReply("org.freedesktop.DBus.Error.UnknownProperty", name));
                else
                    bus.send(message.createReply(QVariant::fromValue(QDBusVariant(properties[interface][name]))));
                if (read)
                    read(interface, name);
                return true;
            }
            if (message.member() == "Set") {
                const auto name = arguments.value(1).toString();
                const auto value = arguments.value(2).value<QDBusVariant>().variant();
                record("Set " + interface + "." + name + " " + text(value));
                if (setter && !setter(interface, name, value)) {
                    bus.send(message.createErrorReply("org.freedesktop.DBus.Error.AccessDenied", name));
                    return true;
                }
                bus.send(message.createReply());
                set(interface, name, value);
                return true;
            }
        }
        QStringList words{message.member()};
        for (const auto &argument : arguments)
            words << text(argument);
        record(words.join(' '));
        if (methods && methods(message, bus))
            return true;
        bus.send(message.createErrorReply("org.freedesktop.DBus.Error.UnknownMethod", message.member()));
        return true;
    }

  private:
    void record(const QString &call) {
        calls << call;
        if (called)
            called(call);
    }
    QDBusConnection bus_;
    QString path_;
};
} // namespace fakedbus
