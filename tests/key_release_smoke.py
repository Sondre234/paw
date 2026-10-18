# SPDX-License-Identifier: GPL-3.0-or-later
"""Key bindings with `release` run as their key comes up, as Hyprland's bindr. Super_L's, as
Windows' Start key, runs only when Super was tapped alone: not after another key (bound or not), a
click, a drag, the wheel or a touchpad swipe while it was held, not when it was held longer than a
tap, nor when it went down while a button was held, and its press and release still reach the
window with the keyboard. A key that is no modifier is the binding's from its press: the window gets neither its
press nor its release. Subscribers hear the launcher and the other panels asked for."""
from pathlib import Path
import sys

import harness

compositor, input_probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false },
    mouse = { focus_follows = false },
    bindings = {
        { key = "Super_L", release = true, action = "launcher" },
        { mods = { "Super" }, key = "p", action = "palette" },
        { key = "F8", release = true, action = "notification_history" },
    },
}"""
# evdev's codes
SUPER, P, W, F8 = 125, 25, 17, 66
# Event times for the taps that wait between Super going down and coming up, so that a slow run
# does not hold it longer than a tap (500 ms).
TAP = 1_000_000

with harness.Compositor(compositor, CONFIG) as desktop:
    log = desktop.root / "window.log"

    def keys():
        return [line for line in log.read_text().splitlines() if line.startswith("key ")]

    def asked(what):
        """How many times subscribers were asked to open `what`."""
        return len(heard.lines(f"{what} "))

    desktop.spawn([input_probe, "--keys", "--no-gestures", "--no-tablet", "Window"],
                  log="window.log")
    desktop.wait_for(lambda: "ready" in log.read_text() and
                     any(w.focused for w in desktop.windows()), "the window mapped and focused")
    heard = desktop.subscribe()
    press = desktop.keyboard()
    desktop.detail = lambda: f"heard: {heard.lines()}, keys: {keys()}"

    # A tap of Super opens the launcher as it comes up, and the window hears Super go down and up.
    press.down(SUPER, time=TAP)
    desktop.wait_for(lambda: keys() == [f"key {SUPER} pressed"], "Super's press at the window")
    desktop.stays(lambda: asked("launcher") == 0, "no launcher while Super is held")
    press.up(SUPER, time=TAP + 150)
    desktop.wait_for(lambda: asked("launcher") == 1, "the launcher as Super comes up")
    desktop.wait_for(lambda: keys()[-1] == f"key {SUPER} released", "Super's release too")

    # Super + P runs its own binding, and Super with a key no binding takes reaches the window:
    # neither is a tap.
    press(SUPER, P)
    desktop.wait_for(lambda: asked("palette") == 1, "Super + P's binding")
    press(SUPER, W)
    desktop.wait_for(lambda: keys()[-2:] == [f"key {W} released", f"key {SUPER} released"],
                     "Super + W at the window")
    desktop.stays(lambda: asked("launcher") == 1, "no launcher after Super + a key")

    # Nor is Super held through a click, a drag of the window (Super + drag moves it), the wheel
    # or a touchpad swipe.
    pointer = desktop.virtual_pointer(pointer_probe, 1280, 720)
    pointer("move 640 360")
    desktop.msg("headless_pointer", "add", "pad")
    swipe = lambda: (desktop.msg("headless_pointer", "swipe", "pad", "begin", "3"),
                     desktop.msg("headless_pointer", "swipe", "pad", "end"))
    for name, done in (("a click", lambda: pointer("click left")),
                       ("a drag", lambda: pointer("press left", "move 700 400", "release left")),
                       ("the wheel", lambda: pointer("scroll 15")), ("a swipe", swipe)):
        press.down(SUPER)
        done()
        press.up(SUPER)
        desktop.stays(lambda: asked("launcher") == 1, f"no launcher after Super and {name}")

    # Nor is Super held longer than a tap, thought better of, however still it was.
    press.down(SUPER, time=TAP + 1000)
    press.up(SUPER, time=TAP + 1600)
    desktop.wait_for(lambda: keys()[-1] == f"key {SUPER} released", "Super held at the window")
    desktop.stays(lambda: asked("launcher") == 1, "no launcher after Super held a while")

    # Nor Super going down while a button is held, as dragging a window by its title.
    pointer("press left")
    press(SUPER)
    pointer("release left")
    desktop.stays(lambda: asked("launcher") == 1, "no launcher after Super during a drag")

    # Tapped again, it opens the launcher again.
    press(SUPER)
    desktop.wait_for(lambda: asked("launcher") == 2, "the launcher on the next tap")

    # F8's binding has the key from its press, and runs as it comes up.
    before = len(keys())
    press.down(F8)
    desktop.stays(lambda: asked("notifications") == 0, "nothing while F8 is held")
    press.up(F8)
    desktop.wait_for(lambda: asked("notifications") == 1, "the history as F8 comes up")
    assert len(keys()) == before, keys()

    # A reload without the binding lets Super be.
    desktop.reload(CONFIG.replace('release = true, action = "launcher"', 'action = "none"'))
    press(SUPER)
    desktop.wait_for(lambda: keys()[-1] == f"key {SUPER} released", "Super at the window")
    desktop.stays(lambda: asked("launcher") == 2, "no launcher without the binding")
