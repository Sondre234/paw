// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "task_model.hpp"
#include <QQuickImageProvider>

// image://windows/ID/SERIAL: the last picture of the window TaskModel numbers ID, as its
// `picture` role names it (the serial only keeps QML from reusing a picture that has changed).
class WindowImages : public QQuickImageProvider {
  public:
    explicit WindowImages(const TaskModel &tasks)
        : QQuickImageProvider(QQuickImageProvider::Image), tasks_(tasks) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override;

  private:
    const TaskModel &tasks_;
};
