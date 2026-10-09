// SPDX-License-Identifier: GPL-3.0-or-later
#include "volume_keys.hpp"
#include "audio.hpp"
#include "backlight.hpp"
#include "osd.hpp"
#include <QProcess>
#include <algorithm>
#include <cstdlib>
#include <iostream>

VolumeKeys::VolumeKeys(Audio &audio, Backlight &backlight, Osd &osd, std::function<QString()> where)
    : audio_(audio), backlight_(backlight), osd_(osd), where_(std::move(where)) {}

bool VolumeKeys::handle(const QString &line) {
    const auto words = line.split(' ');
    const auto what = words.value(0), how = words.value(1);
    if (how == "mute" && words.size() == 2) {
        if (what == "volume")
            toggleMute();
        else if (what == "microphone")
            toggleMicrophone();
        else
            return false;
        return true;
    }
    bool ok = false;
    int step = words.value(2).toInt(&ok);
    if (!ok || words.size() != 3 || step < 1 || step > 100 || (how != "up" && how != "down"))
        return false;
    if (how == "down")
        step = -step;
    if (what == "volume")
        changeVolume(step);
    else if (what == "brightness")
        changeBrightness(step);
    else
        return false;
    return true;
}

void VolumeKeys::changeVolume(int step) {
    if (!audio_.available()) {
        wpctl({"set-mute", "@DEFAULT_AUDIO_SINK@", "0"});
        wpctl({"set-volume", "-l", "1.0", "@DEFAULT_AUDIO_SINK@",
               QString::number(std::abs(step)) + (step > 0 ? "%+" : "%-")});
        return;
    }
    if (audio_.outputs().isEmpty())
        return;
    if (audio_.muted())
        audio_.toggleMute();
    audio_.changeVolume(step);
    showVolume();
}

void VolumeKeys::toggleMute() {
    if (!audio_.available()) {
        wpctl({"set-mute", "@DEFAULT_AUDIO_SINK@", "toggle"});
        return;
    }
    if (audio_.outputs().isEmpty())
        return;
    audio_.toggleMute();
    showVolume();
}

// Shown as the panel's own changes are (ShellController::showVolume), and also where nothing
// changed, at 0 or 100 %.
void VolumeKeys::showVolume() {
    if (!osd_.config().volume)
        return;
    const bool muted = audio_.muted();
    osd_.show(where_(), muted ? "Muted" : "Volume", muted ? 0 : audio_.volume(),
              muted ? "muted" : "volume");
}

void VolumeKeys::toggleMicrophone() {
    if (!audio_.available()) {
        wpctl({"set-mute", "@DEFAULT_AUDIO_SOURCE@", "toggle"});
        return;
    }
    const bool show = osd_.config().volume;
    if (!audio_.hasInput()) {
        if (show)
            osd_.show(where_(), "No microphone", -1, "microphone-muted");
        return;
    }
    audio_.toggleInputMute();
    const bool muted = audio_.inputMuted();
    if (show)
        osd_.show(where_(), muted ? "Microphone muted" : "Microphone on", -1,
                  muted ? "microphone-muted" : "microphone");
}

void VolumeKeys::changeBrightness(int step) {
    if (!backlight_.present())
        return;
    // A backlight at 0 is off on some screens: the keys stop at 1 % on the way down, unless it
    // is lower already.
    const int current = backlight_.percent();
    const int next = std::clamp(current + step, std::min(current, 1), 100);
    backlight_.setPercent(next);
    if (osd_.config().brightness)
        osd_.show(where_(), "Brightness", next, "brightness");
}

void VolumeKeys::wpctl(const QStringList &arguments) {
    if (!QProcess::startDetached("wpctl", arguments))
        std::cerr << "The sound server is out of reach, and wpctl cannot start: the volume keys "
                     "do nothing\n";
}
