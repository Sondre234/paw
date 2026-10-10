// SPDX-License-Identifier: GPL-3.0-or-later
#include "controller.hpp"
#include "icons.hpp"
#include "picker_view.hpp"
#include "preview.hpp"
#include "version.h"
#include "view.hpp"
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <string_view>
#include <QScreen>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QTimer>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>

namespace {
volatile sig_atomic_t signalFd = -1;
void onSignal(int number) {
    int previous = errno;
    char byte = number == SIGHUP ? 'r' : 'q';
    if (signalFd >= 0) {
        auto ignored = write(signalFd, &byte, 1);
        (void)ignored;
    }
    errno = previous;
}
// The view, once its QML has loaded; Qt's errors are printed when it did not.
template <class View> std::unique_ptr<View> loaded(std::unique_ptr<View> view) {
    if (view->status() == QQuickView::Error) {
        for (const auto &error : view->errors())
            std::cerr << error.toString().toStdString() << '\n';
        throw std::runtime_error("could not load shell QML");
    }
    return view;
}
} // namespace
int main(int argc, char **argv) {
    // The compositor sets this so libGLX skips loading the GPU driver, which software rendering
    // does not need. Applications launched from here get the original value back.
    if (const char *vendor = std::getenv("__GLX_VENDOR_LIBRARY_NAME");
        vendor && std::string_view(vendor) == "paw-none") {
        if (const char *saved = std::getenv("PAW_GLX_VENDOR"))
            setenv("__GLX_VENDOR_LIBRARY_NAME", saved, 1);
        else
            unsetenv("__GLX_VENDOR_LIBRARY_NAME");
        unsetenv("PAW_GLX_VENDOR");
    }
    // The compositor of a login session asks its shell to be the session's polkit agent; the
    // applications it launches do not see that.
    const bool polkitAgent = qgetenv("PAW_POLKIT_AGENT") == "1";
    unsetenv("PAW_POLKIT_AGENT");
    // Answered before Qt connects to a display, so that it works from a text console too.
    for (int i = 1; i < argc; ++i)
        if (std::string_view(argv[i]) == "--version" || std::string_view(argv[i]) == "-v") {
            std::cout << "paw-shell " PAW_VERSION "\n";
            return 0;
        }
    QGuiApplication app(argc, argv);
    // Views come and go with outputs (all of them during a VT switch); the shell's lifetime
    // follows the compositor connection instead.
    QGuiApplication::setQuitOnLastWindowClosed(false);
    QCoreApplication::setApplicationName("paw-shell");
    QCoreApplication::setApplicationVersion(PAW_VERSION);
    QGuiApplication::setDesktopFileName("paw-shell");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"config", "Lua configuration file", "path"});
    parser.addOption({"preview", "Open a normal window for UI development"});
    parser.addOption({"preview-desktop", "Preview the desktop instead of the taskbar"});
    parser.addOption({"preview-popup",
                      "Preview the taskbar with one popup open, on stand-in windows, sound, "
                      "tray items and notifications: bar (none), launcher, launcher-all, "
                      "launcher-search, launcher-empty, launcher-menu, power, bar-menu, bar-submenu, "
                      "task-menu, stack-menu, pin-menu, group, thumbnails, keyboard, tray-menu, "
                      "tray-submenu, calendar, clock-empty, calendar-years, mixer, outputs, "
                      "profiles, wallpapers, notifications, wifi, "
                      "quick-settings, quick-settings-mixer, quick-settings-power, "
                      "quick-settings-wifi, quick-settings-bluetooth or quick-settings-pairing; in "
                      "the macOS style system-menu, "
                      "app-menu, window-menu or window-submenu too; or an overlay over the bar: "
                      "osd-volume, osd-text, osd-microphone, cards, power-dialog, auth-dialog, "
                      "palette, palette-empty, palette-calculator, switcher, overview, "
                      "snap-assist, display-mode, palette-files, clipboard, emoji, display-settings "
                      "or display-settings-trial",
                      "name"});
    parser.addOption(
        {"quit-after",
         "Exit after this many milliseconds (for UI tests); with --preview-popup, counted from "
         "the popup opening",
         "milliseconds"});
    parser.addOption({"screenshot", "Save a preview screenshot before exiting", "path"});
    parser.addOption({"icon-theme",
                      "Look icons up in this theme instead of the platform's (a preview on the "
                      "offscreen platform has none)",
                      "name"});
    parser.process(app);
    if (parser.isSet("icon-theme")) {
        // The offscreen platform looks for icon themes in no folder but Qt's resources.
        QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() +
                                   QStandardPaths::locateAll(QStandardPaths::GenericDataLocation,
                                                             "icons",
                                                             QStandardPaths::LocateDirectory));
        QIcon::setThemeName(parser.value("icon-theme"));
    } else {
        useDesktopIconTheme();
    }
    if (!parser.isSet("config"))
        parser.showHelp(1);
    const bool preview = parser.isSet("preview") || parser.isSet("preview-popup");
