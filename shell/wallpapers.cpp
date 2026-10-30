// SPDX-License-Identifier: GPL-3.0-or-later
#include "wallpapers.hpp"
#include <QCollator>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QImageReader>
#include <QRunnable>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>
#include <atomic>

QVariantList findWallpapers(const QString &root, bool byDate) {
    struct Entry {
        QString path, name, folder;
        qint64 saved = 0; // with byDate, seconds since the epoch
    };
    std::vector<Entry> entries;
    const QDir base(root);
    QDirIterator it(root, {"*.png", "*.jpg", "*.jpeg", "*.webp", "*.bmp", "*.PNG", "*.JPG", "*.JPEG", "*.WEBP"},
                    QDir::Files | QDir::Readable, QDirIterator::Subdirectories | QDirIterator::FollowSymlinks);
    while (it.hasNext() && entries.size() < 10000) {
        const QFileInfo file(it.next());
        auto folder = base.relativeFilePath(file.path());
        qint64 saved = 0;
        if (byDate) {
            // A picture downloaded or copied in often keeps an older modification time from
            // where it came; the time the file was made says when it came into the folder.
            const auto born = file.birthTime();
            saved = (born.isValid() ? born : file.lastModified()).toSecsSinceEpoch();
        }
        entries.push_back({file.filePath(), file.completeBaseName(), folder == "." ? QString() : folder, saved});
    }
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(entries.begin(), entries.end(), [&collator](const Entry &a, const Entry &b) {
        if (a.folder != b.folder)
            return collator.compare(a.folder, b.folder) < 0;
        return collator.compare(a.name, b.name) < 0;
    });
    // Those saved in the same second, as a folder copied in at once, stay in that order.
    if (byDate)
        std::stable_sort(entries.begin(), entries.end(),
                         [](const Entry &a, const Entry &b) { return a.saved > b.saved; });
    QVariantList list;
    list.reserve(static_cast<qsizetype>(entries.size()));
    for (const auto &entry : entries)
        list.push_back(QVariantMap{{"path", entry.path}, {"name", entry.name}, {"folder", entry.folder}});
    return list;
}

namespace {
constexpr int thumbnailSize = 256; // the freedesktop "large" size

class ThumbnailResponse : public QQuickImageResponse, public QRunnable {
  public:
    explicit ThumbnailResponse(QString path) : path_(std::move(path)) { setAutoDelete(false); }
    void run() override {
        if (!cancelled_)
            image_ = Thumbnails::thumbnail(path_);
        // Told in the response's own thread (Qt Quick's loading thread), which deletes it once
        // told: emitted on the pool's, it could be deleted while the emit is still running.
        QMetaObject::invokeMethod(this, [this] { Q_EMIT finished(); }, Qt::QueuedConnection);
    }
    void cancel() override { cancelled_ = true; }
    QQuickTextureFactory *textureFactory() const override {
        return QQuickTextureFactory::textureFactoryForImage(image_);
    }
    QString errorString() const override {
        return image_.isNull() && !cancelled_ ? "not a readable picture: " + path_ : QString();
    }

  private:
    QString path_;
    QImage image_;
    std::atomic<bool> cancelled_ = false;
};
} // namespace

Thumbnails::Thumbnails() {
    // Decoding a large picture keeps a core busy; leave some for everything else.
    pool_.setMaxThreadCount(std::max(2, QThread::idealThreadCount() / 2));
}
QQuickImageResponse *Thumbnails::requestImageResponse(const QString &id, const QSize &) {
    auto *response = new ThumbnailResponse(QUrl::fromPercentEncoding(id.toUtf8()));
    pool_.start(response);
    return response;
}
QImage Thumbnails::thumbnail(const QString &path) {
    const QFileInfo file(path);
    if (!file.isFile())
        return {};
    const auto uri = QUrl::fromLocalFile(file.absoluteFilePath()).toEncoded();
    const auto mtime = QString::number(file.lastModified().toSecsSinceEpoch());
    const auto cached =
        QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + "/thumbnails/large/" +
        QCryptographicHash::hash(uri, QCryptographicHash::Md5).toHex() + ".png";
    // A cached thumbnail stands only while it names the file's current modification time.
    QImageReader saved(cached);
    if (saved.text("Thumb::MTime") == mtime) {
        auto image = saved.read();
        if (!image.isNull())
            return image;
    }
    QImageReader reader(path);
    reader.setAutoTransform(true);
    auto size = reader.size();
    // JPEG decodes straight to the smaller size; other formats are decoded whole first.
    if (size.isValid() && (size.width() > thumbnailSize || size.height() > thumbnailSize))
        reader.setScaledSize(size.scaled(thumbnailSize, thumbnailSize, Qt::KeepAspectRatio));
    auto image = reader.read();
    if (image.isNull())
        return {};
    if (image.width() > thumbnailSize || image.height() > thumbnailSize)
        image = image.scaled(thumbnailSize, thumbnailSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    image.setText("Thumb::URI", QString::fromUtf8(uri));
    image.setText("Thumb::MTime", mtime);
    image.setText("Software", "paw");
    QDir().mkpath(QFileInfo(cached).path());
    QSaveFile out(cached);
    // The specification has thumbnails readable only by their owner.
    if (out.open(QIODevice::WriteOnly) && image.save(&out, "PNG") && out.commit())
        QFile::setPermissions(cached, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return image;
}
