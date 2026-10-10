// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QByteArray>
#include <QSocketNotifier>
#include <functional>
#include <linux/netlink.h>
#include <sys/socket.h>
#include <unistd.h>

// Listens on a netlink socket of `protocol` to its multicast `groups`: each time messages have
// come, once all waiting are read, calls `changed` if `relevant` was true of any. Returns the
// socket's notifier, a child of `context`, which closes the socket as it goes; nullptr when the
// socket cannot be opened.
inline QSocketNotifier *watchNetlink(int protocol, unsigned groups, QObject *context,
                                     std::function<bool(const QByteArray &)> relevant,
                                     std::function<void()> changed) {
    const int fd = ::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, protocol);
    if (fd < 0)
        return nullptr;
    sockaddr_nl address{};
    address.nl_family = AF_NETLINK;
    address.nl_groups = groups;
    if (::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) < 0) {
        ::close(fd);
        return nullptr;
    }
    auto *notifier = new QSocketNotifier(fd, QSocketNotifier::Read, context);
    QObject::connect(notifier, &QObject::destroyed, [fd] { ::close(fd); });
    QObject::connect(notifier, &QSocketNotifier::activated, context,
                     [fd, relevant = std::move(relevant), changed = std::move(changed)] {
                         bool any = false;
                         char buffer[8192];
                         for (ssize_t n; (n = ::recv(fd, buffer, sizeof buffer, MSG_DONTWAIT)) > 0;)
                             any = any || relevant(QByteArray(buffer, int(n)));
                         if (any)
                             changed();
                     });
    return notifier;
}
