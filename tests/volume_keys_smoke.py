# SPDX-License-Identifier: GPL-3.0-or-later
"""The volume, microphone and brightness actions: the compositor hands them to the shell (a
subscriber that sends "subscribe shell") as "volume up 5" and the like, which every subscriber
hears; while no shell listens it runs wpctl and brightnessctl instead, here stand-ins that write
down how they were called. The keys bound to them reach them too."""
from pathlib import Path
import os
import sys

import harness

compositor = str(Path(sys.argv[1]).resolve())

CONFIG = """return {
    xwayland = false,
    bindings = {
        { key = "XF86AudioRaiseVolume", action = "volume_up" },
        { key = "XF86AudioLowerVolume", action = "volume_down", amount = 2 },
        { key = "XF86AudioMute", action = "volume_mute" },
        { key = "XF86AudioMicMute", action = "mic_mute" },
        { key = "XF86MonBrightnessUp", action = "brightness_up" },
        { key = "XF86MonBrightnessDown", action = "brightness_down" },
    },
}"""
# evdev's codes of those keys
KEYS = {"XF86AudioRaiseVolume": 115, "XF86AudioLowerVolume": 114, "XF86AudioMute": 113,
        "XF86AudioMicMute": 248, "XF86MonBrightnessUp": 225, "XF86MonBrightnessDown": 224}

with harness.Compositor(compositor, CONFIG, start=False) as desktop:
    root, msg = desktop.root, desktop.msg
    calls = root / "calls"
    calls.touch()
    tools = root / "bin"
    tools.mkdir()
    for name in ("wpctl", "brightnessctl"):
        script = tools / name
        script.write_text(f'#!/bin/sh\necho "{name} $*" >> "{calls}"\n')
        script.chmod(0o755)
    desktop.env["PATH"] = f"{tools}:{os.environ.get('PATH', '/usr/bin:/bin')}"
    desktop.start()

    def called():
        return calls.read_text().splitlines()

    desktop.detail = lambda: f"called: {called()}"

    def key(name):
        for state in ("press", "release"):
            msg("headless_keyboard", "key", "keys", str(KEYS[name]), state)

    # Without a shell, wpctl changes the default output's volume (unmuting it, and no further
    # than 100 %) and mutes the output or the input; brightnessctl changes the backlight.
    msg("volume_up")
    msg("volume_down", "10")
    msg("volume_mute")
    msg("mic_mute")
    msg("brightness_up", "20")
    msg("brightness_down")
    expected = ["wpctl set-mute @DEFAULT_AUDIO_SINK@ 0",
                "wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ 5%+",
                "wpctl set-mute @DEFAULT_AUDIO_SINK@ 0",
                "wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ 10%-",
                "wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle",
                "wpctl set-mute @DEFAULT_AUDIO_SOURCE@ toggle",
                "brightnessctl --quiet --min-value=1 set 20%+",
                "brightnessctl --quiet --min-value=1 set 5%-"]
    desktop.wait_for(lambda: sorted(called()) == sorted(expected), "the stand-ins run")
    # The order of two programs started at once is not the order they write in.
    for words in (("volume_up", "0"), ("volume_up", "101"), ("brightness_down", "x"),
                  ("volume_down", "5", "5"), ("volume_mute", "5"), ("mic_mute", "on")):
        assert "takes" in msg(*words, ok=False), words

    # The keys bound to them run them as well.
    msg("headless_keyboard", "add", "keys")
    for name in KEYS:
        key(name)
    expected += ["wpctl set-mute @DEFAULT_AUDIO_SINK@ 0",
                 "wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ 5%+",
                 "wpctl set-mute @DEFAULT_AUDIO_SINK@ 0",
                 "wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ 2%-",
                 "wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle",
                 "wpctl set-mute @DEFAULT_AUDIO_SOURCE@ toggle",
                 "brightnessctl --quiet --min-value=1 set 5%+",
                 "brightnessctl --quiet --min-value=1 set 5%-"]
    desktop.wait_for(lambda: sorted(called()) == sorted(expected), "the keys ran them")

    # Another subscriber hears what was asked, and the stand-ins still run without a shell.
    other = desktop.subscribe()
    desktop.wait_for(lambda: other.lines("tiling "), "the other subscriber's first state")
    msg("volume_up", "3")
    expected += ["wpctl set-mute @DEFAULT_AUDIO_SINK@ 0",
                 "wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ 3%+"]
    desktop.wait_for(lambda: other.lines("volume ") == ["volume up 3"] and
                     sorted(called()) == sorted(expected), "heard, and wpctl run without a shell")
    count = len(called())

    # With the shell listening, the shell is told and nothing else runs.
    shell = desktop.subscribe(shell=True)
    desktop.wait_for(lambda: shell.lines("tiling "), "the shell's first state")
    msg("volume_down")
    msg("volume_mute")
    msg("mic_mute")
    msg("brightness_up", "15")
    key("XF86MonBrightnessDown")
    lines = ["volume down 5", "volume mute", "microphone mute", "brightness up 15",
             "brightness down 5"]
    desktop.wait_for(lambda: shell.lines("volume ", "microphone ", "brightness ") == lines,
                     "the shell told", detail=lambda: shell.lines("volume ", "microphone ",
                                                                  "brightness "))
    desktop.wait_for(lambda: other.lines("volume ", "microphone ", "brightness ")[1:] == lines,
                     "the other subscriber told too")
    desktop.stays(lambda: len(called()) == count, "no stand-in run while the shell listens")

    # Once the shell has gone, the stand-ins take over again.
    shell.close()
    msg("mic_mute")
    desktop.wait_for(lambda: called()[count:] == ["wpctl set-mute @DEFAULT_AUDIO_SOURCE@ toggle"],
                     "wpctl run once the shell is gone")
print("Volume, microphone and brightness actions, to the shell or wpctl and brightnessctl, passed")
