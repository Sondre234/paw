# SPDX-License-Identifier: GPL-3.0-or-later
"""The volume, microphone and brightness keys end to end, from a key on a headless compositor to
the shell: the shell subscribes as such, so the compositor hands the keys to it instead of running
wpctl itself; the shell sets a backlight in a made-up sysfs and shows the on-screen display, and
with its sound server out of reach runs wpctl. wpctl and brightnessctl are stand-ins that write
down who called them and how."""
from pathlib import Path
import os
import sys

import harness

compositor, shell = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    osd = { timeout = 300 },
    bindings = {
        { key = "XF86AudioRaiseVolume", action = "volume_up" },
        { key = "XF86AudioMicMute", action = "mic_mute" },
        { key = "XF86MonBrightnessDown", action = "brightness_down", amount = 10 },
    },
}"""
KEYS = {"XF86AudioRaiseVolume": 115, "XF86AudioMicMute": 248, "XF86MonBrightnessDown": 224}

with harness.Compositor(compositor, CONFIG, start=False) as desktop:
    root, env, msg = desktop.root, desktop.env, desktop.msg
    calls = root / "calls"
    calls.touch()
    tools = root / "bin"
    tools.mkdir()
    for name in ("wpctl", "brightnessctl"):
        script = tools / name
        script.write_text(f'#!/bin/sh\necho "${{CALLER:-compositor}} {name} $*" >> "{calls}"\n')
        script.chmod(0o755)
    backlight = root / "sys" / "class" / "backlight" / "fake"
    backlight.mkdir(parents=True)
    (backlight / "max_brightness").write_text("100\n")
    (backlight / "brightness").write_text("50\n")
    # No sound server for the shell to reach: it runs wpctl, as it does where it has none.
    env.update(PATH=f"{tools}:{os.environ.get('PATH', '/usr/bin:/bin')}",
               QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software",
               QT_FORCE_STDERR_LOGGING="1", XDG_DATA_HOME=str(root), XDG_DATA_DIRS=str(root),
               DBUS_SESSION_BUS_ADDRESS="disabled:", PAW_SYSFS=str(root / "sys"),
               PULSE_SERVER=f"unix:{root}/no-sound-server")
    env.pop("PAW_LOGIN1_BUS", None)
    shell_log = root / "shell.log"
    desktop.start()

    def called():
        return calls.read_text().splitlines()

    def level():
        # The fake file is rewritten, so a read can find it empty for a moment; sysfs's never is.
        text = (backlight / "brightness").read_text()
        return int(text) if text.strip() else -1

    def log():
        return shell_log.read_text()

    def key(name):
        for state in ("press", "release"):
            msg("headless_keyboard", "key", "keys", str(KEYS[name]), state)

    desktop.detail = lambda: f"called: {called()}, level {level()}\n{log()[-1500:]}"

    # Without the shell, the compositor runs wpctl.
    msg("volume_up")
    desktop.wait_for(lambda: sorted(called()) ==
                     ["compositor wpctl set-mute @DEFAULT_AUDIO_SINK@ 0",
                      "compositor wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ 5%+"],
                     "the compositor's wpctl")

    desktop.spawn([shell, "--config", str(desktop.config)], env={"CALLER": "shell"},
                  log="shell.log")
    desktop.wait_for(lambda: "paw surface rendered: paw taskbar" in log(),
                     "the panel rendered", timeout=30)
    # The shell subscribes asynchronously: ask until it has heard. Until then the compositor runs
    # brightnessctl.
    for _ in range(20):
        msg("brightness_up")
        try:
            desktop.wait_for(lambda: level() > 50, "the shell set the backlight", timeout=1)
            break
        except harness.Timeout:
            pass
    desktop.wait_for(lambda: "paw osd shown on HEADLESS-1" in log() and
                     log().count("osd hidden") == log().count("osd shown"),
                     "the display shown, and gone again")
    start = level()  # by now a second step the loop asked for, had the shell been slow, has landed
    count = len(called())

    # The keys reach the shell, which sets the backlight and, without its sound server, runs
    # wpctl; the compositor runs nothing.
    msg("headless_keyboard", "add", "keys")
    shown = log().count("osd shown")
    key("XF86MonBrightnessDown")
    desktop.wait_for(lambda: level() == start - 10 and log().count("osd shown") > shown,
                     "the backlight down by the binding's 10 %, and the display shown")
    key("XF86AudioRaiseVolume")
    key("XF86AudioMicMute")
    desktop.wait_for(lambda: sorted(called()[count:]) ==
                     ["shell wpctl set-mute @DEFAULT_AUDIO_SINK@ 0",
                      "shell wpctl set-mute @DEFAULT_AUDIO_SOURCE@ toggle",
                      "shell wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ 5%+"],
                     "the shell's wpctl")
    assert not [line for line in called()[count:] if line.startswith("compositor")], called()
print("Volume, microphone and brightness keys reach the shell, which carries them out, passed")
