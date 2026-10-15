// SPDX-License-Identifier: GPL-3.0-or-later
#include "glass.hpp"
#include "glass_blur.hpp"
#include "view.hpp"
#include <QGuiApplication>
#include <QMutex>
#include <QPainter>
#include <QQmlContext>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGTextureMaterial>
#include <QScreen>
#include <cmath>
#include <iostream>
#include <memory>
#include <numbers>

Glass::Glass(QObject *parent) : QObject(parent) {
    // One blur at a time: a new wallpaper on every output at once waits its turn.
    pool_.setMaxThreadCount(1);
    // An output unplugged takes its pictures with it.
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this](QScreen *screen) {
        if (outputs_.remove(screen->name()))
            Q_EMIT changed(screen->name());
    });
}

Glass::~Glass() {
    pool_.clear();
    pool_.waitForDone();
}

void Glass::configure(const paw::ShellBlur &settings) {
    const bool was = enabled();
    const paw::ShellBlur before = settings_;
    settings_ = settings;
    if (!settings_.enabled) {
        outputs_.clear();
    } else if (before.enabled && before != settings_) {
        for (auto it = outputs_.cbegin(); it != outputs_.cend(); ++it)
            if (!it->wallpaper.isNull())
                blur(it.key(), false);
    }
    if (was != enabled())
        Q_EMIT changed({});
}

void Glass::setEffects(bool effects) {
    if (effects == effects_)
        return;
    const bool was = enabled();
    effects_ = effects;
    if (was != enabled())
        Q_EMIT changed({});
}

void Glass::capture(QQuickItem *backdrop, const QColor &background) {
    QQuickWindow *window = backdrop ? backdrop->window() : nullptr;
    if (!enabled() || !window || !window->isVisible() || !window->screen() ||
        backdrop->size().isEmpty())
        return;
    const QString output = window->screen()->name();
    const QSizeF size = backdrop->size();
    // Twice as fine as the blur works at, which then averages it down, but never finer than the
    // output: what a large wallpaper drawn small skips is evened out.
    const double scale = std::max(1.0, blurStep(size, settings_.radius) / 2);
    const QSize target(std::max(1, qRound(size.width() / scale)),
                       std::max(1, qRound(size.height() / scale)));
    auto grab = backdrop->grabToImage(target);
    if (!grab)
        return;
    ++outputs_[output].captures;
    grabs_.push_back(grab);
    QQuickItemGrabResult *result = grab.data();
    connect(result, &QQuickItemGrabResult::ready, this, [this, result, output, size, background] {
        // The grab's result goes once it has told this, never inside its own signal.
        QMetaObject::invokeMethod(
            this,
            [this, result] {
                grabs_.removeIf([result](const auto &g) { return g.data() == result; });
            },
            Qt::QueuedConnection);
        auto it = outputs_.find(output);
        if (it == outputs_.end())
            return; // turned off meanwhile
        it->captures = std::max(0, it->captures - 1);
        QImage picture(result->image().size(), QImage::Format_RGB32);
        picture.fill(background.isValid() ? background.rgb() : qRgb(0, 0, 0));
        {
            QPainter painter(&picture);
            painter.drawImage(0, 0, result->image());
        }
        setWallpaper(output, picture, size);
        Q_EMIT changed(output); // no longer pending, should it not blur
    });
    Q_EMIT changed(output);
}

void Glass::setWallpaper(const QString &output, const QImage &picture, QSizeF size, bool now) {
    if (!settings_.enabled || picture.isNull() || size.isEmpty())
        return;
    auto &entry = outputs_[output];
    entry.wallpaper = picture;
    entry.size = size;
    blur(output, now);
}

void Glass::blur(const QString &output, bool now) {
    auto &entry = outputs_[output];
    const quint64 job = ++jobs_;
    entry.job = job;
    const QImage wallpaper = entry.wallpaper;
    const QSizeF size = entry.size;
    const paw::ShellBlur settings = settings_;
    auto finish = [this, output, job, size](const QImage &blurred) {
        auto it = outputs_.find(output);
        if (it == outputs_.end() || it->job != job)
            return; // turned off, or another came after it
        it->job = 0;
        it->blurred = {blurred, size, ++serial_};
        std::cerr << "paw glass blurred " << output.toStdString() << ", " << blurred.width() << "x"
                  << blurred.height() << " pixels for " << qRound(size.width()) << "x"
                  << qRound(size.height()) << '\n';
        Q_EMIT changed(output);
    };
    const auto work = [wallpaper, size, settings] {
        return blurWallpaper(wallpaper, size, settings.radius, settings.brightness,
                             settings.saturation);
    };
    if (now) {
        finish(work());
        return;
    }
    Q_EMIT changed(output);
    pool_.start([this, work, finish] {
        const QImage blurred = work();
        QMetaObject::invokeMethod(
            this, [finish, blurred] { finish(blurred); }, Qt::QueuedConnection);
    });
}

