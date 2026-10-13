// SPDX-License-Identifier: GPL-3.0-or-later
// What the shell waits for before it tells the compositor a new appearance profile is drawn: each
// window shown to draw a frame, again once a picture in it has loaded, and then a quiet moment,
// or a little longer for a window that keeps drawing; a hidden window holds nothing up.
#include "restyle.hpp"
#include <QTest>

class RestyleTest : public QObject {
    Q_OBJECT
    // Two windows, by address alone.
    const int bar = 0, desktop = 0;

  private Q_SLOTS:
    void waitsForEveryWindowsFrame() {
        RestyleWait wait;
        wait.start(0);
        QVERIFY(wait.shown(&bar, false, 0)); // asked to draw
        QVERIFY(wait.shown(&desktop, false, 0));
        QVERIFY(!wait.shown(&bar, false, 10)); // once
        wait.swapped(&bar, 20);
        QVERIFY(!wait.settled(200)); // the desktop has not drawn
        wait.swapped(&desktop, 210);
        QVERIFY(!wait.settled(210 + RestyleWait::quietMs - 1));
        QVERIFY(wait.settled(210 + RestyleWait::quietMs));
    }
    void waitsForTheFrameAfterAPictureLoaded() {
        RestyleWait wait;
        wait.start(0);
        QVERIFY(wait.shown(&desktop, true, 0)); // the wallpaper decoding
        wait.swapped(&desktop, 10);
        QVERIFY(!wait.shown(&desktop, true, 100));
        QVERIFY(!wait.settled(400)); // still loading
        // Loaded: the window owes the frame that draws it, however quiet it has been since.
        QVERIFY(wait.shown(&desktop, false, 500));
        QVERIFY(!wait.settled(500));
        QVERIFY(!wait.settled(1000));
        wait.swapped(&desktop, 1010);
        QVERIFY(!wait.settled(1010 + RestyleWait::quietMs - 1));
        QVERIFY(wait.settled(1010 + RestyleWait::quietMs));
    }
    void settlesUnderAWindowThatKeepsDrawing() {
        RestyleWait wait;
        wait.start(0);
        wait.shown(&bar, false, 0);
        wait.swapped(&bar, 16);
        QVERIFY(!wait.settled(16));
        // The pointer moving over it: a frame every 16 ms, never quiet.
        qint64 now = 16;
        for (; now < 16 + RestyleWait::settleMs; now += 16) {
            QVERIFY(!wait.settled(now));
            wait.swapped(&bar, now);
        }
        QVERIFY(wait.settled(now));
    }
    void aHiddenWindowHoldsNothingUp() {
        RestyleWait wait;
        wait.start(0);
        wait.shown(&bar, false, 0);
        wait.shown(&desktop, false, 0);
        wait.swapped(&bar, 10);
        wait.hidden(&desktop); // never drew, and will not
        QVERIFY(wait.settled(10 + RestyleWait::quietMs));
        // Shown again, it is new and owes a frame.
        QVERIFY(wait.shown(&desktop, false, 100));
        QVERIFY(!wait.settled(1000));
    }
    void startsAfresh() {
        RestyleWait wait;
        wait.start(0);
        wait.shown(&bar, false, 0);
        wait.swapped(&bar, 10);
        QVERIFY(wait.settled(100));
        wait.start(200);
        QVERIFY(wait.shown(&bar, false, 200)); // asked again
        QVERIFY(!wait.settled(400));
    }
};

QTEST_GUILESS_MAIN(RestyleTest)
#include "restyle_test.moc"
