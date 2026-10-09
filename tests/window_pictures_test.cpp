// SPDX-License-Identifier: GPL-3.0-or-later
#include "window_pictures.hpp"
#include <QTest>
#include <QtEndian>
#include <vector>

namespace {
// A frame as the compositor leaves one in a wl_shm buffer: `words` packed little-endian, one a
// pixel, in rows of `width`.
struct Frame {
    std::vector<quint32> memory;
    QImage image;
    Frame(uint32_t format, int width, const std::vector<quint32> &words) {
        for (quint32 word : words)
            memory.push_back(qToLittleEndian(word));
        const int height = int(words.size()) / width;
        image = QImage(reinterpret_cast<const uchar *>(memory.data()), width, height, width * 4,
                       WindowPictures::imageFormat(format));
    }
};
// A colour of eight bits a channel as ten, so that reading it back is exact.
quint32 ten(int channel) { return quint32(channel << 2 | channel >> 6); }
quint32 rgb30(int r, int g, int b, int alpha) {
    return quint32(alpha) << 30 | ten(r) << 20 | ten(g) << 10 | ten(b);
}
quint32 bgr30(int r, int g, int b, int alpha) {
    return quint32(alpha) << 30 | ten(b) << 20 | ten(g) << 10 | ten(r);
}
bool near(QColor actual, QColor expected) {
    auto close = [](int a, int b) { return std::abs(a - b) <= 1; };
    return close(actual.red(), expected.red()) && close(actual.green(), expected.green()) &&
           close(actual.blue(), expected.blue()) && close(actual.alpha(), expected.alpha());
}
QByteArray describe(QColor actual, QColor expected) {
    return QStringLiteral("got %1 alpha %2, expected %3 alpha %4")
        .arg(actual.name(), QString::number(actual.alpha()), expected.name(),
             QString::number(expected.alpha()))
        .toUtf8();
}
} // namespace

