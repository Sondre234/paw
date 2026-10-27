# SPDX-License-Identifier: GPL-3.0-or-later
"""A key binding with `press` and `release` (a pair, as push-to-talk) runs its press as its key
goes down and its release as it comes up, whatever happened while it was held: another key's
binding, the overview opening, the session locking, a reload without the binding. A key that is no
modifier is the pair's, the window getting neither its press nor its release. Do not disturb going
on and off stands in for the microphone: subscribers hear each. A binding without a key is a chord
of its modifiers on either side's keys, which still reach the window: it goes down with the last
of them and comes up with the first, and one with `release` runs only when tapped alone. A tap of
Super toggles the overview, closing it too, which takes the keys while it is open."""
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
        { mods = { "Ctrl", "Alt" }, press = { action = "wallpaper_next" },
          release = { action = "wallpaper_prev" } },
        { mods = { "Alt", "Shift" }, release = true, action = "notification_history" },
        { mods = { "Super" }, release = true, action = "toggle_overview" },
    },
}"""
# evdev's codes
F9, SUPER, P, X = 67, 125, 25, 45
LEFT_CTRL, RIGHT_CTRL, LEFT_ALT, RIGHT_ALT, LEFT_SHIFT = 29, 97, 56, 100, 42
# Event times for the taps that wait between their keys going down and coming up, so that a slow
# run does not hold them longer than a tap (500 ms).
TAP = 1_000_000

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

    # Ctrl + Alt as a chord: the press as the second of them goes down, either side's, the release
    # as the first comes up, and the window hears every key.
    def wallpaper():
        return heard.lines("wallpaper-cycle ")

    def pressed(*codes):
        return keys()[-len(codes):] == [f"key {code} pressed" for code in codes]

    for first, second in ((LEFT_CTRL, RIGHT_ALT), (LEFT_ALT, RIGHT_CTRL)):
        before = len(wallpaper())
        press.down(first)
        desktop.stays(lambda: len(wallpaper()) == before, "no chord with one modifier")
        press.down(second)
        desktop.wait_for(lambda: wallpaper()[before:] == ["wallpaper-cycle next"],
                         "the chord's press")
        assert pressed(first, second), keys()
        press.up(first)
        desktop.wait_for(lambda: wallpaper()[before:] == ["wallpaper-cycle next",
                                                         "wallpaper-cycle prev"],
                         "the chord's release as the first of its keys comes up")
        press.up(second)
        desktop.stays(lambda: len(wallpaper()) == before + 2, "nothing as the second comes up")

    # Alt + Shift tapped runs as the first of them comes up; with a key between, the key reaches
    # the window and the chord does nothing, and nor does it held longer than a tap.
    press.down(LEFT_ALT, time=TAP)
    press.down(LEFT_SHIFT, time=TAP + 50)
    desktop.stays(lambda: not heard.lines("notifications "), "nothing while Alt + Shift is held")
    press.up(LEFT_SHIFT, time=TAP + 150)
    desktop.wait_for(lambda: len(heard.lines("notifications ")) == 1, "the tap of Alt + Shift")
    press.up(LEFT_ALT, time=TAP + 200)
    press(LEFT_ALT, LEFT_SHIFT, X)
    desktop.wait_for(lambda: keys()[-3:] == [f"key {X} released", f"key {LEFT_SHIFT} released",
                                             f"key {LEFT_ALT} released"], "Alt + Shift + X")
    assert f"key {X} pressed" in keys(), keys()
    press.down(LEFT_ALT, time=TAP + 1000)
    press.down(LEFT_SHIFT, time=TAP + 1050)
    press.up(LEFT_SHIFT, time=TAP + 1700)
    press.up(LEFT_ALT, time=TAP + 1750)
    desktop.stays(lambda: len(heard.lines("notifications ")) == 1,
                  "no tap after Alt + Shift + X, nor held a while")

    # A tap of Super opens the overview, Super + P leaves it open, and a tap closes it again.
    def overview():
        return desktop.msg("get", "overview").split(" ")[0]

    press.down(SUPER, time=TAP + 2000)
    press.up(SUPER, time=TAP + 2100)
    desktop.wait_for(lambda: overview() == "open", "the overview from a tap of Super")
    press(SUPER, P)
    desktop.stays(lambda: overview() == "open", "the overview after Super + P")
    press.down(SUPER, time=TAP + 3000)
    press.up(SUPER, time=TAP + 3100)
    desktop.wait_for(lambda: overview() == "closed", "the overview closed by a tap of Super")
print("A pair's release runs after its press whatever happened meanwhile, chords of modifiers "
      "alone, and a tap of Super toggling the overview, passed")
