// SPDX-License-Identifier: GPL-3.0-or-later
// The shell's image providers on the threads Qt Quick asks them on: the pictures of its models
// (ImageProvider) only on the GUI thread, where a synchronous Image asks, never on the loading
// thread an asynchronous one asks on; and the wallpaper picker's thumbnails (Thumbnails), made on
// a pool of threads, told done in the response's own thread, which Qt Quick deletes it in.
#include "image_provider.hpp"
#include "wallpapers.hpp"
#include <QDir>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickImageResponse>
#include <QQuickTextureFactory>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <atomic>
#include <memory>
#include <thread>

namespace {
// Image.status: Ready and Error.
constexpr int ready = 1, error = 3;

// A square picture `id` pixels wide, counting the requests.
ImageProvider::Picture squares(std::atomic<int> &asked) {
    return [&asked](const QString &id, const QSize &) {
        ++asked;
        QImage image(id.toInt(), id.toInt(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::red);
        return image;
    };
}
} // namespace

class ImageProvidersTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir_;

    // Pictures under the test's directory, `count` of them in `folder`, `size` pixels high and
    // each a pixel wider than the one before, from `size` on.
    QStringList pictures(const QString &folder, int count, int size) {
        QDir().mkpath(dir_.filePath(folder));
        QStringList paths;
        for (int i = 0; i < count; ++i) {
            QImage image(size + i, size, QImage::Format_RGB32);
            image.fill(QColor::fromHsv(i * 37 % 360, 200, 200));
            paths << dir_.filePath(folder + QString("/%1.png").arg(i));
            if (!image.save(paths.last()))
                return {};
        }
        return paths;
    }
    // Images of image://thumbs/ for `paths`, loaded asynchronously, as the wallpaper picker's.
    static QByteArray thumbnailsQml(const QStringList &paths) {
        QByteArray qml = "import QtQuick\nItem {\n";
        for (const auto &path : paths)
            qml += "    Image { asynchronous: true; sourceSize: Qt.size(256, 256); source: \"image://thumbs/" +
                   QUrl::toPercentEncoding(path) + "\" }\n";
        return qml + "}\n";
    }
    static int countWithStatus(QObject *root, int status) {
        int count = 0;
        for (auto *child : root->children())
            count += child->property("status").toInt() == status;
        return count;
    }

  private Q_SLOTS:
    void initTestCase() {
        QVERIFY(dir_.isValid());
        // Thumbnails are kept in the freedesktop cache: here the test's own.
        qputenv("XDG_CACHE_HOME", dir_.filePath("cache").toUtf8());
    }
    // Asked on the GUI thread it serves; on another, nothing, never calling its picture.
    void guiThreadOnly() {
        std::atomic<int> asked = 0;
        ImageProvider provider(squares(asked));
        QSize size;
        QCOMPARE(provider.requestImage("4", &size, {}).size(), QSize(4, 4));
        QCOMPARE(size, QSize(4, 4));
        QImage elsewhere;
        std::thread([&] { elsewhere = provider.requestImage("4", &size, {}); }).join();
        QVERIFY(elsewhere.isNull());
        QVERIFY(size.isEmpty());
        QCOMPARE(asked.load(), 1);
    }
    // In QML, a synchronous Image gets the picture and an asynchronous one, asking on the loading
    // thread, an error.
    void asynchronousImage() {
        std::atomic<int> asked = 0;
        QQmlEngine engine;
        engine.addImageProvider("squares", new ImageProvider(squares(asked)));
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nItem {\n"
                          "    Image { objectName: \"now\"; source: \"image://squares/4\" }\n"
                          "    Image { objectName: \"later\"; asynchronous: true; source: \"image://squares/5\" }\n"
                          "}\n",
                          QUrl());
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression("Failed to get image from provider: image://squares/5"));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *now = root->findChild<QObject *>("now");
        auto *later = root->findChild<QObject *>("later");
        QCOMPARE(now->property("status").toInt(), ready);
        QCOMPARE(now->property("implicitWidth").toReal(), 4.0);
        QTRY_COMPARE(later->property("status").toInt(), error);
        QCOMPARE(asked.load(), 1);
    }
    // A thumbnail is made on the pool, saved in the cache and told done in the response's own
    // thread; one cancelled at once is told done too.
    void thumbnailResponse() {
        const auto paths = pictures("one", 1, 600);
        QVERIFY(!paths.isEmpty());
        Thumbnails provider;
        for (bool cancelled : {false, true}) {
            std::unique_ptr<QQuickImageResponse> response(
                provider.requestImageResponse(QUrl::toPercentEncoding(paths[0]), QSize(256, 256)));
            QThread *told = nullptr;
            int times = 0;
            connect(response.get(), &QQuickImageResponse::finished, this,
                    [&] {
                        told = QThread::currentThread();
                        ++times;
                    },
                    Qt::DirectConnection);
            if (cancelled)
                response->cancel();
            QTRY_COMPARE(times, 1);
            QCOMPARE(told, response->thread());
            if (cancelled) {
                QVERIFY(response->errorString().isEmpty());
                continue;
            }
            QVERIFY2(response->errorString().isEmpty(), qPrintable(response->errorString()));
            std::unique_ptr<QQuickTextureFactory> texture(response->textureFactory());
            QCOMPARE(texture->image().size(), QSize(256, 256));
            QCOMPARE(QDir(dir_.filePath("cache/thumbnails/large")).entryList(QDir::Files).size(), 1);
        }
    }
    // Asynchronous Images of the thumbnails, as the wallpaper picker's, all load; a page of them
    // gone before they have is no harm, its responses deleted as they are told done.
    void thumbnailImages() {
        const auto paths = pictures("many", 24, 300);
        QVERIFY(!paths.isEmpty());
        QQmlEngine engine;
        engine.addImageProvider("thumbs", new Thumbnails);
        QQmlComponent component(&engine);
        component.setData(thumbnailsQml(paths), QUrl());
        for (int round = 0; round < 5; ++round) {
            std::unique_ptr<QObject> gone(component.create());
            QVERIFY2(gone, qPrintable(component.errorString()));
            QCoreApplication::processEvents();
        }
        std::unique_ptr<QObject> root(component.create());
        QTRY_COMPARE(countWithStatus(root.get(), ready), int(paths.size()));
        for (auto *image : root->children())
            QCOMPARE(image->property("implicitWidth").toReal(), 256.0);
    }
};
QTEST_MAIN(ImageProvidersTest)
#include "image_providers_test.moc"
