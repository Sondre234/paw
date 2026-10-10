// SPDX-License-Identifier: GPL-3.0-or-later
#include "view.hpp"
#include "audio.hpp"
#include "task_filter.hpp"
#include "taskbar_model.hpp"
#include <QGuiApplication>
#include <QQuickItem>
#include <QSGRendererInterface>
#include <QScreen>
#include <iostream>
#if PAW_LAYER_SHELL
#include <LayerShellQt/Window>

namespace {
// The layer surface of `window` on `screen`, called `scope`, in `layer`: one that takes no keyboard.
LayerShellQt::Window *makeLayer(QWindow *window, QScreen *screen, const char *scope,
                                LayerShellQt::Window::Layer layer) {
    auto *surface = LayerShellQt::Window::get(window);
    surface->setScreen(screen);
    surface->setScope(scope);
    surface->setLayer(layer);
    surface->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    surface->setActivateOnShow(false);
    return surface;
}
} // namespace
#endif

ShellView::ShellView(ShellController &controller, QScreen *screen, bool desktop, bool preview)
    : QQuickView(controller.engine(), nullptr), controller_(controller), desktop_(desktop), preview_(preview), outputScreen_(screen) {
    setScreen(screen);
    setTitle(desktop ? "paw desktop" : "paw taskbar");
    setColor(Qt::transparent);
    setResizeMode(QQuickView::SizeRootObjectToView);
    setFlags(Qt::FramelessWindowHint);
    static const int registered = qmlRegisterType<TaskFilter>("Paw", 1, 0, "TaskFilter") +
                                  qmlRegisterType<TaskbarModel>("Paw", 1, 0, "TaskbarModel") +
                                  qmlRegisterType<WindowSound>("Paw", 1, 0, "WindowSound") +
                                  qmlRegisterType<PopoverWindow>("Paw", 1, 0, "PopoverWindow") +
                                  qmlRegisterType<MenuBarWindow>("Paw", 1, 0, "MenuBarWindow");
    Q_UNUSED(registered);
    // Known as soon as the window exists, so before any QML asks: every view of the shell draws
    // the same way.
    controller.setEffects(QSGRendererInterface::isApiRhiBased(rendererInterface()->graphicsApi()) &&
                          rendererInterface()->graphicsApi() != QSGRendererInterface::Null);
    // The engine is shared by every view; what differs per view goes in as initial properties.
    // outputName matches the compositor's output name, which the workspace state is keyed by.
    if (!desktop)
        setInitialProperties({{"shellView", QVariant::fromValue(this)}, {"outputName", screen->name()}});
#if PAW_LAYER_SHELL
    if (!preview) {
        using W = LayerShellQt::Window;
        layer_ = makeLayer(this, screen, desktop ? "paw-desktop" : "paw-panel",
                           desktop ? W::LayerBackground : W::LayerTop);
        placeLayer();
    }
#endif
    resizeForContent();
    setSource(QUrl(desktop ? "qrc:/shell/PawShell/Desktop.qml" : "qrc:/shell/PawShell/Panel.qml"));
    connect(&controller, &ShellController::configChanged, this, [this] {
        placeLayer();
        resizeForContent();
    });
    connect(&controller, &ShellController::launcherRequested, this, [this](const QString &output) {
        if (desktop_ || !rootObject() || outputScreen_->name() != output)
            return;
        bool open = !rootObject()->property("launcherOpen").toBool();
        rootObject()->setProperty("launcherOpen", open);
        std::cerr << "paw launcher " << (open ? "opened" : "closed") << " on "
                  << output.toStdString() << '\n';
    });
    connect(&controller, &ShellController::powerMenuRequested, this, [this](const QString &output) {
        if (!desktop_ && rootObject() && outputScreen_->name() == output)
            QMetaObject::invokeMethod(rootObject(), "togglePowerMenu");
    });
    connect(&controller, &ShellController::taskbarRequested, this, [this](const QString &output) {
        if (desktop_ || !rootObject() || outputScreen_->name() != output)
            return;
        QMetaObject::invokeMethod(rootObject(), "toggleBarKeyboard");
        const auto *keys = rootObject()->property("barKeys").value<QObject *>();
        const bool on = keys && keys->property("active").toBool();
        std::cerr << "paw taskbar keyboard " << (on ? "on" : "off") << " on "
                  << output.toStdString() << '\n';
    });
    connect(screen, &QScreen::geometryChanged, this, [this] { resizeForContent(); });
    // A surface made anew as the view shows takes the region again.
    connect(this, &QWindow::visibleChanged, this, [this](bool visible) {
        if (visible)
            applyInput();
    });
}
PopoverWindow *ShellView::popover() const {
    return rootObject() ? rootObject()->findChild<PopoverWindow *>() : nullptr;
}
MenuBarWindow *ShellView::menuBar() const {
    return rootObject() ? rootObject()->findChild<MenuBarWindow *>() : nullptr;
}
// The panel's surface spans the output's width and the bar's margins; the bar is drawn inset. The
// macOS style's dock is at the bottom whatever shell.panel_position says, and its surface reaches
// above the strip it reserves by half its height, room for an icon to bounce in.
void ShellView::placeLayer() {
#if PAW_LAYER_SHELL
    if (!layer_)
        return;
    using W = LayerShellQt::Window;
    if (desktop_) {
        layer_->setAnchors(
            W::Anchors(W::AnchorLeft | W::AnchorRight | W::AnchorTop | W::AnchorBottom));
        layer_->setExclusiveZone(-1);
        return;
    }
    layer_->setAnchors(W::Anchors(W::AnchorLeft | W::AnchorRight |
                                  (controller_.panelSurfaceTop() ? W::AnchorTop : W::AnchorBottom)));
    layer_->setExclusiveZone(controller_.panelExtent());
#endif
}
void ShellView::resizeForContent() {
    int width = preview_ ? previewSize().width() : screen()->geometry().width();
    int height = desktop_ ? (preview_ ? 680 : screen()->geometry().height())
                          : controller_.panelExtent() + controller_.panelHeadroom();
    resize(width, height);
#if PAW_LAYER_SHELL
    if (layer_)
        layer_->setDesiredSize(QSize(0, desktop_ ? 0 : height));
#endif
}

