# SPDX-License-Identifier: GPL-3.0-or-later
"""Drawing tablets through tablet-v2, from a headless one with a pen, an eraser and a pad: a tool
near a window that bound tablet-v2 reaches it with its position, pressure, distance, tilt,
rotation, slider, wheel and buttons, its tip focuses the window and keeps it while down, wherever
it goes; over a window that did not, it is the pointer, its tip the left button and its buttons
the right and middle ones. The pad's buttons go to the window with the keyboard, if it takes them.
The tablet spans every output, or the one tablet.output names."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false },
    mouse = { focus_follows = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    windows = { rules = { { title = "^Draw$", position = { 50, 50 } },
                          { title = "^Plain$", position = { 700, 50 } } } },
    %s
}"""
WIDTH, HEIGHT = 1280, 720
BTN_LEFT, BTN_RIGHT, BTN_MIDDLE, BTN_STYLUS, BTN_STYLUS2 = 272, 273, 274, 331, 332

with harness.Compositor(compositor, CONFIG % "") as desktop:
    msg = desktop.msg

    def windows():
        return {w.title: w.focused for w in desktop.windows()}

    def log(name):
        return (desktop.root / f"{name}.log").read_text().splitlines()

    def tablet():
        return [tuple(row) for row in desktop.rows("tablet")]

    def tool(*words):
        msg("headless_tablet", words[0], "wacom", *words[1:])

    def at(x, y):
        return f"{x / WIDTH:.6f}", f"{y / HEIGHT:.6f}"

    def heard(name, *lines):
        """Whether `name` heard `lines` last, frames left out."""
        events = [line for line in log(name) if line.split()[0] in ("tool", "pad", "pointer") and
                  line != "tool frame" and not line.startswith("pointer motion")]
        return events[-len(lines):] == list(lines)

    def since(name, start, *lines):
        """Whether `name` heard `lines` in this order, among others, from line `start` on."""
        events = iter(log(name)[start:])
        return all(any(event == line for event in events) for line in lines)

    desktop.detail = lambda: f"windows: {windows()}, tablet: {tablet()}\n{log('draw')}\n{log('plain')}"
    msg("headless_tablet", "add", "wacom")
    assert tablet() == [("tablet", "wacom", "-"), ("pad", "wacom-pad", "wacom")], tablet()
    # Plain opens last, and has the keyboard.
    desktop.spawn([probe, "Draw"], log="draw.log")
    desktop.wait_for(lambda: "ready" in log("draw") and "Draw" in windows(), "Draw mapped")
    desktop.spawn([probe, "--no-tablet", "Plain"], log="plain.log")
    desktop.wait_for(lambda: "ready" in log("plain") and windows().get("Plain"), "Plain mapped")

    # The pen comes near Draw: it hears where, in its own coordinates, and every axis.
    tool("in", "pen", *at(150, 130))
    desktop.wait_for(lambda: heard("draw", "tool in pen", "tool motion 100 80"), "the pen near Draw")
    assert ("tool", "pen", "1", "window", "Draw") in tablet(), tablet()
    tool("axis", "pen", "pressure=0.5", "distance=0.25", "tilt=10,-5", "rotation=30",
         "slider=-1", "wheel=15")
    desktop.wait_for(lambda: heard("draw", "tool pressure 32767", "tool distance 16383",
                                   "tool tilt 10.0 -5.0", "tool rotation 30.0",
                                   "tool slider -65535", "tool wheel 15.0 0"), "the axes")
    # Its tip goes down: Draw takes focus and keeps the pen past its edge until the tip lifts.
    assert not windows()["Draw"], windows()
    tool("tip", "pen", "down")
    desktop.wait_for(lambda: heard("draw", "tool down"), "the tip down")
    assert windows()["Draw"], windows()
    tool("axis", "pen", f"x={900 / WIDTH:.6f}", "pressure=1")
    tool("button", "pen", str(BTN_STYLUS), "press")
    tool("button", "pen", str(BTN_STYLUS), "release")
    desktop.wait_for(lambda: heard("draw", "tool motion 850 80", "tool pressure 65535",
                                   f"tool button {BTN_STYLUS} pressed",
                                   f"tool button {BTN_STYLUS} released"), "drawing past the edge")
    assert [line for line in log("plain") if line.startswith("pointer")] == [], log("plain")
    # Lifted over Plain, which takes no tablet: Draw loses it, and it is the pointer there.
    tool("tip", "pen", "up")
    desktop.wait_for(lambda: heard("draw", "tool up", "tool out") and
                     heard("plain", "pointer enter 200 80"), "the pen the pointer over Plain")
    assert ("tool", "pen", "1", "pointer", "-") in tablet(), tablet()
    tool("tip", "pen", "down")
    tool("tip", "pen", "up")
    tool("button", "pen", str(BTN_STYLUS), "press")
    tool("button", "pen", str(BTN_STYLUS), "release")
    tool("button", "pen", str(BTN_STYLUS2), "press")
    tool("button", "pen", str(BTN_STYLUS2), "release")
    desktop.wait_for(lambda: heard("plain", f"pointer button {BTN_LEFT} pressed",
                                   f"pointer button {BTN_LEFT} released",
                                   f"pointer button {BTN_RIGHT} pressed",
                                   f"pointer button {BTN_RIGHT} released",
                                   f"pointer button {BTN_MIDDLE} pressed",
                                   f"pointer button {BTN_MIDDLE} released"),
                     "the tip and buttons as the pointer's")
    assert windows()["Plain"], windows()
    tool("out", "pen")
    desktop.wait_for(lambda: ("tool", "pen", "0", "-", "-") in tablet(), "the pen away")

    # The pad's buttons go to the window with the keyboard, only if it takes them.
    msg("headless_tablet", "pad", "wacom", "button", "1", "press")
    msg("headless_tablet", "pad", "wacom", "button", "1", "release")
    start = len(log("draw"))
    tool("in", "eraser", *at(150, 130))
    tool("tip", "eraser", "down")
    tool("tip", "eraser", "up")
    desktop.wait_for(lambda: windows()["Draw"] and since("draw", start, "tool in eraser",
                                                         "pad enter", "tool down", "tool up"),
                     "the eraser focusing Draw")
    msg("headless_tablet", "pad", "wacom", "button", "1", "press")
    msg("headless_tablet", "pad", "wacom", "button", "1", "release")
    desktop.wait_for(lambda: heard("draw", "pad button 1 pressed", "pad button 1 released"),
                     "the pad's button on Draw")
    assert [line for line in log("draw") if line.startswith("pad button")] == [
        "pad button 1 pressed", "pad button 1 released"], log("draw")
    tool("out", "eraser")

    # tablet.output maps it to one output, whose desktop the pen then points at.
    msg("headless_output", "add", "Side", "800x600")
    desktop.reload(CONFIG % 'tablet = { output = "Side" },')
    assert tablet()[0] == ("tablet", "wacom", "Side"), tablet()
    tool("in", "pen", "0.5", "0.5")
    tool("tip", "pen", "down")
    tool("tip", "pen", "up")
    tool("out", "pen")
    focused = {row[0]: row[2] for row in desktop.rows("workspaces")}
    assert focused == {"HEADLESS-1": "0", "Side": "1"}, focused

    msg("headless_tablet", "remove", "wacom")
    assert tablet() == [], tablet()
    assert "no such tablet" in msg("headless_tablet", "in", "wacom", "pen", "0", "0", ok=False)
