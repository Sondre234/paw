# SPDX-License-Identifier: GPL-3.0-or-later
"""Key bindings with `locked` run while the session is locked, as Hyprland's bindl, and every
other key still goes to the lock screen; bindings with `repeats` run again while their key is
held, as Hyprland's binde, and go on while the session is locked only when they are `locked` too.
The actions are the volume and brightness ones, without a shell, so that stand-ins for wpctl and
brightnessctl write down each time one runs."""
from pathlib import Path
import os
import sys

import harness

compositor, lock_probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    keyboard = { repeat_rate = 40, repeat_delay = 150 },
    bindings = {
        { key = "XF86AudioRaiseVolume", action = "volume_up", locked = true, repeats = true },
        { key = "XF86AudioMute", action = "volume_mute", locked = true },
        { key = "XF86MonBrightnessDown", action = "brightness_down" },
        { key = "XF86MonBrightnessUp", action = "brightness_up", repeats = true },
    },
}"""
# evdev's codes of those keys
KEYS = {"XF86AudioRaiseVolume": 115, "XF86AudioMute": 113, "XF86MonBrightnessDown": 224,
        "XF86MonBrightnessUp": 225}

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

    def count(text):
        return calls.read_text().count(text)

    def brighter():
        return count("set 5%+")

    def darker():
        return count("set 5%-")

    def louder():
        return count("set-volume")

    def muted():
        return count("set-mute @DEFAULT_AUDIO_SINK@ toggle")

    def key(name, *states):
        for state in states or ("press", "release"):
            msg("headless_keyboard", "key", "keys", str(KEYS[name]), state)

    def settled(what, message):
        """What `what` gives once it has stopped changing for a fifth of a second (a program
        started just before may still be writing), failing while it keeps changing."""
        seen = []

        def still():
            seen.append(what())
            return len(seen) > 10 and len(set(seen[-10:])) == 1
        desktop.wait_for(still, message)
        return seen[-1]

    def lock():
        locker = desktop.spawn([lock_probe, "hold", str(root / "locker.log")])
        desktop.wait_for(lambda: "Session locked" in desktop.log.read_text(), "the session locked")
        return locker

    desktop.detail = lambda: calls.read_text()
    msg("headless_keyboard", "add", "keys")

    # Held, a binding that repeats runs again until its key comes up; one that does not, once.
    key("XF86MonBrightnessUp", "press")
    desktop.wait_for(lambda: brighter() >= 3, "the brightness going up while held")
    key("XF86MonBrightnessUp", "release")
    held = settled(brighter, "no more once released")
    key("XF86MonBrightnessDown", "press")
    desktop.wait_for(lambda: darker() == 1, "the brightness down once")
    desktop.stays(lambda: darker() == 1, "a binding that does not repeat, held", duration=.5)
    key("XF86MonBrightnessDown", "release")

    # One that is not `locked` stops repeating as the session locks.
    key("XF86MonBrightnessUp", "press")
    desktop.wait_for(lambda: brighter() >= held + 2, "the brightness going up again")
    locker = lock()
    held = settled(brighter, "the repeat stopped by the lock")
    key("XF86MonBrightnessUp", "release")

    # Locked, the bindings marked so run, and repeat; the others do not, nor does msg.
    assert "the session is locked" in msg("volume_up", ok=False)
    key("XF86AudioMute")
    desktop.wait_for(lambda: muted() == 1, "the mute key while locked")
    key("XF86MonBrightnessDown")
    key("XF86MonBrightnessUp")
    before = louder()
    key("XF86AudioRaiseVolume", "press")
    desktop.wait_for(lambda: louder() >= before + 3, "the volume going up while held, locked")
    key("XF86AudioRaiseVolume", "release")
    settled(louder, "no more once released")
    assert darker() == 1 and brighter() == held and muted() == 1, calls.read_text()

    # Unlocked, every binding runs again.
    locker.terminate()
    assert desktop.reap(locker) == 0
    desktop.wait_for(lambda: "Session unlocked" in desktop.log.read_text(),
                     "the session unlocked")
    key("XF86MonBrightnessDown")
    desktop.wait_for(lambda: darker() == 2, "the brightness key again once unlocked")
print("Locked bindings run while the session is locked, and repeating ones while held, passed")
