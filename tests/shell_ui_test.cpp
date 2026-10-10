// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio.hpp"
#include "controller.hpp"
#include "system_status.hpp"
#include "picker_view.hpp"
#include "view.hpp"
#include <QAbstractItemModel>
#include <QClipboard>
#include <QDeadlineTimer>
#include <QDir>
#include <QDragEnterEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QJSValue>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMimeData>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QWheelEvent>
#include <functional>
#include <iostream>
#include <memory>

namespace {
// Records what the panel asks of the sound server.
class FakeAudio : public Audio {
  public:
    QStringList requests;

  protected:
    void sendVolume(const QString &output, int percent) override {
        requests << QString("volume %1 %2").arg(output).arg(percent);
    }
    void sendMute(const QString &output, bool muted) override {
        requests << QString("mute %1 %2").arg(output).arg(muted);
    }
    void sendOutput(const QString &output, const std::vector<uint32_t> &streams) override {
        requests << QString("output %1 %2").arg(output).arg(streams.size());
    }
    void sendStreamVolume(uint32_t id, int percent) override {
        requests << QString("stream %1 %2").arg(id).arg(percent);
    }
    void sendStreamMute(uint32_t id, bool muted) override {
        requests << QString("stream-mute %1 %2").arg(id).arg(muted);
    }
};

// Records what the panel asks of the media players.
class FakeMedia : public Media {
  public:
    QStringList requests;

  protected:
    void sendCommand(const QString &name, const QString &method) override {
        requests << method + " " + name;
    }
    void sendPosition(const QString &name, const QString &trackId, qint64 position) override {
        requests << QString("SetPosition %1 %2 %3").arg(name, trackId).arg(position);
    }
    void queryPosition(const QString &) override {}
};

// Records the profiles the panel asks power-profiles-daemon for.
class FakePowerMode : public PowerMode {
  public:
    QStringList requests;

  protected:
    void sendProfile(const QString &profile) override { requests << profile; }
};

// Records what the panel asks of NetworkManager.
class FakeWifi : public Wifi {
  public:
    QStringList requests;

  protected:
    void sendEnabled(bool enabled) override { requests << QString("enabled %1").arg(enabled); }
    void sendScan() override { requests << "scan"; }
    void sendConnect(const QString &ssid, const QString &security, const QString &password) override {
        requests << QString("connect %1 %2 %3").arg(ssid, security, password).trimmed();
    }
    void sendDisconnect() override { requests << "disconnect"; }
};

// Records what the panel asks of BlueZ.
class FakeBluetooth : public Bluetooth {
  public:
    QStringList requests;

  protected:
    void sendPowered(bool powered) override { requests << QString("powered %1").arg(powered); }
    void sendDiscovery(bool discovering) override { requests << QString("discovery %1").arg(discovering); }
    void sendConnect(const QString &path) override { requests << "connect " + path.section('/', -1); }
    void sendDisconnect(const QString &path) override { requests << "disconnect " + path.section('/', -1); }
    void sendPair(const QString &path) override { requests << "pair " + path.section('/', -1); }
    void sendForget(const QString &path) override { requests << "forget " + path.section('/', -1); }
    void sendAnswer(bool accepted, const QString &input) override {
        requests << QString("answer %1 %2").arg(accepted).arg(input).trimmed();
    }
};

// Pictures for the stand-in windows, image://test-windows/WIDTHxHEIGHT: that large, in one colour.
class TestPictures : public QQuickImageProvider {
  public:
    TestPictures() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &) override {
        QImage image(id.section('x', 0, 0).toInt(), id.section('x', 1, 1).toInt(), QImage::Format_RGB32);
        image.fill(Qt::darkCyan);
        if (size)
            *size = image.size();
        return image;
    }
};
} // namespace
#ifdef __SANITIZE_ADDRESS__
// The QML objects a test script creates are still reachable to Qt at exit, not to LeakSanitizer.
extern "C" const char *__asan_default_options() { return "detect_leaks=0"; }
#endif

static int fail(const char *why) {
    std::cerr << why << '\n';
    return 1;
}

// Whether `done` comes true within `timeout` milliseconds. QTest::qWaitFor sleeps 10 ms between
// looks, so each round trip through the stand-in compositor cost a sleep; this looks again as
// soon as an event has been handled, a 2 ms tick keeping it looking while none come.
template <typename Predicate> [[nodiscard]] static bool waitFor(Predicate &&done, int timeout = 5000) {
    if (done())
        return true;
    const QDeadlineTimer deadline(timeout, Qt::PreciseTimer);
    QTimer tick;
    tick.setTimerType(Qt::PreciseTimer);
    tick.start(2);
    do {
        QCoreApplication::processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMoreEvents);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        if (done())
            return true;
    } while (!deadline.hasExpired());
    return done();
}

int main(int argc, char **argv) {
    // A named screen, as compositor outputs are: the workspace indicator is keyed by it.
    QTemporaryDir screens;
    QFile layout(screens.filePath("screens.json"));
    if (!screens.isValid() || !layout.open(QIODevice::WriteOnly) ||
        layout.write(R"({"screens": [{"name": "TEST-1", "x": 0, "y": 0, "width": 1280,
                         "height": 720, "logicalDpi": 96, "logicalBaseDpi": 96, "dpr": 1}]})") < 0)
        return fail("could not write the screens layout");
    layout.close();
    qputenv("QT_QPA_PLATFORM", ("offscreen:configfile=" + layout.fileName()).toLocal8Bit());
    // One installed application, found by its StartupWMClass, and a private pin store. GLib
    // caches these directories on first use, so they are set before anything starts.
    QDir(screens.path()).mkpath("data/applications");
    QFile desktopFile(screens.filePath("data/applications/paw-test-app.desktop"));
    if (!desktopFile.open(QIODevice::WriteOnly) ||
        desktopFile.write("[Desktop Entry]\nType=Application\nName=Fake app\nExec=true\n"
                          "StartupWMClass=Fake\n") < 0)
        return fail("could not write the fake application");
    desktopFile.close();
    QFile otherFile(screens.filePath("data/applications/paw-test-other.desktop"));
    if (!otherFile.open(QIODevice::WriteOnly) ||
        otherFile.write("[Desktop Entry]\nType=Application\nName=Other app\nExec=true\n") < 0)
        return fail("could not write the other application");
    otherFile.close();
    // One the menus leave out, whose icon its windows still get.
    QFile hiddenFile(screens.filePath("data/applications/paw-test-hidden.desktop"));
    if (!hiddenFile.open(QIODevice::WriteOnly) ||
        hiddenFile.write("[Desktop Entry]\nType=Application\nName=Hidden app\nExec=true\n"
                         "NoDisplay=true\nIcon=paw-hidden-icon\n"
                         "StartupWMClass=HiddenThing\n") < 0)
        return fail("could not write the hidden application");
    hiddenFile.close();
    qputenv("XDG_DATA_HOME", screens.filePath("data").toLocal8Bit());
    qputenv("XDG_DATA_DIRS", screens.filePath("none").toLocal8Bit());
    qputenv("XDG_STATE_HOME", screens.filePath("state").toLocal8Bit());
    qputenv("XDG_CACHE_HOME", screens.filePath("cache").toLocal8Bit());
    const auto pins = screens.filePath("state/paw/pinned");
    QGuiApplication app(argc, argv);
    if (argc != 2)
        return fail("usage: shell_ui_test CMAKE (run by the launcher as CMAKE -E touch FILE)");
    QTemporaryDir directory;
    if (!directory.isValid())
        return fail("no temporary directory");
    auto config = directory.filePath("init.lua");
    auto marker = directory.filePath("launched");
    // An application with desktop actions: one that runs, and one whose program is missing. It
    // is written before the controller first reads the applications.
    const auto actionMarker = directory.filePath("action");
    QFile actionsFile(screens.filePath("data/applications/paw-test-actions.desktop"));
    if (!actionsFile.open(QIODevice::WriteOnly) ||
        actionsFile.write(QString("[Desktop Entry]\nType=Application\nName=Action app\nExec=true\n"
                                  "GenericName=File toucher\nKeywords=stamp;mark;\n"
                                  "Comment=Leaves a file behind\n"
                                  "Actions=touch;missing;\n\n"
                                  "[Desktop Action touch]\nName=Touch a file\nIcon=document-new\n"
                                  "Exec=\"%1\" -E touch \"%2\"\n\n"
                                  "[Desktop Action missing]\nName=Missing program\n"
                                  "Exec=/nonexistent/paw-missing-program\n")
                              .arg(QString::fromLocal8Bit(argv[1]), actionMarker)
                              .toUtf8()) < 0)
        return fail("could not write the application with actions");
    actionsFile.close();
    // Two pictures for the wallpaper picker, in two subfolders.
    const auto walls = directory.filePath("walls");
    for (const auto *name : {"a/one.png", "b/two.png"}) {
        QDir(walls).mkpath(QFileInfo(name).path());
        QImage picture(64, 36, QImage::Format_RGB32);
        picture.fill(Qt::darkCyan);
        if (!picture.save(walls + "/" + name))
            return fail("could not save a wallpaper");
    }
    // Files for the search to find, in a folder of the test's own (shell.search.directories), so
    // that nothing of the user's is read.
    const auto files = directory.filePath("files");
    QDir(files).mkpath("Archive");
    QFile report(files + "/Quarterly report.txt");
    if (!report.open(QIODevice::WriteOnly) || report.write("quarterly\n") < 0)
        return fail("could not write a file to search for");
    report.close();
    QFile file(config);
    if (!file.open(QIODevice::WriteOnly))
        return fail("could not write the configuration");
    // Long Lua strings preserve paths without shell interpolation. The widgets Quick Settings
    // holds by default are on the bar, where most of this test uses them.
    const QString barWidgets = "widgets={network='bar',battery='bar',volume='bar',tiling='bar',profiles='bar'},";
    const auto lua = QString("return {layout={workspace_names={'web','','','mail'}},"
                             "power={countdown=2},"
                             "profile='dark',profiles={dark={},light={shell={accent='#336699'}}},"
                             "shell={wallpaper='walls/a/one.png'," + barWidgets + "wallpapers=[[%3]],"
                             "search={directories={[[%4]]}},"
                             "launchers={{name='Test app',command={[[%1]],'-E','touch',[[%2]]}}}}}")
                         .arg(QString::fromLocal8Bit(argv[1]), marker, walls, files);
    file.write(lua.toUtf8());
    file.close();
    // A stand-in for the compositor's control socket, with this screen as its only output.
    // Tiling is per output; the focused one it reports first is always the opposite of this
    // screen's, as if another monitor had focus, so the panel must show its own.
    const auto output = app.primaryScreen()->name();
    QString urgentLines; // what a state says about windows asking for attention
    auto state = [&output, &urgentLines](bool tiling, int workspace) {
        return (QString("tiling %1\nworkspace %2\noutput %3 %2 1,2 %4\n")
                    .arg(tiling ? "off" : "on")
                    .arg(workspace)
                    .arg(output)
                    .arg(tiling ? "on" : "off") +
                urgentLines)
            .toUtf8();
    };
    QLocalServer compositor;
    QLocalSocket *subscriber = nullptr;
    bool toggled = false;
    int currentWorkspace = 2;
    QStringList switches, requests;
    int layoutSwitches = 0; // "switch_layout next" requests, answered with the second layout
    bool nightLight = false; // what "night_light_toggle" turns, and the state it then announces
    bool holdSessions = false;
    QString powerRefusal; // what the compositor answers a power action with; "" for ok
    QLocalSocket *pendingSessions = nullptr;
    // What `get monitors` answers, and what `monitors apply` and `monitors reset` are refused
    // with ("" for a trial).
    QByteArray monitorsReply;
    QString monitorsRefusal;
    // What the compositor does with "profile NAME": saves it and reloads the shell.
    std::function<void(const QString &)> pickProfile;
    QObject::connect(&compositor, &QLocalServer::newConnection, [&] {
        auto *client = compositor.nextPendingConnection();
        QObject::connect(client, &QLocalSocket::readyRead, [&, client] {
            if (!client->canReadLine())
                return;
            auto request = client->readLine();
            if (request == "subscribe shell\n") {
                subscriber = client;
                client->write("ok\n" + state(false, 2));
            } else if (request == "output " + output.toUtf8() + " toggle_tiling\n") {
                toggled = !toggled;
                client->write("ok\n");
                client->disconnectFromServer();
                subscriber->write(state(toggled, currentWorkspace));
            } else if (request.startsWith("profile ")) {
                requests.push_back(QString::fromUtf8(request).trimmed());
                client->write("ok\n");
                client->disconnectFromServer();
                pickProfile(QString::fromUtf8(request).trimmed().section(' ', 1));
            } else if (request == "night_light_toggle\n") {
                requests.push_back("night_light_toggle");
                nightLight = !nightLight;
                client->write("ok\n");
                client->disconnectFromServer();
                subscriber->write(nightLight ? "night-light on on\n" : "night-light off off\n");
            } else if (request.startsWith("mode ")) {
                requests.push_back(QString::fromUtf8(request).trimmed());
                client->write("ok\n");
                client->disconnectFromServer();
                subscriber->write(request);
            } else if (request == "switch_layout next\n") {
                ++layoutSwitches;
                client->write("ok\n");
                client->disconnectFromServer();
                subscriber->write("keyboard-layout 2 2 no Norwegian\n");
            } else if (QStringList{"lock", "suspend", "hibernate", "reboot", "poweroff", "logout"}
                           .contains(QString::fromUtf8(request).trimmed())) {
                requests.push_back(QString::fromUtf8(request).trimmed());
                client->write(powerRefusal.isEmpty() ? QByteArray("ok\n")
                                                     : ("error: " + powerRefusal + "\n").toUtf8());
                client->disconnectFromServer();
            } else if (request == "get monitors\n") {
                client->write("ok\n" + monitorsReply);
                client->disconnectFromServer();
            } else if (request.startsWith("monitors ")) {
                requests.push_back(QString::fromUtf8(request).trimmed());
                const bool trial = request.startsWith("monitors apply ") || request == "monitors reset\n";
                if (trial && !monitorsRefusal.isEmpty()) {
                    client->write(("error: " + monitorsRefusal + "\n").toUtf8());
                } else if (trial) {
                    client->write("ok\n15000\n");
                    subscriber->write("monitors-trial 15000\n");
                } else {
                    client->write("ok\n");
                    subscriber->write(request == "monitors keep\n" ? "monitors-kept\n" : "monitors-reverted asked\n");
                }
                client->disconnectFromServer();
            } else if (request == "session list\n") {
                if (holdSessions) { // answered later, by the test
                    pendingSessions = client;
                    return;
                }
                client->write("ok\nwork\t3\t1700000000\n");
                client->disconnectFromServer();
            } else if (request.startsWith("type ")) {
                // Typing what the emoji picker picked; 🙃 has nothing to type into.
                requests.push_back(QString::fromUtf8(request).trimmed());
                client->write(request.contains("🙃") ? "error: nothing has the keyboard\n" : "ok\n");
                client->disconnectFromServer();
            } else if (request.startsWith("output ") || request.startsWith("session ") ||
                       request.startsWith("switcher_confirm ") ||
                       request == "toggle_tiling\n" || request == "layout_monocle\n" ||
                       request == "terminal\n" || request == "snap_left\n" || request == "snap_right\n") {
                if (!request.startsWith("output ")) {
                    requests.push_back(QString::fromUtf8(request).trimmed());
                    client->write("ok\n");
                    client->disconnectFromServer();
                    return;
                }
                switches.push_back(QString::fromUtf8(request).trimmed());
                client->write("ok\n");
                client->disconnectFromServer();
                currentWorkspace = request.trimmed().split(' ').last().toInt();
                subscriber->write(state(toggled, currentWorkspace));
            }
        });
    });
    if (!compositor.listen(directory.filePath("control.sock")))
        return fail("could not listen on the fake control socket");
    qputenv("PAW_SOCKET", compositor.fullServerName().toLocal8Bit());
    ShellController controller(config.toStdString());
    pickProfile = [&controller](const QString &name) {
        paw::save_profile(name.toStdString());
        controller.reload();
    };
    // A stand-in sound server for the volume control, made before the panel so that it outlives
    // it: what reads it never finds it gone.
    FakeAudio audio;
    ShellView view(controller, app.primaryScreen(), false, true);
    if (view.status() != QQuickView::Ready) {
        for (const auto &error : view.errors())
            std::cerr << error.toString().toStdString() << '\n';
        return 1;
    }
    // An application picks its own app ID: a path in it is no icon to load from disk.
    if (controller.iconFor("/etc/hostname") != "application-x-executable" ||
        controller.iconFor("../../x") != "application-x-executable") {
        std::cerr << "a window's app ID can name a file for the icon\n";
        return 1;
    }
    // A window no installed application matches gets an icon the theme has by a name guessed
    // from its app ID, else the generic one.
    {
        const auto theme = screens.filePath("data/icons/hicolor");
        QDir().mkpath(theme + "/16x16/apps");
        QFile index(theme + "/index.theme");
        QImage picture(16, 16, QImage::Format_ARGB32);
        picture.fill(Qt::red);
        if (!index.open(QIODevice::WriteOnly) ||
            index.write("[Icon Theme]\nName=Hicolor\nDirectories=16x16/apps\n\n"
                        "[16x16/apps]\nSize=16\nType=Fixed\n") < 0 ||
            !picture.save(theme + "/16x16/apps/paw-guessed.png"))
            return fail("could not write the icon theme");
        index.close();
        QIcon::setThemeSearchPaths({screens.filePath("data/icons")});
        QIcon::setThemeName("hicolor");
        if (controller.iconFor("Paw-Guessed-1.2") != "paw-guessed" ||
            controller.iconFor("org.example.App") != "application-x-executable" ||
            controller.iconFor("Fake") != "application-x-executable") {
            std::cerr << "a window's icon is not guessed from its app ID\n";
            return 1;
        }
        if (controller.iconFor("HiddenThing") != "paw-hidden-icon" ||
            !controller.appFor("HiddenThing").isEmpty()) {
            std::cerr << "a window of an application the menus leave out has not its icon\n";
            return 1;
        }
    }
    // An application's desktop actions are listed in its entry's order with their icons, and run
    // as the application is started; a failure, or an action that is not there, shows across the
    // panel.
    {
        const auto actions = controller.appActions("paw-test-actions.desktop");
        if (actions.size() != 2 || actions[0].toMap()["action"] != "touch" ||
            actions[0].toMap()["name"] != "Touch a file" ||
            actions[0].toMap()["icon"] != "document-new" ||
            actions[1].toMap()["action"] != "missing" || actions[1].toMap()["icon"] != "" ||
            !controller.appActions("paw-test-app.desktop").isEmpty() ||
            !controller.appActions("pinned:0").isEmpty() ||
            !controller.appActions("not-installed.desktop").isEmpty()) {
            std::cerr << "the desktop actions are not listed as the entries give them\n";
            return 1;
        }
        if (!controller.launchAction("paw-test-actions.desktop", "touch") ||
            !waitFor([&] { return QFile::exists(actionMarker); }) ||
            !controller.error().isEmpty()) {
            std::cerr << "a desktop action did not run: " << controller.error().toStdString() << '\n';
            return 1;
        }
        // The launch is remembered in the state directory, for the start menu's recent list.
        QFile launches(screens.filePath("state/paw/launches"));
        const auto recent = controller.startMenu()->recent();
        if (!launches.open(QIODevice::ReadOnly) ||
            !launches.readAll().startsWith("paw-test-actions.desktop\t1\t") || recent.size() != 1 ||
            recent[0].toMap()["appId"] != "paw-test-actions.desktop")
            return fail("running a desktop action was not recorded as a launch");
        if (controller.launchAction("paw-test-actions.desktop", "missing") ||
            !controller.error().startsWith("Could not launch Action app: ")) {
            std::cerr << "a desktop action whose program is missing was not reported: "
                      << controller.error().toStdString() << '\n';
            return 1;
        }
        if (controller.launchAction("paw-test-actions.desktop", "absent") ||
            controller.error() != "This action is no longer available.") {
            std::cerr << "an action the entry does not have was not refused\n";
            return 1;
        }
        controller.clearError();
    }
    // An installed application's record says what else a search finds it by.
    {
        QVariantMap record;
        for (const auto &app : controller.apps())
            if (app.toMap()["appId"] == "paw-test-actions.desktop")
                record = app.toMap();
        if (record["genericName"] != "File toucher" ||
            record["keywords"].toStringList() != QStringList{"stamp", "mark"} ||
            record["description"] != "Leaves a file behind")
            return fail("an application's generic name, keywords and comment are not in its record");
    }
    // Applications installed or removed while the shell runs are found without being asked.
    {
        auto installed = [&](const QString &id) {
            const auto apps = controller.apps();
            return std::any_of(apps.begin(), apps.end(),
                               [&](const QVariant &app) { return app.toMap()["appId"] == id; });
        };
        QFile later(screens.filePath("data/applications/paw-test-later.desktop"));
        if (!later.open(QIODevice::WriteOnly) ||
            later.write("[Desktop Entry]\nType=Application\nName=Later app\nExec=true\n") < 0)
            return fail("could not write an application to install");
        later.close();
        if (!waitFor([&] { return installed("paw-test-later.desktop"); }, 10000))
            return fail("an application installed while the shell ran was not found");
        later.remove();
        if (!waitFor([&] { return !installed("paw-test-later.desktop"); }, 10000))
            return fail("an application removed while the shell ran stayed listed");
    }
    view.show();
    if (!QTest::qWaitForWindowExposed(&view))
        return fail("the panel never showed");
    // The popups' window, as large as the output, with the bar's along its bottom edge.
    PopoverWindow *popover = view.popover();
    if (!popover || popover->size() != ShellView::previewSize() || popover->isVisible())
        return fail("the panel has no popover the size of its output, or it shows at start");
    // Loader items, Repeater items and the like are found through the item tree rather than as
    // QObject children.
    std::function<QQuickItem *(QQuickItem *, const QString &)> find =
        [&](QQuickItem *item, const QString &name) -> QQuickItem * {
        if (item->objectName() == name)
            return item;
        for (auto *child : item->childItems())
            if (auto *found = find(child, name))
                return found;
        // The panel's popups are in the popover's window.
        return item == view.rootObject() ? find(popover->contentItem(), name) : nullptr;
    };
    // Whether a popup is open, done sliding in, and lies inside the popover, off the bar along its
    // bottom edge, the bar's own surface keeping its size (and, the macOS style's dock, its room
    // above for an icon to bounce in). A menu's first card stands for it.
    auto inPopover = [&](QQuickItem *popup) {
        if (auto *card = popup->property("card").value<QQuickItem *>())
            popup = card;
        const QRectF area = popup->mapRectToScene(QRectF(0, 0, popup->width(), popup->height()));
        const QVariant progress = popup->property("progress");
        return popup->isVisible() && (!progress.isValid() || progress.toReal() == 1) &&
               popup->window() == popover && popover->isVisible() &&
               area.top() >= 0 && area.left() >= 0 &&
               area.bottom() <= popover->height() - controller.panelExtent() && area.right() <= popover->width() &&
               view.height() == controller.panelExtent() + controller.panelHeadroom();
    };
    // The popups are made on first use, not with the panel.
    for (const char *popup : {"audioMixer", "calendar", "audioOutputs", "contextMenu", "groupList",
                              "applicationSearch"})
        if (find(view.rootObject(), popup)) {
            std::cerr << popup << " was made before it was needed\n";
            return 1;
        }
    const QPoint start(30, controller.panelHeight() / 2);
    QTest::mouseMove(&view, start);
    QTest::qWait(200); // Hover first: a tooltip must not swallow the following press.
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, start);
    if (!waitFor([&] { return view.rootObject()->property("launcherOpen").toBool(); })) {
        std::cerr << "hover then click did not open the launcher\n";
        return 1;
    }
    auto *search = find(view.rootObject(), "applicationSearch");
    if (!search || !waitFor([&] { return search->hasActiveFocus(); })) {
        std::cerr << "launcher search did not receive keyboard focus\n";
        return 1;
    }
    for (Qt::Key key : {Qt::Key_T, Qt::Key_E, Qt::Key_S, Qt::Key_T})
        QTest::keyClick(search->window(), key);
    QTest::keyClick(search->window(), Qt::Key_Return);
    if (!waitFor([&] { return QFile::exists(marker); })) {
        std::cerr << "search and Enter did not launch the configured command\n";
        return 1;
    }
    if (view.rootObject()->property("launcherOpen").toBool()) {
        std::cerr << "launcher remained open after launching\n";
        return 1;
    }
    // A moment after startup the popups are made ahead of their first use.
    view.rootObject()->setProperty("warm", true);
    if (!waitFor([&] {
            for (const char *popup : {"audioMixer", "calendar", "audioOutputs", "contextMenu", "groupList"})
                if (!find(view.rootObject(), popup))
                    return false;
            return true;
        })) {
        std::cerr << "the popups were not made ahead of use\n";
        return 1;
    }
    auto panelTiling = [&view] { return view.rootObject()->property("tiling").toBool(); };
    auto *tiling = view.rootObject()->findChild<QQuickItem *>("tilingToggle");
    if (!tiling || !waitFor([&] { return controller.tilingAvailable(); }) ||
        panelTiling()) {
        std::cerr << "tiling state did not arrive from the control socket\n";
        return 1;
    }
    const QPoint toggle =
        tiling->mapToScene(QPointF(tiling->width() / 2, tiling->height() / 2)).toPoint();
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, toggle);
    if (!waitFor([&] { return toggled && panelTiling(); })) {
        std::cerr << "the tiling button did not toggle tiling\n";
        return 1;
    }
    // The workspace indicator shows this output's state and switches it.
    auto workspace = [&](int number) {
        return find(view.rootObject(), QString("workspace%1").arg(number));
    };
    if (!workspace(4) || workspace(5) || !workspace(2)->property("current").toBool() ||
        workspace(1)->property("current").toBool() ||
        !workspace(1)->property("occupied").toBool() ||
        workspace(3)->property("occupied").toBool()) {
        std::cerr << "the workspace indicator does not show the output's workspaces\n";
        return 1;
    }
    if (workspace(1)->property("label").toString() != "web" ||
        workspace(4)->property("label").toString() != "mail" ||
        !workspace(2)->property("label").toString().isEmpty() ||
        workspace(1)->width() <= workspace(2)->width()) {
        std::cerr << "the workspace indicator does not show workspace names\n";
        return 1;
    }
    auto centre = [&](QQuickItem *item) {
        return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    };
    // A click in the middle of an item, in the window it is in.
    auto click = [&](QQuickItem *item, Qt::MouseButton button = Qt::LeftButton, int delay = -1) {
        QTest::mouseClick(item->window(), button, Qt::NoModifier, centre(item), delay);
    };
    click(workspace(3));
    if (!waitFor([&] { return workspace(3)->property("current").toBool(); }) ||
        switches != QStringList{"output " + output + " workspace 3"}) {
        std::cerr << "clicking a workspace did not switch to it\n";
        return 1;
    }
    // The current workspace's pill slides over to it, and takes its width.
    {
        auto *pill = find(view.rootObject(), "workspacePill");
        if (!pill || !waitFor([&] {
                return pill->isVisible() && pill->x() == workspace(3)->x() &&
                       pill->width() == workspace(3)->width();
            }))
            return fail("the current workspace's pill did not settle under it");
    }
    auto scrollAt = [&](QPoint at, int delta) {
        QWheelEvent event(at, view.mapToGlobal(at), QPoint(), QPoint(0, delta), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&view, &event);
    };
    auto scroll = [&](int delta) { scrollAt(centre(workspace(2)), delta); };
    scroll(-120); // down: the next workspace
    if (!waitFor([&] { return workspace(4)->property("current").toBool(); })) {
        std::cerr << "scrolling down did not page to the next workspace\n";
        return 1;
    }
    scroll(-120); // already on the last one
    scroll(60);   // half a notch does nothing yet
    scroll(60);
    if (!waitFor([&] { return workspace(3)->property("current").toBool(); }) ||
        switches.size() != 3 || switches.last() != "output " + output + " workspace 3") {
        std::cerr << "scrolling up did not page back one workspace: "
                  << switches.join(", ").toStdString() << '\n';
        return 1;
    }
    // The bar's empty space, left of its first button, pages workspaces too.
    auto *bar = find(view.rootObject(), "bar");
    scrollAt(bar->mapToScene(QPointF(3, bar->height() / 2)).toPoint(), -120);
    if (!waitFor([&] { return workspace(4)->property("current").toBool(); }) ||
        switches.size() != 4 || switches.last() != "output " + output + " workspace 4") {
        std::cerr << "scrolling the bar's empty space did not page workspaces\n";
        return 1;
    }
    // A window asking for attention marks its workspace in the indicator (and is listed); when
    // it is done, the marks go.
    if (controller.urgentCount() != 0 || workspace(3)->property("urgent").toBool()) {
        std::cerr << "a workspace is urgent before anything asked\n";
        return 1;
    }
    urgentLines = "urgent 2\nurgent-output " + output + " 1,3\n"
                  "urgent-window " + output + "\t3\tfake\tFake\n"
                  "urgent-window " + output + "\t1\t\tNo app id\n";
    subscriber->write(state(toggled, currentWorkspace));
    if (!waitFor([&] { return controller.urgentCount() == 2; }) ||
        !workspace(3)->property("urgent").toBool() || !workspace(1)->property("urgent").toBool() ||
        workspace(2)->property("urgent").toBool() || workspace(4)->property("urgent").toBool()) {
        std::cerr << "the workspace indicator does not mark workspaces with urgent windows\n";
        return 1;
    }
    const auto asking = controller.urgentWindows();
    if (asking.size() != 2 || asking[0].toMap()["appId"].toString() != "fake" ||
        asking[0].toMap()["title"].toString() != "Fake" ||
        asking[0].toMap()["workspace"].toInt() != 3 || !asking[1].toMap()["appId"].toString().isEmpty() ||
        asking[1].toMap()["title"].toString() != "No app id") {
        std::cerr << "the urgent windows were not parsed in order\n";
        return 1;
    }
    if (!find(workspace(3), "workspaceUrgent3") || !find(workspace(3), "workspaceUrgent3")->isVisible() ||
        find(workspace(2), "workspaceUrgent2")->isVisible()) {
        std::cerr << "the urgent marker of a workspace is not shown where it should be\n";
        return 1;
    }
    urgentLines = "urgent 0\n";
    subscriber->write(state(toggled, currentWorkspace));
    if (!waitFor([&] { return controller.urgentCount() == 0; }) ||
        workspace(3)->property("urgent").toBool() || workspace(1)->property("urgent").toBool() ||
        !controller.urgentWindows().isEmpty()) {
        std::cerr << "the workspace marks did not go when the windows stopped asking\n";
        return 1;
    }
    auto rewrite = [&config](const QString &source) {
        QFile again(config);
        return again.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
               again.write(source.toUtf8()) >= 0;
    };
    // Slows the shell's animations down to a quarter of their speed, or brings them back, so that
    // a test sees what moves on its way.
    auto slowMotion = [&](bool slow) {
        if (!rewrite(slow ? QString(lua).replace("return {", "return {animations={speed=0.25},") : lua))
            return false;
        controller.reload();
        return waitFor([&] { return controller.animationSpeed() == (slow ? 0.25 : 1); });
    };
    // shell.workspaces_shown = 3 shows the current workspace with its neighbours, the last
    // three on the last one, and the names follow their workspaces.
    if (!rewrite(QString(lua).replace("shell={", "shell={workspaces_shown=3,")))
        return fail("could not rewrite the configuration");
    controller.reload();
    if (!waitFor([&] { return controller.workspacesShown() == 3 && !workspace(1); }) ||
        !workspace(2) || !workspace(3) || !workspace(4) || !workspace(4)->property("current").toBool() ||
        workspace(4)->property("label").toString() != "mail") {
        std::cerr << "shell.workspaces_shown = 3 did not show the last three workspaces\n";
        return 1;
    }
    // The bar's layout moves the narrower indicator on its next polish, which a grab runs.
    view.grabWindow();
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, centre(workspace(2)));
    if (!waitFor([&] { return workspace(1) && !workspace(4); }) ||
        !workspace(2)->property("current").toBool() || !workspace(3) ||
        workspace(1)->property("label").toString() != "web") {
        std::cerr << "shell.workspaces_shown = 3 did not show the workspaces around the current one\n";
        for (int n = 1; n <= 4; ++n)
        return 1;
    }
    if (!rewrite(lua))
        return fail("could not restore the configuration");
    controller.reload();
    if (!waitFor([&] { return workspace(4) != nullptr; }) || !workspace(1))
        return fail("the workspace indicator did not show every workspace again");
    view.grabWindow();
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, centre(workspace(4)));
    if (!waitFor([&] { return workspace(4)->property("current").toBool(); }))
        return fail("could not switch back to the last workspace");
    // The window switcher's list says which windows are asking for attention (an older
    // five-field line means none) and each window's number (an older line's is 0; a field past
    // it, from a newer compositor, is passed over). Its overlay shows them, and goes on showing
    // them as it fades out once the switcher has closed, though the controller has forgotten
    // them by then.
    SwitcherView switcherView(controller, app.primaryScreen());
    auto overlayListed = [&] { return switcherView.rootObject()->property("listed").toList().size(); };
    subscriber->write(("switcher " + output + " 0 3\n"
                       "switcher-window fake\tFake\t" + output + "\t3\t0\t1\t12\n"
                       "switcher-window \tNo app id\t" + output + "\t1\t1\t0\t13\tnew\n"
                       "switcher-window old\tOld\t" + output + "\t1\t0\n").toUtf8());
    if (!waitFor([&] { return controller.switcherWindows().size() == 3; })) {
        std::cerr << "the switcher's windows were not parsed\n";
        return 1;
    }
    const auto listed = controller.switcherWindows();
    if (!listed[0].toMap()["urgent"].toBool() || listed[1].toMap()["urgent"].toBool() ||
        listed[2].toMap()["urgent"].toBool() || !listed[1].toMap()["minimized"].toBool() ||
        !listed[1].toMap()["appId"].toString().isEmpty()) {
        std::cerr << "the switcher does not say which windows are urgent\n";
        return 1;
    }
    if (listed[0].toMap()["id"].toInt() != 12 || listed[1].toMap()["id"].toInt() != 13 ||
        listed[2].toMap()["id"].toInt() != 0)
        return fail("the switcher's windows' numbers were not parsed");
    if (!waitFor([&] { return switcherView.isVisible() && overlayListed() == 3; }))
        return fail("the switcher's overlay did not show its windows");
    subscriber->write("switcher-close\n");
    if (!waitFor([&] { return controller.switcherWindows().isEmpty(); })) {
        std::cerr << "the switcher did not close\n";
        return 1;
    }
    if (overlayListed() != 3 || !waitFor([&] { return !switcherView.isVisible(); }) ||
        overlayListed() != 3)
        return fail("the switcher's overlay did not hold its windows as it went");
    // The switcher, the overview and the command palette opening on this output close what is
    // open here: the popover would be over them.
    {
        auto openLauncher = [&] {
            view.rootObject()->setProperty("launcherOpen", true);
            return waitFor([&] { return popover->isVisible(); });
        };
        auto closed = [&] {
            return waitFor([&] { return !view.rootObject()->property("menuOpen").toBool(); });
        };
        if (!openLauncher())
            return fail("the launcher did not open");
        subscriber->write(("switcher " + output + " 0 0\n").toUtf8());
        if (!closed())
            return fail("the window switcher opening did not close the launcher");
        subscriber->write("switcher-close\n");
        if (!openLauncher())
            return fail("the launcher did not open again");
        subscriber->write(("overview " + output + " 0 0 1 0 0 0 1100 668 -\n").toUtf8());
        if (!closed())
            return fail("the overview opening did not close the launcher");
        subscriber->write("overview-close\n");
        if (!openLauncher())
            return fail("the launcher did not open a third time");
        controller.palette()->open(output);
        if (!closed())
            return fail("the command palette opening did not close the launcher");
        controller.palette()->close();
    }
    // The keyboard layout indicator: the active layout's short name while there are two or
    // more; clicking it asks for the next; shell.widgets.keyboard_layout = false hides it.
    {
        auto *layout = find(view.rootObject(), "keyboardLayout");
        auto *label = find(view.rootObject(), "keyboardLayoutText");
        if (!layout || !label || layout->isVisible()) {
            std::cerr << "the keyboard layout indicator shows before the compositor names one\n";
            return 1;
        }
        subscriber->write("keyboard-layout 1 1 us English (US)\n");
        if (!waitFor([&] { return controller.keyboardLayout()["count"].toInt() == 1; }) ||
            layout->isVisible()) {
            std::cerr << "the keyboard layout indicator shows with a single layout\n";
            return 1;
        }
        subscriber->write("keyboard-layout 1 2 us English (US)\n");
        if (!waitFor([&] { return layout->isVisible(); }) ||
            label->property("text").toString() != "us" ||
            controller.keyboardLayout()["name"].toString() != "English (US)" ||
            layout->property("description").toString() != "Keyboard layout: English (US)") {
            std::cerr << "the keyboard layout indicator does not show the active layout\n";
            return 1;
        }
        click(layout);
        if (!waitFor([&] { return label->property("text").toString() == "no"; }) ||
            layoutSwitches != 1 || controller.keyboardLayout()["number"].toInt() != 2) {
            std::cerr << "clicking the keyboard layout did not switch to the next\n";
            return 1;
        }
        if (!controller.widgets()["keyboard_layout"].toBool() ||
            !rewrite(QString(lua).replace("widgets={", "widgets={keyboard_layout=false,")))
            return fail("the keyboard layout widget was off, or the configuration could not be rewritten");
        controller.reload();
        if (!waitFor([&] { return !layout->isVisible(); })) {
            std::cerr << "shell.widgets.keyboard_layout = false did not hide the indicator\n";
            return 1;
        }
        if (!rewrite(lua))
            return fail("could not restore the configuration");
        controller.reload();
        if (!waitFor([&] { return layout->isVisible(); })) {
            std::cerr << "the keyboard layout indicator did not come back\n";
            return 1;
        }
    }
    // The binding mode in use, on a pill while it is not the default one; a click leaves it.
    {
        auto *chip = find(view.rootObject(), "bindingMode");
        if (!chip || chip->isVisible() || !controller.bindingMode().isEmpty())
            return fail("the binding mode shows before the compositor names one");
        subscriber->write("mode resize\n");
        if (!waitFor([&] { return chip->isVisible(); }) ||
            controller.bindingMode() != "resize" ||
            find(view.rootObject(), "bindingModeText")->property("text").toString() != "resize")
            return fail("the binding mode in use does not show");
        click(chip);
        if (!waitFor([&] { return !chip->isVisible(); }) ||
            !requests.contains("mode default") || !controller.bindingMode().isEmpty())
            return fail("clicking the binding mode did not leave it");
        requests.removeAll("mode default");
    }
    // The compositor says whether night light is on and who decides.
    if (controller.nightLight() || !controller.nightLightMode().isEmpty())
        return fail("night light is known before the compositor says");
    subscriber->write("night-light on auto\n");
    if (!waitFor([&] { return controller.nightLight() && controller.nightLightMode() == "auto"; }))
        return fail("the night light the compositor reports was not read");
    subscriber->write("night-light off off\n");
    if (!waitFor([&] { return !controller.nightLight() && controller.nightLightMode() == "off"; }))
        return fail("a change of night light was not read");
    // Battery and network widgets show what a (fake) sysfs reports, and only where it exists.
    {
        QDir sys(screens.filePath("sys"));
        auto put = [&](const QString &path, const QString &text) {
            sys.mkpath(QFileInfo(sys.filePath(path)).path());
            QFile f(sys.filePath(path));
            return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(text.toUtf8()) >= 0;
        };
        SystemStatus fake(sys.path());
        QQmlEngine::setObjectOwnership(&fake, QQmlEngine::CppOwnership);
        view.rootObject()->setProperty("statusSource", QVariant::fromValue(&fake));
        auto *battery = find(view.rootObject(), "batteryWidget");
        auto *network = find(view.rootObject(), "networkWidget");
        if (!battery || !network || battery->isVisible() || network->isVisible()) {
            std::cerr << "battery or network widget shown without hardware\n";
            return 1;
        }
        if (!put("class/power_supply/BAT0/type", "Battery\n") ||
            !put("class/power_supply/BAT0/capacity", "10\n") ||
            !put("class/power_supply/BAT0/status", "Discharging\n") ||
            !put("class/net/wlan0/device", "") || !put("class/net/wlan0/wireless", "") ||
            !put("class/net/wlan0/operstate", "up\n"))
            return fail("could not write the fake sysfs");
        fake.refresh();
        auto *level = find(view.rootObject(), "batteryLevel");
        if (!waitFor([&] { return battery->isVisible() && network->isVisible(); }) ||
            !level || battery->property("low").toBool() != true ||
            network->property("linkDown").toBool()) {
            std::cerr << "battery and network widgets did not appear from sysfs\n";
            return 1;
        }
        const qreal nearlyEmpty = level->width();
        if (!put("class/power_supply/BAT0/capacity", "90\n") ||
            !put("class/power_supply/BAT0/status", "Charging\n") ||
            !put("class/net/wlan0/operstate", "down\n"))
            return fail("could not update the fake sysfs");
        fake.refresh();
        if (!waitFor([&] { return level->width() > nearlyEmpty * 5; }) ||
            battery->property("low").toBool() || !network->property("linkDown").toBool()) {
            std::cerr << "battery and network widgets did not follow sysfs changes\n";
            return 1;
        }
        view.rootObject()->setProperty("statusSource", QVariant::fromValue(controller.status()));
    }
    // The clock wakes once a minute, aimed just past the next minute change, not every second.
    {
        auto *text = find(view.rootObject(), "clock");
        auto *tick = text ? text->findChild<QObject *>("clockTick") : nullptr;
        const int interval = tick ? tick->property("interval").toInt() : 0;
        if (!tick || !tick->property("running").toBool() || interval < 50 || interval > 60050) {
            std::cerr << "the clock does not tick at the minute (interval " << interval << ")\n";
            return 1;
        }
    }
    // The clock opens a month calendar, which pages through months and returns to today.
    {
        auto *clock = find(view.rootObject(), "clockButton");
        auto *calendar = find(view.rootObject(), "calendar");
        if (!clock || !calendar || calendar->isVisible()) {
            std::cerr << "calendar missing or open at start\n";
            return 1;
        }
        click(clock);
        if (!waitFor([&] { return calendar->isVisible(); })) {
            std::cerr << "clicking the clock did not open the calendar\n";
            return 1;
        }
        // Every day of the six-week grid is laid out in its own cell.
        auto *grid = find(view.rootObject(), "monthGrid");
        auto *days = grid ? grid->property("contentItem").value<QQuickItem *>() : nullptr;
        auto laidOut = [&] {
            int cells = 0; // The grid's Repeater is a child too, and has no size.
            for (QQuickItem *day : days ? days->childItems() : QList<QQuickItem *>())
                cells += day->width() >= 20 && day->height() >= 20;
            return cells == 42;
        };
        if (!waitFor(laidOut)) {
            std::cerr << "the calendar's days were not laid out:";
            if (days)
                for (QQuickItem *day : days->childItems())
                    std::cerr << ' ' << day->width() << 'x' << day->height();
            std::cerr << '\n';
            return 1;
        }
        const int month = calendar->property("month").toInt();
        click(find(view.rootObject(), "calendarNext"));
        if (!waitFor([&] { return calendar->property("month").toInt() == (month + 1) % 12; })) {
            std::cerr << "the calendar did not page to the next month\n";
            return 1;
        }
        // A wheel notch down pages to the next month, one up back again.
        auto wheelOn = [&](QQuickItem *item, int delta) {
            QWheelEvent event(centre(item), item->window()->mapToGlobal(centre(item)), {}, {0, delta},
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(item->window(), &event);
        };
        wheelOn(grid, -120);
        if (!waitFor([&] { return calendar->property("month").toInt() == (month + 2) % 12; })) {
            std::cerr << "the wheel did not page the calendar to the next month\n";
            return 1;
        }
        wheelOn(grid, 120);
        if (!waitFor([&] { return calendar->property("month").toInt() == (month + 1) % 12; })) {
            std::cerr << "the wheel did not page the calendar back\n";
            return 1;
        }
        auto *today = find(view.rootObject(), "calendarToday");
        if (!today || !today->isEnabled())
            return fail("the calendar has no Today button, or it does nothing on another month");
        click(today);
        if (!waitFor([&] { return calendar->property("month").toInt() == month; }) ||
            today->isEnabled()) {
            std::cerr << "Today did not return the calendar to this month\n";
            return 1;
        }
        // The title zooms out to the year's months, where one is picked, then to a decade's years.
        auto zoomed = [&] { return calendar->property("view").toString(); };
        auto pick = [&](const char *name, const char *property, int value) -> QQuickItem * {
            std::function<QQuickItem *(QQuickItem *)> search = [&](QQuickItem *item) -> QQuickItem * {
                if (item->objectName() == name && item->property(property).toInt() == value)
                    return item;
                for (auto *child : item->childItems())
                    if (auto *found = search(child))
                        return found;
                return nullptr;
            };
            return search(calendar);
        };
        // What is picked is clicked where it rests, once it has zoomed and slid into place.
        auto settled = [&] {
            return calendar->property("zoom").toReal() == 1 && calendar->property("shift").toReal() == 0;
        };
        auto *title = find(view.rootObject(), "calendarTitle");
        click(title);
        auto *months = find(view.rootObject(), "calendarMonths");
        if (!waitFor([&] { return zoomed() == "months" && months && months->isVisible(); }) ||
            grid->isVisible()) {
            std::cerr << "the calendar's title did not zoom out to the months\n";
            return 1;
        }
        const int later = (month + 3) % 12;
        if (!waitFor(settled))
            return fail("the months did not zoom into place");
        click(pick("calendarMonth", "month", later));
        if (!waitFor([&] { return zoomed() == "days" && calendar->property("month").toInt() == later; })) {
            std::cerr << "picking a month did not show its days\n";
            return 1;
        }
        click(title);
        if (!waitFor([&] { return zoomed() == "months"; }))
            return fail("the calendar's title did not zoom out to the months again");
        click(title);
        auto *years = find(view.rootObject(), "calendarYears");
        const int year = calendar->property("year").toInt();
        if (!waitFor([&] { return zoomed() == "years" && years && years->isVisible(); }) ||
            title->isEnabled()) {
            std::cerr << "the calendar's title did not zoom out to the years\n";
            return 1;
        }
        click(find(view.rootObject(), "calendarNext"));
        if (!waitFor([&] { return calendar->property("year").toInt() == year + 10; })) {
            std::cerr << "the next arrow did not page the years by a decade\n";
            return 1;
        }
        if (!waitFor(settled))
            return fail("the years did not slide into place");
        click(pick("calendarYear", "year", year + 11));
        if (!waitFor([&] { return zoomed() == "months" && calendar->property("year").toInt() == year + 11; })) {
            std::cerr << "picking a year did not show its months\n";
            return 1;
        }
        click(today);
        if (!waitFor([&] {
                return zoomed() == "days" && calendar->property("month").toInt() == month &&
                       calendar->property("year").toInt() == QDate::currentDate().year();
            })) {
            std::cerr << "Today did not return from the months to this month's days\n";
            return 1;
        }
        click(clock);
        if (!waitFor([&] { return !calendar->isVisible(); })) {
            std::cerr << "clicking the clock again did not close the calendar\n";
            return 1;
        }
    }
    // Context menus: a task's, then the bar's. Stand-in tasks replace the Wayland ones.
    auto *tasks = view.rootObject()->findChild<QQuickItem *>("taskList");
    // It notes what the taskbar's menus ask of the windows, as "minimize 7", and apart from
    // that what the taskbar asks of their pictures, as "watch 7 240 true" and "unwatch 7", and
    // the peeks it starts and ends as the task model has them, as "peek 7" and "end 7".
    QQmlComponent fakeTasks(view.engine());
    fakeTasks.setData(R"(import QtQml.Models
ListModel {
    property var requests: []
    property var pictures: []
    property var peeks: []
    property int peekedTask: -1
    function note(request) { requests = requests.concat([request]) }
    function watchPicture(id, width, live) { pictures = pictures.concat(["watch " + id + " " + width + " " + live]) }
    function unwatchPicture(id) { pictures = pictures.concat(["unwatch " + id]) }
    function peek(id) { if (peekedTask !== id) { peekedTask = id; peeks = peeks.concat(["peek " + id]) } }
    function endPeek(id) { if (peekedTask === id) { peekedTask = -1; peeks = peeks.concat(["end " + id]) } }
    function activate(id) { note("activate " + id) }
    function minimize(id) { note("minimize " + id) }
    function maximize(id) { note("maximize " + id) }
    function setFullscreen(id, on) { note("fullscreen " + id + " " + on) }
    function close(id) { note("close " + id) }
    function kill(id) { note("kill " + id) }
    function moveToWorkspace(id, number) { note("workspace " + id + " " + number) }
    function moveToOutput(id, output) { note("output " + id + " " + output) }
    function setSticky(id, on) { note("sticky " + id + " " + on) }
    function setFloating(id, on) { note("floating " + id + " " + on) }
    function setAbove(id, on) { note("above " + id + " " + on) }
    ListElement { taskId: 7; title: 'Fake'; appId: 'fake'; active: false; minimized: false; urgent: false
                  maximized: false; fullscreen: false; output: 'TEST-1'; workspace: 2; sticky: false
                  floating: false; tiling: false; above: false; picture: ''; pid: 0 }
})",
                      QUrl());
    QObject *fakeModel = fakeTasks.create();
    if (!tasks || !fakeModel)
        return fail("the task models did not load");
    QQmlEngine::setObjectOwnership(fakeModel, QQmlEngine::CppOwnership);
    view.rootObject()->setProperty("taskSource", QVariant::fromValue(fakeModel));
    view.engine()->addImageProvider("test-windows", new TestPictures);
    // What the menus asked of the stand-in windows since the last call, joined by "|".
    auto taskRequests = [&] {
        const auto asked = fakeModel->property("requests").value<QJSValue>().toVariant().toStringList();
        fakeModel->setProperty("requests", QVariant::fromValue(view.engine()->newArray()));
        return asked.join("|");
    };
    // ListModel's methods take JavaScript arguments, so they are reached through the engine.
    auto editTasks = [&](const QString &body) {
        view.engine()
            ->evaluate("(function(model) { " + body + " })")
            .call({view.engine()->newQObject(fakeModel)});
    };
    QQuickItem *task = nullptr;
    // The button of the index-th window along the bar, past the launchers of the pinned
    // applications.
    auto listedTask = [&](int index) {
        QQuickItem *item = nullptr;
        return waitFor([&] {
            item = nullptr;
            for (int row = 0, windows = 0; row < tasks->property("count").toInt(); ++row) {
                QQuickItem *button = nullptr;
                QMetaObject::invokeMethod(tasks, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, button),
                                          Q_ARG(int, row));
                if (button && !button->property("launcher").toBool() && windows++ == index) {
                    item = button;
                    break;
                }
            }
            return item != nullptr;
        })
                   ? item
                   : nullptr;
    };
    // How many windows' buttons the bar has.
    auto windowButtons = [&] {
        return tasks->property("count").toInt() -
               tasks->property("model").value<QObject *>()->property("launchers").toInt();
    };
    if (!(task = listedTask(0)))
        return fail("the first task is not listed");
    // A window's title is its own: shown as plain text, never read as markup.
    {
        editTasks("model.append({taskId: 99, title: '<b>bold</b>', appId: 'evil', active: false, "
                  "minimized: false, urgent: false})");
        QQuickItem *hostile = listedTask(1);
        int shown = 0;
        bool markup = false;
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
            if (item->property("text").toString() == "<b>bold</b>") {
                ++shown;
                markup = markup || item->property("textFormat").toInt() != Qt::PlainText;
            }
            for (auto *child : item->childItems())
                walk(child);
        };
        if (hostile)
            walk(hostile);
        if (!hostile || shown == 0 || markup) {
            std::cerr << "a window title is not shown as plain text (" << shown << " shown)\n";
            return 1;
        }
        editTasks("model.remove(1)");
    }
    // A window's button fades and grows in as its window opens, its line drawn out, and shrinks
    // away as it closes, taking no more clicks. Slowed down, so that it is seen on its way.
    {
        if (!slowMotion(true))
            return fail("the animations were not slowed down");
        editTasks("model.append({taskId: 98, title: 'Arriving', appId: 'arriving', active: false, "
                  "minimized: false, urgent: false})");
        // Not the button of the window closed just before, which may still be on its way out.
        QPointer<QQuickItem> arriving;
        if (!waitFor([&] {
                arriving = listedTask(1);
                return arriving && arriving->property("taskId").toInt() == 98;
            }) ||
            arriving->opacity() == 1 || arriving->scale() == 1 || arriving->property("reveal").toReal() == 1)
            return fail("a window's button did not fade and grow in");
        if (!waitFor([&] {
                return arriving && arriving->opacity() == 1 && arriving->scale() == 1 &&
                       arriving->property("reveal").toReal() == 1;
            }))
            return fail("a window's button did not come all the way in");
        editTasks("model.remove(1)");
        if (!waitFor([&] { return arriving && arriving->opacity() < 1; }) ||
            arriving->isEnabled())
            return fail("a closing window's button did not fade out, or still takes clicks");
        if (!waitFor([&] { return !arriving || !arriving->isVisible(); }))
            return fail("a closed window's button stayed on the bar");
        // An icon on the bar following a state crossfades to its new shape, as the tiling
        // button's does.
        auto *tilingIcon = find(tiling, "tilingIcon");
        const bool wasTiling = panelTiling();
        click(tiling);
        if (!tilingIcon || !waitFor([&] { return panelTiling() != wasTiling; }) ||
            tilingIcon->property("leaving").toString().isEmpty() ||
            tilingIcon->property("progress").toReal() == 1)
            return fail("the tiling button's icon did not crossfade to its new shape");
        if (!waitFor([&] {
                return tilingIcon->property("leaving").toString().isEmpty() &&
                       tilingIcon->property("progress").toReal() == 1;
            }))
            return fail("the tiling button's icon did not finish its crossfade");
        click(tiling);
        if (!waitFor([&] { return panelTiling() == wasTiling; }))
            return fail("the tiling button did not toggle tiling back");
        if (!slowMotion(false))
            return fail("the animations did not get their speed back");
    }
    auto *menu = find(view.rootObject(), "contextMenu");
    // Repeater delegates are visual children only, so walk the item tree. Rows of the task
    // menus are named contextMenuItem, contextMenuTitle, contextMenuAction and the like.
    std::function<QQuickItem *(QQuickItem *, const QString &)> findMenuItem =
        [&](QQuickItem *parent, const QString &text) -> QQuickItem * {
        for (auto *item : parent->childItems()) {
            if (item->objectName().startsWith("contextMenu") && item->objectName() != "contextMenu" &&
                item->property("text") == text)
                return item;
            if (auto *found = findMenuItem(item, text))
                return found;
        }
        return nullptr;
    };
    auto menuItem = [&](const QString &text) { return findMenuItem(menu, text); };
    // The whole menu must lie inside the popover.
    auto menuShown = [&] { return inPopover(menu); };
    const QPoint entry = centre(task);
    // Held past the long-press time, which once swallowed the right click.
    QTest::mousePress(&view, Qt::RightButton, Qt::NoModifier, entry);
    QTest::qWait(1000);
    QTest::mouseRelease(&view, Qt::RightButton, Qt::NoModifier, centre(task));
    if (!waitFor([&] {
            return view.rootObject()->property("taskMenuId").toInt() == 7 && menuShown();
        }) ||
        !menuItem("Maximize") || !menuItem("Minimize") || !menuItem("Close window")) {
        std::cerr << "right-clicking a task did not show its menu\n";
        return 1;
    }
    click(menuItem("Minimize"));
    if (!waitFor([&] { return !view.rootObject()->property("menuOpen").toBool(); }) ||
        !waitFor([&] { return !popover->isVisible(); })) {
        std::cerr << "choosing a task menu item did not close the menu\n";
        return 1;
    }
    if (const auto asked = taskRequests(); asked != "minimize 7") {
        std::cerr << "minimizing from a task's menu asked " << asked.toStdString() << '\n';
        return 1;
    }
    // The window's entries follow its state, while the menu is open too: maximized, it offers to
    // restore it, and fullscreen is checked; minimized, it offers only to restore it.
    {
        auto checked = [&](const QString &text) {
            return menuItem(text) && menuItem(text)->property("marked").toBool();
        };
        click(task, Qt::RightButton);
        if (!waitFor([&] { return menuShown() && menuItem("Maximize"); }) ||
            !menuItem("Fullscreen") || checked("Fullscreen") || menuItem("Restore"))
            return fail("a window's menu does not offer to maximize it and make it fullscreen");
        editTasks("model.setProperty(0, 'maximized', true); model.setProperty(0, 'fullscreen', true)");
        if (!waitFor([&] {
                return menuItem("Restore") && !menuItem("Maximize") && checked("Fullscreen") &&
                       menuItem("Restore")->property("modelData").toMap()["icon"] == "copy";
            }))
            return fail("the window's menu did not follow it maximized and fullscreen");
        click(menuItem("Fullscreen"));
        if (const auto asked = taskRequests(); asked != "fullscreen 7 false") {
            std::cerr << "leaving fullscreen from a task's menu asked " << asked.toStdString() << '\n';
            return 1;
        }
        editTasks("model.setProperty(0, 'minimized', true)");
        click(task, Qt::RightButton);
        if (!waitFor([&] { return menuShown() && menuItem("Restore"); }) ||
            menuItem("Minimize") || menuItem("Maximize") || menuItem("Fullscreen"))
            return fail("a minimized window's menu offers more than to restore it");
        click(menuItem("Restore"));
        if (const auto asked = taskRequests(); asked != "activate 7") {
            std::cerr << "restoring from a task's menu asked " << asked.toStdString() << '\n';
            return 1;
        }
        editTasks("model.setProperty(0, 'minimized', false); model.setProperty(0, 'maximized', false); "
                  "model.setProperty(0, 'fullscreen', false)");
        if (!waitFor([&] { return !popover->isVisible(); }))
            return fail("the popover did not close after restoring");
    }
    // Moving the window: its monitor's workspaces, by name where they have one, the one it is on
    // marked; a sticky window is on none of them.
    {
        click(task, Qt::RightButton);
        if (!waitFor([&] { return menuShown() && menuItem("Move to workspace"); }))
            return fail("a window's menu does not offer to move it to another workspace");
        click(menuItem("Move to workspace"));
        auto marked = [&](const QString &text) {
            return menuItem(text) && menuItem(text)->property("marked").toBool();
        };
        if (!waitFor([&] {
                return menuItem("web") && menuItem("Workspace 2") && menuItem("Workspace 3") &&
                       menuItem("mail") && marked("Workspace 2") && !marked("web");
            }))
            return fail("the workspace submenu does not list the workspaces, the window's marked");
        editTasks("model.setProperty(0, 'sticky', true)");
        if (!waitFor([&] { return menuItem("mail") && !marked("Workspace 2"); }))
            return fail("the workspace submenu marks a workspace for a sticky window");
        click(menuItem("mail"));
        if (const auto asked = taskRequests(); asked != "workspace 7 4") {
            std::cerr << "moving to a workspace from a task's menu asked " << asked.toStdString() << '\n';
            return 1;
        }
        editTasks("model.setProperty(0, 'sticky', false)");
        if (!waitFor([&] { return !popover->isVisible(); }))
            return fail("the popover did not close after moving the window");
        // With one monitor there is no other to move it to; with two, its own is marked.
        click(task, Qt::RightButton);
        if (!waitFor([&] { return menuShown() && menuItem("Move to workspace"); }) ||
            menuItem("Move to monitor"))
            return fail("a window's menu offers other monitors with only one");
        subscriber->write(state(toggled, currentWorkspace) + "output OTHER-1 1 - off\n");
        if (!waitFor([&] { return menuItem("Move to monitor"); }))
            return fail("a window's menu does not offer to move it to another monitor");
        click(menuItem("Move to monitor"));
        if (!waitFor([&] { return menuItem("OTHER-1") && marked(output); }) || marked("OTHER-1"))
            return fail("the monitor submenu does not list the monitors, the window's marked");
        click(menuItem("OTHER-1"));
        if (const auto asked = taskRequests(); asked != "output 7 OTHER-1") {
            std::cerr << "moving to a monitor from a task's menu asked " << asked.toStdString() << '\n';
            return 1;
        }
        subscriber->write(state(toggled, currentWorkspace));
        if (!waitFor([&] { return !popover->isVisible() && controller.workspaces().size() == 1; }))
            return fail("the popover did not close after moving the window to another monitor");
    }
    // Keeping it on every workspace, and floating it where its workspace tiles.
    {
        click(task, Qt::RightButton);
        if (!waitFor([&] { return menuShown() && menuItem("Keep on all workspaces"); }) ||
            menuItem("Keep on all workspaces")->property("marked").toBool() || menuItem("Float"))
            return fail("a window's menu does not offer to keep it on all workspaces, or floats it "
                        "where nothing tiles");
        editTasks("model.setProperty(0, 'tiling', true)");
        if (!waitFor([&] { return menuItem("Float"); }) ||
            menuItem("Float")->property("marked").toBool())
            return fail("a tiled window's menu does not offer to float it");
        click(menuItem("Float"));
        click(task, Qt::RightButton);
        if (!waitFor([&] { return menuShown() && menuItem("Keep on all workspaces"); }))
            return fail("the window's menu did not open again");
        click(menuItem("Keep on all workspaces"));
        if (const auto asked = taskRequests(); asked != "floating 7 true|sticky 7 true") {
            std::cerr << "floating and sticking from a task's menu asked " << asked.toStdString() << '\n';
            return 1;
        }
        editTasks("model.setProperty(0, 'tiling', false)");
        if (!waitFor([&] { return !popover->isVisible(); }))
            return fail("the popover did not close after making the window sticky");
    }
    // Keeping it above the others, checked while it is.
    {
        auto above = [&] { return menuItem("Keep above others"); };
        click(task, Qt::RightButton);
        if (!waitFor([&] { return menuShown() && above(); }) || above()->property("marked").toBool())
            return fail("a window's menu does not offer to keep it above the others");
        click(above());
        if (const auto asked = taskRequests(); asked != "above 7 true") {
            std::cerr << "keeping a window above from its menu asked " << asked.toStdString() << '\n';
            return 1;
        }
        if (!waitFor([&] { return !popover->isVisible(); }))
            return fail("the popover did not close after keeping the window above");
        editTasks("model.setProperty(0, 'above', true)");
        click(task, Qt::RightButton);
        if (!waitFor([&] { return menuShown() && above() && above()->property("marked").toBool(); }))
            return fail("a window kept above is not checked in its menu");
        click(above());
        if (const auto asked = taskRequests(); asked != "above 7 false") {
            std::cerr << "letting a window go from its menu asked " << asked.toStdString() << '\n';
            return 1;
        }
        editTasks("model.setProperty(0, 'above', false)");
        if (!waitFor([&] { return !popover->isVisible(); }))
            return fail("the popover did not close after letting the window go");
    }
    // Closing the window comes last, in the danger colour.
    {
        click(task, Qt::RightButton);
        if (!waitFor([&] { return menuShown() && menuItem("Close window"); }))
            return fail("the window's menu did not open to close it");
        const auto entries = menu->property("entries").value<QJSValue>();
        const auto last = entries.property(entries.property("length").toUInt() - 1);
        if (last.property("text").toString() != "Close window" ||
            !menuItem("Close window")->property("danger").toBool() ||
            menuItem("Close window")->objectName() != "contextMenuClose")
            return fail("closing a window is not the last entry of its menu, in the danger colour");
        click(menuItem("Close window"));
        if (const auto asked = taskRequests(); asked != "close 7") {
            std::cerr << "closing from a task's menu asked " << asked.toStdString() << '\n';
            return 1;
        }
        if (!waitFor([&] { return !popover->isVisible(); }))
            return fail("the popover did not close after closing the window");
    }
    // Killing its process comes after that, once the process is known.
    {
        editTasks("model.setProperty(0, 'pid', 4242)");
        click(task, Qt::RightButton);
        if (!waitFor([&] { return menuShown() && menuItem("Kill process"); }))
            return fail("the window's menu does not offer to kill its process");
        const auto entries = menu->property("entries").value<QJSValue>();
        const auto last = entries.property(entries.property("length").toUInt() - 1);
        if (last.property("text").toString() != "Kill process" ||
            !menuItem("Kill process")->property("danger").toBool())
            return fail("killing a window's process is not the last entry of its menu, in the danger colour");
        click(menuItem("Kill process"));
        if (const auto asked = taskRequests(); asked != "kill 7") {
            std::cerr << "killing from a task's menu asked " << asked.toStdString() << '\n';
            return 1;
        }
        if (!waitFor([&] { return !popover->isVisible(); }))
            return fail("the popover did not close after killing the process");
        editTasks("model.setProperty(0, 'pid', 0)");
    }
    // A stacked button's menu is about all its windows: counted under the title, minimized or
    // restored, moved and closed together, the workspace they share marked.
    {
        editTasks("model.append({ taskId: 9, title: 'Second', appId: 'fake', active: false, "
                  "minimized: false, urgent: false, maximized: false, fullscreen: false, "
                  "output: 'TEST-1', workspace: 2, sticky: false, floating: false, tiling: false })");
        if (!waitFor([&] { return task->property("stacked").toBool(); }))
            return fail("the application's two windows did not stack");
        auto title = [&] { return find(view.rootObject(), "contextMenuTitle"); };
        click(task, Qt::RightButton);
        if (!waitFor([&] {
                return menuShown() && title() && menuItem("Close all 2 windows") && menuItem("Minimize all");
            }) ||
            title()->property("modelData").toMap()["secondary"] != "2 windows" || menuItem("Maximize") ||
            menuItem("Fullscreen") || menuItem("Close window"))
            return fail("a stacked button's menu is not about all its windows");
        click(menuItem("Move to workspace"));
        if (!waitFor([&] {
                return menuItem("Workspace 2") && menuItem("Workspace 2")->property("marked").toBool();
            }))
            return fail("the workspace both windows are on is not marked");
        click(menuItem("web"));
        if (const auto asked = taskRequests(); asked != "workspace 7 1|workspace 9 1") {
            std::cerr << "moving a stack to a workspace asked " << asked.toStdString() << '\n';
            return 1;
        }
        click(task, Qt::RightButton);
        if (!waitFor([&] { return menuShown() && menuItem("Close all 2 windows"); }))
            return fail("the stacked button's menu did not open again");
        click(menuItem("Close all 2 windows"));
        if (const auto asked = taskRequests(); asked != "close 7|close 9") {
            std::cerr << "closing a stack asked " << asked.toStdString() << '\n';
            return 1;
        }
        editTasks("model.setProperty(0, 'minimized', true); model.setProperty(1, 'minimized', true)");
        click(task, Qt::RightButton);
        if (!waitFor([&] { return menuShown() && menuItem("Restore all"); }) ||
            menuItem("Minimize all"))
            return fail("a stack of minimized windows does not offer to restore them");
        click(menuItem("Restore all"));
        if (const auto asked = taskRequests(); asked != "activate 7|activate 9") {
            std::cerr << "restoring a stack asked " << asked.toStdString() << '\n';
            return 1;
        }
        editTasks("model.remove(1); model.setProperty(0, 'minimized', false)");
        QMetaObject::invokeMethod(tasks, "forceLayout");
        if (!waitFor([&] { return !popover->isVisible() && !task->property("stacked").toBool(); }))
            return fail("the popover did not close after restoring the stack");
    }
    // The task's window belongs to an installed application, which its menu pins. Pinned, grouped
    // as windows are by default, the window's button becomes the application's, staying where it
    // is; with no window left it is the application's launcher, and its own menu unpins it. Pins
    // are remembered in the state directory.
    auto readPins = [&] {
        QFile file(pins);
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    };
    auto pinned = [&] { return find(view.rootObject(), "pinned:paw-test-app.desktop"); };
    auto pinnedTask = [&] { return find(view.rootObject(), "pinnedTask:paw-test-app.desktop"); };
    const int buttons = tasks->property("count").toInt();
    QPointer<QQuickItem> unpinnedButton = task;
    click(task, Qt::RightButton);
    if (!waitFor([&] { return menuShown() && menuItem("Pin to taskbar"); })) {
        std::cerr << "a task's menu did not offer to pin its application\n";
        return 1;
    }
    click(menuItem("Pin to taskbar"));
    if (!waitFor([&] { return pinnedTask() != nullptr; }) ||
        !controller.isPinned("paw-test-app.desktop") ||
        readPins() != "paw-test-app.desktop\n") {
        std::cerr << "pinning a task's application did not pin and save it\n";
        return 1;
    }
    if (pinnedTask() != unpinnedButton || pinned() || tasks->property("count").toInt() != buttons) {
        std::cerr << "a pinned application's window did not keep its button\n";
        return 1;
    }
    if (!waitFor([&] { return !popover->isVisible(); })) {
        std::cerr << "the popover did not close after pinning\n";
        return 1;
    }
    // Dragging a pinned application's button past another's launcher moves the pin there.
    controller.pin("paw-test-other.desktop");
    auto other = [&] { return find(view.rootObject(), "pinned:paw-test-other.desktop"); };
    if (!waitFor([&] {
            return other() && other()->isVisible() &&
                   centre(other()).x() > centre(pinnedTask()).x();
        }))
        return fail("an application just pinned is not after the others");
    {
        const QPoint from = centre(pinnedTask()), to = centre(other());
        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, from);
        for (int step = 1; step <= 10; ++step) {
            QTest::mouseMove(&view, from + (to - from) * step / 10);
            QTest::qWait(10);
        }
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, to);
    }
    if (!waitFor(
            [&] { return readPins() == "paw-test-other.desktop\npaw-test-app.desktop\n"; }) ||
        !waitFor([&] {
            return pinnedTask() && other() &&
                   other()->mapToScene({0, 0}).x() < pinnedTask()->mapToScene({0, 0}).x();
        })) {
        std::cerr << "dragging a pinned application's button past another did not move its pin\n";
        return 1;
    }
    controller.unpin("paw-test-other.desktop");
    editTasks("model.remove(0)");
    if (!waitFor([&] { return !pinnedTask() && pinned() && pinned()->isVisible(); })) {
        std::cerr << "a pinned application's button did not become its launcher once its window closed\n";
        return 1;
    }
    // Slowed down: a window of a pinned application opening draws its line out under the
    // launcher's icon, which stays, and an application just pinned fades and grows in.
    {
        if (!slowMotion(true))
            return fail("the animations were not slowed down");
        editTasks("model.append({taskId: 12, title: 'Fake again', appId: 'fake', active: false, "
                  "minimized: false, urgent: false})");
        QPointer<QQuickItem> arrived;
        if (!waitFor([&] { return (arrived = pinnedTask()) != nullptr; }) ||
            arrived->property("reveal").toReal() == 1 || arrived->opacity() != 1 || arrived->scale() != 1)
            return fail("a pinned application's window opening did not only draw its line out");
        if (!waitFor([&] { return arrived && arrived->property("reveal").toReal() == 1; }))
            return fail("the line of a pinned application's window opening was not drawn all the way");
        editTasks("model.remove(0)");
        controller.pin("paw-test-other.desktop");
        QPointer<QQuickItem> launcher;
        if (!waitFor([&] { return (launcher = other()) != nullptr; }) ||
            (launcher->opacity() == 1 && launcher->scale() == 1))
            return fail("an application just pinned did not grow in");
        if (!waitFor([&] { return launcher && launcher->opacity() == 1 && launcher->scale() == 1; }))
            return fail("an application just pinned did not come all the way in");
        if (!pinned() || pinned()->opacity() != 1 || pinned()->scale() != 1)
            return fail("a launcher that was there already came in again with another");
        controller.unpin("paw-test-other.desktop");
        if (!slowMotion(false))
            return fail("the animations did not get their speed back");
        if (!waitFor([&] { return !other() && !pinnedTask() && pinned() && pinned()->isVisible(); }))
            return fail("the launchers did not go back as they were");
    }
    click(pinned(), Qt::RightButton);
    if (!waitFor([&] { return menuShown() && menuItem("Unpin from taskbar"); }) ||
        !menuItem("Open") || !menuItem("Fake app") ||
        menuItem("Fake app")->objectName() != "contextMenuTitle") {
        std::cerr << "a pinned application's menu did not offer to unpin it\n";
        return 1;
    }
    // One with desktop actions offers them before opening it, and nothing of a window's.
    {
        QTest::keyClick(popover, Qt::Key_Escape);
        controller.pin("paw-test-actions.desktop");
        auto withActions = [&] { return find(view.rootObject(), "pinned:paw-test-actions.desktop"); };
        if (!waitFor([&] { return withActions() && withActions()->isVisible() && !popover->isVisible(); }))
            return fail("the application with desktop actions was not pinned");
        click(withActions(), Qt::RightButton);
        if (!waitFor([&] { return menuShown() && menuItem("Touch a file"); }) ||
            !menuItem("Missing program") || !menuItem("Open") || !menuItem("Unpin from taskbar") ||
            menuItem("New window") || menuItem("Minimize") || menuItem("Close window"))
            return fail("a pinned application's menu does not offer its desktop actions and to open it");
        const auto entries = menu->property("entries").value<QJSValue>();
        QStringList order; // the title or text of each entry, "-" for a separator
        for (quint32 i = 0; i < entries.property("length").toUInt(); ++i) {
            const auto entry = entries.property(i);
            order << (entry.property("separator").toBool() ? QString("-")
                      : entry.property("title").isString() ? entry.property("title").toString()
                                                           : entry.property("text").toString());
        }
        if (order.join("|") != "Action app|-|Touch a file|Missing program|Open|-|Unpin from taskbar") {
            std::cerr << "a pinned application's menu is in the wrong order: "
                      << order.join("|").toStdString() << '\n';
            return 1;
        }
        QTest::keyClick(popover, Qt::Key_Escape);
        controller.unpin("paw-test-actions.desktop");
        if (!waitFor([&] { return !withActions() && !popover->isVisible(); }))
            return fail("the application with desktop actions was not unpinned");
        click(pinned(), Qt::RightButton);
        if (!waitFor([&] { return menuShown() && menuItem("Unpin from taskbar"); }))
            return fail("the pinned application's menu did not open again");
    }
    click(menuItem("Unpin from taskbar"));
    if (!waitFor([&] { return pinned() == nullptr; }) || !readPins().isEmpty() ||
        controller.isPinned("paw-test-app.desktop")) {
        std::cerr << "unpinning did not remove and forget the taskbar button\n";
        return 1;
    }
    if (!waitFor([&] { return !popover->isVisible(); })) {
        std::cerr << "the popover did not close after unpinning\n";
        return 1;
    }
    // Apart (shell.group_windows = false), a pinned application's windows have a button each, as
    // any window has, and each moves along the bar on its own: the first opens in the launcher's
    // place, the others at the end, and dragging one moves neither the others nor the pin. With no
    // window left, the launcher comes back in its place.
    {
        if (!rewrite(QString(lua).replace("shell={", "shell={group_windows=false,")))
            return fail("could not rewrite the configuration");
        controller.reload();
        if (!waitFor([&] { return !controller.groupWindows(); }))
            return fail("shell.group_windows = false was not read");
        controller.pin("paw-test-app.desktop");
        QPointer<QQuickItem> launcher;
        if (!waitFor([&] { return (launcher = pinned()) && launcher->isVisible(); }))
            return fail("the pinned application's launcher is not on the bar");
        editTasks("model.append({ taskId: 20, title: 'Fake one', appId: 'fake', active: false, "
                  "minimized: false, urgent: false });"
                  "model.append({ taskId: 21, title: 'Fake two', appId: 'fake', active: false, "
                  "minimized: false, urgent: false });"
                  "model.append({ taskId: 22, title: 'Second', appId: 'second', active: false, "
                  "minimized: false, urgent: false })");
        auto order = [&] {
            QList<int> ids;
            for (int i = 0; i < 3; ++i) {
                auto *button = listedTask(i);
                ids.push_back(button ? button->property("taskId").toInt() : -1);
            }
            return ids;
        };
        if (!waitFor([&] { return order() == QList<int>{20, 21, 22}; }) || pinned() ||
            listedTask(0) != launcher.data())
            return fail("a pinned application's windows did not open in its launcher's place and at the end");
        // Every button where the list lays it out, none sliding over still.
        auto laidOut = [&] {
            QQuickItem *previous = nullptr;
            for (int row = 0; row < tasks->property("count").toInt(); ++row) {
                QQuickItem *button = nullptr;
                QMetaObject::invokeMethod(tasks, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, button),
                                          Q_ARG(int, row));
                if (!button || (previous && button->x() != previous->x() + previous->width() +
                                                               tasks->property("spacing").toReal()))
                    return false;
                previous = button;
            }
            return true;
        };
        auto drag = [&](QQuickItem *button, QQuickItem *past) {
            if (!waitFor(laidOut))
                return false;
            const QPoint from = centre(button), to = centre(past) + QPoint(int(past->width()) / 4, 0);
            QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, from);
            for (int step = 1; step <= 10; ++step) {
                QTest::mouseMove(&view, from + (to - from) * step / 10);
                QTest::qWait(10);
            }
            QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, to);
            return true;
        };
        if (!drag(listedTask(1), listedTask(2)) ||
            !waitFor([&] { return order() == QList<int>{20, 22, 21}; }) ||
            readPins() != "paw-test-app.desktop\n")
            return fail("a pinned application's window did not move on its own");
        if (!drag(listedTask(0), listedTask(2)) ||
            !waitFor([&] { return order() == QList<int>{22, 21, 20}; }) ||
            readPins() != "paw-test-app.desktop\n")
            return fail("the window in a pinned application's place did not move on its own");
        editTasks("model.remove(0); model.remove(0)");
        if (!waitFor([&] {
                return pinned() && pinned()->isVisible() && windowButtons() == 1 && laidOut() &&
                       pinned()->x() < listedTask(0)->x();
            }))
            return fail("the launcher did not come back in its place once its windows closed");
        editTasks("model.remove(0)");
        controller.unpin("paw-test-app.desktop");
        if (!rewrite(lua))
            return fail("could not restore the configuration");
        controller.reload();
        if (!waitFor([&] { return controller.groupWindows() && !pinned(); }))
            return fail("the configuration was not restored");
    }
    editTasks("model.append({ taskId: 7, title: 'Fake', appId: 'fake', active: false, "
              "minimized: false, urgent: false })");
    if (!(task = listedTask(0)))
        return fail("the re-added task is not listed");
    // A window's menu is headed by its application's name and the window's title. One of an
    // application with desktop actions offers them, with their icons, and to start it again.
    {
        auto title = [&] { return find(view.rootObject(), "contextMenuTitle"); };
        click(task, Qt::RightButton);
        if (!waitFor([&] { return menuShown() && title(); }) ||
            title()->property("text") != "Fake app" ||
            title()->property("modelData").toMap()["secondary"] != "Fake" || !menuItem("New window")) {
            std::cerr << "a window's menu is not headed by its application and title\n";
            return 1;
        }
        // The keyboard passes over the title.
        QTest::keyClick(popover, Qt::Key_Home);
        if (!waitFor([&] { return menuItem("New window")->property("highlighted").toBool(); }) ||
            title()->property("highlighted").toBool())
            return fail("Home did not go to the first entry under the title");
        QTest::keyClick(popover, Qt::Key_Escape);
        editTasks("model.append({ taskId: 8, title: 'Report', appId: 'paw-test-actions', "
                  "active: false, minimized: false, urgent: false })");
        QQuickItem *withActions = listedTask(1);
        if (!withActions)
            return fail("the window with desktop actions is not listed");
        click(withActions, Qt::RightButton);
        if (!waitFor([&] {
                return menuShown() && title() && title()->property("text") == "Action app" &&
                       menuItem("Touch a file") && menuItem("Missing program") &&
                       menuItem("New window");
            }) ||
            menuItem("Touch a file")->objectName() != "contextMenuAction" ||
            menuItem("Touch a file")->property("modelData").toMap()["icon"] != "document-new") {
            std::cerr << "a window's menu does not offer its application's desktop actions\n";
            return 1;
        }
        QFile::remove(actionMarker);
        click(menuItem("Touch a file"));
        if (!waitFor([&] { return QFile::exists(actionMarker); }) ||
            !waitFor([&] { return !view.rootObject()->property("menuOpen").toBool(); })) {
            std::cerr << "a desktop action in a window's menu did not run\n";
            return 1;
        }
        editTasks("model.remove(1)");
        // The list drops the button when it next lays itself out.
        QMetaObject::invokeMethod(tasks, "forceLayout");
    }
    // Empty bar space, right of the only task, opens the bar menu.
    const QPoint empty =
        task->mapToScene(QPointF(task->width() + 40, task->height() / 2)).toPoint();
    QTest::mouseClick(&view, Qt::RightButton, Qt::NoModifier, empty);
    if (!waitFor(
            [&] { return view.rootObject()->property("barMenuOpen").toBool() && menuShown(); }) ||
        !menuItem("Turn tiling off") || !menuItem("Applications")) {
        std::cerr << "right-clicking empty bar space did not show the bar menu\n";
        return 1;
    }
    // While a menu is open the popover holds the keyboard and takes the pointer everywhere but
    // over the bar, whose buttons stay reachable: another one opens its popup in one press. A
    // press beside the popups closes them, and the popover goes, taking nothing any more.
    {
        const QRegion outsideBar(0, 0, popover->width(), popover->height() - view.height());
        if (!popover->keyboard() || popover->inputRegion() != outsideBar) {
            std::cerr << "an open menu does not take the keyboard and the pointer beside the bar\n";
            return 1;
        }
        auto *calendar = find(view.rootObject(), "calendar");
        click(find(view.rootObject(), "clockButton"));
        if (!waitFor([&] { return calendar && inPopover(calendar); }) ||
            view.rootObject()->property("barMenuOpen").toBool() || !popover->isVisible()) {
            std::cerr << "a press on the clock while the bar menu was open did not switch to the calendar\n";
            return 1;
        }
        QTest::mouseClick(popover, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
        if (!waitFor([&] { return !calendar->isVisible() && !popover->isVisible(); }) ||
            view.rootObject()->property("menuOpen").toBool() || popover->keyboard() ||
            !popover->inputRegion().isEmpty()) {
            std::cerr << "a press beside the calendar did not close it and the popover\n";
            return 1;
        }
        QTest::mouseClick(&view, Qt::RightButton, Qt::NoModifier, empty);
        if (!waitFor([&] { return view.rootObject()->property("barMenuOpen").toBool() && menuShown(); }))
            return fail("the bar menu did not open again");
    }
    click(menuItem("Turn tiling off"));
    if (!waitFor([&] { return !toggled && !panelTiling(); }) ||
        view.rootObject()->property("menuOpen").toBool()) {
        std::cerr << "the bar menu did not toggle tiling off\n";
        return 1;
    }
    // The bar menu's appearance entry opens the profiles beside it, the one in use marked, and
    // the menu stays; picking one switches.
    QTest::mouseClick(&view, Qt::RightButton, Qt::NoModifier, empty);
    if (!waitFor([&] {
            return menuShown() && menuItem("Appearance") &&
                   menuItem("Appearance")->property("modelData").toMap()["secondary"] == "dark";
        })) {
        std::cerr << "the bar menu lacks the appearance profiles\n";
        return 1;
    }
    // The card a row is on, and whether one card lies right of another.
    auto cardOf = [](QQuickItem *row) {
        while (row && !row->property("anchorRect").isValid())
            row = row->parentItem();
        return row;
    };
    auto beside = [](QQuickItem *left, QQuickItem *right) {
        const QRectF a = left->mapRectToScene(QRectF(0, 0, left->width(), left->height()));
        const QRectF b = right->mapRectToScene(QRectF(0, 0, right->width(), right->height()));
        return b.left() >= a.right() - 1 && b.top() < a.bottom() && b.bottom() > a.top();
    };
    click(menuItem("Appearance"));
    if (!waitFor([&] {
            return menuShown() && menuItem("dark") && menuItem("light") && menuItem("Applications") &&
                   menuItem("dark")->property("marked").toBool() &&
                   !menuItem("light")->property("marked").toBool() &&
                   menuItem("Appearance")->property("expanded").toBool() &&
                   cardOf(menuItem("dark")) != cardOf(menuItem("Applications")) &&
                   beside(cardOf(menuItem("Applications")), cardOf(menuItem("dark"))) &&
                   inPopover(cardOf(menuItem("dark")));
        })) {
        std::cerr << "the appearance entry did not open the profiles beside the menu\n";
        return 1;
    }
    click(menuItem("light"));
    if (!waitFor([&] {
            return requests == QStringList{"profile light"} && controller.profile() == "light" &&
                   controller.accent() == QColor("#336699") &&
                   !view.rootObject()->property("barMenuOpen").toBool();
        })) {
        std::cerr << "picking a profile did not switch to it: " << requests.join("|").toStdString()
                  << " " << controller.profile().toStdString() << '\n';
        return 1;
    }
    requests.clear();
    QTest::mouseClick(&view, Qt::RightButton, Qt::NoModifier, empty);
    if (!waitFor([&] {
            return menuShown() && menuItem("Appearance") && !menuItem("dark") &&
                   menuItem("Appearance")->property("modelData").toMap()["secondary"] == "light";
        })) {
        std::cerr << "the bar menu did not open without its submenu, with the new profile\n";
        return 1;
    }
    // The keyboard: nothing is highlighted after a right click; Down and Up move, wrapping and
    // skipping nothing that can be chosen; End and Home go to the ends; Right opens a submenu at
    // its first entry, Left closes it; Escape closes a submenu, then the menu. The pointer rests
    // away from the menu, where it would select what opens under it.
    QTest::mouseMove(popover, QPoint(5, 5));
    {
        auto highlighted = [&] {
            QStringList rows;
            std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
                for (auto *child : item->childItems()) {
                    if (child->objectName() == "contextMenuItem" && child->isVisible() &&
                        child->property("highlighted").toBool())
                        rows << child->property("text").toString();
                    walk(child);
                }
            };
            walk(menu);
            return rows.join("|");
        };
        auto key = [&](Qt::Key key, const QString &expected) {
            QTest::keyClick(popover, key);
            return waitFor([&] { return highlighted() == expected; });
        };
        if (!highlighted().isEmpty() || !key(Qt::Key_Down, "Turn tiling on") ||
            !key(Qt::Key_Up, "Appearance") || !key(Qt::Key_Up, "Show desktop") ||
            !key(Qt::Key_Home, "Turn tiling on") || !key(Qt::Key_End, "Appearance") ||
            !key(Qt::Key_Down, "Turn tiling on")) {
            std::cerr << "the arrows, Home and End did not move through the bar menu: "
                      << highlighted().toStdString() << '\n';
            return 1;
        }
        if (!key(Qt::Key_End, "Appearance") || !key(Qt::Key_Right, "Appearance|dark") ||
            !key(Qt::Key_Down, "Appearance|light") || !key(Qt::Key_Down, "Appearance|dark") ||
            !key(Qt::Key_Left, "Appearance") || menuItem("dark")) {
            std::cerr << "Right and Left did not open and close the submenu: "
                      << highlighted().toStdString() << '\n';
            return 1;
        }
        if (!key(Qt::Key_Return, "Appearance|dark") || !key(Qt::Key_Escape, "Appearance") ||
            !view.rootObject()->property("barMenuOpen").toBool()) {
            std::cerr << "Enter did not open the submenu, or Escape closed more than it: "
                      << highlighted().toStdString() << " "
                      << view.rootObject()->property("barMenuOpen").toBool() << '\n';
            return 1;
        }
        QTest::keyClick(popover, Qt::Key_Escape);
        if (!waitFor([&] { return !view.rootObject()->property("menuOpen").toBool(); })) {
            std::cerr << "Escape did not close the bar menu\n";
            return 1;
        }
    }
    // The pointer resting on a submenu's entry opens it, and resting on another closes it; the
    // pointer crossing other entries on its way into the submenu leaves it open.
    QTest::mouseClick(&view, Qt::RightButton, Qt::NoModifier, empty);
    if (!waitFor([&] { return menuShown() && menuItem("Appearance") && !menuItem("dark"); }))
        return fail("the bar menu did not open again");
    QTest::mouseMove(popover, centre(menuItem("Appearance")));
    if (!waitFor([&] { return menuItem("dark") && menuItem("dark")->isVisible(); })) {
        std::cerr << "resting on the appearance entry did not open its submenu\n";
        return 1;
    }
    QTest::mouseMove(popover, centre(menuItem("Show desktop")));
    QTest::mouseMove(popover, centre(menuItem("light")));
    QTest::qWait(400);
    if (!menuItem("light") || !menuItem("Appearance")->property("expanded").toBool()) {
        std::cerr << "crossing another entry into the submenu closed it\n";
        return 1;
    }
    QTest::mouseMove(popover, centre(menuItem("Show desktop")));
    if (!waitFor([&] { return !menuItem("light"); })) {
        std::cerr << "resting on another entry did not close the submenu\n";
        return 1;
    }
    // Heading for the open submenu across another entry leaves it open, the entry crossed taking
    // over only once the pointer rests on it.
    QTest::mouseMove(popover, centre(menuItem("Appearance")));
    if (!waitFor([&] { return menuItem("dark") && menuItem("dark")->isVisible(); }))
        return fail("resting on the appearance entry again did not open its submenu");
    {
        auto *crossed = menuItem("Show desktop");
        const QRectF across = crossed->mapRectToScene(QRectF(0, 0, crossed->width(), crossed->height()));
        const QPoint from = centre(menuItem("Appearance"));
        const QPoint to(qRound(across.right()) - 8, qRound(across.center().y()));
        for (int step = 1; step <= 6; ++step) {
            QTest::mouseMove(popover, from + (to - from) * step / 6);
            QTest::qWait(10);
        }
        if (crossed->property("highlighted").toBool() || !menuItem("dark") ||
            !menuItem("Appearance")->property("highlighted").toBool())
            return fail("heading for the open submenu across another entry took it over");
        if (!waitFor([&] { return crossed->property("highlighted").toBool() && !menuItem("dark"); }))
            return fail("resting on the entry crossed did not take over from the submenu");
    }
    click(menuItem("Show desktop"));
    // The profile button on the bar lists the profiles, the one in use marked, and switches.
    auto *profilesButton = find(view.rootObject(), "profilesButton");
    if (!waitFor([&] { return !view.rootObject()->property("menuOpen").toBool(); }) ||
        !profilesButton || !profilesButton->isVisible()) {
        std::cerr << "the bar lacks the profile button\n";
        return 1;
    }
    click(profilesButton);
    std::function<QQuickItem *(QQuickItem *, const QString &)> findProfile =
        [&](QQuickItem *parent, const QString &name) -> QQuickItem * {
        for (auto *item : parent->childItems()) {
            if (item->objectName() == "profileItem" && item->property("text") == name)
                return item;
            if (auto *found = findProfile(item, name))
                return found;
        }
        return nullptr;
    };
    QQuickItem *profileList = nullptr;
    if (!waitFor([&] {
            profileList = find(view.rootObject(), "profileList");
            return profileList && profileList->isVisible() && findProfile(profileList, "dark") &&
                   findProfile(profileList, "light") &&
                   findProfile(profileList, "light")->property("marked").toBool() &&
                   !findProfile(profileList, "dark")->property("marked").toBool() &&
                   inPopover(profileList);
        })) {
        std::cerr << "the profile button did not list the profiles\n";
        return 1;
    }
    click(findProfile(profileList, "dark"));
    if (!waitFor([&] {
            return requests == QStringList{"profile dark"} && controller.profile() == "dark" &&
                   !profileList->isVisible();
        })) {
        std::cerr << "picking a profile from the bar did not switch to it\n";
        return 1;
    }
    requests.clear();
    // The wallpaper button shows the pictures of shell.wallpapers as thumbnails, the one in use
    // marked; clicking another shows it, keeps it for the profile and leaves the picker open.
    auto *wallpapersButton = find(view.rootObject(), "wallpapersButton");
    if (!wallpapersButton || !wallpapersButton->isVisible()) {
        std::cerr << "the bar lacks the wallpaper button\n";
        return 1;
    }
    click(wallpapersButton);
    std::function<void(QQuickItem *, QList<QQuickItem *> &)> wallpaperItems =
        [&](QQuickItem *parent, QList<QQuickItem *> &found) {
            for (auto *item : parent->childItems()) {
                if (item->objectName() == "wallpaperItem" && item->isVisible())
                    found << item;
                wallpaperItems(item, found);
            }
        };
    auto wallpaperItem = [&](const QString &name) -> QQuickItem * {
        QList<QQuickItem *> found;
        if (auto *picker = find(view.rootObject(), "wallpaperPicker"))
            wallpaperItems(picker, found);
        for (auto *item : found)
            if (item->property("modelData").toMap().value("name") == name)
                return item;
        return nullptr;
    };
    QQuickItem *picker = nullptr;
    if (!waitFor([&] {
            picker = find(view.rootObject(), "wallpaperPicker");
            return picker && picker->isVisible() && wallpaperItem("one") && wallpaperItem("two") &&
                   wallpaperItem("one")->property("current").toBool() &&
                   !wallpaperItem("two")->property("current").toBool() && inPopover(picker);
        })) {
        std::cerr << "the wallpaper button did not show the pictures: " << (picker ? picker->isVisible() : -1)
                  << " " << controller.wallpapers().size() << " " << !!wallpaperItem("one") << !!wallpaperItem("two")
                  << " " << (picker ? picker->height() : 0) << " " << popover->height() << '\n';
        return 1;
    }
    click(wallpaperItem("two"));
    const auto picked = screens.filePath("state/paw/wallpapers");
    auto pickedText = [&picked] {
        QFile f(picked);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    };
    if (!waitFor([&] {
            return controller.wallpaperFile() == walls + "/b/two.png" &&
                   controller.wallpaper() == QUrl::fromLocalFile(walls + "/b/two.png") &&
                   wallpaperItem("two")->property("current").toBool() && picker->isVisible() &&
                   pickedText() == "dark\twalls/a/one.png\t" + walls + "/b/two.png\n";
        })) {
        std::cerr << "picking a wallpaper did not show and keep it: " << pickedText().toStdString() << '\n';
        return 1;
    }
    if (!waitFor([&] {
            return QDir(screens.filePath("cache/thumbnails/large")).entryList(QDir::Files).size() == 2;
        })) {
        std::cerr << "the wallpaper thumbnails were not cached\n";
        return 1;
    }
    controller.pickWallpaper("");
    if (controller.wallpaperFile() != directory.filePath("walls/a/one.png") || pickedText() != "") {
        std::cerr << "clearing the picked wallpaper did not go back to the configured one\n";
        return 1;
    }
    click(wallpapersButton);
    if (!waitFor([&] { return !picker->isVisible(); })) {
        std::cerr << "the wallpaper button did not close the picker\n";
        return 1;
    }
    // A task asking for attention is marked, and unmarked when it stops.
    if (task->property("shownUrgent").toBool() || find(task, "taskUrgent")->isVisible()) {
        std::cerr << "a task is marked urgent before it asked\n";
        return 1;
    }
    editTasks("model.setProperty(0, 'urgent', true)");
    if (!waitFor([&] {
            return task->property("shownUrgent").toBool() && find(task, "taskUrgent")->isVisible();
        })) {
        std::cerr << "a task asking for attention is not marked\n";
        return 1;
    }
    editTasks("model.setProperty(0, 'urgent', false)");
    if (!waitFor([&] {
            return !task->property("shownUrgent").toBool() && !find(task, "taskUrgent")->isVisible();
        })) {
        std::cerr << "a task that stopped asking for attention is still marked\n";
        return 1;
    }
    // Dragging a task along the bar moves it, not the whole list, as far as it is dragged.
    // Each belongs to a different application, or they would share a button.
    editTasks("model.append({ taskId: 8, title: 'Second', appId: 'second', active: false, "
              "minimized: false, urgent: false })");
    editTasks("model.append({ taskId: 9, title: 'Third', appId: 'third', active: false, "
              "minimized: false, urgent: false })");
    QQuickItem *third = listedTask(2);
    if (!third)
        return fail("the third task is not listed");
    auto taskIdAt = [&](int index) {
        QJSValue row;
        QMetaObject::invokeMethod(fakeModel, "get", Q_RETURN_ARG(QJSValue, row),
                                  Q_ARG(int, index));
        return row.property("taskId").toInt();
    };
    const QPoint from = centre(task), to = centre(third) + QPoint(third->width() / 4, 0);
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, from);
    for (int step = 1; step <= 10; ++step) {
        QTest::mouseMove(&view, from + (to - from) * step / 10);
        QTest::qWait(10);
    }
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, to);
    auto shownTaskId = [&](int index) {
        auto *button = listedTask(index);
        return button ? button->property("taskId").toInt() : -1;
    };
    if (!waitFor([&] { return shownTaskId(2) == 7; }) || shownTaskId(0) != 8 ||
        tasks->property("contentX").toReal() != 0) {
        std::cerr << "dragging a task did not reorder the task list\n";
        return 1;
    }
    // An application's windows share one stacked button showing how many there are.
    editTasks("model.append({ taskId: 10, title: 'Group one', appId: 'grouped', active: false, "
              "minimized: false, urgent: false })");
    editTasks("model.append({ taskId: 11, title: 'Group two', appId: 'grouped', active: true, "
              "minimized: false, urgent: false })");
    QQuickItem *stack = nullptr;
    if (!waitFor([&] {
            stack = listedTask(3);
            auto *stackCount = stack ? find(stack, "taskCount") : nullptr;
            return windowButtons() == 4 && stackCount &&
                   stack->property("stacked").toBool() && stack->property("shownActive").toBool() &&
                   stackCount->isVisible();
        })) {
        std::cerr << "an application's windows did not share one stacked task button\n";
        return 1;
    }
    // With shell.thumbnails, on by default, resting on any window's button shows pictures of its
    // windows on a card above it after shell.thumbnails.delay, instead of its tooltip or a
    // stack's list, a tile for each window. Each tile asks the task source for its window's
    // picture while it is there.
    {
        std::function<void(QQuickItem *, const QString &, QList<QQuickItem *> &)> collect =
            [&](QQuickItem *item, const QString &name, QList<QQuickItem *> &found) {
                for (auto *child : item->childItems()) {
                    if (child->objectName() == name && child->isVisible())
                        found << child;
                    collect(child, name, found);
                }
            };
        // The visible items called `name` under `item`, in the order of the item tree.
        auto named = [&](QQuickItem *item, const QString &name) {
            QList<QQuickItem *> found;
            if (item)
                collect(item, name, found);
            return found;
        };
        auto *card = find(view.rootObject(), "windowThumbnails");
        auto *list = find(view.rootObject(), "groupList");
        auto *root = view.rootObject();
        if (!card || !list)
            return fail("the card of window pictures or the stack's list was not made ahead of use");
        auto tiles = [&] { return named(card, "windowThumbnail"); };
        auto titles = [&] {
            QStringList shown;
            for (auto *tile : tiles())
                shown << tile->property("title").toString();
            return shown.join("|");
        };
        auto tileFor = [&](int id) -> QQuickItem * {
            for (auto *tile : tiles())
                if (tile->property("taskId").toInt() == id)
                    return tile;
            return nullptr;
        };
        auto buttonFor = [&](int id) -> QQuickItem * {
            for (int i = 0; i < windowButtons(); ++i)
                if (auto *button = listedTask(i); button && button->property("taskId").toInt() == id)
                    return button;
            return nullptr;
        };
        auto rowOf = [&](int id) {
            for (int row = 0; row < fakeModel->property("count").toInt(); ++row)
                if (taskIdAt(row) == id)
                    return row;
            return -1;
        };
        // Every request for pictures since the section began.
        QStringList pictures;
        auto pictureRequests = [&] {
            const auto asked = fakeModel->property("pictures").value<QJSValue>().toVariant().toStringList();
            fakeModel->setProperty("pictures", QVariant::fromValue(view.engine()->newArray()));
            pictures += asked;
            return asked;
        };
        // Whether the tooltip of a button is shown or waiting to be.
        auto tooltip = [](QQuickItem *button) {
            for (auto *child : button->children())
                if (child->inherits("QQuickToolTip") && child->property("visible").toBool())
                    return true;
            return false;
        };
        // The popover takes the pointer over a popup and between it and the bar alone.
        auto hoverRegion = [&](QQuickItem *popup) {
            QRectF area = popup->mapRectToScene(QRectF(0, 0, popup->width(), popup->height()));
            area.setBottom(popover->height() - view.height());
            return QRegion(area.toAlignedRect());
        };
        const QPoint barSpace = stack->mapToScene(QPointF(stack->width() + 40, stack->height() / 2)).toPoint();
        QTest::mouseMove(&view, barSpace);
        if (!waitFor([&] { return !popover->isVisible(); }))
            return fail("something was open in the popover before a button was hovered");
        pictureRequests();
        pictures.clear();
        auto *single = buttonFor(7);
        if (!single || single->property("stacked").toBool())
            return fail("the window's own button is not on the bar");
        // The pointer crossing a button on its way elsewhere asks for no pictures. Resting there
        // for half the delay asks for its windows', ahead of the card; leaving before the card
        // opens lets them go.
        QTest::mouseMove(&view, centre(single));
        QTest::mouseMove(&view, barSpace);
        QTest::qWait(300);
        if (!pictureRequests().isEmpty() || root->property("groupOpen").toBool())
            return fail("the pointer crossing a window's button asked for its picture");
        QElapsedTimer resting;
        resting.start();
        QTest::mouseMove(&view, centre(single));
        if (!waitFor([&] { return !pictureRequests().isEmpty(); }) ||
            pictures != QStringList{"watch 7 240 true"} || resting.elapsed() < 150 ||
            root->property("groupOpen").toBool()) {
            std::cerr << "halfway into the delay, a window's picture was not asked for ahead of its card: "
                      << pictures.join("|").toStdString() << " after " << resting.elapsed() << " ms\n";
            return 1;
        }
        QTest::mouseMove(&view, barSpace);
        if (!waitFor([&] { return !pictureRequests().isEmpty(); }) ||
            pictures != QStringList{"watch 7 240 true", "unwatch 7"} || root->property("groupOpen").toBool())
            return fail("leaving a window's button before its card opened did not let its picture go");
        QTest::qWait(300);
        if (root->property("groupOpen").toBool() || !pictureRequests().isEmpty())
            return fail("the card of a window's button the pointer left opened all the same");
        pictures.clear();
        // Resting there on: a picture that comes ahead of the card shows on it from the start,
        // without the icon standing in for it first.
        resting.start();
        QTest::mouseMove(&view, centre(single));
        if (card->isVisible() || root->property("groupOpen").toBool())
            return fail("the card of window pictures opened as soon as the pointer came");
        if (!waitFor([&] { return !pictureRequests().isEmpty(); }) || card->isVisible())
            return fail("a window's picture was not asked for ahead of its card");
        editTasks(QString("model.setProperty(%1, 'picture', 'image://test-windows/320x200')").arg(rowOf(7)));
        if (!waitFor([&] { return card->isVisible() && tileFor(7); }) ||
            !find(tileFor(7), "windowThumbnailPicture")->isVisible() ||
            find(tileFor(7), "windowThumbnailPicture")->opacity() != 1 ||
            find(tileFor(7), "windowThumbnailStandIn")->isVisible())
            return fail("a window's picture that came ahead of its card did not show as the card opened");
        // The next one takes its place at once.
        editTasks(QString("model.setProperty(%1, 'picture', 'image://test-windows/200x320')").arg(rowOf(7)));
        if (find(tileFor(7), "windowThumbnailPicture")->opacity() != 1 ||
            find(tileFor(7), "windowThumbnailStandIn")->isVisible())
            return fail("the next picture of a window shown as its card opened did not take its place at once");
        if (!waitFor([&] {
                return inPopover(card) && titles() == "Fake" && !popover->keyboard() &&
                       !root->property("menuOpen").toBool();
            }) ||
            resting.elapsed() < 350) {
            std::cerr << "resting on a window's button did not show its picture after the delay: "
                      << titles().toStdString() << " after " << resting.elapsed() << " ms\n";
            return 1;
        }
        if (tooltip(single))
            return fail("a window's button shows its tooltip beside the card of its picture");
        // Centred over the button, or kept inside the output by the card's margin.
        {
            const QRectF area = card->mapRectToScene(QRectF(0, 0, card->width(), card->height()));
            if (qAbs(area.center().x() - centre(single).x()) > 1 && area.left() != 8)
                return fail("the card of window pictures is not over its button");
        }
        if (!waitFor([&] { return popover->inputRegion() == hoverRegion(card); })) {
            std::cerr << "the popover takes the pointer elsewhere than over the card: "
                      << QDebug::toString(popover->inputRegion()).toStdString() << '\n';
            return 1;
        }
        // Asked for once, at the card's width and to follow the window, and taken over by the
        // card rather than let go and asked for again.
        if (!pictureRequests().isEmpty() || pictures != QStringList{"watch 7 240 true"}) {
            std::cerr << "the card did not take over the window's picture asked for ahead of it: "
                      << pictures.join("|").toStdString() << '\n';
            return 1;
        }
        editTasks(QString("model.setProperty(%1, 'picture', '')").arg(rowOf(7)));
        // Moving onto another button with windows shows its windows at once: a stack's, in its
        // list's order, the focused one marked.
        QTest::mouseMove(&view, centre(stack));
        if (root->property("groupWindowApp").toString() != "grouped" ||
            root->property("groupTask").toInt() != -1 || !card->isVisible())
            return fail("moving onto a stacked button did not show its windows at once");
        if (!waitFor([&] { return titles() == "Group one|Group two" && inPopover(card); }) ||
            !find(tileFor(11), "windowThumbnailLine")->isVisible() ||
            find(tileFor(10), "windowThumbnailLine")->isVisible()) {
            std::cerr << "a stack's card does not show a tile for each window, the focused one marked: "
                      << titles().toStdString() << '\n';
            return 1;
        }
        {
            auto asked = pictureRequests();
            asked.sort();
            if (asked != QStringList{"unwatch 7", "watch 10 240 true", "watch 11 240 true"})
                return fail("switching cards did not trade one window's picture for the others'");
        }
        // Until a picture comes, the application's icon stands in for it; then the picture
        // shows, fitted into its box keeping its proportions.
        auto shows = [&](int id) {
            auto *picture = find(tileFor(id), "windowThumbnailPicture");
            return picture && picture->isVisible() && !find(tileFor(id), "windowThumbnailStandIn")->isVisible();
        };
        if (shows(10) || shows(11))
            return fail("a window without a picture shows one");
        editTasks(QString("model.setProperty(%1, 'picture', 'image://test-windows/320x100')").arg(rowOf(10)));
        if (!waitFor([&] { return shows(10); }) || shows(11) ||
            find(tileFor(10), "windowThumbnailPicture")->size() != QSizeF(240, 75)) {
            std::cerr << "a window's picture does not show in its tile, fitted into it\n";
            return 1;
        }
        // A new picture takes the old one's place, and without one the icon is back.
        editTasks(QString("model.setProperty(%1, 'picture', 'image://test-windows/100x200')").arg(rowOf(10)));
        if (!waitFor([&] {
                return shows(10) && find(tileFor(10), "windowThumbnailPicture")->size() == QSizeF(75, 150);
            }))
            return fail("a window's new picture did not take the place of the old one");
        editTasks(QString("model.setProperty(%1, 'picture', '')").arg(rowOf(10)));
        if (!waitFor([&] { return !shows(10) && find(tileFor(10), "windowThumbnailStandIn")->isVisible(); }))
            return fail("a window whose picture went shows no icon in its place");
        editTasks(QString("model.setProperty(%1, 'picture', 'image://test-windows/320x100')").arg(rowOf(10)));
        if (!waitFor([&] { return shows(10); }))
            return fail("a window's picture did not come back");
        // A window whose process plays sound has a speaker before the cross, which mutes that
        // process's streams and stays, crossed out, while they are muted. A stream belongs to the
        // nearest process up from its own with a window: here the second window's process
        // started the first's, which started the player.
        {
            const QVariant audioBefore = root->property("audioSource");
            root->setProperty("audioSource", QVariant::fromValue<QObject *>(&audio));
            editTasks(QString("model.setProperty(%1, 'pid', 4010)").arg(rowOf(10)));
            editTasks(QString("model.setProperty(%1, 'pid', 4011)").arg(rowOf(11)));
            auto speaker = [&](int id) { return find(tileFor(id), "windowThumbnailSound"); };
            auto speaking = [&](int id) {
                auto *button = speaker(id);
                return button && button->isVisible() && button->opacity() == 1 && button->isEnabled();
            };
            auto silent = [&](int id) { return speaker(id) && !speaker(id)->isVisible(); };
            // What a screen reader calls it: its Accessible.name, the attached object's.
            auto spoken = [](QQuickItem *item) {
                for (auto *child : item->children())
                    if (child->inherits("QQuickAccessibleAttached"))
                        return child->property("name").toString();
                return QString();
            };
            auto *title = find(tileFor(10), "windowThumbnailTitle");
            auto *cross = find(tileFor(10), "windowThumbnailClose");
            if (!title || !cross || !silent(10) || !silent(11))
                return fail("a window that plays nothing has a speaker on its picture");
            auto x = [](QQuickItem *item) { return item->mapToScene(QPointF(0, 0)).x(); };
            const qreal titleX = x(title), titleWidth = title->width(), crossX = x(cross);
            auto player = [](bool muted, bool corked, QList<int> processes) {
                return AudioStreams::Stream{51, "Player", "audio-x-generic", 80, muted, corked, processes};
            };
            const AudioStreams::Stream paused{52, "Paused", "audio-x-generic", 80, false, true, {4011}};
            audio.update({"speakers", {{"speakers", "Speakers", 50, false}},
                          {player(false, false, {4110, 4010, 4011}), paused}});
            if (!waitFor([&] { return speaking(10); }) || !silent(11) ||
                speaker(10)->property("muted").toBool() ||
                spoken(speaker(10)) != "Mute Group one") {
                std::cerr << "the window playing sound has no speaker to mute it, or the other has one\n";
                return 1;
            }
            // Its room comes from the title's end, once laid out: the title starts where it did,
            // the cross keeps its place, and the cross stays while the pointer is on the speaker.
            if (!waitFor([&] { return title->width() < titleWidth; }) || x(title) != titleX ||
                x(cross) != crossX)
                return fail("the speaker moved the title or the cross as it came");
            QTest::mouseMove(popover, centre(speaker(10)));
            if (!waitFor([&] { return cross->isEnabled() && cross->opacity() == 1; }))
                return fail("the cross went while the pointer was on the speaker beside it");
            // A click mutes the window's sound alone, and neither focuses it nor closes the card.
            audio.requests.clear();
            taskRequests();
            click(speaker(10));
            if (!waitFor([&] { return speaker(10)->property("muted").toBool(); }) ||
                audio.requests != QStringList{"stream-mute 51 1"} || !speaking(10) ||
                spoken(speaker(10)) != "Unmute Group one") {
                std::cerr << "clicking the speaker did not mute the window's sound: "
                          << audio.requests.join(", ").toStdString() << '\n';
                return 1;
            }
            if (!root->property("groupOpen").toBool() || !taskRequests().isEmpty())
                return fail("clicking a window's speaker also clicked its picture");
            // Muted, paused or not, it stays to be unmuted; unmuted while paused, it goes.
            audio.update({"speakers", {{"speakers", "Speakers", 50, false}},
                          {player(true, true, {4110, 4010, 4011}), paused}});
            if (!speaking(10) || !speaker(10)->property("muted").toBool())
                return fail("a muted window's speaker went as its sound paused");
            audio.requests.clear();
            click(speaker(10));
            if (!waitFor([&] { return silent(10) && title->width() == titleWidth; }) ||
                audio.requests != QStringList{"stream-mute 51 0"}) {
                std::cerr << "unmuting a paused window did not take its speaker away: "
                          << audio.requests.join(", ").toStdString() << '\n';
                return 1;
            }
            // Windows of one process share its sound, and both say so.
            editTasks(QString("model.setProperty(%1, 'pid', 4010)").arg(rowOf(11)));
            audio.update({"speakers", {{"speakers", "Speakers", 50, false}},
                          {player(false, false, {4110, 4010})}});
            if (!waitFor([&] { return speaking(10) && speaking(11); }))
                return fail("two windows of the process playing sound do not both show it");
            // The sound ending takes the speakers away, the title getting its room back.
            audio.update({"speakers", {{"speakers", "Speakers", 50, false}}, {}});
            if (!waitFor([&] { return silent(10) && silent(11) && title->width() == titleWidth; }))
                return fail("the speakers stayed once the sound had ended");
            root->setProperty("audioSource", audioBefore);
            audio.requests.clear();
        }
        // The pointer can cross from the button to the card without it closing, and a click on
        // a tile focuses its window and closes the card.
        const QPoint into = centre(tileFor(10));
        QEvent leaveBar(QEvent::Leave);
        QCoreApplication::sendEvent(&view, &leaveBar);
        for (int step = 1; step <= 5; ++step) {
            QTest::mouseMove(popover, into + QPoint(0, 30 * (5 - step) / 5));
            QTest::qWait(10);
        }
        QTest::qWait(600);
        if (!card->isVisible())
            return fail("moving from a button into its card closed it");
        taskRequests();
        QString asked;
        QTest::mouseClick(popover, Qt::LeftButton, Qt::NoModifier, into);
        if (!waitFor([&] {
                asked += taskRequests();
                return !card->isVisible() && !popover->isVisible() && asked == "activate 10";
            })) {
            std::cerr << "clicking a window's picture did not focus it and close the card: "
                      << asked.toStdString() << '\n';
            return 1;
        }
        // Shown again: a right click on a tile opens its window's menu instead.
        auto reopen = [&] {
            QEvent leavePopover(QEvent::Leave);
            QCoreApplication::sendEvent(popover, &leavePopover);
            QTest::mouseMove(&view, barSpace);
            QTest::mouseMove(&view, centre(stack));
            return waitFor([&] { return inPopover(card) && tiles().size() == 2; });
        };
        if (!reopen())
            return fail("hovering a stacked button again did not show its windows' pictures");
        click(tileFor(11), Qt::RightButton);
        if (!waitFor([&] {
                return root->property("taskMenuId").toInt() == 11 && menuShown() && !card->isVisible();
            }))
            return fail("a right click on a window's picture did not open its menu in the card's place");
        QTest::keyClick(popover, Qt::Key_Escape);
        if (!waitFor([&] { return !popover->isVisible(); }) || !reopen())
            return fail("the stack's windows did not show again after its window's menu");
        // Resting on a window's picture peeks at the window after half a second; with one
        // peeked at, the next picture takes the peek over at once, without ending it in between;
        // leaving the pictures ends it a moment later, and so does a click on a picture, which
        // focuses its window and closes the card.
        {
            // The peeks the stand-in started and ended since the last call, joined by "|".
            auto peeks = [&] {
                const auto noted = fakeModel->property("peeks").value<QJSValue>().toVariant().toStringList();
                fakeModel->setProperty("peeks", QVariant::fromValue(view.engine()->newArray()));
                return noted.join("|");
            };
            // The middle of a window's picture, where its stand-in is when it has none.
            auto picture = [&](int id) { return centre(find(tileFor(id), "windowThumbnailStandIn")); };
            QString noted;
            auto heard = [&](const QString &expected) {
                return waitFor([&] {
                    noted += (noted.isEmpty() ? "" : "|") + peeks();
                    if (noted.endsWith('|'))
                        noted.chop(1);
                    return noted == expected;
                });
            };
            peeks();
            QEvent offBar(QEvent::Leave);
            QCoreApplication::sendEvent(&view, &offBar);
            QElapsedTimer resting;
            QTest::mouseMove(popover, picture(10));
            resting.start();
            if (!heard("peek 10") || resting.elapsed() < 450) {
                std::cerr << "resting on a window's picture did not peek at it after half a second: "
                          << noted.toStdString() << " after " << resting.elapsed() << " ms\n";
                return 1;
            }
            // Across the gap between the tiles to the next picture.
            noted.clear();
            QTest::mouseMove(popover, (picture(10) + picture(11)) / 2);
            QTest::qWait(50);
            QTest::mouseMove(popover, picture(11));
            resting.restart();
            if (!heard("peek 11") || resting.elapsed() > 300) {
                std::cerr << "the next picture did not take the peek over at once: "
                          << noted.toStdString() << " after " << resting.elapsed() << " ms\n";
                return 1;
            }
            QTest::qWait(300);
            if (!heard("peek 11"))
                return fail("the peek ended as it moved over to the next picture");
            // Off the pictures, onto the tile's title: the peek ends, the card stays.
            noted.clear();
            QTest::mouseMove(popover, centre(find(tileFor(11), "windowThumbnailTitle")));
            if (!heard("end 11") || !inPopover(card))
                return fail("leaving the pictures did not end the peek");
            // Resting again waits again, and a click focuses the window and ends the peek.
            noted.clear();
            QTest::mouseMove(popover, picture(10));
            resting.restart();
            if (!heard("peek 10") || resting.elapsed() < 450)
                return fail("a peek after one had ended did not wait for the pointer to rest");
            noted.clear();
            taskRequests();
            QString focused;
            QTest::mouseClick(popover, Qt::LeftButton, Qt::NoModifier, picture(10));
            if (!heard("end 10") || !waitFor([&] {
                    focused += taskRequests();
                    return focused == "activate 10" && !card->isVisible();
                })) {
                std::cerr << "clicking the picture peeked at did not focus its window and end the peek: "
                          << focused.toStdString() << ", " << noted.toStdString() << '\n';
                return 1;
            }
            if (!reopen())
                return fail("the stack's windows did not show again after a click on one");
        }
        // A middle click closes a window, and so does the cross on a tile under the pointer; a
        // window that closes leaves the card, and the last one closes it.
        asked.clear();
        taskRequests();
        QTest::mouseClick(popover, Qt::MiddleButton, Qt::NoModifier, centre(tileFor(11)));
        if (!waitFor([&] { asked += taskRequests(); return asked == "close 11"; }))
            return fail("a middle click on a window's picture did not close it");
        editTasks(QString("model.remove(%1)").arg(rowOf(11)));
        if (!waitFor([&] { return titles() == "Group one" && inPopover(card); }))
            return fail("the picture of a window that closed stayed on the card");
        auto *cross = find(tileFor(10), "windowThumbnailClose");
        QTest::mouseMove(popover, centre(tileFor(10)));
        if (!waitFor([&] { return cross->isEnabled() && cross->opacity() == 1; }))
            return fail("a window's picture under the pointer has no cross to close it");
        asked.clear();
        click(cross);
        if (!waitFor([&] { asked += taskRequests(); return asked == "close 10"; }))
            return fail("the cross on a window's picture did not close it");
        editTasks(QString("model.remove(%1)").arg(rowOf(10)));
        if (!waitFor([&] { return !card->isVisible() && !root->property("groupOpen").toBool(); }))
            return fail("the card stayed open once all its windows had closed");
        // Each window's picture was let go as often as it was asked for.
        pictureRequests();
        for (int id : {7, 10, 11})
            if (pictures.filter(QRegularExpression(QString("^watch %1 ").arg(id))).size() !=
                pictures.count(QString("unwatch %1").arg(id))) {
                std::cerr << "the windows' pictures were not let go as often as they were asked for: "
                          << pictures.join("|").toStdString() << '\n';
                return 1;
            }
        // Back as they were, for what follows.
        editTasks("model.append({ taskId: 10, title: 'Group one', appId: 'grouped', active: false, "
                  "minimized: false, urgent: false })");
        editTasks("model.append({ taskId: 11, title: 'Group two', appId: 'grouped', active: true, "
                  "minimized: false, urgent: false })");
        QEvent leavePopover(QEvent::Leave);
        QCoreApplication::sendEvent(popover, &leavePopover);
        if (!waitFor([&] {
                stack = listedTask(3);
                return stack && stack->property("stacked").toBool() && windowButtons() == 4;
            }))
            return fail("the stacked button did not come back");
        // Pressing a button closes its card, which stays closed while the pointer stays.
        QTest::mouseMove(&view, centre(stack));
        if (!waitFor([&] { return inPopover(card); }))
            return fail("hovering the stacked button did not show its windows' pictures");
        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, centre(stack));
        if (!waitFor([&] { return !root->property("groupOpen").toBool(); }))
            return fail("pressing a button did not close the card of its windows' pictures");
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, centre(stack));
        QTest::qWait(600);
        if (root->property("groupOpen").toBool())
            return fail("the card of a pressed button's windows came back while the pointer stayed");
        // Open, the card glides to another button's windows and eases to their width, rather than
        // jumping there; opening, it is in its place from the start. Slowed down, so that it is
        // seen on its way.
        {
            if (!slowMotion(true))
                return fail("the animations were not slowed down");
            QTest::mouseMove(&view, barSpace);
            if (!waitFor([&] {
                    single = buttonFor(7);
                    stack = listedTask(3);
                    return !popover->isVisible() && single && stack && stack->property("stacked").toBool();
                }))
                return fail("the buttons were not back on the bar in slow motion");
            QTest::mouseMove(&view, centre(single));
            if (!waitFor([&] { return inPopover(card) && titles() == "Fake"; }))
                return fail("resting on a window's button in slow motion did not show its picture");
            const QRectF from(card->x(), card->y(), card->width(), card->height());
            QTest::mouseMove(&view, centre(stack));
            const qreal width = card->property("placedWidth").toReal();
            if (card->x() != from.x() || card->width() != from.width() || width <= from.width()) {
                std::cerr << "the card did not start from where it was for another button's windows: "
                          << card->x() << "," << card->width() << " from " << from.x() << ","
                          << from.width() << " to " << width << " " << titles().toStdString() << '\n';
                return 1;
            }
            if (!waitFor([&] {
                    return card->width() > from.width() && card->width() < width && card->x() != from.x() &&
                           qAbs(card->y() + card->height() - from.bottom()) < 0.5;
                }))
                return fail("the card did not ease to another button's windows, its edge by the bar staying");
            // Its place and width ease together, and come to rest together.
            if (!waitFor([&] { return card->width() == width && titles() == "Group one|Group two"; }))
                return fail("the card did not come to rest over another button's windows");
            {
                const QRectF area = card->mapRectToScene(QRectF(0, 0, card->width(), card->height()));
                if (qAbs(area.center().x() - centre(stack).x()) > 1 && area.left() != 8)
                    return fail("the card did not glide to the stacked button");
            }
            // A window's first picture fades in over the icon standing in for it, and the next one
            // takes its place at once.
            {
                auto *picture = find(tileFor(11), "windowThumbnailPicture");
                auto *standIn = find(tileFor(11), "windowThumbnailStandIn");
                if (!picture || !standIn || picture->isVisible() || !standIn->isVisible())
                    return fail("a window without a picture has no icon standing in for one");
                editTasks(QString("model.setProperty(%1, 'picture', 'image://test-windows/320x200')").arg(rowOf(11)));
                if (!waitFor([&] {
                        return picture->isVisible() && picture->opacity() > 0 && picture->opacity() < 1 &&
                               standIn->isVisible() && standIn->opacity() > 0 && standIn->opacity() < 1;
                    }))
                    return fail("a window's first picture did not fade in over the icon standing in for it");
                if (!waitFor([&] { return picture->opacity() == 1 && !standIn->isVisible(); }))
                    return fail("a window's first picture did not finish fading in");
                editTasks(QString("model.setProperty(%1, 'picture', 'image://test-windows/200x320')").arg(rowOf(11)));
                if (picture->opacity() != 1 || standIn->isVisible() ||
                    !waitFor([&] { return picture->implicitWidth() == 200; }) || picture->opacity() != 1 ||
                    standIn->isVisible())
                    return fail("a window's next picture did not take the place of the last at once");
                editTasks(QString("model.setProperty(%1, 'picture', '')").arg(rowOf(11)));
            }
            QTest::mouseMove(&view, barSpace);
            if (!waitFor([&] { return !card->isVisible(); }))
                return fail("leaving the stacked button did not close its card in slow motion");
            QTest::mouseMove(&view, centre(single));
            if (!waitFor([&] { return card->isVisible(); }) || card->x() != from.x() ||
                card->width() != from.width())
                return fail("opening over another button than it last showed, the card glided from there");
            QTest::mouseMove(&view, barSpace);
            if (!waitFor([&] { return !popover->isVisible(); }) || !slowMotion(false))
                return fail("the card did not close, or the animations did not get their speed back");
        }
        // As many windows as fit across the output at 60 % of their pictures' size get narrower
        // pictures; with more, the stack lists them instead.
        for (int id = 12; id <= 15; ++id)
            editTasks(QString("model.append({ taskId: %1, title: 'More %1', appId: 'grouped', active: false, "
                              "minimized: false, urgent: false })").arg(id));
        QTest::mouseMove(&view, barSpace);
        QTest::mouseMove(&view, centre(stack));
        if (!waitFor([&] { return inPopover(card) && tiles().size() == 6; }) ||
            root->property("thumbnailWidth").toReal() >= 240 || root->property("thumbnailWidth").toReal() < 144) {
            std::cerr << "six windows' pictures did not fit across the output, narrower: "
                      << root->property("thumbnailWidth").toReal() << '\n';
            return 1;
        }
        editTasks("model.append({ taskId: 16, title: 'More 16', appId: 'grouped', active: false, "
                  "minimized: false, urgent: false })");
        if (!waitFor([&] {
                return inPopover(list) && named(list, "groupWindow").size() == 7 && !card->isVisible();
            }))
            return fail("seven windows, too many for their pictures, were not listed instead");
        for (int id = 12; id <= 16; ++id)
            editTasks(QString("model.remove(%1)").arg(rowOf(id)));
        QTest::mouseMove(&view, barSpace);
        if (!waitFor([&] { return !popover->isVisible(); }))
            return fail("leaving the stacked button did not close its windows' list");
        // The window switcher shows each window as a card with its picture, as Windows 11 does:
        // the application's icon and the window's title over the picture, the pictures at one
        // height and each as wide as its window's proportions, from 3:4 to 2:1, in rows that wrap
        // and are centred. A card finds its window's task by the number the switcher gives it
        // and asks for its picture as soon as the switcher lists it, before it shows, at twice
        // the pictures' height wide (240 * 5/8 * 2 pixels), live; until a picture has come, the
        // icon stands in. It lets the pictures go as the switcher closes.
        {
            // A stand-in model of the switcher's own, which notes what it is asked of pictures as
            // the taskbar's does, and whose windows are on no button of the bar.
            QQmlComponent switcherTasks(view.engine());
            switcherTasks.setData(R"(import QtQml.Models
ListModel {
    property var pictures: []
    function watchPicture(id, width, live) { pictures = pictures.concat(["watch " + id + " " + width + " " + live]) }
    function unwatchPicture(id) { pictures = pictures.concat(["unwatch " + id]) }
    ListElement { taskId: 31; title: 'Wide'; appId: 'wide'; windowId: 501; minimized: false
                  picture: 'image://test-windows/320x180' }
    ListElement { taskId: 32; title: 'Tall'; appId: 'tall'; windowId: 502; minimized: false
                  picture: 'image://test-windows/90x160' }
    ListElement { taskId: 33; title: 'Urgent'; appId: 'urgent'; windowId: 503; minimized: false
                  picture: '' }
    ListElement { taskId: 34; title: 'Minimized'; appId: 'min'; windowId: 504; minimized: true
                  picture: 'image://test-windows/160x100' }
})",
                                  QUrl());
            std::unique_ptr<QObject> switcherModel(switcherTasks.create());
            if (!switcherModel)
                return fail("the switcher's stand-in model did not load");
            QQmlEngine::setObjectOwnership(switcherModel.get(), QQmlEngine::CppOwnership);
            auto pictureRequests = [&] {
                const auto asked =
                    switcherModel->property("pictures").value<QJSValue>().toVariant().toStringList();
                switcherModel->setProperty("pictures", QVariant::fromValue(view.engine()->newArray()));
                return asked;
            };
            auto editTasks = [&](const QString &body) {
                view.engine()
                    ->evaluate("(function(model) { " + body + " })")
                    .call({view.engine()->newQObject(switcherModel.get())});
            };
            auto rowOf = [&](int id) {
                for (int row = 0; row < switcherModel->property("count").toInt(); ++row) {
                    QJSValue task;
                    QMetaObject::invokeMethod(switcherModel.get(), "get", Q_RETURN_ARG(QJSValue, task),
                                              Q_ARG(int, row));
                    if (task.property("taskId").toInt() == id)
                        return row;
                }
                return -1;
            };
            auto *switcherRoot = switcherView.rootObject();
            switcherRoot->setProperty("taskSource", QVariant::fromValue(switcherModel.get()));
            auto line = [&](const QString &appId, const QString &title, bool minimized, bool urgent,
                            int id) {
                return QString("switcher-window %1\t%2\t%3\t1\t%4\t%5\t%6\n")
                    .arg(appId, title, output)
                    .arg(minimized)
                    .arg(urgent)
                    .arg(id);
            };
            const QString opening = "switcher " + output + " 1 5\n" +
                                    line("wide", "Wide", false, false, 501) +
                                    line("tall", "Tall", false, false, 502) +
                                    line("urgent", "Urgent", false, true, 503) +
                                    line("min", "Minimized", true, false, 504) +
                                    line("lost", "Lost", false, false, 505);
            auto cards = [&] { return named(switcherRoot, "switcherCard"); };
            // Another output's switcher is not this one's to show.
            subscriber->write(("switcher OTHER-1 0 1\n" + line("wide", "Wide", false, false, 501)).toUtf8());
            if (!waitFor([&] { return controller.switcherOutput() == "OTHER-1"; }) ||
                !pictureRequests().isEmpty() || !switcherRoot->property("windows").toList().isEmpty())
                return fail("the switcher of another output listed its windows here");
            subscriber->write(opening.toUtf8());
            if (!waitFor([&] { return controller.switcherWindows().size() == 5; }))
                return fail("the switcher's windows were not parsed");
            auto asked = pictureRequests();
            asked.sort();
            if (asked != QStringList{"watch 31 300 true", "watch 32 300 true", "watch 33 300 true",
                                     "watch 34 300 true"}) {
                std::cerr << "the switcher did not ask for its windows' pictures as it opened: "
                          << asked.join("|").toStdString() << '\n';
                return 1;
            }
            if (!waitFor([&] { return switcherView.isVisible() && cards().size() == 5; }))
                return fail("the switcher did not show its windows as cards");
            if (find(switcherRoot, "switcherGrid")->isVisible())
                return fail("the switcher showed its grid of icons beside the cards");
            auto pictureOf = [&](int index) { return find(cards()[index], "switcherCardPicture"); };
            // The icon standing in for the picture fills the picture's box, shown or not.
            auto boxOf = [&](int index) { return find(cards()[index], "switcherCardStandIn"); };
            auto standIn = [&](int index) { return boxOf(index)->isVisible(); };
            // The pictures come at one height; each card is as wide as its picture's proportions
            // make it, a tall one at 3:4, those without a picture yet at 16:10.
            const int widths[] = {267, 113, 240, 240, 240};
            if (!waitFor([&] {
                    for (int i = 0; i < 5; ++i)
                        if (boxOf(i)->width() != widths[i] || boxOf(i)->height() != 150 ||
                            cards()[i]->height() != cards()[0]->height() ||
                            cards()[i]->width() - boxOf(i)->width() != cards()[0]->width() - boxOf(0)->width())
                            return false;
                    return true;
                })) {
                for (int i = 0; i < int(cards().size()); ++i)
                    std::cerr << boxOf(i)->width() << "x" << boxOf(i)->height() << ' ';
                return fail("\nthe switcher's cards are not as wide as their windows' pictures");
            }
            for (int i : {0, 1, 3}) {
                auto *picture = pictureOf(i);
                const qreal aspect = picture->property("implicitWidth").toReal() /
                                     picture->property("implicitHeight").toReal();
                if (!picture->isVisible() || standIn(i) || std::abs(picture->height() - 150) > 0.5 ||
                    std::abs(picture->width() / picture->height() - aspect) > 0.02) {
                    std::cerr << "card " << i << "'s picture is " << picture->width() << "x"
                              << picture->height() << '\n';
                    return fail("a window's picture on its card is not 150 pixels tall in its proportions");
                }
            }
            if (!standIn(2) || !standIn(4) || pictureOf(2)->isVisible())
                return fail("the icon does not stand in for a picture that has not come");
            // Four cards across, and the fifth centred under them.
            const auto row = cards();
            if (row[1]->y() != row[0]->y() || row[3]->y() != row[0]->y() || row[4]->y() <= row[0]->y() ||
                std::abs(row[4]->x() + row[4]->width() / 2 - (row[3]->x() + row[3]->width()) / 2) > 1)
                return fail("the switcher's cards did not wrap into centred rows");
            // A window asking for attention is tinted with a dot, a minimized one faded with a
            // dash, as the grid of icons marks them.
            if (named(switcherRoot, "switcherUrgent") != QList<QQuickItem *>{find(row[2], "switcherUrgent")} ||
                named(switcherRoot, "switcherMinimized") != QList<QQuickItem *>{find(row[3], "switcherMinimized")} ||
                !waitFor([&] { return pictureOf(3)->opacity() == 0.5 && pictureOf(0)->opacity() == 1; }))
                return fail("the switcher's cards do not mark urgent and minimized windows");
            // The selection is the second card, and glides to the next.
            auto *selection = find(switcherRoot, "switcherSelection");
            auto marks = [&](int index) {
                auto *card = cards()[index];
                return selection->isVisible() && selection->x() == card->x() &&
                       selection->y() == card->y() && selection->width() == card->width() &&
                       selection->height() == card->height();
            };
            if (!waitFor([&] { return marks(1); }))
                return fail("the switcher's selection does not mark the selected card");
            subscriber->write("switcher-select 2\n");
            if (!waitFor([&] { return marks(2); }))
                return fail("the switcher's selection did not move to the next card");
            // A picture that comes later fades in over the icon and widens its card, and a window
            // whose task comes later, or learns the window's number later, gets its picture then.
            editTasks(QString("model.setProperty(%1, 'picture', 'image://test-windows/200x100')").arg(rowOf(33)));
            if (!waitFor([&] { return pictureOf(2)->isVisible() && pictureOf(2)->opacity() < 1; }) ||
                !waitFor([&] { return !standIn(2) && boxOf(2)->width() == 300 && marks(2); }) ||
                pictureOf(2)->opacity() != 1)
                return fail("a picture that came later did not fade in and widen its card");
            editTasks("model.append({taskId: 35, title: 'Lost', appId: 'lost', windowId: 0, "
                      "minimized: false, picture: 'image://test-windows/100x100'})");
            if (!pictureRequests().isEmpty() || !standIn(4))
                return fail("a task without the window's number was taken for the window");
            editTasks(QString("model.setProperty(%1, 'windowId', 505)").arg(rowOf(35)));
            if (!waitFor([&] { return !standIn(4) && boxOf(4)->width() == 150; }) ||
                pictureRequests() != QStringList{"watch 35 300 true"})
                return fail("a window's task that came later did not give its card the picture");
            // Clicking a card picks its window, as clicking a cell of the grid does.
            requests.clear();
            click(cards()[3]);
            if (!waitFor([&] { return requests == QStringList{"switcher_confirm 4"}; }))
                return fail("clicking a card of the switcher did not pick its window");
            requests.clear();
            // Closing lets each picture go at once, though the cards stay as the switcher fades.
            subscriber->write("switcher-close\n");
            if (!waitFor([&] { return controller.switcherWindows().isEmpty(); }))
                return fail("the switcher did not close");
            asked = pictureRequests();
            asked.sort();
            if (asked != QStringList{"unwatch 31", "unwatch 32", "unwatch 33", "unwatch 34", "unwatch 35"}) {
                std::cerr << "the switcher did not let its pictures go as it closed: "
                          << asked.join("|").toStdString() << '\n';
                return 1;
            }
            if (!waitFor([&] { return !switcherView.isVisible(); }))
                return fail("the switcher did not go");
            // Many windows shrink the pictures, down to 60 % of their height, and past that the
            // rows scroll to the selection.
            auto many = [&](int count, int selected) {
                QString text = QString("switcher %1 %2 %3\n").arg(output).arg(selected).arg(count);
                for (int i = 0; i < count; ++i)
                    text += line("many", QString("Many %1").arg(i), false, false, 700 + i);
                subscriber->write(text.toUtf8());
                return waitFor([&] {
                    return switcherView.isVisible() && cards().size() == count &&
                           controller.switcherWindows().size() == count;
                });
            };
            auto *scroller = find(switcherRoot, "switcherCardsView");
            auto pictureHeight = [&] { return boxOf(0)->height(); };
            if (!many(12, 0) || pictureHeight() >= 150 || pictureHeight() < 90 ||
                scroller->property("contentHeight").toReal() > scroller->height())
                return fail("twelve windows' cards did not shrink to fit the output");
            subscriber->write("switcher-close\n");
            if (!waitFor([&] { return !switcherView.isVisible(); }) || !many(40, 0) ||
                pictureHeight() != 90 || scroller->property("contentY").toReal() != 0 ||
                scroller->property("contentHeight").toReal() <= scroller->height())
                return fail("forty windows' cards did not shrink to 60 % and scroll");
            subscriber->write("switcher-select 39\n");
            if (!waitFor([&] {
                    auto *last = cards()[39];
                    const qreal top = scroller->property("contentY").toReal();
                    return marks(39) && last->y() >= top && last->y() + last->height() <= top + scroller->height();
                }))
                return fail("the switcher's rows did not scroll to the selected card");
            subscriber->write("switcher-close\n");
            if (!waitFor([&] { return !switcherView.isVisible(); }))
                return fail("the switcher did not go");
            // shell.thumbnails = { live = false }: one picture each.
            if (!rewrite(QString(lua).replace("shell={", "shell={thumbnails={live=false},")))
                return fail("could not rewrite the configuration");
            controller.reload();
            if (!waitFor([&] { return !controller.liveThumbnails(); }))
                return fail("shell.thumbnails = { live = false } was not read");
            pictureRequests();
            subscriber->write(opening.toUtf8());
            if (!waitFor([&] { return pictureRequests().contains("watch 31 300 false"); }))
                return fail("without live pictures, the switcher asked for live ones");
            subscriber->write("switcher-close\n");
            if (!waitFor([&] { return !switcherView.isVisible() && controller.switcherWindows().isEmpty(); }))
                return fail("the switcher did not go");
            if (!rewrite(lua))
                return fail("could not restore the configuration");
            controller.reload();
            if (!waitFor([&] { return controller.liveThumbnails(); }))
                return fail("the configuration was not restored");
            // The switcher goes back to the controller's task model, before this one goes.
            switcherRoot->setProperty("taskSource", QVariant::fromValue<QObject *>(controller.tasks()));
        }
        // shell.thumbnails = { live = false }: the picture asked for ahead of the card is the one
        // it shows, not followed, and not asked for again as the card opens.
        if (!rewrite(QString(lua).replace("shell={", "shell={thumbnails={live=false},")))
            return fail("could not rewrite the configuration");
        controller.reload();
        if (!waitFor([&] { return !controller.liveThumbnails(); }))
            return fail("shell.thumbnails = { live = false } was not read");
        pictureRequests();
        pictures.clear();
        single = buttonFor(7);
        QTest::mouseMove(&view, centre(single));
        if (!waitFor([&] { return inPopover(card) && titles() == "Fake"; }) ||
            (pictureRequests(), pictures != QStringList{"watch 7 240 false"})) {
            std::cerr << "without live pictures, the card did not show the one picture asked for ahead of it: "
                      << pictures.join("|").toStdString() << '\n';
            return 1;
        }
        QTest::mouseMove(&view, barSpace);
        if (!waitFor([&] { pictureRequests(); return !popover->isVisible() && pictures.size() == 2; }) ||
            pictures.last() != "unwatch 7")
            return fail("without live pictures, the window's picture was not let go as its card closed");
        // shell.thumbnails = { enabled = false }: a window's button has its tooltip and no card,
        // and a stack lists its windows as it did.
        if (!rewrite(QString(lua).replace("shell={", "shell={thumbnails={enabled=false},")))
            return fail("could not rewrite the configuration");
        controller.reload();
        if (!waitFor([&] { return !controller.thumbnails(); }))
            return fail("shell.thumbnails = { enabled = false } was not read");
        single = buttonFor(7);
        QTest::mouseMove(&view, centre(single));
        if (!waitFor([&] { return tooltip(single); }))
            return fail("without pictures, a window's button has no tooltip");
        QTest::qWait(600);
        if (card->isVisible() || root->property("groupOpen").toBool())
            return fail("without pictures, resting on a window's button opened a card");
        // The reload may have made the buttons anew.
        if (!waitFor([&] { return (stack = listedTask(3)) && stack->property("stacked").toBool(); }))
            return fail("the stacked button is not on the bar");
        // The tooltip's window goes before the pointer moves on, and the pointer's next move in
        // the bar, which comes with an enter event once the window it was last in has gone, is
        // over bare bar, so that hovering below starts afresh.
        QTest::mouseMove(&view, barSpace);
        if (!waitFor([&] {
                const auto windows = QGuiApplication::topLevelWindows();
                return std::none_of(windows.begin(), windows.end(), [&](QWindow *window) {
                    return window->isVisible() && window != &view && window != popover;
                });
            }))
            return fail("the tooltip of a window's button did not go once the pointer left it");
        QTest::mouseMove(&view, barSpace + QPoint(1, 0));
    }
    // Without pictures, hovering it lists both windows above the bar, without taking the
    // keyboard; leaving hides the list and shrinks the panel again.
    auto *groupList = find(view.rootObject(), "groupList");
    auto groupRows = [&] {
        int rows = 0;
        std::function<void(QQuickItem *)> count = [&](QQuickItem *item) {
            for (auto *child : item->childItems()) {
                rows += child->objectName() == "groupWindow";
                count(child);
            }
        };
        count(groupList);
        return rows;
    };
    QTest::mouseMove(&view, centre(stack));
    if (!groupList || !waitFor([&] {
            return inPopover(groupList) && groupRows() == 2 && !popover->keyboard() &&
                   !view.rootObject()->property("menuOpen").toBool();
        })) {
        std::cerr << "hovering a stacked task did not list its windows "
                  << stack->property("hovered").toBool()
                  << view.rootObject()->property("groupOpen").toBool() << groupRows()
                  << groupList->isVisible() << popover->isVisible() << "\n";
        return 1;
    }
    // The popover takes the pointer over the list alone, and between it and the bar.
    {
        // Once it has slid into place.
        auto box = [&] {
            QRectF area = groupList->mapRectToScene(QRectF(0, 0, groupList->width(), groupList->height()));
            area.setBottom(popover->height() - view.height());
            return area;
        };
        if (!waitFor([&] { return popover->inputRegion() == QRegion(box().toAlignedRect()); })) {
            std::cerr << "the popover takes the pointer elsewhere than over the list: "
                      << QDebug::toString(popover->inputRegion()).toStdString() << " for "
                      << QDebug::toString(box()).toStdString() << '\n';
            return 1;
        }
    }
    // The pointer can cross from the button to the list without it closing, and choosing a
    // window there closes it.
    std::function<QQuickItem *(QQuickItem *)> firstRow = [&](QQuickItem *item) -> QQuickItem * {
        for (auto *child : item->childItems()) {
            if (child->objectName() == "groupWindow")
                return child;
            if (auto *found = firstRow(child))
                return found;
        }
        return nullptr;
    };
    // The pointer leaves the bar's surface and comes into the popover's.
    const QPoint row = centre(firstRow(groupList));
    QEvent leaveBar(QEvent::Leave);
    QCoreApplication::sendEvent(&view, &leaveBar);
    for (int step = 1; step <= 5; ++step) {
        QTest::mouseMove(popover, row + QPoint(0, 30 * (5 - step) / 5));
        QTest::qWait(10);
    }
    QTest::qWait(600);
    if (!groupList->isVisible()) {
        std::cerr << "moving from a stacked task to its windows hid them\n";
        return 1;
    }
    QTest::mouseClick(popover, Qt::LeftButton, Qt::NoModifier, row);
    if (!waitFor([&] { return !groupList->isVisible() && !popover->isVisible(); })) {
        std::cerr << "choosing one of a stacked task's windows did not hide them\n";
        return 1;
    }
    // Hovered again, the list goes once the pointer leaves.
    QEvent leavePopover(QEvent::Leave);
    QCoreApplication::sendEvent(popover, &leavePopover);
    QTest::mouseMove(&view, centre(stack));
    if (!waitFor([&] { return groupList->isVisible(); })) {
        std::cerr << "hovering a stacked task again did not list its windows\n";
        return 1;
    }
    QTest::mouseMove(&view, empty);
    if (!waitFor([&] { return !groupList->isVisible() && !popover->isVisible(); })) {
        std::cerr << "leaving a stacked task did not hide its windows\n";
        return 1;
    }
    // Drags from an application, as Qt's Wayland platform hands them to the window under them:
    // coming into a window, an enter and at once a move, then a move each time they move, and a
    // leave as they go or end. A move the window takes is its answer to the application, taken
    // already when the last one was, as Qt has it; dragTo says whether the window took it.
    QMimeData dragged;
    dragged.setText("dragged text");
    const Qt::DropActions dragActions = Qt::CopyAction | Qt::MoveAction;
    QPointer<QWindow> dragWindow;
    Qt::DropAction dragTaken = Qt::IgnoreAction;
    auto dragGone = [&] {
        if (dragWindow) {
            QDragLeaveEvent leave;
            QCoreApplication::sendEvent(dragWindow, &leave);
        }
        dragWindow = nullptr;
    };
    auto dragTo = [&](QWindow *window, QPoint at) {
        if (window != dragWindow) {
            dragGone();
            dragWindow = window;
            QDragEnterEvent enter(at, dragActions, &dragged, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &enter);
            dragTaken = enter.isAccepted() ? enter.dropAction() : Qt::IgnoreAction;
        }
        QDragMoveEvent move(at, dragActions, &dragged, Qt::LeftButton, Qt::NoModifier);
        if (dragTaken != Qt::IgnoreAction) {
            move.setDropAction(dragTaken);
            move.accept();
        }
        QCoreApplication::sendEvent(window, &move);
        dragTaken = move.isAccepted() ? move.dropAction() : Qt::IgnoreAction;
        return move.isAccepted();
    };
    // Whether `holds` goes on holding for `ms`, for what must not happen.
    auto stays = [&](const std::function<bool()> &holds, int ms = 800) {
        QElapsedTimer waited;
        waited.start();
        while (waited.elapsed() < ms) {
            if (!holds())
                return false;
            QTest::qWait(20);
        }
        return holds();
    };
    // The visible item called `name` under `item` whose window is `id`.
    std::function<QQuickItem *(QQuickItem *, const QString &, int)> windowItem =
        [&](QQuickItem *item, const QString &name, int id) -> QQuickItem * {
        for (auto *child : item->childItems()) {
            if (child->objectName() == name && child->isVisible() && child->property("taskId").toInt() == id)
                return child;
            if (auto *found = windowItem(child, name, id))
                return found;
        }
        return nullptr;
    };
    // A drag resting on a stacked button lists its windows after half a second, the list staying
    // while the drag crosses over to it, and resting on one there brings that window forward;
    // gone from both, the list closes. Neither the bar nor the list takes the drag. (With
    // pictures, further down.)
    {
        QEvent leaveBar(QEvent::Leave);
        QCoreApplication::sendEvent(&view, &leaveBar);
        if (!waitFor([&] { return (stack = listedTask(3)) && stack->property("stacked").toBool(); }))
            return fail("the stacked button is not on the bar");
        taskRequests();
        QElapsedTimer resting;
        resting.start();
        if (dragTo(&view, centre(stack)))
            return fail("the bar took a drag");
        if (!waitFor([&] { return inPopover(groupList) && groupRows() == 2; }) || resting.elapsed() < 450)
            return fail("a drag resting on a stacked button did not list its windows after half a second");
        auto *row = windowItem(groupList, "groupWindow", 10);
        if (!row || dragTo(popover, centre(row)))
            return fail("the list of a stack's windows took a drag");
        resting.start();
        if (!waitFor([&] { return taskRequests() == "activate 10"; }) || resting.elapsed() < 450 ||
            !groupList->isVisible())
            return fail("a drag resting on a window in a stack's list did not bring it forward");
        dragGone();
        if (!waitFor([&] { return !popover->isVisible(); }))
            return fail("the list a drag opened stayed once the drag had gone");
    }
    if (!rewrite(lua))
        return fail("could not restore the configuration");
    controller.reload();
    if (!waitFor([&] { return controller.thumbnails(); }))
        return fail("the taskbar's window pictures did not come back on");
    // With pictures, a drag resting on a window's button brings it forward after half a second,
    // the button lit meanwhile, where the pointer would show its picture; one only crossing it
    // does nothing, and one resting on the focused window's button leaves it, which activating
    // would minimize. A stack's card of pictures opens, and resting on a picture there brings its
    // window forward. The card stays while the drag is over it or its button.
    {
        auto *root = view.rootObject();
        auto *card = find(root, "windowThumbnails");
        auto rowOf = [&](int id) {
            for (int row = 0; row < fakeModel->property("count").toInt(); ++row)
                if (taskIdAt(row) == id)
                    return row;
            return -1;
        };
        QQuickItem *single = nullptr;
        if (!card || !waitFor([&] {
                stack = listedTask(3);
                for (int i = 0; i < windowButtons(); ++i)
                    if (auto *button = listedTask(i); button && button->property("taskId").toInt() == 7)
                        single = button;
                return single && stack && stack->property("stacked").toBool();
            }))
            return fail("the window's button and the stacked one are not on the bar");
        const QPoint bare = stack->mapToScene(QPointF(stack->width() + 40, stack->height() / 2)).toPoint();
        taskRequests();
        dragTo(&view, centre(single));
        dragTo(&view, bare);
        if (!stays([&] { return taskRequests().isEmpty() && !root->property("groupOpen").toBool(); }))
            return fail("a drag crossing a window's button brought it forward");
        QElapsedTimer resting;
        resting.start();
        if (dragTo(&view, centre(single)) || !single->property("dragOver").toBool())
            return fail("a window's button took a drag, or is not lit under it");
        if (!waitFor([&] { return taskRequests() == "activate 7"; }) || resting.elapsed() < 450 ||
            root->property("groupOpen").toBool())
            return fail("a drag resting on a window's button did not bring it forward after half a second");
        editTasks(QString("model.setProperty(%1, 'active', true)").arg(rowOf(7)));
        dragTo(&view, bare);
        if (single->property("dragOver").toBool())
            return fail("a window's button stayed lit once the drag left it");
        dragTo(&view, centre(single));
        if (!stays([&] { return taskRequests().isEmpty(); }))
            return fail("a drag resting on the focused window's button activated it again");
        editTasks(QString("model.setProperty(%1, 'active', false)").arg(rowOf(7)));
        dragTo(&view, centre(stack));
        if (!waitFor([&] {
                return inPopover(card) && windowItem(card, "windowThumbnail", 10) &&
                       windowItem(card, "windowThumbnail", 11);
            }))
            return fail("a drag resting on a stacked button did not show its windows' pictures");
        if (dragTo(popover, centre(windowItem(card, "windowThumbnail", 10))))
            return fail("the card of window pictures took a drag");
        if (!waitFor([&] { return taskRequests() == "activate 10"; }) || !card->isVisible())
            return fail("a drag resting on a window's picture did not bring it forward");
        dragTo(popover, centre(windowItem(card, "windowThumbnail", 11)));
        if (!stays([&] { return taskRequests().isEmpty() && card->isVisible(); }))
            return fail("a drag resting on the focused window's picture activated it again, or the card closed");
        dragTo(&view, centre(stack));
        if (!stays([&] { return card->isVisible() && taskRequests().isEmpty(); }, 500))
            return fail("the card closed with the drag back on its button");
        dragGone();
        if (!waitFor([&] { return !popover->isVisible(); }) || !taskRequests().isEmpty())
            return fail("the card a drag opened stayed once the drag had gone");
    }
    // The keyboard on the bar, which the taskbar_focus action asks for with "taskbar OUTPUT": it
    // starts at the focused window's button, its windows shown at once; the arrows move between
    // the buttons, the card gliding along, and into the card, where the picture selected peeks at
    // its window after a moment; Enter does what a click does, the window that had the keyboard
    // counting as the focused one; Delete closes a window; the Menu key and Shift+F10 open menus
    // whose closing comes back to it; Escape and the action again give the keyboard back; the
    // pointer moving over the bar, or a press, hands the bar back to the pointer. Then without
    // pictures, where a stack lists its windows, and with the bar along the top.
    {
        auto *root = view.rootObject();
        auto *keys = root->property("barKeys").value<QQuickItem *>();
        auto *card = find(root, "windowThumbnails");
        auto *list = find(root, "groupList");
        if (!keys || !card || !list)
            return fail("the panel has no keyboard on the bar, or no card or list of windows");
        auto ask = [&] { subscriber->write(("taskbar " + output + "\n").toUtf8()); };
        auto on = [&] { return keys->property("active").toBool(); };
        auto selected = [&] { return keys->property("button").value<QQuickItem *>(); };
        auto window = [&] { return keys->property("window").toInt(); };
        auto press = [&](Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
            QTest::keyClick(popover, key, modifiers);
        };
        // What a screen reader calls the selection: the attached Accessible object's name.
        auto spoken = [&] {
            for (auto *child : keys->children())
                if (child->inherits("QQuickAccessibleAttached"))
                    return child->property("name").toString();
            return QString();
        };
        auto ringed = [&](QQuickItem *item, const char *ring) {
            auto *found = item ? find(item, ring) : nullptr;
            return found && found->isVisible();
        };
        auto tile = [&](int id) { return windowItem(card, "windowThumbnail", id); };
        auto row = [&](int id) { return windowItem(list, "groupWindow", id); };
        auto rowOf = [&](int id) {
            for (int row = 0; row < fakeModel->property("count").toInt(); ++row)
                if (taskIdAt(row) == id)
                    return row;
            return -1;
        };
        auto buttonFor = [&](int id) -> QQuickItem * {
            for (int i = 0; i < windowButtons(); ++i)
                if (auto *button = listedTask(i); button && button->property("taskId").toInt() == id)
                    return button;
            return nullptr;
        };
        auto peeks = [&] {
            const auto noted = fakeModel->property("peeks").value<QJSValue>().toVariant().toStringList();
            fakeModel->setProperty("peeks", QVariant::fromValue(view.engine()->newArray()));
            return noted.join("|");
        };
        // The keyboard back where it was: off the bar, the popover holding none and gone.
        auto given = [&] {
            return waitFor([&] { return !on() && !popover->keyboard() && !popover->isVisible(); });
        };
        auto highlighted = [&] {
            std::function<bool(QQuickItem *)> walk = [&](QQuickItem *item) {
                for (auto *child : item->childItems())
                    if ((child->objectName().startsWith("contextMenu") && child->isVisible() &&
                         child->property("highlighted").toBool()) ||
                        walk(child))
                        return true;
                return false;
            };
            return walk(menu);
        };
        QQuickItem *single = nullptr;
        auto onBar = [&] {
            return waitFor([&] {
                stack = listedTask(3);
                single = buttonFor(7);
                return stack && single && stack->property("stacked").toBool() && !popover->isVisible();
            });
        };
        if (!onBar())
            return fail("the stacked button and the window's own are not on the bar");
        taskRequests();
        peeks();
        // The pointer elsewhere, so that what opens does not open under it.
        {
            QEvent leaveBar(QEvent::Leave), leavePopover(QEvent::Leave);
            QCoreApplication::sendEvent(&view, &leaveBar);
            QCoreApplication::sendEvent(popover, &leavePopover);
        }
        // Window 11, of the stack, has the keyboard.
        ask();
        if (!waitFor([&] { return on() && popover->keyboard() && popover->isVisible(); }) ||
            selected() != stack || !root->property("groupOpen").toBool() ||
            root->property("groupWindowApp").toString() != "grouped" || !ringed(stack, "taskFocusRing") ||
            spoken() != "Group one and 1 more" || !waitFor([&] { return keys->hasActiveFocus(); }))
            return fail("taskbar_focus did not give the keyboard to the focused window's button, its card open at once");
        if (!waitFor([&] { return inPopover(card) && tile(10) && tile(11); }))
            return fail("the card of the selected button's windows did not show");
        // Left: the window's own button, the card gliding over to its picture, open all the way.
        press(Qt::Key_Left);
        if (selected() != single || root->property("groupWindowApp").toString() != "fake" ||
            ringed(stack, "taskFocusRing") || !ringed(single, "taskFocusRing") || spoken() != "Fake" ||
            !card->isVisible() || card->property("progress").toReal() != 1 ||
            card->width() <= card->property("placedWidth").toReal() + 1)
            return fail("Left did not move to the window's own button, the card gliding to its picture");
        if (!waitFor([&] {
                return card->width() == card->property("placedWidth").toReal() && tile(7) &&
                       card->property("progress").toReal() == 1;
            }))
            return fail("the card did not come to rest over the next button's window");
        press(Qt::Key_Right);
        if (selected() != stack || !waitFor([&] { return tile(10) && tile(11) && !tile(7); }))
            return fail("Right did not move back to the stack and its pictures");
        // Up: into the card, on its first picture. The picture selected peeks at its window after
        // a moment, and the next one takes the peek over at once.
        press(Qt::Key_Up);
        if (window() != 0 || !ringed(tile(10), "windowThumbnailFocusRing") ||
            ringed(tile(11), "windowThumbnailFocusRing") || ringed(stack, "taskFocusRing") || spoken() != "Group one")
            return fail("Up did not move into the card, onto its first picture");
        {
            QElapsedTimer resting;
            press(Qt::Key_Right);
            resting.start();
            if (window() != 1 || !ringed(tile(11), "windowThumbnailFocusRing") || spoken() != "Group two" ||
                !waitFor([&] { return peeks() == "peek 11"; }) || resting.elapsed() < 450)
                return fail("the picture selected did not peek at its window after a moment");
            press(Qt::Key_Left);
            resting.restart();
            if (!waitFor([&] { return peeks() == "peek 10"; }) || resting.elapsed() > 300)
                return fail("the next picture selected did not take the peek over at once");
            auto *cross = find(tile(10), "windowThumbnailClose");
            if (!cross || !waitFor([&] { return cross->opacity() == 1 && cross->isEnabled(); }))
                return fail("the picture selected does not show the cross Delete stands for");
        }
        // The Menu key: its window's menu in the card's place, its first entry highlighted;
        // Escape comes back to the picture.
        press(Qt::Key_Menu);
        if (!waitFor([&] {
                return root->property("taskMenuId").toInt() == 10 && menuShown() && !card->isVisible();
            }) ||
            !highlighted() || !on())
            return fail("the Menu key did not open the selected window's menu in the card's place");
        press(Qt::Key_Escape);
        if (!waitFor([&] { return !root->property("menuOpen").toBool() && inPopover(card) && tile(10); }) ||
            !on() || window() != 0 || !ringed(tile(10), "windowThumbnailFocusRing") || !popover->keyboard() ||
            !waitFor([&] { return keys->hasActiveFocus(); }))
            return fail("closing the window's menu did not come back to its picture on the card");
        // Delete closes the window selected, as its cross does, and the card stays.
        press(Qt::Key_Delete);
        if (!waitFor([&] { return taskRequests() == "close 10"; }) || !on() || !card->isVisible())
            return fail("Delete did not close the selected window");
        // Down: back to the button, whose menu Shift+F10 opens, about all its windows.
        press(Qt::Key_Down);
        if (window() != -1 || selected() != stack || !ringed(stack, "taskFocusRing") || spoken() != "Group one and 1 more")
            return fail("Down did not go back from the card to its button");
        press(Qt::Key_F10, Qt::ShiftModifier);
        if (!waitFor([&] {
                return root->property("taskMenuId").toInt() == 10 && menuShown() &&
                       !root->property("taskMenuGroup").isNull();
            }))
            return fail("Shift+F10 did not open the stacked button's menu");
        press(Qt::Key_Escape);
        if (!waitFor([&] { return !root->property("menuOpen").toBool() && inPopover(card); }) ||
            selected() != stack || !on())
            return fail("closing the button's menu did not come back to the button");
        // Enter on the stack brings up its window after the one that had the keyboard, and gives
        // the keyboard back.
        press(Qt::Key_Return);
        if (!waitFor([&] { return taskRequests() == "activate 10"; }) || !given() || card->isVisible())
            return fail("Enter on a stacked button did not bring up its next window and give the keyboard back");
        // Enter on the button of the window that had the keyboard minimizes it, as a click does.
        editTasks(QString("model.setProperty(%1, 'active', false); model.setProperty(%2, 'active', true)")
                      .arg(rowOf(11))
                      .arg(rowOf(7)));
        ask();
        if (!waitFor([&] { return on() && selected() == single; }))
            return fail("the keyboard on the bar did not start at the focused window's button");
        press(Qt::Key_Enter);
        if (!waitFor([&] { return taskRequests() == "minimize 7"; }) || !given())
            return fail("Enter on the focused window's button did not minimize it");
        editTasks(QString("model.setProperty(%1, 'active', false); model.setProperty(%2, 'active', true)")
                      .arg(rowOf(7))
                      .arg(rowOf(11)));
        // Escape, and the action again, give the keyboard back, changing nothing.
        ask();
        if (!waitFor(on))
            return fail("the keyboard on the bar did not come back");
        press(Qt::Key_Escape);
        if (!given() || !taskRequests().isEmpty())
            return fail("Escape did not give the keyboard back without changing anything");
        ask();
        if (!waitFor(on))
            return fail("the keyboard on the bar did not come back");
        ask();
        if (!given() || !taskRequests().isEmpty())
            return fail("taskbar_focus again did not give the keyboard back");
        // End: the last button. Home: the first, the configured launcher's, which shows no
        // windows and starts its program on Enter.
        ask();
        if (!waitFor(on))
            return fail("the keyboard on the bar did not come back");
        press(Qt::Key_End);
        if (selected() != stack)
            return fail("End did not move to the last button");
        press(Qt::Key_Home);
        if (!selected() || selected()->objectName() != "pinned:pinned:0" || !ringed(selected(), "taskFocusRing") ||
            root->property("groupOpen").toBool() || spoken() != "Test app")
            return fail("Home did not move to the configured launcher's button");
        QFile::remove(marker);
        press(Qt::Key_Return);
        if (!waitFor([&] { return QFile::exists(marker); }) || !given())
            return fail("Enter on a pinned application's button did not start it and give the keyboard back");
        // The pointer moving over the bar hands it back to the pointer, as the card goes; resting
        // there does not.
        const QPoint bare = stack->mapToScene(QPointF(stack->width() + 40, stack->height() / 2)).toPoint();
        QTest::mouseMove(&view, bare);
        ask();
        if (!waitFor([&] { return on() && inPopover(card); }))
            return fail("the keyboard on the bar did not come back with its card");
        QTest::mouseMove(&view, bare);
        if (!stays([&] { return on() && card->isVisible(); }, 300))
            return fail("the pointer resting on the bar took it from the keyboard");
        QTest::mouseMove(&view, bare + QPoint(20, 0));
        QTest::mouseMove(&view, bare + QPoint(40, 0));
        if (!given() || root->property("groupOpen").toBool() || !taskRequests().isEmpty())
            return fail("the pointer moving over the bar did not hand it back to the pointer");
        // A press hands it back too, and goes on to what is under it.
        ask();
        if (!waitFor([&] { return on() && inPopover(card); }))
            return fail("the keyboard on the bar did not come back with its card");
        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, bare + QPoint(40, 0));
        if (!given() || root->property("groupOpen").toBool())
            return fail("a press on the bar did not hand it back to the pointer");
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, bare + QPoint(40, 0));
        // The pointer moving onto the card hands the bar back to it, and the card stays under it
        // for a click on a picture.
        ask();
        if (!waitFor([&] { return on() && inPopover(card) && tile(11); }))
            return fail("the keyboard on the bar did not come back with its card");
        press(Qt::Key_Up);
        QEvent leaveBar(QEvent::Leave);
        QCoreApplication::sendEvent(&view, &leaveBar);
        QTest::mouseMove(popover, centre(tile(11)));
        QTest::mouseMove(popover, centre(tile(11)) + QPoint(10, 0));
        if (!waitFor([&] { return !on(); }) || !stays([&] { return card->isVisible(); }, 400))
            return fail("the pointer moving onto the card did not hand the bar back to it, the card staying");
        QTest::mouseClick(popover, Qt::LeftButton, Qt::NoModifier, centre(tile(11)));
        if (!waitFor([&] { return taskRequests() == "activate 11"; }) || !given())
            return fail("a click on a picture after the keyboard did not focus its window");
        QEvent leavePopover(QEvent::Leave);
        QCoreApplication::sendEvent(popover, &leavePopover);
        peeks();

        // Without pictures, a stack lists its windows, from the row nearest the bar up, and a
        // window's own button shows nothing.
        if (!rewrite(QString(lua).replace("shell={", "shell={thumbnails={enabled=false},")))
            return fail("could not rewrite the configuration");
        controller.reload();
        if (!waitFor([&] { return !controller.thumbnails(); }) || !onBar())
            return fail("the buttons were not on the bar without pictures");
        ask();
        if (!waitFor([&] { return on() && selected() == stack && inPopover(list) && row(10) && row(11); }))
            return fail("the keyboard on the bar did not list the stack's windows at once");
        press(Qt::Key_Up);
        if (window() != 1 || !ringed(row(11), "groupWindowFocusRing") || ringed(row(10), "groupWindowFocusRing") ||
            spoken() != "Group two")
            return fail("Up did not move into the list, onto the row nearest the bar");
        press(Qt::Key_Up);
        press(Qt::Key_Up);
        if (window() != 0 || !ringed(row(10), "groupWindowFocusRing"))
            return fail("Up did not move up the list, and stop at its top");
        press(Qt::Key_Down);
        press(Qt::Key_Down);
        if (window() != -1 || selected() != stack)
            return fail("Down did not move down the list and back to its button");
        press(Qt::Key_Left);
        if (selected() != single || root->property("groupOpen").toBool())
            return fail("a window's own button showed something without pictures");
        press(Qt::Key_Right);
        if (selected() != stack || !waitFor([&] { return inPopover(list); }))
            return fail("the stack's list did not come back");
        press(Qt::Key_Up);
        press(Qt::Key_Up);
        press(Qt::Key_Return);
        if (!waitFor([&] { return taskRequests() == "activate 10"; }) || !given())
            return fail("Enter on a window in a stack's list did not bring it up");

        // Along the top, Down goes into the card below the bar, and Up comes back.
        if (!rewrite(QString(lua).replace("shell={", "shell={panel_position='top',")))
            return fail("could not rewrite the configuration");
        controller.reload();
        if (!waitFor([&] { return controller.thumbnails() && controller.panelTop(); }) || !onBar())
            return fail("the buttons were not on a bar along the top");
        ask();
        if (!waitFor([&] { return on() && card->isVisible() && tile(10) && tile(11); }))
            return fail("the keyboard on a bar along the top did not show the stack's card");
        press(Qt::Key_Up);
        if (window() != -1)
            return fail("Up went into the card below a bar along the top");
        press(Qt::Key_Down);
        if (window() != 0 || !ringed(tile(10), "windowThumbnailFocusRing"))
            return fail("Down did not go into the card below a bar along the top");
        press(Qt::Key_Up);
        if (window() != -1 || selected() != stack)
            return fail("Up did not come back to the button from the card below a bar along the top");
        press(Qt::Key_Escape);
        if (!given() || !taskRequests().isEmpty())
            return fail("Escape did not give the keyboard back from a bar along the top");
        if (!rewrite(lua))
            return fail("could not restore the configuration");
        controller.reload();
        if (!waitFor([&] { return controller.thumbnails() && !controller.panelTop(); }) || !onBar())
            return fail("the bar did not come back along the bottom");
        peeks();
    }
    // Dragging the stack moves all its windows together.
    {
        const QPoint from = centre(stack), to = centre(listedTask(0)) - QPoint(8, 0);
        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, from);
        for (int step = 1; step <= 10; ++step) {
            QTest::mouseMove(&view, from + (to - from) * step / 10);
            QTest::qWait(10);
        }
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, to);
    }
    if (!waitFor([&] { return shownTaskId(0) == 10 && listedTask(0)->property("stacked").toBool(); })) {
        std::cerr << "dragging a stacked task did not move all its windows\n";
        return 1;
    }
    // One window left, the button is a plain one again.
    for (int row = 0; row < fakeModel->property("count").toInt(); ++row)
        if (taskIdAt(row) == 10)
            editTasks(QString("model.remove(%1)").arg(row));
    if (!waitFor([&] {
            auto *single = listedTask(0);
            return single && single->property("taskId").toInt() == 11 &&
                   !single->property("stacked").toBool();
        })) {
        std::cerr << "a stacked task with one window left did not become a plain one\n";
        return 1;
    }
    // The volume control, fed by a stand-in sound server. Its popups open above it, inside
    // the panel's own surface.
    audio.update({"speakers",
                  {{"speakers", "Speakers", 50, false}, {"headset", "Headset", 30, false}},
                  {{41, "Music", "audio-x-generic", 80, false},
                   {42, "Browser", "audio-x-generic", 20, false}}});
    view.rootObject()->setProperty("audioSource", QVariant::fromValue<QObject *>(&audio));
    auto *volume = view.rootObject()->findChild<QQuickItem *>("audioWidget");
    // Laid out once shown: it sits after the task list, not at the bar's start.
    if (!volume || !waitFor([&] { return volume->isVisible() && volume->x() > 0; })) {
        std::cerr << "the volume control did not appear\n";
        return 1;
    }
    // Its tooltip says the volume, not the tooltip's own (empty) accessible name.
    bool tooltipText = false;
    for (auto *child : volume->children())
        if (child->property("text").toString().startsWith("Volume 50%"))
            tooltipText = true;
    if (!tooltipText) {
        std::cerr << "the volume control's tooltip does not show the volume\n";
        return 1;
    }
    auto wheel = [&](int delta) {
        QWheelEvent event(centre(volume), view.mapToGlobal(centre(volume)), {}, {0, delta},
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QGuiApplication::sendEvent(&view, &event);
    };
    wheel(120);
    wheel(120);
    wheel(-120);
    if (audio.requests !=
            QStringList{"volume speakers 55", "volume speakers 60", "volume speakers 55"} ||
        audio.volume() != 55) {
        std::cerr << "scrolling on the volume control did not step the volume: "
                  << audio.requests.join(", ").toStdString() << '\n';
        return 1;
    }
    audio.requests.clear();
    // On the output, the bar's surface along the popover's bottom edge.
    auto above = [&](QQuickItem *popup) {
        if (auto *card = popup->property("card").value<QQuickItem *>())
            popup = card;
        auto box = popup->mapRectToScene(QRectF(0, 0, popup->width(), popup->height()));
        auto widget = volume->mapRectToScene(QRectF(0, 0, volume->width(), volume->height()))
                          .translated(0, popover->height() - view.height());
        return inPopover(popup) && box.bottom() <= widget.top() &&
               box.left() <= widget.center().x() && box.right() >= widget.center().x();
    };
    auto *outputs = find(view.rootObject(), "audioOutputs");
    auto *mixer = find(view.rootObject(), "audioMixer");
    click(volume, Qt::RightButton);
    if (!outputs || !waitFor([&] { return above(outputs); }) ||
        !view.rootObject()->property("menuOpen").toBool()) {
        std::cerr << "right-clicking the volume control did not show the outputs above it\n";
        return 1;
    }
    std::function<QQuickItem *(QQuickItem *, const QString &, const QString &)> findNamed =
        [&](QQuickItem *parent, const QString &name, const QString &text) -> QQuickItem * {
        for (auto *item : parent->childItems()) {
            if (item->objectName() == name && (text.isEmpty() || item->property("text") == text))
                return item;
            if (auto *found = findNamed(item, name, text))
                return found;
        }
        return nullptr;
    };
    auto *headset = findNamed(outputs, "audioOutputItem", "Headset");
    if (!headset)
        return fail("the headset is not listed among the audio outputs");
    click(headset);
    if (!waitFor([&] { return !view.rootObject()->property("menuOpen").toBool(); }) ||
        audio.requests != QStringList{"output headset 2"} || audio.output() != "headset") {
        std::cerr << "choosing an output did not switch to it: "
                  << audio.requests.join(", ").toStdString() << '\n';
        return 1;
    }
    audio.requests.clear();
    click(volume);
    if (!mixer || !waitFor([&] { return above(mixer); })) {
        std::cerr << "clicking the volume control did not show the mixer above it\n";
        return 1;
    }
    QQuickItem *streamSlider = nullptr;
    if (!waitFor(
            [&] { return (streamSlider = findNamed(mixer, "audioStreamSlider", {})); }))
        return fail("no stream slider in the mixer");
    // The first application's slider, clicked three quarters along.
    const auto track =
        streamSlider->mapRectToScene(QRectF(0, 0, streamSlider->width(), streamSlider->height()));
    QTest::mouseClick(popover, Qt::LeftButton, Qt::NoModifier,
                      QPointF(track.left() + track.width() * 0.75, track.center().y()).toPoint());
    if (audio.requests.size() != 1 || !audio.requests[0].startsWith("stream 41 ") ||
        std::abs(audio.requests[0].section(' ', 2).toInt() - 75) > 5 || !mixer->isVisible()) {
        std::cerr << "the mixer's slider did not set the application's volume: "
                  << audio.requests.join(", ").toStdString() << '\n';
        return 1;
    }
    // The server's next report updates the row in place rather than rebuilding it.
    audio.update({"headset",
                  {{"speakers", "Speakers", 55, false}, {"headset", "Headset", 30, false}},
                  {{41, "Music", "audio-x-generic", 74, false},
                   {42, "Browser", "audio-x-generic", 20, false}}});
    if (findNamed(mixer, "audioStreamSlider", {}) != streamSlider) {
        std::cerr << "an update rebuilt the mixer's sliders\n";
        return 1;
    }
    QTest::keyClick(mixer->window(), Qt::Key_Escape);
    if (!waitFor([&] { return !popover->isVisible(); })) {
        std::cerr << "the mixer did not close\n";
        return 1;
    }
    // The command palette: one search over actions, sessions, workspaces, windows and apps.
    {
        PaletteView paletteView(controller, app.primaryScreen());
        if (paletteView.status() != QQuickView::Ready) {
            for (const auto &error : paletteView.errors())
                std::cerr << error.toString().toStdString() << '\n';
            return 1;
        }
        auto palette = controller.palette();
        auto titlesNow = [&] {
            QStringList titles;
            for (const auto &item : palette->results())
                titles << item.toMap()["title"].toString();
            return titles;
        };
        auto openPalette = [&] {
            palette->open(output);
            return waitFor([&] { return paletteView.isVisible(); }) &&
                   waitFor([&] {
                       return find(paletteView.rootObject(), "paletteInput")->hasActiveFocus();
                   });
        };
        auto type = [&](const QString &text) {
            for (const auto &c : text)
                QTest::keyClick(&paletteView, c.toLatin1() ? static_cast<Qt::Key>(c.toUpper().unicode()) : Qt::Key_unknown,
                                Qt::NoModifier);
        };
        if (!openPalette()) {
            std::cerr << "the palette did not open with keyboard focus\n";
            return 1;
        }
        // Saved sessions arrive from the compositor after it opens.
        if (!waitFor([&] { return titlesNow().contains("Restore session work"); })) {
            std::cerr << "the palette lacks the saved session: " << titlesNow().join("|").toStdString() << '\n';
            return 1;
        }
        if (!titlesNow().contains("Workspace 1: web") || !titlesNow().contains("Fake app") ||
            !titlesNow().contains("Toggle tiling") || !titlesNow().contains("Appearance: dark")) {
            std::cerr << "the palette lacks workspaces, apps or actions\n";
            return 1;
        }
        type("tiling");
        if (!waitFor([&] { return !titlesNow().isEmpty() && titlesNow()[0].contains("tiling", Qt::CaseInsensitive); })) {
            std::cerr << "typing did not filter the palette: " << titlesNow().join("|").toStdString() << '\n';
            return 1;
        }
        QTest::keyClick(&paletteView, Qt::Key_Return);
        if (!waitFor([&] { return requests == QStringList{"toggle_tiling"}; }) ||
            !waitFor([&] { return !paletteView.isVisible(); }) || !palette->output().isEmpty()) {
            std::cerr << "running an action from the palette failed: " << requests.join("|").toStdString() << " results " << titlesNow().join("|").toStdString() << " q=" << palette->query().toStdString() << "\n";
            return 1;
        }
        // The arrows move the selection; a filter prefix narrows the kind.
        requests.clear();
        if (!openPalette()) {
            std::cerr << "the palette did not open again\n";
            return 1;
        }
        if (!palette->query().isEmpty()) {
            std::cerr << "the palette kept its last search\n";
            return 1;
        }
        type(">layoutmon");
        QTest::keyClick(&paletteView, Qt::Key_Down);
        QTest::keyClick(&paletteView, Qt::Key_Up);
        if (!waitFor([&] { return titlesNow().contains("Layout: monocle") && palette->selected() == 0; })) {
            std::cerr << "the action filter or the selection is wrong: " << titlesNow().join("|").toStdString() << '\n';
            return 1;
        }
        QTest::keyClick(&paletteView, Qt::Key_Return);
        if (!waitFor([&] { return requests == QStringList{"layout_monocle"}; })) {
            std::cerr << "the layout action was not sent\n";
            return 1;
        }
        requests.clear();
        if (!openPalette()) {
            std::cerr << "the palette did not open a third time\n";
            return 1;
        }
        type(">open terminal");
        QTest::keyClick(&paletteView, Qt::Key_Return);
        if (!waitFor([&] { return requests == QStringList{"terminal"}; })) {
            std::cerr << "the palette did not open a terminal: " << requests.join("|").toStdString()
                      << " results " << titlesNow().join("|").toStdString() << '\n';
            return 1;
        }
        // A late answer with the saved sessions does not move the selection off the entry the
        // user has reached.
        holdSessions = true;
        pendingSessions = nullptr;
        if (!openPalette() || !waitFor([&] { return pendingSessions != nullptr; })) {
            std::cerr << "the palette did not ask for the sessions\n";
            return 1;
        }
        QTest::keyClick(&paletteView, Qt::Key_Down);
        QTest::keyClick(&paletteView, Qt::Key_Down);
        QTest::keyClick(&paletteView, Qt::Key_Down);
        const auto reached = titlesNow().value(palette->selected());
        if (palette->selected() != 3 || reached.isEmpty()) {
            std::cerr << "the arrows did not move the selection\n";
            return 1;
        }
        pendingSessions->write("ok\nwork\t3\t1700000000\n");
        pendingSessions->disconnectFromServer();
        if (!waitFor([&] { return titlesNow().contains("Restore session work"); }) ||
            titlesNow().value(palette->selected()) != reached) {
            std::cerr << "the sessions arriving moved the selection from '"
                      << reached.toStdString() << "' to '"
                      << titlesNow().value(palette->selected()).toStdString() << "'\n";
            return 1;
        }
        holdSessions = false;
        palette->close();
        // A session restores by name, and the text typed can name a new one.
        requests.clear();
        openPalette();
        type("%work");
        QTest::keyClick(&paletteView, Qt::Key_Return);
        if (!waitFor([&] { return requests == QStringList{"session restore work"}; })) {
            std::cerr << "restoring a session from the palette failed: " << requests.join("|").toStdString() << '\n';
            return 1;
        }
        requests.clear();
        openPalette();
        type("evening");
        if (!waitFor([&] { return titlesNow().contains("Save session as evening"); })) {
            std::cerr << "the palette does not offer to save a session by the typed name\n";
            return 1;
        }
        // Searching the web for the words comes just before, at DuckDuckGo by default. (Nothing
        // here runs it: GIO might look the address up.)
        {
            const auto web = palette->results().value(palette->results().size() - 2).toMap();
            if (web["kind"] != "web" || web["title"] != "Search the web for “evening”" ||
                web["target"] != "https://duckduckgo.com/?q=evening" ||
                web["subtitle"] != "Web · duckduckgo.com")
                return fail("the palette does not offer to search the web");
        }
        QTest::keyClick(&paletteView, Qt::Key_Up); // wraps to the last: the save entry
        QTest::keyClick(&paletteView, Qt::Key_Return);
        if (!waitFor([&] { return requests == QStringList{"session save evening"}; })) {
            std::cerr << "saving a session from the palette failed: " << requests.join("|").toStdString() << '\n';
            return 1;
        }
        // A workspace switches the palette's own output.
        switches.clear();
        openPalette();
        type("#3");
        QTest::keyClick(&paletteView, Qt::Key_Return);
        if (!waitFor([&] { return switches == QStringList{"output " + output + " workspace 3"}; })) {
            std::cerr << "switching workspace from the palette failed: " << switches.join("|").toStdString() << '\n';
            return 1;
        }
        // A calculation's value comes first, and Enter copies it; = asks for nothing else.
        requests.clear();
        QGuiApplication::clipboard()->clear();
        openPalette();
        type("2*(3+4)");
        if (!waitFor([&] { return titlesNow().value(0) == "14"; }) ||
            palette->results().value(0).toMap()["kind"] != "calc")
            return fail("the palette did not calculate");
        QTest::keyClick(&paletteView, Qt::Key_Return);
        if (!waitFor([&] { return QGuiApplication::clipboard()->text() == "14"; }) ||
            !waitFor([&] { return !paletteView.isVisible(); }) || !requests.isEmpty())
            return fail("the palette did not copy a calculation's value");
        openPalette();
        type("=pi");
        if (!waitFor([&] { return titlesNow() == QStringList{"3.14159265359"}; }))
            return fail("= did not narrow the palette to the calculator");
        palette->setQuery("5 km in mi");
        if (titlesNow().value(0) != "3.10685596119 mi" ||
            palette->results().value(0).toMap()["target"] != "3.10685596119")
            return fail("the palette did not convert units");
        palette->setQuery("e");
        if (palette->results().value(0).toMap()["kind"] == "calc")
            return fail("the palette calculated a lone constant");
        palette->close();
        // Files, read once a search first asks for them, are found among the rest and alone
        // with /; Enter opens one in its application and Ctrl+Enter its folder. Nothing here
        // opens either, which the panel says.
        controller.clearError();
        openPalette();
        type("quarterly");
        if (!waitFor([&] { return titlesNow().contains("Quarterly report.txt"); }, 10000))
            return fail("the palette did not find a file");
        palette->setQuery("/archive");
        if (titlesNow() != QStringList{"Archive"} || palette->results()[0].toMap()["folder"] != true)
            return fail("/ did not narrow the palette to files");
        palette->setQuery("/quarterly");
        const auto found = palette->results().value(0).toMap();
        if (found["kind"] != "file" || found["target"] != files + "/Quarterly report.txt")
            return fail("the palette's file has the wrong path");
        QTest::keyClick(&paletteView, Qt::Key_Return);
        if (!waitFor([&] { return controller.error().startsWith("Could not open Quarterly report.txt: "); }) ||
            !waitFor([&] { return !paletteView.isVisible(); }) || !requests.isEmpty())
            return fail(("opening a file from the palette did not try GIO's default application: " +
                         controller.error().toStdString()).c_str());
        controller.clearError();
        openPalette();
        type("/quarterly");
        if (!waitFor([&] { return titlesNow() == QStringList{"Quarterly report.txt"}; }))
            return fail("the palette did not find the file again");
        QTest::keyClick(&paletteView, Qt::Key_Return, Qt::ControlModifier);
        if (!waitFor([&] { return controller.error().startsWith("Could not open the folder of Quarterly report.txt: "); }) ||
            !waitFor([&] { return !paletteView.isVisible(); }))
            return fail("Ctrl+Enter did not open a file's folder from the palette");
        controller.clearError();
        // Escape closes without running anything, and so does losing the keyboard.
        requests.clear();
        openPalette();
        type("tiling");
        QTest::keyClick(&paletteView, Qt::Key_Escape);
        if (!waitFor([&] { return !paletteView.isVisible(); }) || !requests.isEmpty()) {
            std::cerr << "Escape did not close the palette quietly\n";
            return 1;
        }
        // Its entries for another search (the start menu's): the windows of the model it is
        // given, no applications; one runs as it would from the palette.
        int windows = 0;
        bool apps = false, tiling = false;
        for (const auto &item : palette->entries(fakeModel)) {
            windows += item.toMap()["kind"] == "window";
            apps = apps || item.toMap()["kind"] == "app";
            tiling = tiling || item.toMap()["title"] == "Toggle tiling";
        }
        const int rows = qobject_cast<QAbstractItemModel *>(fakeModel)->rowCount();
        if (rows == 0 || windows != rows || apps || !tiling)
            return fail("the palette's entries for another search are not its windows and actions");
        palette->run({{"kind", "action"}, {"target", "toggle_tiling"}}, output);
        if (!waitFor([&] { return requests == QStringList{"toggle_tiling"}; }))
            return fail("an entry of the palette's did not run outside it");
        requests.clear();
        // An application it launches is among those launched lately.
        palette->run({{"kind", "app"}, {"target", "paw-test-other.desktop"}}, output);
        if (!waitFor([&] {
                return controller.startMenu()->recent().value(0).toMap()["appId"] == "paw-test-other.desktop";
            }))
            return fail("an application launched from the palette was not recorded");
        // A web search opens its address in the default browser, here an address nothing opens,
        // which the panel says.
        controller.clearError();
        palette->run({{"kind", "web"}, {"target", "paw-test-scheme:evening"}}, output);
        if (!controller.error().startsWith("Could not open paw-test-scheme:evening: "))
            return fail("a web search from the palette did not ask GIO for the browser");
        controller.clearError();
    }
    // The clipboard history's popup: the compositor's clipboard_history action opens it on an
    // output; it lists what was copied, newest first, searches it, copies an entry again with
    // Enter (here through Qt's clipboard, without data-control) and closes, pins one with Ctrl+P,
    // forgets one with Delete, and closes with Escape. The compositor saying the session is
    // locked stops it keeping anything.
    {
        PickerView clipboardView(controller, app.primaryScreen(), "clipboard", "ClipboardPicker.qml",
                                 controller.clipboard());
        if (clipboardView.status() != QQuickView::Ready) {
            for (const auto &error : clipboardView.errors())
                std::cerr << error.toString().toStdString() << '\n';
            return 1;
        }
        auto *history = controller.clipboard();
        history->preview({{{"text/plain", "git log --oneline"}},
                          {{"text/plain", "https://example.org/report"}},
                          {{"text/plain", "Quarterly numbers"}}});
        auto texts = [&] {
            QStringList list;
            for (const auto &entry : history->entries())
                list << entry.toMap()["text"].toString();
            return list;
        };
        auto *list = find(clipboardView.rootObject(), "clipboardList");
        auto shownRows = [&] { return list->property("count").toInt(); };
        auto type = [&](const QString &text) {
            for (const auto &c : text)
                QTest::keyClick(&clipboardView, c.toLatin1());
        };
        auto openHistory = [&] {
            subscriber->write(("clipboard " + output + "\n").toUtf8());
            return waitFor([&] { return clipboardView.isVisible() && history->output() == output; }) &&
                   waitFor([&] { return find(clipboardView.rootObject(), "clipboardSearch")->hasActiveFocus(); });
        };
        if (!openHistory())
            return fail("the clipboard_history action did not open the clipboard history");
        if (texts() != QStringList({"git log --oneline", "https://example.org/report", "Quarterly numbers"}) ||
            shownRows() != 3)
            return fail("the clipboard history does not list what was copied, newest first");
        QGuiApplication::clipboard()->clear();
        type("example");
        if (!waitFor([&] { return shownRows() == 1; }))
            return fail("the clipboard history's search did not narrow it");
        QTest::keyClick(&clipboardView, Qt::Key_Return);
        if (!waitFor([&] { return QGuiApplication::clipboard()->text() == "https://example.org/report"; }) ||
            !waitFor([&] { return !clipboardView.isVisible(); }) || !history->output().isEmpty() ||
            texts().value(0) != "https://example.org/report")
            return fail("Enter did not copy the clipboard history's entry again and close it");
        if (!openHistory() || shownRows() != 3)
            return fail("the clipboard history did not open afresh");
        QTest::keyClick(&clipboardView, Qt::Key_P, Qt::ControlModifier);
        if (!history->entries().value(0).toMap()["pinned"].toBool())
            return fail("Ctrl+P did not pin the clipboard history's entry");
        QTest::keyClick(&clipboardView, Qt::Key_Down);
        QTest::keyClick(&clipboardView, Qt::Key_Delete);
        if (!waitFor([&] { return shownRows() == 2; }) ||
            texts() != QStringList({"https://example.org/report", "Quarterly numbers"}))
            return fail("Delete did not forget the clipboard history's entry");
        click(find(clipboardView.rootObject(), "clipboardClear"));
        if (!waitFor([&] { return texts() == QStringList{"https://example.org/report"}; }))
            return fail("Clear all did not forget all but the pinned entries");
        QTest::keyClick(&clipboardView, Qt::Key_Escape);
        if (!waitFor([&] { return !clipboardView.isVisible(); }) || !history->output().isEmpty())
            return fail("Escape did not close the clipboard history");
        subscriber->write("locked on\n");
        if (!waitFor([&] { return history->locked(); }) ||
            history->record({{"text/plain", "while locked"}}))
            return fail("the clipboard history kept what was copied while the session was locked");
        subscriber->write("locked off\n");
        if (!waitFor([&] { return !history->locked(); }) ||
            !history->record({{"text/plain", "unlocked"}}))
            return fail("the clipboard history kept nothing once the session was unlocked");
    }
    // The emoji picker: the compositor's emoji_picker action opens it on an output, on the tab
    // of the first group; typing searches by name and keyword, and Enter picks an emoji, which
    // the compositor is asked to type once the picker has closed; Ctrl+T changes the skin tone of
    // those that take one, and what was picked lately has a tab of its own, first. When the
    // compositor has nothing to type into, the emoji is copied instead, and the panel says so.
    {
        PickerView emojiView(controller, app.primaryScreen(), "emoji", "EmojiPicker.qml",
                             controller.emoji());
        if (emojiView.status() != QQuickView::Ready) {
            for (const auto &error : emojiView.errors())
                std::cerr << error.toString().toStdString() << '\n';
            return 1;
        }
        auto *picker = controller.emoji();
        if (picker->size() < 1800 || picker->groups().size() != 9)
            return fail("the emoji picker's table was not compiled in");
        auto *grid = find(emojiView.rootObject(), "emojiGrid");
        auto *name = find(emojiView.rootObject(), "emojiName");
        auto shownCount = [&] { return grid->property("count").toInt(); };
        auto type = [&](const QString &text) {
            for (const auto &c : text)
                QTest::keyClick(&emojiView, c.toLatin1());
        };
        auto openPicker = [&] {
            subscriber->write(("emoji " + output + "\n").toUtf8());
            return waitFor([&] { return emojiView.isVisible() && picker->output() == output; }) &&
                   waitFor([&] { return find(emojiView.rootObject(), "emojiSearch")->hasActiveFocus(); });
        };
        if (!openPicker())
            return fail("the emoji_picker action did not open the emoji picker");
        if (shownCount() < 100 || name->property("text") != "grinning face" ||
            find(emojiView.rootObject(), "emojiTab:Recently used"))
            return fail("the emoji picker did not open on its first group");
        requests.clear();
        type("thumbs up");
        if (!waitFor([&] { return name->property("text") == "thumbs up"; }))
            return fail("the emoji picker's search did not find thumbs up first");
        QTest::keyClick(&emojiView, Qt::Key_Return);
        if (!waitFor([&] { return requests.contains("type 👍"); }) ||
            !waitFor([&] { return !emojiView.isVisible(); }) || !picker->output().isEmpty())
            return fail("Enter did not have the compositor type the emoji picked");
        // A skin tone, for what takes one.
        requests.clear();
        if (!openPicker())
            return fail("the emoji picker did not open again");
        if (!find(emojiView.rootObject(), "emojiTab:Recently used") || shownCount() != 1)
            return fail("the emoji picked lately have no tab of their own, first");
        for (int i = 0; i < 3; ++i)
            QTest::keyClick(&emojiView, Qt::Key_T, Qt::ControlModifier);
        type("thumbs up");
        QTest::keyClick(&emojiView, Qt::Key_Return);
        if (picker->tone() != 3 || !waitFor([&] { return requests.contains("type 👍🏽"); }))
            return fail("Ctrl+T did not give the emoji picked a skin tone");
        picker->setTone(0);
        // Nothing to type into: copied instead.
        controller.clearError();
        QGuiApplication::clipboard()->clear();
        if (!openPicker())
            return fail("the emoji picker did not open a third time");
        type("upside");
        QTest::keyClick(&emojiView, Qt::Key_Return);
        if (!waitFor([&] { return QGuiApplication::clipboard()->text() == "🙃"; }) ||
            !controller.error().startsWith("Copied 🙃 to paste it, as nothing has the keyboard"))
            return fail(("the emoji picked was not copied when it could not be typed: " +
                         controller.error().toStdString()).c_str());
        controller.clearError();
        if (!openPicker())
            return fail("the emoji picker did not open a fourth time");
        QTest::keyClick(&emojiView, Qt::Key_Escape);
        if (!waitFor([&] { return !emojiView.isVisible(); }) || !picker->output().isEmpty())
            return fail("Escape did not close the emoji picker");
    }
    // The start menu: its pinned applications and those launched lately, every application from
    // A to Z, and a search over applications, windows and actions, each moved through with the
    // keyboard; Escape clears the search, then closes the menu.
    {
        auto *start = controller.startMenu();
        auto *launcher = find(view.rootObject(), "launcher");
        auto *search = find(view.rootObject(), "applicationSearch");
        auto launcherOpen = [&] { return view.rootObject()->property("launcherOpen").toBool(); };
        auto openStart = [&] {
            view.rootObject()->setProperty("launcherOpen", true);
            return waitFor([&] { return inPopover(launcher) && search->hasActiveFocus(); });
        };
        auto item = [&](const QString &name) { return find(view.rootObject(), name); };
        auto shown = [&](const QString &name) { return item(name) && item(name)->isVisible(); };
        auto key = [&](Qt::Key key) { QTest::keyClick(popover, key); };
        auto type = [&](const QString &text) {
            for (const QChar c : text)
                QTest::keyClick(popover, c.toLatin1());
        };
        if (!launcher || !search)
            return fail("the start menu is missing");
        // With no pins of its own, it has the taskbar's, which are none here.
        if (!start->pinned().isEmpty() || QFile::exists(screens.filePath("state/paw/start-pinned")))
            return fail("the start menu has pins of its own before any was made");
        for (const auto *id : {"paw-test-app.desktop", "paw-test-other.desktop",
                               "paw-test-actions.desktop"})
            start->pin(id);
        if (!openStart() || !waitFor([&] { return shown("startTile:paw-test-actions.desktop"); }) ||
            !shown("startTile:paw-test-app.desktop") || !shown("startAllApps"))
            return fail("the start menu did not open on its pinned applications");
        // Down goes to the first tile, Right to the next, and Enter launches it, recorded among
        // those launched lately.
        key(Qt::Key_Down);
        if (!waitFor([&] { return item("startTile:paw-test-app.desktop")->property("current").toBool(); }))
            return fail("Down did not go to the first pinned application");
        key(Qt::Key_Right);
        if (!waitFor([&] { return item("startTile:paw-test-other.desktop")->property("current").toBool(); }) ||
            item("startTile:paw-test-app.desktop")->property("current").toBool())
            return fail("Right did not go to the next pinned application");
        key(Qt::Key_Return);
        if (!waitFor([&] { return !launcherOpen(); }) || start->recent().isEmpty() ||
            start->recent()[0].toMap()["appId"] != "paw-test-other.desktop")
            return fail("Enter did not launch the pinned application the keyboard was at");
        if (!openStart() || !waitFor([&] { return shown("startRecent:paw-test-other.desktop"); }) ||
            item("startRecent:paw-test-other.desktop")->property("subtitle") != "Just now" ||
            item("startTile:paw-test-other.desktop")->property("current").toBool())
            return fail("the start menu does not list what was launched lately, or kept the keyboard's place");
        // All apps lists them by letter; a letter's heading shows the letters to jump to.
        click(item("startAllApps"));
        if (!waitFor([&] {
                return shown("startApp:paw-test-other.desktop") && !shown("startTile:paw-test-app.desktop");
            }) ||
            !shown("startApp:pinned:0"))
            return fail("All apps did not list every application");
        click(item("startLetter:O"));
        if (!waitFor([&] { return shown("startLetters") && item("startJump:F")->isEnabled(); }) ||
            item("startJump:Q")->isEnabled())
            return fail("a letter's heading did not show the letters to jump to");
        click(item("startJump:F"));
        if (!waitFor([&] { return !shown("startLetters"); }))
            return fail("jumping to a letter did not show the list again");
        click(item("startLetter:O"));
        if (!waitFor([&] { return shown("startLetters"); }))
            return fail("a letter's heading did not show the letters again");
        key(Qt::Key_Escape);
        if (!waitFor([&] { return !shown("startLetters"); }) || !launcherOpen() ||
            !shown("startApp:paw-test-other.desktop"))
            return fail("Escape did not close only the letters");
        key(Qt::Key_Down);
        key(Qt::Key_Down);
        if (!waitFor([&] { return item("startApp:paw-test-app.desktop")->property("current").toBool(); }))
            return fail("Down did not move through All apps past the letters' headings");
        click(item("startBack"));
        if (!waitFor([&] { return shown("startTile:paw-test-app.desktop"); }))
            return fail("Back did not show the pinned applications again");
        // An application's menu, from a right press on its tile or row: Open, its desktop
        // actions, pinning to the start menu and the taskbar, and a tile's Move to front.
        auto *appMenu = item("startAppMenu");
        auto menuFor = [&](const QString &name) {
            click(item(name), Qt::RightButton);
            return waitFor([&] { return inPopover(appMenu) && shown("startMenu:open"); });
        };
        auto choose = [&](const QString &entry) {
            if (!shown(entry))
                return false;
            click(item(entry));
            return waitFor([&] { return !appMenu->isVisible(); });
        };
        auto startPins = [&] {
            QFile file(screens.filePath("state/paw/start-pinned"));
            return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
        };
        if (!appMenu || !menuFor("startTile:paw-test-other.desktop") || !shown("startMenu:unpin") ||
            !shown("startMenu:front") || item("startMenu:taskbar")->property("text") != "Pin to taskbar" ||
            shown("startMenu:pin"))
            return fail("a pinned tile's menu does not offer to unpin it, move it to the front and pin it to the taskbar");
        if (!choose("startMenu:taskbar") || !waitFor([&] { return controller.isPinned("paw-test-other.desktop"); }) ||
            !launcherOpen() || !search->hasActiveFocus())
            return fail("Pin to taskbar from the start menu did not pin it, or closed more than the menu");
        if (!menuFor("startTile:paw-test-other.desktop") ||
            item("startMenu:taskbar")->property("text") != "Unpin from taskbar" || !choose("startMenu:taskbar") ||
            !waitFor([&] { return !controller.isPinned("paw-test-other.desktop"); }))
            return fail("Unpin from taskbar from the start menu did not unpin it");
        if (!menuFor("startTile:paw-test-other.desktop") || !choose("startMenu:front") ||
            !waitFor([&] {
                return startPins() == "paw-test-other.desktop\npaw-test-app.desktop\n"
                                      "paw-test-actions.desktop\n";
            }))
            return fail(("Move to front did not move the tile first: " + startPins().toStdString()).c_str());
        if (!menuFor("startTile:paw-test-other.desktop") || shown("startMenu:front") ||
            !choose("startMenu:unpin") ||
            !waitFor([&] { return !item("startTile:paw-test-other.desktop"); }) ||
            startPins() != "paw-test-app.desktop\npaw-test-actions.desktop\n")
            return fail("Unpin from Start did not remove the tile and forget it");
        // A row of All apps pins to the start menu; a configured launcher is only opened.
        click(item("startAllApps"));
        if (!waitFor([&] { return shown("startApp:paw-test-other.desktop"); }) ||
            !menuFor("startApp:paw-test-other.desktop") || shown("startMenu:unpin") || shown("startMenu:front") ||
            !choose("startMenu:pin") ||
            !waitFor([&] { return startPins().endsWith("paw-test-other.desktop\n"); }))
            return fail("Pin to Start from All apps did not pin it at the end");
        if (!menuFor("startApp:pinned:0") || shown("startMenu:pin") || shown("startMenu:taskbar"))
            return fail("a configured launcher's menu offers to pin it");
        key(Qt::Key_Escape);
        if (!waitFor([&] { return !appMenu->isVisible() && search->hasActiveFocus(); }) || !launcherOpen())
            return fail("Escape in an application's menu did not close only the menu");
        click(item("startBack"));
        // The menu key opens the menu of what the keyboard is at, its first entry highlighted;
        // a desktop action runs from it.
        key(Qt::Key_Down);
        key(Qt::Key_Right);
        QTest::keyClick(popover, Qt::Key_Menu);
        if (!waitFor([&] {
                return inPopover(appMenu) && shown("startMenu:action:touch") &&
                       item("startMenu:open")->property("highlighted").toBool();
            }))
            return fail("the menu key did not open the menu of the tile the keyboard was at");
        QFile::remove(actionMarker);
        if (!choose("startMenu:action:touch") || !waitFor([&] { return QFile::exists(actionMarker); }) ||
            !waitFor([&] { return !launcherOpen(); }))
            return fail("a desktop action from the start menu did not run, or the menu stayed");
        if (!openStart())
            return fail("the start menu did not open after a desktop action");
        // Dragging a tile onto another's place moves it there, the press launching nothing.
        {
            auto *dragged = item("startTile:paw-test-other.desktop");
            auto *first = item("startTile:paw-test-app.desktop");
            if (!dragged || !first || !waitFor([&] { return dragged->isVisible() && first->isVisible(); }))
                return fail("the tiles to drag are not shown");
            const QPoint from = centre(dragged), to = centre(first);
            QTest::mousePress(popover, Qt::LeftButton, Qt::NoModifier, from);
            for (int step = 1; step <= 10; ++step) {
                QTest::mouseMove(popover, from + (to - from) * step / 10);
                QTest::qWait(10);
            }
            QTest::mouseRelease(popover, Qt::LeftButton, Qt::NoModifier, to);
            if (!waitFor([&] {
                    return startPins() == "paw-test-other.desktop\npaw-test-app.desktop\n"
                                          "paw-test-actions.desktop\n";
                }) ||
                !launcherOpen() || start->recent()[0].toMap()["appId"] == "paw-test-other.desktop")
                return fail(("dragging a tile did not move it: " + startPins().toStdString()).c_str());
        }
        // A search groups what it finds: the best match first, then applications, windows and
        // actions; the keyboard moves through them all and Enter runs the one it is at.
        editTasks("model.append({ taskId: 42, title: 'Quarterly report', appId: 'paw-test-other', "
                  "active: false, minimized: false, urgent: false })");
        type("quarterly");
        if (!waitFor([&] { return shown("startBestMatch"); }) ||
            item("startBestMatch")->property("result").toMap()["title"] != "Quarterly report" ||
            item("startBestOpen")->property("text") != "Switch to")
            return fail("searching for a window's title did not find it as the best match");
        // One that finds nothing offers to search the web, and with a prefix says it found
        // nothing.
        search->setProperty("text", "");
        type("zqxw");
        if (!waitFor([&] {
                return shown("startBestMatch") &&
                       item("startBestMatch")->property("result").toMap()["kind"] == "web";
            }) ||
            item("startBestOpen")->property("text") != "Search" || shown("startNothing"))
            return fail("a search that finds nothing does not offer to search the web");
        search->setProperty("text", "");
        type("@zqxw");
        if (!waitFor([&] { return shown("startNothing"); }) || shown("startBestMatch"))
            return fail("a search that finds nothing does not say so");
        search->setProperty("text", "");
        type("action");
        if (!waitFor([&] { return shown("startBestMatch") && shown("startBestAction:touch"); }) ||
            item("startBestMatch")->property("result").toMap()["title"] != "Action app")
            return fail("an application found as the best match does not offer its desktop actions");
        // The keyboard reaches the best match's buttons: Right or Tab moves on to Open and then
        // its actions, Left back; Enter presses the one it is at.
        auto atButton = [&](const QString &name) { return item(name)->property("current").toBool(); };
        key(Qt::Key_Right);
        if (!waitFor([&] { return atButton("startBestOpen"); }))
            return fail("Right did not move from the best match to its Open button");
        key(Qt::Key_Tab);
        if (!waitFor([&] { return atButton("startBestAction:touch"); }) || atButton("startBestOpen"))
            return fail("Tab did not move on to the best match's first action");
        key(Qt::Key_Left);
        if (!waitFor([&] { return atButton("startBestOpen"); }) || atButton("startBestAction:touch"))
            return fail("Left did not move back to the best match's Open button");
        key(Qt::Key_Right);
        QFile::remove(actionMarker);
        key(Qt::Key_Return);
        if (!waitFor([&] { return QFile::exists(actionMarker); }) ||
            !waitFor([&] { return !launcherOpen(); }))
            return fail("Enter did not run the best match's action the keyboard was at");
        if (!openStart())
            return fail("the start menu did not open after running the best match's action");
        search->setProperty("text", "");
        type("app");
        // Every application's name has it (and "Applications menu", an action): the model says
        // which is best.
        const auto apps = start->search("app", controller.palette()->entries(fakeModel));
        const auto best = apps.value(0).toMap()["title"].toString(), next = apps.value(1).toMap()["title"].toString();
        if (apps.size() < 4 || apps[1].toMap()["group"] != "apps" ||
            !waitFor([&] { return shown("startBestMatch") && shown("startResult:" + next); }) ||
            item("startBestMatch")->property("result").toMap()["title"] != best)
            return fail("searching for applications did not list them under the best match");
        key(Qt::Key_Down);
        if (!waitFor([&] { return item("startResult:" + next)->property("current").toBool(); }) ||
            item("startBestMatch")->property("current").toBool())
            return fail("Down did not move from the best match to the next result");
        key(Qt::Key_Up);
        if (!waitFor([&] { return item("startBestMatch")->property("current").toBool(); }))
            return fail("Up did not move back to the best match");
        // Moving the pointer onto a result chooses it. (Results appearing under the pointer
        // where it rests do not: the best match stayed chosen above, and below.)
        QTest::mouseMove(popover, centre(item("startResult:" + next)) + QPoint(0, 2));
        QTest::mouseMove(popover, centre(item("startResult:" + next)));
        if (!waitFor([&] { return item("startResult:" + next)->property("current").toBool(); }))
            return fail("moving the pointer onto a result did not choose it");
        // The field's cross clears the search, the keyboard staying there.
        click(item("startClear"));
        if (!waitFor([&] { return search->property("text").toString().isEmpty() && !shown("startClear"); }) ||
            !search->hasActiveFocus() || !launcherOpen())
            return fail("the search field's cross did not clear it");
        type("app");
        // Escape clears the search, then closes the menu.
        key(Qt::Key_Escape);
        if (!waitFor([&] {
                return search->property("text").toString().isEmpty() && shown("startTile:paw-test-app.desktop");
            }) ||
            !launcherOpen())
            return fail("Escape did not clear the search first");
        key(Qt::Key_Escape);
        if (!waitFor([&] { return !launcherOpen(); }))
            return fail("Escape did not close the start menu once the search was clear");
        // An action found runs as from the palette.
        requests.clear();
        if (!openStart())
            return fail("the start menu did not open again");
        type("toggle tiling");
        if (!waitFor([&] {
                return shown("startBestMatch") &&
                       item("startBestMatch")->property("result").toMap()["title"] == "Toggle tiling";
            }))
            return fail("the start menu's search did not find an action");
        key(Qt::Key_Return);
        if (!waitFor([&] { return requests == QStringList{"toggle_tiling"} && !launcherOpen(); }))
            return fail("Enter did not run the action the start menu found");
        requests.clear();
        // A calculation's value is the best match, which Enter copies.
        QGuiApplication::clipboard()->clear();
        if (!openStart())
            return fail("the start menu did not open for a calculation");
        type("2^10");
        if (!waitFor([&] {
                return shown("startBestMatch") &&
                       item("startBestMatch")->property("result").toMap()["title"] == "1024";
            }) ||
            item("startBestOpen")->property("text") != "Copy")
            return fail("the start menu's search did not calculate");
        key(Qt::Key_Return);
        if (!waitFor([&] { return QGuiApplication::clipboard()->text() == "1024" && !launcherOpen(); }) ||
            !requests.isEmpty())
            return fail("Enter did not copy the value the start menu calculated");
        // A file found is the best match when nothing matches better, with Open folder beside
        // Open, which the keyboard reaches.
        controller.clearError();
        if (!openStart())
            return fail("the start menu did not open for a file");
        type("/quarterly");
        if (!waitFor([&] {
                return shown("startBestMatch") && shown("startBestAction:folder") &&
                       item("startBestMatch")->property("result").toMap()["title"] == "Quarterly report.txt";
            }) ||
            item("startBestOpen")->property("text") != "Open")
            return fail("the start menu's search did not find a file");
        key(Qt::Key_Right);
        key(Qt::Key_Right);
        if (!waitFor([&] { return item("startBestAction:folder")->property("current").toBool(); }))
            return fail("the keyboard did not reach the file's Open folder");
        key(Qt::Key_Return);
        if (!waitFor([&] {
                return controller.error().startsWith("Could not open the folder of Quarterly report.txt: ") &&
                       !launcherOpen();
            }))
            return fail("Open folder did not open the file's folder from the start menu");
        controller.clearError();
        // More pins than a page holds go on pages, which the wheel, the dots beside them and the
        // keyboard moving past the last row turn.
        {
            QStringList extra;
            for (int i = 0; i < 20; ++i) {
                extra << QString("paw-test-page%1.desktop").arg(i);
                QFile entry(screens.filePath("data/applications/" + extra.last()));
                if (!entry.open(QIODevice::WriteOnly) ||
                    entry.write(QString("[Desktop Entry]\nType=Application\nName=Page app %1\nExec=true\n")
                                    .arg(i, 2, 10, QChar('0'))
                                    .toUtf8()) < 0)
                    return fail("could not write an application to pin");
            }
            auto installed = [&](const QString &id) {
                const auto apps = controller.apps();
                return std::any_of(apps.begin(), apps.end(),
                                   [&](const QVariant &app) { return app.toMap()["appId"] == id; });
            };
            if (!waitFor([&] { return installed(extra.last()); }, 10000))
                return fail("the applications to pin were not found");
            for (const auto &id : extra)
                start->pin(id);
            auto *home = item("startHome");
            if (!openStart() || !waitFor([&] { return shown("startPage1"); }) ||
                home->property("page").toInt() != 0)
                return fail("more pins than a page holds did not go on pages");
            auto *pinned = item("startPinned");
            const QPoint over = centre(pinned);
            QWheelEvent wheel(over, popover->mapToGlobal(over), QPoint(), QPoint(0, -120), Qt::NoButton,
                              Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(popover, &wheel);
            if (!waitFor([&] { return home->property("page").toInt() == 1; }))
                return fail("the wheel did not turn the pins' page");
            click(item("startPage0"));
            if (!waitFor([&] { return home->property("page").toInt() == 0; }))
                return fail("a page's dot did not show its page");
            for (int row = 0; row < 4; ++row)
                key(Qt::Key_Down);
            if (!waitFor([&] { return home->property("page").toInt() == 1; }) ||
                home->property("current").toInt() != 3 * home->property("columns").toInt())
                return fail("Down past the last row did not go on to the next page");
            view.rootObject()->setProperty("launcherOpen", false);
            for (const auto &id : extra) {
                start->unpin(id);
                QFile::remove(screens.filePath("data/applications/" + id));
            }
            if (!waitFor([&] { return !installed(extra.last()); }, 10000))
                return fail("the pinned applications were not removed again");
        }
        // Along its bottom, the user's name and picture.
        start->setUser("Robin Lee", QUrl::fromLocalFile(walls + "/a/one.png"));
        if (!openStart() || !waitFor([&] { return shown("userPicture"); }) ||
            item("userName")->property("text") != "Robin Lee")
            return fail("the start menu does not show the user's name and picture");
        view.rootObject()->setProperty("launcherOpen", false);
        editTasks("model.remove(model.count - 1)");
    }
    // The power menu, from the power button in the launcher's bottom-right corner, lists what
    // the compositor says may run, and runs it.
    {
        auto *button = find(view.rootObject(), "powerButton");
        auto *menu = find(view.rootObject(), "powerMenu");
        auto item = [&](const QString &action) {
            return find(view.rootObject(), "powerItem:" + action);
        };
        auto openLauncher = [&] {
            if (!view.rootObject()->property("launcherOpen").toBool())
                QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, start);
            return waitFor([&] { return view.rootObject()->property("launcherOpen").toBool(); });
        };
        if (!button || !menu || !openLauncher() || button->isVisible()) {
            std::cerr << "the power button shows before the compositor said what may run\n";
            return 1;
        }
        subscriber->write("power lock,suspend,reboot,poweroff,logout\n");
        if (!waitFor([&] { return button->isVisible(); })) {
            std::cerr << "the power button did not appear\n";
            return 1;
        }
        // The footer's layout places the button on its next polish.
        if (!waitFor([&] {
                auto *launcher = find(view.rootObject(), "launcher");
                const auto corner = button->mapRectToScene(QRectF(0, 0, button->width(), button->height()));
                const auto area = launcher->mapRectToScene(QRectF(0, 0, launcher->width(), launcher->height()));
                return corner.right() >= area.right() - 40 && corner.bottom() >= area.bottom() - 40;
            }))
            return fail("the power button is not in the launcher's bottom-right corner");
        auto openMenu = [&] {
            if (!openLauncher())
                return false;
            click(button);
            return waitFor([&] {
                return inPopover(menu);
            });
        };
        if (!openMenu() || !item("lock") || !item("suspend") || item("hibernate")) {
            std::cerr << "the power menu does not list what may run\n";
            return 1;
        }
        // A press beside the menu closes it and leaves the launcher open; the button toggles it.
        click(find(view.rootObject(), "applicationSearch"));
        if (!waitFor([&] { return !menu->isVisible(); }) ||
            !view.rootObject()->property("launcherOpen").toBool())
            return fail("a press beside the power menu did not close only the power menu");
        if (!openMenu())
            return fail("the power menu did not open again");
        click(button);
        if (!waitFor([&] { return !menu->isVisible(); }))
            return fail("the power button did not close its menu");
        if (!openMenu())
            return fail("the power menu did not open a third time");
        requests.clear();
        click(item("suspend"));
        if (!waitFor([&] {
                return requests == QStringList{"suspend"} && !menu->isVisible() &&
                       !view.rootObject()->property("launcherOpen").toBool();
            })) {
            std::cerr << "suspending from the power menu failed: " << requests.join("|").toStdString()
                      << '\n';
            return 1;
        }
        // Restart, power off and log out ask first, on the panel's output: a dialog counts down
        // and goes ahead when it runs out, on its button or on Enter; Escape, Cancel or a click
        // beside it gives up.
        PowerView dialog(controller, app.primaryScreen());
        if (dialog.status() != QQuickView::Ready) {
            for (const auto &error : dialog.errors())
                std::cerr << error.toString().toStdString() << '\n';
            return 1;
        }
        auto *power = controller.power();
        auto ask = [&](const QString &action) {
            if (!openMenu() || !item(action))
                return false;
            click(item(action));
            return waitFor([&] {
                return dialog.isVisible() && power->pending() == action && !menu->isVisible();
            });
        };
        auto gaveUp = [&] {
            return waitFor([&] { return !dialog.isVisible() && power->pending().isEmpty(); });
        };
        auto dialogItem = [&](const char *name) { return find(dialog.rootObject(), name); };
        requests.clear();
        if (!ask("poweroff") || power->countdown() != 2 || power->output() != output ||
            power->pendingTitle() != "Power off" ||
            power->message() != "The computer powers off in 2 seconds." || !requests.isEmpty() ||
            dialogItem("powerMessage")->property("text") != power->message()) {
            std::cerr << "power off did not ask first: " << power->message().toStdString() << '\n';
            return 1;
        }
        QTest::keyClick(&dialog, Qt::Key_Escape);
        // It says what it asked as it fades out, the question over by then.
        if (!gaveUp() || dialog.rootObject()->property("pendingTitle") != "Power off" ||
            dialog.rootObject()->property("pending") != "poweroff") {
            std::cerr << "Escape did not give up the power off, or the dialog forgot its question as it went\n";
            return 1;
        }
        if (!ask("reboot") || power->message() != "The computer restarts in 2 seconds.")
            return fail("the restart was not asked about");
        QTest::mouseClick(&dialog, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
        if (!gaveUp()) {
            std::cerr << "a click beside the dialog did not give up the restart\n";
            return 1;
        }
        if (!ask("logout"))
            return fail("the log out was not asked about");
        QTest::mouseClick(&dialog, Qt::LeftButton, Qt::NoModifier, centre(dialogItem("powerCancel")));
        if (!gaveUp()) {
            std::cerr << "Cancel did not give up the log out\n";
            return 1;
        }
        if (!ask("reboot"))
            return fail("the restart was not asked about again");
        QTest::mouseClick(&dialog, Qt::LeftButton, Qt::NoModifier, centre(dialogItem("powerConfirm")));
        if (!waitFor([&] { return requests == QStringList{"reboot"}; }) || !gaveUp()) {
            std::cerr << "the restart button did not restart: " << requests.join("|").toStdString()
                      << '\n';
            return 1;
        }
        requests.clear();
        if (!ask("logout"))
            return fail("the log out was not asked about again");
        QTest::keyClick(&dialog, Qt::Key_Return);
        if (!waitFor([&] { return requests == QStringList{"logout"}; }) || !gaveUp()) {
            std::cerr << "Enter did not log out\n";
            return 1;
        }
        // The keyboard starts on the action's button; Tab takes it to Cancel, where Enter gives
        // up.
        requests.clear();
        if (!ask("reboot") || !dialogItem("powerConfirm")->hasActiveFocus())
            return fail("the power dialog's keyboard is not on its action's button");
        QTest::keyClick(&dialog, Qt::Key_Tab);
        if (!waitFor([&] { return dialogItem("powerCancel")->hasActiveFocus(); }))
            return fail("Tab did not take the power dialog's keyboard to Cancel");
        QTest::keyClick(&dialog, Qt::Key_Return);
        if (!gaveUp() || !requests.isEmpty())
            return fail("Enter on Cancel did not give up the restart");
        requests.clear();
        if (!ask("poweroff"))
            return fail("the power off was not asked about");
        if (!waitFor([&] {
                return power->countdown() == 1 &&
                       power->message() == "The computer powers off in 1 second.";
            }) ||
            !requests.isEmpty() ||
            !waitFor([&] { return requests == QStringList{"poweroff"}; }) || !gaveUp()) {
            std::cerr << "the countdown did not run out into a power off: "
                      << requests.join("|").toStdString() << '\n';
            return 1;
        }
        // An action that may no longer run is not asked about any more.
        if (!ask("reboot"))
            return fail("the restart was not asked about a third time");
        subscriber->write("power lock,suspend,poweroff,logout\n");
        if (!gaveUp() || !requests.contains("poweroff") || requests.size() != 1) {
            std::cerr << "a restart that may no longer run was still asked about\n";
            return 1;
        }
        subscriber->write("power lock,suspend,reboot,poweroff,logout\n");
        // The command palette offers the same, and asks first the same way.
        {
            auto *palette = controller.palette();
            auto titles = [&] {
                QStringList found;
                for (const auto &result : palette->results())
                    found << result.toMap()["title"].toString();
                return found;
            };
            if (!waitFor([&] { return power->available().size() == 5; }))
                return fail("the power menu did not list five actions");
            palette->open(output);
            if (!waitFor([&] {
                    return titles().contains("Lock screen") && titles().contains("Suspend") &&
                           titles().contains("Restart…") && titles().contains("Power off…") &&
                           titles().contains("Log out…");
                }) ||
                titles().contains("Hibernate")) {
                std::cerr << "the palette does not offer what may run: "
                          << titles().join("|").toStdString() << '\n';
                return 1;
            }
            requests.clear();
            palette->setQuery(">power off");
            palette->activate(0);
            if (!waitFor([&] { return dialog.isVisible() && power->pending() == "poweroff"; }) ||
                !requests.isEmpty()) {
                std::cerr << "power off from the palette did not ask first\n";
                return 1;
            }
            power->cancel();
            palette->open(output);
            palette->setQuery("lock screen");
            palette->activate(0);
            if (!waitFor([&] { return requests == QStringList{"lock"}; }) ||
                !power->pending().isEmpty()) {
                std::cerr << "locking from the palette failed: " << requests.join("|").toStdString()
                          << '\n';
                return 1;
            }
        }
        // The power_menu action opens the launcher with the menu up and the keyboard in it: the
        // arrows choose, Enter runs, and asking again closes both.
        requests.clear();
        subscriber->write(("power-menu " + output + "\n").toUtf8());
        if (!waitFor([&] { return menu->isVisible() && menu->hasActiveFocus(); })) {
            std::cerr << "power_menu did not open the power menu with the keyboard\n";
            return 1;
        }
        subscriber->write(("power-menu " + output + "\n").toUtf8());
        if (!waitFor([&] {
                return !menu->isVisible() && !view.rootObject()->property("launcherOpen").toBool();
            })) {
            std::cerr << "power_menu again did not close the power menu\n";
            return 1;
        }
        subscriber->write(("power-menu " + output + "\n").toUtf8());
        if (!waitFor([&] { return menu->isVisible() && menu->hasActiveFocus(); }))
            return fail("the power menu did not open from the compositor");
        QTest::keyClick(menu->window(), Qt::Key_Down);
        QTest::keyClick(menu->window(), Qt::Key_Down);
        QTest::keyClick(menu->window(), Qt::Key_Up);
        QTest::keyClick(menu->window(), Qt::Key_Return);
        if (!waitFor([&] { return requests == QStringList{"suspend"} && !menu->isVisible(); })) {
            std::cerr << "the keyboard did not run the chosen power action: "
                      << requests.join("|").toStdString() << '\n';
            return 1;
        }
        // What the compositor refuses, at once or later, shows across the panel.
        powerRefusal = "no screen locker: power.lock_command is not set";
        if (!openMenu())
            return fail("the power menu did not open for the refusal");
        click(item("lock"));
        if (!waitFor([&] {
                return controller.error() ==
                       "Lock screen: no screen locker: power.lock_command is not set";
            })) {
            std::cerr << "a refused power action was not reported: "
                      << controller.error().toStdString() << '\n';
            return 1;
        }
        powerRefusal.clear();
        subscriber->write("power-error Suspend cancelled: the screen did not lock within 5 seconds\n");
        if (!waitFor([&] {
                return controller.error() ==
                       "Suspend cancelled: the screen did not lock within 5 seconds";
            })) {
            std::cerr << "a power action cancelled later was not reported\n";
            return 1;
        }
        subscriber->write("spawn-error Cannot launch kitty: No such file or directory\n");
        if (!waitFor([&] {
                return controller.error() == "Cannot launch kitty: No such file or directory";
            })) {
            std::cerr << "a program that did not start was not reported\n";
            return 1;
        }
        controller.clearError();
        // With nothing that may run, the button goes.
        if (!openLauncher() || !button->isVisible())
            return fail("the power button was gone before nothing could run");
        subscriber->write("power -\n");
        if (!waitFor([&] { return !button->isVisible(); })) {
            std::cerr << "the power button stayed with nothing to offer\n";
            return 1;
        }
        view.rootObject()->setProperty("launcherOpen", false);
        subscriber->write("power lock,suspend,reboot,poweroff,logout\n");
    }
    // Notifications: the bell, the cards, their buttons and the history, fed by hand the way the
    // D-Bus service feeds them.
    {
        auto *daemon = controller.notifications();
        auto *bell = find(view.rootObject(), "notificationBell");
        if (!bell || bell->isVisible()) {
            std::cerr << "the bell shows without a notification daemon\n";
            return 1;
        }
        daemon->setServing(true);
        // The bell is off unless shell.widgets.notifications asks for it: the clock does its work.
        QTest::qWait(50);
        if (bell->isVisible() || controller.widgets()["notifications"].toString() != "quick" ||
            !rewrite(QString(lua).replace("widgets={", "widgets={notifications='bar',")))
            return fail("the bell showed by default, or the configuration could not be rewritten");
        controller.reload();
        if (!waitFor([&] { return bell->isVisible() && bell->x() > 0; })) {
            std::cerr << "the bell did not appear once the daemon served and the setting asked\n";
            return 1;
        }
        CardsView cards(controller, app.primaryScreen());
        if (cards.status() != QQuickView::Ready) {
            for (const auto &error : cards.errors())
                std::cerr << error.toString().toStdString() << '\n';
            return 1;
        }
        if (cards.isVisible()) {
            std::cerr << "the cards' surface is up with nothing to show\n";
            return 1;
        }
        QSignalSpy invoked(daemon, &NotificationCenter::actionInvoked);
        QSignalSpy closed(daemon, &NotificationCenter::closed);
        auto make = [&](const QString &summary, bool actions) {
            Notification n;
            n.app = "Test";
            n.summary = summary;
            n.body = "with <b>bold</b>";
            n.timeout = 60000;
            if (actions)
                n.actions = {{"default", "Open"}, {"yes", "Yes"}};
            return daemon->notify(n);
        };
        auto card = [&]() { return find(cards.rootObject(), "notificationCard"); };
        make("Hello", true);
        if (!waitFor([&] { return cards.isVisible() && card() && card()->height() > 20; }) ||
            controller.cardsOutput() != output) {
            std::cerr << "a notification did not raise a card on its output\n";
            return 1;
        }
        // Top right of the output, inside the surface, whose width is the card and its margins.
        const auto box = card()->mapRectToScene(QRectF(0, 0, card()->width(), card()->height()));
        if (box.right() > cards.width() || box.left() < 0 || box.width() < 300) {
            std::cerr << "the card is not laid out inside its surface\n";
            return 1;
        }
        if (auto *badge = find(view.rootObject(), "notificationBadge");
            !badge || !waitFor([&] { return badge->isVisible(); })) {
            std::cerr << "the bell has no badge for an unread notification\n";
            return 1;
        }
        if (auto *badge = find(view.rootObject(), "clockBadge");
            !badge || !waitFor([&] { return badge->isVisible(); })) {
            std::cerr << "the clock has no badge for an unread notification\n";
            return 1;
        }
        // (Hovering a card holds its timer; offscreen Qt sends no hover to these items, so
        // notifications_smoke checks that with a real pointer.)
        // An action's button runs it and dismisses the card; the surface goes once it has left.
        auto *button = find(cards.rootObject(), "notificationAction");
        if (!button || button->property("text").toString() != "Yes") {
            std::cerr << "the card has no button for its action (and none for the default one)\n";
            return 1;
        }
        QTest::mouseClick(&cards, Qt::LeftButton, Qt::NoModifier, centre(button));
        if (!waitFor([&] { return invoked.count() == 1; }) ||
            invoked.at(0).at(1).toString() != "yes" || closed.count() != 1 ||
            closed.at(0).at(1).toUInt() != NotificationCenter::Dismissed) {
            std::cerr << "clicking an action's button did not run it and dismiss the card\n";
            return 1;
        }
        if (!waitFor([&] { return !cards.isVisible(); }, 3000)) {
            std::cerr << "the cards' surface stayed up after the last card left\n";
            return 1;
        }
        // A click on the card runs its default action.
        invoked.clear();
        closed.clear();
        make("Again", true);
        if (!waitFor([&] { return cards.isVisible() && card() && card()->height() > 20; })) {
            std::cerr << "another notification did not raise a card\n";
            return 1;
        }
        QTest::qWait(300); // the slide in
        QTest::mouseClick(&cards, Qt::LeftButton, Qt::NoModifier,
                          card()->mapToScene(QPointF(card()->width() - 60, 12)).toPoint());
        if (!waitFor([&] { return invoked.count() == 1; }) ||
            invoked.at(0).at(1).toString() != "default") {
            std::cerr << "clicking a card did not run its default action\n";
            return 1;
        }
        if (!waitFor([&] { return !cards.isVisible(); }, 3000)) {
            std::cerr << "the clicked card stayed\n";
            return 1;
        }
        // The close button dismisses without running anything.
        invoked.clear();
        closed.clear();
        make("Quiet", false);
        if (!waitFor([&] { return cards.isVisible() && card() && card()->height() > 20; })) {
            std::cerr << "a second notification did not raise a card\n";
            return 1;
        }
        QTest::qWait(300);
        auto *close = find(cards.rootObject(), "notificationClose");
        QTest::mouseClick(&cards, Qt::LeftButton, Qt::NoModifier, centre(close));
        if (!waitFor([&] { return closed.count() == 1; }) || invoked.count() != 0) {
            std::cerr << "the close button did not dismiss the card\n";
            return 1;
        }
        if (!waitFor([&] { return !cards.isVisible(); }, 3000)) {
            std::cerr << "the dismissed card stayed\n";
            return 1;
        }
        // A card's countdown line runs down with its timer, and both stop while the pointer is
        // on the card.
        {
            Notification n;
            n.app = "Test";
            n.summary = "Counting down";
            n.timeout = 5000;
            const uint id = daemon->notify(n);
            QQuickItem *line = nullptr;
            if (!waitFor([&] {
                    line = find(cards.rootObject(), "notificationCountdown");
                    return cards.isVisible() && card() && line && line->isVisible() &&
                           line->width() > 0;
                }))
                return fail("a card with a timeout has no countdown line");
            const qreal start = line->width();
            if (!waitFor([&] { return line->width() < start - 2; }))
                return fail("a card's countdown line does not run down");
            QTest::mouseMove(&cards, centre(card()));
            if (!waitFor([&] { return !daemon->timerRunning(id); }))
                return fail("the pointer on a card did not hold its timer");
            const qreal held = line->width();
            QTest::qWait(300);
            if (line->width() != held) {
                std::cerr << "a card's countdown line ran on while the pointer held it: " << held
                          << " then " << line->width() << '\n';
                return 1;
            }
            QTest::mouseMove(&cards, QPoint(1, 1));
            if (!waitFor(
                    [&] { return daemon->timerRunning(id) && line->width() < held - 2; }))
                return fail("a card's countdown did not run on once the pointer left it");
            daemon->dismiss(id);
            if (!waitFor([&] { return !cards.isVisible(); }, 3000))
                return fail("the counting card stayed");
        }
        // The history opens from the bell, marks what it shows as seen, and clears.
        auto *history = find(view.rootObject(), "notificationHistory");
        if (!history || history->isVisible()) {
            std::cerr << "the history is missing or open at start\n";
            return 1;
        }
        make("Kept one", true);
        make("Kept two", false);
        click(bell);
        if (!waitFor([&] { return history->isVisible(); }) || daemon->unread() != 0) {
            std::cerr << "clicking the bell did not open the history and mark it read\n";
            return 1;
        }
        // One application's notifications, under one heading, a row each.
        auto *list = find(view.rootObject(), "notificationList");
        std::function<QList<QQuickItem *>(QQuickItem *, const QString &)> findAll =
            [&](QQuickItem *item, const QString &name) {
                QList<QQuickItem *> found;
                if (item->objectName() == name)
                    found << item;
                for (auto *child : item->childItems())
                    found << findAll(child, name);
                return found;
            };
        auto rows = [&] { return list ? findAll(list, "notificationRow") : QList<QQuickItem *>(); };
        if (!list || daemon->history()->count() < 3 || !waitFor([&] {
                return list->property("count").toInt() == 1 && rows().size() == 2;
            })) {
            std::cerr << "the history does not list the notifications by application, the newest two of many\n";
            return 1;
        }
        // Asked for, the rest show too.
        QTest::qWait(300); // the flyout's slide in
        click(find(list, "groupToggle"));
        if (!waitFor([&] { return rows().size() == daemon->history()->count(); })) {
            std::cerr << "expanding an application did not list all its notifications\n";
            return 1;
        }
        // Too many for the room the calendar leaves on this short output: the calendar gives up
        // its time and date to them, and where the list still goes on past the card's edge, that
        // edge fades out.
        auto *flyoutCalendar = find(view.rootObject(), "calendar");
        auto *calendarTime = find(view.rootObject(), "calendarTime");
        auto *fadeBottom = find(view.rootObject(), "notificationFadeBottom");
        if (!waitFor([&] {
                return flyoutCalendar->property("compact").toBool() && !calendarTime->isVisible() &&
                       fadeBottom->isVisible() == !list->property("atYEnd").toBool();
            }))
            return fail("the calendar did not make room for the notifications, or the list's edge does not fade where it is cut off");
        // An action's button runs it, from the history as from a card.
        invoked.clear();
        auto *historyAction = find(list, "notificationHistoryAction");
        if (!historyAction || historyAction->property("text").toString() != "Yes")
            return fail("the history has no button for a notification's action");
        click(historyAction);
        if (!waitFor([&] { return invoked.count() == 1; }) ||
            invoked.at(0).at(1).toString() != "yes") {
            std::cerr << "the history's action button did not run the action\n";
            return 1;
        }
        // The pointer over a notification shows its cross, which removes it.
        const int kept = daemon->history()->count();
        QTest::mouseMove(popover, centre(rows().first()));
        auto *remove = find(rows().first(), "removeNotification");
        if (!remove || !waitFor([&] { return remove->isVisible(); }))
            return fail("the pointer over a notification did not show its cross");
        click(remove);
        if (!waitFor([&] { return daemon->history()->count() == kept - 1 && rows().size() == kept - 1; })) {
            std::cerr << "the cross did not remove the notification\n";
            return 1;
        }
        auto *dndSwitch = find(view.rootObject(), "dndSwitch");
        QTest::qWait(300);
        QTest::mouseMove(dndSwitch->window(), centre(dndSwitch));
        QTest::qWait(50);
        click(dndSwitch, Qt::LeftButton, 80);
        if (!waitFor([&] { return daemon->dnd(); })) {
            std::cerr << "the do-not-disturb switch did nothing\n";
            return 1;
        }
        daemon->setDnd(false);
        click(find(view.rootObject(), "clearNotifications"));
        auto *empty = find(view.rootObject(), "notificationsEmpty");
        if (!waitFor([&] { return daemon->history()->count() == 0 && empty && empty->isVisible(); })) {
            std::cerr << "Clear all did not empty the history and say so\n";
            return 1;
        }
        if (!waitFor([&] { return !flyoutCalendar->property("compact").toBool() && calendarTime->isVisible(); }))
            return fail("the calendar did not show its time and date again once the notifications were gone");
        click(bell); // close the popup
        // The compositor's notification_history action (Super + N) opens the flyout on the output
        // it names, and closes it again.
        subscriber->write(("notifications " + output + "\n").toUtf8());
        if (!waitFor([&] { return inPopover(history); })) {
            std::cerr << "notification_history did not open the clock flyout\n";
            return 1;
        }
        subscriber->write("notifications ELSEWHERE-1\n");
        subscriber->write(("notifications " + output + "\n").toUtf8());
        if (!waitFor([&] { return !history->isVisible() && !popover->isVisible(); })) {
            std::cerr << "notification_history did not close the clock flyout\n";
            return 1;
        }
        // Right-clicking the bell toggles do-not-disturb.
        click(bell, Qt::RightButton);
        if (!waitFor([&] { return daemon->dnd(); })) {
            std::cerr << "right-clicking the bell did not turn do-not-disturb on\n";
            return 1;
        }
        daemon->setDnd(false);
        // So does right-clicking the clock, which shows a crossed-out bell while it is on.
        auto *clockDnd = find(view.rootObject(), "clockDnd");
        click(find(view.rootObject(), "clockButton"), Qt::RightButton);
        if (!waitFor([&] { return daemon->dnd() && clockDnd && clockDnd->isVisible(); })) {
            std::cerr << "right-clicking the clock did not turn do-not-disturb on\n";
            return 1;
        }
        click(find(view.rootObject(), "clockButton"), Qt::RightButton);
        if (!waitFor([&] { return !daemon->dnd() && !clockDnd->isVisible(); })) {
            std::cerr << "right-clicking the clock again did not turn do-not-disturb off\n";
            return 1;
        }
        if (!rewrite(lua))
            return fail("could not restore the configuration");
        controller.reload();
        if (!waitFor([&] { return !bell->isVisible(); })) {
            std::cerr << "the bell stayed once the setting was gone\n";
            return 1;
        }
    }
    // Quick Settings: with the widgets placed in it, a button left of the clock shows their state,
    // and its flyout holds a tile for each beside night light; the bar keeps none of their own.
    {
        // By default, but for the wallpapers and tiling.
        const QString quickLua =
            QString(lua).replace(barWidgets, "widgets={wallpapers='quick',tiling='quick'},");
        if (!rewrite(quickLua))
            return fail("could not rewrite the configuration");
        controller.reload();
        // A battery and a link that is down, as the battery test left the fake sysfs.
        SystemStatus fake(screens.filePath("sys"));
        QQmlEngine::setObjectOwnership(&fake, QQmlEngine::CppOwnership);
        view.rootObject()->setProperty("statusSource", QVariant::fromValue(&fake));
        auto *button = find(view.rootObject(), "quickSettingsButton");
        if (!button || !waitFor([&] { return button->isVisible() && button->x() > 0; }))
            return fail("the Quick Settings button did not appear with widgets placed in it");
        for (const char *name : {"networkWidget", "batteryWidget", "audioWidget", "tilingToggle",
                                 "profilesButton", "wallpapersButton", "notificationBell"})
            if (find(view.rootObject(), name)->isVisible()) {
                std::cerr << name << " stayed on the bar while placed in Quick Settings\n";
                return 1;
            }
        // The button shows the volume and the battery, and the network only while its link is
        // down; with none of those, sliders.
        auto *networkIcon = find(view.rootObject(), "quickSettingsNetwork");
        auto *volumeIcon = find(view.rootObject(), "quickSettingsVolume");
        auto *batteryIcon = find(view.rootObject(), "quickSettingsBattery");
        auto *slidersIcon = find(view.rootObject(), "quickSettingsSliders");
        auto shows = [&](bool network, bool volume, bool battery, bool sliders) {
            return networkIcon->isVisible() == network && volumeIcon->isVisible() == volume &&
                   batteryIcon->isVisible() == battery && slidersIcon->isVisible() == sliders;
        };
        auto description = [&] { return button->property("description").toString(); };
        if (!networkIcon || !volumeIcon || !batteryIcon || !slidersIcon ||
            !waitFor([&] { return shows(true, true, true, false); }) ||
            !description().contains("Network disconnected"))
            return fail("the Quick Settings button does not show a link that is down");
        // A wired link that is up and no battery: the volume alone, and without a sound server the
        // sliders.
        QDir linkSys(screens.filePath("link"));
        linkSys.mkpath("class/net/eth0");
        for (const auto &[name, text] : {std::pair{"device", ""}, {"operstate", "up\n"}}) {
            QFile file(linkSys.filePath(QString("class/net/eth0/") + name));
            if (!file.open(QIODevice::WriteOnly) || file.write(text) < 0)
                return fail("could not write the fake network interface");
        }
        SystemStatus linked(linkSys.path());
        QQmlEngine::setObjectOwnership(&linked, QQmlEngine::CppOwnership);
        view.rootObject()->setProperty("statusSource", QVariant::fromValue(&linked));
        if (!waitFor([&] { return linked.networkState() == "ethernet" && shows(false, true, false, false); }) ||
            description().contains("connected"))
            return fail("the Quick Settings button shows a link that is up");
        FakeAudio silent; // never available
        QQmlEngine::setObjectOwnership(&silent, QQmlEngine::CppOwnership);
        view.rootObject()->setProperty("audioSource", QVariant::fromValue<QObject *>(&silent));
        if (!waitFor([&] { return shows(false, false, false, true); }) ||
            description() != "Quick settings")
            return fail("the Quick Settings button shows no sliders with nothing else to show");
        view.rootObject()->setProperty("audioSource", QVariant::fromValue<QObject *>(&audio));
        view.rootObject()->setProperty("statusSource", QVariant::fromValue(&fake));
        if (!waitFor([&] { return shows(true, true, true, false); }))
            return fail("the Quick Settings button did not show the link going down");
        click(button);
        auto *quick = find(view.rootObject(), "quickSettings");
        if (!quick || !waitFor([&] { return inPopover(quick); }))
            return fail("clicking the Quick Settings button did not open its flyout");
        auto tile = [&](const QString &name) { return find(quick, "quickTile:" + name); };
        for (const char *name : {"dnd", "nightLight", "tiling", "profiles", "wallpapers", "network"})
            if (!tile(name) || !tile(name)->isVisible()) {
                std::cerr << "Quick Settings has no " << name << " tile\n";
                return 1;
            }
        if (tile("network")->property("label").toString() != "Disconnected" ||
            tile("network")->property("detail").toString() != "wlan0" ||
            !find(quick, "quickBattery")->isVisible())
            return fail("Quick Settings does not show the network and the battery as they are");
        // Do not disturb, night light (through the compositor) and this monitor's tiling.
        auto *daemon = controller.notifications();
        click(tile("dnd"));
        if (!waitFor([&] { return daemon->dnd() && tile("dnd")->property("checked").toBool(); }))
            return fail("the do-not-disturb tile did not turn it on");
        click(tile("dnd"));
        if (!waitFor([&] { return !daemon->dnd(); }))
            return fail("the do-not-disturb tile did not turn it off");
        requests.clear();
        click(tile("nightLight"));
        if (!waitFor([&] { return tile("nightLight")->property("checked").toBool(); }) ||
            requests != QStringList{"night_light_toggle"})
            return fail("the night light tile did not ask the compositor to toggle it");
        click(tile("nightLight"));
        if (!waitFor([&] { return !tile("nightLight")->property("checked").toBool(); }))
            return fail("the night light tile did not toggle it back");
        const bool tiled = panelTiling();
        click(tile("tiling"));
        if (!waitFor([&] { return panelTiling() != tiled && tile("tiling")->property("checked").toBool() != tiled; }))
            return fail("the tiling tile did not toggle this monitor's tiling");
        click(tile("tiling"));
        if (!waitFor([&] { return panelTiling() == tiled; }))
            return fail("the tiling tile did not toggle tiling back");
        // The appearance tile lists the profiles under it; picking one switches to it.
        click(tile("profiles"));
        auto *profiles = find(quick, "quickProfiles");
        if (!profiles || !waitFor([&] { return profiles->isVisible() && profiles->height() > 0; }))
            return fail("the appearance tile did not list the profiles");
        requests.clear();
        QQuickItem *light = nullptr;
        for (auto *row : profiles->childItems())
            if (row->property("text").toString() == "light")
                light = row;
        if (!light)
            return fail("the light profile is not listed in Quick Settings");
        click(light);
        if (!waitFor([&] { return controller.profile() == "light"; }) ||
            requests != QStringList{"profile light"})
            return fail("picking a profile in Quick Settings did not switch to it");
        controller.pickProfile("dark");
        if (!waitFor([&] { return controller.profile() == "dark"; }))
            return fail("the dark profile did not come back");
        // The volume: its slider and mute, the outputs to pick from, and the applications'.
        auto *sound = find(quick, "quickSound");
        auto *volumeSlider = find(quick, "quickVolumeSlider");
        if (!sound || !sound->isVisible() || !volumeSlider)
            return fail("Quick Settings has no volume");
        audio.requests.clear();
        const auto volumeTrack =
            volumeSlider->mapRectToScene(QRectF(0, 0, volumeSlider->width(), volumeSlider->height()));
        QTest::mouseClick(popover, Qt::LeftButton, Qt::NoModifier,
                          QPointF(volumeTrack.left() + volumeTrack.width() * 0.25, volumeTrack.center().y()).toPoint());
        if (audio.requests.size() != 1 || !audio.requests[0].startsWith("volume headset ") ||
            std::abs(audio.requests[0].section(' ', 2).toInt() - 25) > 6) {
            std::cerr << "the Quick Settings volume slider did not set the volume: "
                      << audio.requests.join(", ").toStdString() << '\n';
            return 1;
        }
        click(find(quick, "quickMute"));
        if (!waitFor([&] { return audio.muted(); }))
            return fail("the Quick Settings mute button did not mute");
        click(find(quick, "quickMute"));
        auto *outputsList = find(quick, "quickOutputs");
        click(find(quick, "quickOutputsToggle"));
        if (!outputsList || !waitFor([&] { return outputsList->isVisible() && outputsList->height() > 0; }))
            return fail("the outputs did not open under the volume");
        audio.requests.clear();
        click(findNamed(outputsList, "quickOutputItem", "Speakers"));
        if (!waitFor([&] { return audio.output() == "speakers"; }) ||
            audio.requests != QStringList{"output speakers 2"}) {
            std::cerr << "picking an output in Quick Settings did not switch to it: "
                      << audio.requests.join(", ").toStdString() << '\n';
            return 1;
        }
        auto *streamsList = find(quick, "quickStreams");
        click(find(quick, "quickMixerToggle"));
        if (!streamsList || !waitFor([&] { return streamsList->isVisible() && !outputsList->isVisible(); }))
            return fail("the applications' volumes did not open in place of the outputs");
        QQuickItem *quickStream = nullptr;
        if (!waitFor([&] { return (quickStream = findNamed(streamsList, "quickStreamSlider", {})); }))
            return fail("no application's slider in Quick Settings");
        QTest::qWait(50); // laid out
        audio.requests.clear();
        const auto streamTrack =
            quickStream->mapRectToScene(QRectF(0, 0, quickStream->width(), quickStream->height()));
        QTest::mouseClick(popover, Qt::LeftButton, Qt::NoModifier,
                          QPointF(streamTrack.left() + streamTrack.width() * 0.5, streamTrack.center().y()).toPoint());
        if (audio.requests.size() != 1 || !audio.requests[0].startsWith("stream 41 ")) {
            std::cerr << "the Quick Settings application slider did not set its volume: "
                      << audio.requests.join(", ").toStdString() << '\n';
            return 1;
        }
        // The screen's brightness, only where there is a backlight; the slider sets it (in a
        // fake sysfs tree, by writing the level).
        auto *brightness = find(quick, "quickBrightness");
        if (!brightness || brightness->isVisible())
            return fail("Quick Settings shows a brightness without a backlight");
        QDir lightSys(screens.filePath("light"));
        lightSys.mkpath("class/backlight/fake");
        for (const auto &[name, text] : {std::pair{"max_brightness", "200\n"}, {"brightness", "100\n"}}) {
            QFile level(lightSys.filePath(QString("class/backlight/fake/") + name));
            if (!level.open(QIODevice::WriteOnly) || level.write(text) < 0)
                return fail("could not write the fake backlight");
        }
        Backlight fakeLight(lightSys.path());
        QQmlEngine::setObjectOwnership(&fakeLight, QQmlEngine::CppOwnership);
        view.rootObject()->setProperty("backlightSource", QVariant::fromValue(&fakeLight));
        auto *brightnessSlider = find(quick, "quickBrightnessSlider");
        if (!waitFor([&] { return brightness->isVisible() && brightnessSlider->property("value").toInt() == 50; }))
            return fail("Quick Settings does not show the backlight's level");
        QTest::qWait(50); // laid out
        const auto lightTrack =
            brightnessSlider->mapRectToScene(QRectF(0, 0, brightnessSlider->width(), brightnessSlider->height()));
        QTest::mouseClick(popover, Qt::LeftButton, Qt::NoModifier,
                          QPointF(lightTrack.left() + lightTrack.width() * 0.8, lightTrack.center().y()).toPoint());
        QFile written(lightSys.filePath("class/backlight/fake/brightness"));
        if (!written.open(QIODevice::ReadOnly) || std::abs(written.readAll().trimmed().toInt() - 160) > 12 ||
            std::abs(fakeLight.percent() - 80) > 6) {
            std::cerr << "the brightness slider did not set the backlight: " << fakeLight.percent() << "%\n";
            return 1;
        }
        view.rootObject()->setProperty("backlightSource", QVariant::fromValue(controller.backlight()));
        // The button's wheel changes the volume and a middle click mutes, as on the bar's.
        audio.requests.clear();
        const int before = audio.volume();
        QWheelEvent notch(centre(button), view.mapToGlobal(centre(button)), {}, {0, 120},
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QGuiApplication::sendEvent(&view, &notch);
        if (audio.volume() != before + 5)
            return fail("the wheel on the Quick Settings button did not raise the volume");
        click(button, Qt::MiddleButton);
        if (!waitFor([&] { return audio.muted(); }))
            return fail("a middle click on the Quick Settings button did not mute");
        audio.toggleMute();
        // The wallpaper tile opens the bar's own picker in its place.
        if (!quick->isVisible())
            click(button);
        if (!waitFor([&] { return inPopover(quick); }))
            return fail("Quick Settings did not open again");
        click(tile("wallpapers"));
        auto *picker = find(view.rootObject(), "wallpaperPicker");
        if (!waitFor([&] { return picker && inPopover(picker) && !quick->isVisible(); }))
            return fail("the wallpaper tile did not open the wallpaper picker");
        QTest::keyClick(picker->window(), Qt::Key_Escape);
        if (!waitFor([&] { return !popover->isVisible(); }))
            return fail("the wallpaper picker did not close");
        view.rootObject()->setProperty("statusSource", QVariant::fromValue(controller.status()));
        // With nothing placed in it (do-not-disturb, which this test's configuration leaves in
        // it, switched off), the button goes.
        if (!rewrite(QString(lua).replace("widgets={", "widgets={notifications=false,")))
            return fail("could not rewrite the configuration");
        controller.reload();
        if (!waitFor([&] { return !button->isVisible(); }))
            return fail("the Quick Settings button stayed with nothing placed in it");
        if (!rewrite(lua))
            return fail("could not restore the configuration");
        controller.reload();
    }
    // The display settings window. Quick Settings opens it on this monitor, which the compositor
    // says is beside another; a click selects a monitor and a drag moves it, snapping beside the
    // other. A setting changed and Apply put every monitor's settings on trial, and the question
    // counts down with the keyboard on Revert: Keep keeps them, Escape takes them back, and a
    // trial that runs out says so. A refusal says why. HDR shows only where it can be had, else
    // why not; the last monitor showing the desktop stays on. Reset to configuration, the palette's
    // entry, and closing, which takes a trial back.
    {
        DisplaySettingsView window(controller, app.primaryScreen());
        if (window.status() != QQuickView::Ready) {
            for (const auto &error : window.errors())
                std::cerr << error.toString().toStdString() << '\n';
            return 1;
        }
        auto *settings = controller.displaySettings();
        auto item = [&](const QString &name) { return find(window.rootObject(), name); };
        auto monitor = [](DisplaySettings *settings, const QString &name) {
            for (const auto &entry : settings->monitors())
                if (entry.toMap()["name"] == name)
                    return entry.toMap();
            return QVariantMap();
        };
        auto inWindow = [&](const QString &name) {
            auto *found = item(name);
            return found && found->isVisible() && found->window() == &window;
        };
        auto trialShown = [&] { return item("displayTrial")->property("opacity").toReal() == 1; };
        auto trialGone = [&] { return !item("displayTrial")->isVisible(); };
        monitorsReply = (output +
                         "\tTest Maker Panel 1\t0\tconfig\t1\ton\t-\t0\t0\t1280x720@60.000\t1\t0\t-\t8\t8\toff\tsdr\t"
                         "the monitor does not offer BT.2020 with PQ\t1\t1280x720@60.000*\n"
                         "EXT-1\tOther Maker Monitor 2\t0\tdefault\t1\ton\t-\t1280\t0\t1920x1080@60.000\t1\t0\toff\t8\t8\t"
                         "off\tsdr\t-\t0\t1920x1080@60.000*,1920x1080@144.000,1280x720@60.000\n").toUtf8();
        // Quick Settings' entry closes the flyout and opens the window on this monitor.
        auto *button = find(view.rootObject(), "quickSettingsButton");
        auto *quick = find(view.rootObject(), "quickSettings");
        click(button);
        if (!quick || !waitFor([&] { return inPopover(quick); }) || !find(quick, "quickDisplaySettings"))
            return fail("Quick Settings did not open with its display settings entry");
        QTest::qWait(50); // laid out
        click(find(quick, "quickDisplaySettings"));
        if (!waitFor([&] { return window.isVisible() && !popover->isVisible(); }) ||
            settings->output() != output ||
            !waitFor([&] { return settings->monitors().size() == 2 && !settings->busy(); }) ||
            settings->selected() != output || !inWindow("displayTile:" + output) || !inWindow("displayTile:EXT-1"))
            return fail("Quick Settings' entry did not open the display settings window on the monitors");
        QTest::qWait(50); // laid out
        if (!window.rootObject()->hasActiveFocus() && !waitFor([&] { return window.rootObject()->hasActiveFocus(); }))
            return fail("the display settings window did not take the keyboard");
        // The monitor is chosen by a click on it; this one has HDR to be had, the other says why
        // it has not.
        click(item("displayTile:EXT-1"));
        if (!waitFor([&] { return settings->selected() == "EXT-1"; }) || !inWindow("displayHdr") ||
            inWindow("displayHdrUnavailable") || !inWindow("displayVrr"))
            return fail("a click did not choose a monitor, or HDR does not show where it can be had");
        click(item("displayTile:" + output));
        if (!waitFor([&] { return settings->selected() == output; }) || inWindow("displayHdr") ||
            !inWindow("displayHdrUnavailable") ||
            !item("displayNote")->property("text").toString().contains("HDR cannot be had: the monitor does not offer BT.2020 with PQ."))
            return fail("a monitor without HDR does not say why");
        // A drag of EXT-1 to below this monitor puts it there, snapping under it.
        {
            auto *tile = item("displayTile:EXT-1");
            auto *mine = item("displayTile:" + output);
            const QPoint from = centre(tile);
            const QPointF below = mine->mapToScene(QPointF(mine->width() / 2, mine->height() * 1.6));
            QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, from);
            for (int step = 1; step <= 10; ++step)
                QTest::mouseMove(&window, (QPointF(from) + (below - QPointF(from)) * step / 10.0).toPoint(), 10);
            const bool ghost = item("displayGhost")->isVisible();
            QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, below.toPoint());
            const auto ext = monitor(settings, "EXT-1");
            if (!ghost || ext["y"].toInt() != 720 || ext["x"].toInt() < -1919 || ext["x"].toInt() > 1279 ||
                settings->selected() != "EXT-1" || !settings->changed())
                return fail("a drag did not move a monitor under the other, showing where it would snap");
        }
        // A setting through the keyboard: Down on the scale takes the next.
        settings->reload();
        if (!waitFor([&] { return !settings->changed() && !settings->busy(); }))
            return fail("the display settings did not read the monitors again");
        settings->select("EXT-1");
        // A switch follows the settings: adaptive sync on, then Undo changes turns it off again.
        click(item("displayVrr"));
        if (!waitFor([&] { return monitor(settings, "EXT-1")["vrr"].toBool(); }) ||
            !item("displayVrr")->property("checked").toBool())
            return fail("the adaptive sync switch did not turn it on");
        click(item("displayUndo"));
        if (!waitFor([&] { return !settings->changed() && !settings->busy(); }) ||
            item("displayVrr")->property("checked").toBool())
            return fail("Undo changes did not turn the adaptive sync switch back off");
        item("displayScale")->forceActiveFocus();
        QTest::keyClick(&window, Qt::Key_Down);
        if (!waitFor([&] { return monitor(settings, "EXT-1")["scale"].toDouble() == 1.25; }) ||
            !item("displayApply")->isEnabled())
            return fail("Down on the scale did not take the next one");
        // Apply: every monitor's settings on trial, the question counting down from 15 seconds
        // with the keyboard on Revert.
        requests.clear();
        click(item("displayApply"));
        if (!waitFor([&] { return trialShown() && settings->trial(); }) || requests.size() != 1 ||
            !requests[0].startsWith("monitors apply " + output + " enabled=on mode=1280x720@60.000 scale=1 ") ||
            !requests[0].contains(" EXT-1 enabled=on mode=1920x1080@60.000 scale=1.25 transform=0 position=1280,0 vrr=off ") ||
            !item("displayCountdown")->property("text").toString().contains("15 seconds") ||
            !waitFor([&] { return item("displayRevert")->hasActiveFocus(); }))
            return fail("Apply did not put the settings on trial and ask whether to keep them");
        if (item("displayApply")->isEnabled() || item("displayResolution")->isEnabled())
            return fail("the window could be changed while settings were on trial");
        // Kept, the settings read the monitors again, which a change made meanwhile would lose.
        click(item("displayKeep"));
        if (!waitFor([&] { return trialGone() && !settings->trial() && !settings->busy(); }) ||
            !requests.contains("monitors keep"))
            return fail("Keep did not keep the settings");
        // Again, and Escape takes them back.
        settings->setScale("EXT-1", 1.5);
        click(item("displayApply"));
        if (!waitFor([&] { return trialShown(); }))
            return fail("Apply did not put the settings on trial again");
        QTest::keyClick(&window, Qt::Key_Escape);
        if (!waitFor([&] { return trialGone(); }) || !requests.contains("monitors revert") || !window.isVisible())
            return fail("Escape on the question did not take the settings back, leaving the window open");
        // A trial that runs out says so.
        subscriber->write("monitors-trial 1000\n");
        if (!waitFor([&] { return trialShown(); }) ||
            !item("displayCountdown")->property("text").toString().contains("1 second."))
            return fail("a trial the compositor told of did not ask whether to keep it");
        subscriber->write("monitors-reverted timeout\n");
        if (!waitFor([&] { return trialGone() && !settings->busy(); }) ||
            item("displayMessage")->property("text") != "The settings were not kept; the previous ones are back.")
            return fail("a trial that ran out did not say so");
        // A refusal says why.
        monitorsRefusal = "EXT-1 refused these settings; nothing changed";
        settings->setScale("EXT-1", 2);
        click(item("displayApply"));
        if (!waitFor([&] {
                return item("displayMessage")->property("text") == "EXT-1 refused these settings; nothing changed.";
            }) || settings->trial())
            return fail("a refusal did not say why");
        monitorsRefusal.clear();
        // The last monitor showing the desktop stays on: with EXT-1 off, this one cannot be.
        settings->select("EXT-1");
        click(item("displayEnabled"));
        if (!waitFor([&] { return inWindow("displayChip:EXT-1") && !inWindow("displayTile:EXT-1"); }))
            return fail("a monitor turned off did not leave the arrangement for the row under it");
        click(item("displayTile:" + output));
        if (!waitFor([&] { return settings->selected() == output; }) || item("displayEnabled")->isEnabled())
            return fail("the last monitor showing the desktop could be turned off");
        click(item("displayChip:EXT-1"));
        if (!waitFor([&] { return settings->selected() == "EXT-1"; }))
            return fail("a click on a monitor under the arrangement did not choose it");
        // Reset to configuration, where the window's settings are kept, puts the configuration's on
        // trial.
        monitorsReply.replace("\tdefault\t", "\twindow\t");
        settings->reload();
        if (!waitFor([&] { return item("displayReset")->isEnabled(); }))
            return fail("Reset to configuration is not offered where the window's settings are kept");
        requests.clear();
        click(item("displayReset"));
        if (!waitFor([&] { return trialShown(); }) || requests.value(0) != "monitors reset")
            return fail("Reset to configuration did not put the configuration's settings on trial");
        // Closing takes a trial back; the cross closes.
        click(item("displaySettingsClose"));
        if (!waitFor([&] { return !window.isVisible() && !settings->open(); }) ||
            !requests.contains("monitors revert"))
            return fail("closing during a trial did not take it back");
        // The command palette offers it, through the compositor's action.
        const auto entries = controller.palette()->entries(nullptr);
        if (std::none_of(entries.begin(), entries.end(), [](const QVariant &entry) {
                return entry.toMap()["title"] == "Display settings" && entry.toMap()["target"] == "display_settings";
            }))
            return fail("the command palette does not offer the display settings");
        // The compositor's display-settings line opens it; Escape closes it.
        subscriber->write(("display-settings " + output + "\n").toUtf8());
        if (!waitFor([&] { return window.isVisible(); }))
            return fail("the compositor's display-settings line did not open the window");
        QTest::qWait(50);
        window.rootObject()->forceActiveFocus();
        QTest::keyClick(&window, Qt::Key_Escape);
        if (!waitFor([&] { return !window.isVisible() && !settings->open(); }))
            return fail("Escape did not close the display settings window");
        monitorsReply.clear();
    }
    // Quick Settings' media card: the current player's track and controls, the other players a
    // step away, and none without a player or with shell.widgets.media off. The players are put
    // in the model by hand, as the MPRIS backend puts them.
    {
        FakeMedia media;
        QQmlEngine::setObjectOwnership(&media, QQmlEngine::CppOwnership);
        view.rootObject()->setProperty("mediaSource", QVariant::fromValue<QObject *>(&media));
        // With every other widget on the bar, the Quick Settings button is there for a player.
        if (!rewrite(QString(lua).replace("widgets={", "widgets={notifications=false,")))
            return fail("could not rewrite the configuration");
        controller.reload();
        auto *button = find(view.rootObject(), "quickSettingsButton");
        if (!waitFor([&] { return !button->isVisible(); }))
            return fail("the Quick Settings button shows with nothing placed in it and no player");
        Media::Player music;
        music.name = "org.mpris.MediaPlayer2.music";
        music.identity = "Music";
        music.status = "Paused";
        music.title = "Harbour Lights";
        music.artist = "The Late Ferries";
        music.trackId = "/track/1";
        music.length = 214'000'000;
        music.position = 83'000'000;
        music.canPlay = music.canPause = music.canGoNext = music.canGoPrevious = music.canSeek = true;
        music.canRaise = true;
        media.setPlayer(music);
        if (!waitFor([&] { return button->isVisible() && button->x() > 0; }))
            return fail("the Quick Settings button did not show for a player");
        click(button);
        auto *quick = find(view.rootObject(), "quickSettings");
        auto *card = find(quick, "quickMedia");
        if (!card || !waitFor([&] { return inPopover(quick) && card->isVisible(); }))
            return fail("Quick Settings did not open on the media card");
        auto text = [&](const char *name) { return find(card, name)->property("text").toString(); };
        if (text("quickMediaTitle") != "Harbour Lights" || text("quickMediaArtist") != "The Late Ferries" ||
            text("quickMediaElapsed") != "1:23")
            return fail("the media card does not show the track and where it is");
        click(find(card, "quickMediaPlayPause"));
        click(find(card, "quickMediaNext"));
        click(find(card, "quickMediaPrevious"));
        click(find(card, "quickMediaPlayer"));
        const QString name = music.name;
        if (media.requests != QStringList{"PlayPause " + name, "Next " + name, "Previous " + name, "Raise " + name}) {
            std::cerr << "the media card's controls did not reach the player: "
                      << media.requests.join(", ").toStdString() << '\n';
            return 1;
        }
        // A press on the position seeks there.
        media.requests.clear();
        auto *position = find(card, "quickMediaPosition");
        const auto track = position->mapRectToScene(QRectF(0, 0, position->width(), position->height()));
        QTest::mouseClick(popover, Qt::LeftButton, Qt::NoModifier,
                          QPointF(track.left() + track.width() * 0.5, track.center().y()).toPoint());
        if (media.requests.size() != 1 || !media.requests[0].startsWith("SetPosition " + name + " /track/1 ") ||
            std::abs(media.requests[0].section(' ', 3).toLongLong() - 107'000'000) > 12'000'000 ||
            std::abs(media.position() - 107'000) > 12'000) {
            std::cerr << "a press on the media card's position did not seek: "
                      << media.requests.join(", ").toStdString() << '\n';
            return 1;
        }
        // Playing, the button shows pause; a player that cannot go on greys its control out.
        music.status = "Playing";
        music.canGoNext = false;
        media.setPlayer(music);
        if (!waitFor([&] {
                return find(card, "quickMediaPlayPause")->property("text").toString() == "Pause" &&
                       !find(card, "quickMediaNext")->isEnabled();
            }))
            return fail("the media card does not follow the player's state");
        // A second player: arrows step to it, and its track shows.
        auto *nextPlayer = find(card, "quickMediaNextPlayer");
        if (nextPlayer->isVisible())
            return fail("the media card offers other players with one");
        Media::Player browser = music;
        browser.name = "org.mpris.MediaPlayer2.firefox";
        browser.identity = "Firefox";
        browser.status = "Paused";
        browser.title = "A walk along the coast";
        browser.artist = "";
        media.setPlayer(browser);
        if (!waitFor([&] { return nextPlayer->isVisible(); }) || text("quickMediaTitle") != "Harbour Lights")
            return fail("a paused player took the place of the one playing");
        click(nextPlayer);
        if (!waitFor([&] { return text("quickMediaTitle") == "A walk along the coast"; }) ||
            media.player() != browser.name || find(card, "quickMediaArtist")->isVisible())
            return fail("the media card did not step to the other player");
        // shell.widgets.media off leaves it out.
        if (!rewrite(QString(lua).replace("widgets={", "widgets={media=false,")))
            return fail("could not rewrite the configuration");
        controller.reload();
        if (!waitFor([&] { return !card->isVisible(); }))
            return fail("the media card stayed with shell.widgets.media off");
        QTest::keyClick(popover, Qt::Key_Escape);
        if (!waitFor([&] { return !popover->isVisible(); }))
            return fail("Quick Settings did not close");
        // Gone with the players.
        if (!rewrite(lua))
            return fail("could not restore the configuration");
        controller.reload();
        media.removePlayer(music.name);
        media.removePlayer(browser.name);
        if (card->isVisible() || media.available())
            return fail("the media card stayed without players");
        view.rootObject()->setProperty("mediaSource", QVariant::fromValue<QObject *>(controller.media()));
    }
    // Quick Settings' power mode: a tile while power-profiles-daemon runs, its profiles listed
    // under it, the one picked asked for; none with shell.widgets.power_mode off.
    {
        FakePowerMode mode;
        QQmlEngine::setObjectOwnership(&mode, QQmlEngine::CppOwnership);
        view.rootObject()->setProperty("powerModeSource", QVariant::fromValue<QObject *>(&mode));
        if (!rewrite(QString(lua).replace("widgets={", "widgets={notifications=false,")))
            return fail("could not rewrite the configuration");
        controller.reload();
        auto *button = find(view.rootObject(), "quickSettingsButton");
        if (!waitFor([&] { return !button->isVisible(); }))
            return fail("the Quick Settings button shows with nothing placed in it and no daemon");
        mode.update({true, "balanced", {"power-saver", "balanced", "performance"}, ""});
        if (!waitFor([&] { return button->isVisible() && button->x() > 0; }))
            return fail("the Quick Settings button did not show for the power mode");
        click(button);
        auto *quick = find(view.rootObject(), "quickSettings");
        auto *tile = find(quick, "quickTile:powerMode");
        if (!tile || !waitFor([&] { return inPopover(quick) && tile->isVisible(); }) ||
            tile->property("detail").toString() != "Balanced" || tile->property("checked").toBool())
            return fail("Quick Settings has no power mode tile showing the profile");
        click(tile);
        auto *list = find(quick, "quickPowerModes");
        if (!list || !waitFor([&] { return list->isVisible() && list->height() > 0; }))
            return fail("the power mode tile did not list the profiles");
        QTest::qWait(50); // laid out
        click(findNamed(list, "quickPowerModeItem", "Power saver"));
        if (mode.requests != QStringList{"power-saver"} ||
            !waitFor([&] {
                return tile->property("detail").toString() == "Power saver" && tile->property("checked").toBool();
            }))
            return fail("picking a power mode did not ask the daemon for it");
        // Performance held back says why.
        mode.update({true, "power-saver", {"power-saver", "balanced", "performance"}, "lap-detected"});
        auto *performance = findNamed(list, "quickPowerModeItem", "Performance");
        if (!performance || !waitFor([&] {
                return performance->property("modelData").toMap().value("secondary").toString() == "Limited on a lap";
            }))
            return fail("the power modes do not say performance is held back");
        if (!rewrite(QString(lua).replace("widgets={", "widgets={power_mode=false,")))
            return fail("could not rewrite the configuration");
        controller.reload();
        if (!waitFor([&] { return !tile->isVisible(); }))
            return fail("the power mode tile stayed with shell.widgets.power_mode off");
        if (!rewrite(lua))
            return fail("could not restore the configuration");
        controller.reload();
        // Gone with the daemon, its list too.
        if (!waitFor([&] { return tile->isVisible(); }))
            return fail("the power mode tile did not come back");
        mode.update({});
        if (!waitFor([&] { return !tile->isVisible() && !list->isVisible(); }))
            return fail("the power mode tile stayed without the daemon");
        QTest::keyClick(popover, Qt::Key_Escape);
        if (!waitFor([&] { return !popover->isVisible(); }))
            return fail("Quick Settings did not close");
        view.rootObject()->setProperty("powerModeSource", QVariant::fromValue<QObject *>(controller.powerMode()));
    }
    // Wi-Fi through NetworkManager: the network widget names the network and lists the networks
    // in range, where one is connected to (with a password where it needs one, asked again when
    // refused) or disconnected from and the radio switched; in Quick Settings a tile turns the
    // radio on and off and its chevron lists them. The state is put in the model by hand, as the
    // NetworkManager backend puts it.
    {
        FakeWifi wifi;
        QQmlEngine::setObjectOwnership(&wifi, QQmlEngine::CppOwnership);
        Wifi::State state;
        state.available = state.hasWifi = state.enabled = true;
        state.accessPoints = {{"Home", 80, "wpa-psk"}, {"Office", 60, "sae"}, {"Cafe", 40, "open"},
                              {"Corp", 70, "enterprise"}};
        state.known = {"Home"};
        state.ssid = "Home";
        state.strength = 80;
        state.primaryType = "wifi";
        state.primaryName = "Home";
        wifi.update(state);
        view.rootObject()->setProperty("wifiSource", QVariant::fromValue<QObject *>(&wifi));
        // On the bar (this test's configuration puts the network there).
        auto *widget = find(view.rootObject(), "networkWidget");
        if (!widget || !waitFor([&] { return widget->isVisible() && widget->x() > 0; }) ||
            widget->property("description").toString() != "Wi-Fi: Home, signal 80%")
            return fail("the network widget does not name the Wi-Fi network and its signal");
        click(widget);
        auto *popup = find(view.rootObject(), "wifiPopup");
        if (!popup || !waitFor([&] { return inPopover(popup); }))
            return fail("the network widget did not open the Wi-Fi networks");
        if (wifi.requests != QStringList{"scan"})
            return fail("opening the Wi-Fi networks did not look for networks");
        auto row = [&](const QString &ssid) { return findNamed(popup, "wifiNetwork", ssid); };
        // Every network has its own field and buttons, shown while it is open: the one shown.
        std::function<QQuickItem *(QQuickItem *, const QString &)> visible =
            [&](QQuickItem *parent, const QString &name) -> QQuickItem * {
            for (auto *item : parent->childItems()) {
                if (item->objectName() == name && item->isVisible())
                    return item;
                if (auto *found = visible(item, name))
                    return found;
            }
            return nullptr;
        };
        auto shown = [&](const char *name) { return visible(popup, name) != nullptr; };
        if (!row("Home") || !row("Office") || !row("Cafe") || !row("Corp") ||
            row("Home")->mapToScene(QPointF()).y() > row("Corp")->mapToScene(QPointF()).y())
            return fail("the Wi-Fi networks are not listed, the one connected first");
        // A network that needs a password asks for it; Connect waits for eight characters.
        wifi.requests.clear();
        click(row("Office"));
        QQuickItem *password = nullptr;
        if (!waitFor([&] {
                password = visible(popup, "wifiPassword");
                return password && password->hasActiveFocus();
            }))
            return fail("a secured network did not ask for its password");
        auto *connectButton = visible(popup, "wifiConnect");
        for (char c : std::string("short"))
            QTest::keyClick(popover, c);
        if (!connectButton || connectButton->isEnabled())
            return fail("Connect takes a password too short for WPA");
        for (char c : std::string("-enough"))
            QTest::keyClick(popover, c);
        QTest::keyClick(popover, Qt::Key_Return);
        // The card has the keyboard back, for Escape.
        if (wifi.requests != QStringList{"connect Office sae short-enough"} ||
            !waitFor([&] { return !password->isVisible() && popup->hasActiveFocus(); }))
            return fail("the password did not connect to the network and give the keyboard back");
        // Refused, it is asked for again.
        wifi.connectionFailed("Office", "Could not connect to Office: the password may be wrong", true);
        if (!waitFor([&] { return shown("wifiPassword") && shown("wifiPasswordRefused"); }))
            return fail("a refused password was not asked for again");
        QTest::qWait(50); // laid out
        click(visible(popup, "wifiCancel"));
        if (!waitFor([&] { return !shown("wifiPassword"); }))
            return fail("Cancel did not close the password");
        // An open network connects at a click on Connect; one that needs a sign-in cannot.
        wifi.requests.clear();
        click(row("Cafe"));
        QQuickItem *cafeConnect = nullptr;
        if (!waitFor([&] {
                cafeConnect = visible(popup, "wifiConnect");
                return cafeConnect && !shown("wifiPassword");
            }))
            return fail("an open network offered no Connect");
        QTest::qWait(50); // laid out
        click(cafeConnect);
        if (wifi.requests != QStringList{"connect Cafe open"}) {
            std::cerr << "Connect did not connect to the open network: "
                      << wifi.requests.join(", ").toStdString() << '\n';
            return 1;
        }
        click(row("Corp"));
        QTest::qWait(50); // laid out
        if (shown("wifiConnect"))
            return fail("a network that needs a sign-in offered Connect");
        // The one connected disconnects.
        wifi.requests.clear();
        click(row("Home"));
        QQuickItem *disconnectButton = nullptr;
        if (!waitFor([&] {
                disconnectButton = visible(popup, "wifiDisconnect");
                return disconnectButton != nullptr;
            }))
            return fail("the network connected to offered no Disconnect");
        QTest::qWait(50); // laid out
        click(disconnectButton);
        if (wifi.requests != QStringList{"disconnect"})
            return fail("Disconnect did not disconnect");
        // The switch turns the radio off, and the list says so.
        wifi.requests.clear();
        click(find(popup, "wifiSwitch"));
        if (wifi.requests != QStringList{"enabled 0"} || !waitFor([&] { return shown("wifiListEmpty"); }))
            return fail("the Wi-Fi switch did not turn the radio off");
        QTest::keyClick(popover, Qt::Key_Escape);
        if (!waitFor([&] { return !popover->isVisible(); }))
            return fail("the Wi-Fi networks did not close");
        // In Quick Settings: the tile in place of the network's state.
        if (!rewrite(QString(lua).replace("network='bar',", "")))
            return fail("could not rewrite the configuration");
        controller.reload();
        auto *button = find(view.rootObject(), "quickSettingsButton");
        if (!waitFor([&] { return !widget->isVisible() && button->isVisible(); }))
            return fail("the network widget stayed on the bar");
        click(button);
        auto *quick = find(view.rootObject(), "quickSettings");
        auto *tile = find(quick, "quickTile:wifi");
        if (!tile || !waitFor([&] { return inPopover(quick) && tile->isVisible(); }) ||
            find(quick, "quickTile:network")->isVisible() || tile->property("checked").toBool() ||
            tile->property("detail").toString() != "Off")
            return fail("Quick Settings has no Wi-Fi tile in place of the network's state");
        wifi.requests.clear();
        click(tile);
        if (wifi.requests != QStringList{"enabled 1"} || !tile->property("checked").toBool())
            return fail("the Wi-Fi tile did not turn the radio on");
        auto *list = find(quick, "quickWifiList");
        click(find(quick, "quickTile:wifi:arrow"));
        if (!list || !waitFor([&] { return list->isVisible() && list->height() > 0; }) ||
            wifi.requests != QStringList{"enabled 1", "scan"} || !findNamed(list, "wifiNetwork", "Office"))
            return fail("the Wi-Fi tile's chevron did not list the networks");
        // Without NetworkManager, the network's state as before.
        wifi.update({});
        if (!waitFor([&] { return !tile->isVisible() && !list->isVisible(); }))
            return fail("the Wi-Fi tile stayed without NetworkManager");
        QTest::keyClick(popover, Qt::Key_Escape);
        if (!waitFor([&] { return !popover->isVisible(); }))
            return fail("Quick Settings did not close");
        if (!rewrite(lua))
            return fail("could not restore the configuration");
        controller.reload();
        view.rootObject()->setProperty("wifiSource", QVariant::fromValue<QObject *>(controller.wifi()));
    }
    // Bluetooth through BlueZ: Quick Settings' tile turns the adapter on and off, and its chevron
    // lists the devices, paired ones connected, disconnected and forgotten, those in range found and
    // paired with, and what BlueZ asks as one pairs answered. The state is put in the model by hand,
    // as the BlueZ backend puts it, and its questions asked as the agent asks them.
    {
        FakeBluetooth bluetooth;
        QQmlEngine::setObjectOwnership(&bluetooth, QQmlEngine::CppOwnership);
        auto device = [](const QString &name, bool paired, bool connected, int battery = -1) {
            BluetoothDevice device;
            device.path = "/org/bluez/hci0/" + name;
            device.name = name;
            device.icon = name == "Buds" ? "audio-headphones" : "phone";
            device.paired = paired;
            device.connected = connected;
            device.named = true;
            device.battery = battery;
            return device;
        };
        bluetooth.update({true, true, false,
                          {device("Speaker", true, false), device("Buds", true, true, 72), device("Phone", false, false)}});
        view.rootObject()->setProperty("bluetoothSource", QVariant::fromValue<QObject *>(&bluetooth));
        auto *button = find(view.rootObject(), "quickSettingsButton");
        click(button);
        auto *quick = find(view.rootObject(), "quickSettings");
        auto *tile = find(quick, "quickTile:bluetooth");
        if (!tile || !waitFor([&] { return inPopover(quick) && tile->isVisible(); }) ||
            tile->property("detail").toString() != "Buds" || !tile->property("checked").toBool())
            return fail("Quick Settings has no Bluetooth tile naming the device connected");
        click(tile);
        if (bluetooth.requests != QStringList{"powered 0"} || tile->property("detail").toString() != "Off")
            return fail("the Bluetooth tile did not turn the adapter off");
        click(tile);
        auto *list = find(quick, "quickBluetoothList");
        click(find(quick, "quickTile:bluetooth:arrow"));
        if (!list || !waitFor([&] { return list->isVisible() && list->height() > 0; }))
            return fail("the Bluetooth tile's chevron did not list the devices");
        // Each device has its buttons, shown while it is open: the one shown.
        std::function<QQuickItem *(QQuickItem *, const QString &)> visible =
            [&](QQuickItem *parent, const QString &name) -> QQuickItem * {
            for (auto *item : parent->childItems()) {
                if (item->objectName() == name && item->isVisible())
                    return item;
                if (auto *found = visible(item, name))
                    return found;
            }
            return nullptr;
        };
        auto row = [&](const QString &name) { return findNamed(list, "bluetoothDevice", name); };
        // Opens a device and clicks its button `name` once it shows, `text` on it.
        auto act = [&](const QString &device, const char *name, const QString &text) {
            click(row(device));
            QQuickItem *button = nullptr;
            if (!waitFor([&] {
                    button = visible(list, name);
                    return button && (text.isEmpty() || button->property("text").toString() == text);
                }))
                return false;
            QTest::qWait(50); // laid out
            click(button);
            return true;
        };
        if (!row("Buds") || !row("Speaker") || row("Phone") ||
            row("Buds")->mapToScene(QPointF()).y() > row("Speaker")->mapToScene(QPointF()).y())
            return fail("the paired devices are not listed, the one connected first");
        bluetooth.requests.clear();
        if (!act("Speaker", "bluetoothConnect", "Connect") || !act("Buds", "bluetoothConnect", "Disconnect") ||
            !act("Buds", "bluetoothForget", {}))
            return fail("the paired devices offer no Connect, Disconnect and Forget");
        if (bluetooth.requests != QStringList{"connect Speaker", "disconnect Buds", "forget Buds"}) {
            std::cerr << "the paired devices' buttons did not reach BlueZ: "
                      << bluetooth.requests.join(", ").toStdString() << '\n';
            return 1;
        }
        // Looking for devices lists those in range, to pair with.
        bluetooth.requests.clear();
        click(find(list, "bluetoothLook"));
        if (!waitFor([&] { return row("Phone") != nullptr; }) || bluetooth.requests != QStringList{"discovery 1"})
            return fail("Pair a new device did not look for devices");
        if (!act("Phone", "bluetoothConnect", "Pair"))
            return fail("a device in range offers no Pair");
        // What BlueZ asks: a passkey to confirm, a PIN to type, a code to type on the device.
        bluetooth.ask({"confirm", "/org/bluez/hci0/Phone", "Phone", "482916"});
        auto *request = find(list, "bluetoothRequest");
        if (!request || !waitFor([&] {
                return request->isVisible() && find(list, "bluetoothRequestCode")->property("text").toString() == "482916";
            }))
            return fail("the passkey to confirm is not shown");
        QTest::qWait(50); // laid out
        click(find(list, "bluetoothAccept"));
        bluetooth.ask({"pin", "/org/bluez/hci0/Phone", "Phone", {}});
        auto *answer = find(list, "bluetoothAnswer");
        if (!waitFor([&] { return answer->isVisible() && answer->hasActiveFocus(); }))
            return fail("the PIN to type has no field with the keyboard");
        for (char c : std::string("0000"))
            QTest::keyClick(popover, c);
        QTest::keyClick(popover, Qt::Key_Return);
        if (!waitFor([&] { return !request->isVisible() && quick->hasActiveFocus(); }))
            return fail("the PIN did not answer and give the keyboard back");
        bluetooth.ask({"display", "/org/bluez/hci0/Phone", "Phone", "654321"});
        if (!waitFor([&] { return request->isVisible() && !find(list, "bluetoothAccept")->isVisible(); }))
            return fail("the code to type on the device is not shown on its own");
        QTest::qWait(50); // laid out
        click(find(list, "bluetoothReject"));
        if (bluetooth.requests != QStringList{"discovery 1", "pair Phone", "answer 1", "answer 1 0000", "answer 0"}) {
            std::cerr << "the answers did not reach BlueZ: " << bluetooth.requests.join(", ").toStdString() << '\n';
            return 1;
        }
        // Closed, it stops looking.
        QTest::keyClick(popover, Qt::Key_Escape);
        if (!waitFor([&] { return !popover->isVisible(); }) || bluetooth.requests.last() != "discovery 0")
            return fail("closing Quick Settings did not stop looking for devices");
        if (!rewrite(QString(lua).replace("widgets={", "widgets={bluetooth=false,")))
            return fail("could not rewrite the configuration");
        controller.reload();
        if (!waitFor([&] { return !tile->isVisible(); }))
            return fail("the Bluetooth tile stayed with shell.widgets.bluetooth off");
        if (!rewrite(lua))
            return fail("could not restore the configuration");
        controller.reload();
        bluetooth.update({});
        if (!waitFor([&] { return !tile->isVisible(); }))
            return fail("the Bluetooth tile stayed without BlueZ");
        view.rootObject()->setProperty("bluetoothSource", QVariant::fromValue<QObject *>(controller.bluetooth()));
    }
    // The system tray: hidden while empty, a button for each item shown in the order they came,
    // and clicks and the wheel passed on to the item's application. The items are put in the
    // model by hand, as the D-Bus host puts them.
    {
        auto *trayModel = controller.tray();
        auto *trayRow = find(view.rootObject(), "tray");
        if (!trayRow || trayRow->isVisible()) {
            std::cerr << "the tray shows without items\n";
            return 1;
        }
        auto solid = [](const QColor &color) {
            QImage image(22, 22, QImage::Format_ARGB32);
            image.fill(color);
            return image;
        };
        auto trayItem = [&](const QString &key, const QString &status, const QColor &color) {
            TrayItem item;
            item.key = key;
            item.id = key;
            item.title = "Title of " + key;
            item.status = status;
            item.icon = {solid(color)};
            return item;
        };
        TrayItem first = trayItem("first", "Active", Qt::green);
        first.toolTipTitle = "First";
        first.toolTipText = "<b>not markup</b>";
        trayModel->add(first);
        trayModel->add(trayItem("asleep", "Passive", Qt::red));
        trayModel->add(trayItem("third", "NeedsAttention", Qt::blue));
        auto trayButtons = [&] {
            QList<QQuickItem *> shown;
            for (auto *child : trayRow->childItems())
                if (child->objectName() == "trayItem" && child->isVisible())
                    shown << child;
            std::sort(shown.begin(), shown.end(), [](QQuickItem *a, QQuickItem *b) { return a->x() < b->x(); });
            return shown;
        };
        if (!waitFor([&] { return trayRow->isVisible() && trayButtons().size() == 2; }) ||
            trayButtons()[0]->property("key").toString() != "first" ||
            trayButtons()[1]->property("key").toString() != "third" ||
            trayButtons()[0]->property("toolTip").toString() != "First\n<b>not markup</b>") {
            std::cerr << "the tray does not show its items in order, leaving out the passive one\n";
            return 1;
        }
        // The icon comes from the model's pixels.
        auto iconColor = [&](QQuickItem *button) {
            const QImage frame = view.grabWindow();
            return frame.pixelColor(centre(button));
        };
        if (!waitFor([&] { return iconColor(trayButtons()[0]) == QColor(Qt::green); }) ||
            iconColor(trayButtons()[1]) != QColor(Qt::blue)) {
            std::cerr << "the tray does not draw its items' icons\n";
            return 1;
        }
        QSignalSpy activated(trayModel, &TrayModel::activateRequested);
        QSignalSpy secondary(trayModel, &TrayModel::secondaryActivateRequested);
        QSignalSpy context(trayModel, &TrayModel::contextMenuRequested);
        QSignalSpy scrolled(trayModel, &TrayModel::scrollRequested);
        QQuickItem *button = trayButtons()[0];
        // The point is on the screen: this panel is at the bottom of a 720 pixel high output.
        const QPoint at = centre(button);
        const QVariantList point{"first", at.x(), 720 - view.height() + at.y()};
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, at);
        if (!waitFor([&] { return activated.size() == 1; }) || activated[0] != point) {
            std::cerr << "a left click on a tray item did not activate it\n";
            return 1;
        }
        QTest::mouseClick(&view, Qt::MiddleButton, Qt::NoModifier, at);
        if (!waitFor([&] { return secondary.size() == 1; }) || secondary[0] != point) {
            std::cerr << "a middle click on a tray item did not reach its secondary action\n";
            return 1;
        }
        QTest::mouseClick(&view, Qt::RightButton, Qt::NoModifier, at);
        if (!waitFor([&] { return context.size() == 1; }) || context[0] != point) {
            std::cerr << "a right click on a tray item without a menu did not ask for its own\n";
            return 1;
        }
        // The wheel goes to the item a notch at a time, not to the workspaces.
        switches.clear();
        scrollAt(at, -120);
        scrollAt(at, 60);
        scrollAt(at, 60);
        QWheelEvent sideways(at, view.mapToGlobal(at), QPoint(), QPoint(120, 0), Qt::NoButton, Qt::NoModifier,
                             Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&view, &sideways);
        if (!waitFor([&] { return scrolled.size() == 3; }) ||
            scrolled[0] != QVariantList{"first", -120, "vertical"} ||
            scrolled[1] != QVariantList{"first", 120, "vertical"} ||
            scrolled[2] != QVariantList{"first", 120, "horizontal"} || !switches.isEmpty()) {
            std::cerr << "the wheel over a tray item did not scroll it\n";
            return 1;
        }
        // A changed picture is drawn again; an item going passive leaves.
        trayModel->find("first")->icon = {solid(Qt::yellow)};
        trayModel->changed("first", true);
        if (!waitFor([&] { return iconColor(trayButtons()[0]) == QColor(Qt::yellow); })) {
            std::cerr << "the tray did not draw an item's new icon\n";
            return 1;
        }
        trayModel->find("first")->status = "Passive";
        trayModel->changed("first", true);
        if (!waitFor([&] { return trayButtons().size() == 1; }) ||
            trayButtons()[0]->property("key").toString() != "third") {
            std::cerr << "a passive tray item stayed\n";
            return 1;
        }
        // An item's menu opens on a right press, above the item in the popover; a submenu opens
        // beside its entry, the menu staying, and picking an entry closes it. The application
        // hears of each level as it opens and closes.
        auto menuEntry = [](QVariantMap properties, std::vector<int> children = {}) {
            TrayMenuEntry entry;
            entry.properties = std::move(properties);
            entry.children = std::move(children);
            entry.read();
            return entry;
        };
        TrayItem withMenu = trayItem("menu", "Active", Qt::cyan);
        withMenu.menuPath = "/MenuBar";
        withMenu.menu = {{0, menuEntry({}, {1, 2, 3, 4})},
                         {1, menuEntry({{"label", "_Open"}})},
                         {2, menuEntry({{"type", "separator"}})},
                         {3, menuEntry({{"label", "_More"}}, {31, 32})},
                         {31, menuEntry({{"label", "_Deep"}, {"toggle-type", "checkmark"}, {"toggle-state", 1}})},
                         {32, menuEntry({{"label", "Radio"}, {"toggle-type", "radio"}, {"toggle-state", 0}})},
                         {4, menuEntry({{"label", "Disabled"}, {"enabled", false}})}};
        trayModel->add(withMenu);
        auto trayButton = [&](const QString &key) -> QQuickItem * {
            for (auto *shown : trayButtons())
                if (shown->property("key").toString() == key)
                    return shown;
            return nullptr;
        };
        // Laid out after the one already there.
        if (!waitFor([&] {
                const auto shown = trayButtons();
                return shown.size() == 2 && shown[0]->property("key").toString() == "third" &&
                       shown[1]->property("key").toString() == "menu";
            })) {
            std::cerr << "a tray item with a menu did not show after the others\n";
            return 1;
        }
        QSignalSpy opened(trayModel, &TrayModel::menuOpenRequested);
        QSignalSpy closed(trayModel, &TrayModel::menuCloseRequested);
        QSignalSpy picked(trayModel, &TrayModel::menuClickRequested);
        auto *trayMenu = find(view.rootObject(), "trayMenu");
        auto trayEntries = [&] {
            QList<QQuickItem *> found;
            std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
                for (auto *child : item->childItems()) {
                    if (child->objectName() == "trayMenuItem" && child->isVisible())
                        found << child;
                    walk(child);
                }
            };
            if (trayMenu)
                walk(trayMenu);
            std::sort(found.begin(), found.end(), [](QQuickItem *a, QQuickItem *b) {
                return a->mapToScene({0, 0}).y() < b->mapToScene({0, 0}).y();
            });
            return found;
        };
        auto trayLabels = [&] {
            QStringList labels;
            for (auto *entry : trayEntries())
                labels << entry->property("text").toString();
            return labels;
        };
        auto trayEntry = [&](const QString &label) -> QQuickItem * {
            for (auto *entry : trayEntries())
                if (entry->property("text").toString() == label)
                    return entry;
            return nullptr;
        };
        auto trayMenuShown = [&] {
            return inPopover(trayMenu);
        };
        auto menuOpen = [&] { return view.rootObject()->property("menuOpen").toBool(); };
        const QPoint menuAt = centre(trayButton("menu"));
        // Held past the long-press time, as with the bar's menus.
        QTest::mousePress(&view, Qt::RightButton, Qt::NoModifier, menuAt);
        QTest::qWait(1000);
        QTest::mouseRelease(&view, Qt::RightButton, Qt::NoModifier, menuAt);
        if (!trayMenu || !waitFor([&] {
                return trayMenuShown() && trayLabels() == QStringList{"Open", "More", "Disabled"} &&
                       opened.size() == 1;
            }) ||
            opened[0] != QVariantList{"menu", 0} || context.size() != 1) {
            std::cerr << "right-clicking a tray item did not open its menu: "
                      << trayLabels().join("|").toStdString() << '\n';
            return 1;
        }
        click(trayEntry("Disabled"));
        QTest::qWait(100);
        if (!picked.isEmpty() || !trayMenuShown()) {
            std::cerr << "a disabled tray menu entry could be picked\n";
            return 1;
        }
        click(trayEntry("More"));
        if (!waitFor([&] {
                return trayEntry("Deep") && trayEntry("Radio") && trayEntry("Open") &&
                       trayEntry("More")->property("expanded").toBool() && opened.size() == 2;
            }) ||
            opened[1] != QVariantList{"menu", 3} || !closed.isEmpty() ||
            !trayEntry("Deep")->property("modelData").toMap()["checked"].toBool() ||
            trayEntry("Radio")->property("modelData").toMap()["checked"].toBool()) {
            std::cerr << "a tray submenu did not open beside its entry: " << trayLabels().join("|").toStdString() << '\n';
            return 1;
        }
        QTest::keyClick(popover, Qt::Key_Left);
        if (!waitFor([&] { return !trayEntry("Deep") && closed.size() == 1; }) ||
            closed[0] != QVariantList{"menu", 3} || !trayMenuShown() ||
            trayLabels() != QStringList{"Open", "More", "Disabled"}) {
            std::cerr << "closing a tray submenu did not tell its application, or closed the menu\n";
            return 1;
        }
        // The application changing the menu while it is open shows at once.
        auto &openEntry = trayModel->find("menu")->menu[1];
        openEntry.properties["label"] = "_Reopen";
        openEntry.read();
        trayModel->menuEdited("menu");
        if (!waitFor([&] { return trayEntry("Reopen") != nullptr; })) {
            std::cerr << "a tray menu did not follow its application's change\n";
            return 1;
        }
        // Picked with a submenu open, both levels close, the deeper first.
        click(trayEntry("More"));
        if (!waitFor([&] { return trayEntry("Deep") && opened.size() == 3; }))
            return fail("the tray submenu did not open again");
        click(trayEntry("Reopen"));
        if (!waitFor([&] { return picked.size() == 1 && !menuOpen(); }) ||
            picked[0] != QVariantList{"menu", 1} ||
            !waitFor([&] { return !popover->isVisible() && closed.size() == 3; }) ||
            closed[1] != QVariantList{"menu", 3} || closed[2] != QVariantList{"menu", 0}) {
            std::cerr << "picking a tray menu entry did not reach the item and close the menu\n";
            return 1;
        }
        // An item that is only a menu opens it on a left click; a second press closes it.
        trayModel->find("menu")->itemIsMenu = true;
        trayModel->changed("menu", false);
        QTest::qWait(50);
        const qsizetype activations = activated.size();
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, menuAt);
        if (!waitFor([&] { return trayMenuShown(); }) || activated.size() != activations) {
            std::cerr << "a left click on a tray item that is only a menu did not open it\n";
            return 1;
        }
        // A second right click closes it.
        click(trayButton("menu"), Qt::RightButton);
        if (!waitFor([&] { return !menuOpen(); })) {
            std::cerr << "a second right click did not close the tray menu\n";
            return 1;
        }
        // Escape closes it too.
        click(trayButton("menu"));
        if (!waitFor([&] { return trayMenuShown(); }))
            return fail("the tray menu did not open again");
        QTest::keyClick(trayMenu->window(), Qt::Key_Escape);
        if (!waitFor([&] { return !menuOpen(); })) {
            std::cerr << "Escape did not close the tray menu\n";
            return 1;
        }
        // An item that cannot be activated shows its menu after a left click.
        trayModel->find("menu")->itemIsMenu = false;
        trayModel->changed("menu", false);
        QTest::qWait(50);
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, menuAt);
        if (!waitFor([&] { return activated.size() == activations + 1; }))
            return fail("clicking the tray menu's item did not activate it");
        Q_EMIT trayModel->activationRefused("menu");
        if (!waitFor([&] { return trayMenuShown(); })) {
            std::cerr << "a tray item that refused activation did not show its menu\n";
            return 1;
        }
        // The menu goes with its item.
        trayModel->remove("menu");
        if (!waitFor([&] { return !menuOpen() && !popover->isVisible(); })) {
            std::cerr << "a tray menu stayed open after its item went\n";
            return 1;
        }
        trayModel->clear();
        if (!waitFor([&] { return !trayRow->isVisible(); })) {
            std::cerr << "the tray stayed with no items\n";
            return 1;
        }
    }
    // The design tokens follow the configuration: the animation settings set every duration,
    // popups are opaque over a translucent bar, and a light panel is known for one.
    {
        QQmlComponent probe(view.engine());
        // In the module's folder, where Theme is found as the panel finds it.
        probe.setData("import QtQuick\nQtObject { property QtObject theme: Theme }",
                      QUrl("qrc:/shell/PawShell/ThemeProbe.qml"));
        std::unique_ptr<QObject> holder(probe.create());
        auto *theme = holder ? holder->property("theme").value<QObject *>() : nullptr;
        if (!theme) {
            std::cerr << "Theme did not load: " << probe.errorString().toStdString() << '\n';
            return 1;
        }
        auto token =[&](const char *name) { return theme->property(name); };
        if (token("durationFast").toInt() != 120 || token("light").toBool() ||
            token("surface").value<QColor>() != controller.panelColor())
            return fail("Theme does not follow the default configuration");
        if (!rewrite(QString(lua).replace("shell={wallpaper", "animations={speed=2},shell={wallpaper")))
            return fail("could not rewrite the configuration");
        controller.reload();
        if (!waitFor([&] { return token("durationFast").toInt() == 60; }))
            return fail("Theme's durations do not follow animations.speed");
        if (!rewrite(QString(lua).replace(
                "shell={wallpaper", "animations={enabled=false},shell={panel_color='#f3f2fbcc',"
                                    "text_color='#141a48',accent='#4a64dc',wallpaper")))
            return fail("could not rewrite the configuration");
        controller.reload();
        if (!waitFor([&] { return token("light").toBool(); }) ||
            token("durationFast").toInt() != 0 || token("durationSlow").toInt() != 0 ||
            token("bar").value<QColor>().alpha() != 0xcc ||
            token("surface").value<QColor>().alpha() != 255 ||
            token("hover").value<QColor>().alpha() == 0 ||
            token("textOnAccent").value<QColor>() != token("surface").value<QColor>()) {
            std::cerr << "Theme does not follow a light, translucent profile without animations\n";
            return 1;
        }
        if (!rewrite(lua))
            return fail("could not restore the configuration");
        controller.reload();
    }
    // Launchpad, the macOS style's launcher: it opens over the whole output with the keyboard in
    // its search; the arrows move its highlight and page past a page's edge, Page Down and Up,
    // the wheel and the dots page; typing filters the grid, Enter launches the best match; Escape
    // clears the search and then closes it; a click on an application launches it, a right click
    // opens its menu, and a click beside the applications closes Launchpad.
    {
        // The macOS style's tokens are the taskbar style's own in that style, which so keeps its
        // look, and macOS's in the macOS style.
        QQmlComponent probe(view.engine());
        probe.setData("import QtQuick\nQtObject { property QtObject theme: Theme }",
                      QUrl("qrc:/shell/PawShell/ThemeProbe.qml"));
        std::unique_ptr<QObject> holder(probe.create());
        auto *theme = holder ? holder->property("theme").value<QObject *>() : nullptr;
        if (!theme)
            return fail("Theme did not load");
        auto token = [&](const char *name) { return theme->property(name); };
        if (token("macos").toBool() || token("menuRowHeight") != token("rowHeight") ||
            token("menuRadius") != token("radiusMedium") || token("menuHighlight") != token("hover") ||
            token("popupSurface") != token("surface") || token("popupOutline") != token("border") ||
            token("textOnAccentFill") != token("textOnAccent") || token("buttonFace") != token("surfaceRaised"))
            return fail("the macOS style's tokens are not the taskbar style's own in that style");
        if (!rewrite(QString(lua).replace("shell={wallpaper", "shell={style='macos',wallpaper")))
            return fail("could not rewrite the configuration");
        controller.reload();
        if (!waitFor([&] { return controller.style() == "macos"; }))
            return fail("the configuration's macOS style was not read");
        // Spotlight lists what it finds by kind after the top hit, each kind once (but for
        // searching the web and saving a session at the end), files among them.
        {
            auto *palette = controller.palette();
            palette->open(output);
            palette->setQuery("a");
            QStringList runs;
            for (const auto &item : palette->results().mid(1)) {
                const auto map = item.toMap();
                if (map["kind"] == "web" || map["title"].toString().startsWith("Save session as"))
                    continue;
                if (runs.isEmpty() || runs.last() != map["kind"].toString())
                    runs << map["kind"].toString();
            }
            palette->close();
            if (!runs.contains("file") || !runs.contains("app") || runs.removeDuplicates() > 0)
                return fail(("Spotlight does not list its results by kind: " + runs.join(' ').toStdString()).c_str());
        }
        if (!token("macos").toBool() || token("menuRowHeight").toInt() != 24 ||
            token("menuHighlight") != token("accent") ||
            token("textOnAccentFill").value<QColor>() != QColor(Qt::white))
            return fail("the macOS style's menus are not drawn as macOS draws them");
        QFile::remove(marker);
        auto *root = view.rootObject();
        QQuickItem *launchpad = nullptr;
        auto openLaunchpad = [&] {
            root->setProperty("launcherOpen", true);
            return waitFor([&] {
                launchpad = find(root, "launchpad");
                return launchpad && launchpad->property("progress").toReal() == 1;
            });
        };
        auto closed = [&] {
            return waitFor([&] { return !root->property("launcherOpen").toBool() && !launchpad->isVisible(); });
        };
        if (!openLaunchpad())
            return fail("the launcher did not open as Launchpad in the macOS style");
        if (launchpad->window() != popover ||
            launchpad->mapRectToScene(QRectF(0, 0, launchpad->width(), launchpad->height())) !=
                QRectF(QPointF(0, 0), QSizeF(popover->size())))
            return fail("Launchpad does not cover the whole output");
        auto *field = find(root, "launchpadSearch");
        if (!field || !waitFor([&] { return field->hasActiveFocus(); }))
            return fail("Launchpad's search did not take the keyboard");
        // Pages of two: Action app and Fake app, then Other app and Test app.
        launchpad->setProperty("columns", 2);
        launchpad->setProperty("rows", 1);
        auto at = [&] {
            return QPoint(launchpad->property("page").toInt(), launchpad->property("current").toInt());
        };
        auto press = [&](Qt::Key key) { QTest::keyClick(popover, key); };
        if (launchpad->property("pageCount").toInt() != 2 || at() != QPoint(0, -1) ||
            !find(root, "launchpadDots")->isVisible())
            return fail("Launchpad did not cut four applications into two pages of two");
        press(Qt::Key_Right);
        if (at() != QPoint(0, 0))
            return fail("an arrow did not highlight the page's first application");
        press(Qt::Key_Right);
        press(Qt::Key_Right);
        if (at() != QPoint(1, 2))
            return fail("Right past the page's edge did not go on to the next page");
        press(Qt::Key_Left);
        if (at() != QPoint(0, 1))
            return fail("Left past the page's edge did not go back to the page before");
        press(Qt::Key_PageDown);
        if (at() != QPoint(1, 3))
            return fail("Page Down did not show the next page");
        press(Qt::Key_PageUp);
        if (at() != QPoint(0, 1))
            return fail("Page Up did not show the page before");
        auto wheel = [&](int delta) {
            const QPoint centre(popover->width() / 2, popover->height() / 2);
            QWheelEvent event(centre, popover->mapToGlobal(centre), QPoint(), QPoint(0, delta), Qt::NoButton,
                              Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(popover, &event);
        };
        wheel(-120);
        if (launchpad->property("page").toInt() != 1)
            return fail("a wheel notch down did not show the next page");
        wheel(120);
        if (launchpad->property("page").toInt() != 0)
            return fail("a wheel notch up did not show the page before");
        // Dragging beside the applications, to the left, shows the next page.
        {
            const int y = launchpad->property("gridTop").toInt() - 10;
            const QPoint from(popover->width() - 50, y), to(popover->width() - 450, y);
            QTest::mousePress(popover, Qt::LeftButton, Qt::NoModifier, from);
            for (int step = 1; step <= 10; ++step) {
                QTest::mouseMove(popover, from + (to - from) * step / 10);
                QTest::qWait(10);
            }
            QTest::mouseRelease(popover, Qt::LeftButton, Qt::NoModifier, to);
            if (launchpad->property("page").toInt() != 1 || !root->property("launcherOpen").toBool())
                return fail("dragging Launchpad to the left did not show the next page");
            press(Qt::Key_PageUp);
        }
        // Typing filters the grid, the best match highlighted.
        for (Qt::Key key : {Qt::Key_O, Qt::Key_T, Qt::Key_H})
            press(key);
        auto shown = [&] { return launchpad->property("shown").toList(); };
        if (!waitFor([&] { return shown().size() == 1; }) ||
            shown()[0].toMap()["name"] != "Other app" || at() != QPoint(0, 0) ||
            !find(root, "launchpadApp:paw-test-other.desktop") ||
            find(root, "launchpadApp:paw-test-app.desktop"))
            return fail("typing in Launchpad did not filter its grid");
        press(Qt::Key_Escape);
        if (field->property("text").toString() != "" || shown().size() != 4 ||
            !root->property("launcherOpen").toBool())
            return fail("Escape did not clear Launchpad's search first");
        press(Qt::Key_Escape);
        if (!closed())
            return fail("Escape did not close Launchpad");
        // A click beside the applications closes it.
        if (!openLaunchpad())
            return fail("Launchpad did not open again");
        QTest::mouseClick(popover, Qt::LeftButton, Qt::NoModifier, QPoint(5, popover->height() / 2));
        if (!closed())
            return fail("a click beside Launchpad's applications did not close it");
        // A right click on an application opens its menu, over Launchpad.
        if (!openLaunchpad())
            return fail("Launchpad did not open a third time");
        auto *other = find(root, "launchpadApp:paw-test-other.desktop");
        if (!other)
            return fail("Launchpad does not show the other application");
        click(other, Qt::RightButton);
        auto *appMenu = find(root, "launchpadMenu");
        if (!appMenu || !waitFor([&] { return inPopover(appMenu); }) ||
            !find(root, "launchpad:open") || !find(root, "launchpad:dock"))
            return fail("a right click on an application in Launchpad did not open its menu");
        press(Qt::Key_Escape);
        if (!waitFor([&] { return !appMenu->property("open").toBool(); }) ||
            !root->property("launcherOpen").toBool())
            return fail("Escape in an application's menu closed Launchpad too");
        // The menu key opens the highlighted application's.
        press(Qt::Key_Right);
        press(Qt::Key_Menu);
        if (!waitFor([&] { return inPopover(appMenu); }))
            return fail("the menu key did not open the highlighted application's menu in Launchpad");
        press(Qt::Key_Escape);
        if (!waitFor([&] { return !appMenu->property("open").toBool(); }))
            return fail("Escape did not close the application's menu in Launchpad");
        // Typing and Enter launch the best match, and Launchpad closes.
        for (Qt::Key key : {Qt::Key_T, Qt::Key_E, Qt::Key_S, Qt::Key_T, Qt::Key_Space, Qt::Key_A})
            press(key);
        if (!waitFor([&] { return shown().size() > 0 && shown()[0].toMap()["name"] == "Test app"; }))
            return fail("Launchpad's search did not find the configured launcher first");
        press(Qt::Key_Return);
        if (!waitFor([&] { return QFile::exists(marker); }) || !closed())
            return fail("Enter in Launchpad's search did not launch the best match and close it");
        QFile::remove(marker);
        // A click on an application launches it.
        if (!openLaunchpad())
            return fail("Launchpad did not open a fourth time");
        launchpad->setProperty("columns", 2);
        launchpad->setProperty("rows", 1);
        auto *secondDot = find(root, "launchpadDot:1");
        if (!secondDot)
            return fail("Launchpad has no dot for its second page");
        click(secondDot);
        auto *test = find(root, "launchpadApp:pinned:0");
        if (launchpad->property("page").toInt() != 1 || !test ||
            !waitFor([&] { return test->mapToScene(QPointF(0, 0)).x() >= 0 &&
                                          test->mapToScene(QPointF(0, 0)).x() < popover->width(); }))
            return fail("clicking the second page's dot did not show it");
        click(test);
        if (!waitFor([&] { return QFile::exists(marker); }) || !closed())
            return fail("clicking an application in Launchpad did not launch it and close it");
        QFile::remove(marker);
        if (!rewrite(lua))
            return fail("could not restore the configuration");
        controller.reload();
        if (!waitFor([&] { return controller.style() == "taskbar"; }))
            return fail("the configuration's taskbar style was not read back");
    }
    // The desktop's menu: a right press opens it where it was, with Show desktop and the
    // appearance profiles beside their entry; a press elsewhere closes it.
    {
        ShellView desktop(controller, app.primaryScreen(), true, true);
        desktop.show();
        if (!QTest::qWaitForWindowExposed(&desktop) || !desktop.rootObject())
            return fail("the desktop never showed");
        auto *desktopMenu = find(desktop.rootObject(), "desktopMenu");
        auto entry = [&](const QString &text) {
            std::function<QQuickItem *(QQuickItem *)> walk = [&](QQuickItem *item) -> QQuickItem * {
                for (auto *child : item->childItems()) {
                    if (child->objectName() == "desktopMenuItem" && child->property("text") == text)
                        return child;
                    if (auto *found = walk(child))
                        return found;
                }
                return nullptr;
            };
            return desktopMenu ? walk(desktopMenu) : nullptr;
        };
        const QPoint at(200, 150);
        QTest::mousePress(&desktop, Qt::RightButton, Qt::NoModifier, at);
        QTest::mouseRelease(&desktop, Qt::RightButton, Qt::NoModifier, at);
        auto *card = desktopMenu ? desktopMenu->property("card").value<QQuickItem *>() : nullptr;
        if (!card || !waitFor([&] { return card->isVisible() && card->property("progress").toReal() == 1; }) ||
            !entry("Show desktop") || !entry("Appearance") ||
            card->mapToScene(QPointF(0, 0)).toPoint() != at)
            return fail("a right press on the desktop did not open its menu there");
        QTest::mouseClick(&desktop, Qt::LeftButton, Qt::NoModifier, QPoint(600, 500));
        if (!waitFor([&] { return !card->isVisible(); }))
            return fail("a press beside the desktop's menu did not close it");
    }
    // The macOS style, from a profile: the menu bar along the top in a surface of its own and the
    // dock at the bottom of the panel's surface, in place of the taskbar, laid out anew as the
    // profile changes and back again.
    {
        if (!rewrite(QString(lua).replace("profiles={", "profiles={mac={shell={style='macos',panel_height=64,"
                                                         "panel_margin={bottom=6},panel_radius=20}},")))
            return fail("could not rewrite the configuration");
        controller.reload();
        controller.pickProfile("mac");
        auto *root = view.rootObject();
        MenuBarWindow *menuBar = view.menuBar();
        auto inBar = [&](const QString &name) { return menuBar ? find(menuBar->contentItem(), name) : nullptr; };
        if (!menuBar || !waitFor([&] {
                return menuBar->isVisible() && inBar("menuBar") && find(root, "dock") &&
                       !find(root, "taskList") && !find(root, "bar");
            }))
            return fail("the macOS style did not put a menu bar and a dock in place of the taskbar");
        // The menu bar spans the output; the dock sits in the middle of the panel's surface, lifted
        // off the bottom, which takes the pointer only over it and has room above it to bounce.
        auto *dock = find(root, "dock");
        auto sceneRect = [](QQuickItem *item) {
            return item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
        };
        const int barHeight = root->property("menuBarHeight").toInt();
        if (!waitFor([&] { return root->height() == view.height(); }))
            return fail("the panel did not take the dock's size");
        const QRectF dockRect = sceneRect(dock);
        if (barHeight < 20 || menuBar->height() != barHeight ||
            menuBar->width() != ShellView::previewSize().width() ||
            view.height() != controller.panelExtent() + controller.panelHeadroom() ||
            dockRect.bottom() != view.height() - controller.panelMarginBottom() ||
            dockRect.height() != controller.panelHeight() ||
            std::abs(dockRect.center().x() - view.width() / 2.0) > 1 ||
            view.inputRegion() != QRegion(dockRect.toAlignedRect()))
            return fail("the menu bar and the dock are not laid out as the macOS style has them");
        subscriber->write("mode resize\n");
        if (!waitFor(
                [&] { return inBar("bindingMode") && inBar("bindingMode")->isVisible(); }))
            return fail("the menu bar does not show the binding mode in use");
        subscriber->write("mode default\n");
        if (!waitFor([&] { return !inBar("bindingMode")->isVisible(); }))
            return fail("the menu bar shows the binding mode after it was left");

        // Windows of three: two of the fake application's and one of the application with actions.
        for (const auto &app : controller.pinned())
            controller.unpin(app.toMap()["appId"].toString());
        controller.power()->setAvailable("lock,suspend,reboot,poweroff,logout");
        const QString roles = ", minimized: false, urgent: false, maximized: false, fullscreen: false, "
                              "output: '" + output + "', workspace: 2, sticky: false, floating: false, tiling: true, "
                              "above: false})";
        editTasks("model.clear(); "
                  "model.append({taskId: 31, title: 'Fake window', appId: 'fake', active: true" + roles + "; "
                  "model.append({taskId: 32, title: 'Action window', appId: 'paw-test-actions', active: false" + roles + "; "
                  "model.append({taskId: 33, title: 'Second fake', appId: 'fake', active: false" + roles);
        taskRequests();
        // The focused window's application is named in bold, "Desktop" while none is focused.
        auto *appTitle = inBar("appMenuTitle");
        auto named = [&](const QString &name) {
            return waitFor([&] { return appTitle && appTitle->property("text").toString() == name; });
        };
        if (!named("Fake app"))
            return fail("the menu bar does not name the focused window's application");
        editTasks("model.setProperty(0, 'active', false); model.setProperty(1, 'active', true)");
        if (!named("Action app"))
            return fail("the menu bar's application did not follow the focus");
        editTasks("model.setProperty(1, 'active', false)");
        if (!named("Desktop"))
            return fail("the menu bar does not say Desktop while no window is focused");
        editTasks("model.setProperty(0, 'active', true)");
        if (!named("Fake app"))
            return fail("the menu bar did not name the application focused again");

        // Each menu opens on a press, below its title with their left edges in line, in the
        // popover, which takes the keyboard and every press but those on the bars.
        auto *barMenu = find(root, "menuBarMenu");
        // A row of the open menu, by its name or its text.
        auto entryOf = [&](const QString &name) -> QQuickItem * {
            std::function<QQuickItem *(QQuickItem *)> walk = [&](QQuickItem *item) -> QQuickItem * {
                for (auto *child : item->childItems()) {
                    if (child->isVisible() && (child->objectName() == name || child->property("text") == name))
                        return child;
                    if (auto *found = walk(child))
                        return found;
                }
                return nullptr;
            };
            return barMenu ? walk(barMenu) : nullptr;
        };
        auto openMenu = [&](const QString &kind) {
            auto *title = inBar(kind + "MenuTitle");
            if (!title)
                return false;
            QTest::mousePress(menuBar, Qt::LeftButton, Qt::NoModifier, centre(title));
            QTest::mouseRelease(menuBar, Qt::LeftButton, Qt::NoModifier, centre(title));
            barMenu = find(root, "menuBarMenu");
            return waitFor([&] {
                auto *card = barMenu ? barMenu->property("card").value<QQuickItem *>() : nullptr;
                if (!card || card->property("progress").toReal() != 1 || root->property("menuBarMenu") != kind)
                    return false;
                const QRectF at = sceneRect(card);
                return std::abs(at.left() - title->mapToScene(QPointF(0, 0)).x()) < 1 && at.top() >= barHeight &&
                       at.top() <= barHeight + 8 && popover->isVisible();
            });
        };
        auto chosen = [&](const QString &entry) {
            QQuickItem *row = nullptr;
            if (!waitFor([&] { return (row = entryOf(entry)) != nullptr; }))
                return false;
            click(row);
            return waitFor([&] { return root->property("menuBarMenu").toString().isEmpty(); });
        };
        if (!openMenu("app") || !waitFor([&] {
                return entryOf("appMenuNewWindow") && entryOf("appMenuHideOthers") && entryOf("appMenuHide") &&
                       entryOf("appMenuQuit") && entryOf("appMenuHide")->property("text") == "Hide Fake app" &&
                       entryOf("appMenuQuit")->property("text") == "Quit Fake app";
            }))
            return fail("the application's menu did not open below its name with its entries");
        // The popover leaves holes for the menu bar's strip and the dock.
        const QRegion holes = popover->inputRegion();
        const QPointF dockInPopover = dockRect.center() + QPointF(0, popover->height() - view.height());
        if (holes.contains(QPoint(500, barHeight / 2)) || holes.contains(dockInPopover.toPoint()) ||
            !holes.contains(QPoint(10, popover->height() - 10)) || !holes.contains(QPoint(500, 300)))
            return fail("the popover takes presses on the menu bar or the dock");
        // Launchpad covers both bars, and takes every press.
        root->setProperty("launcherOpen", true);
        if (!waitFor([&] { return popover->inputRegion().contains(QPoint(500, barHeight / 2)) &&
                                          popover->inputRegion().contains(dockInPopover.toPoint()); }))
            return fail("the popover leaves the bars' strips to them while Launchpad covers them");
        root->setProperty("launcherOpen", false);
        if (!openMenu("app"))
            return fail("the application's menu did not open again after Launchpad");
        if (!chosen("appMenuQuit") || taskRequests() != "close 31|close 33")
            return fail("quitting the application did not close its windows");
        if (!openMenu("app") || !chosen("appMenuHide") || taskRequests() != "minimize 31|minimize 33")
            return fail("hiding the application did not minimize its windows");
        // Moving onto another title while a menu is open opens that one; Left and Right step
        // through them.
        if (!openMenu("app"))
            return fail("the application's menu did not open again");
        QTest::mouseMove(menuBar, centre(inBar("windowMenuTitle")));
        if (!waitFor([&] { return root->property("menuBarMenu") == "window" && entryOf("windowMenu:minimize"); }))
            return fail("moving onto the Window menu's title did not open it in place of the other");
        QTest::keyClick(popover, Qt::Key_Right);
        if (!waitFor([&] { return root->property("menuBarMenu") == "system" && entryOf("systemMenu:lock"); }))
            return fail("Right did not go on to the system menu");
        QTest::keyClick(popover, Qt::Key_Left);
        if (!waitFor([&] { return root->property("menuBarMenu") == "window"; }))
            return fail("Left did not go back to the Window menu");
        if (!chosen("windowMenu:minimize") || taskRequests() != "minimize 31")
            return fail("the Window menu did not minimize the focused window");
        requests.clear();
        if (!openMenu("window") || !chosen("windowMenu:left") || taskRequests() != "activate 31" ||
            !waitFor([&] { return requests.contains("snap_left"); }))
            return fail("the Window menu did not tile the focused window to the left");
        if (!openMenu("window") || !chosen("windowMenu:fullscreen") || taskRequests() != "fullscreen 31 true")
            return fail("the Window menu did not make the window fullscreen");
        if (!openMenu("window") || !waitFor([&] {
                return entryOf("windowMenu:above") && entryOf("windowMenu:above")->property("text") == "Keep Above Others";
            }) || !chosen("windowMenu:above") || taskRequests() != "above 31 true")
            return fail("the Window menu did not keep the window above the others");
        // The system menu: the appearance profiles beside it, and the power actions.
        if (!openMenu("system") ||
            !waitFor([&] { return entryOf("systemMenuAppearance") && entryOf("Restart…") && entryOf("Shut Down…"); }) ||
            !chosen("systemMenu:lock") ||
            !waitFor([&] { return requests.contains("lock"); }))
            return fail("the system menu did not lock the screen");
        if (!waitFor([&] { return !popover->isVisible(); }))
            return fail("the popover stayed after the menu bar's menus closed");
        // Displays… opens the display settings window on this monitor.
        if (!openMenu("system") || !chosen("systemMenuDisplays") ||
            !waitFor([&] { return controller.displaySettings()->open(); }) ||
            controller.displaySettings()->output() != output)
            return fail("the system menu's Displays… did not open the display settings window");
        controller.displaySettings()->close();
        if (!waitFor([&] { return !popover->isVisible(); }))
            return fail("the popover stayed after the system menu opened the display settings");

        // The clock and Quick Settings open their popups at the output's right edge below the
        // menu bar; search opens the command palette there, and closes it again.
        auto rightUnderBar = [&](const char *name) {
            auto *popup = find(root, name);
            if (!popup)
                return false;
            const QRectF at = sceneRect(popup);
            const QVariant progress = popup->property("progress");
            return popup->isVisible() && progress.toReal() == 1 && at.top() >= barHeight &&
                   at.bottom() <= popover->height() - controller.panelExtent() &&
                   at.right() >= popover->width() - 16 && at.right() <= popover->width();
        };
        click(inBar("clockButton"));
        if (!waitFor([&] { return rightUnderBar("calendar"); }))
            return fail("the clock did not open its flyout at the right below the menu bar");
        click(inBar("quickSettingsButton"));
        if (!waitFor([&] { return rightUnderBar("quickSettings") && !find(root, "calendar")->isVisible(); }))
            return fail("the Quick Settings button did not open them at the right below the menu bar");
        click(inBar("quickSettingsButton"));
        if (!waitFor([&] { return !popover->isVisible(); }))
            return fail("Quick Settings did not close on a second click");
        click(inBar("searchButton"));
        if (!waitFor([&] { return controller.palette()->output() == output; }))
            return fail("the search button did not open the command palette");
        click(inBar("searchButton"));
        if (!waitFor([&] { return controller.palette()->output().isEmpty(); }))
            return fail("the search button did not close the command palette");

        // A failure shows across the menu bar until dismissed.
        controller.launch("not-installed.desktop");
        auto *error = inBar("menuBarError");
        if (!error || !waitFor([&] { return error->isVisible(); }))
            return fail("a failure did not show across the menu bar");
        controller.clearError();
        if (!waitFor([&] { return !error->isVisible(); }))
            return fail("a failure dismissed stayed on the menu bar");

        // The dock: the applications button, the pinned applications (the configured launcher),
        // each running application once with a dot under it, a line, and the Trash.
        editTasks("model.setProperty(0, 'minimized', false); model.setProperty(2, 'minimized', false)");
        auto icon = [&](const QString &name) { return find(root, name); };
        auto dotted = [&](QQuickItem *app) {
            auto *dot = app ? find(app, "dockDot") : nullptr;
            return dot && dot->isVisible();
        };
        auto leftOf = [&](const QString &left, const QString &right) {
            return icon(left) && icon(right) && centre(icon(left)).x() < centre(icon(right)).x();
        };
        if (!waitFor([&] {
                return leftOf("dockLauncher", "dockApp:pinned:0") && leftOf("dockApp:pinned:0", "dockApp:fake") &&
                       leftOf("dockApp:fake", "dockApp:paw-test-actions") &&
                       leftOf("dockApp:paw-test-actions", "dockSeparator") && leftOf("dockSeparator", "dockTrash");
            }) ||
            !dotted(icon("dockApp:fake")) || !dotted(icon("dockApp:paw-test-actions")) ||
            dotted(icon("dockApp:pinned:0")) || !icon("dockApp:fake")->property("stacked").toBool())
            return fail("the dock does not list the pinned applications, then the running ones, then the Trash");
        // A click on a running application brings up its next window.
        click(icon("dockApp:fake"));
        if (!waitFor([&] { return taskRequests() == "activate 33"; }))
            return fail("clicking a running application on the dock did not bring up its next window");
        // Resting on one with several windows lists them above it, pictures of windows or not
        // (shell.thumbnails is on).
        QTest::mouseMove(&view, centre(icon("dockApp:fake")));
        auto *groupList = find(root, "groupList");
        if (!waitFor([&] { return groupList && groupList->isVisible() && groupList->property("progress").toReal() == 1; }) ||
            sceneRect(groupList).bottom() > popover->height() - view.height() + dockRect.top())
            return fail("resting on an application with two windows did not list them above the dock");
        if (!controller.thumbnails() || root->property("thumbnailsOpen").toBool() ||
            (find(root, "windowThumbnails") && find(root, "windowThumbnails")->isVisible()))
            return fail("the dock shows pictures of an application's windows");
        QTest::mouseMove(&view, QPoint(10, 10));
        if (!waitFor([&] { return !root->property("groupOpen").toBool(); }))
            return fail("the windows listed on the dock did not go with the pointer");
        // A drag resting on an application's icon brings its window forward, or lists its windows
        // when it has several, where resting on one brings it forward; on one without windows it
        // does nothing, and the dock takes no drop.
        {
            taskRequests();
            if (dragTo(&view, centre(icon("dockApp:paw-test-actions"))) ||
                !waitFor([&] { return taskRequests() == "activate 32"; }) ||
                root->property("groupOpen").toBool())
                return fail("a drag resting on an application with one window on the dock did not bring it forward");
            dragTo(&view, centre(icon("dockApp:pinned:0")));
            if (!stays([&] { return taskRequests().isEmpty() && !root->property("groupOpen").toBool(); }))
                return fail("a drag resting on an application without windows on the dock did something");
            dragTo(&view, centre(icon("dockApp:fake")));
            if (!waitFor([&] { return groupList->isVisible() && windowItem(groupList, "groupWindow", 33); }))
                return fail("a drag resting on an application with two windows on the dock did not list them");
            if (dragTo(popover, centre(windowItem(groupList, "groupWindow", 33))) ||
                !waitFor([&] { return taskRequests() == "activate 33"; }))
                return fail("a drag resting on a window listed on the dock did not bring it forward");
            dragGone();
            if (!waitFor([&] { return !root->property("groupOpen").toBool(); }))
                return fail("the windows a drag listed on the dock stayed once it had gone");
        }
        // Its menu opens above it: its windows, then keeping it in the dock, hiding and quitting.
        auto *contextMenu = find(root, "contextMenu");
        auto contextEntry = [&](const QString &name) -> QQuickItem * {
            std::function<QQuickItem *(QQuickItem *)> walk = [&](QQuickItem *item) -> QQuickItem * {
                for (auto *child : item->childItems()) {
                    if (child->isVisible() && (child->objectName() == name || child->property("text") == name) &&
                        child->objectName().startsWith("contextMenu"))
                        return child;
                    if (auto *found = walk(child))
                        return found;
                }
                return nullptr;
            };
            return contextMenu ? walk(contextMenu) : nullptr;
        };
        click(icon("dockApp:fake"), Qt::RightButton);
        if (!waitFor([&] {
                auto *card = contextMenu->property("card").value<QQuickItem *>();
                return card && card->property("progress").toReal() == 1 && contextEntry("Fake window") &&
                       contextEntry("Second fake") && contextEntry("contextMenuKeep") &&
                       sceneRect(card).bottom() <= popover->height() - view.height() + dockRect.top() &&
                       std::abs(sceneRect(card).center().x() - centre(icon("dockApp:fake")).x()) < 2;
            }))
            return fail("an application's menu on the dock did not open above it with its windows");
        click(contextEntry("contextMenuKeep"));
        if (!waitFor([&] { return icon("dockApp:paw-test-app.desktop") && !icon("dockApp:fake"); }) ||
            !controller.isPinned("paw-test-app.desktop") || !dotted(icon("dockApp:paw-test-app.desktop")))
            return fail("keeping an application in the dock did not pin it");
        // A pinned application without windows starts on a click and bounces, three times at most,
        // or until a window of it opens.
        auto bouncing = [&](const QString &name) { return icon(name) && icon(name)->property("bouncing").toBool(); };
        // A bounce can come and go between two looks; a spy sees it.
        QSignalSpy bounced(icon("dockApp:pinned:0"), SIGNAL(bouncingChanged()));
        click(icon("dockApp:pinned:0"));
        if (!waitFor([&] { return bounced.count() > 0; }) ||
            !waitFor([&] { return !bouncing("dockApp:pinned:0") && icon("dockApp:pinned:0")->property("lift").toReal() == 0; }, 5000))
            return fail("a pinned application's icon did not bounce as it started, or did not stop");
        controller.pin("paw-test-other.desktop");
        // Grown in, and laid out after the other pinned applications: the row places a new icon
        // a moment after it comes, at its start until then.
        if (!waitFor([&] {
                auto *other = icon("dockApp:paw-test-other.desktop");
                auto *first = icon("dockApp:pinned:0");
                return other && first && other->property("grow").toReal() == 1 &&
                       other->mapToScene(QPointF(0, 0)).x() > first->mapToScene(QPointF(first->width(), 0)).x();
            }))
            return fail("a pinned application did not come onto the dock");
        click(icon("dockApp:paw-test-other.desktop"));
        if (!waitFor([&] { return bouncing("dockApp:paw-test-other.desktop"); }))
            return fail("starting an application from the dock did not bounce its icon");
        editTasks("model.append({taskId: 34, title: 'Other window', appId: 'paw-test-other', active: false" + roles);
        if (!waitFor([&] { return icon("dockApp:paw-test-other.desktop")->property("bouncesLeft").toInt() <= 1; }) ||
            !waitFor([&] { return !bouncing("dockApp:paw-test-other.desktop"); }))
            return fail("an icon went on bouncing after its application's window opened");
        // The Trash shows whether it holds anything.
        QDir(screens.path()).mkpath("data/Trash/files");
        QFile trashed(screens.filePath("data/Trash/files/old.txt"));
        if (!trashed.open(QIODevice::WriteOnly) || trashed.write("x") < 0)
            return fail("could not put a file in the trash");
        trashed.close();
        if (!waitFor([&] { return icon("dockTrash")->property("iconName") == "user-trash-full"; }, 10000))
            return fail("the dock's Trash did not fill as a file went into the trash");
        trashed.remove();
        if (!waitFor([&] { return icon("dockTrash")->property("iconName") == "user-trash"; }, 10000))
            return fail("the dock's Trash did not empty as the trash did");

        // The keyboard on the dock, as Control-F3 gives macOS's: it starts at the focused window's
        // application, its windows listed at once, ringed and named on its tag where there is no
        // list; Up goes into the list from the row nearest the dock, Enter brings a window up.
        {
            auto *keys = root->property("barKeys").value<QQuickItem *>();
            auto ask = [&] { subscriber->write(("taskbar " + output + "\n").toUtf8()); };
            auto on = [&] { return keys->property("active").toBool(); };
            auto selected = [&] { return keys->property("button").value<QQuickItem *>(); };
            auto ringed = [&](QQuickItem *item) {
                auto *ring = item ? find(item, "dockFocusRing") : nullptr;
                return ring && ring->isVisible();
            };
            auto tagged = [](QQuickItem *item) {
                for (auto *child : item->children())
                    if (child->inherits("QQuickToolTip") && child->property("visible").toBool())
                        return true;
                return false;
            };
            QEvent leaveDock(QEvent::Leave), leavePopover(QEvent::Leave);
            QCoreApplication::sendEvent(&view, &leaveDock);
            QCoreApplication::sendEvent(popover, &leavePopover);
            taskRequests();
            auto *fake = icon("dockApp:paw-test-app.desktop");
            ask();
            if (!waitFor([&] { return on() && popover->keyboard(); }) || selected() != fake ||
                !ringed(fake) || !waitFor([&] { return inPopover(groupList) && windowItem(groupList, "groupWindow", 33); }))
                return fail("taskbar_focus did not give the keyboard to the dock at the focused application, its windows listed");
            QTest::keyClick(popover, Qt::Key_Up);
            auto *row = windowItem(groupList, "groupWindow", 33);
            if (keys->property("window").toInt() != 1 || !row || !find(row, "groupWindowFocusRing")->isVisible() ||
                ringed(fake))
                return fail("Up did not go into the list of the application's windows on the dock");
            QTest::keyClick(popover, Qt::Key_Down);
            QTest::keyClick(popover, Qt::Key_Right);
            auto *other = icon("dockApp:paw-test-other.desktop");
            if (selected() != other || !ringed(other) || root->property("groupOpen").toBool() ||
                !waitFor([&] { return tagged(other); }))
                return fail("Right did not go on to the next application on the dock, named on its tag");
            QTest::keyClick(popover, Qt::Key_Left);
            QTest::keyClick(popover, Qt::Key_Up);
            QTest::keyClick(popover, Qt::Key_Return);
            if (!waitFor([&] { return taskRequests() == "activate 33"; }) ||
                !waitFor([&] { return !on() && !popover->keyboard(); }))
                return fail("Enter on a window listed on the dock did not bring it up and give the keyboard back");
            if (!waitFor([&] { return !popover->isVisible(); }))
                return fail("the popover stayed once the keyboard left the dock");
        }

        // Back to a taskbar profile: the menu bar goes, and the taskbar is as it was.
        controller.pickProfile("dark");
        if (!waitFor([&] { return !menuBar->isVisible() && find(root, "bar") && !find(root, "dock"); }) ||
            view.height() != controller.panelExtent() || !view.inputRegion().isEmpty())
            return fail("the taskbar did not come back in place of the macOS style's bars");
        if (!rewrite(lua))
            return fail("could not restore the configuration");
        controller.reload();
    }
    std::cout << "Hover/click, launcher keyboard focus, search, command launch, tiling toggle, and "
                 "workspace indicator, task and bar context menus, pinning a window's application, "
                 "reordering pins, a pinned application's windows moving apart, "
                 "task reordering, grouped windows, the keyboard on the bar, the volume control, the "
                 "command palette, and the "
                 "notification bell, cards and history, the tray, the design tokens, and the macOS style's "
                 "menu bar and dock passed\n";
}
