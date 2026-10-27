# SPDX-License-Identifier: GPL-3.0-or-later
"""A key binding with `press` and `release` (a pair, as push-to-talk) runs its press as its key
goes down and its release as it comes up, whatever happened while it was held: another key's
binding, the overview opening, the session locking, a reload without the binding. A key that is no
modifier is the pair's, the window getting neither its press nor its release. Do not disturb going
on and off stands in for the microphone: subscribers hear each."""
from pathlib import Path
import sys

import harness

compositor, input_probe, lock_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false },
    mouse = { focus_follows = false },
    overview = { animation = false },
    bindings = {
        { key = "F9", press = { action = "dnd_on" }, release = { action = "dnd_off" } },
        { mods = { "Super" }, key = "p", action = "palette" },
    },
}"""
# evdev's codes
F9, SUPER, P = 67, 125, 25

with harness.Compositor(compositor, CONFIG) as desktop:
    log = desktop.root / "window.log"

    def keys():
        return [line for line in log.read_text().splitlines() if line.startswith("key ")]

    def heard_dnd():
        return heard.lines("dnd ")

    desktop.spawn([input_probe, "--keys", "--no-gestures", "--no-tablet", "Window"],
                  log="window.log")
    desktop.wait_for(lambda: "ready" in log.read_text() and
                     any(w.focused for w in desktop.windows()), "the window mapped and focused")
    heard = desktop.subscribe()
    press = desktop.keyboard()
    desktop.detail = lambda: f"heard: {heard.lines()}, keys: {keys()}"

    # The press runs as F9 goes down, the release as it comes up; the window hears neither.
    press.down(F9)
    desktop.wait_for(lambda: heard_dnd() == ["dnd on"], "the press as F9 goes down")
    desktop.stays(lambda: heard_dnd() == ["dnd on"], "nothing more while F9 is held")
    press.up(F9)
    desktop.wait_for(lambda: heard_dnd() == ["dnd on", "dnd off"], "the release as F9 comes up")
    assert keys() == [], keys()

    # Another key's binding meanwhile lets the release be, unlike a tap's.
    press.down(F9)
    press(SUPER, P)
    desktop.wait_for(lambda: len(heard.lines("palette ")) == 1, "Super + P while F9 is held")
    press.up(F9)
    desktop.wait_for(lambda: heard_dnd()[-1:] == ["dnd off"] and len(heard_dnd()) == 4,
                     "the release after another key")

    # So does the overview opening, which takes the keys.
    press.down(F9)
    desktop.msg("toggle_overview")
    desktop.wait_for(lambda: desktop.msg("get", "overview").startswith("open"), "the overview")
    press.up(F9)
    desktop.wait_for(lambda: len(heard_dnd()) == 6, "the release with the overview open")
    desktop.msg("toggle_overview")

    # And the session locking.
    press.down(F9)
    locker = desktop.spawn([lock_probe, "hold", str(desktop.root / "locker.log")])
    desktop.wait_for(lambda: "Session locked" in desktop.log.read_text(), "the session locked")
    press.up(F9)
    desktop.wait_for(lambda: len(heard_dnd()) == 8, "the release on the lock screen")
    # Locked, a pair's press runs only with `locked`.
    press(F9)
    desktop.stays(lambda: len(heard_dnd()) == 8, "no press on the lock screen")
    locker.terminate()
    assert desktop.reap(locker) == 0
    desktop.wait_for(lambda: "Session unlocked" in desktop.log.read_text(), "the session unlocked")

    # And a reload without the binding: the release kept from the press runs.
    press.down(F9)
    desktop.wait_for(lambda: len(heard_dnd()) == 9, "the press before the reload")
    desktop.reload(CONFIG.replace('press = { action = "dnd_on" }, release = { action = "dnd_off" }',
                                  'action = "none"'))
    press.up(F9)
    desktop.wait_for(lambda: heard_dnd()[-1:] == ["dnd off"] and len(heard_dnd()) == 10,
                     "the release after a reload")
    press(F9)
    desktop.wait_for(lambda: keys()[-1:] == [f"key {F9} released"], "F9 at the window, unbound")
    assert len(heard_dnd()) == 10, heard_dnd()
print("A pair's release runs after its press whatever happened meanwhile, passed")
