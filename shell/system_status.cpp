// SPDX-License-Identifier: GPL-3.0-or-later
#include "system_status.hpp"
#include <QDir>
#include <QFile>
#include <QSocketNotifier>
#include <algorithm>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <unistd.h>

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
        openNetlink(NETLINK_KOBJECT_UEVENT, 1, true); // group 1: the kernel's own uevents
        openNetlink(NETLINK_ROUTE, RTMGRP_LINK, false);
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
void SystemStatus::openNetlink(int protocol, unsigned groups, bool uevents) {
    const int fd = ::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, protocol);
    if (fd < 0)
        return;
    sockaddr_nl address{};
    address.nl_family = AF_NETLINK;
    address.nl_groups = groups;
    if (::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) < 0) {
        ::close(fd);
        return;
    }
    auto *notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, [this, fd, uevents] {
        bool relevant = false;
        char buffer[8192];
        for (ssize_t n; (n = ::recv(fd, buffer, sizeof buffer, MSG_DONTWAIT)) > 0;)
            relevant = relevant || !uevents || relevantUevent(QByteArray(buffer, int(n)));
        if (relevant)
            debounce_.start();
    });
    sockets_.append(notifier);
}
SystemStatus::~SystemStatus() {
    for (auto *notifier : sockets_) {
        const int fd = int(notifier->socket());
        delete notifier;
        ::close(fd);
    }
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
