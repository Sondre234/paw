// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QCoreApplication>
#include <QQuickImageProvider>
#include <QThread>
#include <functional>

// An image provider serving what `picture` gives for the part of the URL after the provider's name
// and the size asked for (empty for none), at its own size.
//
// It serves only on the GUI thread: the shell's pictures come from its models, which belong to
// that thread and have no lock, and an asynchronous Image asks on Qt Quick's loading thread
// instead, where it gets no picture.
class ImageProvider : public QQuickImageProvider {
  public:
    using Picture = std::function<QImage(const QString &id, const QSize &requested)>;
    explicit ImageProvider(Picture picture)
        : QQuickImageProvider(QQuickImageProvider::Image), picture_(std::move(picture)) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override {
        const auto *app = QCoreApplication::instance();
        QImage image =
            app && QThread::currentThread() == app->thread() ? picture_(id, requested) : QImage();
        if (size)
            *size = image.size();
        return image;
    }

  private:
    Picture picture_;
};