Glass::Picture Glass::picture(const QString &output) const {
    return enabled() ? outputs_.value(output).blurred : Picture{};
}

bool Glass::pending(const QString &output) const {
    const auto it = outputs_.constFind(output);
    return enabled() && it != outputs_.cend() && (it->job != 0 || it->captures > 0);
}

namespace {
// Each window's texture of the blurred picture its glass shows, shared by all of it: made on the
// window's render thread as its glass is first drawn, made again for a new picture, and let go
// as the window's graphics go. A node holds the texture it draws until it takes the next, so a
// picture replaced lasts until the last glass showing it has moved on.
struct WindowTexture {
    quint64 serial = 0;
    std::shared_ptr<QSGTexture> texture;
};
QMutex texturesLock; // windows may render on threads of their own
QHash<QQuickWindow *, WindowTexture> textures;

std::shared_ptr<QSGTexture> windowTexture(QQuickWindow *window, const Glass::Picture &picture) {
    QMutexLocker lock(&texturesLock);
    auto it = textures.find(window);
    if (it == textures.end()) {
        it = textures.insert(window, {});
        // On the render thread, with its graphics still there to release the texture from.
        QObject::connect(
            window, &QQuickWindow::sceneGraphInvalidated, window,
            [window] {
                QMutexLocker lock(&texturesLock);
                if (auto found = textures.find(window); found != textures.end())
                    *found = {};
            },
            Qt::DirectConnection);
        QObject::connect(window, &QObject::destroyed, [window] {
            QMutexLocker lock(&texturesLock);
            textures.remove(window);
        });
    }
    if (!it->texture || it->serial != picture.serial) {
        // Never in an atlas: the coordinates run over the whole texture, and its edges clamp.
        it->texture.reset(
            window->createTextureFromImage(picture.image, QQuickWindow::TextureIsOpaque));
        it->serial = picture.serial;
    }
    return it->texture;
}

// The glass's rounded rectangle, its texture coordinates where each point lies on the output.
class GlassNode : public QSGGeometryNode {
  public:
    GlassNode()
        : geometry_(QSGGeometry::defaultAttributes_TexturedPoint2D(), 0, 0,
                    QSGGeometry::UnsignedShortType) {
        geometry_.setDrawingMode(QSGGeometry::DrawTriangles);
        setGeometry(&geometry_);
        material_.setFiltering(QSGTexture::Linear);
        opaque_.setFiltering(QSGTexture::Linear);
        // While it is faded the one, at rest the other.
        setMaterial(&material_);
        setOpaqueMaterial(&opaque_);
    }
    void setTexture(std::shared_ptr<QSGTexture> texture) {
        if (texture == texture_)
            return;
        texture_ = std::move(texture);
        material_.setTexture(texture_.get());
        opaque_.setTexture(texture_.get());
        markDirty(DirtyMaterial);
    }
    // `rect` in the item's coordinates, with corners `radius` round, `toOutput` mapping it onto
    // the output, which the picture covers `output` of.
    void shape(QRectF rect, qreal radius, const QTransform &toOutput, QSizeF output) {
        // Rounded, half a pixel in, under the antialiased edge of what is drawn over it, which
        // the glass's own hard edge would otherwise show past at the corners; square, to the edge.
        const qreal inset = radius > 0 ? 0.5 : 0;
        rect.adjust(inset, inset, -inset, -inset);
        radius = std::clamp(radius - inset, 0.0, std::min(rect.width(), rect.height()) / 2);
        // A point every two pixels or so along each corner's arc.
        const int steps =
            radius < 0.5 ? 0 : std::clamp(static_cast<int>(std::ceil(radius / 2)), 2, 24);
        const QPointF centres[4] = {{rect.left() + radius, rect.top() + radius},
                                    {rect.right() - radius, rect.top() + radius},
                                    {rect.right() - radius, rect.bottom() - radius},
                                    {rect.left() + radius, rect.bottom() - radius}};
        QList<QPointF> outline;
        for (int corner = 0; corner < 4; ++corner) {
            // From the left edge round the top-left corner, clockwise.
            const double from = std::numbers::pi * (1 + corner * 0.5);
            for (int i = 0; i <= steps; ++i) {
                const double angle = from + (steps ? std::numbers::pi / 2 * i / steps : 0);
                outline.push_back(centres[corner] +
                                  QPointF(std::cos(angle) * radius, std::sin(angle) * radius));
            }
        }
        const int count = static_cast<int>(outline.size());
        geometry_.allocate(count + 1, 3 * count);
        auto *vertices = geometry_.vertexDataAsTexturedPoint2D();
        const QRectF sub = texture_ ? texture_->normalizedTextureSubRect() : QRectF(0, 0, 1, 1);
        const auto put = [&](int index, QPointF point) {
            const QPointF at = toOutput.map(point);
            vertices[index].set(
                static_cast<float>(point.x()), static_cast<float>(point.y()),
                static_cast<float>(sub.x() + sub.width() * at.x() / output.width()),
                static_cast<float>(sub.y() + sub.height() * at.y() / output.height()));
        };
        // A fan from the middle, which a convex shape is.
        put(0, rect.center());
        auto *indices = geometry_.indexDataAsUShort();
        for (int i = 0; i < count; ++i) {
            put(i + 1, outline[i]);
            indices[3 * i] = 0;
            indices[3 * i + 1] = static_cast<quint16>(i + 1);
            indices[3 * i + 2] = static_cast<quint16>((i + 1) % count + 1);
        }
        markDirty(DirtyGeometry);
    }

