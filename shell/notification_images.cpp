// SPDX-License-Identifier: GPL-3.0-or-later
#include "notification_images.hpp"

QImage NotificationImages::requestImage(const QString &id, QSize *size, const QSize &) {
    const uint number = id.section('/', 0, 0).toUInt();
    const Notification *n = center_.cards()->find(number);
    if (!n)
        n = center_.history()->find(number);
    QImage image = n ? n->image : QImage();
    if (size)
        *size = image.size();
    return image;
}
