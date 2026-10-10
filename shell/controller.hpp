// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "app_match.hpp"
#include "audio.hpp"
#include "authentication.hpp"
#include "backlight.hpp"
#include "bluetooth.hpp"
#include "clipboard.hpp"
#include "display_modes.hpp"
#include "display_settings.hpp"
#include "emoji.hpp"
#include "file_index.hpp"
#include "media.hpp"
#include "notifications.hpp"
#include "power_mode.hpp"
#include "osd.hpp"
#include "volume_keys.hpp"
#include "palette.hpp"
#include "power.hpp"
#include "paw/config.hpp"
#include "start_menu.hpp"
#include "system_status.hpp"
#include "task_model.hpp"
#include "tray.hpp"
#include "wifi.hpp"
#include <QColor>
#include <QHash>
#include <QLocalSocket>
#include <QMap>
#include <QObject>
#include <QRect>
#include <QUrl>
#include <QVariantList>
#include <functional>
#include <vector>

typedef struct _GAppInfo GAppInfo;
class QFileSystemWatcher;
class QQmlEngine;
class ShellController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QColor accent READ accent NOTIFY configChanged)
    Q_PROPERTY(QColor panelColor READ panelColor NOTIFY configChanged)
    Q_PROPERTY(QColor textColor READ textColor NOTIFY configChanged)
    Q_PROPERTY(QColor background READ background NOTIFY configChanged)
    // windows.urgent_color, the colour of whatever marks a window that asks for attention.
    Q_PROPERTY(QColor urgentColor READ urgentColor NOTIFY configChanged)
    // The wallpaper: one picked from the panel for the profile in use, else shell.wallpaper.
    // wallpaperFile is the same as a path, "" for none.
    Q_PROPERTY(QUrl wallpaper READ wallpaper NOTIFY wallpaperChanged)
    Q_PROPERTY(QString wallpaperFile READ wallpaperFile NOTIFY wallpaperChanged)
    // The picker's pictures as {path, name, folder} (see findWallpapers), read by
    // findWallpapers(), and the folder they come from.
    Q_PROPERTY(QVariantList wallpapers READ wallpapers NOTIFY wallpapersChanged)
    Q_PROPERTY(QString wallpaperFolder READ wallpaperFolder NOTIFY configChanged)
    // How the shell is laid out (shell.style): "taskbar" or "macos".
    Q_PROPERTY(QString style READ style NOTIFY configChanged)
    Q_PROPERTY(int panelHeight READ panelHeight NOTIFY configChanged)
    Q_PROPERTY(bool panelTop READ panelTop NOTIFY configChanged)
    Q_PROPERTY(int panelMarginTop READ panelMarginTop NOTIFY configChanged)
    Q_PROPERTY(int panelMarginRight READ panelMarginRight NOTIFY configChanged)
    Q_PROPERTY(int panelMarginBottom READ panelMarginBottom NOTIFY configChanged)
    Q_PROPERTY(int panelMarginLeft READ panelMarginLeft NOTIFY configChanged)
    Q_PROPERTY(int panelExtent READ panelExtent NOTIFY configChanged)
    Q_PROPERTY(int panelRadius READ panelRadius NOTIFY configChanged)
    Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY configChanged)
    Q_PROPERTY(int fontSize READ fontSize NOTIFY configChanged)
    Q_PROPERTY(bool iconsOnly READ iconsOnly NOTIFY configChanged)
    // animations.enabled and animations.speed, which the shell's animations follow too.
    Q_PROPERTY(bool animations READ animations NOTIFY configChanged)
    Q_PROPERTY(qreal animationSpeed READ animationSpeed NOTIFY configChanged)
    // Whether Qt Quick draws through the GPU, which shader effects such as shadows need; its
    // software renderer cannot draw them. Set from what the views actually use.
    Q_PROPERTY(bool effects READ effects NOTIFY effectsChanged)
    Q_PROPERTY(bool groupWindows READ groupWindows NOTIFY configChanged)
    // shell.thumbnails: whether resting on a taskbar button shows pictures of its windows, after
    // how many milliseconds, how wide each is, and whether they follow the windows while shown.
    Q_PROPERTY(bool thumbnails READ thumbnails NOTIFY configChanged)
    Q_PROPERTY(int thumbnailDelay READ thumbnailDelay NOTIFY configChanged)
    Q_PROPERTY(int thumbnailSize READ thumbnailSize NOTIFY configChanged)
    Q_PROPERTY(bool liveThumbnails READ liveThumbnails NOTIFY configChanged)
    // features.sticky: whether a window can be shown on every workspace of its monitor.
    Q_PROPERTY(bool stickyWindows READ stickyWindows NOTIFY configChanged)
    // Which panel widgets Lua enables, and where: {workspaces, clock, calendar, keyboard_layout,
    // power, tray, media, power_mode, bluetooth} as booleans, and {battery, network, volume, tiling,
    // profiles, wallpapers, notifications}, which can move, as "bar", "quick" (Quick Settings) or
    // "" (hidden).
    Q_PROPERTY(QVariantMap widgets READ widgets NOTIFY configChanged)
    // The compositor's active keyboard layout: {number (from 1), count, short ("us"), name}, or
    // empty without a compositor.
    Q_PROPERTY(QVariantMap keyboardLayout READ keyboardLayout NOTIFY keyboardLayoutChanged)
    // The compositor's night light: whether it warms the screen now, and who decides: "auto"
    // (the schedule), "on" or "off" (an override), or "" without a compositor.
    Q_PROPERTY(bool nightLight READ nightLight NOTIFY nightLightChanged)
    Q_PROPERTY(QString nightLightMode READ nightLightMode NOTIFY nightLightChanged)
    // The binding mode in use, "" outside any.
    Q_PROPERTY(QString bindingMode READ bindingMode NOTIFY bindingModeChanged)
    Q_PROPERTY(QVariantList pinned READ pinned NOTIFY appsChanged)
    // Configured launchers and installed applications, as {appId, name, icon, pinned (to the
    // taskbar), configured, genericName, keywords, description}.
    Q_PROPERTY(QVariantList apps READ apps NOTIFY appsChanged)
    // Whether the user's trash holds anything, for the dock's Trash: watched once the style is
    // macOS, which has a dock.
    Q_PROPERTY(bool trashFull READ trashFull NOTIFY trashChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    // What is wrong with the configuration while the default one stands in for it; "" when it
    // loaded.
    Q_PROPERTY(QString configError READ configError NOTIFY configChanged)
    Q_PROPERTY(TaskModel *tasks READ tasks CONSTANT)
    Q_PROPERTY(Audio *audio READ audio CONSTANT)
    Q_PROPERTY(SystemStatus *status READ status CONSTANT)
    // The screen backlight, which Quick Settings sets.
    Q_PROPERTY(Backlight *backlight READ backlight CONSTANT)
    // The media players (MPRIS), which Quick Settings and the media keys control.
    Q_PROPERTY(Media *media READ media CONSTANT)
    // The power mode (power-profiles-daemon), which Quick Settings switches.
    Q_PROPERTY(PowerMode *powerMode READ powerMode CONSTANT)
    // Wi-Fi and the connection out (NetworkManager), which Quick Settings and the network widget
    // show and control.
    Q_PROPERTY(Wifi *wifi READ wifi CONSTANT)
    // Bluetooth (BlueZ), which Quick Settings switches, connects and pairs.
    Q_PROPERTY(Bluetooth *bluetooth READ bluetooth CONSTANT)
    // The notification daemon (cards, history, do-not-disturb) and the on-screen display.
    Q_PROPERTY(NotificationCenter *notifications READ notifications CONSTANT)
    Q_PROPERTY(Osd *osd READ osd CONSTANT)
    // The display mode popup (Windows' Win+P), which the compositor opens and steps.
    Q_PROPERTY(DisplayModes *displayModes READ displayModes CONSTANT)
    // The display settings window: the monitors' settings, tried and kept.
    Q_PROPERTY(DisplaySettings *displaySettings READ displaySettings CONSTANT)
    // The system tray's items, empty until startTray().
    Q_PROPERTY(TrayModel *tray READ tray CONSTANT)
    // The output showing the notification cards now: chosen when the first card appears and kept
    // until the last is gone.
    Q_PROPERTY(QString cardsOutput READ cardsOutput NOTIFY cardsOutputChanged)
    Q_PROPERTY(bool tiling READ tiling NOTIFY tilingChanged)
    Q_PROPERTY(bool tilingAvailable READ tilingAvailable NOTIFY tilingChanged)
    Q_PROPERTY(int workspaceCount READ workspaceCount NOTIFY configChanged)
    // shell.workspaces_shown: how many the indicator shows around the current one; 0: all.
    Q_PROPERTY(int workspacesShown READ workspacesShown NOTIFY configChanged)
    // layout.workspace_names, padded with "" to workspaceCount.
    Q_PROPERTY(QStringList workspaceNames READ workspaceNames NOTIFY configChanged)
    Q_PROPERTY(QVariantMap workspaces READ workspaces NOTIFY workspacesChanged)
    // The configuration's appearance profiles, sorted, and the one in use ("" for none).
    Q_PROPERTY(QStringList profiles READ profiles NOTIFY configChanged)
    Q_PROPERTY(QString profile READ profile NOTIFY configChanged)
    // The compositor's window switcher (Alt+Tab): the output showing it, empty while closed, and
    // its windows as {appId, title, output, workspace, minimized, urgent, id}, most recently
    // focused first; `id` is the window's number (TaskModel's windowId), 0 when not given.
    Q_PROPERTY(QString switcherOutput READ switcherOutput NOTIFY switcherChanged)
    Q_PROPERTY(QVariantList switcherWindows READ switcherWindows NOTIFY switcherChanged)
    Q_PROPERTY(int switcherSelected READ switcherSelected NOTIFY switcherSelectedChanged)
    // The command palette (Super + P).
    Q_PROPERTY(Palette *palette READ palette CONSTANT)
    // The power menu: what may run of lock, suspend, hibernate and the rest.
    Q_PROPERTY(Power *power READ power CONSTANT)
    // The polkit agent's requests, for the authentication dialog.
    Q_PROPERTY(Authentication *authentication READ authentication CONSTANT)
    // The start menu's pins, launch history, applications by letter, search and user.
    Q_PROPERTY(StartMenu *startMenu READ startMenu CONSTANT)
    // The history of what is copied (shell.clipboard), and its popup.
    Q_PROPERTY(ClipboardHistory *clipboard READ clipboard CONSTANT)
    // The emoji picker.
    Q_PROPERTY(EmojiPicker *emoji READ emoji CONSTANT)
    // The compositor's overview: the output showing it, empty while closed; its thumbnails as
    // {x, y, w, h, appId, title, workspace, urgent} and workspace strip cells as {x, y, w, h,
    // workspace, windows}, in the output's coordinates; the selected thumbnail, the workspace
    // shown (from 1) and the typed filter. The compositor draws the thumbnails; the shell draws
    // the text over them.
    Q_PROPERTY(QString overviewOutput READ overviewOutput NOTIFY overviewChanged)
    Q_PROPERTY(QVariantList overviewWindows READ overviewWindows NOTIFY overviewChanged)
    Q_PROPERTY(QVariantList overviewStrip READ overviewStrip NOTIFY overviewChanged)
    Q_PROPERTY(int overviewViewed READ overviewViewed NOTIFY overviewChanged)
    Q_PROPERTY(QString overviewFilter READ overviewFilter NOTIFY overviewChanged)
    // What the panels leave of the output, as a rect in its coordinates.
    Q_PROPERTY(QRect overviewArea READ overviewArea NOTIFY overviewChanged)
    Q_PROPERTY(int overviewSelected READ overviewSelected NOTIFY overviewSelectedChanged)
    // Snap Assist: the overview in the free slot beside a window just snapped, `overviewArea`
    // being that slot; it has no strip and no search.
    Q_PROPERTY(bool overviewAssist READ overviewAssist NOTIFY overviewChanged)
  public:
    explicit ShellController(std::filesystem::path path, QObject *parent = nullptr);
    ~ShellController() override;
    QColor accent() const;
    QColor panelColor() const;
    QColor textColor() const;
    QColor background() const;
    QColor urgentColor() const;
    // The windows asking for attention, longest waiting first (the first 16), as {output,
    // workspace, appId, title}, which the taskbar marks. Each output's entry in `workspaces`
    // lists the workspaces they are on, as `urgent`.
    QVariantList urgentWindows() const { return urgentWindows_; }
    QUrl wallpaper() const;
    QString wallpaperFile() const;
    QVariantList wallpapers() const { return wallpapers_; }
    QString wallpaperFolder() const;
    // Reads the picker's folder again.
    Q_INVOKABLE void findWallpapers();
    // Shows `path` as the wallpaper of the profile in use, remembered across sessions until the
    // configured wallpaper changes; an empty path goes back to the configured one.
    Q_INVOKABLE void pickWallpaper(const QString &path);
    QString style() const { return config_.shell.macos_style ? "macos" : "taskbar"; }
    int panelHeight() const { return config_.shell.panel_height; }
    bool panelTop() const { return config_.shell.panel_top; }
    int panelMarginTop() const { return config_.shell.panel_margin[0]; }
    int panelMarginRight() const { return config_.shell.panel_margin[1]; }
    int panelMarginBottom() const { return config_.shell.panel_margin[2]; }
    int panelMarginLeft() const { return config_.shell.panel_margin[3]; }
    // The strip the panel reserves: the bar and the margins above and below it.
    int panelExtent() const { return panelHeight() + panelMarginTop() + panelMarginBottom(); }
    // Whether the panel's surface lies along the output's top edge: as shell.panel_position says,
    // but for the macOS style's dock, which is at the bottom.
    bool panelSurfaceTop() const { return panelTop() && !config_.shell.macos_style; }
    // The room the panel's surface has above the strip it reserves: the dock's, for an icon to
    // bounce in; none for the taskbar.
    int panelHeadroom() const { return config_.shell.macos_style ? panelHeight() / 2 : 0; }
    // How far below the bars the command palette opens on an output `height` tall: a sixth of
    // it, or a quarter in the macOS style, where Spotlight opens.
    int paletteDrop(int height) const { return height / (config_.shell.macos_style ? 4 : 6); }
    // How far from the output's bottom edge the on-screen display stands there: 48 pixels, or
    // clear of the macOS style's dock.
    int osdBottom() const { return config_.shell.macos_style ? panelExtent() + 16 : 48; }
    int panelRadius() const { return config_.shell.panel_radius; }
    QString fontFamily() const { return QString::fromStdString(config_.shell.font); }
    int fontSize() const { return config_.shell.font_size; }
    bool softwareRenderer() const { return config_.shell.software_renderer; }
    bool iconsOnly() const { return config_.shell.icons_only; }
    bool animations() const { return config_.settings.animations; }
    qreal animationSpeed() const { return config_.settings.animation_speed; }
    bool effects() const { return effects_; }
    void setEffects(bool effects);
    bool groupWindows() const { return config_.shell.group_windows; }
    bool thumbnails() const { return config_.shell.thumbnails.enabled; }
    int thumbnailDelay() const { return config_.shell.thumbnails.delay; }
    int thumbnailSize() const { return config_.shell.thumbnails.size; }
    bool liveThumbnails() const { return config_.shell.thumbnails.live; }
    bool stickyWindows() const { return config_.settings.sticky; }
    bool enabled() const { return config_.shell.enabled; }
    QStringList profiles() const;
    QString profile() const { return QString::fromStdString(config_.profile); }
    // Switches to another appearance profile: the compositor saves the choice and reloads.
    Q_INVOKABLE void pickProfile(const QString &name) { send("profile " + name); }
    QVariantMap widgets() const;
    QVariantMap keyboardLayout() const { return keyboardLayout_; }
    bool nightLight() const { return nightLight_; }
    QString nightLightMode() const { return nightLightMode_; }
    QString bindingMode() const { return bindingMode_; }
    // What the compositor says of night light; the preview's stand-in says it too.
    void setNightLight(bool on, const QString &mode);
    QVariantList pinned() const;
    QVariantList apps() const;
    QString error() const { return error_; }
    QString configError() const { return configError_; }
    TaskModel *tasks() { return &tasks_; }
    Audio *audio() { return audio_.get(); }
    Palette *palette() { return &palette_; }
    Power *power() { return &power_; }
    Authentication *authentication() { return &authentication_; }
    // Registers the shell as the session's polkit authentication agent when it is built in and
    // shell.polkit_agent is on, unless another agent serves the session; later configuration
    // reloads follow the setting. Returns whether it is the agent.
    bool startPolkit();
    StartMenu *startMenu() { return &startMenu_; }
    // The files the palette's and the start menu's search find (shell.search).
    FileIndex *files() { return &files_; }
    ClipboardHistory *clipboard() { return &clipboard_; }
    EmojiPicker *emoji() { return &emoji_; }
    // Opens a file in its default application, or with `folder` the folder that holds it; a
    // failure shows across the panel.
    Q_INVOKABLE bool openFile(const QString &path, bool folder = false);
    // shell.search.web: the search engine's address, %s where the words go; empty for none.
    QString webSearch() const { return QString::fromStdString(config_.shell.search.web); }
    // Opens an address in the default browser; a failure shows across the panel.
    Q_INVOKABLE bool openUrl(const QString &url);
    // Sends the compositor a request (an action, or "session restore NAME"), as `paw msg`
    // would; `done` gets its whole reply. Without a session, `done` is not called.
    void ask(const QByteArray &line, std::function<void(const QByteArray &)> done);
    Q_INVOKABLE void send(const QString &line);
    // The one QML engine every view runs in, made on first use: the shell's types, icons and
    // imports load once instead of once per output.
    QQmlEngine *engine();
    SystemStatus *status() { return &status_; }
    Backlight *backlight() { return &backlight_; }
    Media *media() { return media_.get(); }
    PowerMode *powerMode() { return powerMode_.get(); }
    Wifi *wifi() { return wifi_.get(); }
    Bluetooth *bluetooth() { return bluetooth_.get(); }
    NotificationCenter *notifications() { return &notifications_; }
    Osd *osd() { return &osd_; }
    DisplayModes *displayModes() { return &displayModes_; }
    DisplaySettings *displaySettings() { return &displaySettings_; }
    QString cardsOutput() const { return cardsOutput_; }
    // The output that overlays for the focused monitor belong on: the focused one when it
    // exists, else the primary screen.
    QString overlayOutput() const { return outputOrPrimary(focusedOutput_); }
    // Whether an output of that name is connected.
    static bool hasOutput(const QString &name);
    // `name` while an output of that name is connected, else the primary screen's ("" without
    // one), as what belongs on an output that has gone shows there.
    static QString outputOrPrimary(const QString &name);
    // Something the compositor tells the user once ("notice SUMMARY<tab>BODY"), such as a window
    // taking the keyboard's shortcuts: a notification of the desktop's own, kept in the history,
    // and the summary on the on-screen display while no card shows (notifications off, or do not
    // disturb).
    void notice(const QString &summary, const QString &body);
    // Starts answering on the session bus when notifications are enabled and QtDBus is built
    // in; later configuration reloads follow the setting. Returns whether it is serving.
    bool startNotifications();
    TrayModel *tray() { return &tray_; }
    // Starts the tray's host on the session bus when QtDBus is built in and shell.widgets.tray
    // is on; later configuration reloads follow the setting. Returns whether it is running.
    bool startTray();
    bool tiling() const { return tiling_; }
    bool tilingAvailable() const { return subscribed_; }
    int workspaceCount() const { return config_.settings.workspaces; }
    int workspacesShown() const { return config_.shell.workspaces_shown; }
    QStringList workspaceNames() const {
        QStringList names;
        for (int i = 0; i < workspaceCount(); ++i)
            names << (i < static_cast<int>(config_.workspace_names.size())
                          ? QString::fromStdString(config_.workspace_names[i])
                          : QString());
        return names;
    }
    // By output name: {current: N, occupied: [N, ...], tiling: bool}, numbered from 1.
    QVariantMap workspaces() const { return workspaces_; }
    QString switcherOutput() const { return switcher_.output; }
    QVariantList switcherWindows() const { return switcher_.windows; }
    int switcherSelected() const { return switcher_.selected; }
    // Focuses the switcher's window at `index` and closes it.
    Q_INVOKABLE void switcherPick(int index);
    QString overviewOutput() const { return overview_.output; }
    QVariantList overviewWindows() const { return overview_.windows; }
    QVariantList overviewStrip() const { return overview_.strip; }
    int overviewViewed() const { return overview_.viewed; }
    QString overviewFilter() const { return overview_.filter; }
    QRect overviewArea() const { return overview_.area; }
    int overviewSelected() const { return overview_.selected; }
    bool overviewAssist() const { return overview_.assist; }
    Q_INVOKABLE bool launch(const QString &id);
    bool trashFull() const { return trashFull_; }
    // Opens the trash in the file manager, as `gio open trash:///` does, or its folder where
    // nothing opens trash:///; a failure shows across the panel.
    Q_INVOKABLE bool openTrash();
    // What an installed application's desktop entry offers besides starting it (its [Desktop
    // Action …] groups, such as "New window"), in the entry's order, as {action, name, icon}:
    // icon is the action's own icon name or path, "" when it has none. Empty for a configured
    // launcher or an id that is not installed.
    Q_INVOKABLE QVariantList appActions(const QString &id) const;
    // Runs one of an application's actions as launch() starts the application, a failure shown
    // across the panel the same way; returns whether it started.
    Q_INVOKABLE bool launchAction(const QString &id, const QString &action);
    // Reads the installed applications again, as GIO's monitor says they changed.
    void refreshApps();
    // Pins an installed application to the taskbar, remembered across sessions. Configured
    // launchers stay pinned; unpinning them means editing the configuration.
    Q_INVOKABLE void pin(const QString &id);
    Q_INVOKABLE void unpin(const QString &id);
    // Puts the applications pinned from the shell in the order they have in `order`, as the
    // taskbar has them; configured launchers keep their places ahead of them.
    void orderPins(const QStringList &order);
    // The installed application a window's app id belongs to, or empty when none matches.
    Q_INVOKABLE QString appFor(const QString &windowAppId) const;
    Q_INVOKABLE bool isPinned(const QString &id) const;
    // The pinned application whose taskbar slot a window with this app id takes over, or empty.
    Q_INVOKABLE QString pinnedAppFor(const QString &windowAppId) const;
    // The icon of a window's application, else of a desktop entry the menus leave out that it
    // matches, else an icon the theme has by a name guessed from its app id, else
    // application-x-executable.
    Q_INVOKABLE QString iconFor(const QString &windowAppId) const;
    Q_INVOKABLE void reload();
    Q_INVOKABLE void clearError();
    // Toggles tiling on `output`, or on the focused output when it is empty.
    Q_INVOKABLE void toggleTiling(const QString &output = {});
    Q_INVOKABLE void showWorkspace(const QString &output, int number);
  Q_SIGNALS:
    void configChanged();
    void effectsChanged();
    void wallpaperChanged();
    void wallpapersChanged();
    void appsChanged();
    void errorChanged();
    void disabled();
    void tilingChanged();
    void workspacesChanged();
    void launcherRequested(const QString &output);
    void switcherChanged();
    void switcherSelectedChanged();
    void overviewChanged();
    void overviewSelectedChanged();
    void cardsOutputChanged();
    void keyboardLayoutChanged();
    void nightLightChanged();
    void bindingModeChanged();
    void trashChanged();
    // The compositor asked for the notification history on `output`.
    void notificationsRequested(const QString &output);
    // The compositor asked for the power menu on `output`.
    void powerMenuRequested(const QString &output);
    // The compositor asked the taskbar (or the dock) on `output` to take the keyboard, or to give
    // it back (the taskbar_focus action).
    void taskbarRequested(const QString &output);

  private:
    struct App {
        QString id, name, icon;
        paw::Command command;
        GAppInfo *info = nullptr;
        bool pinned = false;
        QString wmClass;
        // What else a search finds it by: its desktop entry's GenericName, Keywords and Comment.
        QString genericName = {};
        QStringList keywords = {};
        QString description = {};
    };
    // Loads the configuration, or the default one with configError_ set when it has an error.
    void loadConfig();
    // Hands the file index and the start menu shell.search's settings.
    void configureSearch();
    // Hands the clipboard history shell.clipboard's.
    void configureClipboard();
    std::filesystem::path path_;
    paw::Config config_;
    TaskModel tasks_;
    FileIndex files_;
    Palette palette_{*this};
    Power power_{*this};
    StartMenu startMenu_{{}, this};
    ClipboardHistory clipboard_;
    EmojiPicker emoji_;
    // Types `text` into what has the keyboard through the compositor, else copies it and says so.
    void typeText(const QString &text);
    std::unique_ptr<Audio> audio_ = makeAudio();
    SystemStatus status_{"/sys", nullptr, true};
    NotificationCenter notifications_;
    Osd osd_;
    DisplayModes displayModes_;
    DisplaySettings displaySettings_{[this](const QByteArray &line,
                                            std::function<void(const QByteArray &)> done) {
        ask(line, std::move(done));
    }};
    // $PAW_SYSFS names another sysfs tree, polled, for tests.
    Backlight backlight_{qEnvironmentVariable("PAW_SYSFS", "/sys"), !qEnvironmentVariableIsSet("PAW_SYSFS"),
                         qEnvironmentVariableIsSet("PAW_SYSFS") ? 100 : 0};
    // The compositor's volume, microphone and brightness lines.
    VolumeKeys volumeKeys_{*audio_, backlight_, osd_, [this] { return overlayOutput(); }};
    std::unique_ptr<Media> media_ = makeMedia();
    std::unique_ptr<PowerMode> powerMode_ = makePowerMode();
    std::unique_ptr<Wifi> wifi_ = makeWifi();
    std::unique_ptr<Bluetooth> bluetooth_ = makeBluetooth();
    QString focusedOutput_, cardsOutput_;
    QVariantMap keyboardLayout_;
    bool nightLight_ = false;
    QString nightLightMode_;
    QString bindingMode_;
    void setBindingMode(const QString &mode);
    QObject *notificationService_ = nullptr;
    TrayModel tray_;
    QObject *trayHost_ = nullptr;
    Authentication authentication_;
    QObject *polkitAgent_ = nullptr;
    bool servePolkit_ = false;
    QString polkitProblem_; // why the agent did not register, said once
    void updatePolkitAgent();
    QFileSystemWatcher *trashWatcher_ = nullptr;
    bool trashFull_ = false;
    // Reads whether the trash holds anything and watches it for a change, from the first time
    // the style is macOS on.
    void watchTrash();
    bool serveTray_ = false, trayNoBusReported_ = false;
    void updateTrayHost();
    bool serveNotifications_ = false, noBusReported_ = false;
    int lastVolume_ = -1;
    bool lastMuted_ = false;
    QString lastVolumeOutput_;
    void updateCards();
    void updateNotificationService();
    void showVolume();
    void handleDnd(const QString &verb);
    std::vector<App> apps_;
    // The desktop entries of apps_, for finding a window's (appFor), and what appFor and
    // iconFor found for each app id since they last changed.
    app_match::Index appIndex_;
    mutable QHash<QString, QString> appFor_, iconFor_;
    // The entries the menus leave out, for their windows' icons, and those icons by entry id.
    app_match::Index hiddenIndex_;
    QHash<QString, QString> hiddenIcons_;
    // Desktop ids pinned from the shell, in the order they were pinned.
    QStringList userPins_;
    QString error_;
    QString configError_;
    QQmlEngine *engine_ = nullptr;
    bool effects_ = false;
    // Compositor state from its control socket ($PAW_SOCKET), kept open by "subscribe".
    QLocalSocket *state_ = nullptr;
    bool subscribed_ = false, tiling_ = false;
    QVariantMap workspaces_, nextWorkspaces_;
    QVariantList urgentWindows_, nextUrgentWindows_;
    // What the switcher and the overview show (their properties above), and what comes in for
    // them line by line until all of it has (`switcherPending_`, `overviewPending_` lines to go).
    struct Switcher {
        QString output = {};
        QVariantList windows = {};
        int selected = 0;
    };
    Switcher switcher_, nextSwitcher_;
    int switcherPending_ = 0;
    void showSwitcher();
    struct Overview {
        QString output = {}, filter = {};
        QVariantList windows = {}, strip = {};
        int selected = 0, viewed = 1;
        QRect area = {};
        bool assist = false;
    };
    Overview overview_, nextOverview_;
    int overviewPending_ = 0;
    // Shows nextOverview_ once its windows and strip have come, `pending` lines more.
    void expectOverview(int pending);
    void showOverview();
    void clearOverview();
    void subscribe();
    void request(const QByteArray &line, const QString &unavailable);
    void report(const QString &message);
    // Whether `error` is empty, as it is when what was asked went ahead, which clears the error
    // shown across the panel; else `failure` and the error are shown there.
    bool succeeded(const QString &error, const QString &failure);
    void clearApps();
    void sortApps();
    // Starts an installed application's `info` with the environment every launch gets, showing a
    // failure across the panel as one of the application called `name`.
    bool start(GAppInfo *info, const QString &name);
    static QString pinsPath();
    void savePins();
    // Wallpapers picked from the panel, by profile: the one picked, and shell.wallpaper as it
    // was then; the pick stands while that is still what the configuration says.
    struct PickedWallpaper {
        QString configured, picked;
    };
    QMap<QString, PickedWallpaper> pickedWallpapers_;
    QVariantList wallpapers_;
    static QString pickedWallpapersPath();
    static QVariantMap record(const App &app);
};
