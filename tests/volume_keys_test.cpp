// SPDX-License-Identifier: GPL-3.0-or-later
// The volume, microphone and brightness keys as the shell carries out what the compositor passes
// on: on a sound server that notes what is asked of it and a backlight in a made-up sysfs, shown
// on the on-screen display; and through wpctl, a stand-in on PATH, while the sound server is out
// of reach.
#include "audio.hpp"
#include "backlight.hpp"
#include "osd.hpp"
#include "volume_keys.hpp"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>

namespace {
// A sound server that notes what is asked of it, as "volume speakers 55".
class TestAudio : public Audio {
  public:
    QStringList requests;

  protected:
    void sendVolume(const QString &output, int percent) override {
        requests << QString("volume %1 %2").arg(output).arg(percent);
    }
    void sendMute(const QString &output, bool muted) override {
        requests << QString("mute %1 %2").arg(output).arg(muted);
    }
    void sendOutput(const QString &, const std::vector<uint32_t> &) override {}
    void sendStreamVolume(uint32_t, int) override {}
    void sendStreamMute(uint32_t, bool) override {}
    void sendInputMute(const QString &input, bool muted) override {
        requests << QString("input-mute %1 %2").arg(input).arg(muted);
    }
};

// Speakers at `volume` and, unless `microphone` is false, a microphone as the default input.
Audio::State sound(int volume, bool muted = false, bool microphone = true) {
    Audio::State state{"speakers", {{"speakers", "Speakers", volume, muted}}, {}};
    if (microphone) {
        state.input = "mic";
        state.inputs = {{"mic", "Microphone", 80, false}};
    }
    return state;
}

void write(const QString &path, const QString &text) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "cannot write");
    file.write(text.toUtf8());
}

QString read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
}

QString here() { return "DP-1"; }
} // namespace

class VolumeKeysTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir_;

    // What the display shows, as "DP-1 Volume 55 volume", or "" while hidden.
    static QString shown(const Osd &osd) {
        return osd.active() ? QString("%1 %2 %3 %4")
                                  .arg(osd.output(), osd.text())
                                  .arg(osd.percent())
                                  .arg(osd.kind())
                            : QString();
    }

  private Q_SLOTS:
    void initTestCase() { qunsetenv("PAW_LOGIN1_BUS"); }

    void volume() {
        TestAudio audio;
        audio.update(sound(50));
        Backlight backlight(dir_.filePath("no-sysfs"));
        Osd osd;
        VolumeKeys keys(audio, backlight, osd, here);
        QVERIFY(keys.handle("volume up 5"));
        QCOMPARE(audio.volume(), 55);
        QCOMPARE(audio.requests, QStringList{"volume speakers 55"});
        QCOMPARE(shown(osd), QString("DP-1 Volume 55 volume"));
        // At the top it stays there, and the display still shows it.
        QVERIFY(keys.handle("volume up 50"));
        osd.hide();
        QVERIFY(keys.handle("volume up 5"));
        QCOMPARE(audio.requests, (QStringList{"volume speakers 55", "volume speakers 100"}));
        QCOMPARE(shown(osd), QString("DP-1 Volume 100 volume"));
        QVERIFY(keys.handle("volume down 100"));
        QCOMPARE(audio.volume(), 0);
        // Mute toggles; a step while muted unmutes too.
        audio.requests.clear();
        QVERIFY(keys.handle("volume up 20"));
        QVERIFY(keys.handle("volume mute"));
        QVERIFY(audio.muted());
        QCOMPARE(shown(osd), QString("DP-1 Muted 0 muted"));
        QVERIFY(keys.handle("volume down 5"));
        QVERIFY(!audio.muted());
        QCOMPARE(audio.requests, (QStringList{"volume speakers 20", "mute speakers 1",
                                              "mute speakers 0", "volume speakers 15"}));
        QCOMPARE(shown(osd), QString("DP-1 Volume 15 volume"));
        QVERIFY(keys.handle("volume mute"));
        QVERIFY(keys.handle("volume mute"));
        QVERIFY(!audio.muted());
        QCOMPARE(shown(osd), QString("DP-1 Volume 15 volume"));
        // Without an output there is nothing to change or show.
        audio.update({});
        audio.requests.clear();
        osd.hide();
        QVERIFY(keys.handle("volume up 5"));
        QVERIFY(keys.handle("volume mute"));
        QVERIFY(audio.requests.isEmpty());
        QVERIFY(!osd.active());
    }

    void microphone() {
        TestAudio audio;
        audio.update(sound(50));
        Backlight backlight(dir_.filePath("no-sysfs"));
        Osd osd;
        VolumeKeys keys(audio, backlight, osd, here);
        QVERIFY(audio.hasInput() && !audio.inputMuted());
        QVERIFY(keys.handle("microphone mute"));
        QVERIFY(audio.inputMuted() && !audio.muted());
        QCOMPARE(shown(osd), QString("DP-1 Microphone muted -1 microphone-muted"));
        QVERIFY(keys.handle("microphone mute"));
        QVERIFY(!audio.inputMuted());
        QCOMPARE(shown(osd), QString("DP-1 Microphone on -1 microphone"));
        QCOMPARE(audio.requests, (QStringList{"input-mute mic 1", "input-mute mic 0"}));
        // Without one, the display says so.
        audio.update(sound(50, false, false));
        audio.requests.clear();
        QVERIFY(!audio.hasInput());
        QVERIFY(keys.handle("microphone mute"));
        QVERIFY(audio.requests.isEmpty());
        QCOMPARE(shown(osd), QString("DP-1 No microphone -1 microphone-muted"));
    }

    void brightness() {
        const auto root = dir_.filePath("sysfs");
        const auto level = root + "/class/backlight/panel/brightness";
        write(root + "/class/backlight/panel/max_brightness", "1000\n");
        write(level, "500\n");
        TestAudio audio;
        Backlight backlight(root);
        Osd osd;
        VolumeKeys keys(audio, backlight, osd, here);
        QCOMPARE(backlight.percent(), 50);
        QVERIFY(keys.handle("brightness up 5"));
        QCOMPARE(read(level), QString("550"));
        QCOMPARE(shown(osd), QString("DP-1 Brightness 55 brightness"));
        QVERIFY(keys.handle("brightness up 100"));
        QCOMPARE(read(level), QString("1000"));
        // On the way down it stops at 1 %, short of dark.
        QVERIFY(keys.handle("brightness down 100"));
        QCOMPARE(read(level), QString("10"));
        QCOMPARE(shown(osd), QString("DP-1 Brightness 1 brightness"));
        QVERIFY(keys.handle("brightness down 5"));
        QCOMPARE(backlight.percent(), 1);
        // Without a backlight there is nothing to change.
        Backlight none(dir_.filePath("no-sysfs"));
        Osd quiet;
        VolumeKeys without(audio, none, quiet, here);
        QVERIFY(without.handle("brightness up 5"));
        QVERIFY(!quiet.active());
    }

    void displayTurnedOff() {
        TestAudio audio;
        audio.update(sound(50));
        const auto root = dir_.filePath("sysfs-off");
        write(root + "/class/backlight/panel/max_brightness", "100\n");
        write(root + "/class/backlight/panel/brightness", "40\n");
        Backlight backlight(root);
        Osd osd;
        paw::OsdConfig config;
        config.volume = false;
        config.brightness = false;
        osd.configure(config);
        VolumeKeys keys(audio, backlight, osd, here);
        for (const char *line :
             {"volume up 5", "volume mute", "microphone mute", "brightness up 5"})
            QVERIFY(keys.handle(line));
        QCOMPARE(audio.volume(), 55);
        QVERIFY(audio.muted() && audio.inputMuted());
        QCOMPARE(backlight.percent(), 45);
        QVERIFY(!osd.active());
    }

    void otherLines() {
        TestAudio audio;
        audio.update(sound(50));
        Backlight backlight(dir_.filePath("no-sysfs"));
        Osd osd;
        VolumeKeys keys(audio, backlight, osd, here);
        for (const char *line : {"volume sideways 5", "volume up", "volume up 0", "volume up 101",
                                 "volume up 5 5", "volume up x", "volume mute now",
                                 "microphone up 5", "brightness mute", "tiling on", ""})
            QVERIFY2(!keys.handle(line), line);
        QVERIFY(audio.requests.isEmpty());
        QVERIFY(!osd.active());
    }

    // While the sound server is out of reach, wpctl changes the sound instead.
    void wpctlWithoutSoundServer() {
        const auto calls = dir_.filePath("calls");
        write(dir_.filePath("bin/wpctl"), "#!/bin/sh\necho \"$*\" >> '" + calls + "'\n");
        QFile::setPermissions(dir_.filePath("bin/wpctl"), QFile::ReadOwner | QFile::ExeOwner);
        qputenv("PATH", (dir_.filePath("bin") + ":" + qgetenv("PATH")).toUtf8());
        TestAudio audio;
        QVERIFY(!audio.available());
        Backlight backlight(dir_.filePath("no-sysfs"));
        Osd osd;
        VolumeKeys keys(audio, backlight, osd, here);
        for (const char *line : {"volume up 5", "volume down 3", "volume mute", "microphone mute"})
            QVERIFY(keys.handle(line));
        QStringList expected{"set-mute @DEFAULT_AUDIO_SINK@ 0",
                             "set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ 5%+",
                             "set-mute @DEFAULT_AUDIO_SINK@ 0",
                             "set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ 3%-",
                             "set-mute @DEFAULT_AUDIO_SINK@ toggle",
                             "set-mute @DEFAULT_AUDIO_SOURCE@ toggle"};
        std::sort(expected.begin(), expected.end());
        auto called = [&] {
            auto lines = read(calls).split('\n', Qt::SkipEmptyParts);
            std::sort(lines.begin(), lines.end());
            return lines;
        };
        QTRY_COMPARE_WITH_TIMEOUT(called(), expected, 10000);
        QVERIFY(audio.requests.isEmpty());
        QVERIFY(!osd.active());
    }
};

QTEST_GUILESS_MAIN(VolumeKeysTest)
#include "volume_keys_test.moc"
