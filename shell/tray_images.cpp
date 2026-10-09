// SPDX-License-Identifier: GPL-3.0-or-later
#include "tray_images.hpp"
#include "icons.hpp"

QImage TrayImages::requestImage(const QString &id, QSize *size, const QSize &requested) {
    const QSize wanted = requested.isEmpty() ? QSize(22, 22) : requested;
    const QStringList parts = id.split('/');
    QImage image;
    if (parts.value(1) == "menu") {
        image = model_.menuPicture(parts.value(0).toInt(), parts.value(2).toInt(), wanted);
        // An icon that is not found leaves the space empty.
        if (image.isNull()) {
            image = QImage(wanted, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
        }
    } else {
        image = model_.picture(parts.value(0).toInt(), wanted);
        if (image.isNull())
            image = Icons().requestImage("application-x-executable", nullptr, wanted);
    }
    if (size)
        *size = image.size();
    return image;
}
