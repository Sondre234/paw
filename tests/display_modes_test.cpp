// SPDX-License-Identifier: GPL-3.0-or-later
// The display mode popup's model: the compositor's lines and the choice a click takes.
#include "display_modes.hpp"
#include <QSignalSpy>
#include <QTest>

class DisplayModesTest : public QObject {
    Q_OBJECT
  private Q_SLOTS:
    void readsTheCompositorsLines() {
        DisplayModes modes;
        QSignalSpy changed(&modes, &DisplayModes::changed);
        QVERIFY(!modes.handle("osd DP-1 40 Volume"));
        QVERIFY(modes.handle("display-mode DP-1 duplicate extend internal,duplicate,extend,external"));
        QVERIFY(modes.active());
        QCOMPARE(modes.output(), QString("DP-1"));
        QCOMPARE(modes.shown(), QString("duplicate"));
        QCOMPARE(modes.current(), QString("extend"));
        QCOMPARE(modes.choices(),
                 QStringList({"internal", "duplicate", "extend", "external"}));
        QVERIFY(modes.handle("display-mode-close"));
        QVERIFY(!modes.active());
        QCOMPARE(changed.count(), 2);
        // A line it cannot read is its own, but changes nothing.
        QVERIFY(modes.handle("display-mode DP-1 sideways extend extend"));
        QVERIFY(modes.handle("display-mode DP-1"));
        QVERIFY(!modes.active());
        QCOMPARE(changed.count(), 2);
    }
    void aClickAsksForTheChoice() {
        DisplayModes modes;
        QSignalSpy requests(&modes, &DisplayModes::request);
        modes.handle("display-mode HDMI-A-1 extend extend extend");
        modes.choose("duplicate"); // not offered with one monitor
        modes.choose("extend");
        QCOMPARE(requests.count(), 1);
        QCOMPARE(requests.first().first().toString(), QString("display_mode extend"));
    }
};

QTEST_GUILESS_MAIN(DisplayModesTest)
#include "display_modes_test.moc"
