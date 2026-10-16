// SPDX-License-Identifier: GPL-3.0-or-later
// The shell's image providers on the threads Qt Quick asks them on: the pictures of its models
// (ImageProvider) only on the GUI thread, where a synchronous Image asks, never on the loading
// thread an asynchronous one asks on.
#include "image_provider.hpp"
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QTest>
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

  private Q_SLOTS:
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
};
QTEST_MAIN(ImageProvidersTest)
#include "image_providers_test.moc"
