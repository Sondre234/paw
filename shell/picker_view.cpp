// SPDX-License-Identifier: GPL-3.0-or-later
#include "picker_view.hpp"
#include <QQuickItem>
#include <QScreen>
#if PAW_LAYER_SHELL
#include <LayerShellQt/Window>
#endif

PickerView::PickerView(ShellController &controller, QScreen *screen, const char *name,
                       const QString &file, QObject *model)
    : OverlayView(controller, screen, name, true), model_(model) {
    setTitle(QString("paw ") + name);
    setResizeMode(QQuickView::SizeViewToRootObject);
    setInitialProperties({{"screenSize", screen->geometry().size()}});
#if PAW_LAYER_SHELL
    using W = LayerShellQt::Window;
    layer_->setScope(QString("paw-") + name);
    layer_->setAnchors(W::AnchorTop);
    layer_->setExclusiveZone(0);
#endif
    place();
    connect(&controller, &ShellController::configChanged, this, &PickerView::place);
    load(file);
    // The surface is as big as the card wants, whatever size the compositor last configured.
    if (auto *root = rootObject()) {
        auto fit = [this, root] {
            const QSize wanted(qRound(root->width()), qRound(root->height()));
            if (size() != wanted)
                resize(wanted);
#if PAW_LAYER_SHELL
            layer_->setDesiredSize(wanted);
#endif
        };
        connect(root, &QQuickItem::widthChanged, this, fit);
        connect(root, &QQuickItem::heightChanged, this, fit);
        connect(this, &QWindow::heightChanged, this, fit);
        fit();
    }
    connect(screen, &QScreen::geometryChanged, this, [this] {
        if (rootObject())
            rootObject()->setProperty("screenSize", outputScreen_->geometry().size());
        place();
    });
    // Clicking elsewhere takes the keyboard away, which closes it; giving it up as it goes does
    // not, but a model that waits for it (the emoji picker's, to type what was picked into the
    // window that has it again) hears of it.
    connect(this, &QWindow::activeChanged, this, [this] {
        if (isActive())
            wasActive_ = true;
        else if (wasActive_ && isVisible() && !leaving())
            QMetaObject::invokeMethod(model_, "close");
        else if (leaving() && model_->metaObject()->indexOfMethod("keyboardReleased()") >= 0)
            QMetaObject::invokeMethod(model_, "keyboardReleased");
    });
    connect(model_, SIGNAL(openChanged()), this, SLOT(update()));
}

// Centred, below the bars by ShellController::paletteDrop.
void PickerView::place() {
#if PAW_LAYER_SHELL
    if (layer_)
        layer_->setMargins(
            QMargins(0, controller_.paletteDrop(outputScreen_->geometry().height()), 0, 0));
#endif
}

void PickerView::update() {
    if (model_->property("output").toString() != outputScreen_->name()) {
        dismiss();
        return;
    }
    // Opened afresh, or again while it was going: it starts anew.
    if (!isVisible() || leaving()) {
        wasActive_ = false;
        if (rootObject())
            QMetaObject::invokeMethod(rootObject(), "reset");
        present();
    }
}