void ShellView::setInputRects(const QVariantList &rects) {
    if (rects == inputRects_)
        return;
    inputRects_ = rects;
    Q_EMIT inputRectsChanged();
    applyInput();
}
void ShellView::applyInput() {
    inputRegion_ = QRegion();
    for (const auto &rect : std::as_const(inputRects_))
        inputRegion_ += rect.toRectF().toAlignedRect();
    if (!layer_)
        return;
    // An empty mask is no mask: the whole surface.
    setMask(inputRegion_);
    // The region is the surface's state, sent with its next frame.
    update();
}

PopoverWindow::PopoverWindow(QWindow *parent) : QQuickWindow(parent) {
    setTitle("paw popover");
    setColor(Qt::transparent);
    setFlags(Qt::FramelessWindowHint);
    // Losing the keyboard while holding it means something else was chosen. The menu bar never
    // takes it as a layer surface, but a preview's platform may give it the focus as it shows:
    // the popover takes it back.
    connect(this, &QWindow::activeChanged, this, [this] {
        if (isActive() || !open_ || !keyboard_)
            return;
        if (panel_ && QGuiApplication::focusWindow() && QGuiApplication::focusWindow() == panel_->menuBar())
            requestActivate();
        else
            Q_EMIT dismissed();
    });
}
void PopoverWindow::setPanel(QQuickWindow *panel) {
    auto *view = qobject_cast<ShellView *>(panel);
    if (!view || panel_)
        return;
    panel_ = view;
    QScreen *screen = view->outputScreen();
    setScreen(screen);
#if PAW_LAYER_SHELL
    if (view->layerShell()) {
        using W = LayerShellQt::Window;
        // Over the panels and the windows, fullscreen ones too: the launcher asked for with
        // Super + R must show over a video.
        layer_ = makeLayer(this, screen, "paw-popover", W::LayerOverlay);
        layer_->setAnchors(W::Anchors(W::AnchorTop | W::AnchorBottom | W::AnchorLeft | W::AnchorRight));
        // The whole output, the bar's strip included: a popup is placed by the bar. The
        // compositor sizes it to the output.
        layer_->setExclusiveZone(-1);
        layer_->setDesiredSize(QSize(0, 0));
    }
#endif
    fit();
    connect(screen, &QScreen::geometryChanged, this, &PopoverWindow::fit);
    Q_EMIT panelChanged();
    applyOpen();
}
void PopoverWindow::fit() {
    resize(layer_ ? panel_->outputScreen()->geometry().size() : ShellView::previewSize());
}
void PopoverWindow::setOpen(bool open) {
    if (open == open_)
        return;
    open_ = open;
    Q_EMIT openChanged();
    applyOpen();
}
void PopoverWindow::applyOpen() {
    if (!panel_)
        return;
    const auto output = panel_->outputScreen()->name().toStdString();
    if (open_ && !isVisible()) {
        applyInput();
        show();
        applyKeyboard();
        std::cerr << "paw popover shown on " << output << '\n';
    } else if (!open_ && isVisible()) {
        hide();
        std::cerr << "paw popover hidden on " << output << '\n';
    }
}
void PopoverWindow::prepare() {
    if (!layer_ || prepared_ || isVisible())
        return;
    prepared_ = true;
    applyInput();
    show();
    connect(
        this, &QQuickWindow::frameSwapped, this,
        [this] {
            if (!open_)
                hide();
        },
        Qt::ConnectionType(Qt::QueuedConnection | Qt::SingleShotConnection));
}
void PopoverWindow::setKeyboard(bool keyboard) {
    if (keyboard == keyboard_)
        return;
    keyboard_ = keyboard;
    Q_EMIT keyboardChanged();
    applyKeyboard();
}
void PopoverWindow::applyKeyboard() {
#if PAW_LAYER_SHELL
    if (layer_)
        layer_->setKeyboardInteractivity(keyboard_ ? LayerShellQt::Window::KeyboardInteractivityExclusive
                                                   : LayerShellQt::Window::KeyboardInteractivityNone);
#endif
    if (keyboard_ && isVisible())
        requestActivate();
}
void PopoverWindow::setInputRects(const QVariantList &rects) {
    if (rects == inputRects_)
        return;
    inputRects_ = rects;
    Q_EMIT inputRectsChanged();
    applyInput();
}
void PopoverWindow::applyInput() {
    inputRegion_ = QRegion();
    for (const auto &rect : std::as_const(inputRects_))
        inputRegion_ += rect.toRectF().toAlignedRect();
    if (!layer_)
        return;
    // An empty mask would be no mask, the whole surface; nothing takes no input instead.
    if (inputRegion_.isEmpty()) {
        setFlag(Qt::WindowTransparentForInput, true);
    } else {
        setMask(inputRegion_);
        setFlag(Qt::WindowTransparentForInput, false);
    }
    // The region is the surface's state, sent with its next frame.
    update();
}
MenuBarWindow::MenuBarWindow(QWindow *parent) : QQuickWindow(parent) {
    setTitle("paw menu bar");
    setColor(Qt::transparent);
    // A preview's window too leaves the focus with the panel and its popover.
    setFlags(Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
}
void MenuBarWindow::setPanel(QQuickWindow *panel) {
    auto *view = qobject_cast<ShellView *>(panel);
    if (!view || panel_)
        return;
    panel_ = view;
    QScreen *screen = view->outputScreen();
    setScreen(screen);
#if PAW_LAYER_SHELL
    if (view->layerShell()) {
        using W = LayerShellQt::Window;
        layer_ = makeLayer(this, screen, "paw-menubar", W::LayerTop);
        layer_->setAnchors(W::Anchors(W::AnchorTop | W::AnchorLeft | W::AnchorRight));
    }
#endif
    fit();
    connect(screen, &QScreen::geometryChanged, this, &MenuBarWindow::fit);
    Q_EMIT panelChanged();
    applyShown();
}
// Across the output, and as tall as the bar, which is the strip it reserves.
void MenuBarWindow::fit() {
    if (!panel_)
        return;
    const int width = layer_ ? panel_->outputScreen()->geometry().width() : ShellView::previewSize().width();
    resize(width, barHeight_);
#if PAW_LAYER_SHELL
    if (layer_) {
        layer_->setDesiredSize(QSize(0, barHeight_));
        layer_->setExclusiveZone(barHeight_);
    }
#endif
}
void MenuBarWindow::setBarHeight(int height) {
    if (height == barHeight_ || height < 1)
        return;
    barHeight_ = height;
    Q_EMIT barHeightChanged();
    fit();
}
void MenuBarWindow::setShown(bool shown) {
    if (shown == shown_)
        return;
    shown_ = shown;
    Q_EMIT shownChanged();
    applyShown();
}
void MenuBarWindow::applyShown() {
    if (!panel_)
        return;
    const auto output = panel_->outputScreen()->name().toStdString();
    if (shown_ && !isVisible()) {
        // Hidden, the surface and its exclusive zone are gone; shown, it is made anew.
        show();
        std::cerr << "paw menu bar shown on " << output << '\n';
        connect(
            this, &QQuickWindow::frameSwapped, this,
            [this] { std::cerr << "paw surface rendered: " << title().toStdString() << '\n'; },
            Qt::SingleShotConnection);
    } else if (!shown_ && isVisible()) {
        hide();
        std::cerr << "paw menu bar hidden on " << output << '\n';
    }
}

OverlayView::OverlayView(ShellController &controller, QScreen *screen, const char *name, bool keyboard)
    : QQuickView(controller.engine(), nullptr), controller_(controller), outputScreen_(screen), name_(name),
      keyboard_(keyboard) {
    setScreen(screen);
    setColor(Qt::transparent);
    setFlags(Qt::FramelessWindowHint);
#if PAW_LAYER_SHELL
    using W = LayerShellQt::Window;
    layer_ = W::get(this);
    layer_->setScreen(screen);
    layer_->setLayer(W::LayerOverlay);
    holdKeyboard(keyboard_);
    layer_->setActivateOnShow(keyboard_);
#endif
}
void OverlayView::load(const QString &file) {
    setSource(QUrl("qrc:/shell/PawShell/" + file));
    if (auto *root = rootObject())
        connect(root, SIGNAL(progressChanged()), this, SLOT(settle()));
}
void OverlayView::holdKeyboard(bool hold) {
#if PAW_LAYER_SHELL
    using W = LayerShellQt::Window;
    layer_->setKeyboardInteractivity(hold ? W::KeyboardInteractivityExclusive
                                          : W::KeyboardInteractivityNone);
#else
    Q_UNUSED(hold);
#endif
}
bool OverlayView::present() {
    auto *root = rootObject();
    if (leaving_) {
        leaving_ = false;
        setFlag(Qt::WindowTransparentForInput, false);
        if (keyboard_) {
            holdKeyboard(true);
            requestActivate();
        }
        if (root)
            root->setProperty("shown", true);
        std::cerr << "paw " << name_ << " kept on " << outputScreen_->name().toStdString() << '\n';
        return false;
    }
    if (isVisible())
        return false;
    if (root)
        root->setProperty("shown", true);
    show();
    if (keyboard_)
        requestActivate();
    std::cerr << "paw " << name_ << " shown on " << outputScreen_->name().toStdString() << '\n';
    return true;
}
void OverlayView::dismiss() {
    if (!isVisible() || leaving_)
        return;
    leaving_ = true;
    // The pointer and the keyboard go to what is under it with its next frame, its first going.
    setFlag(Qt::WindowTransparentForInput, true);
    if (keyboard_)
        holdKeyboard(false);
    if (auto *root = rootObject())
        root->setProperty("shown", false);
    settle();
}
void OverlayView::settle() {
    auto *root = rootObject();
    if (!leaving_ || (root && root->property("progress").toReal() > 0))
        return;
    leaving_ = false;
    hide();
    setFlag(Qt::WindowTransparentForInput, false);
    if (keyboard_)
        holdKeyboard(true);
    std::cerr << "paw " << name_ << " hidden on " << outputScreen_->name().toStdString() << '\n';
}
namespace {
// Shows `view` while `want` and hides it otherwise, saying so as "paw NAME shown on OUTPUT".
void showWhile(QQuickView *view, bool want, const char *name, const QScreen *screen) {
    if (want && !view->isVisible()) {
        view->show();
        std::cerr << "paw " << name << " shown on " << screen->name().toStdString() << '\n';
    } else if (!want && view->isVisible()) {
        view->hide();
        std::cerr << "paw " << name << " hidden on " << screen->name().toStdString() << '\n';
    }
}
} // namespace
void followRoot(QQuickView *view, LayerShellQt::Window *layer, QQuickItem *root) {
    auto fit = [view, layer, root] {
        const QSize wanted(qRound(root->width()), qRound(root->height()));
        if (view->size() != wanted)
            view->resize(wanted);
#if PAW_LAYER_SHELL
        if (layer)
            layer->setDesiredSize(wanted);
#else
        Q_UNUSED(layer);
#endif
    };
    QObject::connect(root, &QQuickItem::widthChanged, view, fit);
    QObject::connect(root, &QQuickItem::heightChanged, view, fit);
    QObject::connect(view, &QWindow::heightChanged, view, fit);
    QObject::connect(view, &QWindow::widthChanged, view, fit);
    fit();
}
OverviewView::OverviewView(ShellController &controller, QScreen *screen)
    : OverlayView(controller, screen, "overview", false) {
    setTitle("paw overview");
    resize(screen->geometry().size());
    setInitialProperties({{"screenSize", screen->geometry().size()}});
#if PAW_LAYER_SHELL
    using W = LayerShellQt::Window;
    layer_->setScope("paw-overview");
    layer_->setAnchors(W::Anchors(W::AnchorTop | W::AnchorBottom | W::AnchorLeft | W::AnchorRight));
    layer_->setExclusiveZone(-1);
#endif
    load("Overview.qml");
    connect(screen, &QScreen::geometryChanged, this, [this] {
        resize(outputScreen_->geometry().size());
        if (rootObject())
            rootObject()->setProperty("screenSize", outputScreen_->geometry().size());
    });
    connect(&controller, &ShellController::overviewChanged, this, &OverviewView::update);
}
void OverviewView::update() {
    if (controller_.overviewOutput() == outputScreen_->name()) {
        // It takes no input: the compositor keeps the pointer and the keyboard while the
        // overview is open, and Snap Assist leaves the rest of the screen to the windows
        // under it. present() gives input back to a view it keeps as it goes.
        setFlag(Qt::WindowTransparentForInput, true);
        present();
        setFlag(Qt::WindowTransparentForInput, true);
    } else {
        dismiss();
    }
}
PowerView::PowerView(ShellController &controller, QScreen *screen)
    : OverlayView(controller, screen, "power dialog", true) {
    setTitle("paw power");
    setResizeMode(QQuickView::SizeRootObjectToView);
    resize(screen->geometry().size());
#if PAW_LAYER_SHELL
    using W = LayerShellQt::Window;
    layer_->setScope("paw-power");
    layer_->setAnchors(W::Anchors(W::AnchorTop | W::AnchorBottom | W::AnchorLeft | W::AnchorRight));
    layer_->setExclusiveZone(-1);
#endif
    load("PowerDialog.qml");
    connect(screen, &QScreen::geometryChanged, this,
            [this] { resize(outputScreen_->geometry().size()); });
    connect(controller.power(), &Power::pendingChanged, this, &PowerView::update);
}
AuthView::AuthView(ShellController &controller, QScreen *screen)
    : OverlayView(controller, screen, "authentication dialog", true) {
    setTitle("paw authentication");
    setResizeMode(QQuickView::SizeRootObjectToView);
    resize(screen->geometry().size());
#if PAW_LAYER_SHELL
    using W = LayerShellQt::Window;
    layer_->setScope("paw-authentication");
    layer_->setAnchors(W::Anchors(W::AnchorTop | W::AnchorBottom | W::AnchorLeft | W::AnchorRight));
    layer_->setExclusiveZone(-1);
#endif
    load("AuthDialog.qml");
    connect(screen, &QScreen::geometryChanged, this,
            [this] { resize(outputScreen_->geometry().size()); });
    connect(controller.authentication(), &Authentication::changed, this, &AuthView::update);
    // Its output gone, the primary screen's view takes over.
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &AuthView::update,
            Qt::QueuedConnection);
    update();
}
void AuthView::update() {
    auto *auth = controller_.authentication();
    const bool mine = auth->open() &&
                      ShellController::outputOrPrimary(auth->output()) == outputScreen_->name();
    if (!mine) {
        dismiss();
        return;
    }
    // A request that opened, here or again while it was going: an empty field with the keyboard.
    const bool fresh = auth->serial() != serial_;
    serial_ = auth->serial();
    const bool again = leaving();
    if ((present() || again || fresh) && rootObject())
        QMetaObject::invokeMethod(rootObject(), "reset");
}
void PowerView::update() {
    auto *power = controller_.power();
    const bool mine = !power->pending().isEmpty() && power->output() == outputScreen_->name();
    if (!mine) {
        dismiss();
        return;
    }
    // Asked afresh, or again while it was going: the keyboard starts on its action's button.
    const bool again = leaving();
    if ((present() || again) && rootObject())
        QMetaObject::invokeMethod(rootObject(), "reset");
}
SwitcherView::SwitcherView(ShellController &controller, QScreen *screen)
    : OverlayView(controller, screen, "switcher", false), delay_(new QTimer(this)) {
    setTitle("paw switcher");
    setResizeMode(QQuickView::SizeViewToRootObject);
    // Only the switcher on its own output lists the windows and watches their pictures.
    setInitialProperties(
        {{"screenSize", screen->geometry().size()}, {"outputName", screen->name()}});
#if PAW_LAYER_SHELL
    using W = LayerShellQt::Window;
    layer_->setScope("paw-switcher");
    layer_->setAnchors(W::Anchors());
    layer_->setExclusiveZone(0);
#endif
    load("Switcher.qml");
    // The surface is as big as the switcher wants, whatever size the compositor last configured:
    // its cards take their widths from their windows' pictures, which may come once it shows,
    // and a configure for the size before would otherwise leave it cut off.
    if (auto *root = rootObject())
        followRoot(this, layer_, root);
    connect(screen, &QScreen::geometryChanged, this, [this] {
        if (rootObject())
            rootObject()->setProperty("screenSize", outputScreen_->geometry().size());
    });
    // A quick Alt+Tab switches without the overlay flashing up.
    delay_->setSingleShot(true);
    delay_->setInterval(120);
    connect(delay_, &QTimer::timeout, this, [this] {
        if (controller_.switcherOutput() == outputScreen_->name())
            present();
    });
    connect(&controller, &ShellController::switcherChanged, this, &SwitcherView::update);
}
void SwitcherView::update() {
    if (controller_.switcherOutput() != outputScreen_->name()) {
        delay_->stop();
        dismiss();
    } else if (leaving()) {
        // Opened again while it was going: it comes back at once.
        present();
    } else if (!isVisible() && !delay_->isActive()) {
        delay_->start();
    }
}
CardsView::CardsView(ShellController &controller, QScreen *screen)
    : QQuickView(controller.engine(), nullptr), controller_(controller), outputScreen_(screen) {
    setScreen(screen);
    setTitle("paw notifications");
    setColor(Qt::transparent);
    setResizeMode(QQuickView::SizeViewToRootObject);
    setFlags(Qt::FramelessWindowHint);
    setInitialProperties({{"outputName", screen->name()}});
#if PAW_LAYER_SHELL
    layer_ = makeLayer(this, screen, "paw-notifications", LayerShellQt::Window::LayerOverlay);
    layer_->setExclusiveZone(0);
    placeLayer();
#endif
    setSource(QUrl("qrc:/shell/PawShell/NotificationCards.qml"));
    if (auto *root = rootObject()) {
        connect(root, SIGNAL(activeChanged()), this, SLOT(update()));
        followRoot(this, layer_, root);
    }
    connect(&controller, &ShellController::configChanged, this, [this] { placeLayer(); });
    update();
}
void CardsView::placeLayer() {
#if PAW_LAYER_SHELL
    using W = LayerShellQt::Window;
    W::Anchors anchors;
    anchors |= controller_.notifications()->bottom() ? W::AnchorBottom : W::AnchorTop;
    anchors |= controller_.notifications()->left() ? W::AnchorLeft : W::AnchorRight;
    layer_->setAnchors(anchors);
#endif
}
void CardsView::update() {
    showWhile(this, rootObject() && rootObject()->property("active").toBool(), "notifications",
              outputScreen_);
}
OsdView::OsdView(ShellController &controller, QScreen *screen)
    : QQuickView(controller.engine(), nullptr), controller_(controller), outputScreen_(screen) {
    setScreen(screen);
    setTitle("paw osd");
    setColor(Qt::transparent);
    setResizeMode(QQuickView::SizeViewToRootObject);
    setFlags(Qt::FramelessWindowHint | Qt::WindowTransparentForInput);
    setInitialProperties({{"outputName", screen->name()}});
#if PAW_LAYER_SHELL
    layer_ = makeLayer(this, screen, "paw-osd", LayerShellQt::Window::LayerOverlay);
    layer_->setExclusiveZone(-1);
    placeLayer();
#endif
    setSource(QUrl("qrc:/shell/PawShell/Osd.qml"));
    if (auto *root = rootObject()) {
        connect(root, SIGNAL(visibleNowChanged()), this, SLOT(update()));
        followRoot(this, layer_, root);
    }
    connect(&controller, &ShellController::configChanged, this, [this] { placeLayer(); });
}
void OsdView::placeLayer() {
#if PAW_LAYER_SHELL
    using W = LayerShellQt::Window;
    layer_->setAnchors(controller_.osd()->top() ? W::AnchorTop : W::AnchorBottom);
    layer_->setMargins(QMargins(0, 48, 0, controller_.osdBottom()));
#endif
}
void OsdView::update() {
    showWhile(this, rootObject() && rootObject()->property("visibleNow").toBool(), "osd",
              outputScreen_);
}
DisplayModeView::DisplayModeView(ShellController &controller, QScreen *screen)
    : QQuickView(controller.engine(), nullptr), outputScreen_(screen) {
    setScreen(screen);
    setTitle("paw display mode");
    setColor(Qt::transparent);
    setResizeMode(QQuickView::SizeViewToRootObject);
    setFlags(Qt::FramelessWindowHint);
    setInitialProperties({{"outputName", screen->name()}});
#if PAW_LAYER_SHELL
    using W = LayerShellQt::Window;
    layer_ = makeLayer(this, screen, "paw-display-mode", W::LayerOverlay);
    layer_->setAnchors(W::Anchors());
    layer_->setExclusiveZone(-1);
#endif
    setSource(QUrl("qrc:/shell/PawShell/DisplayMode.qml"));
    if (auto *root = rootObject()) {
        connect(root, SIGNAL(visibleNowChanged()), this, SLOT(update()));
        followRoot(this, layer_, root);
    }
}
void DisplayModeView::update() {
    showWhile(this, rootObject() && rootObject()->property("visibleNow").toBool(), "display mode",
              outputScreen_);
}
ConfigErrorView::ConfigErrorView(ShellController &controller, QScreen *screen)
    : QQuickView(controller.engine(), nullptr), outputScreen_(screen) {
    setScreen(screen);
    setTitle("paw configuration error");
    setColor(Qt::transparent);
    setResizeMode(QQuickView::SizeRootObjectToView);
    setFlags(Qt::FramelessWindowHint | Qt::WindowTransparentForInput);
#if PAW_LAYER_SHELL
    using W = LayerShellQt::Window;
    layer_ = makeLayer(this, screen, "paw-config-error", W::LayerOverlay);
    layer_->setAnchors(W::Anchors(W::AnchorTop) | W::AnchorLeft | W::AnchorRight);
    layer_->setExclusiveZone(-1);
#endif
    setSource(QUrl("qrc:/shell/PawShell/ConfigError.qml"));
    if (auto *root = rootObject()) {
        connect(root, SIGNAL(visibleNowChanged()), this, SLOT(update()));
        connect(root, SIGNAL(implicitHeightChanged()), this, SLOT(update()));
    }
    update();
}
void ConfigErrorView::update() {
    auto *root = rootObject();
    const bool want = root && root->property("visibleNow").toBool();
    if (root) {
        // The compositor stretches it across the output; only its height is asked for.
        const int height = qMax(1, qRound(root->implicitHeight()));
        resize(outputScreen_->geometry().width(), height);
#if PAW_LAYER_SHELL
        layer_->setDesiredSize(QSize(0, height));
#endif
    }
    if (want && !isVisible()) {
        show();
        std::cerr << "paw configuration error shown on " << outputScreen_->name().toStdString()
                  << '\n';
    } else if (!want && isVisible()) {
        hide();
    }
}
DisplaySettingsView::DisplaySettingsView(ShellController &controller, QScreen *screen)
    : OverlayView(controller, screen, "display settings", true) {
    setTitle("paw display settings");
    setResizeMode(QQuickView::SizeViewToRootObject);
    setInitialProperties({{"screenSize", screen->geometry().size()}});
#if PAW_LAYER_SHELL
    using W = LayerShellQt::Window;
    layer_->setScope("paw-display-settings");
    layer_->setAnchors(W::Anchors());
    layer_->setExclusiveZone(0);
#endif
    load("DisplaySettings.qml");
    if (auto *root = rootObject())
        followRoot(this, layer_, root);
    connect(screen, &QScreen::geometryChanged, this, [this] {
        if (rootObject())
            rootObject()->setProperty("screenSize", outputScreen_->geometry().size());
    });
    connect(controller.displaySettings(), &DisplaySettings::openChanged, this,
            &DisplaySettingsView::update);
    // Its output gone, as a trial may turn it off, the primary screen's view takes over, and
    // gives it back as the output returns.
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, &DisplaySettingsView::update,
            Qt::QueuedConnection);
    connect(qGuiApp, &QGuiApplication::screenAdded, this, &DisplaySettingsView::update,
            Qt::QueuedConnection);
    update();
}
void DisplaySettingsView::update() {
    const auto *settings = controller_.displaySettings();
    const bool mine = settings->open() &&
                      ShellController::outputOrPrimary(settings->output()) == outputScreen_->name();
    if (!mine) {
        dismiss();
        return;
    }
    // Opened afresh, or again while it was going: the keyboard starts on the window.
    const bool again = leaving();
    if ((present() || again) && rootObject())
        QMetaObject::invokeMethod(rootObject(), "reset");
}
