// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "controller.hpp"
#include <QQuickView>
#include <QTimer>

namespace LayerShellQt {
class Window;
}
class PopoverWindow;
class MenuBarWindow;
class ShellView : public QQuickView {
    Q_OBJECT
    // Where the panel takes the pointer: rectangles in its coordinates, or the whole surface while
    // there are none, as for the taskbar. The dock lists only its own rectangle, so that the
    // desktop beside it and under its surface's headroom stays reachable.
    Q_PROPERTY(QVariantList inputRects READ inputRects WRITE setInputRects NOTIFY inputRectsChanged)
  public:
    ShellView(ShellController &controller, QScreen *screen, bool desktop, bool preview);
    QScreen *outputScreen() const { return outputScreen_; }
    // Whether it is a layer surface; a preview is an ordinary window.
    bool layerShell() const { return layer_ != nullptr; }
    // The taskbar's popover, where its popups are drawn; nullptr for the desktop.
    PopoverWindow *popover() const;
    // The menu bar of the macOS style; nullptr for the desktop.
    MenuBarWindow *menuBar() const;
    // The size of the output a preview stands for.
    static QSize previewSize() { return {1100, 720}; }
    QVariantList inputRects() const { return inputRects_; }
    void setInputRects(const QVariantList &rects);
    // inputRects as a region, empty for the whole surface. Applied to the surface with layer shell
    // only, as a preview's platform may have no input regions.
    QRegion inputRegion() const { return inputRegion_; }

  Q_SIGNALS:
    void inputRectsChanged();

  private:
    ShellController &controller_;
    bool desktop_, preview_;
    LayerShellQt::Window *layer_ = nullptr;
    QScreen *outputScreen_;
    QVariantList inputRects_;
    QRegion inputRegion_;
    void placeLayer();
    void resizeForContent();
    void applyInput();
};

// The popups of one output's taskbar, in a surface of their own over the whole output: the bar's
// surface keeps its size whatever opens, and a popup can be as large as the output. Panel.qml
// declares it, so its popups stay in the panel's QML tree and state; its coordinates are the
// output's. With layer shell it is a layer surface above everything but the lock screen (above a
// fullscreen video too, for the launcher Super + R opens); without, an ordinary window as large
// as a preview's output.
//
// It is shown while `open`, holds the keyboard while `keyboard`, and takes the pointer only within
// `inputRects` (rectangles in its coordinates), so that the bar's surface beside the popups, and
// the windows beside a list shown on hover, stay reachable.
class PopoverWindow : public QQuickWindow {
    Q_OBJECT
    // The bar's view, whose output it covers. Set once, before it first opens.
    Q_PROPERTY(QQuickWindow *panel READ panel WRITE setPanel NOTIFY panelChanged)
    Q_PROPERTY(bool open READ open WRITE setOpen NOTIFY openChanged)
    Q_PROPERTY(bool keyboard READ keyboard WRITE setKeyboard NOTIFY keyboardChanged)
    Q_PROPERTY(QVariantList inputRects READ inputRects WRITE setInputRects NOTIFY inputRectsChanged)
  public:
    explicit PopoverWindow(QWindow *parent = nullptr);
    QQuickWindow *panel() const { return panel_; }
    void setPanel(QQuickWindow *panel);
    bool open() const { return open_; }
    void setOpen(bool open);
    bool keyboard() const { return keyboard_; }
    void setKeyboard(bool keyboard);
    QVariantList inputRects() const { return inputRects_; }
    void setInputRects(const QVariantList &rects);
    // Where it takes the pointer: inputRects as a region. Applied to the surface with layer shell
    // only, as a preview's platform may have no input regions.
    QRegion inputRegion() const { return inputRegion_; }
    // Shows it for a frame, empty and taking nothing, and hides it again: its first real opening
    // then finds the graphics set up (tens of milliseconds through the GPU) instead of waiting
    // for them. With layer shell only, and once.
    Q_INVOKABLE void prepare();

  Q_SIGNALS:
    void panelChanged();
    void openChanged();
    void keyboardChanged();
    void inputRectsChanged();
    // The keyboard went elsewhere while it held it, as when a window on another output is
    // clicked: the popups close.
    void dismissed();

  private:
    ShellView *panel_ = nullptr;
    bool open_ = false, keyboard_ = false, prepared_ = false;
    QVariantList inputRects_;
    QRegion inputRegion_;
    LayerShellQt::Window *layer_ = nullptr;
    void fit();
    void applyOpen();
    void applyKeyboard();
    void applyInput();
};

// The menu bar of the macOS style along the top of one output, in a surface of its own while the
// panel's is the dock at the bottom. Panel.qml declares it, as it declares its popover, so the
// menu bar shares the panel's QML tree, its state and its popover; its coordinates are the
// output's along its top edge. With layer shell it is a layer surface on the top layer, across the
// output and `barHeight` tall, reserving that strip as an exclusive zone; without, an ordinary
// window as wide as a preview's output.
//
// It is shown while `shown`, and never takes the keyboard: its menus are the popover's.
class MenuBarWindow : public QQuickWindow {
    Q_OBJECT
    // The panel's view, whose output it is on. Set once, before it first shows.
    Q_PROPERTY(QQuickWindow *panel READ panel WRITE setPanel NOTIFY panelChanged)
    Q_PROPERTY(bool shown READ shown WRITE setShown NOTIFY shownChanged)
    Q_PROPERTY(int barHeight READ barHeight WRITE setBarHeight NOTIFY barHeightChanged)
  public:
    explicit MenuBarWindow(QWindow *parent = nullptr);
    QQuickWindow *panel() const { return panel_; }
    void setPanel(QQuickWindow *panel);
    bool shown() const { return shown_; }
    void setShown(bool shown);
    int barHeight() const { return barHeight_; }
    void setBarHeight(int height);

