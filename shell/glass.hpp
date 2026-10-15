// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "paw/config.hpp"
#include <QHash>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QSharedPointer>
#include <QThreadPool>
#include <QTransform>

class QQuickItemGrabResult;
class QQuickWindow;

// The shell's frosted glass (shell.blur): for each output, the wallpaper as the desktop shows it
// there, blurred once each time it changes (blurWallpaper, on a thread of its own), which every
// surface of the shell on that output shows the part of that lies behind it (GlassItem). It is
// the wallpaper that shows through, never the windows, as Hyprland's xray has it.
//
// Outputs are known by their screens' names. Desktop.qml hands it each output's wallpaper as it
// is drawn (capture); a preview, which has no desktop, hands it the one it draws its pictures
// over (setWallpaper).
class Glass : public QObject {
    Q_OBJECT
  public:
    explicit Glass(QObject *parent = nullptr);
    ~Glass() override;
    // shell.blur, from each configuration loaded: on, the pictures it has are blurred again
    // when the blur's settings changed; off, they go.
    void configure(const paw::ShellBlur &settings);
    // Whether the shell draws through the GPU: the glass is drawn only then.
    void setEffects(bool effects);
    // Whether the glass is drawn: on, through the GPU.
    bool enabled() const { return settings_.enabled && effects_; }
    // Takes a picture of `backdrop`, the wallpaper's layers on its output's desktop as they are
    // drawn, over `background` (appearance.background, under a picture with holes), and blurs
    // it; nothing while the glass is off. Desktop.qml calls it once a new wallpaper has faded in
    // and as the configuration changes.
    Q_INVOKABLE void capture(QQuickItem *backdrop, const QColor &background);
    // `picture`, the wallpaper as output `output` shows it, `size` logical pixels, is blurred on
    // a thread, or here and now with `now`; nothing while the glass is off.
    void setWallpaper(const QString &output, const QImage &picture, QSizeF size, bool now = false);

    // An output's blurred wallpaper, which covers `size` of its logical pixels; a new serial
    // for each, from 1. Null while there is none.
    struct Picture {
        QImage image;
        QSizeF size;
        quint64 serial = 0;
    };
    Picture picture(const QString &output) const;
    // Whether a new picture of `output` is on its way: taken from the desktop or being blurred.
    bool pending(const QString &output) const;

  Q_SIGNALS:
    // The picture of `output` came, changed or went, or one is on its way. `output` is empty
    // for every output, as the glass is turned on or off.
    void changed(const QString &output);

  private:
    struct Output {
        QImage wallpaper; // as handed over, kept to blur again with other settings
        QSizeF size;
        Picture blurred;
        quint64 job = 0;  // the blur under way, whose result is the one wanted
        int captures = 0; // pictures of the desktop under way
    };
    paw::ShellBlur settings_;
    bool effects_ = false;
    QHash<QString, Output> outputs_;
    QList<QSharedPointer<QQuickItemGrabResult>> grabs_; // until they are ready
    quint64 serial_ = 0, jobs_ = 0;
    QThreadPool pool_;
    void blur(const QString &output, bool now);
};

// A rounded rectangle of frosted glass, under a surface drawn translucent over it: the part of
// its output's blurred wallpaper (Glass) that lies behind it, its corners `radius` round (then
// inset by half a pixel, under the antialiased edge of what is drawn over it). It shows while the
// glass is on, drawn through the GPU, and its output has a picture: `active`, which the surface
// over it reads to be translucent. The picture is its window's texture, one for all the glass
// there, uploaded once for each new picture. Where it is on the output is found anew with each
// frame its window draws, so that a card sliding in or a switcher growing shows, at each step, the
// wallpaper behind it: the glass stays put on the screen as a pane of glass would.
class GlassItem : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(qreal radius READ radius WRITE setRadius NOTIFY radiusChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    // A new picture of its output is on its way: Restyle waits for it, as for a picture loading.
    Q_PROPERTY(bool pending READ pending NOTIFY pendingChanged)
  public:
    explicit GlassItem(QQuickItem *parent = nullptr);
    qreal radius() const { return radius_; }
    void setRadius(qreal radius);
    bool active() const { return active_; }
    bool pending() const { return pending_; }

  Q_SIGNALS:
    void radiusChanged();
    void activeChanged();
    void pendingChanged();

  protected:
    void componentComplete() override;
    void itemChange(ItemChange change, const ItemChangeData &data) override;
    void geometryChange(const QRectF &geometry, const QRectF &old) override;
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *data) override;

  private:
    QPointer<Glass> glass_;
    QMetaObject::Connection frames_;
    qreal radius_ = 0;
    bool active_ = false, pending_ = false;
    Glass::Picture picture_;
    // From the item's coordinates to its output's, and the picture, as last drawn.
    QTransform drawn_;
    quint64 drawnSerial_ = 0;
    QTransform placement() const;
    QString output() const;
    void refresh();
    void follow();
};
