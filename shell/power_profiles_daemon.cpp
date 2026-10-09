// SPDX-License-Identifier: GPL-3.0-or-later
#include "power_profiles_daemon.hpp"
#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDBusVariant>

namespace {
// The daemon's name, object and interface: the newer, and the one before 0.20.
struct Name {
    const char *service, *path;
};
constexpr Name names[] = {{"org.freedesktop.UPower.PowerProfiles", "/org/freedesktop/UPower/PowerProfiles"},
                          {"net.hadess.PowerProfiles", "/net/hadess/PowerProfiles"}};
constexpr auto propertiesInterface = "org.freedesktop.DBus.Properties";

// Profiles, aa{sv}: each profile's name ("Profile").
QStringList profileNames(const QVariant &value) {
    QStringList list;
    if (value.metaType() != QMetaType::fromType<QDBusArgument>())
        return list;
    const auto argument = value.value<QDBusArgument>();
    if (argument.currentType() != QDBusArgument::ArrayType)
        return list;
    argument.beginArray();
    while (!argument.atEnd()) {
        QVariantMap profile;
        argument >> profile;
        const auto name = profile.value("Profile").toString();
        if (!name.isEmpty())
            list << name;
    }
    argument.endArray();
    return list;
}
// What the daemon said, read so that nothing refers to the message it came in.
void settle(QVariantMap &properties) {
    if (properties.contains("Profiles"))
        properties["Profiles"] = profileNames(properties.value("Profiles"));
}
} // namespace

PowerProfilesDaemon::PowerProfilesDaemon(const QDBusConnection &bus, QObject *parent)
    : PowerMode(parent), bus_(bus) {
    if (!bus_.isConnected())
        return;
    watcher_ = new QDBusServiceWatcher(this);
    watcher_->setConnection(bus_);
    watcher_->setWatchMode(QDBusServiceWatcher::WatchForOwnerChange);
    for (const auto &name : names) {
        watcher_->addWatchedService(name.service);
        bus_.connect(name.service, name.path, propertiesInterface, "PropertiesChanged", this,
                     SLOT(propertiesChanged(QString, QVariantMap, QStringList)));
    }
    connect(watcher_, &QDBusServiceWatcher::serviceOwnerChanged, this, [this] { find(); });
    find();
}
void PowerProfilesDaemon::find(int from) {
    if (from >= int(std::size(names))) {
        daemon_ = -1;
        properties_.clear();
        publish();
        return;
    }
    auto message = QDBusMessage::createMethodCall("org.freedesktop.DBus", "/org/freedesktop/DBus",
                                                  "org.freedesktop.DBus", "NameHasOwner");
    message << QString(names[from].service);
    auto *call = new QDBusPendingCallWatcher(bus_.asyncCall(message), this);
    connect(call, &QDBusPendingCallWatcher::finished, this, [this, from](QDBusPendingCallWatcher *done) {
        done->deleteLater();
        if (!done->isError() && done->reply().arguments().value(0).toBool()) {
            daemon_ = from;
            read();
        } else {
            find(from + 1);
        }
    });
}
void PowerProfilesDaemon::read() {
    if (daemon_ < 0)
        return;
    const int daemon = daemon_;
    auto message = QDBusMessage::createMethodCall(names[daemon].service, names[daemon].path,
                                                  propertiesInterface, "GetAll");
    message << QString(names[daemon].service);
    auto *call = new QDBusPendingCallWatcher(bus_.asyncCall(message), this);
    connect(call, &QDBusPendingCallWatcher::finished, this, [this, daemon](QDBusPendingCallWatcher *done) {
        done->deleteLater();
        if (daemon != daemon_)
            return;
        if (done->isError()) {
            properties_.clear();
            daemon_ = -1;
        } else {
            properties_ = qdbus_cast<QVariantMap>(done->reply().arguments().value(0));
            settle(properties_);
        }
        publish();
    });
}
void PowerProfilesDaemon::publish() {
    State state;
    state.available = daemon_ >= 0 && properties_.contains("ActiveProfile");
    state.profile = properties_.value("ActiveProfile").toString();
    state.profiles = properties_.value("Profiles").toStringList();
    state.degraded = properties_.value("PerformanceDegraded").toString();
    if (state.degraded.isEmpty())
        state.degraded = properties_.value("PerformanceInhibited").toString();
    update(std::move(state));
}
void PowerProfilesDaemon::propertiesChanged(const QString &interface, const QVariantMap &changed,
                                            const QStringList &invalidated) {
    if (daemon_ < 0 || interface != names[daemon_].service)
        return;
    QVariantMap values = changed;
    settle(values);
    for (auto it = values.constBegin(); it != values.constEnd(); ++it)
        properties_[it.key()] = it.value();
    if (!invalidated.isEmpty())
        read();
    else
        publish();
}
void PowerProfilesDaemon::sendProfile(const QString &profile) {
    if (daemon_ < 0)
        return;
    auto message = QDBusMessage::createMethodCall(names[daemon_].service, names[daemon_].path,
                                                  propertiesInterface, "Set");
    message << QString(names[daemon_].service) << QStringLiteral("ActiveProfile")
            << QVariant::fromValue(QDBusVariant(profile));
    auto *call = new QDBusPendingCallWatcher(bus_.asyncCall(message), this);
    connect(call, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *done) {
        done->deleteLater();
        if (!done->isError())
            return;
        Q_EMIT failed("Could not change the power mode: " + done->error().message());
        // The profile the daemon runs shows again.
        read();
    });
}

std::unique_ptr<PowerMode> makePowerMode() {
    return std::make_unique<PowerProfilesDaemon>(QDBusConnection::systemBus());
}
