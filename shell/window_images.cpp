// SPDX-License-Identifier: GPL-3.0-or-later
#include "window_images.hpp"

QImage WindowImages::requestImage(const QString &id, QSize *size, const QSize &) {
    QImage image = tasks_.picture(id.section('/', 0, 0).toInt());
    // A window that closed as its picture was asked for leaves nothing to show.
    if (image.isNull()) {
        image = QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
    }
    if (size)
        *size = image.size();
    return image;
}
