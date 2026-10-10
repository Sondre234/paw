// SPDX-License-Identifier: GPL-3.0-or-later
#include "controller.hpp"
#include "icons.hpp"
#include "image_provider.hpp"
#include "wallpapers.hpp"
#include <QQmlContext>
#include <QQmlEngine>
#include <QClipboard>
#include <QRegularExpression>
#include <QDir>
#include <QGuiApplication>
#include <QScreen>
#include <QStandardPaths>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QIcon>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QTimer>
#include <algorithm>
#include <functional>
#include <gio/gdesktopappinfo.h>
#include <iostream>
#include <memory>
#if PAW_DBUS || PAW_TRAY
#include "dbus_util.hpp"
#endif
#if PAW_DBUS
#include "notification_service.hpp"
#endif
#if PAW_TRAY
#include "tray_host.hpp"
#endif
#if PAW_POLKIT
#include "polkit_agent.hpp"
#endif

namespace {
// image://tray/SERIAL/REVISION: a tray item's icon, or the stand-in for an item without one;
// image://tray/SERIAL/menu/ID/REVISION: an entry of its menu. The revision only keeps QML from
// reusing a picture that has changed.
ImageProvider::Picture trayImage(TrayModel &model) {
    return [&model](const QString &id, const QSize &requested) {
        const QSize wanted = requested.isEmpty() ? QSize(22, 22) : requested;
        const QStringList parts = id.split('/');
        if (parts.value(1) == "menu") {
            QImage image =
                model.menuPicture(parts.value(0).toInt(), parts.value(2).toInt(), wanted);
            // An icon that is not found leaves the space empty.
            if (image.isNull()) {
                image = QImage(wanted, QImage::Format_ARGB32_Premultiplied);
                image.fill(Qt::transparent);
            }
            return image;
        }
        QImage image = model.picture(parts.value(0).toInt(), wanted);
        return image.isNull() ? Icons().requestImage("application-x-executable", nullptr, wanted)
                              : image;
    };
}
} // namespace
ShellController::ShellController(std::filesystem::path path, QObject *parent)
    : QObject(parent), path_(std::move(path)), tasks_(this) {
    loadConfig();
    watchTrash();
    QFile pins(pinsPath());
    if (pins.open(QIODevice::ReadOnly | QIODevice::Text))
        for (const auto &line : QString::fromUtf8(pins.readAll()).split('\n', Qt::SkipEmptyParts))
            if (!userPins_.contains(line.trimmed()))
                userPins_.push_back(line.trimmed());
    // One line per profile: its name, the configured wallpaper and the picked one, by tabs.
    QFile picked(pickedWallpapersPath());
    if (picked.open(QIODevice::ReadOnly | QIODevice::Text))
        for (const auto &line : QString::fromUtf8(picked.readAll()).split('\n', Qt::SkipEmptyParts)) {
            const auto fields = line.split('\t');
            if (fields.size() == 3 && !fields[2].isEmpty())
                pickedWallpapers_[fields[0]] = {fields[1], fields[2]};
        }
    // The start menu follows the applications and the taskbar's pins, which seed its own.
    connect(this, &ShellController::appsChanged, &startMenu_,
            [this] { startMenu_.setApps(apps(), userPins_); });
    connect(&startMenu_, &StartMenu::failed, this, &ShellController::report);
    connect(&startMenu_, &StartMenu::installedChanged, this, &ShellController::refreshApps);
    startMenu_.setFiles(&files_);
    configureSearch();
    connect(&clipboard_, &ClipboardHistory::failed, this, &ShellController::report);
    configureClipboard();
    connect(&emoji_, &EmojiPicker::typeRequested, this, &ShellController::typeText);
    refreshApps();
    subscribe();
    notifications_.configure(config_.notifications);
    osd_.configure(config_.osd);
    connect(&displayModes_, &DisplayModes::request, this, &ShellController::send);
    power_.setCountdown(config_.power.countdown);
    connect(&power_, &Power::failed, this, &ShellController::report);
    connect(notifications_.cards(), &NotificationModel::countChanged, this,
            &ShellController::updateCards);
    connect(&notifications_, &NotificationCenter::received, this, &ShellController::updateCards);
    connect(audio_.get(), &Audio::changed, this, &ShellController::showVolume);
    connect(&backlight_, &Backlight::changed, this, [this](int percent) {
        if (config_.osd.brightness)
            osd_.show(overlayOutput(), "Brightness", percent, "brightness");
    });
    connect(&backlight_, &Backlight::failed, this, &ShellController::report);
    authentication_.setOutputSource([this] { return overlayOutput(); });
    // A monitor plugged in or out shows in the display settings window, unless it holds changes
    // not applied yet.
    auto monitorsChanged = [this] {
        if (displaySettings_.open() && !displaySettings_.trial() && !displaySettings_.changed())
            displaySettings_.reload();
    };
    connect(qGuiApp, &QGuiApplication::screenAdded, this, monitorsChanged);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, monitorsChanged);
    connect(powerMode_.get(), &PowerMode::failed, this, &ShellController::report);
    connect(wifi_.get(), &Wifi::failed, this, &ShellController::report);
    connect(bluetooth_.get(), &Bluetooth::failed, this, &ShellController::report);
}
ShellController::~ShellController() {
    delete engine_; // Before the objects its context refers to go away.
    delete trayHost_; // Before the model it fills.
    delete polkitAgent_; // Likewise.
    clearApps();
}
QQmlEngine *ShellController::engine() {
    if (!engine_) {
        engine_ = new QQmlEngine(this);
        engine_->addImageProvider("icons", new Icons);
        // image://notify/ID/STAMP: the picture a notification carried in its image-data hint
        // (the stamp only keeps QML from reusing a cached picture when a notification is
        // replaced).
        engine_->addImageProvider("notify", new ImageProvider([this](const QString &id, QSize) {
            const uint number = id.section('/', 0, 0).toUInt();
            const Notification *n = notifications_.cards()->find(number);
            if (!n)
                n = notifications_.history()->find(number);
            return n ? n->image : QImage();
        }));
        engine_->addImageProvider("tray", new ImageProvider(trayImage(tray_)));
        engine_->addImageProvider("thumbs", new Thumbnails);
        engine_->addImageProvider("windows", new ImageProvider([this](const QString &id, QSize) {
            return windowImage(tasks_, id);
        }));
        engine_->addImageProvider("clipboard", new ImageProvider([this](const QString &id, QSize) {
            return clipboardImage(clipboard_, id);
        }));
        engine_->rootContext()->setContextProperty("shell", this);
    }
    return engine_;
}
void ShellController::setEffects(bool effects) {
    if (effects == effects_)
        return;
    effects_ = effects;
    Q_EMIT effectsChanged();
}
namespace {
// Configuration colors are #RRGGBB or CSS-style #RRGGBBAA; Qt reads eight digits as #AARRGGBB.
QColor color(const std::string &value) {
    auto result = QColor::fromString(QString::fromStdString(value.substr(0, 7)));
    if (value.size() == 9)
        result.setAlpha(std::stoi(value.substr(7), nullptr, 16));
    return result;
}
} // namespace
QColor ShellController::accent() const { return color(config_.shell.accent); }
QColor ShellController::urgentColor() const {
    const auto &c = config_.settings.urgent_color; // premultiplied
    const float a = c[3];
    return a > 0 ? QColor::fromRgbF(c[0] / a, c[1] / a, c[2] / a, a) : QColor();
}
QColor ShellController::panelColor() const { return color(config_.shell.panel_color); }
QColor ShellController::textColor() const { return color(config_.shell.text_color); }
QColor ShellController::background() const {
    return QColor::fromRgbF(config_.settings.background[0], config_.settings.background[1],
                            config_.settings.background[2]);
}
QString ShellController::wallpaperFile() const {
    const auto configured = QString::fromStdString(config_.shell.wallpaper);
    auto it = pickedWallpapers_.find(profile());
    if (it != pickedWallpapers_.end() && it->configured == configured)
        return it->picked;
    if (configured.isEmpty())
        return {};
    std::filesystem::path file(config_.shell.wallpaper);
    if (file.is_relative())
        file = std::filesystem::absolute(path_).parent_path() / file;
    return QString::fromStdString(file.string());
}
QUrl ShellController::wallpaper() const {
    const auto file = wallpaperFile();
    return file.isEmpty() ? QUrl() : QUrl::fromLocalFile(file);
}
QString ShellController::wallpaperFolder() const {
    auto folder = QString::fromStdString(config_.shell.wallpapers);
    if (folder.startsWith("~/"))
        return QDir::homePath() + folder.mid(1);
    if (!folder.isEmpty())
        return folder;
    const auto pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    return QFileInfo(pictures + "/wallpapers").isDir() ? pictures + "/wallpapers" : pictures;
}
void ShellController::findWallpapers() {
    wallpapers_ = ::findWallpapers(wallpaperFolder());
    Q_EMIT wallpapersChanged();
}
QString ShellController::pickedWallpapersPath() {
    return QFileInfo(pinsPath()).path() + "/wallpapers";
}
void ShellController::pickWallpaper(const QString &path) {
    if (path.isEmpty())
        pickedWallpapers_.remove(profile());
    else
        pickedWallpapers_[profile()] = {QString::fromStdString(config_.shell.wallpaper), path};
    const auto file = pickedWallpapersPath();
    QDir().mkpath(QFileInfo(file).path());
    QSaveFile out(file);
    if (out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        for (auto it = pickedWallpapers_.begin(); it != pickedWallpapers_.end(); ++it)
            out.write((it.key() + '\t' + it->configured + '\t' + it->picked + '\n').toUtf8());
        if (!out.commit())
            report("Could not save the wallpaper: " + out.errorString());
    }
    Q_EMIT wallpaperChanged();
}
void ShellController::clearApps() {
    for (auto &app : apps_)
        if (app.info)
            g_object_unref(app.info);
    apps_.clear();
}
void ShellController::refreshApps() {
    clearApps();
    int index = 0;
    for (const auto &launcher : config_.shell.launchers)
        apps_.push_back({QString("pinned:%1").arg(index++), QString::fromStdString(launcher.name),
                         QString::fromStdString(launcher.icon), launcher.command, nullptr, true, {}});
    QList<app_match::Entry> entries, hidden;
    hiddenIcons_.clear();
    GList *list = g_app_info_get_all();
    for (GList *item = list; item; item = item->next) {
        auto *info = G_APP_INFO(item->data);
        if (!g_app_info_get_id(info))
            continue;
        QString icon = "application-x-executable";
        GIcon *gicon = g_app_info_get_icon(info);
        if (gicon && G_IS_THEMED_ICON(gicon)) {
            const char *const *names = g_themed_icon_get_names(G_THEMED_ICON(gicon));
            if (names && *names)
                icon = QString::fromUtf8(*names);
        } else if (gicon && G_IS_FILE_ICON(gicon)) {
            char *path = g_file_get_path(g_file_icon_get_file(G_FILE_ICON(gicon)));
            if (path) {
                icon = QString::fromUtf8(path);
                g_free(path);
            }
        }
        const auto id = QString::fromUtf8(g_app_info_get_id(info));
        const char *wmClass = G_IS_DESKTOP_APP_INFO(info)
                                  ? g_desktop_app_info_get_startup_wm_class(G_DESKTOP_APP_INFO(info))
                                  : nullptr;
        // An entry the menus leave out (NoDisplay, or not for this desktop) still gives the
        // windows of its program an icon: a password prompt's, a portal's file dialog's.
        if (!g_app_info_should_show(info)) {
            if (gicon) {
                hidden.push_back({id, QString::fromUtf8(wmClass ? wmClass : ""),
                                  QString::fromUtf8(g_app_info_get_commandline(info)),
                                  QString::fromUtf8(g_app_info_get_display_name(info))});
                hiddenIcons_.insert(id, icon);
            }
            continue;
        }
        apps_.push_back({id, QString::fromUtf8(g_app_info_get_display_name(info)), icon, {},
                         G_APP_INFO(g_object_ref(info)), userPins_.contains(id),
                         QString::fromUtf8(wmClass ? wmClass : "")});
        auto &app = apps_.back();
        if (G_IS_DESKTOP_APP_INFO(info)) {
            auto *entry = G_DESKTOP_APP_INFO(info);
            app.genericName = QString::fromUtf8(g_desktop_app_info_get_generic_name(entry));
            for (auto *keyword = g_desktop_app_info_get_keywords(entry); keyword && *keyword; ++keyword)
                app.keywords.push_back(QString::fromUtf8(*keyword));
        }
        app.description = QString::fromUtf8(g_app_info_get_description(info));
        entries.push_back({id, app.wmClass, QString::fromUtf8(g_app_info_get_commandline(info)),
                           app.name});
    }
    g_list_free_full(list, g_object_unref);
    appIndex_ = app_match::Index(entries);
    hiddenIndex_ = app_match::Index(hidden);
    appFor_.clear();
    iconFor_.clear();
    sortApps();
    Q_EMIT appsChanged();
}
// Configured launchers first, then applications pinned from the shell in the order they were
// pinned, then everything else by name.
void ShellController::sortApps() {
    auto rank = [this](const App &app) {
        if (!app.command.empty())
            return -1;
        return app.pinned ? int(userPins_.indexOf(app.id)) : int(userPins_.size());
    };
    std::stable_sort(apps_.begin(), apps_.end(), [&rank](const App &a, const App &b) {
        if (rank(a) != rank(b))
            return rank(a) < rank(b);
        if (a.pinned)
            return false;
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
}
QString ShellController::pinsPath() {
    auto state = qEnvironmentVariable("XDG_STATE_HOME");
    if (state.isEmpty() || QDir::isRelativePath(state))
        state = QDir::homePath() + "/.local/state";
    return state + "/paw/pinned";
}
void ShellController::savePins() {
    const auto path = pinsPath();
    QDir().mkpath(QFileInfo(path).path());
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        for (const auto &id : userPins_)
            file.write(id.toUtf8() + '\n');
        if (file.commit())
            return;
    }
    report("Could not save pinned applications: " + file.errorString());
}
void ShellController::pin(const QString &id) {
    auto it = std::find_if(apps_.begin(), apps_.end(),
                           [&id](const App &app) { return app.id == id && app.info; });
    if (it == apps_.end() || it->pinned)
        return;
    it->pinned = true;
    userPins_.push_back(id);
    savePins();
    sortApps();
    Q_EMIT appsChanged();
}
void ShellController::unpin(const QString &id) {
    auto it = std::find_if(apps_.begin(), apps_.end(),
                           [&id](const App &app) { return app.id == id && app.info; });
    if (!userPins_.removeOne(id))
        return;
    if (it != apps_.end())
        it->pinned = false;
    savePins();
    sortApps();
    Q_EMIT appsChanged();
}
void ShellController::orderPins(const QStringList &order) {
    QStringList pins;
    for (const auto &id : order)
        if (userPins_.contains(id) && !pins.contains(id))
            pins.push_back(id);
    for (const auto &id : userPins_)
        if (!pins.contains(id))
            pins.push_back(id);
    if (pins == userPins_)
        return;
    userPins_ = pins;
    savePins();
    sortApps();
    Q_EMIT appsChanged();
}
bool ShellController::isPinned(const QString &id) const {
    return std::any_of(apps_.begin(), apps_.end(),
                       [&id](const App &app) { return app.id == id && app.pinned; });
}
QString ShellController::appFor(const QString &windowAppId) const {
    if (windowAppId.isEmpty())
        return {};
    auto known = appFor_.constFind(windowAppId);
    if (known == appFor_.cend())
        known = appFor_.insert(windowAppId, appIndex_.find(windowAppId));
    return *known;
}
QString ShellController::pinnedAppFor(const QString &windowAppId) const {
    if (windowAppId.isEmpty())
        return {};
    // A configured launcher has no desktop file; its window is known by the program it runs
    // (kitty's app id is "kitty") or by an icon named after the application.
    for (const auto &app : apps_)
        if (!app.command.empty() &&
            (QFileInfo(QString::fromStdString(app.command.front()))
                     .fileName()
                     .compare(windowAppId, Qt::CaseInsensitive) == 0 ||
             app.icon.compare(windowAppId, Qt::CaseInsensitive) == 0))
            return app.id;
    auto id = appFor(windowAppId);
    return isPinned(id) ? id : QString();
}
QVariantMap ShellController::record(const App &app) {
    return {{"appId", app.id},
            {"name", app.name},
            {"icon", app.icon},
            {"pinned", app.pinned},
            {"configured", !app.command.empty()},
            {"genericName", app.genericName},
            {"keywords", app.keywords},
            {"description", app.description}};
}
QVariantList ShellController::apps() const {
    QVariantList list;
    for (const auto &app : apps_)
        list.push_back(record(app));
    return list;
}
QVariantMap ShellController::widgets() const {
    const auto &w = config_.shell.widgets;
    auto place = [](paw::WidgetPlace where) {
        return where == paw::WidgetPlace::Bar     ? QStringLiteral("bar")
               : where == paw::WidgetPlace::Quick ? QStringLiteral("quick")
                                                       : QString();
    };
    return {{"workspaces", w.workspaces},       {"battery", place(w.battery)},
            {"network", place(w.network)},      {"volume", place(w.volume)},
            {"clock", w.clock},                 {"calendar", w.calendar},
            {"tiling", place(w.tiling)},        {"profiles", place(w.profiles)},
            {"wallpapers", place(w.wallpapers)}, {"keyboard_layout", w.keyboard_layout},
            {"power", w.power},                 {"tray", w.tray},
            {"notifications", place(w.notifications)},
            {"media", w.media},
            {"power_mode", w.power_mode},
            {"bluetooth", w.bluetooth}};
}
QStringList ShellController::profiles() const {
    QStringList names;
    for (const auto &name : config_.profiles)
        names.push_back(QString::fromStdString(name));
    return names;
}
QVariantList ShellController::pinned() const {
    QVariantList list;
    for (const auto &app : apps_)
        if (app.pinned)
            list.push_back(record(app));
    return list;
}
void ShellController::report(const QString &message) {
    error_ = message;
    Q_EMIT errorChanged();
    QTimer::singleShot(8000, this, [this, message] {
        if (error_ == message)
            clearError();
    });
}
void ShellController::clearError() {
    error_.clear();
    Q_EMIT errorChanged();
}
bool ShellController::launch(const QString &id) {
    auto it =
        std::find_if(apps_.begin(), apps_.end(), [&id](const App &app) { return app.id == id; });
    if (it == apps_.end()) {
        report("This application is no longer available.");
        return false;
    }
    if (it->command.empty()) {
        if (!start(it->info, it->name))
            return false;
        startMenu_.record(id);
        return true;
    }
    QProcess process;
    auto env = QProcessEnvironment::systemEnvironment();
    env.remove("QT_WAYLAND_SHELL_INTEGRATION");
    process.setProcessEnvironment(env);
    process.setWorkingDirectory(QDir::homePath());
    process.setProgram(QString::fromStdString(it->command.front()));
    QStringList arguments;
    for (size_t i = 1; i < it->command.size(); ++i)
        arguments << QString::fromStdString(it->command[i]);
    process.setArguments(arguments);
    if (!process.startDetached()) {
        report("Could not launch " + it->name + ": " + process.errorString());
        return false;
    }
    clearError();
    return true;
}
namespace {
// The user's trash, where the freedesktop.org specification has it: its items are in files/.
QString trashFolder() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/Trash";
}
} // namespace
void ShellController::watchTrash() {
    if (!trashWatcher_) {
        if (!config_.shell.macos_style)
            return;
        trashWatcher_ = new QFileSystemWatcher(this);
        connect(trashWatcher_, &QFileSystemWatcher::directoryChanged, this, &ShellController::watchTrash);
    }
    // The folder of its items, or the nearest above it until that is made.
    QString watched = trashFolder() + "/files";
    while (!QFileInfo(watched).isDir() && QFileInfo(watched).path() != watched)
        watched = QFileInfo(watched).path();
    if (trashWatcher_->directories() != QStringList{watched}) {
        if (!trashWatcher_->directories().isEmpty())
            trashWatcher_->removePaths(trashWatcher_->directories());
        trashWatcher_->addPath(watched);
    }
    const bool full = !QDir(trashFolder() + "/files").isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden);
    if (full != trashFull_) {
        trashFull_ = full;
        Q_EMIT trashChanged();
    }
}
bool ShellController::openTrash() {
    GAppLaunchContext *context = g_app_launch_context_new();
    g_app_launch_context_unsetenv(context, "QT_WAYLAND_SHELL_INTEGRATION");
    GError *error = nullptr;
    bool success = g_app_info_launch_default_for_uri("trash:///", context, nullptr);
    if (!success) {
        QDir().mkpath(trashFolder() + "/files");
        const QByteArray folder = QUrl::fromLocalFile(trashFolder() + "/files").toEncoded();
        success = g_app_info_launch_default_for_uri(folder.constData(), context, &error);
    }
    g_object_unref(context);
    if (!success) {
        report("Could not open the Trash: " + QString::fromUtf8(error ? error->message : "unknown error"));
        if (error)
            g_error_free(error);
        return false;
    }
    clearError();
    return true;
}
void ShellController::configureSearch() {
    const auto &search = config_.shell.search;
    FileIndex::Settings settings;
    settings.enabled = search.files;
    for (const auto &folder : search.directories) {
        auto path = QString::fromStdString(folder);
        if (path == "~" || path.startsWith("~/"))
            path = QDir::homePath() + path.sliced(1);
        settings.roots.push_back(QDir::cleanPath(path));
    }
    settings.depth = search.depth;
    settings.limit = search.max_files;
    files_.configure(settings);
    startMenu_.setWebSearch(webSearch());
}
void ShellController::typeText(const QString &text) {
    // Without the compositor, or with nothing for it to type into, what was picked is copied, to
    // paste.
    auto copy = [this, text](const QString &why) {
        QGuiApplication::clipboard()->setText(text);
        report("Copied " + text + " to paste it, as " + why);
    };
    if (qEnvironmentVariable("PAW_SOCKET").isEmpty())
        return copy("there is no compositor to type it");
    ask(("type " + text + "\n").toUtf8(), [copy](const QByteArray &reply) {
        if (!reply.startsWith("ok"))
            copy(QString::fromUtf8(reply).trimmed().remove(QRegularExpression("^error: ")));
    });
}
void ShellController::configureClipboard() {
    const auto &clipboard = config_.shell.clipboard;
    ClipboardHistory::Settings settings;
    settings.enabled = clipboard.enabled;
    settings.limit = clipboard.max_entries;
    settings.images = clipboard.images;
    settings.persist = clipboard.persist;
    clipboard_.configure(settings);
}
bool ShellController::openUrl(const QString &url) {
    // In the default browser, as the shell's own platform settings are not its.
    GAppLaunchContext *context = g_app_launch_context_new();
    g_app_launch_context_unsetenv(context, "QT_WAYLAND_SHELL_INTEGRATION");
    GError *error = nullptr;
    const bool success = g_app_info_launch_default_for_uri(url.toUtf8().constData(), context, &error);
    g_object_unref(context);
    if (!success) {
        report("Could not open " + url + ": " +
               QString::fromUtf8(error ? error->message : "unknown error"));
        if (error)
            g_error_free(error);
        return false;
    }
    clearError();
    return true;
}
bool ShellController::openFile(const QString &path, bool folder) {
    const auto error = FileIndex::open(path, folder);
    if (!error.isEmpty()) {
        const auto name = QFileInfo(path).fileName();
        report("Could not open " + (folder ? "the folder of " + name : name) + ": " + error);
        return false;
    }
    clearError();
    return true;
}
bool ShellController::start(GAppInfo *info, const QString &name) {
    // The shell's own platform settings are not the application's.
    GAppLaunchContext *context = g_app_launch_context_new();
    g_app_launch_context_unsetenv(context, "QT_WAYLAND_SHELL_INTEGRATION");
    GError *error = nullptr;
    const bool success = g_app_info_launch(info, nullptr, context, &error);
    g_object_unref(context);
    if (!success) {
        report("Could not launch " + name + ": " +
               QString::fromUtf8(error ? error->message : "unknown error"));
        if (error)
            g_error_free(error);
        return false;
    }
    clearError();
    return true;
}
namespace {
// The key file of an installed application's desktop entry, for what GIO does not read from it;
// nullptr when it cannot be read. Freed with g_key_file_unref.
GKeyFile *desktopEntry(GDesktopAppInfo *info) {
    const char *path = g_desktop_app_info_get_filename(info);
    GKeyFile *file = g_key_file_new();
    if (path && g_key_file_load_from_file(file, path, G_KEY_FILE_KEEP_TRANSLATIONS, nullptr))
        return file;
    g_key_file_unref(file);
    return nullptr;
}
bool hasAction(GDesktopAppInfo *info, const QByteArray &action) {
    for (auto *name = g_desktop_app_info_list_actions(info); name && *name; ++name)
        if (action == *name)
            return true;
    return false;
}
} // namespace
QVariantList ShellController::appActions(const QString &id) const {
    QVariantList list;
    auto it = std::find_if(apps_.begin(), apps_.end(),
                           [&id](const App &app) { return app.id == id && app.info; });
    if (it == apps_.end() || !G_IS_DESKTOP_APP_INFO(it->info))
        return list;
    auto *info = G_DESKTOP_APP_INFO(it->info);
    const gchar *const *actions = g_desktop_app_info_list_actions(info);
    if (!actions || !*actions)
        return list;
    // GIO gives an action's name but not its icon, which is in the action's own group.
    GKeyFile *file = desktopEntry(info);
    for (auto *action = actions; *action; ++action) {
        char *name = g_desktop_app_info_get_action_name(info, *action);
        const QByteArray group = QByteArray("Desktop Action ") + *action;
        char *icon = file ? g_key_file_get_string(file, group.constData(), "Icon", nullptr) : nullptr;
        list.push_back(QVariantMap{{"action", QString::fromUtf8(*action)},
                                   {"name", QString::fromUtf8(name ? name : *action)},
                                   {"icon", QString::fromUtf8(icon ? icon : "")}});
        g_free(name);
        g_free(icon);
    }
    if (file)
        g_key_file_unref(file);
    return list;
}
bool ShellController::launchAction(const QString &id, const QString &action) {
    auto it = std::find_if(apps_.begin(), apps_.end(),
                           [&id](const App &app) { return app.id == id && app.info; });
    const QByteArray name = action.toUtf8();
    if (it == apps_.end() || !G_IS_DESKTOP_APP_INFO(it->info) ||
        !hasAction(G_DESKTOP_APP_INFO(it->info), name)) {
        report("This action is no longer available.");
        return false;
    }
    auto *info = G_DESKTOP_APP_INFO(it->info);
    // An application started over D-Bus is asked to run the action itself, and reports its own
    // failures.
    if (g_desktop_app_info_get_boolean(info, "DBusActivatable")) {
        GAppLaunchContext *context = g_app_launch_context_new();
        g_app_launch_context_unsetenv(context, "QT_WAYLAND_SHELL_INTEGRATION");
        g_desktop_app_info_launch_action(info, name.constData(), context);
        g_object_unref(context);
        clearError();
        startMenu_.record(id);
        return true;
    }
    // GIO would start any other action with nowhere to say that it failed. Started as an entry of
    // its own whose command is the action's, it runs the same way and a missing program is
    // reported as launch() reports one.
    GDesktopAppInfo *entry = nullptr;
    if (GKeyFile *file = desktopEntry(info)) {
        const QByteArray group = "Desktop Action " + name;
        if (char *exec = g_key_file_get_string(file, group.constData(), "Exec", nullptr)) {
            g_key_file_set_string(file, G_KEY_FILE_DESKTOP_GROUP, G_KEY_FILE_DESKTOP_KEY_EXEC, exec);
            g_free(exec);
            entry = g_desktop_app_info_new_from_keyfile(file);
        }
        g_key_file_unref(file);
    }
    if (!entry) {
        report("Could not launch " + it->name + ": its desktop entry gives no command for this");
        return false;
    }
    const bool started = start(G_APP_INFO(entry), it->name);
    g_object_unref(entry);
    if (started)
        startMenu_.record(id);
    return started;
}
void ShellController::reload() {
    try {
        loadConfig();
        watchTrash();
        notifications_.configure(config_.notifications);
        osd_.configure(config_.osd);
        power_.setCountdown(config_.power.countdown);
        configureSearch();
        configureClipboard();
        updateNotificationService();
        updateTrayHost();
        updatePolkitAgent();
        refreshApps();
        Q_EMIT configChanged();
        Q_EMIT wallpaperChanged();
        if (!enabled())
            Q_EMIT disabled();
    } catch (const std::exception &error) {
        std::cerr << "Shell reload rejected: " << error.what() << '\n';
        report("Configuration unchanged: " + QString::fromUtf8(error.what()));
    }
}
void ShellController::loadConfig() {
    std::string error;
    auto next = paw::load_config_or_default(path_, error);
    config_ = std::move(next);
    configError_ = QString::fromStdString(error);
    if (!error.empty())
        std::cerr << "Shell configuration error; using the default configuration: " << error
                  << '\n';
}
namespace {
// The rectangle of the four numbers X Y WIDTH HEIGHT from `words[from]` on.
QRect rectAt(const QStringList &words, int from) {
    return QRect(words[from].toInt(), words[from + 1].toInt(), words[from + 2].toInt(),
                 words[from + 3].toInt());
}
} // namespace
void ShellController::subscribe() {
    const auto path = qEnvironmentVariable("PAW_SOCKET");
    if (path.isEmpty())
        return;
    state_ = new QLocalSocket(this);
    // As the shell, which carries out the volume keys the compositor would otherwise hand to wpctl.
    connect(state_, &QLocalSocket::connected, this, [this] { state_->write("subscribe shell\n"); });
    connect(state_, &QLocalSocket::readyRead, this, [this] {
        bool outputs = false, urgentSeen = false;
        while (state_->canReadLine()) {
            const auto raw = QString::fromUtf8(state_->readLine());
            const auto line = raw.trimmed();
            if (line == "ok") {
                subscribed_ = true;
                Q_EMIT tilingChanged();
            } else if (line.startsWith("tiling ")) {
                // Each state starts with this line and lists every output after it.
                tiling_ = line == "tiling on";
                nextWorkspaces_.clear();
                nextUrgentCount_ = 0;
                nextUrgentWindows_.clear();
                urgentSeen = true;
                outputs = true;
                Q_EMIT tilingChanged();
            } else if (line.startsWith("output ")) {
                outputs = true;
                // output NAME CURRENT OCCUPIED TILING, where OCCUPIED is "1,3" or "-" and
                // TILING is "on" or "off".
                const auto words = line.split(' ');
                if (words.size() != 5)
                    continue;
                QVariantList occupied;
                for (const auto &number : words[3].split(',', Qt::SkipEmptyParts))
                    if (number != "-")
                        occupied.push_back(number.toInt());
                nextWorkspaces_[words[1]] = QVariantMap{{"current", words[2].toInt()},
                                                        {"occupied", occupied},
                                                        {"urgent", QVariantList()},
                                                        {"tiling", words[4] == "on"}};
            } else if (line.startsWith("urgent ")) {
                nextUrgentCount_ = line.sliced(7).toInt();
            } else if (line.startsWith("urgent-output ")) {
                // urgent-output NAME 2,3: the workspaces of that output with urgent windows.
                const auto words = line.split(' ');
                if (words.size() != 3 || !nextWorkspaces_.contains(words[1]))
                    continue;
                QVariantList urgent;
                for (const auto &number : words[2].split(',', Qt::SkipEmptyParts))
                    urgent.push_back(number.toInt());
                auto state = nextWorkspaces_[words[1]].toMap();
                state["urgent"] = urgent;
                nextWorkspaces_[words[1]] = state;
            } else if (line.startsWith("urgent-window ")) {
                // OUTPUT, WORKSPACE, APP_ID, TITLE, separated by tabs; read untrimmed so an
                // empty app id keeps its place.
                const auto fields = raw.sliced(14).chopped(1).split('\t');
                if (fields.size() == 4) {
                    nextUrgentWindows_.push_back(QVariantMap{{"output", fields[0]},
                                                             {"workspace", fields[1].toInt()},
                                                             {"appId", fields[2]},
                                                             {"title", fields[3]}});
                }
            } else if (line.startsWith("focused ")) {
                const auto name = line.sliced(8) == "-" ? QString() : line.sliced(8);
                if (name != focusedOutput_) {
                    focusedOutput_ = name;
                    Q_EMIT focusedOutputChanged();
                }
            } else if (line.startsWith("keyboard-layout ")) {
                // keyboard-layout N COUNT SHORT NAME
                const auto words = line.split(' ');
                if (words.size() < 4)
                    continue;
                const QVariantMap layout{{"number", words[1].toInt()},
                                         {"count", words[2].toInt()},
                                         {"short", words[3]},
                                         {"name", QStringList(words.mid(4)).join(' ')}};
                if (layout != keyboardLayout_) {
                    keyboardLayout_ = layout;
                    Q_EMIT keyboardLayoutChanged();
                }
            } else if (line.startsWith("mode ")) {
                // mode NAME: the binding mode in use, "default" outside any.
                setBindingMode(line.sliced(5) == "default" ? QString() : line.sliced(5));
            } else if (line.startsWith("night-light ")) {
                // night-light ACTIVE MODE
                const auto words = line.split(' ');
                if (words.size() == 3)
                    setNightLight(words[1] == "on", words[2]);
            } else if (line.startsWith("notice ")) {
                const auto fields = line.sliced(7).split('\t');
                notice(fields[0], fields.value(1));
            } else if (line.startsWith("dnd ")) {
                handleDnd(line.sliced(4));
            } else if (line.startsWith("volume ") || line.startsWith("microphone ") ||
                       line.startsWith("brightness ")) {
                volumeKeys_.handle(line);
            } else if (line.startsWith("osd ")) {
                // osd OUTPUT PERCENT TEXT
                const auto words = line.split(' ');
                if (words.size() >= 4) {
                    bool ok = false;
                    const int percent = words[2].toInt(&ok);
                    if (ok)
                        osd_.show(words[1] == "-" ? overlayOutput() : words[1],
                                  QStringList(words.mid(3)).join(' '), percent);
                }
            } else if (displayModes_.handle(line) || displaySettings_.handle(line)) {
                // The display mode popup's lines; "display-settings OUTPUT", and a trial's.
            } else if (line.startsWith("notifications ")) {
                Q_EMIT notificationsRequested(line.sliced(14));
            } else if (line.startsWith("media ")) {
                // media VERB: a media key, for the current player.
                media_->command(line.sliced(6));
            } else if (line.startsWith("clipboard ")) {
                clipboard_.toggle(line.sliced(10));
            } else if (line.startsWith("emoji ")) {
                emoji_.toggle(line.sliced(6));
            } else if (line.startsWith("locked ")) {
                // locked on|off: nothing copied while the session is locked is kept.
                clipboard_.setLocked(line == "locked on");
            } else if (line.startsWith("power ")) {
                // power ACTIONS: those that may run, as "lock,suspend,logout", or "-".
                power_.setAvailable(line.sliced(6));
            } else if (line.startsWith("power-menu ")) {
                Q_EMIT powerMenuRequested(line.sliced(11));
            } else if (line.startsWith("taskbar ")) {
                Q_EMIT taskbarRequested(line.sliced(8));
            } else if (line.startsWith("power-error ")) {
                report(line.sliced(12));
            } else if (line.startsWith("spawn-error ")) {
                // A program a binding, a hot corner or the palette asked for did not start.
                report(line.sliced(12));
            } else if (line.startsWith("launcher ")) {
                Q_EMIT launcherRequested(line.sliced(9));
            } else if (line.startsWith("palette ")) {
                palette_.open(line.sliced(8));
            } else if (line.startsWith("switcher ")) {
                // switcher OUTPUT SELECTED COUNT, then COUNT switcher-window lines.
                const auto words = line.split(' ');
                if (words.size() != 4)
                    continue;
                nextSwitcher_ = {.output = words[1], .selected = words[2].toInt()};
                switcherPending_ = words[3].toInt();
                if (switcherPending_ <= 0)
                    showSwitcher();
            } else if (line.startsWith("switcher-window ") && switcherPending_ > 0) {
                // APP_ID, TITLE, OUTPUT, WORKSPACE, MINIMIZED, URGENT, ID, separated by tabs, of
                // which an older compositor leaves the last ones out and a newer one may add
                // more. The line is read untrimmed so an empty app id keeps its place.
                const auto fields = raw.sliced(16).chopped(1).split('\t');
                if (fields.size() >= 5)
                    nextSwitcher_.windows.push_back(QVariantMap{{"appId", fields[0]},
                                                               {"title", fields[1]},
                                                               {"output", fields[2]},
                                                               {"workspace", fields[3].toInt()},
                                                               {"minimized", fields[4] == "1"},
                                                               {"urgent", fields.size() >= 6 && fields[5] == "1"},
                                                               {"id", fields.size() >= 7 ? fields[6].toInt() : 0}});
                if (--switcherPending_ == 0)
                    showSwitcher();
            } else if (line.startsWith("switcher-select ")) {
                switcher_.selected = line.sliced(16).toInt();
                Q_EMIT switcherSelectedChanged();
            } else if (line.startsWith("overview ")) {
                // overview OUTPUT COUNT SELECTED VIEWED STRIP X Y WIDTH HEIGHT FILTER (the area the
                // panels leave; the filter is "-" when empty), then COUNT overview-window and
                // STRIP overview-strip lines.
                const auto words = line.split(' ');
                if (words.size() < 11)
                    continue;
                const auto filter = QStringList(words.mid(10)).join(' ');
                nextOverview_ = {.output = words[1],
                                 .filter = filter == "-" ? QString() : filter,
                                 .selected = words[3].toInt(),
                                 .viewed = words[4].toInt(),
                                 .area = rectAt(words, 6)};
                expectOverview(words[2].toInt() + words[5].toInt());
            } else if (line.startsWith("overview-assist ")) {
                // overview-assist OUTPUT COUNT SELECTED X Y WIDTH HEIGHT (the free slot beside a
                // window just snapped), then COUNT overview-window lines.
                const auto words = line.split(' ');
                if (words.size() < 8)
                    continue;
                nextOverview_ = {.output = words[1],
                                 .selected = words[3].toInt(),
                                 .viewed = 0,
                                 .area = rectAt(words, 4),
                                 .assist = true};
                expectOverview(words[2].toInt());
            } else if ((line.startsWith("overview-window ") || line.startsWith("overview-strip ")) &&
                       overviewPending_ > 0) {
                // X Y WIDTH HEIGHT, then APP_ID, TITLE, WORKSPACE, URGENT (a window) or WORKSPACE,
                // WINDOWS (a strip cell) separated by tabs; read untrimmed so an empty app id stays.
                const bool window = line.startsWith("overview-window ");
                const auto rest = raw.sliced(window ? 16 : 15).chopped(1);
                const auto fields = rest.section(' ', 4).split('\t');
                QVariantMap cell{{"x", rest.section(' ', 0, 0).toInt()},
                                 {"y", rest.section(' ', 1, 1).toInt()},
                                 {"w", rest.section(' ', 2, 2).toInt()},
                                 {"h", rest.section(' ', 3, 3).toInt()}};
                if (window && (fields.size() == 3 || fields.size() == 4)) {
                    cell.insert("appId", fields[0]);
                    cell.insert("title", fields[1]);
                    cell.insert("workspace", fields[2].toInt());
                    cell.insert("urgent", fields.size() == 4 && fields[3] == "1");
                    nextOverview_.windows.push_back(cell);
                } else if (!window && fields.size() == 2) {
                    cell.insert("workspace", fields[0].toInt());
                    cell.insert("windows", fields[1].toInt());
                    nextOverview_.strip.push_back(cell);
                }
                if (--overviewPending_ == 0)
                    showOverview();
            } else if (line.startsWith("overview-select ")) {
                overview_.selected = line.sliced(16).toInt();
                Q_EMIT overviewSelectedChanged();
            } else if (line == "overview-close") {
                clearOverview();
            } else if (line == "switcher-close") {
                switcherPending_ = 0;
                switcher_.output.clear();
                switcher_.windows.clear();
                Q_EMIT switcherChanged();
            }
        }
        if (outputs && workspaces_ != nextWorkspaces_) {
            workspaces_ = nextWorkspaces_;
            Q_EMIT workspacesChanged();
        }
        if (urgentSeen && (urgentCount_ != nextUrgentCount_ || urgentWindows_ != nextUrgentWindows_)) {
            urgentCount_ = nextUrgentCount_;
            urgentWindows_ = nextUrgentWindows_;
            QList<QPair<QString, QString>> windows;
            for (const auto &item : urgentWindows_)
                windows.push_back({item.toMap()["appId"].toString(), item.toMap()["title"].toString()});
            tasks_.setUrgent(windows);
            Q_EMIT urgentChanged();
        }
    });
    connect(state_, &QLocalSocket::disconnected, this, [this] {
        subscribed_ = false;
        if (!keyboardLayout_.isEmpty()) {
            keyboardLayout_.clear();
            Q_EMIT keyboardLayoutChanged();
        }
        power_.setAvailable("-");
        setNightLight(false, {});
        setBindingMode({});
        if (urgentCount_ != 0 || !urgentWindows_.isEmpty()) {
            urgentCount_ = 0;
            urgentWindows_.clear();
            tasks_.setUrgent({});
            Q_EMIT urgentChanged();
        }
        Q_EMIT tilingChanged();
        if (!switcher_.output.isEmpty()) {
            switcher_.output.clear();
            switcher_.windows.clear();
            Q_EMIT switcherChanged();
        }
        clearOverview();
    });
    state_->connectToServer(path);
}
void ShellController::setBindingMode(const QString &mode) {
    if (mode == bindingMode_)
        return;
    bindingMode_ = mode;
    Q_EMIT bindingModeChanged();
}
void ShellController::setNightLight(bool on, const QString &mode) {
    if (on == nightLight_ && mode == nightLightMode_)
        return;
    nightLight_ = on;
    nightLightMode_ = mode;
    Q_EMIT nightLightChanged();
}
bool ShellController::hasOutput(const QString &name) {
    const auto screens = QGuiApplication::screens();
    return std::any_of(screens.begin(), screens.end(),
                       [&name](const QScreen *screen) { return screen->name() == name; });
}
QString ShellController::outputOrPrimary(const QString &name) {
    if (hasOutput(name))
        return name;
    auto *primary = QGuiApplication::primaryScreen();
    return primary ? primary->name() : QString();
}
// The cards stay on one output while any is showing, so they do not jump about as the focus
// moves; the next batch goes where the focus is then. An output that has gone since the cards
// opened there is not one to keep them on.
void ShellController::updateCards() {
    const QString next = notifications_.cards()->count() > 0
                             ? (cardsOutput_.isEmpty() || !hasOutput(cardsOutput_) ? overlayOutput()
                                                                                    : cardsOutput_)
                             : QString();
    if (next == cardsOutput_)
        return;
    cardsOutput_ = next;
    Q_EMIT cardsOutputChanged();
}
void ShellController::notice(const QString &summary, const QString &body) {
    Notification notification;
    notification.app = "paw";
    notification.icon = "input-keyboard";
    notification.summary = summary;
    notification.body = body.toHtmlEscaped();
    notifications_.notify(std::move(notification));
    if (!notifications_.enabled() || notifications_.dnd())
        osd_.show(overlayOutput(), summary, -1);
}
void ShellController::handleDnd(const QString &verb) {
    if (verb == "toggle")
        notifications_.toggleDnd();
    else if (verb == "on" || verb == "off")
        notifications_.setDnd(verb == "on");
    else
        return;
    const bool on = notifications_.dnd();
    osd_.show(overlayOutput(), on ? "Do not disturb" : "Notifications on", -1, on ? "dnd" : "notifications");
}
// The volume changed on the default output (by keys, the panel, or another program).
void ShellController::showVolume() {
    Audio *audio = audio_.get();
    if (!audio->available()) {
        lastVolume_ = -1;
        return;
    }
    const int volume = audio->volume();
    const bool muted = audio->muted();
    const QString output = audio->output();
    const bool first = lastVolume_ < 0 || output != lastVolumeOutput_;
    const bool differs = volume != lastVolume_ || muted != lastMuted_;
    lastVolume_ = volume;
    lastMuted_ = muted;
    lastVolumeOutput_ = output;
    if (first || !differs || !config_.osd.volume)
        return;
    osd_.show(overlayOutput(), muted ? "Muted" : "Volume", muted ? 0 : volume,
              muted ? "muted" : "volume");
}
bool ShellController::startNotifications() {
    serveNotifications_ = true;
    updateNotificationService();
    return notificationService_ != nullptr;
}
void ShellController::updateNotificationService() {
    if (!serveNotifications_)
        return;
#if PAW_DBUS
    if (config_.notifications.enabled && !notificationService_) {
        // A session with no bus has no notifications to serve.
        if (!dbus::haveSessionBus()) {
            if (!noBusReported_)
                std::cerr << "paw notifications: no session bus (DBUS_SESSION_BUS_ADDRESS is unset)\n";
            noBusReported_ = true;
            return;
        }
        auto *service = new NotificationService(notifications_, this);
        if (service->start()) {
            notificationService_ = service;
            std::cerr << "paw notifications: serving org.freedesktop.Notifications\n";
        } else {
            std::cerr << "paw notifications: " << service->error().toStdString() << '\n';
            delete service;
        }
    } else if (!config_.notifications.enabled && notificationService_) {
        delete notificationService_;
        notificationService_ = nullptr;
        notifications_.setServing(false);
    }
#endif
}
bool ShellController::startTray() {
    serveTray_ = true;
    updateTrayHost();
    return trayHost_ != nullptr;
}
void ShellController::updateTrayHost() {
    if (!serveTray_)
        return;
#if PAW_TRAY
    // Off, the shell leaves the names to another tray: applications would otherwise think their
    // icons are shown.
    if (!config_.shell.widgets.tray) {
        if (trayHost_)
            std::cerr << "paw tray: off, its names released\n";
        delete trayHost_;
        trayHost_ = nullptr;
    } else if (!trayHost_) {
        if (!dbus::haveSessionBus()) {
            if (!trayNoBusReported_)
                std::cerr << "paw tray: no session bus (DBUS_SESSION_BUS_ADDRESS is unset)\n";
            trayNoBusReported_ = true;
            return;
        }
        auto *host = new TrayHost(tray_, this);
        if (host->start()) {
            trayHost_ = host;
            std::cerr << "paw tray: "
                      << (host->ownsWatcher() ? "serving org.kde.StatusNotifierWatcher"
                                              : "showing the items of another StatusNotifierWatcher")
                      << '\n';
        } else {
            std::cerr << "paw tray: " << host->error().toStdString() << '\n';
            delete host;
        }
    }
#endif
}
bool ShellController::startPolkit() {
    servePolkit_ = true;
    updatePolkitAgent();
    return polkitAgent_ != nullptr;
}
void ShellController::updatePolkitAgent() {
    if (!servePolkit_)
        return;
#if PAW_POLKIT
    if (!config_.shell.polkit_agent) {
        if (polkitAgent_)
            std::cerr << "paw polkit: off, no longer the authentication agent\n";
        delete polkitAgent_;
        polkitAgent_ = nullptr;
        polkitProblem_.clear();
    } else if (!polkitAgent_) {
        auto *agent = new PolkitAgent(authentication_, this);
        if (agent->start()) {
            polkitAgent_ = agent;
            polkitProblem_.clear();
            std::cerr << "paw polkit: the session's authentication agent\n";
        } else {
            // Quietly off: said once, tried again at the next reload.
            const QString problem = agent->taken()
                                        ? "another authentication agent serves this session"
                                        : agent->error();
            if (problem != polkitProblem_)
                std::cerr << "paw polkit: " << problem.toStdString() << '\n';
            polkitProblem_ = problem;
            delete agent;
        }
    }
#endif
}
void ShellController::showSwitcher() {
    switcher_ = nextSwitcher_;
    Q_EMIT switcherChanged();
    Q_EMIT switcherSelectedChanged();
}
void ShellController::expectOverview(int pending) {
    overviewPending_ = pending;
    if (overviewPending_ <= 0)
        showOverview();
}
void ShellController::showOverview() {
    overview_ = nextOverview_;
    Q_EMIT overviewChanged();
    Q_EMIT overviewSelectedChanged();
}
void ShellController::clearOverview() {
    overviewPending_ = 0;
    if (overview_.output.isEmpty())
        return;
    overview_.output.clear();
    overview_.windows.clear();
    overview_.strip.clear();
    overview_.filter.clear();
    Q_EMIT overviewChanged();
}
void ShellController::switcherPick(int index) {
    if (index >= 0 && index < switcher_.windows.size())
        request(QString("switcher_confirm %1\n").arg(index + 1).toUtf8(),
                "The window switcher needs a running paw session.");
}
QString ShellController::iconFor(const QString &windowAppId) const {
    const auto id = appFor(windowAppId);
    for (const auto &app : apps_)
        if (!id.isEmpty() && app.id == id)
            return app.icon;
    auto known = iconFor_.constFind(windowAppId);
    if (known == iconFor_.cend()) {
        // The icon of an entry the menus leave out, else a name the icon theme has, guessed
        // from the app id; never a path, which only a desktop entry's Icon= may be.
        QString icon = hiddenIcons_.value(hiddenIndex_.find(windowAppId));
        for (const auto &guess : app_match::iconGuesses(windowAppId))
            if (icon.isEmpty() && QIcon::hasThemeIcon(guess))
                icon = guess;
        known = iconFor_.insert(windowAppId, icon.isEmpty() ? "application-x-executable" : icon);
    }
    return *known;
}
void ShellController::toggleTiling(const QString &output) {
    request(output.isEmpty() ? QByteArray("toggle_tiling\n")
                             : QString("output %1 toggle_tiling\n").arg(output).toUtf8(),
            "Tiling needs a running paw session.");
}
void ShellController::showWorkspace(const QString &output, int number) {
    if (!output.isEmpty() && number >= 1 && number <= workspaceCount())
        request(QString("output %1 workspace %2\n").arg(output).arg(number).toUtf8(),
                "Workspaces need a running paw session.");
}
void ShellController::send(const QString &line) {
    request(line.toUtf8() + "\n", "The compositor needs a running paw session.");
}
void ShellController::ask(const QByteArray &line, std::function<void(const QByteArray &)> done) {
    const auto path = qEnvironmentVariable("PAW_SOCKET");
    if (path.isEmpty())
        return;
    auto *socket = new QLocalSocket(this);
    auto reply = std::make_shared<QByteArray>();
    connect(socket, &QLocalSocket::connected, socket, [socket, line] { socket->write(line); });
    connect(socket, &QLocalSocket::readyRead, socket,
            [socket, reply] { reply->append(socket->readAll()); });
    connect(socket, &QLocalSocket::disconnected, socket, [socket, reply, done] {
        reply->append(socket->readAll());
        done(*reply);
        socket->deleteLater();
    });
    connect(socket, &QLocalSocket::errorOccurred, socket, [socket](auto error) {
        if (error != QLocalSocket::PeerClosedError)
            socket->deleteLater();
    });
    socket->connectToServer(path);
}
void ShellController::request(const QByteArray &line, const QString &unavailable) {
    const auto path = qEnvironmentVariable("PAW_SOCKET");
    if (path.isEmpty()) {
        report(unavailable);
        return;
    }
    // One request per connection; the new state arrives through the subscription.
    auto *request = new QLocalSocket(this);
    connect(request, &QLocalSocket::connected, request, [request, line] { request->write(line); });
    connect(request, &QLocalSocket::disconnected, request, &QObject::deleteLater);
    connect(request, &QLocalSocket::errorOccurred, this,
            [this, request](QLocalSocket::LocalSocketError error) {
                if (error != QLocalSocket::PeerClosedError)
                    report("Could not reach the compositor: " + request->errorString());
                request->deleteLater();
            });
    request->connectToServer(path);
}
