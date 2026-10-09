// SPDX-License-Identifier: GPL-3.0-or-later
// The system tray on a private session bus: a dbus-daemon this test starts and kills, never the
// real one. Fake items (tests/tray_fake_item.hpp), each on a connection of its own, play the
// applications.
#include "tray_fake_item.hpp"
#include "tray_host.hpp"
#include "tray_watcher.hpp"
#include <QBuffer>
#include <QDBusConnectionInterface>
#include <QDir>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <memory>
#include <sys/stat.h>

using namespace faketray;

// How a test application's item behaves.
struct Options {
    bool byPath = false; // registers by object path, as Ayatana's library does
    bool getAll = true;
    bool activate = true;
    bool registers = true;
};

class TrayDbusTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir_;
    QProcess bus_;
    QString address_;
    int connections_ = 0;

    // A new connection to the private bus, as another process would have.
    QDBusConnection connect() {
        return QDBusConnection::connectToBus(address_, QString("tray-test-%1").arg(++connections_));
    }
    // Closes a connection, as a process quitting would. It only closes once nothing refers to it,
    // so `bus` is cleared first.
    void disconnect(QDBusConnection &bus) {
        const QString name = bus.name();
        bus = QDBusConnection(QString());
        QDBusConnection::disconnectFromBus(name);
    }
    // Calls that let this thread's event loop run, since the watcher lives in it too.
    QDBusMessage callWatcher(const QDBusConnection &bus, const QString &method, const QVariantList &arguments) {
        auto message = QDBusMessage::createMethodCall(watcherService, watcherPath, watcherService, method);
        message.setArguments(arguments);
        return bus.call(message, QDBus::BlockWithGui, 5000);
    }
    QVariant watcherProperty(const QString &name) {
        auto message = QDBusMessage::createMethodCall(watcherService, watcherPath, propertiesInterface, "Get");
        message.setArguments({QString(watcherService), name});
        const auto reply = connect().call(message, QDBus::BlockWithGui, 5000);
        return reply.type() == QDBusMessage::ReplyMessage
                   ? reply.arguments().value(0).value<QDBusVariant>().variant()
                   : QVariant();
    }
    QStringList registered() { return watcherProperty("RegisteredStatusNotifierItems").toStringList(); }

    // An application with one item and its menu, on a connection of its own.
    struct App {
        QDBusConnection bus{QString()};
        std::unique_ptr<Item> item;
        std::unique_ptr<Menu> menu;
        QString service;
        QString key() const { return service + item->path(); }
    };
    int names_ = 0;
    std::unique_ptr<App> start(const QString &title, Options options = {}) {
        auto app = std::make_unique<App>();
        app->bus = connect();
        const QString path = options.byPath ? "/org/ayatana/NotificationItem/fake" : "/StatusNotifierItem";
        app->item = std::make_unique<Item>(app->bus, path);
        app->menu = std::make_unique<Menu>(app->bus, "/MenuBar");
        app->item->getAll = options.getAll;
        app->item->activate = options.activate;
        app->item->properties = {{"Id", "fake"},
                                 {"Title", title},
                                 {"Status", "Active"},
                                 {"IconName", "fake-icon"},
                                 {"ToolTip", toolTip(title, "Some <b>bold</b> text")},
                                 {"ItemIsMenu", false},
                                 {"Menu", QVariant::fromValue(QDBusObjectPath("/MenuBar"))}};
        app->bus.registerVirtualObject(path, app->item.get());
        app->bus.registerVirtualObject("/MenuBar", app->menu.get());
        app->service = app->bus.baseService();
        if (!options.byPath) {
            app->service = QString("org.kde.StatusNotifierItem-%1-%2").arg(QCoreApplication::applicationPid()).arg(++names_);
            app->bus.registerService(app->service);
        }
        if (options.registers)
            callWatcher(app->bus, "RegisterStatusNotifierItem", {options.byPath ? path : app->service});
        return app;
    }
    void quit(std::unique_ptr<App> &app) {
        app->bus.unregisterObject(app->item->path());
        app->bus.unregisterObject(app->menu->path());
        disconnect(app->bus);
        app.reset();
    }

  private Q_SLOTS:
    void initTestCase() {
        if (QStandardPaths::findExecutable("dbus-daemon").isEmpty())
            QSKIP("dbus-daemon is not installed");
        registerTypes();
        // No service directories: a bus name nobody owns stays unowned, whatever is installed.
        QFile config(dir_.filePath("bus.conf"));
        QVERIFY(config.open(QIODevice::WriteOnly));
        config.write(("<busconfig><type>session</type><listen>unix:dir=" + dir_.path() +
                      "</listen><auth>EXTERNAL</auth><policy context=\"default\">"
                      "<allow send_destination=\"*\" eavesdrop=\"true\"/><allow eavesdrop=\"true\"/>"
                      "<allow own=\"*\"/></policy></busconfig>").toUtf8());
        config.close();
        bus_.start("dbus-daemon", {"--config-file=" + dir_.filePath("bus.conf"), "--nofork",
                                   "--print-address=1"});
        QVERIFY(bus_.waitForStarted());
        QVERIFY(bus_.waitForReadyRead(5000));
        address_ = QString::fromUtf8(bus_.readLine()).trimmed();
        QVERIFY2(address_.startsWith("unix:"), qPrintable(address_));
    }
    void cleanupTestCase() {
        if (bus_.state() != QProcess::NotRunning) {
            bus_.kill(); // this test's own daemon, by its process
            bus_.waitForFinished(3000);
        }
    }

    // The watcher: items by name and by path, hosts, and what goes when its owner leaves.
    void watcher() {
        TrayWatcher watcher;
        QVERIFY2(watcher.start(connect()), qPrintable(watcher.error()));
        TrayWatcher second;
        QVERIFY(!second.start(connect()));
        QVERIFY(second.error().contains("another watcher"));
        QSignalSpy added(&watcher, &TrayWatcher::StatusNotifierItemRegistered);
        QSignalSpy removed(&watcher, &TrayWatcher::StatusNotifierItemUnregistered);
        QCOMPARE(watcherProperty("ProtocolVersion").toInt(), 0);
        QCOMPARE(watcherProperty("IsStatusNotifierHostRegistered").toBool(), false);
        QVERIFY(registered().isEmpty());

        // By name: the item's object is at /StatusNotifierItem.
        auto named = connect();
        const QString name = QString("org.kde.StatusNotifierItem-%1-7").arg(QCoreApplication::applicationPid());
        QVERIFY(named.registerService(name));
        QCOMPARE(callWatcher(named, "RegisterStatusNotifierItem", {name}).type(), QDBusMessage::ReplyMessage);
        QCOMPARE(registered(), QStringList{name + "/StatusNotifierItem"});
        QCOMPARE(added.size(), 1);
        QCOMPARE(added[0][0].toString(), name + "/StatusNotifierItem");
        // Again is no second entry.
        QCOMPARE(callWatcher(named, "RegisterStatusNotifierItem", {name}).type(), QDBusMessage::ReplyMessage);
        QCOMPARE(registered().size(), 1);

        // By path, as Ayatana's library does: the caller's connection is the service.
        auto ayatana = connect();
        QCOMPARE(callWatcher(ayatana, "RegisterStatusNotifierItem", {"/org/ayatana/NotificationItem/x"}).type(),
                 QDBusMessage::ReplyMessage);
        const QString byPath = ayatana.baseService() + "/org/ayatana/NotificationItem/x";
        QCOMPARE(registered(), (QStringList{name + "/StatusNotifierItem", byPath}));

        // Nonsense is refused: a name nobody owns, a malformed path, an empty name.
        auto other = connect();
        auto refused = callWatcher(other, "RegisterStatusNotifierItem", {"org.example.Nobody"});
        QCOMPARE(refused.type(), QDBusMessage::ErrorMessage);
        QCOMPARE(callWatcher(other, "RegisterStatusNotifierItem", {"/bad//path"}).type(), QDBusMessage::ErrorMessage);
        QCOMPARE(callWatcher(other, "RegisterStatusNotifierItem", {"/path\n"}).type(), QDBusMessage::ErrorMessage);
        QCOMPARE(callWatcher(other, "RegisterStatusNotifierItem", {QString()}).type(), QDBusMessage::ErrorMessage);
        QCOMPARE(callWatcher(other, "RegisterStatusNotifierItem", {"not a name"}).type(), QDBusMessage::ErrorMessage);
        QCOMPARE(registered().size(), 2);

        // A host: registered while its name is owned.
        QSignalSpy hostAdded(&watcher, &TrayWatcher::StatusNotifierHostRegistered);
        QSignalSpy hostRemoved(&watcher, &TrayWatcher::StatusNotifierHostUnregistered);
        auto host = connect();
        QVERIFY(host.registerService("org.kde.StatusNotifierHost-test"));
        QCOMPARE(callWatcher(host, "RegisterStatusNotifierHost", {"org.kde.StatusNotifierHost-test"}).type(),
                 QDBusMessage::ReplyMessage);
        QCOMPARE(hostAdded.size(), 1);
        QCOMPARE(watcherProperty("IsStatusNotifierHostRegistered").toBool(), true);
        QCOMPARE(callWatcher(other, "RegisterStatusNotifierHost", {"org.example.NoHost"}).type(),
                 QDBusMessage::ErrorMessage);

        // Owners leaving take their items and hosts with them.
        disconnect(ayatana);
        QTRY_COMPARE(removed.size(), 1);
        QCOMPARE(removed[0][0].toString(), byPath);
        QCOMPARE(registered(), QStringList{name + "/StatusNotifierItem"});
        QVERIFY(named.unregisterService(name)); // the name goes, the connection stays
        QTRY_COMPARE(removed.size(), 2);
        QVERIFY(registered().isEmpty());
        disconnect(host);
        QTRY_COMPARE(hostRemoved.size(), 1);
        QCOMPARE(watcherProperty("IsStatusNotifierHostRegistered").toBool(), false);
        disconnect(named);
        disconnect(other);
    }
    // The name is free again once the watcher goes.
    void watcherReleasesTheName() {
        auto bus = connect();
        {
            TrayWatcher watcher;
            QVERIFY2(watcher.start(bus), qPrintable(watcher.error()));
            QVERIFY(bus.interface()->isServiceRegistered(watcherService));
        }
        QTRY_VERIFY(!bus.interface()->isServiceRegistered(watcherService));
        TrayWatcher again;
        QVERIFY2(again.start(connect()), qPrintable(again.error()));
    }

    // The host serves the watcher, reads items registered either way, follows their changes,
    // passes on clicks and the wheel, and drops items whose application leaves.
    void host() {
        TrayModel model;
        TrayHost host(model);
        QVERIFY2(host.start(connect()), qPrintable(host.error()));
        QVERIFY(host.ownsWatcher());
        QCOMPARE(host.hostName(), QString("org.kde.StatusNotifierHost-%1").arg(QCoreApplication::applicationPid()));
        QCOMPARE(watcherProperty("IsStatusNotifierHostRegistered").toBool(), true);
        auto named = start("Named");
        QTRY_COMPARE(model.count(), 1);
        const TrayItem *item = model.find(named->key());
        QVERIFY(item);
        QCOMPARE(item->title, QString("Named"));
        QCOMPARE(item->id, QString("fake"));
        QCOMPARE(item->status, QString("Active"));
        QCOMPARE(item->iconName, QString("fake-icon"));
        QCOMPARE(item->toolTipTitle, QString("Named"));
        QCOMPARE(item->toolTipText, QString("Some bold text"));
        QCOMPARE(item->menuPath, QString("/MenuBar"));
        QCOMPARE(item->itemIsMenu, false);
        QCOMPARE(model.shown(), 1);
        // Ayatana's way: by path, with neither GetAll nor Activate. It comes after, in the order
        // of registration.
        auto ayatana = start("Ayatana", {.byPath = true, .getAll = false, .activate = false});
        QTRY_COMPARE(model.count(), 2);
        QCOMPARE(model.items()[1].key, ayatana->key());
        QCOMPARE(model.items()[1].title, QString("Ayatana"));
        QCOMPARE(model.items()[0].key, named->key());

        // Changes, announced by their signals.
        named->item->change("Title", "Renamed", "NewTitle");
        QTRY_COMPARE(model.find(named->key())->title, QString("Renamed"));
        const int revision = model.find(named->key())->revision;
        named->item->change("IconName", "other-icon", "NewIcon");
        QTRY_COMPARE(model.find(named->key())->iconName, QString("other-icon"));
        QVERIFY(model.find(named->key())->revision > revision);
        named->item->change("Status", "Passive", "NewStatus", {"Passive"});
        QTRY_COMPARE(model.shown(), 1);
        QCOMPARE(model.count(), 2);
        named->item->change("Status", "NeedsAttention", "NewStatus", {"NeedsAttention"});
        QTRY_COMPARE(model.shown(), 2);
        QCOMPARE(model.find(named->key())->status, QString("NeedsAttention"));
        named->item->change("ToolTip", toolTip("Tip", "a &amp; b<br>c"), "NewToolTip");
        QTRY_COMPARE(model.find(named->key())->toolTipText, QString("a & b\nc"));
        QCOMPARE(model.find(named->key())->toolTip(), QString("Tip\na & b\nc"));
        named->item->change("IconThemePath", "/nonexistent/icons", "NewIconThemePath", {"/nonexistent/icons"});
        QTRY_COMPARE(model.find(named->key())->iconThemePath, QString("/nonexistent/icons"));
        ayatana->item->change("Title", "Ayatana again", "NewTitle"); // read with one Get each
        QTRY_COMPARE(model.find(ayatana->key())->title, QString("Ayatana again"));
        // An item announcing changes without pause is still read while it goes on.
        {
            int frame = 0;
            QTimer stream;
            QObject::connect(&stream, &QTimer::timeout, [&] {
                named->item->change("Title", QString("Frame %1").arg(++frame), "NewTitle");
            });
            stream.start(2);
            QTRY_VERIFY(model.find(named->key())->title.startsWith("Frame"));
        }
        named->item->change("Title", "Renamed", "NewTitle");
        QTRY_COMPARE(model.find(named->key())->title, QString("Renamed"));

        // What the panel asks reaches the item.
        model.activate(named->key(), 10, 20);
        QTRY_COMPARE(named->item->calls.value(0), QString("activate 10 20"));
        model.secondaryActivate(named->key(), 3, 4);
        model.contextMenu(named->key(), 5, 6);
        model.scroll(named->key(), -240, false);
        model.scroll(named->key(), 120, true);
        QTRY_COMPARE(named->item->calls.size(), 5);
        QCOMPARE(named->item->calls, (QStringList{"activate 10 20", "secondary 3 4", "context 5 6",
                                                  "scroll -240 vertical", "scroll 120 horizontal"}));
        // An item without Activate asks the panel for its menu instead.
        QSignalSpy refused(&model, &TrayModel::activationRefused);
        model.activate(ayatana->key(), 1, 2);
        QTRY_COMPARE(refused.size(), 1);
        QCOMPARE(refused[0][0].toString(), ayatana->key());

        // An application quitting, or giving up its name, takes its item away.
        quit(ayatana);
        QTRY_COMPARE(model.count(), 1);
        QCOMPARE(registered(), QStringList{named->key()});
        named->bus.unregisterService(named->service);
        QTRY_COMPARE(model.count(), 0);
        quit(named);
    }
    // Properties of the wrong type, or missing, take their defaults.
    void mistypedProperties() {
        TrayModel model;
        TrayHost host(model);
        QVERIFY(host.start(connect()));
        auto app = start("Odd", {.registers = false});
        app->item->properties = {{"Id", 7},
                                 {"Title", QStringList{"a", "b"}},
                                 {"Status", "Sleeping"},
                                 {"IconName", true},
                                 {"ToolTip", "just a string"},
                                 {"ItemIsMenu", "yes"},
                                 {"Menu", 42},
                                 {"IconPixmap", "not pixels"}};
        callWatcher(app->bus, "RegisterStatusNotifierItem", {app->service});
        QTRY_COMPARE(model.count(), 1);
        const TrayItem *item = model.find(app->key());
        QCOMPARE(item->id, QString());
        QCOMPARE(item->title, QString());
        QCOMPARE(item->status, QString("Active"));
        QCOMPARE(item->iconName, QString());
        QCOMPARE(item->toolTip(), QString());
        QCOMPARE(item->itemIsMenu, false);
        QCOMPARE(item->menuPath, QString());
        // A menu given as a string path is taken; Chromium's "no menu" path is none.
        app->item->change("Menu", "/Some/Menu", "NewMenu");
        QTRY_COMPARE(model.find(app->key())->menuPath, QString("/Some/Menu"));
        app->item->change("Menu", QVariant::fromValue(QDBusObjectPath("/NO_DBUSMENU")), "NewMenu");
        QTRY_COMPARE(model.find(app->key())->menuPath, QString());
        quit(app);
    }
    // IconPixmap is ARGB32 in network byte order; the size nearest the panel's is picked, a named
    // icon wins over pixels, the attention icon shows while the item needs attention, and the
    // overlay sits in the bottom right corner.
    void pictures() {
        auto solid = [](const QColor &color, int size) {
            QImage image(size, size, QImage::Format_ARGB32);
            image.fill(color);
            return image;
        };
        QByteArray bytes;
        for (int i = 0; i < 4; ++i)
            bytes.append("\x80\x11\x22\x33", 4); // A, R, G, B
        QCOMPARE(trayImageFromArgb32(2, 2, bytes).pixelColor(1, 1), QColor(0x11, 0x22, 0x33, 0x80));
        QVERIFY(trayImageFromArgb32(2, 2, bytes.left(15)).isNull());
        QVERIFY(trayImageFromArgb32(0, 2, bytes).isNull());
        QVERIFY(trayImageFromArgb32(-1, -1, bytes).isNull());
        QVERIFY(trayImageFromArgb32(100000, 100000, bytes).isNull());
        const QList<QImage> sizes{solid(Qt::red, 16), solid(Qt::green, 32), solid(Qt::blue, 64)};
        QCOMPARE(trayPickPixmap(sizes, {22, 22}).pixelColor(5, 5), QColor(Qt::green));
        QCOMPARE(trayPickPixmap(sizes, {22, 22}).size(), QSize(22, 22));
        QCOMPARE(trayPickPixmap(sizes, {16, 16}).pixelColor(5, 5), QColor(Qt::red));
        QCOMPARE(trayPickPixmap(sizes, {100, 100}).pixelColor(5, 5), QColor(Qt::blue));
        QCOMPARE(trayPickPixmap(sizes, {8, 8}).pixelColor(1, 1), QColor(Qt::red));
        QVERIFY(trayPickPixmap({}, {22, 22}).isNull());

        TrayModel model;
        TrayHost host(model);
        QVERIFY(host.start(connect()));
        auto app = start("Pictures", {.registers = false});
        // Sizes in any order, one of them malformed.
        app->item->properties["IconName"] = "";
        app->item->properties["IconPixmap"] = QVariant::fromValue(
            QList<Pixmap>{square(Qt::blue, 64), {30, 30, "short"}, square(Qt::red, 16), square(Qt::green, 32)});
        callWatcher(app->bus, "RegisterStatusNotifierItem", {app->service});
        QTRY_COMPARE(model.count(), 1);
        const int serial = model.find(app->key())->serial;
        QCOMPARE(model.find(app->key())->icon.size(), 3);
        QCOMPARE(model.picture(serial, {22, 22}).pixelColor(11, 11), QColor(Qt::green));
        QCOMPARE(model.picture(serial, {64, 64}).pixelColor(11, 11), QColor(Qt::blue));
        app->item->properties["AttentionIconPixmap"] = pixmaps(Qt::yellow, {22});
        app->item->change("Status", "NeedsAttention", "NewStatus", {"NeedsAttention"});
        QTRY_COMPARE(model.picture(serial, {22, 22}).pixelColor(11, 11), QColor(Qt::yellow));
        app->item->change("Status", "Active", "NewStatus", {"Active"});
        QTRY_COMPARE(model.picture(serial, {22, 22}).pixelColor(11, 11), QColor(Qt::green));
        app->item->change("OverlayIconPixmap", pixmaps(Qt::white, {11}), "NewOverlayIcon");
        QTRY_COMPARE(model.picture(serial, {22, 22}).pixelColor(20, 20), QColor(Qt::white));
        QCOMPARE(model.picture(serial, {22, 22}).pixelColor(2, 2), QColor(Qt::green));
        app->item->change("OverlayIconPixmap", QVariant(), "NewOverlayIcon");
        QTRY_COMPARE(model.picture(serial, {22, 22}).pixelColor(20, 20), QColor(Qt::green));

        // A name in the item's own folder, or in a theme laid out in it (the largest), or a file
        // by absolute path wins over the pixels.
        QTemporaryDir icons;
        QVERIFY(solid(Qt::cyan, 22).save(icons.filePath("probe-icon.png")));
        QVERIFY(QDir(icons.path()).mkpath("hicolor/22x22/apps") && QDir(icons.path()).mkpath("hicolor/48x48/apps"));
        QVERIFY(solid(Qt::magenta, 22).save(icons.filePath("hicolor/22x22/apps/nested.png")));
        QVERIFY(solid(Qt::darkMagenta, 48).save(icons.filePath("hicolor/48x48/apps/nested.png")));
        app->item->properties["IconThemePath"] = icons.path();
        app->item->change("IconName", "probe-icon", "NewIcon");
        QTRY_COMPARE(model.picture(serial, {22, 22}).pixelColor(11, 11), QColor(Qt::cyan));
        app->item->change("IconName", "nested", "NewIcon");
        QTRY_COMPARE(model.picture(serial, {22, 22}).pixelColor(11, 11), QColor(Qt::darkMagenta));
        QVERIFY(solid(Qt::darkCyan, 22).save(icons.filePath("absolute.png")));
        app->item->change("IconName", icons.filePath("absolute.png"), "NewIcon");
        QTRY_COMPARE(model.picture(serial, {22, 22}).pixelColor(11, 11), QColor(Qt::darkCyan));
        // A name found nowhere, a relative path or a pipe falls back on the pixels; the pipe is
        // never read.
        QCOMPARE(mkfifo(QFile::encodeName(icons.filePath("pipe.png")).constData(), 0600), 0);
        for (const QString &name : {QString("no-such-icon-anywhere"), QString("../probe-icon"), icons.filePath("pipe.png")}) {
            app->item->change("IconName", name, "NewIcon");
            QTRY_COMPARE(model.find(app->key())->iconName, name);
            QCOMPARE(model.picture(serial, {22, 22}).pixelColor(11, 11), QColor(Qt::green));
        }
        QCOMPARE(trayIconFile("probe", "/"), QString()); // a bounded look, even from the root
        // Without any icon there is no picture: the panel draws a stand-in.
        app->item->properties.remove("IconPixmap");
        app->item->change("IconName", "", "NewIcon");
        QTRY_VERIFY(model.picture(serial, {22, 22}).isNull());
        quit(app);
    }
    // com.canonical.dbusmenu: the layout with its kinds of entries, AboutToShow and the events,
    // and the two ways an application changes the menu.
    void menus() {
        QCOMPARE(trayMenuLabel("_File"), QString("File"));
        QCOMPARE(trayMenuLabel("Save __as_"), QString("Save _as"));
        QCOMPARE(trayMenuLabel("a___b"), QString("a_b"));
        TrayModel model;
        TrayHost host(model);
        QVERIFY(host.start(connect()));
        QSignalSpy changed(&model, &TrayModel::menuChanged);
        auto app = start("Menu");
        QTRY_VERIFY(!model.menu(app->key(), 0).isEmpty());
        auto labels = [&](int parent) {
            QStringList list;
            for (const auto &entry : model.menu(app->key(), parent))
                list << (entry.toMap()["separator"].toBool() ? "-" : entry.toMap()["label"].toString());
            return list;
        };
        auto entry = [&](int parent, int index) { return model.menu(app->key(), parent).value(index).toMap(); };
        // The hidden entry is left out.
        QCOMPARE(labels(0), (QStringList{"Open probe", "-", "Options", "Disabled", "Quit"}));
        QCOMPARE(entry(0, 2)["submenu"].toBool(), true);
        QCOMPARE(entry(0, 2)["id"].toInt(), 3);
        QCOMPARE(entry(0, 3)["enabled"].toBool(), false);
        QCOMPARE(entry(0, 0)["enabled"].toBool(), true);
        QVERIFY(entry(0, 4)["icon"].toString().startsWith("image://tray/"));
        QVERIFY(entry(0, 0)["icon"].toString().isEmpty());
        QCOMPARE(labels(3), (QStringList{"Show _hidden", "Radio A", "Radio B"}));
        QCOMPARE(entry(3, 0)["toggle"].toString(), QString("checkmark"));
        QCOMPARE(entry(3, 0)["checked"].toBool(), true);
        QCOMPARE(entry(3, 1)["toggle"].toString(), QString("radio"));
        QCOMPARE(entry(3, 1)["checked"].toBool(), true);
        QCOMPARE(entry(3, 2)["checked"].toBool(), false);
        QVERIFY(model.menu(app->key(), 99).isEmpty());
        QVERIFY(model.menu("no such item", 0).isEmpty());

        // Opening asks AboutToShow and says so; picking an entry and closing are events.
        app->menu->calls.clear();
        model.openMenu(app->key(), 0);
        model.openMenu(app->key(), 3);
        model.clickMenu(app->key(), 31);
        model.closeMenu(app->key(), 3);
        QTRY_COMPARE(app->menu->calls.size(), 6);
        QCOMPARE(app->menu->calls, (QStringList{"abouttoshow 0", "event 0 opened", "abouttoshow 3",
                                                "event 3 opened", "event 31 clicked", "event 3 closed"}));
        // AboutToShow saying the entries changed fetches them again.
        app->menu->needUpdate = true;
        app->menu->entries[1].properties["label"] = "Updated on opening";
        model.openMenu(app->key(), 0);
        QTRY_COMPARE(labels(0).value(0), QString("Updated on opening"));
        app->menu->needUpdate = false;

        // LayoutUpdated: the layout is fetched again.
        changed.clear();
        app->menu->entries[7] = {{{"label", "Added"}}, {}};
        app->menu->entries[0].children << 7;
        app->menu->layoutUpdated();
        QTRY_COMPARE(labels(0).value(5), QString("Added"));
        QVERIFY(changed.size() >= 1);
        QCOMPARE(changed.last()[0].toString(), app->key());
        // ItemsPropertiesUpdated: entries change in place, and removed properties take their
        // defaults.
        app->menu->propertiesUpdated({{1, {{"label", "_Changed"}}}, {7, {{"toggle-type", "checkmark"}, {"toggle-state", 1}}}},
                                     {{4, {"enabled"}}, {99, {"label"}}});
        QTRY_COMPARE(labels(0).value(0), QString("Changed"));
        QCOMPARE(entry(0, 3)["enabled"].toBool(), true);
        QCOMPARE(entry(0, 5)["checked"].toBool(), true);

        // Separators only between entries, never two together.
        app->menu->entries = {{0, {{}, {1, 2, 3, 4, 5, 6, 7}}},
                              {1, {{{"type", "separator"}}, {}}},
                              {2, {{{"label", "A"}}, {}}},
                              {3, {{{"type", "separator"}}, {}}},
                              {4, {{{"type", "separator"}}, {}}},
                              {5, {{{"label", "B"}}, {}}},
                              {6, {{{"label", "Gone"}, {"visible", false}}, {}}},
                              {7, {{{"type", "separator"}}, {}}}};
        app->menu->layoutUpdated();
        QTRY_COMPARE(labels(0), (QStringList{"A", "-", "B"}));

        // icon-data, a PNG, is the entry's icon.
        QImage red(16, 16, QImage::Format_ARGB32);
        red.fill(Qt::red);
        QByteArray png;
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly) && red.save(&buffer, "PNG"));
        app->menu->propertiesUpdated({{2, {{"icon-data", png}}}}, {});
        QTRY_VERIFY(!entry(0, 0)["icon"].toString().isEmpty());
        const int serial = model.find(app->key())->serial;
        QCOMPARE(model.menuPicture(serial, 2, {16, 16}).pixelColor(8, 8), QColor(Qt::red));
        QVERIFY(model.menuPicture(serial, 5, {16, 16}).isNull());

        // A layout that is not one, or none at all, leaves the menu as it was.
        app->menu->layoutReply = [] { return QVariantList{1u, QString("not a layout")}; };
        app->menu->layoutUpdated();
        QTRY_VERIFY(app->menu->calls.last().startsWith("layout"));
        QTest::qWait(50);
        QCOMPARE(labels(0), (QStringList{"A", "-", "B"}));
        app->menu->layoutReply = {};
        // Another menu path (NewMenu): its menu replaces the old; none takes it away.
        app->menu->entries = Menu::sample();
        app->item->change("Menu", QVariant::fromValue(QDBusObjectPath("/Other")), "NewMenu");
        QTRY_VERIFY(model.menu(app->key(), 0).isEmpty());
        app->bus.unregisterObject("/MenuBar");
        app->bus.registerVirtualObject("/Other", app->menu.get());
        app->item->change("Menu", QVariant::fromValue(QDBusObjectPath("/MenuBar")), "NewMenu");
        QTRY_VERIFY(model.find(app->key())->menuPath == "/MenuBar");
        app->item->change("Menu", QVariant::fromValue(QDBusObjectPath("/Other")), "NewMenu");
        QTRY_COMPARE(labels(0).value(0), QString("Open probe"));
        app->bus.unregisterObject("/Other");
        app->bus.registerVirtualObject("/MenuBar", app->menu.get());
        // The menu goes with its item.
        changed.clear();
        quit(app);
        QTRY_VERIFY(changed.size() >= 1);
        QVERIFY(model.menu(changed.last()[0].toString(), 0).isEmpty());
    }
    // Random, often wrong, applications: mistyped properties, broken pixmaps, malformed layouts
    // and entry updates, signals with odd arguments, bogus registrations, and applications coming
    // and going while the panel asks things of them. The host must neither crash nor stop
    // answering, and keeps to the items that exist.
    void fuzz() {
        // A warning from Qt (a message the host could not send, a value it misread) fails too.
        QTest::failOnWarning(QRegularExpression("."));
        QRandomGenerator random(20261005);
        TrayModel model;
        TrayHost host(model);
        QVERIFY(host.start(connect()));
        auto text = [&](int most) {
            static const QString alphabet = "ab_ _<>&;/\nå世{}#x";
            QString result;
            for (int i = random.bounded(most + 1); i > 0; --i)
                result += alphabet[random.bounded(int(alphabet.size()))];
            return result;
        };
        auto brokenPixmaps = [&] {
            QList<Pixmap> list;
            for (int n = random.bounded(4); n > 0; --n) {
                const int width = random.bounded(-2, 40), height = random.bounded(-2, 40);
                list << Pixmap{width, height, QByteArray(random.bounded(0, 40 * 40 * 4 + 10), char(random.bounded(256)))};
            }
            if (random.bounded(8) == 0)
                list << Pixmap{100000, 100000, QByteArray(16, 'x')};
            return QVariant::fromValue(list);
        };
        auto value = [&]() -> QVariant {
            switch (random.bounded(12)) {
            case 0: return int(random.bounded(1000)) - 500;
            case 1: return text(30);
            case 2: return random.bounded(2) == 0;
            case 3: return QStringList{text(5), text(5)};
            case 4: return brokenPixmaps();
            case 5: return pixmaps(QColor::fromRgb(random.generate()), {int(random.bounded(1, 64))});
            case 6: return toolTip(text(20), text(60));
            case 7: return QVariant::fromValue(QDBusObjectPath(random.bounded(2) ? "/MenuBar" : "/Nowhere"));
            case 8: return QVariant::fromValue(ToolTip{text(5), {square(Qt::red, 3)}, text(5), text(5)});
            case 9: return QVariant::fromValue(Properties{1, {{"x", 1}}}); // a structure of another shape
            case 10: return QByteArray(random.bounded(50), 'q');
            default: return double(random.bounded(100)) / 3;
            }
        };
        static const char *const names[] = {"Id", "Title", "Status", "IconName", "IconPixmap", "IconThemePath",
                                            "AttentionIconName", "AttentionIconPixmap", "OverlayIconName",
                                            "OverlayIconPixmap", "ToolTip", "ItemIsMenu", "Menu", "Category"};
        static const char *const statuses[] = {"Active", "Passive", "NeedsAttention", "Bogus"};
        static const char *const itemSignals[] = {"NewTitle", "NewIcon", "NewAttentionIcon", "NewOverlayIcon",
                                                  "NewToolTip", "NewStatus", "NewIconThemePath", "NewMenu", "Unknown"};
        auto properties = [&] {
            QVariantMap map;
            for (int n = random.bounded(1, 10); n > 0; --n) {
                const QString name = names[random.bounded(int(std::size(names)))];
                map[name] = name == "Status" && random.bounded(2) ? QVariant(statuses[random.bounded(4)]) : value();
            }
            return map;
        };
        auto entryProperties = [&] {
            static const char *const keys[] = {"label", "enabled", "visible", "type", "toggle-type", "toggle-state",
                                               "icon-name", "icon-data", "children-display", "shortcut", "junk"};
            static const char *const words[] = {"separator", "checkmark", "radio", "submenu", "standard"};
            QVariantMap map;
            for (int n = random.bounded(5); n > 0; --n)
                map[keys[random.bounded(int(std::size(keys)))]] =
                    random.bounded(3) == 0 ? QVariant(words[random.bounded(int(std::size(words)))]) : value();
            return map;
        };
        // A tree of random entries, sometimes deeper or wider than the host reads, with children
        // that are no entries.
        std::function<Layout(int)> randomLayout = [&](int depth) {
            Layout layout{int(random.bounded(-5, 60)), entryProperties(), {}};
            const bool wide = depth == 0 && random.bounded(6) == 0;
            const int children = wide ? 1100 : depth >= 11 ? 0 : random.bounded(depth < 3 ? 4 : 2);
            for (int c = 0; c < children; ++c) {
                const int kind = random.bounded(8);
                if (kind == 0)
                    layout.children << QDBusVariant(text(5));
                else if (kind == 1)
                    layout.children << QDBusVariant(QVariant::fromValue(Pixmap{1, 1, "abcd"}));
                else
                    layout.children << QDBusVariant(QVariant::fromValue(wide ? Layout{c, {{"label", "x"}}, {}}
                                                                              : randomLayout(depth + 1)));
            }
            return layout;
        };
        auto layoutReply = [&]() -> QVariantList {
            switch (random.bounded(6)) {
            case 0: return {1u, QString("junk")};
            case 1: return {};
            case 2: return {1u};
            case 3: return {1u, QVariant::fromValue(Pixmap{1, 1, "x"})};
            default: return {uint(random.bounded(100)), QVariant::fromValue(randomLayout(0))};
            }
        };
        auto randomTree = [&] {
            std::map<int, Menu::Entry> tree;
            for (int n = random.bounded(12); n > 0; --n) {
                QList<int> children;
                for (int c = random.bounded(3); c > 0; --c)
                    children << random.bounded(12);
                tree[random.bounded(12)] = {entryProperties(), children};
            }
            return tree;
        };
        auto junk = connect();
        std::vector<std::unique_ptr<App>> apps;
        auto anyApp = [&]() -> App * { return apps.empty() ? nullptr : apps[random.bounded(int(apps.size()))].get(); };
        int mostItems = 0, mostEntries = 0; // that the run did show things
        for (int i = 0; i < 1500; ++i) {
            App *app = anyApp();
            switch (random.bounded(9)) {
            case 0:
                if (apps.size() < 6) {
                    auto fresh = start(text(8), {.byPath = random.bounded(2) == 0, .getAll = random.bounded(3) != 0,
                                                 .activate = random.bounded(2) == 0, .registers = false});
                    fresh->item->properties = properties();
                    // Most have a menu, so that layouts are read.
                    if (random.bounded(4) != 0)
                        fresh->item->properties["Menu"] = QVariant::fromValue(QDBusObjectPath("/MenuBar"));
                    if (random.bounded(3) == 0)
                        fresh->menu->layoutReply = layoutReply;
                    callWatcher(fresh->bus, "RegisterStatusNotifierItem",
                                {fresh->item->path().startsWith("/org") ? fresh->item->path() : fresh->service});
                    apps.push_back(std::move(fresh));
                }
                break;
            case 1:
                if (app) {
                    auto it = std::find_if(apps.begin(), apps.end(), [app](const auto &a) { return a.get() == app; });
                    quit(*it);
                    apps.erase(it);
                }
                break;
            case 2:
                if (app) {
                    app->item->properties[names[random.bounded(int(std::size(names)))]] = value();
                    const int extra = random.bounded(3);
                    app->item->send(itemSignals[random.bounded(int(std::size(itemSignals)))],
                                    extra == 0 ? QVariantList{} : extra == 1 ? QVariantList{text(10)} : QVariantList{42});
                }
                break;
            case 3:
                if (app) {
                    app->menu->entries = randomTree();
                    app->menu->layoutReply = random.bounded(2) ? std::function<QVariantList()>(layoutReply) : nullptr;
                    app->menu->layoutUpdated(random.bounded(-2, 20));
                }
                break;
            case 4:
                if (app) {
                    QList<Properties> updated;
                    QList<Removed> removed;
                    for (int n = random.bounded(4); n > 0; --n)
                        updated << Properties{int(random.bounded(-3, 15)), entryProperties()};
                    for (int n = random.bounded(3); n > 0; --n)
                        removed << Removed{int(random.bounded(-3, 15)), {text(6), "label", "enabled", "type"}};
                    app->menu->propertiesUpdated(updated, removed);
                }
                break;
            case 5:
                if (app) {
                    // The menu's signals with arguments of the wrong types.
                    auto message = QDBusMessage::createSignal(app->menu->path(), menuInterface,
                                                              random.bounded(2) ? "ItemsPropertiesUpdated" : "LayoutUpdated");
                    message.setArguments(random.bounded(2) ? QVariantList{text(5), 5} : QVariantList{});
                    app->bus.send(message);
                }
                break;
            case 6: {
                // What the panel asks, of items that exist and some that do not.
                const QString key = app && random.bounded(4) ? app->key() : text(10);
                const int id = random.bounded(-3, 15);
                switch (random.bounded(8)) {
                case 0: model.activate(key, random.bounded(-5, 5000), random.bounded(-5, 5000)); break;
                case 1: model.secondaryActivate(key, 1, 2); break;
                case 2: model.contextMenu(key, 1, 2); break;
                case 3: model.scroll(key, random.bounded(-500, 500), random.bounded(2) == 0); break;
                case 4: model.openMenu(key, id); break;
                case 5: model.clickMenu(key, id); break;
                case 6: model.closeMenu(key, id); break;
                default:
                    for (const auto &entry : model.menu(key, id))
                        QVERIFY(entry.toMap().contains("label"));
                }
                for (const auto &item : model.items()) {
                    const int size = random.bounded(1, 64);
                    model.picture(item.serial, {size, size});
                    model.menuPicture(item.serial, id, {16, 16});
                    QVERIFY(item.toolTip().size() <= 1201);
                    QVERIFY(item.icon.size() <= 16);
                }
                break;
            }
            case 7:
                callWatcher(junk, "RegisterStatusNotifierItem",
                            {random.bounded(3) == 0 ? "/" + text(8) : random.bounded(2) ? text(20) : ":1.9999" + text(4)});
                if (random.bounded(4) == 0)
                    callWatcher(junk, "RegisterStatusNotifierHost", {text(12)});
                break;
            default:
                // A burst of signals at once.
                if (app)
                    for (int n = 0; n < 20; ++n)
                        app->item->send(itemSignals[random.bounded(int(std::size(itemSignals)))]);
            }
            if (i % 10 == 0)
                QTest::qWait(2);
            mostItems = std::max(mostItems, model.count());
            for (const auto &item : model.items())
                mostEntries = std::max(mostEntries, int(item.menu.size()));
        }
        QVERIFY2(mostItems >= 3 && mostEntries >= 5, qPrintable(QString("%1 %2").arg(mostItems).arg(mostEntries)));
        QTest::qWait(100);
        // Still answering, still the host, and showing nothing that is gone.
        QCOMPARE(watcherProperty("IsStatusNotifierHostRegistered").toBool(), true);
        QCOMPARE(watcherProperty("ProtocolVersion").toInt(), 0);
        for (const auto &item : model.items())
            QVERIFY2(std::any_of(apps.begin(), apps.end(), [&](const auto &app) { return app->key() == item.key; }),
                     qPrintable(item.key));
        while (!apps.empty()) {
            quit(apps.back());
            apps.pop_back();
        }
        disconnect(junk);
        QTRY_COMPARE(model.count(), 0);
        QTRY_VERIFY(registered().isEmpty());
    }
    // Another program serves the watcher: the host registers with it and follows its items, and
    // serves the name itself once that program goes.
    void hostOfAnotherWatcher() {
        auto other = std::make_unique<TrayWatcher>();
        QVERIFY(other->start(connect()));
        auto early = start("Early");
        TrayModel model;
        TrayHost host(model);
        QVERIFY(host.start(connect()));
        QVERIFY(!host.ownsWatcher());
        QTRY_VERIFY(other->hostRegistered());
        QTRY_COMPARE(model.count(), 1);
        auto late = start("Late", {.byPath = true});
        QTRY_COMPARE(model.count(), 2);
        quit(late);
        QTRY_COMPARE(model.count(), 1);
        other.reset();
        QTRY_VERIFY(host.ownsWatcher());
        QCOMPARE(model.count(), 1);
        // The item registers again with the new watcher, as items do; it is not shown twice.
        callWatcher(early->bus, "RegisterStatusNotifierItem", {early->service});
        QCOMPARE(registered(), QStringList{early->key()});
        QCOMPARE(model.count(), 1);
        quit(early);
        QTRY_COMPARE(model.count(), 0);
    }
};
QTEST_MAIN(TrayDbusTest)
#include "tray_dbus_test.moc"
