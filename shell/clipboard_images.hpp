// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "clipboard.hpp"
#include <QQuickImageProvider>

// image://clipboard/ID/SERIAL: the picture of the clipboard history's entry ID, as its `image`
// names it (the serial only keeps QML from reusing a picture that changed); an empty one when the
// entry has gone.
class ClipboardImages : public QQuickImageProvider {
  public:
    explicit ClipboardImages(const ClipboardHistory &history)
        : QQuickImageProvider(QQuickImageProvider::Image), history_(history) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &) override {
        QImage image = history_.picture(id.section('/', 0, 0).toInt());
        if (image.isNull()) {
            image = QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
        }
        if (size)
            *size = image.size();
        return image;
    }

  private:
    const ClipboardHistory &history_;
};