class WindowPicturesTest : public QObject {
    Q_OBJECT
  private Q_SLOTS:
    // Each format the compositor may offer reads as the colours it packs, premultiplied where it
    // has alpha, opaque whatever the padding holds where it has none: one pixel #336699, the
    // other #804020, at half alpha in eight bits, two thirds in two, or with padding.
    void formats_data() {
        QTest::addColumn<uint>("format");
        QTest::addColumn<quint32>("opaque");
        QTest::addColumn<quint32>("translucent");
        QTest::addColumn<int>("alpha");
        QTest::newRow("ABGR8888") << uint(WL_SHM_FORMAT_ABGR8888) << 0xff996633u << 0x80102040u
                                  << 0x80;
        QTest::newRow("XBGR8888") << uint(WL_SHM_FORMAT_XBGR8888) << 0x00996633u << 0x00204080u
                                  << 0xff;
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
        QTest::newRow("ARGB8888") << uint(WL_SHM_FORMAT_ARGB8888) << 0xff336699u << 0x80402010u
                                  << 0x80;
        QTest::newRow("XRGB8888") << uint(WL_SHM_FORMAT_XRGB8888) << 0x00336699u << 0x00804020u
                                  << 0xff;
        // #804020 at two thirds, premultiplied, is #552b15.
        QTest::newRow("ARGB2101010") << uint(WL_SHM_FORMAT_ARGB2101010)
                                     << rgb30(0x33, 0x66, 0x99, 3) << rgb30(0x55, 0x2b, 0x15, 2)
                                     << 0xaa;
        QTest::newRow("XRGB2101010") << uint(WL_SHM_FORMAT_XRGB2101010)
                                     << rgb30(0x33, 0x66, 0x99, 0) << rgb30(0x80, 0x40, 0x20, 2)
                                     << 0xff;
        QTest::newRow("ABGR2101010") << uint(WL_SHM_FORMAT_ABGR2101010)
                                     << bgr30(0x33, 0x66, 0x99, 3) << bgr30(0x55, 0x2b, 0x15, 2)
                                     << 0xaa;
        QTest::newRow("XBGR2101010") << uint(WL_SHM_FORMAT_XBGR2101010)
                                     << bgr30(0x33, 0x66, 0x99, 1) << bgr30(0x80, 0x40, 0x20, 0)
                                     << 0xff;
#endif
    }
    void formats() {
        QFETCH(uint, format);
        QFETCH(quint32, opaque);
        QFETCH(quint32, translucent);
        QFETCH(int, alpha);
        Frame frame(format, 2, {opaque, translucent});
        QVERIFY(!frame.image.isNull());
        QCOMPARE(frame.image.hasAlphaChannel(), alpha != 0xff);
        // Kept at its own size, as a frame smaller than the picture is.
        const QImage picture = WindowPictures::scaled(frame.image, 240);
        QCOMPARE(picture.size(), QSize(2, 1));
        const QColor first = picture.pixelColor(0, 0), second = picture.pixelColor(1, 0);
        QVERIFY2(near(first, QColor(0x33, 0x66, 0x99)), describe(first, QColor(0x33, 0x66, 0x99)));
        QVERIFY2(near(second, QColor(0x80, 0x40, 0x20, alpha)),
                 describe(second, QColor(0x80, 0x40, 0x20, alpha)));
        // Qt reads a pixel without alpha as opaque but draws its padding along: none is left.
        if (alpha == 0xff) {
            QCOMPARE(picture.format(), QImage::Format_RGB32);
            const auto *words = reinterpret_cast<const QRgb *>(picture.constScanLine(0));
            QCOMPARE(qAlpha(words[0]), 0xff);
            QCOMPARE(qAlpha(words[1]), 0xff);
        }
    }
    void unknownFormats() {
        for (uint32_t format : {uint32_t(WL_SHM_FORMAT_RGB565), uint32_t(WL_SHM_FORMAT_RGBA8888),
                                uint32_t(WL_SHM_FORMAT_BGRA8888), uint32_t(WL_SHM_FORMAT_NV12),
                                uint32_t(WL_SHM_FORMAT_RGB888)})
            QCOMPARE(WindowPictures::imageFormat(format), QImage::Format_Invalid);
    }
    // The box asked of a scaled capture source: 0.625 as tall as it is wide, rounded, never
    // empty.
    void boxes() {
        QCOMPARE(WindowPictures::pictureBox(240), QSize(240, 150));
        QCOMPARE(WindowPictures::pictureBox(300), QSize(300, 188));
        QCOMPARE(WindowPictures::pictureBox(241), QSize(241, 151));
        QCOMPARE(WindowPictures::pictureBox(1), QSize(1, 1));
        QCOMPARE(WindowPictures::pictureBox(0), QSize(1, 1));
        QCOMPARE(WindowPictures::pictureBox(-5), QSize(1, 1));
    }
    // A frame a scaled source made to fit the box is kept at its size, so that scaling it is
    // only a copy; one a pixel too large for it (a compositor rounding otherwise) is scaled.
    void scaledFrames() {
        for (const QSize frame : {QSize(300, 169), QSize(300, 188), QSize(133, 188), QSize(1, 1)})
            QCOMPARE(WindowPictures::pictureSize(frame, 300), frame);
        QCOMPARE(WindowPictures::pictureSize({301, 169}, 300), QSize(300, 168));
    }
    // Scaled down, never up, into a box 0.625 as tall as it is wide, keeping the aspect ratio.
    void sizes() {
        QCOMPARE(WindowPictures::pictureSize({3840, 2160}, 240), QSize(240, 135));
        QCOMPARE(WindowPictures::pictureSize({320, 240}, 160), QSize(133, 100));
        QCOMPARE(WindowPictures::pictureSize({2000, 1250}, 480), QSize(480, 300));
        QCOMPARE(WindowPictures::pictureSize({100, 50}, 240), QSize(100, 50));
        QCOMPARE(WindowPictures::pictureSize({240, 150}, 240), QSize(240, 150));
        QCOMPARE(WindowPictures::pictureSize({241, 150}, 240), QSize(240, 149));
        QCOMPARE(WindowPictures::pictureSize({4000, 10}, 240), QSize(240, 1));
        QCOMPARE(WindowPictures::pictureSize({10, 4000}, 240), QSize(1, 150));
        // A box 151 tall for 241: rounded, not cut.
        QCOMPARE(WindowPictures::pictureSize({2410, 1510}, 241), QSize(241, 151));
        QCOMPARE(WindowPictures::pictureSize({500, 500}, 0), QSize(1, 1));
        QCOMPARE(WindowPictures::pictureSize({0, 300}, 240), QSize());
    }
    // A picture shares no memory with the buffer, which the next frame is copied into, whether
    // it was scaled or kept at its size.
    void sharesNothing() {
        for (int width : {240, 100}) {
            std::vector<quint32> words(200 * 100, 0xff336699u);
            Frame frame(WL_SHM_FORMAT_ABGR8888, 200, words);
            const QImage picture = WindowPictures::scaled(frame.image, width);
            QCOMPARE(picture.size(), width == 240 ? QSize(200, 100) : QSize(100, 50));
            std::fill(frame.memory.begin(), frame.memory.end(), 0u);
            QCOMPARE(picture.pixelColor(picture.width() / 2, picture.height() / 2),
                     QColor(0x99, 0x66, 0x33));
        }
    }
    // A 4K window scaled down whole: its colours stay, its padding does not show.
    void large() {
        std::vector<quint32> words(3840 * 2160);
        for (size_t i = 0; i < words.size(); ++i)
            words[i] = (i / 3840 < 1080 ? 0x0023314au : 0x00417bc4u);
        Frame frame(WL_SHM_FORMAT_XBGR8888, 3840, words);
        const QImage picture = WindowPictures::scaled(frame.image, 240);
        QCOMPARE(picture.size(), QSize(240, 135));
        QVERIFY(near(picture.pixelColor(120, 20), QColor(0x4a, 0x31, 0x23)));
        QVERIFY(near(picture.pixelColor(120, 115), QColor(0xc4, 0x7b, 0x41)));
        QCOMPARE(picture.format(), QImage::Format_RGB32);
        QCOMPARE(qAlpha(reinterpret_cast<const QRgb *>(picture.constScanLine(20))[120]), 0xff);
    }
};
QTEST_GUILESS_MAIN(WindowPicturesTest)
#include "window_pictures_test.moc"
