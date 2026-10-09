// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QImage>
#include <QObject>
#include <QStringList>
#include <QTemporaryDir>
#include <memory>

class Audio;
class Backlight;
class Bluetooth;
class Media;
class PowerMode;
class QQuickItem;
class Wifi;
class QQuickView;
class QScreen;
class ShellController;
class SystemStatus;

// What the panel shows in `--preview-popup`, which runs without a compositor, a sound server or
// a session bus: windows (one application's three of them stacked, with stand-in pictures, one
// minimized, one asking for attention), sound outputs and applications playing, a battery and
// Wi-Fi, a backlight, tray items (one with a menu), a few notifications, every power action,
// night light on, a music player playing with a browser paused behind it, power-profiles-daemon
// in its balanced mode, NetworkManager connected to a Wi-Fi network among others, and BlueZ with
// devices connected, paired and in range. The controller's own models take the tray, the notifications and the power
// actions; the panel's sources, which the tests swap the same way, take the rest.
class PreviewData : public QObject {
    Q_OBJECT
  public:
    explicit PreviewData(ShellController &controller);
    ~PreviewData() override;
    // Points the panel's sources at the stand-ins; before its first frame, so the bar is laid
    // out with them.
    void fill(QQuickItem *panel);
    // Opens popup `name` on the panel, as a click on its button would (see previewPopup in
    // Panel.qml for the names), or shows overlay surface `name` (see surfaces()) over the bar
    // alone; false for a name it does not know.
    bool open(QQuickItem *panel, const QString &name);
    // The overlay surfaces it shows, each in a window of its own with stand-ins for what the
    // compositor would tell it: the on-screen display, the notification cards, the switcher,
    // the palette, the power dialog and the overview.
    static QStringList surfaces();
    // `desktop`, a picture from previewOnDesktop, with the overlay surface shown drawn over it
    // where the compositor would place it; unchanged when none is.
    QImage withSurface(QImage desktop) const;

  private:
    ShellController &controller_;
    // The panel filled, whose menu bar (the macOS style's) is reserved too.
    QQuickItem *panel_ = nullptr;
    QRect usableArea() const;
    std::unique_ptr<QQuickView> surface_;
    QString surfaceName_;
    bool showSurface(QScreen *screen, const QString &name);
    QTemporaryDir sysfs_;
    std::unique_ptr<Audio> audio_;
    std::unique_ptr<SystemStatus> status_;
    std::unique_ptr<Backlight> backlight_;
    std::unique_ptr<Media> media_;
    std::unique_ptr<PowerMode> powerMode_;
    std::unique_ptr<Wifi> wifi_;
    std::unique_ptr<Bluetooth> bluetooth_;
    QObject *tasks_ = nullptr;
};

// A screenshot of the panel's preview as the output would show it: the desktop (the wallpaper, or
// the background colour), the bar along its edge, the menu bar of the macOS style along the top,
// and the popover with its popups over them all, so that a translucent colour shows what it would
// show there. `popover` is null while nothing is open, `menuBar` without a menu bar.
QImage previewOnDesktop(QImage panel, QImage popover, QImage menuBar, bool panelTop,
                        ShellController &controller);
