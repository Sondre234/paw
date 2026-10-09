// SPDX-License-Identifier: GPL-3.0-or-later
// The display settings window's model, against a stand-in for the compositor's control socket:
// reading the monitors, changing them with the arrangement following, the request Apply sends,
// a trial kept, taken back and refused, and closing during a trial.
#include "display_settings.hpp"
#include <QSignalSpy>
#include <QTest>

namespace {
// Three monitors as `get monitors` gives them: a 1440p one on the left with HDR possible, the
// laptop's panel at a scale of 1.5 to its right, and a third off.
const QByteArray monitors =
    "ok\n"
    "DP-3\tDell Inc. DELL U2720Q "
    "4KX\t0\tconfig\t1\ton\t-\t-1920\t0\t2560x1440@143.912\t1\t0\toff\t8\t8\t"
    "off\tsdr\t-\t1\t2560x1440@59.951*,2560x1440@143.912,1920x1080@60.000,1920x1080@143.912\n"
    "eDP-1\tBOE 0x0BCA "
    "\t1\tdefault\t1\ton\t-\t640\t0\t2880x1800@120.000\t1.5\t0\t-\t8\t8\toff\tsdr\t"
    "the monitor does not offer BT.2020 with PQ\t0\t2880x1800@120.000*,2880x1800@60.000\n"
    "HDMI-A-1\t  \t0\twindow\t0\toff\t-\t0\t0\t1920x1080@60.000\t1\t0\t-\t8\t8\toff\tsdr\t"
    "the monitor does not offer BT.2020 with PQ\t0\t1920x1080@60.000*\n";

QVariantMap monitor(const DisplaySettings &settings, const QString &name) {
    for (const auto &entry : settings.monitors())
        if (entry.toMap()["name"] == name)
            return entry.toMap();
    return {};
}
QPoint at(const DisplaySettings &settings, const QString &name) {
    const auto m = monitor(settings, name);
    return {m["x"].toInt(), m["y"].toInt()};
}
} // namespace

class DisplaySettingsTest : public QObject {
    Q_OBJECT
    // What the stand-in compositor was asked, and what it answers next ("" for the usual).
    QStringList requests;
    QByteArray answer;
    std::unique_ptr<DisplaySettings> settings;