  private:
    QSGGeometry geometry_;
    QSGTextureMaterial material_;
    QSGOpaqueTextureMaterial opaque_;
    std::shared_ptr<QSGTexture> texture_;
};
} // namespace

GlassItem::GlassItem(QQuickItem *parent) : QQuickItem(parent) { setFlag(ItemHasContents); }

void GlassItem::setRadius(qreal radius) {
    if (qFuzzyCompare(radius, radius_))
        return;
    radius_ = radius;
    Q_EMIT radiusChanged();
    update();
}

void GlassItem::componentComplete() {
    QQuickItem::componentComplete();
    // The shell's controller holds the glass; a test's QML without it has none, and no glass.
    if (auto *context = qmlContext(this))
        if (auto *shell = context->contextProperty("shell").value<QObject *>())
            glass_ = shell->property("glass").value<Glass *>();
    if (glass_)
        connect(glass_, &Glass::changed, this, [this](const QString &output) {
            if (output.isEmpty() || output == this->output())
                refresh();
        });
    refresh();
}

void GlassItem::itemChange(ItemChange change, const ItemChangeData &data) {
    QQuickItem::itemChange(change, data);
    if (change == ItemSceneChange) {
        disconnect(frames_);
        if (data.window) {
            // Before each frame is synchronised, after the animations have stepped.
            frames_ = connect(data.window, &QQuickWindow::afterAnimating, this, &GlassItem::follow);
            connect(data.window, &QWindow::screenChanged, this, &GlassItem::refresh,
                    Qt::UniqueConnection);
        }
        refresh();
    }
}

void GlassItem::geometryChange(const QRectF &geometry, const QRectF &old) {
    QQuickItem::geometryChange(geometry, old);
    update();
}

QString GlassItem::output() const {
    const QQuickWindow *window = this->window();
    return window && window->screen() ? window->screen()->name() : QString();
}

void GlassItem::refresh() {
    const QString output = this->output();
    const Glass::Picture picture = glass_ && window() ? glass_->picture(output) : Glass::Picture{};
    const bool active = !picture.image.isNull();
    const bool pending = glass_ && glass_->pending(output);
    if (picture.serial != picture_.serial) {
        picture_ = picture;
        update();
    }
    if (active != active_) {
        active_ = active;
        Q_EMIT activeChanged();
        update();
    }
    if (pending != pending_) {
        pending_ = pending;
        Q_EMIT pendingChanged();
    }
}

QTransform GlassItem::placement() const {
    const QQuickWindow *window = this->window();
    if (!window)
        return {};
    const QPointF origin = surfaceOrigin(window, picture_.size);
    return itemTransform(nullptr, nullptr) * QTransform::fromTranslate(origin.x(), origin.y());
}

void GlassItem::follow() {
    if (active_ && isVisible() && (placement() != drawn_ || drawnSerial_ != picture_.serial))
        update();
}

QSGNode *GlassItem::updatePaintNode(QSGNode *old, UpdatePaintNodeData *) {
    QQuickWindow *window = this->window();
    const auto api =
        window ? window->rendererInterface()->graphicsApi() : QSGRendererInterface::Unknown;
    // Qt's software renderer draws no geometry of its own: the surface over it stays as it is
    // without the glass.
    if (!active_ || !QSGRendererInterface::isApiRhiBased(api) ||
        api == QSGRendererInterface::Null || width() < 1 || height() < 1 ||
        picture_.size.isEmpty()) {
        delete old;
        return nullptr;
    }
    auto *node = static_cast<GlassNode *>(old);
    if (!node)
        node = new GlassNode;
    node->setTexture(windowTexture(window, picture_));
    drawn_ = placement();
    drawnSerial_ = picture_.serial;
    node->shape(QRectF(0, 0, width(), height()), radius_, drawn_, picture_.size);
    return node;
}
