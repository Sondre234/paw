// SPDX-License-Identifier: GPL-3.0-or-later
// The blurred wallpaper behind the shell's frosted glass (shell.blur): smaller than the output,
// a flat colour staying flat to its edges, a sharp edge spread over about the blur's width and no
// further, and the brightness and saturation applied.
#include "glass_blur.hpp"
#include <QColor>
#include <QTest>
#include <cmath>

namespace {
QImage filled(QSize size, QColor color) {
    QImage image(size, QImage::Format_RGB32);
    image.fill(color);
    return image;
}
// Black on the left half, white on the right.
QImage halves(QSize size) {
    QImage image = filled(size, Qt::black);
    for (int y = 0; y < size.height(); ++y)
        for (int x = size.width() / 2; x < size.width(); ++x)
            image.setPixel(x, y, qRgb(255, 255, 255));
    return image;
}
// The grey of the blurred picture at a share `along` of its width, in its middle row.
int greyAt(const QImage &image, double along) {
    const int x = std::clamp(static_cast<int>(along * image.width()), 0, image.width() - 1);
    return qGray(image.pixel(x, image.height() / 2));
}
} // namespace

class GlassBlurTest : public QObject {
    Q_OBJECT
  private Q_SLOTS:
    void smallerThanTheOutput() {
        // A 40-pixel blur over 2560 by 1440: a pixel for five of the output's.
        const QImage blurred = blurWallpaper(filled({1280, 720}, Qt::red), {2560, 1440}, 40);
        QCOMPARE(blurStep({2560, 1440}, 40), 5.0);
        QCOMPARE(blurred.size(), QSize(512, 288));
        QVERIFY(!blurred.hasAlphaChannel());
        // A small blur over a 4K output still keeps to about a million pixels.
        QVERIFY(blurStep({3840, 2160}, 4) > 2.8);
        const QImage large = blurWallpaper(filled({3840, 2160}, Qt::red), {3840, 2160}, 4);
        QVERIFY(large.width() * large.height() < 1'010'000);
        QVERIFY(blurWallpaper({}, {2560, 1440}, 40).isNull());
        QVERIFY(blurWallpaper(filled({16, 9}, Qt::red), {}, 40).isNull());
    }
    void flatStaysFlatToTheEdges() {
        const QImage blurred =
            blurWallpaper(filled({640, 360}, QColor(30, 120, 200)), {1920, 1080}, 60);
        for (QPoint at : {QPoint(0, 0), QPoint(blurred.width() - 1, blurred.height() - 1),
                          QPoint(blurred.width() / 2, 0), QPoint(0, blurred.height() / 2)})
            QCOMPARE(QColor(blurred.pixel(at)), QColor(30, 120, 200));
    }
    void spreadsAnEdgeOverItsWidth() {
        // Over 1000 pixels: a 40-pixel blur leaves the quarters as they were, greys the middle
        // and rises steadily across it; a 120-pixel blur spreads it further.
        const QImage narrow = blurWallpaper(halves({1000, 100}), {1000, 100}, 40);
        QCOMPARE(greyAt(narrow, 0.25), 0);
        QCOMPARE(greyAt(narrow, 0.75), 255);
        QVERIFY(std::abs(greyAt(narrow, 0.5) - 128) < 20);
        for (double at = 0.4; at < 0.6; at += 0.01)
            QVERIFY(greyAt(narrow, at) <= greyAt(narrow, at + 0.01));
        // 30 pixels left of the edge, still grey with the wide blur, all but black with the
        // narrow one.
        const QImage wide = blurWallpaper(halves({1000, 100}), {1000, 100}, 120);
        QVERIFY(greyAt(narrow, 0.47) < 30);
        QVERIFY(greyAt(wide, 0.47) > 60);
    }
    void brightnessAndSaturation() {
        const QColor orange(240, 120, 40);
        const QImage dim = blurWallpaper(filled({64, 36}, orange), {640, 360}, 40, 0.5);
        QCOMPARE(QColor(dim.pixel(0, 0)), QColor(120, 60, 20));
        const QImage grey = blurWallpaper(filled({64, 36}, orange), {640, 360}, 40, 1, 0);
        const QColor g = grey.pixel(5, 5);
        QVERIFY(g.red() == g.green() && g.green() == g.blue());
        // More vivid: the strongest channel up, the weakest down.
        const QColor vivid =
            blurWallpaper(filled({64, 36}, orange), {640, 360}, 40, 1, 1.5).pixel(5, 5);
        QVERIFY(vivid.red() > orange.red() && vivid.blue() < orange.blue());
    }
};

QTEST_GUILESS_MAIN(GlassBlurTest)
#include "glass_blur_test.moc"
