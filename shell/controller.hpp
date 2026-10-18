// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "app_match.hpp"
#include "audio.hpp"
#include "authentication.hpp"
#include "backlight.hpp"
#include "bar_resize.hpp"
#include "bluetooth.hpp"
#include "caption.hpp"
#include "clipboard.hpp"
#include "desktop_clock.hpp"
#include "desktop_icons.hpp"
#include "display_modes.hpp"
#include "display_settings.hpp"
#include "emoji.hpp"
#include "file_index.hpp"
#include "glass.hpp"
#include "media.hpp"
#include "notifications.hpp"
#include "power_mode.hpp"
#include "osd.hpp"
#include "volume_keys.hpp"
#include "wallpaper_cycle.hpp"
#include "palette.hpp"
#include "power.hpp"
#include "paw/config.hpp"
#include "restyle.hpp"
#include "rice_trial.hpp"
#include "run_launcher.hpp"
#include "shortcuts.hpp"
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
#include <QPoint>
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
    // The warm colour beside the accent, for touches that mark no state: the focused window
    // border's last colour when that is a gradient (washi's sakura after its sky blue), else
    // windows.urgent_color, the warm one of each shipped profile.
    Q_PROPERTY(QColor blush READ blush NOTIFY configChanged)
    // The wallpaper of the monitors without one of their own: one picked from the panel for the
    // profile in use, else shell.wallpaper. wallpaperFile is the same as a path, "" for none.
    // Each ShellView has its own monitor's (wallpaperOn).
    Q_PROPERTY(QUrl wallpaper READ wallpaper NOTIFY wallpaperChanged)
    Q_PROPERTY(QString wallpaperFile READ wallpaperFile NOTIFY wallpaperChanged)
    // shell.wallpaper_mode, how the wallpaper covers the output: "fill", "fit", "center", "tile"
    // or "stretch".
    Q_PROPERTY(QString wallpaperMode READ wallpaperMode NOTIFY configChanged)
    // The picker's pictures as {path, name, folder} (see findWallpapers), read by
    // findWallpapers(), and the folder they come from.
    Q_PROPERTY(QVariantList wallpapers READ wallpapers NOTIFY wallpapersChanged)
    Q_PROPERTY(QString wallpaperFolder READ wallpaperFolder NOTIFY configChanged)
    // shell.start_icon, the start button's picture; empty for the four squares.
    Q_PROPERTY(QUrl startIcon READ startIcon NOTIFY configChanged)
    // How the shell is laid out (shell.style): "taskbar" or "macos".
    Q_PROPERTY(QString style READ style NOTIFY configChanged)
    // shell.panel_height, or while the bar is resized by an edge (barResizing) the height it is
    // dragged to; panelSizeChanged comes with each change, and with every configChanged. The
    // margins below are as the drag has them too.
    Q_PROPERTY(int panelHeight READ panelHeight NOTIFY panelSizeChanged)
    // Where each output's bar goes (its edge, the strip it reserves) is its ShellView's to say:
    // panelEdge and the rest below for that output.
    // shell.panel_draggable: whether the taskbar can be dragged to another edge, and resized by
    // its edges (not the macOS style's dock). While one is dragged, the compositor has the
    // pointer and tells where it is: the bar is barDragOutput's, and would go to barDragEdge, the
    // edge of that output nearest the pointer. With panelPerMonitor (shell.panel_per_monitor) it
    // moves alone, and its panel alone shows where it would go; else every monitor's bar moves,
    // and every panel shows it. While it is resized (barResizing), every bar is as tall as
    // panelHeight says, inset by the margins below.
    Q_PROPERTY(bool panelDraggable READ panelDraggable NOTIFY configChanged)
    Q_PROPERTY(bool panelPerMonitor READ panelPerMonitor NOTIFY configChanged)
    // shell.panel_dock: when a floating taskbar docks: "always" while a window reaches its edge,
    // "maximized" only while one fills a monitor that does not tile, or "never".
    Q_PROPERTY(QString panelDock READ panelDock NOTIFY configChanged)
    Q_PROPERTY(bool barDragging READ barDragging NOTIFY barDragChanged)
    Q_PROPERTY(QString barDragOutput READ barDragOutput NOTIFY barDragChanged)
    Q_PROPERTY(QString barDragEdge READ barDragEdge NOTIFY barDragChanged)
    Q_PROPERTY(bool barResizing READ barResizing NOTIFY barDragChanged)
    // shell.mirror_menus: whether the bar's right-click menus run in reverse along the top.
    Q_PROPERTY(bool mirrorMenus READ mirrorMenus NOTIFY configChanged)
    Q_PROPERTY(int panelMarginTop READ panelMarginTop NOTIFY panelSizeChanged)
    Q_PROPERTY(int panelMarginRight READ panelMarginRight NOTIFY panelSizeChanged)
    Q_PROPERTY(int panelMarginBottom READ panelMarginBottom NOTIFY panelSizeChanged)
    Q_PROPERTY(int panelMarginLeft READ panelMarginLeft NOTIFY panelSizeChanged)
    Q_PROPERTY(int panelRadius READ panelRadius NOTIFY configChanged)
    // shell.frame = "windows": the bar, the dock and the popups framed as the windows are, by
    // windows.border_width in border_inactive_color (a gradient's first colour) and, while
    // windows.shadow is on, its shadow: soft for windowShadowBlur pixels, offset by
    // windowShadowOffset, windowShadowColor under the focused window and
    // windowShadowInactiveColor under the others (transparent while it is off).
    Q_PROPERTY(bool frameWindows READ frameWindows NOTIFY configChanged)
    Q_PROPERTY(int windowBorderWidth READ windowBorderWidth NOTIFY configChanged)
    Q_PROPERTY(QColor windowBorderColor READ windowBorderColor NOTIFY configChanged)
    Q_PROPERTY(QColor windowShadowColor READ windowShadowColor NOTIFY configChanged)
    Q_PROPERTY(QColor windowShadowInactiveColor READ windowShadowInactiveColor NOTIFY configChanged)
    Q_PROPERTY(int windowShadowBlur READ windowShadowBlur NOTIFY configChanged)
    Q_PROPERTY(QPoint windowShadowOffset READ windowShadowOffset NOTIFY configChanged)
    Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY configChanged)
    Q_PROPERTY(int fontSize READ fontSize NOTIFY configChanged)
    Q_PROPERTY(bool iconsOnly READ iconsOnly NOTIFY configChanged)
    // animations.enabled and animations.speed, which the shell's animations follow too.
    Q_PROPERTY(bool animations READ animations NOTIFY configChanged)
    Q_PROPERTY(qreal animationSpeed READ animationSpeed NOTIFY configChanged)
    // While a new appearance profile comes in, from the reload that brings it until it is on the
    // screen (Restyle): the compositor shows a picture of the old look meanwhile and crossfades
    // from it, so the shell's own motion waits and the new look shows at once.
    Q_PROPERTY(bool restyling READ restyling NOTIFY restylingChanged)
    // Whether Qt Quick draws through the GPU, which shader effects such as shadows need; its
    // software renderer cannot draw them. Set from what the views actually use.
    Q_PROPERTY(bool effects READ effects NOTIFY effectsChanged)
    // shell.blur: whether the bar and the popups are frosted glass, the wallpaper blurred behind
    // them, as far as the configuration says; it is drawn only with effects, where the glass's
    // GlassItem says so (`active`). `glass` holds each output's blurred wallpaper.
    Q_PROPERTY(bool blur READ blur NOTIFY configChanged)
    Q_PROPERTY(Glass *glass READ glass CONSTANT)
    Q_PROPERTY(bool groupWindows READ groupWindows NOTIFY configChanged)
    // shell.taskbar_buttons: "all", "main_and_own" or "own", which windows each monitor's taskbar
    // shows, and the main monitor, whose taskbar shows the pinned applications (and with
    // "main_and_own" every window): the compositor's primary one, else the leftmost screen.
    Q_PROPERTY(QString taskbarButtons READ taskbarButtons NOTIFY configChanged)
    Q_PROPERTY(QString primaryOutput READ primaryOutput NOTIFY primaryOutputChanged)
    // shell.thumbnails: whether resting on a taskbar button shows pictures of its windows, after
    // how many milliseconds, how wide each is, and whether they follow the windows while shown.
    Q_PROPERTY(bool thumbnails READ thumbnails NOTIFY configChanged)
    Q_PROPERTY(int thumbnailDelay READ thumbnailDelay NOTIFY configChanged)
    Q_PROPERTY(int thumbnailSize READ thumbnailSize NOTIFY configChanged)
    Q_PROPERTY(bool liveThumbnails READ liveThumbnails NOTIFY configChanged)
    // shell.thumbnails.workspaces: whether resting on a workspace shows a picture of it.
    Q_PROPERTY(bool workspaceThumbnails READ workspaceThumbnails NOTIFY configChanged)
    // peek.switcher: whether the window selected in the switcher or the palette peeks.
    Q_PROPERTY(bool peekSelection READ peekSelection NOTIFY configChanged)
    // features.sticky: whether a window can be shown on every workspace of its monitor.
    Q_PROPERTY(bool stickyWindows READ stickyWindows NOTIFY configChanged)
    // Which panel widgets Lua enables, and where: {workspaces, clock, calendar, keyboard_layout,
    // power, tray, media, power_mode, bluetooth} as booleans, and {battery, network, volume, tiling,
    // profiles, wallpapers, notifications}, which can move, as "bar", "quick" (Quick Settings) or
    // "" (hidden).
    Q_PROPERTY(QVariantMap widgets READ widgets NOTIFY configChanged)
    // shell.tiling_bar, the bar in Hyprland's style shown in place of the taskbar on a monitor
    // while it tiles: {enabled, top, height, marginTop, marginRight, marginBottom, marginLeft,
    // radius, style ("bar", "islands" or "pills"), color, textColor, accent, borderColor,
    // borderWidth, font, fontSize, spacing, dots, clockFormat, and left, center and right, the
    // module names of each group}. The colours and the type are the shell's where it sets none,
    // but the font, "" for Qt's default as Theme has it.
    Q_PROPERTY(QVariantMap tilingBar READ tilingBar NOTIFY configChanged)
    // The compositor's active keyboard layout: {number (from 1), count, short ("us"), name}, or
    // empty without a compositor.
    Q_PROPERTY(QVariantMap keyboardLayout READ keyboardLayout NOTIFY keyboardLayoutChanged)
    // The compositor's night light: whether it warms the screen now, and who decides: "auto"
    // (the schedule), "on" or "off" (an override), or "" without a compositor.
    Q_PROPERTY(bool nightLight READ nightLight NOTIFY nightLightChanged)
    Q_PROPERTY(QString nightLightMode READ nightLightMode NOTIFY nightLightChanged)
    // Whether the compositor's keep awake holds the idle steps off; false without a compositor.
    Q_PROPERTY(bool keepAwake READ keepAwake NOTIFY keepAwakeChanged)
    // The binding mode in use, "" outside any.
    Q_PROPERTY(QString bindingMode READ bindingMode NOTIFY bindingModeChanged)
    Q_PROPERTY(QVariantList pinned READ pinned NOTIFY appsChanged)
    // Configured launchers and installed applications, as {appId, name, icon, pinned (to the
    // taskbar), configured, genericName, keywords, description, program}.
    Q_PROPERTY(QVariantList apps READ apps NOTIFY appsChanged)
    // Counts the changes of `apps`, for QML that reads one record with appRecord() to follow
    // them without making all of them.
    Q_PROPERTY(int appsRevision READ appsRevision NOTIFY appsChanged)
    // Where QML finds an icon by name or path: "image://icons/" + name. It changes with the icon
    // theme (appearance.icon_theme), so that every image of one is looked up anew rather than
    // taken from Qt Quick's cache.
    Q_PROPERTY(QString iconBase READ iconBase NOTIFY iconThemeChanged)
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
    // The caption `paw tour` narrates itself with (the compositor's `caption`).
    Q_PROPERTY(Caption *caption READ caption CONSTANT)
    // The desktop clock (shell.desktop_clock), which the `desktop_clock` action shows and hides.
    Q_PROPERTY(DesktopClock *desktopClock READ desktopClock CONSTANT)
    // The desktop's icons (shell.desktop_icons), which its menu and the `desktop_icons` action
    // show and hide.
    Q_PROPERTY(DesktopIcons *desktopIcons READ desktopIcons CONSTANT)
    // The display mode popup (Windows' Win+P), which the compositor opens and steps.
    Q_PROPERTY(DisplayModes *displayModes READ displayModes CONSTANT)
    // The display settings window: the monitors' settings, tried and kept.
    Q_PROPERTY(DisplaySettings *displaySettings READ displaySettings CONSTANT)
    // A rice on trial (`paw rice try`), and the question whether to keep it.
    Q_PROPERTY(RiceTrial *riceTrial READ riceTrial CONSTANT)
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
    // Each output's workspaces by its name: {current, occupied, urgent, tiling, touched}, the
    // last the edges of its usable area a window reaches ("top", "bottom", "left", "right").
    Q_PROPERTY(QVariantMap workspaces READ workspaces NOTIFY workspacesChanged)
    // The configuration's appearance profiles, sorted, and the one in use ("" for none).
    Q_PROPERTY(QStringList profiles READ profiles NOTIFY configChanged)
    Q_PROPERTY(QString profile READ profile NOTIFY configChanged)
    // Whether profile_schedule switches profiles at sunrise and sunset, and whether the profile
    // in use is the one it wants now (the compositor's "profile-schedule on").
    Q_PROPERTY(bool hasProfileSchedule READ hasProfileSchedule NOTIFY configChanged)
    Q_PROPERTY(bool profileScheduled READ profileScheduled NOTIFY profileScheduledChanged)
    // The compositor's window switcher (Alt+Tab): the output showing it, empty while closed, and
    // its windows as {appId, title, output, workspace, minimized, urgent, id}, most recently
    // focused first; `id` is the window's number (TaskModel's windowId), 0 when not given.
    // `switcherApp`: it lists one application's windows (switcher_app, Alt+`); kept as the
    // switcher closes, so that it goes as it showed.
    Q_PROPERTY(QString switcherOutput READ switcherOutput NOTIFY switcherChanged)
    Q_PROPERTY(QVariantList switcherWindows READ switcherWindows NOTIFY switcherChanged)
    Q_PROPERTY(bool switcherApp READ switcherApp NOTIFY switcherChanged)
    Q_PROPERTY(int switcherSelected READ switcherSelected NOTIFY switcherSelectedChanged)
    // The command palette (Super + P).
    Q_PROPERTY(Palette *palette READ palette CONSTANT)
    // The power menu: what may run of lock, suspend, hibernate and the rest.
    Q_PROPERTY(Power *power READ power CONSTANT)
    // The polkit agent's requests, for the authentication dialog.
    Q_PROPERTY(Authentication *authentication READ authentication CONSTANT)
    // The start menu's pins, launch history, applications by letter, search and user.
    Q_PROPERTY(StartMenu *startMenu READ startMenu CONSTANT)
    // The run launcher (Super + R): applications, programs and commands, and windows.
    Q_PROPERTY(RunLauncher *runLauncher READ runLauncher CONSTANT)
    // The history of what is copied (shell.clipboard), and its popup.
    Q_PROPERTY(ClipboardHistory *clipboard READ clipboard CONSTANT)
    // The emoji picker.
    Q_PROPERTY(EmojiPicker *emoji READ emoji CONSTANT)
    // The sheet of keyboard shortcuts.
    Q_PROPERTY(Shortcuts *shortcuts READ shortcuts CONSTANT)
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
    // The compositor's exploded view (paw xray): the output showing it, empty while closed; the
    // area its stack of sheets keeps to and the width of the column left of it for a legend (0
    // for none), in the output's coordinates; the sheets, bottom first, as {layer, colour, name,
    // detail}; and the one under the pointer (-1 for none). The compositor draws the sheets; the
    // shell names them.
    Q_PROPERTY(QString xrayOutput READ xrayOutput NOTIFY xrayChanged)
    Q_PROPERTY(QRect xrayArea READ xrayArea NOTIFY xrayChanged)
    Q_PROPERTY(int xrayLegend READ xrayLegend NOTIFY xrayChanged)
    Q_PROPERTY(QVariantList xraySheets READ xraySheets NOTIFY xrayChanged)
    Q_PROPERTY(int xrayHovered READ xrayHovered NOTIFY xrayHoveredChanged)
  public:
    explicit ShellController(std::filesystem::path path, QObject *parent = nullptr);
    ~ShellController() override;
    QColor accent() const;
    QColor panelColor() const;
    QColor textColor() const;
    QColor background() const;
    QColor urgentColor() const;
    QColor blush() const;
    // The windows asking for attention, longest waiting first (the first 16), as {output,
    // workspace, appId, title}, which the taskbar marks. Each output's entry in `workspaces`
    // lists the workspaces they are on, as `urgent`.
    QVariantList urgentWindows() const { return urgentWindows_; }
    QUrl wallpaper() const;
    QString wallpaperFile() const { return wallpaperFileOn({}); }
    QString wallpaperMode() const;
    QVariantList wallpapers() const { return wallpapers_; }
    QString wallpaperFolder() const;
    QUrl startIcon() const;
    // Reads the picker's folder again.
    Q_INVOKABLE void findWallpapers();
    // Shows `path` as the wallpaper of the profile in use on every monitor, remembered across
    // sessions until the configured wallpaper changes; an empty path goes back to the configured
    // one.
    Q_INVOKABLE void pickWallpaper(const QString &path);
    // Shows `path` on monitor `output` alone, remembered as pickWallpaper remembers one until the
    // wallpaper the configuration gives that monitor changes; an empty path goes back to what it
    // shows without.
    Q_INVOKABLE void pickWallpaperOn(const QString &path, const QString &output);
    // The wallpaper of monitor `output`: picked for it alone, else picked for every monitor while
    // the configuration gives it the wallpaper it did then, else its shell.monitor_wallpapers
    // entry, else shell.wallpaper; a path, "" for none. wallpaperFile is a monitor's without
    // anything of its own.
    Q_INVOKABLE QString wallpaperFileOn(const QString &output) const;
    QUrl wallpaperOn(const QString &output) const;
    // Whether monitor `output` shows a wallpaper picked for it alone, not the one picked for
    // every monitor.
    Q_INVOKABLE bool ownWallpaper(const QString &output) const;
    // The wallpaper slideshow (shell.wallpaper_cycle): picks the picture after the one shown
    // (`direction` 1) or before it (-1) in its folder, as the wallpaper_next and wallpaper_prev
    // actions and its timer do, for every monitor or with per_monitor for each monitor alone, from
    // the left; with `tell`, says so when the folder has none (the timer does not).
    Q_INVOKABLE void stepWallpaper(int direction, bool tell = true);
    // The slideshow's folder: shell.wallpaper_cycle.folder, else the folder of shell.wallpaper,
    // else the picker's.
    QString wallpaperCycleFolder() const;
    WallpaperCycle *wallpaperCycle() { return &wallpaperCycle_; }
    QString style() const { return config_.shell.macos_style ? "macos" : "taskbar"; }
    int panelHeight() const { return barResizing_ ? barResize_.height : config_.shell.panel_height; }
    // Whether shell.panel_position, the edge of the bars of the monitors without one of their
    // own, is the top.
    bool panelTop() const { return config_.shell.panel_edge == paw::PanelEdge::Top; }
    // The edge of the bar on output `output` (by its name; none for the monitors without an edge
    // of their own): shell.panel_positions' entry for it over shell.panel_position
    // (shell.panel_per_monitor), but for the macOS style's dock, which is at the bottom.
    paw::PanelEdge panelSurfaceEdge(const QString &output = {}) const {
        return config_.shell.macos_style ? paw::PanelEdge::Bottom
                                         : config_.shell.edge_of(output.toStdString());
    }
    bool panelDraggable() const {
        return config_.shell.panel_draggable && !config_.shell.macos_style;
    }
    QString panelDock() const {
        switch (config_.shell.panel_dock) {
        case paw::ShellConfig::PanelDock::Never: return "never";
        case paw::ShellConfig::PanelDock::Maximized: return "maximized";
        default: return "always";
        }
    }
    bool panelPerMonitor() const { return config_.shell.panel_per_monitor; }
    bool barDragging() const { return barDragging_; }
    QString barDragOutput() const { return barDragOutput_; }
    QString barDragEdge() const { return barDragEdge_; }
    // Asks the compositor to take the pointer for a drag of `output`'s bar, the left button held
    // on it (`panel_drag OUTPUT`): it says "panel-drag OUTPUT EDGE" as the edge of that output
    // nearest the pointer changes, and "panel-dropped OUTPUT EDGE" as the button is let go, which
    // moves the bar there (movePanel), or "panel-drag-cancelled", which leaves it, after
    // cancelBarDrag (Escape).
    Q_INVOKABLE void startBarDrag(const QString &output);
    Q_INVOKABLE void cancelBarDrag();
    bool barResizing() const { return barResizing_; }
    // Asks the compositor to take the pointer for a resize of `output`'s bar, the left button
    // held on its edge facing `side` ("top", "bottom", "left" or "right") `distance` logical
    // pixels from the output's edge the drag is told from (bar_resize::measuredFrom; `panel_drag
    // resize EDGE`): it says "panel-resize OUTPUT DISTANCE" as the pointer's distance from that
    // edge changes, which sizes every bar by as much as the pointer moved since the press, as
    // bar_resize::resized has it within the schema's ranges for shell.panel_height and
    // shell.panel_margin, and "panel-resized OUTPUT DISTANCE" as the button is let go, which
    // keeps that size (resizePanel), or "panel-drag-cancelled", which puts the old one back,
    // after cancelBarDrag (Escape).
    Q_INVOKABLE void startBarResize(const QString &output, const QString &side, int distance);
    // Moves the taskbar on `output` to `edge` ("top", "bottom", "left" or "right"): writes it
    // into the configuration file, as output's shell.panel_positions entry with panelPerMonitor,
    // else as shell.panel_position, where it takes effect (the profile in use when that sets it),
    // and loads the file again, as the compositor does once it sees the file change. A failure
    // leaves the file as it was and is shown across the panel.
    Q_INVOKABLE void movePanel(const QString &output, const QString &edge);
    // Sizes the taskbar as `size` says, as movePanel moves it: writes shell.panel_height and the
    // sides of shell.panel_margin that changed where they take effect, in one write (all four
    // sides for a margin given as one number), and loads the file again.
    void resizePanel(const bar_resize::Size &size);
    // panelSurfaceEdge's name, "top", "bottom", "left" or "right", and whether it is a side,
    // along which the bar stands upright.
    QString panelEdge(const QString &output = {}) const;
    bool panelVertical(const QString &output = {}) const {
        return panelSurfaceEdge(output) == paw::PanelEdge::Left ||
               panelSurfaceEdge(output) == paw::PanelEdge::Right;
    }
    bool mirrorMenus() const { return config_.shell.mirror_menus; }
    // shell.panel_margin's side `index` (top, right, bottom, left), or the drag's while the bar
    // is resized.
    int panelMargin(int index) const {
        return barResizing_ ? barResize_.margin[index] : config_.shell.panel_margin[index];
    }
    int panelMarginTop() const { return panelMargin(0); }
    int panelMarginRight() const { return panelMargin(1); }
    int panelMarginBottom() const { return panelMargin(2); }
    int panelMarginLeft() const { return panelMargin(3); }
    // The strip the panel on `output` reserves: the bar and the margins on either side of it,
    // across it.
    int panelExtent(const QString &output = {}) const {
        return panelHeight() + (panelVertical(output) ? panelMarginLeft() + panelMarginRight()
                                                      : panelMarginTop() + panelMarginBottom());
    }
    // The room the panel's surface on `output` has past the strip it reserves, towards the middle
    // of the output, which takes no input: the dock's, for an icon to bounce in, and a floating
    // bar's framed as windows are (shell.frame) for its shadow to fall into, as far as the shadow
    // reaches past the bar less the margin on that side; none otherwise.
    int panelHeadroom(const QString &output = {}) const;
    // How far below the bars the command palette opens on an output `height` tall: a sixth of
    // it, or a quarter in the macOS style, where Spotlight opens.
    int paletteDrop(int height) const { return height / (config_.shell.macos_style ? 4 : 6); }
    // How far from the output's bottom edge the on-screen display stands there: 48 pixels, or
    // clear of the macOS style's dock.
    int osdBottom() const { return config_.shell.macos_style ? panelExtent() + 16 : 48; }
    int panelRadius() const { return config_.shell.panel_radius; }
    bool frameWindows() const { return config_.shell.frame_windows; }
    int windowBorderWidth() const { return config_.settings.border_width; }
    QColor windowBorderColor() const;
    QColor windowShadowColor() const;
    QColor windowShadowInactiveColor() const;
    int windowShadowBlur() const { return config_.settings.shadow_blur; }
    QPoint windowShadowOffset() const {
        return {config_.settings.shadow_x, config_.settings.shadow_y};
    }
    QString fontFamily() const { return QString::fromStdString(config_.shell.font); }
    int fontSize() const { return config_.shell.font_size; }
    bool softwareRenderer() const { return config_.shell.software_renderer; }
    bool iconsOnly() const { return config_.shell.icons_only; }
    bool animations() const { return config_.settings.animations; }
    qreal animationSpeed() const { return config_.settings.animation_speed; }
    bool restyling() const { return restyling_; }
    bool effects() const { return effects_; }
    void setEffects(bool effects);
    bool groupWindows() const { return config_.shell.group_windows; }
    QString taskbarButtons() const;
    QString primaryOutput() const;
    bool thumbnails() const { return config_.shell.thumbnails.enabled; }
    int thumbnailDelay() const { return config_.shell.thumbnails.delay; }
    int thumbnailSize() const { return config_.shell.thumbnails.size; }
    bool liveThumbnails() const { return config_.shell.thumbnails.live; }
    bool workspaceThumbnails() const { return config_.shell.thumbnails.workspaces; }
    bool peekSelection() const { return config_.settings.effects.peek_switcher; }
    bool stickyWindows() const { return config_.settings.sticky; }
    bool enabled() const { return config_.shell.enabled; }
    QStringList profiles() const;
    QString profile() const { return QString::fromStdString(config_.profile); }
    // Switches to another appearance profile: the compositor saves the choice and reloads.
    Q_INVOKABLE void pickProfile(const QString &name) { send("profile " + name); }
    bool hasProfileSchedule() const { return config_.settings.profile_schedule.day[0] != '\0'; }
    bool profileScheduled() const { return profileScheduled_; }
    QVariantMap widgets() const;
    QVariantMap tilingBar() const;
    // The edge the tiling bar is along, and the strip it reserves there: the bar and its margins
    // across it.
    paw::PanelEdge tilingBarEdge() const {
        return config_.shell.tiling_bar.top ? paw::PanelEdge::Top : paw::PanelEdge::Bottom;
    }
    int tilingBarExtent() const {
        const auto &bar = config_.shell.tiling_bar;
        return bar.height + bar.margin[0] + bar.margin[2];
    }
    QVariantMap keyboardLayout() const { return keyboardLayout_; }
    bool nightLight() const { return nightLight_; }
    QString nightLightMode() const { return nightLightMode_; }
    bool keepAwake() const { return keepAwake_; }
    QString bindingMode() const { return bindingMode_; }
    // What the compositor says of night light; the preview's stand-in says it too.
    void setNightLight(bool on, const QString &mode);
    QVariantList pinned() const;
    QVariantList apps() const;
    int appsRevision() const { return appsRevision_; }
    QString iconBase() const;
    // The icon theme in use: appearance.icon_theme where it is installed, else the one the shell
    // started with.
    QString iconTheme() const;
    // The record of the application `id`, as `apps` has it; undefined in QML for none.
    Q_INVOKABLE QVariant appRecord(const QString &id) const;
    QString error() const { return error_; }
    QString configError() const { return configError_; }
    TaskModel *tasks() { return &tasks_; }
    Audio *audio() { return audio_.get(); }
    Palette *palette() { return &palette_; }
    bool blur() const { return config_.shell.blur.enabled; }
    Glass *glass() { return &glass_; }
    Power *power() { return &power_; }
    Authentication *authentication() { return &authentication_; }
    // Registers the shell as the session's polkit authentication agent when it is built in and
    // shell.polkit_agent is on, unless another agent serves the session; later configuration
    // reloads follow the setting. Returns whether it is the agent.
    bool startPolkit();
    StartMenu *startMenu() { return &startMenu_; }
    RunLauncher *runLauncher() { return &runLauncher_; }
    // The files the palette's and the start menu's search find (shell.search).
    FileIndex *files() { return &files_; }
    ClipboardHistory *clipboard() { return &clipboard_; }
    EmojiPicker *emoji() { return &emoji_; }
    Shortcuts *shortcuts() { return &shortcuts_; }
    // The configuration in force.
    const paw::Config &config() const { return config_; }
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
    // Qt's RectangularShadow (6.11) makes the shader effect it draws with by default without a
    // parent (in its private part's constructor, before that knows its shadow) and never deletes
    // it, some 2.7 KB lost with every shadow made and gone (a popup's card, the bar docking):
    // Shadow.qml hands that effect to its shadow here, to go with it, while adoptsShadowMaterial,
    // for the Qt versions known to leak it (before 6.12).
    Q_INVOKABLE void keepShadowMaterial(QObject *shadow);
    static bool adoptsShadowMaterial();
    SystemStatus *status() { return &status_; }
    Backlight *backlight() { return &backlight_; }
    Media *media() { return media_.get(); }
    PowerMode *powerMode() { return powerMode_.get(); }
    Wifi *wifi() { return wifi_.get(); }
    Bluetooth *bluetooth() { return bluetooth_.get(); }
    NotificationCenter *notifications() { return &notifications_; }
    Osd *osd() { return &osd_; }
    Caption *caption() { return &caption_; }
    DesktopClock *desktopClock() { return &desktopClock_; }
    DesktopIcons *desktopIcons() { return &desktopIcons_; }
    DisplayModes *displayModes() { return &displayModes_; }
    DisplaySettings *displaySettings() { return &displaySettings_; }
    RiceTrial *riceTrial() { return &riceTrial_; }
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
    // A colour picked off the screen with the compositor's colour picker ("color-picked
    // #rrggbb"), copied already: a swatch of it and its hex on the on-screen display, with do not
    // disturb on too, or a notification while the display is off. Anything but #rrggbb is
    // ignored.
    void colorPicked(const QString &hex);
    // Starts answering on the session bus when notifications are enabled and QtDBus is built
    // in; later configuration reloads follow the setting. Returns whether it is serving.
    bool startNotifications();
    TrayModel *tray() { return &tray_; }
    // Starts the tray's host on the session bus when QtDBus is built in and shell.widgets.tray
    // is on; later configuration reloads follow the setting. Returns whether it is running.
    bool startTray();
    // Whether the focused output tiles, as the compositor says; until it has, as layout.tiling
    // says, which a preview without a compositor shows.
    bool tiling() const { return tilingKnown_ ? tiling_ : config_.settings.tiling; }
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
    // By output name: {current: N, occupied: [N, ...], tiling: bool, layout: "dwindle", ...},
    // numbered from 1.
    QVariantMap workspaces() const { return workspaces_; }
    QString switcherOutput() const { return switcher_.output; }
    QVariantList switcherWindows() const { return switcher_.windows; }
    bool switcherApp() const { return switcher_.app; }
    int switcherSelected() const { return switcher_.selected; }
    // Focuses the switcher's window at `index` and closes it.
    Q_INVOKABLE void switcherPick(int index);
    // Tells the compositor that its switcher shows, so that the selected window peeks once the
    // selection has rested (switcher_peek); nothing without a session.
    Q_INVOKABLE void switcherShown();
    QString overviewOutput() const { return overview_.output; }
    QVariantList overviewWindows() const { return overview_.windows; }
    QVariantList overviewStrip() const { return overview_.strip; }
    int overviewViewed() const { return overview_.viewed; }
    QString overviewFilter() const { return overview_.filter; }
    QRect overviewArea() const { return overview_.area; }
    int overviewSelected() const { return overview_.selected; }
    bool overviewAssist() const { return overview_.assist; }
    QString xrayOutput() const { return xray_.output; }
    QRect xrayArea() const { return xray_.area; }
    int xrayLegend() const { return xray_.legend; }
    QVariantList xraySheets() const { return xray_.sheets; }
    int xrayHovered() const { return xray_.hovered; }
    Q_INVOKABLE bool launch(const QString &id);
    // Starts the program `command` (its words) in the home folder, with the environment of a
    // configured launcher's, detached from the shell; a failure shows across the panel as one of
    // `name`'s.
    bool startProgram(const QStringList &command, const QString &name);
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
    // Tiles `output`'s workspace shown with `layout` ("dwindle", "master", ...), turning tiling on
    // there too when `turnOn`, once the layout is set, so that its windows tile with it at once.
    Q_INVOKABLE void tileWith(const QString &output, const QString &layout, bool turnOn);
    Q_INVOKABLE void showWorkspace(const QString &output, int number);
    // A step past `output`'s last workspace (`way` 1) or its first (-1), which the compositor
    // answers by nudging that output's windows that way and back; nothing without a compositor.
    Q_INVOKABLE void nudgeWorkspace(const QString &output, int way);
  Q_SIGNALS:
    void configChanged();
    void restylingChanged();
    void barDragChanged();
    void panelSizeChanged();
    void effectsChanged();
    void wallpaperChanged();
    void wallpapersChanged();
    void appsChanged();
    void iconThemeChanged();
    void errorChanged();
    void disabled();
    void tilingChanged();
    void workspacesChanged();
    void launcherRequested(const QString &output);
    void switcherChanged();
    void switcherSelectedChanged();
    void overviewChanged();
    void overviewSelectedChanged();
    void xrayChanged();
    void xrayHoveredChanged();
    void cardsOutputChanged();
    void primaryOutputChanged();
    void keyboardLayoutChanged();
    void nightLightChanged();
    void profileScheduledChanged();
    void keepAwakeChanged();
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
        // The program it runs, by its file name (app_match::program of its Exec line, or a
        // configured launcher's command's first word), for the run launcher's programs.
        QString program = {};
    };
    // Loads the configuration, or the default one with configError_ set when it has an error.
    // `reloading`, one that could not be checked in time or memory throws paw::ConfigTrialError
    // instead, leaving the configuration as it is, as the compositor does.
    void loadConfig(bool reloading = false);
    // Looks icons up in appearance.icon_theme where it is installed, else in the theme the
    // shell started with, as the configuration names another, and has every icon drawn anew.
    void followIconTheme();
    QString iconThemeBase_;   // the theme the shell started with
    QString iconThemeWanted_; // appearance.icon_theme as last followed, "" for none
    int iconGeneration_ = 0;  // the icon theme changes so far, in iconBase
    // Hands the file index and the start menu shell.search's settings.
    void configureSearch();
    // Hands the clipboard history shell.clipboard's.
    void configureClipboard();
    // The desktop clock, from the configuration loaded.
    void configureDesktopClock();
    // The desktop's icons, from the configuration loaded.
    void configureDesktopIcons();
    // The wallpaper slideshow, from the configuration loaded.
    void configureWallpaperCycle();
    std::filesystem::path path_;
    paw::Config config_;
    bool restyling_ = false;
    Restyle restyle_;
    Glass glass_;
    // Restyle settled: tells the compositor the profile's look is drawn.
    void restyled();
    TaskModel tasks_;
    FileIndex files_;
    Palette palette_{*this};
    Power power_{*this};
    StartMenu startMenu_{{}, this};
    RunLauncher runLauncher_{*this};
    ClipboardHistory clipboard_;
    EmojiPicker emoji_;
    Shortcuts shortcuts_{*this};
    // Types `text` into what has the keyboard through the compositor, else copies it and says so.
    void typeText(const QString &text);
    std::unique_ptr<Audio> audio_ = makeAudio();
    SystemStatus status_{"/sys", nullptr, true};
    NotificationCenter notifications_;
    Osd osd_;
    Caption caption_;
    DesktopClock desktopClock_;
    DesktopIcons desktopIcons_;
    WallpaperCycle wallpaperCycle_;
    DisplayModes displayModes_;
    DisplaySettings displaySettings_{[this](const QByteArray &line,
                                            std::function<void(const QByteArray &)> done) {
        ask(line, std::move(done));
    }};
    RiceTrial riceTrial_{[this](const QByteArray &line) { send(QString::fromUtf8(line)); },
                         [this] { return overlayOutput(); }};
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
    // The primary monitor as the compositor says, empty until it has.
    QString primaryOutput_;
    QVariantMap keyboardLayout_;
    bool nightLight_ = false;
    QString nightLightMode_;
    bool profileScheduled_ = false;
    void setProfileScheduled(bool on);
    bool keepAwake_ = false;
    void setKeepAwake(bool on);
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
    void handleBarDrag(const QString &line);
    void handleBarResize(const QStringList &words);
    std::vector<App> apps_;
    int appsRevision_ = 0;
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
    bool barDragging_ = false;
    QString barDragOutput_, barDragEdge_;
    // A resize of the bar (startBarResize): the edge it is along and the side of the bar dragged,
    // the pointer's distance from the edge of the output it is told from as it was pressed (-1
    // while none is asked for or under way), the bar's size then, and the size it is dragged to.
    bool barResizing_ = false;
    paw::PanelEdge barResizeEdge_ = paw::PanelEdge::Bottom, barResizeSide_ = paw::PanelEdge::Top;
    int barResizeFrom_ = -1;
    bar_resize::Size barResizeStart_, barResize_;
    QQmlEngine *engine_ = nullptr;
    bool effects_ = false;
    // Compositor state from its control socket ($PAW_SOCKET), kept open by "subscribe".
    QLocalSocket *state_ = nullptr;
    bool subscribed_ = false, tiling_ = false, tilingKnown_ = false;
    QVariantMap workspaces_, nextWorkspaces_;
    QVariantList urgentWindows_, nextUrgentWindows_;
    // What the switcher and the overview show (their properties above), and what comes in for
    // them line by line until all of it has (`switcherPending_`, `overviewPending_` lines to go).
    struct Switcher {
        QString output = {};
        QVariantList windows = {};
        int selected = 0;
        bool app = false;
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
    // The exploded view, and what comes in for it until its sheets have (`xrayPending_` lines).
    struct Xray {
        QString output = {};
        int hovered = -1;
        QRect area = {};
        int legend = 0;
        QVariantList sheets = {};
    };
    Xray xray_, nextXray_;
    int xrayPending_ = 0;
    void showXray();
    void clearXray();
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
    // Wallpapers picked from the panel, by profile and then by monitor ("" for every monitor):
    // the one picked, and the wallpaper the configuration gave the monitor then (shell.wallpaper
    // for every monitor); the pick stands while that is still what the configuration says.
    struct PickedWallpaper {
        QString configured, picked;
    };
    QMap<QString, QMap<QString, PickedWallpaper>> pickedWallpapers_;
    // The wallpaper the configuration gives monitor `output`, as written: its
    // shell.monitor_wallpapers entry, else shell.wallpaper.
    QString configuredWallpaper(const QString &output) const;
    // Writes pickedWallpapers_ to its file, and tells.
    void savePickedWallpapers();
    // Notes `path` picked for monitor `output` alone, or none for an empty path, without saving.
    void notePick(const QString &path, const QString &output);
    QVariantList wallpapers_;
    static QString pickedWallpapersPath();
    static QVariantMap record(const App &app);
};