#if !PAW_LAYER_SHELL
    if (!preview) {
        std::cerr
            << "This build supports --preview only; build with LayerShellQt for a desktop shell\n";
        return 1;
    }
#endif
    if (!preview && QGuiApplication::platformName() != "wayland") {
        std::cerr << "paw-shell requires the Qt Wayland platform\n";
        return 1;
    }
    try {
        int quitAfter = 0;
        if (parser.isSet("quit-after")) {
            bool ok = false;
            quitAfter = parser.value("quit-after").toInt(&ok);
            if (!ok || quitAfter < 1)
                throw std::runtime_error("--quit-after must be a positive integer");
        }
        ShellController controller(parser.value("config").toStdString());
        if (!controller.enabled())
            return 0;
        // shell.renderer = "software" spares a weak machine Qt's GL/Vulkan set-up, which costs
        // startup time, memory and threads, at the price of effects. The environment's choice,
        // if any, wins.
        if (controller.softwareRenderer() && !qEnvironmentVariableIsSet("QT_QUICK_BACKEND") &&
            !qEnvironmentVariableIsSet("QSG_RHI_BACKEND"))
            QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        QObject::connect(&controller, &ShellController::disabled, &app, &QCoreApplication::quit);
        QObject::connect(controller.tasks(), &TaskModel::disconnected, &app,
                         &QCoreApplication::quit);
        if (!preview && !controller.tasks()->connectDisplay()) {
            std::cerr << "The compositor must support foreign-toplevel-management\n";
            return 1;
        }
        // Without data-control the clipboard history keeps nothing, quietly.
        if (!preview && !controller.clipboard()->connectDisplay())
            std::cerr << "The compositor offers no ext-data-control-v1: no clipboard history\n";
        qmlRegisterUncreatableType<TaskModel>("Paw", 1, 0, "TaskModel", "Provided by the shell");
        // Made before the views, which refer to it, and so destroyed after them.
        std::unique_ptr<PreviewData> previewData;
        if (parser.isSet("preview-popup"))
            previewData = std::make_unique<PreviewData>(controller);
        std::vector<std::unique_ptr<ShellView>> views;
        // The other surfaces of each output: the overlays, the cards, the on-screen display, the
        // display mode popup and the configuration error banner.
        std::vector<std::pair<QScreen *, std::unique_ptr<QQuickView>>> overlays;
        // --quit-after's end, with --screenshot's picture of the first view.
        auto quit = [&] {
            if (!parser.isSet("screenshot")) {
                app.quit();
                return;
            }
            QImage shot = views.front()->grabWindow();
            if (preview && !parser.isSet("preview-desktop")) {
                auto *popover = views.front()->popover();
                auto *menuBar = views.front()->menuBar();
                shot = previewOnDesktop(shot,
                                        popover && popover->isVisible() ? popover->grabWindow()
                                                                        : QImage(),
                                        menuBar && menuBar->isVisible() ? menuBar->grabWindow()
                                                                        : QImage(),
                                        controller.panelSurfaceTop(), controller);
                if (previewData)
                    shot = previewData->withSurface(shot);
            }
            if (!shot.save(parser.value("screenshot")))
                app.exit(1);
            else
                app.quit();
        };
        auto addScreen = [&](QScreen *screen) {
            // Qt's stand-in while the compositor has no outputs has no wl_output to attach to.
            if (!preview && screen->name().isEmpty())
                return;
            for (bool desktop : {true, false}) {
                if (preview && desktop != parser.isSet("preview-desktop"))
                    continue;
                auto view = loaded(std::make_unique<ShellView>(controller, screen, desktop, preview));
                auto reportFrame = [window = view.get()] {
                    QObject::connect(
                        window, &QQuickWindow::frameSwapped, window,
                        [window] {
                            std::cerr
                                << "paw surface rendered: " << window->title().toStdString()
                                << '\n';
                        },
                        Qt::SingleShotConnection);
                };
                reportFrame();
                QObject::connect(&controller, &ShellController::configChanged, view.get(),
                                 reportFrame);
                if (previewData && !desktop) {
                    previewData->fill(view->rootObject());
                    // Once the bar is laid out, so the popup opens by its button.
                    QObject::connect(
                        view.get(), &QQuickWindow::frameSwapped, &app,
                        [&, root = view->rootObject()] {
                            const auto name = parser.value("preview-popup");
                            if (!previewData->open(root, name)) {
                                std::cerr << "paw-shell: no popup to preview called "
                                          << name.toStdString() << '\n';
                                app.exit(1);
                            } else if (quitAfter > 0) {
                                QTimer::singleShot(quitAfter, &app, quit);
                            }
                        },
                        Qt::ConnectionType(Qt::QueuedConnection | Qt::SingleShotConnection));
                }
                view->show();
                if (preview && !desktop && !previewData)
                    view->rootObject()->setProperty("launcherOpen", true);
                views.push_back(std::move(view));
            }
            if (!preview) {
                auto add = [&](std::unique_ptr<QQuickView> view) {
                    overlays.emplace_back(screen, loaded(std::move(view)));
                };
                add(std::make_unique<SwitcherView>(controller, screen));
                add(std::make_unique<PickerView>(controller, screen, "palette", "Palette.qml",
                                                 controller.palette()));
                add(std::make_unique<PickerView>(controller, screen, "clipboard",
                                                 "ClipboardPicker.qml", controller.clipboard()));
                add(std::make_unique<PickerView>(controller, screen, "emoji", "EmojiPicker.qml",
                                                 controller.emoji()));
                add(std::make_unique<PowerView>(controller, screen));
                add(std::make_unique<AuthView>(controller, screen));
                add(std::make_unique<DisplaySettingsView>(controller, screen));
                add(std::make_unique<OverviewView>(controller, screen));
                add(std::make_unique<CardsView>(controller, screen));
                add(std::make_unique<OsdView>(controller, screen));
                add(std::make_unique<DisplayModeView>(controller, screen));
                add(std::make_unique<ConfigErrorView>(controller, screen));
            }
        };
        for (auto *screen : QGuiApplication::screens()) {
            addScreen(screen);
            if (preview)
                break;
        }
        if (views.empty())
            throw std::runtime_error("no output available");
        // The daemon answers once the surfaces to show its cards exist. A preview stays off the
        // session bus unless asked to.
        if (!preview || qEnvironmentVariableIsSet("PAW_PREVIEW_DBUS")) {
            controller.startNotifications();
            controller.startTray();
            if (polkitAgent)
                controller.startPolkit();
        }
        QObject::connect(&app, &QGuiApplication::screenAdded, &app, [&](QScreen *screen) {
            if (preview)
                return;
            try {
                addScreen(screen);
            } catch (const std::exception &error) {
                std::cerr << error.what() << '\n';
            }
        });
        QObject::connect(&app, &QGuiApplication::screenRemoved, &app, [&](QScreen *screen) {
            std::erase_if(views,
                          [screen](const auto &view) { return view->outputScreen() == screen; });
            std::erase_if(overlays, [screen](const auto &overlay) { return overlay.first == screen; });
        });
        int pipeFds[2];
        if (pipe2(pipeFds, O_NONBLOCK | O_CLOEXEC) < 0)
            throw std::runtime_error("cannot create signal pipe");
        signalFd = pipeFds[1];
        struct sigaction action{};
        action.sa_handler = onSignal;
        sigemptyset(&action.sa_mask);
        for (int signal : {SIGHUP, SIGTERM, SIGINT})
            sigaction(signal, &action, nullptr);
        QSocketNotifier notifier(pipeFds[0], QSocketNotifier::Read);
        QObject::connect(&notifier, &QSocketNotifier::activated, &app, [&] {
            char bytes[64];
            ssize_t count = read(pipeFds[0], bytes, sizeof(bytes));
            for (ssize_t i = 0; i < count; ++i)
                if (bytes[i] == 'r')
                    controller.reload();
                else
                    app.quit();
        });
        // A previewed popup starts the clock when it opens instead.
        if (quitAfter > 0 && !previewData)
            QTimer::singleShot(quitAfter, &app, quit);
        std::cerr << "paw shell ready: " << views.size() << " surfaces, drawn "
                  << (controller.effects() ? "on the GPU" : "in software") << '\n';
        int result = app.exec();
        signalFd = -1;
        close(pipeFds[0]);
        close(pipeFds[1]);
        return result;
    } catch (const std::exception &error) {
        std::cerr << "paw-shell: " << error.what() << '\n';
        return 1;
    }
}
