// SPDX-License-Identifier: GPL-3.0-or-later
#include "controller.hpp"
#include "gio_launch.hpp"
#include "icons.hpp"
#include "image_provider.hpp"
#include "paw/config_edit.hpp"
#include "paw/config_schema.hpp"
#include "state_files.hpp"
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
#include <QLibraryInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>
#include <algorithm>
#include <functional>
#include <gio/gdesktopappinfo.h>
#include <iostream>
#include <map>
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
    connect(this, &ShellController::appsChanged, this, [this] { ++appsRevision_; });
    connect(&restyle_, &Restyle::settled, this, &ShellController::restyled);
    {
        std::lock_guard guard(iconThemeLock());
        iconThemeBase_ = QIcon::themeName();
    }
    loadConfig();
    watchTrash();
    QFile pins(pinsPath());
    if (pins.open(QIODevice::ReadOnly | QIODevice::Text))
        for (const auto &line : QString::fromUtf8(pins.readAll()).split('\n', Qt::SkipEmptyParts))
            if (!userPins_.contains(line.trimmed()))
                userPins_.push_back(line.trimmed());
    // A line for each pick: the profile's name, the wallpaper the configuration gave and the one
    // picked, by tabs, and for a pick for one monitor alone the monitor's name after another.
    QFile picked(pickedWallpapersPath());
    if (picked.open(QIODevice::ReadOnly | QIODevice::Text))
        for (const auto &line : QString::fromUtf8(picked.readAll()).split('\n', Qt::SkipEmptyParts)) {
            const auto fields = line.split('\t');
            if ((fields.size() == 3 || (fields.size() == 4 && !fields[3].isEmpty())) &&
                !fields[2].isEmpty())
                pickedWallpapers_[fields[0]][fields.value(3)] = {fields[1], fields[2]};
        }
    // The start menu follows the applications and the taskbar's pins, which seed its own.
    connect(this, &ShellController::appsChanged, &startMenu_,
            [this] { startMenu_.setApps(apps(), userPins_); });
    connect(&startMenu_, &StartMenu::failed, this, &ShellController::report);
    connect(&runLauncher_, &RunLauncher::failed, this, &ShellController::report);
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
    configureDesktopClock();
    connect(&desktopIcons_, &DesktopIcons::failed, this, &ShellController::report);
    configureDesktopIcons();
    connect(&wallpaperCycle_, &WallpaperCycle::due, this, [this] { stepWallpaper(1, false); });
    configureWallpaperCycle();
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
    // The leftmost screen stands for the primary one until the compositor names it.
    connect(qGuiApp, &QGuiApplication::screenAdded, this, &ShellController::primaryOutputChanged);
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &ShellController::primaryOutputChanged);
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
        // The frosted glass under the bars and every card, overlays' too, which may come
        // without a panel.
        static const int glass = qmlRegisterType<GlassItem>("Paw", 1, 0, "Glass");
        Q_UNUSED(glass);
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
    glass_.setEffects(effects);
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
// A colour of the compositor's settings, which are premultiplied.
static QColor unpremultiplied(const float (&c)[4]) {
    const float a = c[3];
    return a > 0 ? QColor::fromRgbF(c[0] / a, c[1] / a, c[2] / a, a) : QColor(Qt::transparent);
}
QColor ShellController::urgentColor() const {
    const auto &c = config_.settings.urgent_color;
    return c[3] > 0 ? unpremultiplied(c) : QColor();
}
QColor ShellController::blush() const {
    const auto &border = config_.settings.border_active_gradient;
    if (border.count >= 2 && border.stops[border.count - 1][3] > 0)
        return unpremultiplied(border.stops[border.count - 1]);
    return urgentColor();
}
int ShellController::panelHeadroom(const QString &output) const {
    const auto &shell = config_.shell;
    const auto &settings = config_.settings;
    if (shell.macos_style)
        return panelHeight() / 2;
    const bool floating = shell.panel_radius > 0 || panelMargin(0) > 0 || panelMargin(1) > 0 ||
                          panelMargin(2) > 0 || panelMargin(3) > 0;
    if (!shell.frame_windows || !settings.shadow || !effects_ || !floating)
        return 0;
    // A bar along the top casts its shadow down, into the output; one along the bottom, up; one
    // along a side, across. The margin on the side towards the middle is room it has already.
    int inwards = 0, margin = 0;
    switch (panelSurfaceEdge(output)) {
    case paw::PanelEdge::Top:
        inwards = settings.shadow_y, margin = panelMargin(2);
        break;
    case paw::PanelEdge::Bottom:
        inwards = -settings.shadow_y, margin = panelMargin(0);
        break;
    case paw::PanelEdge::Left:
        inwards = settings.shadow_x, margin = panelMargin(1);
        break;
    case paw::PanelEdge::Right:
        inwards = -settings.shadow_x, margin = panelMargin(3);
        break;
    }
    return std::max(0, settings.shadow_blur + std::max(0, inwards) - margin);
}
QString ShellController::panelEdge(const QString &output) const {
    switch (panelSurfaceEdge(output)) {
    case paw::PanelEdge::Top:
        return "top";
    case paw::PanelEdge::Left:
        return "left";
    case paw::PanelEdge::Right:
        return "right";
    case paw::PanelEdge::Bottom:
        break;
    }
    return "bottom";
}
QColor ShellController::windowBorderColor() const {
    return unpremultiplied(config_.settings.border_inactive);
}
QColor ShellController::windowShadowColor() const {
    return config_.settings.shadow ? unpremultiplied(config_.settings.shadow_color)
                                   : QColor(Qt::transparent);
}
QColor ShellController::windowShadowInactiveColor() const {
    return config_.settings.shadow ? unpremultiplied(config_.settings.shadow_inactive_color)
                                   : QColor(Qt::transparent);
}
QColor ShellController::panelColor() const { return color(config_.shell.panel_color); }
QColor ShellController::textColor() const { return color(config_.shell.text_color); }
QColor ShellController::background() const {
    return QColor::fromRgbF(config_.settings.background[0], config_.settings.background[1],
                            config_.settings.background[2]);
}
QString ShellController::configuredWallpaper(const QString &output) const {
    const auto &own = config_.shell.monitor_wallpapers;
    const auto it = output.isEmpty() ? own.end() : own.find(output.toStdString());
    return QString::fromStdString(it != own.end() ? it->second : config_.shell.wallpaper);
}
QString ShellController::wallpaperFileOn(const QString &output) const {
    const auto configured = configuredWallpaper(output);
    // Picked for the monitor alone, else for every monitor, while the configuration gives the
    // monitor what it did when picked.
    const auto picks = pickedWallpapers_.value(profile());
    for (const auto &key : output.isEmpty() ? QStringList{""} : QStringList{output, ""})
        if (const auto it = picks.find(key); it != picks.end() && it->configured == configured)
            return it->picked;
    if (configured.isEmpty())
        return {};
    std::filesystem::path file(configured.toStdString());
    if (file.is_relative())
        file = std::filesystem::absolute(path_).parent_path() / file;
    return QString::fromStdString(file.string());
}
QUrl ShellController::wallpaperOn(const QString &output) const {
    const auto file = wallpaperFileOn(output);
    return file.isEmpty() ? QUrl() : QUrl::fromLocalFile(file);
}
bool ShellController::ownWallpaper(const QString &output) const {
    // Picked for every monitor, a monitor with a wallpaper configured for it alone has the same
    // picked for it too, which is none of its own.
    const auto picks = pickedWallpapers_.value(profile());
    const auto it = picks.find(output), every = picks.find(QString());
    return !output.isEmpty() && it != picks.end() && it->configured == configuredWallpaper(output) &&
           (every == picks.end() || every->picked != it->picked);
}
QString ShellController::wallpaperMode() const {
    switch (config_.shell.wallpaper_mode) {
    case paw::WallpaperMode::Fit:
        return "fit";
    case paw::WallpaperMode::Center:
        return "center";
    case paw::WallpaperMode::Tile:
        return "tile";
    case paw::WallpaperMode::Stretch:
        return "stretch";
    case paw::WallpaperMode::Fill:
        break;
    }
    return "fill";
}
QUrl ShellController::wallpaper() const { return wallpaperOn({}); }
QUrl ShellController::startIcon() const {
    if (config_.shell.start_icon.empty())
        return {};
    std::filesystem::path file(config_.shell.start_icon);
    if (file.is_relative())
        file = std::filesystem::absolute(path_).parent_path() / file;
    return QUrl::fromLocalFile(QString::fromStdString(file.string()));
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
    // Every monitor shows it: those picked for alone too, and those the configuration gives a
    // wallpaper of their own, picked for each.
    auto &picks = pickedWallpapers_[profile()];
    picks.clear();
    if (!path.isEmpty()) {
        picks[""] = {configuredWallpaper({}), path};
        for (const auto &[name, own] : config_.shell.monitor_wallpapers)
            if (const auto output = QString::fromStdString(name); configuredWallpaper(output) !=
                                                                    configuredWallpaper({}))
                picks[output] = {configuredWallpaper(output), path};
    }
    savePickedWallpapers();
}
void ShellController::pickWallpaperOn(const QString &path, const QString &output) {
    if (output.isEmpty())
        return pickWallpaper(path);
    notePick(path, output);
    savePickedWallpapers();
}
void ShellController::notePick(const QString &path, const QString &output) {
    auto &picks = pickedWallpapers_[profile()];
    if (path.isEmpty())
        picks.remove(output);
    else
        picks[output] = {configuredWallpaper(output), path};
}
void ShellController::savePickedWallpapers() {
    QByteArray lines;
    for (auto profile = pickedWallpapers_.begin(); profile != pickedWallpapers_.end();) {
        if (profile->isEmpty()) {
            profile = pickedWallpapers_.erase(profile);
            continue;
        }
        for (auto it = profile->begin(); it != profile->end(); ++it) {
            // A name the line could not hold back is left out.
            if (it.key().contains('\t') || it.key().contains('\n'))
                continue;
            lines += (profile.key() + '\t' + it->configured + '\t' + it->picked +
                      (it.key().isEmpty() ? "" : '\t' + it.key()) + '\n')
                         .toUtf8();
        }
        ++profile;
    }
    if (const auto error = saveFile(pickedWallpapersPath(), lines); !error.isEmpty())
        report("Could not save the wallpaper: " + error);
    Q_EMIT wallpaperChanged();
}
QString ShellController::wallpaperCycleFolder() const {
    const auto &folder = config_.shell.wallpaper_cycle.folder;
    if (folder == "~" || folder.starts_with("~/"))
        return QDir::homePath() + QString::fromStdString(folder.substr(1));
    if (!folder.empty()) {
        std::filesystem::path path(folder);
        if (path.is_relative())
            path = std::filesystem::absolute(path_).parent_path() / path;
        return QString::fromStdString(path.string());
    }
    if (!config_.shell.wallpaper.empty()) {
        std::filesystem::path file(config_.shell.wallpaper);
        if (file.is_relative())
            file = std::filesystem::absolute(path_).parent_path() / file;
        return QString::fromStdString(file.parent_path().string());
    }
    return wallpaperFolder();
}
void ShellController::configureWallpaperCycle() {
    wallpaperCycle_.configure(config_.shell.wallpaper_cycle, wallpaperCycleFolder());
}
void ShellController::stepWallpaper(int direction, bool tell) {
    // One picture for every monitor, or with per_monitor each monitor the next after the one to
    // its left's, picked for it alone.
    QStringList outputs{QString()};
    if (config_.shell.wallpaper_cycle.per_monitor) {
        auto screens = QGuiApplication::screens();
        std::stable_sort(screens.begin(), screens.end(), [](const QScreen *a, const QScreen *b) {
            const auto pa = a->geometry().topLeft(), pb = b->geometry().topLeft();
            return pa.x() != pb.x() ? pa.x() < pb.x() : pa.y() < pb.y();
        });
        outputs.clear();
        for (const auto *screen : screens)
            if (!screen->name().isEmpty())
                outputs.push_back(screen->name());
        if (outputs.isEmpty())
            outputs.push_back(QString());
    }
    QStringList shown;
    for (const auto &output : outputs)
        shown.push_back(wallpaperFileOn(output));
    const auto next = wallpaperCycle_.step(shown, direction);
    if (next.isEmpty()) {
        if (tell)
            osd_.show(overlayOutput(), "No wallpapers in " + wallpaperCycle_.folder(), -1);
        return;
    }
    if (next == shown)
        return;
    if (outputs.size() == 1 && outputs.first().isEmpty())
        return pickWallpaper(next.first());
    for (qsizetype i = 0; i < outputs.size(); ++i)
        notePick(next[i], outputs[i]);
    savePickedWallpapers();
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
    for (const auto &launcher : config_.shell.launchers) {
        apps_.push_back({QString("pinned:%1").arg(index++), QString::fromStdString(launcher.name),
                         QString::fromStdString(launcher.icon), launcher.command, nullptr, true, {}});
        if (!launcher.command.empty())
            apps_.back().program =
                QFileInfo(QString::fromStdString(launcher.command.front())).fileName();
    }
    QList<app_match::Entry> entries, hidden;
    hiddenIcons_.clear();
    GList *list = g_app_info_get_all();
    for (GList *item = list; item; item = item->next) {
        auto *info = G_APP_INFO(item->data);
        if (!g_app_info_get_id(info))
            continue;
        GIcon *gicon = g_app_info_get_icon(info);
        const QString icon = gioIconName(gicon, "application-x-executable");
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
        const auto commandLine = QString::fromUtf8(g_app_info_get_commandline(info));
        app.program = app_match::program(commandLine);
        entries.push_back({id, app.wmClass, commandLine, app.name});
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
    return stateDir() + "/pinned";
}
void ShellController::savePins() {
    QByteArray lines;
    for (const auto &id : userPins_)
        lines += id.toUtf8() + '\n';
    if (const auto error = saveFile(pinsPath(), lines); !error.isEmpty())
        report("Could not save pinned applications: " + error);
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
            {"description", app.description},
            {"program", app.program}};
}
QVariantList ShellController::apps() const {
    QVariantList list;
    for (const auto &app : apps_)
        list.push_back(record(app));
    return list;
}
QVariant ShellController::appRecord(const QString &id) const {
    for (const auto &app : apps_)
        if (app.id == id)
            return record(app);
    return {};
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
QVariantMap ShellController::tilingBar() const {
    const auto &shell = config_.shell;
    const auto &bar = shell.tiling_bar;
    auto own = [](const std::string &value, const std::string &fallback) {
        return color(value.empty() ? fallback : value);
    };
    auto names = [](const std::vector<std::string> &modules) {
        QStringList list;
        for (const auto &module : modules)
            list << QString::fromStdString(module);
        return list;
    };
    const auto accent = own(bar.accent, shell.accent);
    return {{"enabled", bar.enabled},
            {"top", bar.top},
            {"height", bar.height},
            {"marginTop", bar.margin[0]},
            {"marginRight", bar.margin[1]},
            {"marginBottom", bar.margin[2]},
            {"marginLeft", bar.margin[3]},
            {"radius", bar.radius},
            {"style", bar.style == paw::TilingBarStyle::Islands ? "islands"
                      : bar.style == paw::TilingBarStyle::Pills ? "pills"
                                                                : "bar"},
            {"color", own(bar.color, shell.panel_color)},
            {"textColor", own(bar.text_color, shell.text_color)},
            {"accent", accent},
            {"borderColor", bar.border_color.empty() ? accent : color(bar.border_color)},
            {"borderWidth", bar.border_width},
            {"font", QString::fromStdString(bar.font.empty() ? shell.font : bar.font)},
            {"fontSize", bar.font_size > 0 ? bar.font_size : shell.font_size},
            {"spacing", bar.spacing},
            {"dots", bar.workspace_dots},
            {"clockFormat", QString::fromStdString(bar.clock_format)},
            {"left", names(bar.left)},
            {"center", names(bar.center)},
            {"right", names(bar.right)}};
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
void ShellController::startBarDrag(const QString &output) {
    if (!panelDraggable() || barDragging_)
        return;
    // Refused once the button is let go, or while the pointer is busy: then there is no drag.
    ask(("panel_drag " + output + "\n").toUtf8(), [](const QByteArray &reply) {
        if (!reply.startsWith("ok"))
            std::cerr << "paw taskbar not dragged: " << reply.trimmed().toStdString() << '\n';
    });
}
void ShellController::cancelBarDrag() {
    if (barDragging_ || barResizing_)
        ask("panel_drag cancel\n", [](const QByteArray &) {});
}
namespace {
// The edges by name, in paw::PanelEdge's order.
const char *const edgeNames[] = {"top", "bottom", "left", "right"};
// The range of the setting `path`, as the schema has it, or `otherwise`.
std::pair<int, int> settingRange(std::string_view path, std::pair<int, int> otherwise) {
    for (const auto &option : paw::config_options())
        if (std::string_view(option.path) == path)
            return {int(option.min), int(option.max)};
    return otherwise;
}
} // namespace
void ShellController::startBarResize(const QString &output, const QString &side, int distance) {
    const auto named = std::find(std::begin(edgeNames), std::end(edgeNames), side.toStdString());
    if (!panelDraggable() || barDragging_ || barResizing_ || named == std::end(edgeNames))
        return;
    barResizeEdge_ = panelSurfaceEdge(output);
    barResizeSide_ = paw::PanelEdge(named - std::begin(edgeNames));
    barResizeFrom_ = distance;
    const auto from = bar_resize::measuredFrom(barResizeEdge_, barResizeSide_);
    ask("panel_drag resize " + QByteArray(edgeNames[int(from)]) + "\n", [this](const QByteArray &reply) {
        if (reply.startsWith("ok"))
            return;
        std::cerr << "paw taskbar not resized: " << reply.trimmed().toStdString() << '\n';
        if (!barResizing_)
            barResizeFrom_ = -1;
    });
}
// "panel-resize OUTPUT DISTANCE" or "panel-resized OUTPUT DISTANCE", as words: the bar follows the
// pointer from where it was pressed, or, when the shell did not ask, by the inner edge of OUTPUT's
// bar from where the compositor first says the pointer is.
void ShellController::handleBarResize(const QStringList &words) {
    bool number = false;
    const int distance = words.value(2).toInt(&number);
    if (words.size() != 3 || !number)
        return;
    if (!barResizing_) {
        barResizing_ = true;
        if (barResizeFrom_ < 0) {
            barResizeEdge_ = panelSurfaceEdge(words[1]);
            barResizeSide_ = bar_resize::inner(barResizeEdge_);
            barResizeFrom_ = distance;
        }
        barResizeStart_ = {config_.shell.panel_height, std::to_array(config_.shell.panel_margin)};
        barResize_ = barResizeStart_;
        Q_EMIT barDragChanged();
    }
    static const auto heights = settingRange("shell.panel_height", {24, 100});
    static const auto margins = settingRange("shell.panel_margin.top", {0, 200});
    const auto size = bar_resize::resized(barResizeStart_, barResizeEdge_, barResizeSide_,
                                          distance - barResizeFrom_, heights, margins);
    if (words[0] == "panel-resized") {
        barResize_ = size;
        // Written while the bar is still drawn at that size, so that it never shows the old one.
        resizePanel(size);
        barResizing_ = false;
        barResizeFrom_ = -1;
        Q_EMIT barDragChanged();
        Q_EMIT panelSizeChanged();
    } else if (size != barResize_) {
        barResize_ = size;
        Q_EMIT panelSizeChanged();
    }
}
// "panel-drag OUTPUT EDGE", "panel-dropped OUTPUT EDGE" or "panel-drag-cancelled", or a resize's.
void ShellController::handleBarDrag(const QString &line) {
    const auto words = line.split(' ');
    if (words[0].startsWith("panel-resize")) {
        handleBarResize(words);
        return;
    }
    if (words[0] == "panel-drag-cancelled" && barResizing_) {
        // The old size back.
        barResizing_ = false;
        barResizeFrom_ = -1;
        Q_EMIT barDragChanged();
        Q_EMIT panelSizeChanged();
        return;
    }
    const bool dropped = words[0] == "panel-dropped";
    barDragging_ = words[0] == "panel-drag" && words.size() == 3;
    if (words.size() == 3) {
        barDragOutput_ = words[1];
        barDragEdge_ = words[2];
    }
    Q_EMIT barDragChanged();
    if (dropped && words.size() == 3)
        movePanel(words[1], words[2]);
}
void ShellController::movePanel(const QString &output, const QString &edge) {
    static const std::map<QString, paw::PanelEdge> edges{{"top", paw::PanelEdge::Top},
                                                         {"bottom", paw::PanelEdge::Bottom},
                                                         {"left", paw::PanelEdge::Left},
                                                         {"right", paw::PanelEdge::Right}};
    const auto it = edges.find(edge);
    const auto name = output.toStdString();
    if (it == edges.end() || output.isEmpty() || it->second == config_.shell.edge_of(name))
        return;
    // That monitor's entry, or the edge of every monitor.
    const bool alone = config_.shell.panel_per_monitor;
    const std::vector<std::string> setting =
        alone ? std::vector<std::string>{"shell", "panel_positions", name}
              : std::vector<std::string>{"shell", "panel_position"};
    try {
        paw::save_setting(path_, setting, '"' + edge.toStdString() + '"',
                          [&](const paw::Config &config) {
                              return config.shell.panel_per_monitor == alone &&
                                     (alone ? config.shell.edge_of(name)
                                            : config.shell.panel_edge) == it->second;
                          });
    } catch (const std::exception &error) {
        report("Could not move the taskbar: " + QString::fromUtf8(error.what()));
        return;
    }
    std::cerr << "paw taskbar " << (alone ? "on " + name : std::string("of every monitor"))
              << " moved to the " << edge.toStdString() << " edge\n";
    // At once, rather than waiting for the compositor, which reloads too while it watches the file.
    reload();
}
void ShellController::resizePanel(const bar_resize::Size &size) {
    const auto &shell = config_.shell;
    const auto margin = std::to_array(shell.panel_margin);
    if (size.height == shell.panel_height && size.margin == margin)
        return;
    static const char *const sides[] = {"top", "right", "bottom", "left"};
    // The height, and the sides of the margin that changed; with `whole`, all four as one table.
    auto edits = [&](bool whole) {
        std::vector<paw::SettingEdit> edits;
        if (size.height != shell.panel_height)
            edits.push_back({{"shell", "panel_height"}, std::to_string(size.height),
                             [height = size.height](const paw::Config &config) {
                                 return config.shell.panel_height == height;
                             }});
        std::string table;
        for (int i = 0; i < 4; ++i) {
            table += (i ? ", " : "{ ") + std::string(sides[i]) + " = " + std::to_string(size.margin[i]);
            if (!whole && size.margin[i] != margin[i])
                edits.push_back({{"shell", "panel_margin", sides[i]}, std::to_string(size.margin[i]),
                                 [i, side = size.margin[i]](const paw::Config &config) {
                                     return config.shell.panel_margin[i] == side;
                                 }});
        }
        if (whole)
            edits.push_back({{"shell", "panel_margin"}, table + " }",
                             [wanted = size.margin](const paw::Config &config) {
                                 return std::to_array(config.shell.panel_margin) == wanted;
                             }});
        return edits;
    };
    try {
        try {
            paw::save_settings(path_, edits(false));
        } catch (const std::runtime_error &) {
            // A margin given as one number, for every side, has no side of its own to set.
            if (size.margin == margin)
                throw;
            paw::save_settings(path_, edits(true));
        }
    } catch (const std::exception &error) {
        report("Could not resize the taskbar: " + QString::fromUtf8(error.what()));
        return;
    }
    std::cerr << "paw taskbar resized to " << size.height << " pixels, its margins " << size.margin[0]
              << ' ' << size.margin[1] << ' ' << size.margin[2] << ' ' << size.margin[3]
              << " (top, right, bottom, left)\n";
    reload();
}
void ShellController::clearError() {
    error_.clear();
    Q_EMIT errorChanged();
}
bool ShellController::succeeded(const QString &error, const QString &failure) {
    if (!error.isEmpty()) {
        report(failure + ": " + error);
        return false;
    }
    clearError();
    return true;
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
    QStringList command;
    for (const auto &word : it->command)
        command << QString::fromStdString(word);
    return startProgram(command, it->name);
}
bool ShellController::startProgram(const QStringList &command, const QString &name) {
    if (command.isEmpty())
        return false;
    QProcess process;
    auto env = QProcessEnvironment::systemEnvironment();
    env.remove("QT_WAYLAND_SHELL_INTEGRATION");
    process.setProcessEnvironment(env);
    process.setWorkingDirectory(QDir::homePath());
    process.setProgram(command.front());
    process.setArguments(command.mid(1));
    if (!process.startDetached()) {
        report("Could not launch " + name + ": " + process.errorString());
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
    // The Trash's own location where GIO knows it, else its folder.
    QString error = gioOpen("trash:///");
    if (!error.isEmpty()) {
        QDir().mkpath(trashFolder() + "/files");
        error = gioOpen(QUrl::fromLocalFile(trashFolder() + "/files").toEncoded());
    }
    return succeeded(error, "Could not open the Trash");
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
void ShellController::configureDesktopClock() {
    const auto &clock = config_.shell.desktop_clock;
    desktopClock_.configure(clock, color(clock.color.empty() ? config_.shell.text_color : clock.color));
}
void ShellController::configureDesktopIcons() {
    const auto &icons = config_.shell.desktop_icons;
    DesktopIcons::Settings settings;
    settings.enabled = icons.enabled;
    settings.size = icons.size;
    settings.folder = QString::fromStdString(icons.folder);
    if (settings.folder.startsWith("~/"))
        settings.folder = QDir::homePath() + settings.folder.sliced(1);
    // Unset, the environment's XDG_DESKTOP_DIR, else the one user-dirs.dirs names.
    if (settings.folder.isEmpty()) {
        settings.folder = qEnvironmentVariable("XDG_DESKTOP_DIR");
        if (settings.folder.isEmpty() || QDir::isRelativePath(settings.folder))
            settings.folder = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    }
    settings.folder = QDir::cleanPath(settings.folder);
    // The launchers as refreshApps lists them, by their places.
    QList<DesktopIcons::Launcher> launchers;
    int index = 0;
    for (const auto &launcher : config_.shell.launchers)
        launchers.push_back({QString("pinned:%1").arg(index++), QString::fromStdString(launcher.name),
                             QString::fromStdString(launcher.icon)});
    desktopIcons_.configure(settings, launchers);
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
    // In the default browser.
    return succeeded(gioOpen(url.toUtf8()), "Could not open " + url);
}
bool ShellController::openFile(const QString &path, bool folder) {
    const auto name = QFileInfo(path).fileName();
    return succeeded(FileIndex::open(path, folder),
                     "Could not open " + (folder ? "the folder of " + name : name));
}
bool ShellController::start(GAppInfo *info, const QString &name) {
    return succeeded(gioLaunch([info](GAppLaunchContext *context, GError **error) {
                         return g_app_info_launch(info, nullptr, context, error);
                     }),
                     "Could not launch " + name);
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
        gioLaunch([info, &name](GAppLaunchContext *context, GError **) {
            g_desktop_app_info_launch_action(info, name.constData(), context);
            return true;
        });
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
        const auto before = config_.profile;
        loadConfig(true);
        const bool restyle = !config_.profile.empty() && config_.profile != before;
        watchTrash();
        notifications_.configure(config_.notifications);
        osd_.configure(config_.osd);
        configureDesktopClock();
        configureDesktopIcons();
        configureWallpaperCycle();
        power_.setCountdown(config_.power.countdown);
        configureSearch();
        configureClipboard();
        updateNotificationService();
        updateTrayHost();
        updatePolkitAgent();
        refreshApps();
        // A new appearance profile shows at once, under the compositor's picture of the old one,
        // which crossfades to it once Restyle finds it on the screen. Any reload says which
        // profile is drawn once it is: switched there and back, the compositor waits for the one
        // the shell had all along.
        if (restyle && !restyling_) {
            restyling_ = true;
            Q_EMIT restylingChanged();
        }
        Q_EMIT configChanged();
        Q_EMIT panelSizeChanged();
        Q_EMIT wallpaperChanged();
        // Tiling follows layout.tiling until the compositor says.
        if (!tilingKnown_)
            Q_EMIT tilingChanged();
        if (!config_.profile.empty())
            restyle_.start();
        if (!enabled())
            Q_EMIT disabled();
    } catch (const std::exception &error) {
        std::cerr << "Shell reload rejected: " << error.what() << '\n';
        report("Configuration unchanged: " + QString::fromUtf8(error.what()));
    }
}
void ShellController::restyled() {
    if (restyling_) {
        restyling_ = false;
        Q_EMIT restylingChanged();
    }
    // None, when a reload since left the configuration without profiles: nothing to tell.
    if (config_.profile.empty())
        return;
    // Quietly: a shell without a compositor, or one that does not crossfade, has no one to tell.
    const auto name = QString::fromStdString(config_.profile);
    ask(("profile_drawn " + name + "\n").toUtf8(), [](const QByteArray &) {});
}
void ShellController::loadConfig(bool reloading) {
    std::string error;
    auto next = paw::load_config_or_default(path_, error, reloading);
    config_ = std::move(next);
    glass_.configure(config_.shell.blur);
    configError_ = QString::fromStdString(error);
    if (!error.empty())
        std::cerr << "Shell configuration error; using the default configuration: " << error
                  << '\n';
    followIconTheme();
}
void ShellController::followIconTheme() {
    // Only as the setting changes: a theme set meanwhile otherwise (a test's, the preview's)
    // stands until then.
    const auto wanted = QString::fromUtf8(config_.settings.icon_theme);
    if (wanted == iconThemeWanted_)
        return;
    iconThemeWanted_ = wanted;
    QString theme = iconThemeBase_;
    if (iconThemeInstalled(wanted))
        theme = wanted;
    else if (!wanted.isEmpty())
        std::cerr << "The icon theme " << wanted.toStdString() << " is not installed; skipping it"
                  << (theme.isEmpty() ? "" : " for " + theme.toStdString()) << '\n';
    {
        std::lock_guard guard(iconThemeLock());
        if (QIcon::themeName() == theme)
            return;
        // Qt looks in its own folders again as the theme is cleared, leaving any it was given.
        const auto folders = QIcon::themeSearchPaths();
        QIcon::setThemeName(theme);
        if (QIcon::themeSearchPaths() != folders)
            QIcon::setThemeSearchPaths(folders);
    }
    // What was found in the old theme is looked for again: the icons guessed for windows, the
    // tray's pictures and menus, and every image://icons image, by a URL of its own.
    iconFor_.clear();
    tray_.redraw();
    ++iconGeneration_;
    Q_EMIT iconThemeChanged();
}
QString ShellController::iconBase() const {
    return iconGeneration_ ? QString("image://icons/@%1/").arg(iconGeneration_)
                           : QString("image://icons/");
}
QString ShellController::iconTheme() const {
    std::lock_guard guard(iconThemeLock());
    return QIcon::themeName();
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
                tilingKnown_ = true;
                nextWorkspaces_.clear();
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
                                                        {"touched", QVariantList()},
                                                        {"tiling", words[4] == "on"}};
            } else if (line.startsWith("layout ")) {
                // layout NAME LAYOUT: the tiling layout of the workspace the output shows.
                const auto words = line.split(' ');
                if (words.size() != 3 || !nextWorkspaces_.contains(words[1]))
                    continue;
                auto state = nextWorkspaces_[words[1]].toMap();
                state["layout"] = words[2];
                nextWorkspaces_[words[1]] = state;
            } else if (line.startsWith("touched ")) {
                // touched NAME EDGES: the edges of the output's usable area a window reaches,
                // "top,left" or "-", along which a floating bar docks.
                const auto words = line.split(' ');
                if (words.size() != 3 || !nextWorkspaces_.contains(words[1]))
                    continue;
                QVariantList edges;
                for (const auto &edge : words[2].split(',', Qt::SkipEmptyParts))
                    if (edge != "-")
                        edges.push_back(edge);
                auto state = nextWorkspaces_[words[1]].toMap();
                state["touched"] = edges;
                nextWorkspaces_[words[1]] = state;
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
            } else if (line.startsWith("cursor-theme ")) {
                // cursor-theme SIZE NAME: the cursor the compositor draws, which the programs
                // the shell starts draw over their windows too.
                // Set as it changes alone: other threads read the environment too.
                const auto size = line.section(' ', 1, 1).toUtf8();
                const auto theme = line.section(' ', 2).toUtf8();
                if (size.toInt() > 0 && !theme.isEmpty() &&
                    (qgetenv("XCURSOR_SIZE") != size || qgetenv("XCURSOR_THEME") != theme)) {
                    qputenv("XCURSOR_SIZE", size);
                    qputenv("XCURSOR_THEME", theme);
                }
            } else if (line.startsWith("focused ")) {
                // The output the compositor says has the focus, where overlays for it go.
                focusedOutput_ = line.sliced(8) == "-" ? QString() : line.sliced(8);
            } else if (line.startsWith("primary ")) {
                if (const auto primary = line.sliced(8) == "-" ? QString() : line.sliced(8);
                    primary != primaryOutput_) {
                    primaryOutput_ = primary;
                    Q_EMIT primaryOutputChanged();
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
            } else if (line.startsWith("profile-schedule ")) {
                // profile-schedule on|off: the profile in use is the one the schedule wants.
                setProfileScheduled(line.sliced(17) == "on");
            } else if (line.startsWith("keep-awake ")) {
                // keep-awake on|off
                setKeepAwake(line.sliced(11) == "on");
            } else if (line.startsWith("notice ")) {
                const auto fields = line.sliced(7).split('\t');
                notice(fields[0], fields.value(1));
            } else if (line.startsWith("color-picked ")) {
                colorPicked(line.sliced(13));
            } else if (line.startsWith("dnd ")) {
                handleDnd(line.sliced(4));
            } else if (line.startsWith("panel-drag") || line.startsWith("panel-dropped ") ||
                       line.startsWith("panel-resize")) {
                handleBarDrag(line);
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
            } else if (displayModes_.handle(line) || displaySettings_.handle(line) ||
                       riceTrial_.handle(line)) {
                // The display mode popup's lines; "display-settings OUTPUT", and a trial's; a
                // rice's trial.
            } else if (line.startsWith("notifications ")) {
                Q_EMIT notificationsRequested(line.sliced(14));
            } else if (line.startsWith("media ")) {
                // media VERB: a media key, for the current player.
                media_->command(line.sliced(6));
            } else if (line.startsWith("clipboard ")) {
                clipboard_.toggle(line.sliced(10));
            } else if (line.startsWith("emoji ")) {
                emoji_.toggle(line.sliced(6));
            } else if (line.startsWith("run ")) {
                // run OUTPUT: the run launcher, opened or closed there.
                runLauncher_.toggle(line.sliced(4));
            } else if (line.startsWith("shortcuts ")) {
                // shortcuts OUTPUT: the sheet of keyboard shortcuts, opened or closed there.
                shortcuts_.toggle(line.sliced(10));
            } else if (line.startsWith("locked ")) {
                // locked on|off: nothing copied while the session is locked is kept, and the
                // wallpaper slideshow waits.
                clipboard_.setLocked(line == "locked on");
                wallpaperCycle_.setPaused(line == "locked on");
            } else if (line.startsWith("power ")) {
                // power ACTIONS: those that may run, as "lock,suspend,logout", or "-".
                power_.setAvailable(line.sliced(6));
            } else if (line.startsWith("power-menu ")) {
                Q_EMIT powerMenuRequested(line.sliced(11));
            } else if (line.startsWith("taskbar ")) {
                Q_EMIT taskbarRequested(line.sliced(8));
            } else if (line.startsWith("power-error ")) {
                report(line.sliced(12));
            } else if (line.startsWith("config-error ")) {
                // A reload the compositor turned down, keeping the configuration; so does the
                // shell, which hears of no reload then.
                report(line.sliced(13));
            } else if (line.startsWith("spawn-error ")) {
                // A program a binding, a hot corner or the palette asked for did not start.
                report(line.sliced(12));
            } else if (line.startsWith("launcher ")) {
                Q_EMIT launcherRequested(line.sliced(9));
            } else if (line.startsWith("palette ")) {
                palette_.open(line.sliced(8));
            } else if (line.startsWith("switcher ")) {
                // switcher OUTPUT SELECTED COUNT [app], then COUNT switcher-window lines; "app"
                // when it lists one application's windows.
                const auto words = line.split(' ');
                if (words.size() < 4)
                    continue;
                nextSwitcher_ = {.output = words[1],
                                 .selected = words[2].toInt(),
                                 .app = words.size() > 4 && words[4] == "app"};
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
            } else if (line.startsWith("xray ")) {
                // xray OUTPUT COUNT HOVERED X Y WIDTH HEIGHT LEGEND (the area the stack keeps to
                // and the legend's column left of it), then COUNT xray-sheet lines.
                const auto words = line.split(' ');
                if (words.size() < 9)
                    continue;
                nextXray_ = {.output = words[1],
                             .hovered = words[3].toInt(),
                             .area = rectAt(words, 4),
                             .legend = words[8].toInt()};
                xrayPending_ = words[2].toInt();
                if (xrayPending_ <= 0)
                    showXray();
            } else if (line.startsWith("xray-sheet ") && xrayPending_ > 0) {
                // LAYER, COLOUR (#rrggbb), NAME, DETAIL, separated by tabs; read untrimmed so an
                // empty field keeps its place.
                const auto fields = raw.sliced(11).chopped(1).split('\t');
                if (fields.size() >= 4)
                    nextXray_.sheets.push_back(QVariantMap{{"layer", fields[0]},
                                                           {"colour", QColor(fields[1])},
                                                           {"name", fields[2]},
                                                           {"detail", fields[3]}});
                if (--xrayPending_ == 0)
                    showXray();
            } else if (line.startsWith("xray-hover ")) {
                xray_.hovered = line.sliced(11).toInt();
                Q_EMIT xrayHoveredChanged();
            } else if (line == "xray-close") {
                clearXray();
            } else if (line.startsWith("caption ")) {
                // caption OUTPUT MILLISECONDS TITLE<tab>BODY<tab>KEYS, "-" for the focused output.
                caption_.showLine(line.sliced(8), overlayOutput());
            } else if (line == "caption-hide") {
                caption_.hide();
            } else if (line.startsWith("wallpaper-cycle ")) {
                // wallpaper-cycle next|prev: the wallpaper_next and wallpaper_prev actions.
                const auto verb = line.sliced(16);
                if (verb == "next" || verb == "prev")
                    stepWallpaper(verb == "next" ? 1 : -1);
            } else if (line.startsWith("desktop-clock ")) {
                // desktop-clock toggle|on|off: the desktop_clock action.
                if (desktopClock_.handle(line.sliced(14)))
                    osd_.show(overlayOutput(), desktopClock_.shown() ? "Desktop clock on"
                                                                     : "Desktop clock off", -1);
            } else if (line.startsWith("desktop-icons ")) {
                // desktop-icons toggle|on|off: the desktop_icons action.
                if (desktopIcons_.handle(line.sliced(14)))
                    osd_.show(overlayOutput(), desktopIcons_.shown() ? "Desktop icons shown"
                                                                     : "Desktop icons hidden", -1);
            }
        }
        if (outputs && workspaces_ != nextWorkspaces_) {
            workspaces_ = nextWorkspaces_;
            Q_EMIT workspacesChanged();
        }
        if (urgentSeen && urgentWindows_ != nextUrgentWindows_) {
            urgentWindows_ = nextUrgentWindows_;
            QList<QPair<QString, QString>> windows;
            for (const auto &item : urgentWindows_) {
                const auto window = item.toMap();
                windows.push_back({window.value("appId").toString(), window.value("title").toString()});
            }
            tasks_.setUrgent(windows);
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
        setProfileScheduled(false);
        setKeepAwake(false);
        setBindingMode({});
        caption_.hide(); // nothing narrates it any more
        if (!urgentWindows_.isEmpty()) {
            urgentWindows_.clear();
            tasks_.setUrgent({});
        }
        Q_EMIT tilingChanged();
        if (!switcher_.output.isEmpty()) {
            switcher_.output.clear();
            switcher_.windows.clear();
            Q_EMIT switcherChanged();
        }
        clearOverview();
        clearXray();
    });
    state_->connectToServer(path);
}
void ShellController::setBindingMode(const QString &mode) {
    if (mode == bindingMode_)
        return;
    bindingMode_ = mode;
    Q_EMIT bindingModeChanged();
}
void ShellController::setProfileScheduled(bool on) {
    if (on == profileScheduled_)
        return;
    profileScheduled_ = on;
    Q_EMIT profileScheduledChanged();
}
void ShellController::setKeepAwake(bool on) {
    if (on == keepAwake_)
        return;
    keepAwake_ = on;
    Q_EMIT keepAwakeChanged();
}
void ShellController::setNightLight(bool on, const QString &mode) {
    if (on == nightLight_ && mode == nightLightMode_)
        return;
    nightLight_ = on;
    nightLightMode_ = mode;
    Q_EMIT nightLightChanged();
}
QString ShellController::taskbarButtons() const {
    switch (config_.shell.taskbar_buttons) {
    case paw::ShellConfig::TaskbarButtons::MainAndOwn:
        return "main_and_own";
    case paw::ShellConfig::TaskbarButtons::Own:
        return "own";
    case paw::ShellConfig::TaskbarButtons::All:
        break;
    }
    return "all";
}
QString ShellController::primaryOutput() const {
    if (!primaryOutput_.isEmpty() && hasOutput(primaryOutput_))
        return primaryOutput_;
    // Without a compositor (a preview), or until it has said: the leftmost screen, the topmost
    // of those at the same x.
    const QScreen *leftmost = nullptr;
    for (const auto *screen : QGuiApplication::screens()) {
        const auto at = screen->geometry().topLeft();
        if (!leftmost || at.x() < leftmost->geometry().x() ||
            (at.x() == leftmost->geometry().x() && at.y() < leftmost->geometry().y()))
            leftmost = screen;
    }
    return leftmost ? leftmost->name() : QString();
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
void ShellController::colorPicked(const QString &hex) {
    static const QRegularExpression rgb("^#[0-9a-f]{6}$");
    if (!rgb.match(hex).hasMatch())
        return;
    if (osd_.config().enabled) {
        osd_.show(overlayOutput(), hex, -1, "color");
        return;
    }
    Notification notification;
    notification.app = "paw";
    notification.icon = "color-select";
    notification.summary = "Colour copied";
    notification.body = hex;
    notifications_.notify(std::move(notification));
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
void ShellController::showXray() {
    xray_ = nextXray_;
    Q_EMIT xrayChanged();
    Q_EMIT xrayHoveredChanged();
}
void ShellController::clearXray() {
    xrayPending_ = 0;
    if (xray_.output.isEmpty())
        return;
    xray_ = {};
    Q_EMIT xrayChanged();
    Q_EMIT xrayHoveredChanged();
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
void ShellController::switcherShown() {
    // A switcher closed meanwhile has the request refused, which needs no word.
    ask("switcher_peek\n", [](const QByteArray &) {});
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
        std::lock_guard guard(iconThemeLock());
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
void ShellController::tileWith(const QString &output, const QString &layout, bool turnOn) {
    const auto line = QString("output %1 layout_%2\n").arg(output, layout).toUtf8();
    if (!turnOn)
        return request(line, "Tiling needs a running paw session.");
    // Each request has a connection of its own, which the compositor may take in either order:
    // tiling is turned on once the layout is set, so that the windows tile with it straight away
    // rather than with the one before.
    ask(line, [this, output](const QByteArray &reply) {
        if (reply.startsWith("ok"))
            toggleTiling(output);
    });
}
void ShellController::showWorkspace(const QString &output, int number) {
    if (!output.isEmpty() && number >= 1 && number <= workspaceCount())
        request(QString("output %1 workspace %2\n").arg(output).arg(number).toUtf8(),
                "Workspaces need a running paw session.");
}
void ShellController::nudgeWorkspace(const QString &output, int way) {
    if (!output.isEmpty() && way && !qEnvironmentVariable("PAW_SOCKET").isEmpty())
        request(QString("workspace_nudge %1 %2\n").arg(output, way > 0 ? "next" : "prev").toUtf8(),
                {});
}
void ShellController::send(const QString &line) {
    request(line.toUtf8() + "\n", "The compositor needs a running paw session.");
}
bool ShellController::adoptsShadowMaterial() {
    // Recheck with every update of Qt, and move the version up while it still leaks the effect
    // (shell_ui fails once it no longer does, or this no longer takes it).
    static const bool leaks = QLibraryInfo::version() < QVersionNumber(6, 12);
    return leaks;
}
void ShellController::keepShadowMaterial(QObject *shadow) {
    // A Qt that deleted the effect itself as the shadow's private part goes, which comes after
    // the shadow's children, would delete it twice: so only for the versions known to leak it.
    if (!adoptsShadowMaterial())
        return;
    auto *material = shadow ? shadow->property("material").value<QObject *>() : nullptr;
    if (material && !material->parent())
        material->setParent(shadow);
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
    // The controller as the context, not the socket: a socket still connected as the shell
    // quits says it disconnected as it is destroyed, among the controller's children, after
    // what `done` answers to has gone; the controller's connections are cut before that.
    connect(socket, &QLocalSocket::disconnected, this, [socket, reply, done] {
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
