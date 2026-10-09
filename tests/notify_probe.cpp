// SPDX-License-Identifier: GPL-3.0-or-later
// A notification client for the end-to-end tests, on the session bus named by
// $DBUS_SESSION_BUS_ADDRESS:
//   notify_probe notify SUMMARY [--body TEXT] [--app NAME] [--icon NAME] [--action KEY=LABEL]...
//                       [--urgency 0|1|2] [--timeout MS] [--replaces ID] [--value PERCENT]
//                       [--tag TEXT]   prints the id
//   notify_probe close ID
//   notify_probe info                  prints the server's name and spec version
//   notify_probe watch                 prints "closed ID REASON" and "action ID KEY" as they come
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QTextStream>
#include <QTimer>
#include <cstdio>

namespace {
constexpr auto service = "org.freedesktop.Notifications";
constexpr auto path = "/org/freedesktop/Notifications";
QTextStream out(stdout);
class Printer : public QObject {
    Q_OBJECT
  public Q_SLOTS:
    void NotificationClosed(uint id, uint reason) { out << "closed " << id << ' ' << reason << Qt::endl; }
    void ActionInvoked(uint id, const QString &key) { out << "action " << id << ' ' << key << Qt::endl; }
};
} // namespace

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments().mid(1);
    auto bus = QDBusConnection::sessionBus();
    if (args.isEmpty() || !bus.isConnected()) {
        std::fputs("usage: notify_probe notify|close|info|watch ... (needs a session bus)\n", stderr);
        return 2;
    }
    auto call = [&](const QString &method, const QVariantList &arguments) {
        auto message = QDBusMessage::createMethodCall(service, path, service, method);
        message.setArguments(arguments);
        return bus.call(message, QDBus::Block, 5000);
    };
    if (args[0] == "watch") {
        Printer printer;
        bus.connect(service, path, service, "NotificationClosed", &printer, SLOT(NotificationClosed(uint, uint)));
        bus.connect(service, path, service, "ActionInvoked", &printer, SLOT(ActionInvoked(uint, QString)));
        out << "watching" << Qt::endl;
        return app.exec();
    }
    if (args[0] == "info") {
        const auto reply = call("GetServerInformation", {});
        if (reply.type() != QDBusMessage::ReplyMessage) {
            std::fputs(qPrintable(reply.errorMessage() + '\n'), stderr);
            return 1;
        }
        out << reply.arguments().at(0).toString() << ' ' << reply.arguments().at(3).toString() << Qt::endl;
        return 0;
    }
    if (args[0] == "close" && args.size() == 2) {
        call("CloseNotification", {args[1].toUInt()});
        return 0;
    }
    if (args[0] == "notify" && args.size() >= 2) {
        QString body, application = "probe", icon;
        QStringList actions;
        QVariantMap hints;
        int timeout = -1;
        uint replaces = 0;
        for (qsizetype i = 2; i + 1 < args.size(); i += 2) {
            const QString &flag = args[i], &value = args[i + 1];
            if (flag == "--body") body = value;
            else if (flag == "--app") application = value;
            else if (flag == "--icon") icon = value;
            else if (flag == "--action") actions << value.section('=', 0, 0) << value.section('=', 1);
            else if (flag == "--urgency") hints["urgency"] = QVariant::fromValue<uchar>(uchar(value.toInt()));
            else if (flag == "--timeout") timeout = value.toInt();
            else if (flag == "--replaces") replaces = value.toUInt();
            else if (flag == "--value") hints["value"] = value.toInt();
            else if (flag == "--tag") hints["x-canonical-private-synchronous"] = value;
            else {
                std::fputs("unknown option\n", stderr);
                return 2;
            }
        }
        const auto reply = call("Notify", {application, replaces, icon, args[1], body, actions, hints, timeout});
        if (reply.type() != QDBusMessage::ReplyMessage) {
            std::fputs(qPrintable(reply.errorMessage() + '\n'), stderr);
            return 1;
        }
        out << reply.arguments().at(0).toUInt() << Qt::endl;
        return 0;
    }
    std::fputs("bad arguments\n", stderr);
    return 2;
}
#include "notify_probe.moc"
