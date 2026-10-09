// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "tray.hpp"
#include <QQuickImageProvider>

// image://tray/SERIAL/REVISION: a tray item's icon, or the stand-in for an item without one;
// image://tray/SERIAL/menu/ID/REVISION: an entry of its menu. The revision only keeps QML from
// reusing a picture that has changed.
class TrayImages : public QQuickImageProvider {
  public:
    explicit TrayImages(TrayModel &model) : QQuickImageProvider(QQuickImageProvider::Image), model_(model) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override;

  private:
    TrayModel &model_;
};
