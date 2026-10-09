// SPDX-License-Identifier: GPL-3.0-or-later
// A media player for the end-to-end tests, on the session bus named by $DBUS_SESSION_BUS_ADDRESS:
// org.mpris.MediaPlayer2.NAME with one track, taking every control (see tests/fake_dbus.hpp).
//   mpris_probe NAME [--status Playing|Paused|Stopped] [--title TEXT]
// Prints "ready" once it owns its name, "read" each time a client reads its position (the shell
// does as it finds the player and as its state changes), and a line for each call it gets
// ("PlayPause", "Next", "Previous", "Stop", "Raise", "SetPosition TRACK POSITION"). Reads commands,
// one a line, answering each with "ok":   status Playing|Paused|Stopped   title TEXT   quit
#include "fake_dbus.hpp"
#include <QCoreApplication>
#include <QDBusObjectPath>
#include <QSocketNotifier>
#include <QTextStream>
#include <cstdio>
#include <unistd.h>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QStringList args = app.arguments().mid(1);
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected() || args.isEmpty()) {
        std::fputs("mpris_probe NAME needs a session bus\n", stderr);
        return 2;
    }
    const QString name = "org.mpris.MediaPlayer2." + args.takeFirst();
    QString status = "Paused", title = "Probe song";
    for (qsizetype i = 0; i + 1 < args.size(); i += 2) {
        if (args[i] == "--status")
            status = args[i + 1];
        else if (args[i] == "--title")
            title = args[i + 1];
        else {
            std::fputs("unknown option\n", stderr);
            return 2;
        }
    }
    QTextStream out(stdout);
    auto print = [&out](const QString &line) { out << line << Qt::endl; };
    auto metadata = [](const QString &title) {
        return QVariantMap{{"xesam:title", title},
                           {"xesam:artist", QStringList{"Probe"}},
                           {"mpris:length", qlonglong(200'000'000)},
                           {"mpris:trackid", QVariant::fromValue(QDBusObjectPath("/probe/track/1"))}};
    };
    fakedbus::Object player(bus, "/org/mpris/MediaPlayer2");
    player.properties["org.mpris.MediaPlayer2"] = {{"Identity", "Probe"}, {"CanRaise", true}};
    player.properties["org.mpris.MediaPlayer2.Player"] = {{"PlaybackStatus", status},
                                                          {"Metadata", metadata(title)},
                                                          {"Position", qlonglong(0)},
                                                          {"Rate", 1.0},
                                                          {"CanPlay", true},
                                                          {"CanPause", true},
                                                          {"CanGoNext", true},
                                                          {"CanGoPrevious", true},
                                                          {"CanSeek", true},
                                                          {"CanControl", true}};
    player.methods = [](const QDBusMessage &call, QDBusConnection &connection) {
        connection.send(call.createReply());
        return true;
    };
    player.called = print;
    player.read = [&print](const QString &, const QString &property) {
        if (property == "Position")
            print("read");
    };
    if (!bus.registerService(name)) {
        std::fputs("cannot own the player's name\n", stderr);
        return 1;
    }
    print("ready");
    auto run = [&](const QString &line) {
        const QString word = line.section(' ', 0, 0), rest = line.section(' ', 1);
        if (word == "status") {
            player.set("org.mpris.MediaPlayer2.Player", "PlaybackStatus", rest);
        } else if (word == "title") {
            player.set("org.mpris.MediaPlayer2.Player", "Metadata", metadata(rest));
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
