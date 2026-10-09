# SPDX-License-Identifier: GPL-3.0-or-later
"""Touchpad gestures reach the window under the pointer through pointer-gestures: swipes,
pinches and holds from a headless pointer, as libinput hands them over, with their fingers,
deltas, scale, rotation and whether they were cancelled. A window that never bound the gestures
gets none of them, and they go to the window the pointer is on, not to the focused one."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false },
    mouse = { focus_follows = false },
    windows = { placement = "cascade" },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
}"""

with harness.Compositor(compositor, CONFIG) as desktop:
    msg = desktop.msg

    def windows():
        """{title: (focused, centre)} of every window."""
        found = {}
        for row in desktop.rows("windows"):
            x, y, w, h = (int(n) for n in row[4:8])
            found[row[9]] = (row[1] == "1", (x + w // 2, y + h // 2))
        return found

    def log(name):
        return (desktop.root / f"{name}.log").read_text().splitlines()

    desktop.detail = lambda: f"windows: {windows()}\n{log('gestures')}\n{log('plain')}"
    desktop.spawn([probe, "Gestures"], log="gestures.log")
    desktop.wait_for(lambda: "ready" in log("gestures") and "Gestures" in windows(),
                     "the window binding gestures mapped")
    msg("headless_pointer", "add", "touchpad")
    msg("headless_pointer", "move", "touchpad", *map(str, windows()["Gestures"][1]))
    desktop.wait_for(lambda: any(line.startswith("pointer enter") for line in log("gestures")),
                     "the pointer on the window")

    def gesture(*words):
        msg("headless_pointer", words[0], "touchpad", *words[1:])

    def heard(name, lines):
        """Whether `lines` came to the window in this order, after what it heard before."""
        events = [line for line in log(name) if not line.startswith("pointer")]
        return events[-len(lines):] == lines

    # Four fingers: the compositor takes no swipe of four fingers by default.
    gesture("swipe", "begin", "4")
    gesture("swipe", "update", "12.5", "-3")
    gesture("swipe", "update", "-2", "0.5")
    gesture("swipe", "end")
    desktop.wait_for(lambda: heard("gestures", ["swipe begin 4", "swipe update 12.5 -3.0",
                                                "swipe update -2.0 0.5", "swipe end 0"]),
                     "the swipe passed on")
    gesture("pinch", "begin", "2")
    gesture("pinch", "update", "1", "-1", "1.25", "15")
    gesture("pinch", "cancel")
    desktop.wait_for(lambda: heard("gestures", ["pinch begin 2", "pinch update 1.0 -1.0 1.25 15.0",
                                                "pinch end 1"]),
                     "the pinch passed on, cancelled")
    gesture("hold", "begin", "3")
    gesture("hold", "end")
    desktop.wait_for(lambda: heard("gestures", ["hold begin 3", "hold end 0"]),
                     "the hold passed on")

    # A window that never bound the gestures opens over the first, and the pointer goes to it:
    # it hears no gesture, and the first, no longer under the pointer, neither.
    desktop.spawn([probe, "--no-gestures", "Plain"], log="plain.log")
    desktop.wait_for(lambda: "ready" in log("plain") and "Plain" in windows(),
                     "the window without gestures mapped")
    msg("headless_pointer", "move", "touchpad", *map(str, windows()["Plain"][1]))
    desktop.wait_for(lambda: any(line.startswith("pointer enter") for line in log("plain")),
                     "the pointer on the second window")
    before = len(log("gestures"))
    gesture("pinch", "begin", "2")
    gesture("pinch", "update", "0", "0", "0.5", "0")
    gesture("pinch", "end")
    gesture("hold", "begin", "3")
    gesture("hold", "end")
    desktop.stays(lambda: not any(line.split()[0] in ("swipe", "pinch", "hold")
                                  for line in log("plain")) and
                  [line for line in log("gestures")[before:] if not line.startswith("pointer")] == [],
                  "no gesture reached either window")

    # Back over the first window, which is not focused: gestures follow the pointer.
    msg("headless_pointer", "move", "touchpad", *map(str, (windows()["Gestures"][1][0] - 190,
                                                           windows()["Gestures"][1][1] - 140)))
    assert windows()["Plain"][0], windows()
    gesture("pinch", "begin", "3")
    gesture("pinch", "end")
    desktop.wait_for(lambda: heard("gestures", ["pinch begin 3", "pinch end 0"]),
                     "the pinch passed on to the window under the pointer")

    # Unplugged, the pointer leaves the window as it was.
    msg("headless_pointer", "remove", "touchpad")
    assert "no such pointer" in msg("headless_pointer", "swipe", "touchpad", "begin", "3", ok=False)
