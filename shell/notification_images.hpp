// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "notifications.hpp"
#include <QQuickImageProvider>

// image://notify/ID/STAMP: the picture a notification carried in its image-data hint (the stamp
// only keeps QML from reusing a cached picture when a notification is replaced).
class NotificationImages : public QQuickImageProvider {
  public:
    explicit NotificationImages(NotificationCenter &center)
        : QQuickImageProvider(QQuickImageProvider::Image), center_(center) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override;

  private:
    NotificationCenter &center_;
};