  private Q_SLOTS:
    void init() {
        requests.clear();
        answer.clear();
        settings = std::make_unique<DisplaySettings>(
            [this](const QByteArray &line, std::function<void(const QByteArray &)> done) {
                requests << QString::fromUtf8(line).trimmed();
                QByteArray reply = !answer.isEmpty()          ? answer
                                   : line == "get monitors\n" ? monitors
                                   : line.startsWith("monitors apply") || line == "monitors reset\n"
                                       ? QByteArray("ok\n15000\n")
                                       : QByteArray("ok\n");
                if (!line.startsWith("get "))
                    answer.clear();
                done(reply);
            });
    }
    void reads() {
        QVERIFY(!settings->load("error: no\n"));
        settings->show("eDP-1");
        QCOMPARE(requests, QStringList{"get monitors"});
        QVERIFY(settings->open());
        QCOMPARE(settings->monitors().size(), 3);
        // The window opened on eDP-1 selects it; the arrangement starts at 0, 0.
        QCOMPARE(settings->selected(), QString("eDP-1"));
        QCOMPARE(at(*settings, "DP-3"), QPoint(0, 0));
        QCOMPARE(at(*settings, "eDP-1"), QPoint(2560, 0));
        const auto panel = monitor(*settings, "eDP-1");
        QCOMPARE(panel["title"], QVariant("Built-in display"));
        QCOMPARE(panel["logicalWidth"], QVariant(1920));
        QCOMPARE(panel["vrrSupported"], QVariant(false));
        QCOMPARE(panel["hdrPossible"], QVariant(false));
        QCOMPARE(panel["hdrWhy"], QVariant("the monitor does not offer BT.2020 with PQ"));
        const auto dell = monitor(*settings, "DP-3");
        QCOMPARE(dell["title"], QVariant("Dell Inc. DELL U2720Q 4KX"));
        QCOMPARE(dell["primary"], QVariant(true));
        QCOMPARE(dell["hdrPossible"], QVariant(true));
        QCOMPARE(dell["refresh"], QVariant(143912));
        // Each resolution once, largest first, the recommended one said; the rates at the one it
        // has, fastest first.
        const auto resolutions = dell["resolutions"].toList();
        QCOMPARE(resolutions.size(), 2);
        QCOMPARE(resolutions[0].toMap()["label"], QVariant("2560 × 1440 (recommended)"));
        QCOMPARE(resolutions[1].toMap()["label"], QVariant("1920 × 1080"));
        const auto rates = dell["rates"].toList();
        QCOMPARE(rates.size(), 2);
        QCOMPARE(rates[0].toMap()["label"], QVariant("143.91 Hz"));
        QCOMPARE(rates[1].toMap()["label"], QVariant("59.95 Hz"));
        // HDMI-A-1 is off, out of the arrangement; it has a monitor's settings kept from the
        // window, which Reset to configuration takes back.
        QCOMPARE(monitor(*settings, "HDMI-A-1")["inLayout"], QVariant(false));
        QCOMPARE(monitor(*settings, "HDMI-A-1")["title"], QVariant("HDMI-A-1"));
        QVERIFY(settings->kept());
        QCOMPARE(settings->layoutCount(), 2);
        QCOMPARE(settings->extent(), QSize(4480, 1440));
        QVERIFY(!settings->changed());
    }
    void changes() {
        settings->show("DP-3");
        // A scale makes eDP-1 smaller: it stays beside DP-3, the arrangement starting at 0, 0.
        settings->setScale("eDP-1", 2);
        QCOMPARE(monitor(*settings, "eDP-1")["logicalWidth"], QVariant(1440));
        QCOMPARE(at(*settings, "eDP-1"), QPoint(2560, 0));
        QVERIFY(settings->changed());
        // DP-3 gets a smaller resolution, at its rate there; eDP-1 follows it.
        settings->setResolution("DP-3", 1920, 1080);
        QCOMPARE(monitor(*settings, "DP-3")["refresh"], QVariant(143912));
        QCOMPARE(at(*settings, "eDP-1"), QPoint(1920, 0));
        settings->setResolution("DP-3", 1366, 768); // not offered
        QCOMPARE(monitor(*settings, "DP-3")["width"], QVariant(1920));
        settings->setRefresh("DP-3", 60000);
        QCOMPARE(monitor(*settings, "DP-3")["refresh"], QVariant(60000));
        // A quarter turn stands DP-3 up.
        settings->setTransform("DP-3", 1);
        QCOMPARE(monitor(*settings, "DP-3")["logicalWidth"], QVariant(1080));
        QCOMPARE(at(*settings, "eDP-1"), QPoint(1080, 0));
        // HDMI-A-1 on goes right of the others; off again, it leaves.
        settings->setEnabled("HDMI-A-1", true);
        QCOMPARE(at(*settings, "HDMI-A-1"), QPoint(2520, 0));
        QCOMPARE(settings->layoutCount(), 3);
        // It mirrors eDP-1, out of the arrangement; DP-3 can then mirror none of the others but
        // eDP-1.
        settings->setMirror("HDMI-A-1", "eDP-1");
        QCOMPARE(monitor(*settings, "HDMI-A-1")["inLayout"], QVariant(false));
        QCOMPARE(monitor(*settings, "eDP-1")["mirroredBy"], QVariant(QStringList{"HDMI-A-1"}));
        settings->setMirror("DP-3", "HDMI-A-1");
        QVERIFY(monitor(*settings, "DP-3")["mirror"].toString().isEmpty());
        // eDP-1 mirroring DP-3 takes its mirror along: HDMI-A-1 shows DP-3 too.
        settings->setMirror("eDP-1", "DP-3");
        QCOMPARE(monitor(*settings, "HDMI-A-1")["mirror"], QVariant("DP-3"));
        QCOMPARE(settings->layoutCount(), 1);
        // The last monitor showing the desktop stays on, and mirrors none.
        settings->setEnabled("DP-3", false);
        settings->setMirror("DP-3", "eDP-1");
        QCOMPARE(monitor(*settings, "DP-3")["inLayout"], QVariant(true));
        // eDP-1 back to its own desktop, right of DP-3; HDMI-A-1 turned off.
        settings->setMirror("eDP-1", {});
        QCOMPARE(at(*settings, "eDP-1"), QPoint(1080, 0));
        settings->setEnabled("HDMI-A-1", false);
        // The primary monitor, adaptive sync where there is any, 10 bits, and HDR where it can be
        // had.
        settings->setPrimary("eDP-1");
        QCOMPARE(monitor(*settings, "eDP-1")["primary"], QVariant(true));
        QCOMPARE(monitor(*settings, "DP-3")["primary"], QVariant(false));
        settings->setVrr("DP-3", true);
        settings->setVrr("eDP-1", true); // none
        settings->setBitDepth("DP-3", 10);
        settings->setHdr("DP-3", true);
        settings->setHdr("eDP-1", true); // cannot be had
        QCOMPARE(monitor(*settings, "eDP-1")["hdr"], QVariant(false));
        QCOMPARE(
            QString::fromUtf8(settings->applyRequest()),
            QString(
                "monitors apply "
                "DP-3 enabled=on mode=1920x1080@60.000 scale=1 transform=1 position=0,0 vrr=on "
                "mirror=- bit_depth=10 hdr=on primary=off "
                "eDP-1 enabled=on mode=2880x1800@120.000 scale=2 transform=0 position=1080,0 "
                "mirror=- bit_depth=8 hdr=off primary=on "
                "HDMI-A-1 enabled=off mode=1920x1080@60.000 scale=1 transform=0 position=2520,0 "
                "mirror=DP-3 bit_depth=8 hdr=off primary=off\n"));
        // Disabling the primary monitor makes another one primary.
        settings->setEnabled("HDMI-A-1", true); // mirrors DP-3 again
        settings->setEnabled("eDP-1", false);
        QCOMPARE(monitor(*settings, "DP-3")["primary"], QVariant(true));
        // Reloading drops what was not applied.
        settings->reload();
        QVERIFY(!settings->changed());
    }
    void drags() {
        settings->show("DP-3");
        // Dragged below DP-3 and dropped a little off, eDP-1 snaps under it, lining up with its
        // left edge within the threshold; DP-3 stays where it was.
        QCOMPARE(settings->snapped("eDP-1", 30, 1500, 50), QPoint(0, 1440));
        settings->place("eDP-1", 30, 1500, 50);
        QCOMPARE(at(*settings, "eDP-1"), QPoint(0, 1440));
        QCOMPARE(at(*settings, "DP-3"), QPoint(0, 0));
        QCOMPARE(settings->extent(), QSize(2560, 1440 + 1200));
        // Dropped to its left, the arrangement moves to start at 0, 0.
        settings->place("eDP-1", -2000, 100, 0);
        QCOMPARE(at(*settings, "eDP-1"), QPoint(0, 100));
        QCOMPARE(at(*settings, "DP-3"), QPoint(1920, 0));
        QCOMPARE(settings->selected(), QString("eDP-1"));
        QVERIFY(settings->changed());
    }
    void trial() {
        settings->show("DP-3");
        QSignalSpy trialSpy(settings.get(), &DisplaySettings::trialChanged);
        settings->setScale("DP-3", 1.25);
        requests.clear();
        settings->apply();
        QCOMPARE(requests.size(), 2);
        QVERIFY(requests[0].startsWith(
            "monitors apply DP-3 enabled=on mode=2560x1440@143.912 scale=1.25 "));
        QCOMPARE(requests[1], QString("get monitors"));
        QVERIFY(settings->trial());
        QCOMPARE(settings->secondsLeft(), 15);
        // Applying again, or resetting, waits for the trial to end.
        requests.clear();
        settings->apply();
        settings->reset();
        QVERIFY(requests.isEmpty());
        // Kept: the trial is over, the monitors read anew.
        settings->keep();
        QCOMPARE(requests, (QStringList{"monitors keep", "get monitors"}));
        QVERIFY(!settings->trial());
        QCOMPARE(settings->message(), QString());
        // The compositor saying so too changes nothing more.
        requests.clear();
        QVERIFY(settings->handle("monitors-kept"));
        QVERIFY(requests.isEmpty());
        // Taken back by asking: nothing said of it.
        settings->apply();
        settings->revert();
        QVERIFY(!settings->trial());
        QVERIFY(requests.contains("monitors revert"));
        QCOMPARE(settings->message(), QString());
        // Run out: said, and the monitors read anew.
        settings->apply();
        requests.clear();
        QVERIFY(settings->handle("monitors-reverted timeout"));
        QVERIFY(!settings->trial());
        QCOMPARE(requests, QStringList{"get monitors"});
        QCOMPARE(settings->message(),
                 QString("The settings were not kept; the previous ones are back."));
        // A trial a script started shows too, and its end.
        QVERIFY(settings->handle("monitors-trial 3000"));
        QVERIFY(settings->trial());
        QCOMPARE(settings->secondsLeft(), 3);
        QVERIFY(settings->handle("monitors-reverted refused eDP-1"));
        QCOMPARE(settings->message(),
                 QString("eDP-1 did not take its settings; the previous ones are back."));
        QVERIFY(!settings->handle("monitors-unknown"));
        // A refusal says why, and nothing is on trial.
        answer = "error: DP-3 refused these settings; nothing changed\n";
        settings->apply();
        QVERIFY(!settings->trial());
        QCOMPARE(settings->message(), QString("DP-3 refused these settings; nothing changed."));
        // Kept, but the file cannot be written: said.
        settings->apply();
        answer = "error: cannot write /state/paw/outputs: Read-only file system\n";
        settings->keep();
        QVERIFY(!settings->trial());
        QCOMPARE(
            settings->message(),
            QString(
                "Kept for now, but cannot write /state/paw/outputs: Read-only file system."));
        // Reset to configuration puts the configuration's settings on trial.
        requests.clear();
        settings->reset();
        QCOMPARE(requests, (QStringList{"monitors reset", "get monitors"}));
        QVERIFY(settings->trial());
        QVERIFY(trialSpy.count() > 0);
    }
    void closing() {
        // "display-settings OUTPUT" opens it there; closing during a trial takes it back.
        QVERIFY(settings->handle("display-settings eDP-1"));
        QVERIFY(settings->open());
        QCOMPARE(settings->output(), QString("eDP-1"));
        settings->apply();
        requests.clear();
        settings->close();
        QVERIFY(!settings->open());
        QCOMPARE(requests, QStringList{"monitors revert"});
        QVERIFY(!settings->trial());
        // Closing without one asks nothing.
        settings->show("DP-3");
        requests.clear();
        settings->close();
        QVERIFY(requests.isEmpty());
    }
};

QTEST_GUILESS_MAIN(DisplaySettingsTest)
#include "display_settings_test.moc"
