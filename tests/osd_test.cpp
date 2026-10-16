// SPDX-License-Identifier: GPL-3.0-or-later
// The on-screen display: its own timing, and what feeds it (the sound server's volume and a
// backlight); the compositor's notices, which it says while no notification card shows; and the
// caption `paw tour` narrates itself with, in its place.
#include "backlight.hpp"
#include "caption.hpp"
#include "controller.hpp"
#include "osd.hpp"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {
void write(const QString &path, const QString &text) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "cannot write");
    file.write(text.toUtf8());
}
Audio::State speakers(int volume, bool muted = false, const QString &output = "speakers") {
    return {output,
            {{"speakers", "Speakers", output == "speakers" ? volume : 30, output == "speakers" ? muted : false},
             {"headset", "Headset", output == "headset" ? volume : 30, output == "headset" ? muted : false}},
            {}};
}
} // namespace

class OsdTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir_;
    QString config(const QString &body) {
        write(dir_.filePath("init.lua"), "return {" + body + "}");
        return dir_.filePath("init.lua");
    }

  private Q_SLOTS:
    void showsThenHides() {
        Osd osd;
        paw::OsdConfig c;
        c.timeout = 200;
        osd.configure(c);
        QVERIFY(!osd.active());
        QSignalSpy changed(&osd, &Osd::changed);
        osd.show("DP-1", "Volume", 40, "volume");
        QVERIFY(osd.active());
        QCOMPARE(osd.output(), QString("DP-1"));
        QCOMPARE(osd.text(), QString("Volume"));
        QCOMPARE(osd.percent(), 40);
        QCOMPARE(osd.kind(), QString("volume"));
        QTRY_VERIFY_WITH_TIMEOUT(!osd.active(), 2000);
        QCOMPARE(changed.count(), 2);
    }
    void showingAgainRestartsTheTime() {
        Osd osd;
        paw::OsdConfig c;
        c.timeout = 300;
        osd.configure(c);
        osd.show("A", "one", -1);
        QTest::qWait(200);
        osd.show("B", "two", 10);
        QTest::qWait(200);
        QVERIFY(osd.active()); // 400 ms after the first, 200 after the second
        QCOMPARE(osd.output(), QString("B"));
        QCOMPARE(osd.text(), QString("two"));
        QTRY_VERIFY_WITH_TIMEOUT(!osd.active(), 2000);
    }
    void levelsAreClamped() {
        Osd osd;
        osd.show("A", "x", 250);
        QCOMPARE(osd.percent(), 100);
        osd.show("A", "x", -5);
        QCOMPARE(osd.percent(), -1);
        osd.show("A", QString(1000, 'x'), 5);
        QCOMPARE(osd.text().size(), 200);
    }
    void disabledStaysHidden() {
        Osd osd;
        paw::OsdConfig c;
        c.enabled = false;
        osd.configure(c);
        osd.show("A", "x", 1);
        QVERIFY(!osd.active());
        c.enabled = true;
        osd.configure(c);
        osd.show("A", "x", 1);
        QVERIFY(osd.active());
        // Switching it off hides what is showing.
        c.enabled = false;
        osd.configure(c);
        QVERIFY(!osd.active());
    }
    void volumeChangesShowIt() {
        ShellController controller(config("").toStdString());
        Audio *audio = controller.audio();
        Osd *osd = controller.osd();
        // The first state only says where the volume is.
        audio->update(speakers(50));
        QVERIFY(!osd->active());
        audio->update(speakers(60));
        QVERIFY(osd->active());
        QCOMPARE(osd->percent(), 60);
        QCOMPARE(osd->kind(), QString("volume"));
        QCOMPARE(osd->text(), QString("Volume"));
        QCOMPARE(osd->output(), controller.overlayOutput());
        osd->hide();
        // The same state again (the server confirming a change) says nothing.
        audio->update(speakers(60));
        QVERIFY(!osd->active());
        // Muting shows "Muted" with an empty level.
        audio->update(speakers(60, true));
        QVERIFY(osd->active());
        QCOMPARE(osd->kind(), QString("muted"));
        QCOMPARE(osd->text(), QString("Muted"));
        QCOMPARE(osd->percent(), 0);
        osd->hide();
        audio->update(speakers(60, false));
        QVERIFY(osd->active());
        QCOMPARE(osd->kind(), QString("volume"));
        osd->hide();
        // Changing the panel's own volume control shows it too, and steps build on each other.
        audio->changeVolume(5);
        QVERIFY(osd->active());
        QCOMPARE(osd->percent(), 65);
        osd->hide();
        // Choosing another output is not a volume change.
        audio->update(speakers(20, false, "headset"));
        QVERIFY(!osd->active());
        audio->update(speakers(25, false, "headset"));
        QVERIFY(osd->active());
        QCOMPARE(osd->percent(), 25);
        osd->hide();
        // The sound server going away and back re-establishes the baseline quietly.
        audio->setUnavailable();
        audio->update(speakers(80));
        QVERIFY(!osd->active());
    }
    void volumeSettingSilencesIt() {
        ShellController controller(config("osd={volume=false}").toStdString());
        controller.audio()->update(speakers(50));
        controller.audio()->update(speakers(60));
        QVERIFY(!controller.osd()->active());
        ShellController off(config("osd={enabled=false}").toStdString());
        off.audio()->update(speakers(50));
        off.audio()->update(speakers(60));
        QVERIFY(!off.osd()->active());
    }
    void configReachesTheDisplay() {
        ShellController controller(config("osd={timeout=900,position='top'}").toStdString());
        QCOMPARE(controller.osd()->config().timeout, 900);
        QVERIFY(controller.osd()->top());
        write(dir_.filePath("init.lua"), "return {osd={timeout=400}}");
        controller.reload();
        QCOMPARE(controller.osd()->config().timeout, 400);
        QVERIFY(!controller.osd()->top());
    }
    void backlightLevel() {
        QTemporaryDir sys;
        write(sys.filePath("class/backlight/intel_backlight/max_brightness"), "96000\n");
        write(sys.filePath("class/backlight/intel_backlight/brightness"), "48000\n");
        Backlight light(sys.path());
        QVERIFY(light.present());
        QCOMPARE(light.percent(), 50);
        QVERIFY(!light.watching());
        QSignalSpy changed(&light, &Backlight::changed);
        // Nothing changed: nothing said.
        light.refresh();
        QCOMPARE(changed.count(), 0);
        write(sys.filePath("class/backlight/intel_backlight/brightness"), "9600\n");
        light.refresh();
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.at(0).at(0).toInt(), 10);
        // actual_brightness, when there is one, is what the hardware shows.
        write(sys.filePath("class/backlight/intel_backlight/actual_brightness"), "96000\n");
        light.refresh();
        QCOMPARE(light.percent(), 100);
        QCOMPARE(changed.count(), 2);
        // Out-of-range values are clamped.
        write(sys.filePath("class/backlight/intel_backlight/actual_brightness"), "999999\n");
        light.refresh();
        QCOMPARE(light.percent(), 100);
        QCOMPARE(changed.count(), 2);
    }
    void noBacklight() {
        QTemporaryDir sys;
        Backlight none(sys.path());
        QVERIFY(!none.present());
        QCOMPARE(none.percent(), -1);
        Backlight missing("/nonexistent/sysfs", true);
        QVERIFY(!missing.present());
        write(sys.filePath("class/backlight/broken/brightness"), "5\n"); // no max_brightness
        none.refresh();
        QVERIFY(!none.present());
        write(sys.filePath("class/backlight/broken/max_brightness"), "0\n");
        none.refresh();
        QVERIFY(!none.present());
    }
    void backlightAppearing() {
        QTemporaryDir sys;
        Backlight light(sys.path());
        QSignalSpy changed(&light, &Backlight::changed);
        write(sys.filePath("class/backlight/acpi_video0/max_brightness"), "10\n");
        write(sys.filePath("class/backlight/acpi_video0/brightness"), "3\n");
        light.refresh();
        QVERIFY(light.present());
        QCOMPARE(light.percent(), 30);
        // The first level found is only what is there, not a change.
        QCOMPARE(changed.count(), 0);
        write(sys.filePath("class/backlight/acpi_video0/brightness"), "4\n");
        light.refresh();
        QCOMPARE(changed.count(), 1);
    }
    // Set from Quick Settings: the level shows at once, and in a sysfs tree of a test's own the
    // brightness file is written; the kernel's report that follows brings no on-screen display.
    void backlightSet() {
        QTemporaryDir sys;
        write(sys.filePath("class/backlight/intel_backlight/max_brightness"), "96000\n");
        write(sys.filePath("class/backlight/intel_backlight/brightness"), "48000\n");
        Backlight light(sys.path());
        QCOMPARE(light.name(), QString("intel_backlight"));
        QSignalSpy level(&light, &Backlight::levelChanged);
        QSignalSpy changed(&light, &Backlight::changed);
        QSignalSpy failed(&light, &Backlight::failed);
        light.setPercent(25);
        QCOMPARE(light.percent(), 25);
        QCOMPARE(level.count(), 1);
        QFile file(sys.filePath("class/backlight/intel_backlight/brightness"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll().trimmed(), QByteArray("24000"));
        light.refresh();
        QCOMPARE(changed.count(), 0);
        light.setPercent(140);
        QCOMPARE(light.percent(), 100);
        QCOMPARE(failed.count(), 0);
        // Without a backlight there is nothing to set.
        QTemporaryDir empty;
        Backlight none(empty.path());
        QSignalSpy noneLevel(&none, &Backlight::levelChanged);
        none.setPercent(50);
        QCOMPARE(none.percent(), -1);
        QCOMPARE(noneLevel.count(), 0);
    }
    // The compositor's caption line: its output ("-" for the focused one), its time, a title, a
    // sentence and keys; a new one takes its place and its time starts anew.
    void captionLine() {
        Caption caption;
        QSignalSpy changed(&caption, &Caption::changed);
        QVERIFY(caption.showLine("DP-1 300 Tiling\tWindows split the screen.\tSuper + S", "X"));
        QVERIFY(caption.active());
        QCOMPARE(caption.output(), QString("DP-1"));
        QCOMPARE(caption.title(), QString("Tiling"));
        QCOMPARE(caption.body(), QString("Windows split the screen."));
        QCOMPARE(caption.keys(), QString("Super + S"));
        QTest::qWait(200);
        QVERIFY(caption.showLine("- 300 Welcome", "HDMI-A-1"));
        QCOMPARE(caption.output(), QString("HDMI-A-1"));
        QCOMPARE(caption.title(), QString("Welcome"));
        QCOMPARE(caption.body(), QString());
        QCOMPARE(caption.keys(), QString());
        QTest::qWait(200);
        QVERIFY(caption.active()); // 400 ms after the first, 200 after the second
        QTRY_VERIFY_WITH_TIMEOUT(!caption.active(), 2000);
        QCOMPARE(changed.count(), 3);
        // What is not a caption shows nothing.
        for (const char *line : {"", "DP-1", "DP-1 0 Title", "DP-1 x Title", "DP-1 300 ",
                                 "DP-1 300 \tBody"})
            QVERIFY2(!caption.showLine(line, "X"), line);
        QVERIFY(!caption.active());
        caption.show("A", "Title", "Body", "Keys", 60000);
        caption.hide();
        QVERIFY(!caption.active());
    }
    void noticeIsANotification() {
        ShellController controller(config("").toStdString());
        controller.notice("Shortcuts go to VM", "Super + Shift + Escape gives them back");
        const auto *cards = controller.notifications()->cards();
        QCOMPARE(cards->count(), 1);
        QCOMPARE(cards->items()[0].app, QString("paw"));
        QCOMPARE(cards->items()[0].summary, QString("Shortcuts go to VM"));
        QCOMPARE(cards->items()[0].body, QString("Super + Shift + Escape gives them back"));
        QVERIFY(!controller.osd()->active());
        // While no card shows, the on-screen display says it too; the history keeps it either
        // way, the body as text, not markup.
        controller.notifications()->setDnd(true);
        controller.notice("Shortcuts go to <b>", "Ctrl & Q gives them back");
        QVERIFY(controller.osd()->active());
        QCOMPARE(controller.osd()->text(), QString("Shortcuts go to <b>"));
        QCOMPARE(controller.osd()->percent(), -1);
        const auto *history = controller.notifications()->history();
        QCOMPARE(history->count(), 2);
        QCOMPARE(history->items()[0].body, QString("Ctrl &amp; Q gives them back"));
    }
    void colorPickedShowsItsSwatch() {
        // A colour picked shows on the on-screen display, with do not disturb on too, and adds
        // no notification; what is not #rrggbb shows nothing.
        ShellController controller(config("").toStdString());
        controller.notifications()->setDnd(true);
        for (const char *line : {"", "417bc4", "#417BC4", "#417bc", "#417bc4 HEADLESS-1", "#zzzzzz"})
            controller.colorPicked(line);
        QVERIFY(!controller.osd()->active());
        controller.colorPicked("#417bc4");
        QVERIFY(controller.osd()->active());
        QCOMPARE(controller.osd()->kind(), QString("color"));
        QCOMPARE(controller.osd()->text(), QString("#417bc4"));
        QCOMPARE(controller.osd()->percent(), -1);
        QCOMPARE(controller.notifications()->history()->count(), 0);
        // With the display off, a notification says it instead.
        ShellController quiet(config("osd = { enabled = false }").toStdString());
        quiet.colorPicked("#f5bde6");
        QVERIFY(!quiet.osd()->active());
        const auto *cards = quiet.notifications()->cards();
        QCOMPARE(cards->count(), 1);
        QCOMPARE(cards->items()[0].summary, QString("Colour copied"));
        QCOMPARE(cards->items()[0].body, QString("#f5bde6"));
    }
    void uevents() {
        QVERIFY(Backlight::relevantUevent(QByteArray("ACTION=change\0SUBSYSTEM=backlight\0", 34)));
        QVERIFY(!Backlight::relevantUevent(QByteArray("ACTION=change\0SUBSYSTEM=power_supply\0", 38)));
        QVERIFY(!Backlight::relevantUevent(QByteArray("SUBSYSTEM=backlightish\0", 23)));
    }
};
QTEST_MAIN(OsdTest)
#include "osd_test.moc"
