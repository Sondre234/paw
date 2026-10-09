# SPDX-License-Identifier: GPL-3.0-or-later
"""The media keys end to end: a headless compositor with the shell and stand-in media players
(tests/mpris_probe) on a private session bus, a dbus-daemon this test starts and kills. A key the
compositor binds reaches the shell as a "media" line, and the shell calls the player playing most
lately: the one that starts playing takes the keys over, and when it goes they go back. The shell
never sees the real session bus, nor the system bus."""
import os
from pathlib import Path
import select
import shutil
import subprocess
import sys

import harness

compositor, shell, mpris = (str(Path(p).resolve()) for p in sys.argv[1:4])
if not shutil.which("dbus-daemon"):
    print("dbus-daemon not found: the media keys were not driven")
    sys.exit(0)

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false },
    bindings = {
        { mods = {}, key = "XF86AudioPlay", action = "media_play_pause" },
        { mods = {}, key = "XF86AudioNext", action = "media_next" },
        { mods = {}, key = "XF86AudioPrev", action = "media_previous" },
        { mods = {}, key = "XF86AudioStop", action = "media_stop" },
    },
}"""
# evdev key codes of the keys above.
KEYS = {"XF86AudioPlay": 164, "XF86AudioNext": 163, "XF86AudioPrev": 165, "XF86AudioStop": 166}


class Player:
    """An mpris_probe and the lines it printed."""

    def __init__(self, desktop, name, *options):
        self.process = desktop.spawn([mpris, name, *options], stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE)
        self.lines = []
        self.partial = b""
        desktop.wait_for(lambda: "ready" in self.read(), f"the player {name} on the bus")

    def read(self):
        """What it printed so far, a line each. Reads the pipe itself: a buffered reader would
        hold lines select() no longer reports."""
        out = self.process.stdout
        while select.select([out], [], [], 0)[0]:
            data = os.read(out.fileno(), 4096)
            if not data:
                break
            self.partial += data
            *lines, self.partial = self.partial.split(b"\n")
            self.lines.extend(line.decode() for line in lines)
        return self.lines

    def calls(self):
        """The controls it was sent."""
        return [line for line in self.read() if line not in ("ready", "read", "ok")]

    def tell(self, command):
        oks = self.read().count("ok")
        self.process.stdin.write(command.encode() + b"\n")
        self.process.stdin.flush()
        desktop.wait_for(lambda: self.read().count("ok") > oks, f"the player took '{command}'")


with harness.Compositor(compositor, CONFIG, bus=True, start=False) as desktop:
    root, env, msg = desktop.root, desktop.env, desktop.msg
    env.update(QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software",
               QT_FORCE_STDERR_LOGGING="1", XDG_DATA_HOME=str(root), XDG_DATA_DIRS=str(root),
               XDG_STATE_HOME=str(root), DBUS_SYSTEM_BUS_ADDRESS="disabled:")
    desktop.start()
    shell_log = root / "shell.log"
    desktop.detail = lambda: shell_log.read_text()[-1500:]
    music = Player(desktop, "music", "--status", "Paused")
    desktop.spawn([shell, "--config", str(desktop.config)], log="shell.log")
    desktop.wait_for(lambda: "paw surface rendered: paw taskbar" in shell_log.read_text(),
                     "the shell's panel")
    # The shell reads a player's position once it has read the rest of it.
    desktop.wait_for(lambda: "read" in music.read(), "the shell finding the player")
    msg("headless_keyboard", "add", "keys")

    def press(key):
        msg("headless_keyboard", "key", "keys", str(KEYS[key]), "press")
        msg("headless_keyboard", "key", "keys", str(KEYS[key]), "release")

    expected = []
    for key, call in [("XF86AudioPlay", "PlayPause"), ("XF86AudioNext", "Next"),
                      ("XF86AudioPrev", "Previous"), ("XF86AudioStop", "Stop")]:
        press(key)
        expected.append(call)
        desktop.wait_for(lambda: music.calls() == expected, f"{key} reaching the player",
                         detail=music.calls)
    msg("media_next")
    expected.append("Next")
    desktop.wait_for(lambda: music.calls() == expected, "media_next reaching the player",
                     detail=music.calls)

    # Another player starts playing: the keys are its own until it goes.
    video = Player(desktop, "video", "--status", "Paused", "--title", "A video")
    desktop.wait_for(lambda: "read" in video.read(), "the shell finding the second player")
    reads = video.read().count("read")
    video.tell("status Playing")
    desktop.wait_for(lambda: video.read().count("read") > reads, "the shell following it")
    press("XF86AudioNext")
    desktop.wait_for(lambda: video.calls() == ["Next"], "the key reaching the player playing",
                     detail=video.calls)
    video.process.stdin.write(b"quit\n")
    video.process.stdin.flush()
    assert desktop.reap(video.process) == 0
    music.tell("status Playing")
    press("XF86AudioPlay")
    expected.append("PlayPause")
    desktop.wait_for(lambda: music.calls() == expected, "the key back with the first player",
                     detail=music.calls)
