// SPDX-License-Identifier: GPL-3.0-or-later
// A tray item for the end-to-end tests, on the session bus named by $DBUS_SESSION_BUS_ADDRESS:
// a StatusNotifierItem with a solid square for an icon and a menu of every kind of entry (see
// tests/tray_fake_item.hpp). It registers with the watcher once there is one, as applications do.
//   tray_probe [--path] [--no-getall] [--no-activate] [--color #RRGGBB] [--title TEXT]
// --path registers by object path, as Ayatana's library does, instead of by bus name.
// Prints "registered SERVICE PATH", then a line for each call the host makes ("activate X Y",
// "secondary X Y", "context X Y", "scroll DELTA ORIENTATION", "abouttoshow ID", "event ID TYPE",
// "layout PARENT"). Reads commands, one a line, answering each with "ok":
//   status Passive|Active|NeedsAttention   title TEXT   icon NAME   color #RRGGBB
//   tooltip TEXT   label ID TEXT (a menu entry's)   add ID TEXT (a menu entry)   quit
#include "tray_fake_item.hpp"
#include <QCoreApplication>
#include <QDBusConnectionInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QSocketNotifier>
#include <QTextStream>
#include <cstdio>
#include <unistd.h>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    faketray::registerTypes();
    QStringList args = app.arguments().mid(1);
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        std::fputs("tray_probe needs a session bus\n", stderr);
        return 2;
    }
    QTextStream out(stdout);
    auto print = [&out](const QString &line) { out << line << Qt::endl; };
    const bool byPath = args.removeAll("--path") > 0;
    const QString path = byPath ? "/org/ayatana/NotificationItem/tray_probe" : "/StatusNotifierItem";
    faketray::Item item(bus, path);
    faketray::Menu menu(bus, "/MenuBar");
    item.getAll = args.removeAll("--no-getall") == 0;
    item.activate = args.removeAll("--no-activate") == 0;
    QColor color("#ff00ff");
    QString title = "Tray probe";
    for (qsizetype i = 0; i + 1 < args.size(); i += 2) {
        if (args[i] == "--color")
            color = QColor(args[i + 1]);
        else if (args[i] == "--title")
            title = args[i + 1];
        else {
            std::fputs("unknown option\n", stderr);
            return 2;
        }
    }
    item.called = menu.called = print;
    item.properties = {{"Category", "ApplicationStatus"},
                       {"Id", "tray_probe"},
                       {"Title", title},
                       {"Status", "Active"},
                       {"IconName", ""},
                       {"IconPixmap", faketray::pixmaps(color, {16, 22, 32, 48})},
                       {"ToolTip", faketray::toolTip(title, "A <b>test</b> item")},
                       {"ItemIsMenu", false},
                       {"Menu", QVariant::fromValue(QDBusObjectPath("/MenuBar"))}};
    if (!bus.registerVirtualObject(path, &item) || !bus.registerVirtualObject("/MenuBar", &menu)) {
        std::fputs("cannot export the item\n", stderr);
        return 1;
    }
    // By name, the item takes one of its own, as KDE's and Qt's items do.
    QString service = bus.baseService();
    if (!byPath) {
        service = QString("org.kde.StatusNotifierItem-%1-1").arg(QCoreApplication::applicationPid());
        if (!bus.registerService(service)) {
            std::fputs("cannot own the item's name\n", stderr);
            return 1;
        }
    }
    auto registerItem = [&] {
        auto call = QDBusMessage::createMethodCall(faketray::watcherService, faketray::watcherPath,
                                                   faketray::watcherService, "RegisterStatusNotifierItem");
        call.setArguments({byPath ? path : service});
        auto *watch = new QDBusPendingCallWatcher(bus.asyncCall(call), &app);
        QObject::connect(watch, &QDBusPendingCallWatcher::finished, &app, [&, watch] {
            watch->deleteLater();
            if (watch->isError())
                print("refused " + watch->error().message());
            else
                print("registered " + service + " " + path);
        });
    };
    // Registered again whenever a watcher appears, as the specification asks of items.
    QDBusServiceWatcher watcher(faketray::watcherService, bus, QDBusServiceWatcher::WatchForRegistration);
    QObject::connect(&watcher, &QDBusServiceWatcher::serviceRegistered, &app, registerItem);
    if (bus.interface()->isServiceRegistered(faketray::watcherService))
        registerItem();
    auto run = [&](const QString &line) {
        const QString word = line.section(' ', 0, 0), rest = line.section(' ', 1);
        if (word == "status") {
            item.change("Status", rest, "NewStatus", {rest});
        } else if (word == "title") {
            item.change("Title", rest, "NewTitle");
        } else if (word == "icon") {
            item.change("IconName", rest, "NewIcon");
        } else if (word == "color") {
            item.change("IconPixmap", faketray::pixmaps(QColor(rest), {16, 22, 32, 48}), "NewIcon");
        } else if (word == "tooltip") {
            item.change("ToolTip", faketray::toolTip(title, rest), "NewToolTip");
        } else if (word == "label") {
            menu.propertiesUpdated({{rest.section(' ', 0, 0).toInt(), {{"label", rest.section(' ', 1)}}}}, {});
        } else if (word == "add") {
            const int id = rest.section(' ', 0, 0).toInt();
            menu.entries[id] = {{{"label", rest.section(' ', 1)}}, {}};
            menu.entries[0].children << id;
            menu.layoutUpdated();
        } else if (word == "quit") {
            app.quit();
            return;
        } else {
            print("unknown " + word);
            return;
        }
        print("ok");
    };
    // Read by hand: a buffered reader would sit on lines the notifier no longer reports.
    QSocketNotifier input(STDIN_FILENO, QSocketNotifier::Read);
    QByteArray pending;
    QObject::connect(&input, &QSocketNotifier::activated, &app, [&] {
        char buffer[4096];
        const ssize_t count = read(STDIN_FILENO, buffer, sizeof(buffer));
        if (count <= 0) {
            app.quit();
            return;
        }
        pending.append(buffer, count);
        for (qsizetype end; (end = pending.indexOf('\n')) >= 0; pending.remove(0, end + 1))
            run(QString::fromUtf8(pending.left(end)));
    });
    return app.exec();
}
