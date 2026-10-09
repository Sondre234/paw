// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
#include <QStringList>
#include <functional>

class Audio;
class Backlight;
class Osd;

// The keyboard's volume, microphone and brightness keys, as the compositor passes them on:
// "volume up|down PERCENT", "volume mute", "microphone mute" and "brightness up|down PERCENT".
// They change the sound server's default output (unmuting it as its volume changes, as Windows
// and KDE do) and default input, and the backlight, never quite down to dark; and they show on
// the on-screen display on the output `where` names, also when the level is at its end already.
// While the sound server cannot be reached, wpctl changes the sound instead.
class VolumeKeys {
  public:
    VolumeKeys(Audio &audio, Backlight &backlight, Osd &osd, std::function<QString()> where);
    // Carries out one line from the compositor; false when it is none of these.
    bool handle(const QString &line);

  private:
    Audio &audio_;
    Backlight &backlight_;
    Osd &osd_;
    std::function<QString()> where_;
    void changeVolume(int step);
    void toggleMute();
    void toggleMicrophone();
    void changeBrightness(int step);
    void showVolume();
    static void wpctl(const QStringList &arguments);
};
