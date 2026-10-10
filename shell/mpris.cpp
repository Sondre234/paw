// SPDX-License-Identifier: GPL-3.0-or-later
#include "mpris.hpp"
#include "dbus_util.hpp"
#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QFileInfo>
#include <QUrl>
#include <iostream>

namespace {
constexpr auto prefix = "org.mpris.MediaPlayer2.";
constexpr auto objectPath = "/org/mpris/MediaPlayer2";
constexpr auto rootInterface = "org.mpris.MediaPlayer2";
constexpr auto playerInterface = "org.mpris.MediaPlayer2.Player";
constexpr auto propertiesInterface = "org.freedesktop.DBus.Properties";
constexpr auto busService = "org.freedesktop.DBus";
constexpr auto busPath = "/org/freedesktop/DBus";

// A variant holding a{sv}: a QVariantMap, or a D-Bus argument still to read as one.
QVariantMap map(const QVariant &value) {
    if (value.metaType() == QMetaType::fromType<QDBusArgument>())
        return qdbus_cast<QVariantMap>(value.value<QDBusArgument>());
    return value.toMap();
}
// A string or an object path, which players send track ids as.
QString text(const QVariant &value) {
    if (value.metaType() == QMetaType::fromType<QDBusObjectPath>())
        return value.value<QDBusObjectPath>().path();
    return value.toString();
}
// The metadata read as plain values, so that nothing refers to the message it came in.
void settle(QVariantMap &properties) {
    if (properties.contains("Metadata"))
        properties["Metadata"] = map(properties.value("Metadata"));
}
} // namespace

bool Mpris::playerName(const QString &name) {
    return name.startsWith(prefix) && name.size() > qsizetype(qstrlen(prefix)) &&
           name != "org.mpris.MediaPlayer2.playerctld";
}

Mpris::Mpris(const QDBusConnection &bus, QObject *parent) : Media(parent), bus_(bus) {
    if (!bus_.isConnected())
        return;
    bus_.connect(busService, busPath, busService, "NameOwnerChanged", this,
                 SLOT(nameOwnerChanged(QString, QString, QString)));
    bus_.connect(QString(), objectPath, propertiesInterface, "PropertiesChanged", this,
                 SLOT(propertiesChanged(QDBusMessage)));
    bus_.connect(QString(), objectPath, playerInterface, "Seeked", this, SLOT(seeked(QDBusMessage)));
    // The players there already, each with its owner.
    auto list = QDBusMessage::createMethodCall(busService, busPath, busService, "ListNames");
    dbus::whenAnswered(bus_.asyncCall(list), this, [this](QDBusPendingCallWatcher *done) {
        if (done->isError())
            return;
        for (const auto &name : done->reply().arguments().value(0).toStringList()) {
            if (!playerName(name))
                continue;
            auto owner = QDBusMessage::createMethodCall(busService, busPath, busService, "GetNameOwner");
            owner << name;
            dbus::whenAnswered(bus_.asyncCall(owner), this,
                               [this, name](QDBusPendingCallWatcher *answer) {
                                   // One that appeared meanwhile is known from NameOwnerChanged
                                   // already.
                                   if (!answer->isError() && !known_.contains(name))
                                       add(name, answer->reply().arguments().value(0).toString());
                               });
        }
    });
}