  Q_SIGNALS:
    void panelChanged();
    void shownChanged();
    void barHeightChanged();

  private:
    ShellView *panel_ = nullptr;
    bool shown_ = false;
    int barHeight_ = 28;
    LayerShellQt::Window *layer_ = nullptr;
    void fit();
    void applyShown();
};

// An overlay that comes in and goes out on the shell's motion: the command palette, the power
// dialog, the overview and the window switcher. Its root item has a `shown` property, set as it
// shows and cleared as it goes, and a `progress` from 0 to 1 that its QML animates after it; the
// window hides once progress is back at 0, at once with animations off. While it goes it takes no
// input and holds no keyboard, so the windows under it have the pointer and the keyboard at once
// and the compositor carries on as if it were gone; asked to show again, it comes back from where
// it was.
class OverlayView : public QQuickView {
    Q_OBJECT
  public:
    QScreen *outputScreen() const { return outputScreen_; }

  protected:
    // `name` is how its log lines call it ("palette" in "paw palette shown on DP-1"), and
    // `keyboard` whether it holds the keyboard while it is on.
    OverlayView(ShellController &controller, QScreen *screen, const char *name, bool keyboard);
    // Loads its QML from `file` in the shell's module.
    void load(const QString &file);
    // Shows it, or keeps it when it was going; returns whether it was hidden.
    bool present();
    // Starts it going, unless it is hidden or going already.
    void dismiss();
    // Whether it is going.
    bool leaving() const { return leaving_; }
    ShellController &controller_;
    LayerShellQt::Window *layer_ = nullptr;
    QScreen *outputScreen_;

  private Q_SLOTS:
    // Hides it once it has gone.
    void settle();

  private:
    const char *name_;
    bool keyboard_, leaving_ = false;
    void holdKeyboard(bool hold);
};

// The command palette's overlay on one output: a search box near the top, holding the keyboard
// while the palette is open there.
class PaletteView : public OverlayView {
    Q_OBJECT
  public:
    PaletteView(ShellController &controller, QScreen *screen);

  private:
    bool wasActive_ = false;
    void update();
    void place();
};

// The confirmation of power off, restart and log out on one output: a dimmed cover with the
// dialog in its middle, holding the keyboard while it waits.
class PowerView : public OverlayView {
    Q_OBJECT
  public:
    PowerView(ShellController &controller, QScreen *screen);

  private:
    void update();
};

// The polkit authentication dialog on one output: a dimmed cover with the dialog in its middle,
// holding the keyboard while a request waits for its password on that output (the one overlays
// belonged on as it opened, or the primary screen once that one is gone).
class AuthView : public OverlayView {
    Q_OBJECT
  public:
    AuthView(ShellController &controller, QScreen *screen);

  private:
    int serial_ = 0;
    void update();
};

// The display settings window on one output, in its middle, holding the keyboard while it is open
// there: on the output it was opened on, or on the primary screen once that one is gone, as the
// monitors change under it.
class DisplaySettingsView : public OverlayView {
    Q_OBJECT
  public:
    DisplaySettingsView(ShellController &controller, QScreen *screen);

  private:
    void update();
};

// The overview's overlay on one output: the text over the compositor's thumbnails (titles,
// workspace labels, the search box), covering the output while the overview is open there.
class OverviewView : public OverlayView {
    Q_OBJECT
  public:
    OverviewView(ShellController &controller, QScreen *screen);

  private:
    void update();
};

// The window switcher's overlay on one output, shown in the middle of it while the compositor's
// switcher is open there.
class SwitcherView : public OverlayView {
    Q_OBJECT
  public:
    SwitcherView(ShellController &controller, QScreen *screen);

  private:
    QTimer *delay_;
    void update();
};

// The notification cards on one output: a stack from a corner, on screen while it is the output
// the controller chose for them and until the last card has slid away.
class CardsView : public QQuickView {
    Q_OBJECT
  public:
    CardsView(ShellController &controller, QScreen *screen);
    QScreen *outputScreen() const { return outputScreen_; }

  private Q_SLOTS:
    void update();

  private:
    ShellController &controller_;
    LayerShellQt::Window *layer_ = nullptr;
    QScreen *outputScreen_;
    void placeLayer();
};

// The on-screen display on one output: a pill near an edge, for the moment it is shown.
class OsdView : public QQuickView {
    Q_OBJECT
  public:
    OsdView(ShellController &controller, QScreen *screen);
    QScreen *outputScreen() const { return outputScreen_; }

  private Q_SLOTS:
    void update();

  private:
    ShellController &controller_;
    LayerShellQt::Window *layer_ = nullptr;
    QScreen *outputScreen_;
    void placeLayer();
};

// The display mode popup on one output (Windows' Win+P), in the middle, while the compositor
// shows it there and until it has faded. It takes the pointer, not the keyboard: the compositor
// takes the keys that step it.
class DisplayModeView : public QQuickView {
    Q_OBJECT
  public:
    DisplayModeView(ShellController &controller, QScreen *screen);
    QScreen *outputScreen() const { return outputScreen_; }

  private Q_SLOTS:
    void update();

  private:
    LayerShellQt::Window *layer_ = nullptr;
    QScreen *outputScreen_;
};

// The configuration error banner across the top of one output, shown while the default
// configuration stands in for one with an error. Clicks pass through it.
class ConfigErrorView : public QQuickView {
    Q_OBJECT
  public:
    ConfigErrorView(ShellController &controller, QScreen *screen);
    QScreen *outputScreen() const { return outputScreen_; }

  private Q_SLOTS:
    void update();

  private:
    LayerShellQt::Window *layer_ = nullptr;
    QScreen *outputScreen_;
};
