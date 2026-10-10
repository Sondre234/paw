// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QQuickImageProvider>
#include <functional>

// An image provider serving what `picture` gives for the part of the URL after the provider's name
// and the size asked for (empty for none), at its own size.
class ImageProvider : public QQuickImageProvider {
  public:
    using Picture = std::function<QImage(const QString &id, const QSize &requested)>;
    explicit ImageProvider(Picture picture)
        : QQuickImageProvider(QQuickImageProvider::Image), picture_(std::move(picture)) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override {
        QImage image = picture_(id, requested);
        if (size)
            *size = image.size();
        return image;
    }

  private:
    Picture picture_;
};
