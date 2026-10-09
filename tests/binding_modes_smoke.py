# SPDX-License-Identifier: GPL-3.0-or-later
"""Binding modes, as sway's modes and Hyprland's submaps: a binding with the mode action puts a
mode in use, whose bindings then take the place of the others (bare keys among them), until one
leaves it. Subscribers hear "mode NAME" and `get mode` says it; `paw msg mode NAME` changes
it, a reload and locking the session leave it. The bindings are volume actions, without a shell,
so that a stand-in for wpctl writes down each one that runs."""
from pathlib import Path
import os
import socket
import sys

import harness

compositor, lock_probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    modes = {
        sound = {
            { key = "Up", action = "volume_up" },
            { key = "Down", action = "volume_down", amount = 1 },
            { key = "r", action = "mode", mode = "resize" },
            { key = "Escape", action = "mode", mode = "default" },
            { key = "Return", action = "mode", mode = "default" },
        },
        resize = {
            { key = "Left", action = "resize_left" },
            { key = "Escape", action = "mode", mode = "default" },
        },
    },
    bindings = {
        { mods = { "Super" }, key = "s", action = "mode", mode = "sound" },
        { mods = { "Super" }, key = "Up", action = "volume_mute" },
    },
}"""
# evdev's codes
KEYS = {"Super": 125, "s": 31, "r": 19, "Up": 103, "Down": 108, "Left": 105, "Escape": 1,
        "Return": 28}

with harness.Compositor(compositor, CONFIG, start=False) as desktop:
    root, msg = desktop.root, desktop.msg
    calls = root / "calls"
    calls.touch()
    tools = root / "bin"
    tools.mkdir()
    script = tools / "wpctl"
    script.write_text(f'#!/bin/sh\necho "wpctl $*" >> "{calls}"\n')
    script.chmod(0o755)
    desktop.env["PATH"] = f"{tools}:{os.environ.get('PATH', '/usr/bin:/bin')}"
    desktop.start()

    def called():
        return [line for line in calls.read_text().splitlines() if "set-mute @DEFAULT_AUDIO_SINK@ 0"
                not in line]

    def mode():
        return msg("get", "mode").strip()

    def press(*names):
        """Presses the keys in order, then releases them the other way round."""
        for name in names:
            msg("headless_keyboard", "key", "keys", str(KEYS[name]), "press")
        for name in reversed(names):
            msg("headless_keyboard", "key", "keys", str(KEYS[name]), "release")

    class Subscriber:
        """The binding modes the control socket's state stream announced, in order, without
        repeats."""

        def __init__(self):
            self.socket = socket.socket(socket.AF_UNIX)
            self.socket.connect(desktop.env["PAW_SOCKET"])
            self.socket.sendall(b"subscribe\n")
            self.socket.settimeout(0.05)
            self.buffer = ""

        def heard(self):
            try:
                while data := self.socket.recv(8192):
                    self.buffer += data.decode()
            except socket.timeout:
                pass
            lines = [line for line in self.buffer.splitlines() if line.startswith("mode ")]
            return [line for i, line in enumerate(lines) if i == 0 or lines[i - 1] != line]

    desktop.detail = lambda: f"mode {mode()}, called {called()}"
    subscriber = Subscriber()
    desktop.wait_for(lambda: subscriber.heard() == ["mode default"], "the first state")
    msg("headless_keyboard", "add", "keys")
    assert mode() == "default"

    # Outside the mode its keys are not bound.
    press("Up")
    desktop.stays(lambda: called() == [], "a bare arrow outside the mode")
    # In it, they are, and the others are not.
    press("Super", "s")
    assert mode() == "sound"
    press("Up")
    press("Down")
    desktop.wait_for(lambda: sorted(called()) ==
                     ["wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ 1%-",
                      "wpctl set-volume -l 1.0 @DEFAULT_AUDIO_SINK@ 5%+"], "the mode's bindings")
    press("Super", "Up")
    desktop.stays(lambda: len(called()) == 2, "a binding outside the mode, in it")
    # One mode leads to another, and Escape or Return back out.
    press("r")
    assert mode() == "resize"
    press("Escape")
    assert mode() == "default"
    press("Super", "s")
    press("Return")
    assert mode() == "default"
    press("Super", "Up")
    desktop.wait_for(lambda: called()[2:] == ["wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle"],
                     "the bindings outside any mode again")
    desktop.wait_for(lambda: subscriber.heard() == ["mode default", "mode sound", "mode resize",
                                                    "mode default", "mode sound", "mode default"],
                     "every change heard", detail=subscriber.heard)

    # paw msg names modes as the bindings do.
    msg("mode", "resize")
    assert mode() == "resize"
    msg("mode", "default")
    assert mode() == "default"
    assert "no mode nope; the modes are default, resize, sound" in msg("mode", "nope", ok=False)
    assert "mode takes a mode's name" in msg("mode", ok=False)
    assert "mode takes a mode's name" in msg("mode", "sound", "resize", ok=False)

    # A reload leaves the mode, as does locking the session.
    msg("mode", "sound")
    desktop.reload()
    assert mode() == "default"
    press("Up")
    desktop.stays(lambda: len(called()) == 3, "the mode's bindings gone with it")
    msg("mode", "sound")
    locker = desktop.spawn([lock_probe, "hold", str(root / "locker.log")])
    desktop.wait_for(lambda: "Session locked" in desktop.log.read_text(), "the session locked")
    assert mode() == "default"
    locker.terminate()
    assert desktop.reap(locker) == 0
    desktop.wait_for(lambda: "Session unlocked" in desktop.log.read_text(),
                     "the session unlocked")
    desktop.wait_for(lambda: subscriber.heard()[6:] == ["mode resize", "mode default",
                                                        "mode sound", "mode default",
                                                        "mode sound", "mode default"],
                     "the later changes heard", detail=subscriber.heard)
print("Binding modes: entered, left, their keys their own, by msg, left on reload and lock, passed")
