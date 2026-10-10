// SPDX-License-Identifier: GPL-3.0-or-later
#include "system_status.hpp"
#include "netlink.hpp"
#include <QDir>
#include <QFile>
#include <algorithm>
#include <linux/rtnetlink.h>

namespace {
constexpr int pollMs = 5000;      // no kernel messages to rely on
constexpr int backupPollMs = 120000; // with them, a safety net for the battery level
} // namespace

SystemStatus::SystemStatus(QString root, QObject *parent, bool watch)
    : QObject(parent), root_(std::move(root)) {
    debounce_.setSingleShot(true);
    debounce_.setInterval(200); // a burst of messages (an interface coming up) is one read
    connect(&debounce_, &QTimer::timeout, this, &SystemStatus::refresh);
    if (watch) {
        auto changed = [this] { debounce_.start(); };
        // Group 1: the kernel's own uevents; every link change.
        for (auto *notifier :
             {watchNetlink(NETLINK_KOBJECT_UEVENT, 1, this, relevantUevent, changed),
              watchNetlink(NETLINK_ROUTE, RTMGRP_LINK, this, [](const QByteArray &) { return true; },
                           changed)})
            if (notifier)
                sockets_.append(notifier);
    }
    connect(&timer_, &QTimer::timeout, this, &SystemStatus::refresh);
    refresh();
}
bool SystemStatus::relevantUevent(const QByteArray &message) {
    for (const auto &field : message.split('\0'))
        if (field == "SUBSYSTEM=power_supply" || field == "SUBSYSTEM=net")
            return true;
    return false;
}
// Polling is the only source without the sockets; with them a slow timer covers the battery
// level, which changes without a message, and it is off where there is no battery.
void SystemStatus::updatePolling(bool battery) {
    const int interval = !watching() ? pollMs : battery ? backupPollMs : 0;
    if (interval == pollInterval())
        return;
    if (interval > 0)
        timer_.start(interval);
    else
        timer_.stop();
}
QString SystemStatus::read(const QString &path) const {
    QFile file(root_ + "/" + path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
}
void SystemStatus::refresh() {
    // The first battery (type "Battery", not a mains adapter or a peripheral's "scope=Device").
    bool present = false;
    int percent = 0;
    QString state = "unknown";
    const QDir supplies(root_ + "/class/power_supply");
    for (const auto &name : supplies.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const auto base = "class/power_supply/" + name + "/";
        if (read(base + "type") != "Battery" || read(base + "scope") == "Device")
            continue;
        bool ok = false;
        percent = read(base + "capacity").toInt(&ok);
        if (!ok)
            continue;
        present = true;
        percent = std::clamp(percent, 0, 100);
        const auto status = read(base + "status").toLower();
        state = status == "charging" ? "charging"
                : status == "full" || status == "not charging" ? "full"
                : status == "discharging" ? "discharging" : "unknown";
        break;
    }
    // A wired link that is up wins over Wi-Fi only when Wi-Fi is not; either beats none.
    QString net = "none", iface;
    const QDir nets(root_ + "/class/net");
    for (const auto &name : nets.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const auto base = "class/net/" + name + "/";
        if (name == "lo" || !QFile::exists(root_ + "/" + base + "device"))
            continue; // loopback and virtual interfaces (bridges, veth, tun)
        const bool wireless = QFile::exists(root_ + "/" + base + "wireless"),
                   up = read(base + "operstate") == "up";
        const QString kind = !up ? "disconnected" : wireless ? "wifi" : "ethernet";
        auto rank = [](const QString &k) { return k == "ethernet" ? 3 : k == "wifi" ? 2 : 1; };
        if (net == "none" || rank(kind) > rank(net)) {
            net = kind;
            iface = name;
        }
    }
    updatePolling(present);
    if (present == batteryPresent_ && percent == batteryPercent_ && state == batteryState_ &&
        net == networkState_ && iface == networkInterface_)
        return;
    batteryPresent_ = present;
    batteryPercent_ = percent;
    batteryState_ = state;
    networkState_ = net;
    networkInterface_ = iface;
    Q_EMIT changed();
}
QString SystemStatus::batteryText() const {
    if (!batteryPresent_)
        return {};
    const QString suffix = batteryState_ == "charging"      ? ", charging"
                           : batteryState_ == "full"        ? ", full"
                           : batteryState_ == "discharging" ? ", on battery"
                                                            : QString();
    return "Battery " + QString::number(batteryPercent_) + "%" + suffix;
}
QString SystemStatus::networkText() const {
    if (networkState_ == "none")
        return {};
    const QString what = networkState_ == "wifi" ? "Wi-Fi" : networkState_ == "ethernet" ? "Wired" : "";
    return networkState_ == "disconnected" ? "Network disconnected (" + networkInterface_ + ")"
                                           : what + " connected (" + networkInterface_ + ")";
}
