// SPDX-License-Identifier: GPL-3.0-or-later
// The display settings window's arrangement: a dragged monitor snapping beside the others, its
// edges lining up with theirs, never overlapping them; an arrangement settling after a monitor
// changed size, and starting at 0, 0; a monitor's logical size.
#include "display_layout.hpp"
#include <QTest>

using namespace display_layout;

class DisplayLayoutTest : public QObject {
    Q_OBJECT
  private Q_SLOTS:
    void snapsBesideAnother() {
        const QList<QRect> others{{0, 0, 2560, 1440}};
        const QSize size(1920, 1080);
        // Dropped a little away on the right: against the right edge, where it was dropped
        // along it.
        QCOMPARE(snap(others, size, {2700, 300}), QPoint(2560, 300));
        // Dropped overlapping from the left: out to the left edge.
        QCOMPARE(snap(others, size, {-1500, 100}), QPoint(-1920, 100));
        // Dropped below: under the bottom edge.
        QCOMPARE(snap(others, size, {400, 1300}), QPoint(400, 1440));
        // Far away to the bottom right: it keeps a pixel of the edge it touches, no less.
        QCOMPARE(snap(others, size, {5000, 5000}), QPoint(2559, 1440));
        // With no others it goes where it was dropped.
        QCOMPARE(snap({}, size, {123, -45}), QPoint(123, -45));
    }
    void linesUpEdgesNearby() {
        const QList<QRect> others{{0, 0, 2560, 1440}};
        const QSize size(1920, 1080);
        // Tops, bottoms and centres within the threshold come in line; further, they do not.
        QCOMPARE(snap(others, size, {2600, 30}, 50), QPoint(2560, 0));
        QCOMPARE(snap(others, size, {2600, 340}, 50), QPoint(2560, 360)); // bottoms
        QCOMPARE(snap(others, size, {2600, 200}, 50), QPoint(2560, 180)); // centres
        QCOMPARE(snap(others, size, {2600, 100}, 50), QPoint(2560, 100)); // none near
        QCOMPARE(snap(others, size, {2600, 30}, 0), QPoint(2560, 30));    // no threshold
        // Under it, lefts and rights the same.
        QCOMPARE(snap(others, size, {40, 1500}, 50), QPoint(0, 1440));
        QCOMPARE(snap(others, size, {610, 1500}, 50), QPoint(640, 1440)); // rights
        // A third monitor lines up with either.
        const QList<QRect> two{{0, 0, 2560, 1440}, {2560, 200, 1920, 1080}};
        QCOMPARE(snap(two, QSize(1280, 1024), {4500, 195}, 50), QPoint(4480, 200));
        QCOMPARE(snap(two, QSize(1280, 1024), {4500, 230}, 50), QPoint(4480, 228)); // centres
    }
    void neverOverlaps() {
        // Two side by side; a third dropped onto the seam goes beside one of them, overlapping
        // neither.
        const QList<QRect> two{{0, 0, 1920, 1080}, {1920, 0, 1920, 1080}};
        const QSize size(1280, 720);
        for (const QPoint &dropped :
             {QPoint(1500, 200), QPoint(1900, 0), QPoint(1000, 500), QPoint(-10, 1000)}) {
            const QRect placed(snap(two, size, dropped), size);
            for (const auto &other : two)
                QVERIFY2(!placed.intersects(other),
                         qPrintable(QString("dropped at %1,%2").arg(dropped.x()).arg(dropped.y())));
            QVERIFY(arranged(two + QList<QRect>{placed}));
        }
        // On the seam's top it goes over the two, where it was dropped across.
        QCOMPARE(snap(two, size, {1500, -100}), QPoint(1500, -720));
        // Hemmed in on every side but the far right, it goes there.
        const QList<QRect> ring{{0, 0, 100, 100}, {100, 0, 100, 100}, {0, 100, 100, 100}};
        QCOMPARE(snap(ring, QSize(100, 100), {100, 100}), QPoint(100, 100));
    }
    void settlesAfterAChange() {
        // The left monitor shrank from 2560 to 2048 wide (a scale of 1.25): the right one follows
        // it, where it stays level.
        QCOMPARE(settle({{0, 0, 2048, 1152}, {2560, 0, 1920, 1080}}),
                 (QList<QRect>{{0, 0, 2048, 1152}, {2048, 0, 1920, 1080}}));
        // It grew: the right one moves out of its way.
        QCOMPARE(settle({{0, 0, 3840, 2160}, {2560, 0, 1920, 1080}}),
                 (QList<QRect>{{0, 0, 3840, 2160}, {3840, 0, 1920, 1080}}));
        // A row of three keeps its order and its touching, the middle one narrower.
        QCOMPARE(settle({{0, 0, 1920, 1080}, {1920, 0, 1280, 720}, {3840, 0, 1920, 1080}}),
                 (QList<QRect>{{0, 0, 1920, 1080}, {1920, 0, 1280, 720}, {3200, 0, 1920, 1080}}));
        // An arrangement that is already one stays as it is.
        const QList<QRect> stacked{
            {0, 0, 1920, 1080}, {300, 1080, 1280, 1024}, {1920, 500, 1080, 1920}};
        QVERIFY(arranged(stacked));
        QCOMPARE(settle(stacked), stacked);
        // One far off comes back beside the others.
        const auto far = settle({{0, 0, 1920, 1080}, {9000, 9000, 1920, 1080}});
        QVERIFY(arranged(far));
        QCOMPARE(far[0], QRect(0, 0, 1920, 1080));
        QCOMPARE(settle({}), QList<QRect>{});
    }
    void startsAtTheOrigin() {
        QCOMPARE(normalise({{-1920, 120, 1920, 1080}, {0, 0, 2560, 1440}}),
                 (QList<QRect>{{0, 120, 1920, 1080}, {1920, 0, 2560, 1440}}));
        QCOMPARE(normalise({}), QList<QRect>{});
    }
    void tellsAnArrangement() {
        QVERIFY(arranged({{0, 0, 100, 100}}));
        QVERIFY(arranged({{0, 0, 100, 100}, {100, 50, 100, 100}}));
        QVERIFY(!arranged({{0, 0, 100, 100}, {150, 0, 100, 100}}));   // a gap
        QVERIFY(!arranged({{0, 0, 100, 100}, {50, 50, 100, 100}}));   // an overlap
        QVERIFY(!arranged({{0, 0, 100, 100}, {100, 100, 100, 100}})); // corners alone
    }
    void logicalSizes() {
        QCOMPARE(logicalSize({2560, 1440}, 1.0, 0), QSize(2560, 1440));
        QCOMPARE(logicalSize({2560, 1440}, 1.25, 0), QSize(2048, 1152));
        QCOMPARE(logicalSize({1280, 720}, 1.5, 0), QSize(853, 480));
        QCOMPARE(logicalSize({2560, 1440}, 2.0, 1), QSize(720, 1280));
        QCOMPARE(logicalSize({2560, 1440}, 1.0, 6), QSize(2560, 1440));
        QCOMPARE(logicalSize({1920, 1080}, 0, 0), QSize(1920, 1080));
    }
};

QTEST_GUILESS_MAIN(DisplayLayoutTest)
#include "display_layout_test.moc"
