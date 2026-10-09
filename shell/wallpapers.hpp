// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QQuickAsyncImageProvider>
#include <QString>
#include <QThreadPool>
#include <QVariantList>

// The wallpaper picker's images: every picture under `root` and its subfolders, as
// {path, name, folder} sorted by folder and then by name, numbers in order ("2" before "10").
// `folder` is the subfolder's path from `root`, "" for the pictures directly in it.
QVariantList findWallpapers(const QString &root);

// image://thumbs/<percent-encoded path>: a picture shrunk to fit 256 pixels, kept in the
// freedesktop thumbnail cache ($XDG_CACHE_HOME/thumbnails/large) that file managers share.
// Thumbnails are made on a pool of threads, so a folder of large pictures fills in quickly.
class Thumbnails : public QQuickAsyncImageProvider {
  public:
    Thumbnails();
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;
    // The thumbnail of `path`, read from the cache or made and saved there; null when the file
    // is not a picture Qt can read.
    static QImage thumbnail(const QString &path);

  private:
    QThreadPool pool_;
};
