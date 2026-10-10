// SPDX-License-Identifier: GPL-3.0-or-later
#include "backlight.hpp"
#include "netlink.hpp"
#include <QDir>
#include <QFile>
#if PAW_DBUS
#include "dbus_util.hpp"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#endif
#include <algorithm>
#include <cmath>

namespace {
int number(const QString &path, bool *ok) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *ok = false;
        return 0;
    }
    return file.readAll().trimmed().toInt(ok);
}
} // namespace

Backlight::Backlight(QString root, bool watch, int pollMs, QObject *parent)
    : QObject(parent), root_(std::move(root)) {
    if (watch) // group 1: the kernel's own uevents
        notifier_ = watchNetlink(NETLINK_KOBJECT_UEVENT, 1, this, relevantUevent, [this] { refresh(); });
    timer_.setInterval(pollMs > 0 ? pollMs : 1000);
    connect(&timer_, &QTimer::timeout, this, &Backlight::refresh);
    refresh();
    // Polling is the fallback, and pointless without a backlight.
    if ((watch && !notifier_ && present_) || pollMs > 0)
        timer_.start();
}
bool Backlight::relevantUevent(const QByteArray &message) {
    for (const auto &field : message.split('\0'))
        if (field == "SUBSYSTEM=backlight")
            return true;
    return false;
}
void Backlight::refresh() {
    // The first backlight with a readable level, by name; "actual_brightness" is what the
    // hardware reports, "brightness" what was asked for.
    int level = -1;
    const QDir dir(root_ + "/class/backlight");
    for (const auto &name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        bool ok = false, maxOk = false;
        const int max = number(dir.filePath(name + "/max_brightness"), &maxOk);
        int value = number(dir.filePath(name + "/actual_brightness"), &ok);
        if (!ok)
            value = number(dir.filePath(name + "/brightness"), &ok);
        if (ok && maxOk && max > 0) {
            level = std::clamp(int(std::lround(100.0 * value / max)), 0, 100);
            name_ = name;
            max_ = max;
            break;
        }
    }
    present_ = level >= 0;
    if (!present_) {
        name_.clear();
        max_ = 0;
    }
    if (level == percent_)
        return;
    const bool first = !known_;
    percent_ = level;
    known_ = true;
    Q_EMIT levelChanged();
    if (!first && present_)
        Q_EMIT changed(percent_);
}
void Backlight::setPercent(int percent) {
    if (!present_)
        return;
    percent = std::clamp(percent, 0, 100);
    const auto value = uint(std::lround(percent / 100.0 * max_));
    // Shown at once, so that a slider being dragged does not jump back before the kernel reports
    // the level; the report then matches it and brings no on-screen display.
    if (percent != percent_) {
        percent_ = percent;
        Q_EMIT levelChanged();
    }
    const auto bus = qEnvironmentVariable("PAW_LOGIN1_BUS");
    if (bus.isEmpty() && root_ != "/sys") {
        QFile file(root_ + "/class/backlight/" + name_ + "/brightness");
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
            file.write(QByteArray::number(value) + '\n') < 0)
            Q_EMIT failed("Could not set the brightness: " + file.errorString());
        return;
    }
#if PAW_DBUS
    // The session's own object, which logind lets its user set the backlight through.
    const auto connection = bus.isEmpty() ? QDBusConnection::systemBus()
                                          : QDBusConnection::connectToBus(bus, QStringLiteral("paw-login1"));
    auto message = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.login1"), QStringLiteral("/org/freedesktop/login1/session/auto"),
        QStringLiteral("org.freedesktop.login1.Session"), QStringLiteral("SetBrightness"));
    message << QStringLiteral("backlight") << name_ << value;
    dbus::whenAnswered(connection.asyncCall(message), this, [this](QDBusPendingCallWatcher *done) {
        if (done->isError())
            Q_EMIT failed("Could not set the brightness: " + done->error().message());
    });
#else
    Q_EMIT failed(QStringLiteral("Could not set the brightness: paw was built without Qt's D-Bus module"));
#endif
}