void Mpris::nameOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner) {
    if (!playerName(name))
        return;
    if (!oldOwner.isEmpty() || newOwner.isEmpty())
        forget(name);
    if (!newOwner.isEmpty())
        add(name, newOwner);
}
void Mpris::add(const QString &name, const QString &owner) {
    known_[name] = Known{owner, {}, {}, false, false};
    readAll(name, rootInterface);
    readAll(name, playerInterface);
}
void Mpris::forget(const QString &name) {
    if (known_.remove(name))
        removePlayer(name);
}
void Mpris::readAll(const QString &name, const QString &interface) {
    auto message = QDBusMessage::createMethodCall(name, objectPath, propertiesInterface, "GetAll");
    message << interface;
    const QString owner = known_.value(name).owner;
    dbus::whenAnswered(bus_.asyncCall(message), this,
                       [this, name, owner, interface](QDBusPendingCallWatcher *done) {
                           auto it = known_.find(name);
                           // Gone, or owned by another since it was asked.
                           if (it == known_.end() || it->owner != owner)
                               return;
                           // A player that answers with an error has nothing to show; it is not
                           // asked again.
                           auto properties = done->isError()
                                                 ? QVariantMap()
                                                 : map(done->reply().arguments().value(0));
                           settle(properties);
                           if (interface == rootInterface) {
                               it->root = properties;
                               it->rootRead = true;
                           } else {
                               it->player = properties;
                               it->playerRead = true;
                           }
                           publish(name);
                           if (interface == playerInterface)
                               queryPosition(name);
                       });
}
void Mpris::publish(const QString &name, qint64 position) {
    auto it = known_.constFind(name);
    if (it == known_.constEnd() || !it->rootRead || !it->playerRead)
        return;
    const auto &root = it->root, &properties = it->player;
    Player player;
    player.name = name;
    player.identity = root.value("Identity").toString();
    if (player.identity.isEmpty())
        player.identity = name.mid(qstrlen(prefix)).section('.', 0, 0);
    player.desktopEntry = root.value("DesktopEntry").toString();
    player.canRaise = root.value("CanRaise").toBool();
    const auto status = properties.value("PlaybackStatus").toString();
    player.status = status == "Playing" || status == "Paused" ? status : QStringLiteral("Stopped");
    const auto metadata = properties.value("Metadata").toMap();
    player.title = metadata.value("xesam:title").toString();
    // A file without tags, named by its file instead.
    if (player.title.isEmpty())
        player.title = QUrl(metadata.value("xesam:url").toString()).fileName();
    player.artist = metadata.value("xesam:artist").toStringList().join(", ");
    player.album = metadata.value("xesam:album").toString();
    player.art = metadata.value("mpris:artUrl").toString();
    player.trackId = text(metadata.value("mpris:trackid"));
    if (player.trackId == "/org/mpris/MediaPlayer2/TrackList/NoTrack")
        player.trackId.clear();
    player.length = std::max<qint64>(0, metadata.value("mpris:length").toLongLong());
    const double rate = properties.value("Rate", 1.0).toDouble();
    player.rate = rate > 0 ? rate : 1;
    player.canPlay = properties.value("CanPlay").toBool();
    player.canPause = properties.value("CanPause").toBool();
    player.canGoNext = properties.value("CanGoNext").toBool();
    player.canGoPrevious = properties.value("CanGoPrevious").toBool();
    player.canSeek = properties.value("CanSeek").toBool();
    player.canControl = properties.value("CanControl", true).toBool();
    player.position = position;
    setPlayer(std::move(player));
}
QStringList Mpris::namesOf(const QString &owner) const {
    QStringList names;
    for (auto it = known_.constBegin(); it != known_.constEnd(); ++it)
        if (it->owner == owner)
            names << it.key();
    return names;
}
void Mpris::propertiesChanged(const QDBusMessage &message) {
    const auto arguments = message.arguments();
    if (arguments.size() < 2)
        return;
    const auto interface = arguments[0].toString();
    if (interface != rootInterface && interface != playerInterface)
        return;
    auto changed = map(arguments[1]);
    settle(changed);
    const auto invalidated = arguments.value(2).toStringList();
    for (const auto &name : namesOf(message.service())) {
        auto &known = known_[name];
        auto &properties = interface == rootInterface ? known.root : known.player;
        for (auto it = changed.constBegin(); it != changed.constEnd(); ++it)
            properties[it.key()] = it.value();
        // Some players only say that a property changed; it is read again.
        if (!invalidated.isEmpty()) {
            readAll(name, interface);
            continue;
        }
        publish(name);
        // Where it is now, after a change of track, state or speed.
        if (changed.contains("PlaybackStatus") || changed.contains("Metadata") || changed.contains("Rate"))
            queryPosition(name);
    }
}
void Mpris::seeked(const QDBusMessage &message) {
    const auto position = message.arguments().value(0).toLongLong();
    for (const auto &name : namesOf(message.service()))
        setPosition(name, position);
}
void Mpris::sendCommand(const QString &name, const QString &method) {
    auto message = QDBusMessage::createMethodCall(name, objectPath,
                                                  method == "Raise" ? rootInterface : playerInterface, method);
    dbus::whenAnswered(bus_.asyncCall(message), this,
                       [name, method](QDBusPendingCallWatcher *done) {
                           if (done->isError())
                               std::cerr << "paw media: " << name.toStdString() << " refused "
                                         << method.toStdString() << ": "
                                         << done->error().message().toStdString() << '\n';
                       });
}
void Mpris::sendPosition(const QString &name, const QString &trackId, qint64 position) {
    auto message = QDBusMessage::createMethodCall(name, objectPath, playerInterface, "SetPosition");
    message << QVariant::fromValue(QDBusObjectPath(trackId)) << position;
    bus_.asyncCall(message);
}
void Mpris::queryPosition(const QString &name) {
    auto message = QDBusMessage::createMethodCall(name, objectPath, propertiesInterface, "Get");
    message << QString(playerInterface) << QStringLiteral("Position");
    dbus::whenAnswered(bus_.asyncCall(message), this, [this, name](QDBusPendingCallWatcher *done) {
        if (done->isError())
            return;
        const auto value = done->reply().arguments().value(0).value<QDBusVariant>().variant();
        bool ok = false;
        const auto position = value.toLongLong(&ok);
        if (ok)
            setPosition(name, position);
    });
}

std::unique_ptr<Media> makeMedia() {
    // Without an address libdbus would start a bus of its own ("autolaunch") that no player
    // knows of; a session with no bus has no players. A bus that cannot be reached leaves the
    // backend idle.
    const bool haveBus = !qEnvironmentVariableIsEmpty("DBUS_SESSION_BUS_ADDRESS") ||
                         QFileInfo::exists(qEnvironmentVariable("XDG_RUNTIME_DIR") + "/bus");
    return std::make_unique<Mpris>(haveBus ? QDBusConnection::sessionBus()
                                           : QDBusConnection(QStringLiteral("paw-no-bus")));
}
