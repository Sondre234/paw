# SPDX-License-Identifier: GPL-3.0-or-later
"""Touchscreens, from a headless one: a finger goes to the window under it through wl_touch, with
several at once, motion past the window's edge in its own coordinates, frames and cancel, and
focuses it; a panel takes fingers too. A window that never bound wl_touch gets the pointer and
its left button instead, from one finger at a time, as do the window controls (a tap on the
close light closes the window), the drag strip (a finger drags the window) and the desktop. The
screen is mapped to touch.output, else to the output the device names, else to a built-in
panel, else across every output; clients see a touch capability only while there is a
touchscreen."""
from pathlib import Path
import sys

import harness

compositor, probe, window_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false },
    mouse = { focus_follows = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    windows = { controls = "traffic_lights", magnet = { enabled = false }, rules = {
        { title = "^Touch$", position = { 50, 50 } },
        { title = "^Plain$", position = { 700, 50 } },
        { title = "^Framed$", position = { 100, 400 } } } },
    %s
}"""
WIDTH, HEIGHT = 1280, 720

with harness.Compositor(compositor, CONFIG % "") as desktop:
    msg = desktop.msg

    def windows():
        """{title: (focused, x, y)}."""
        return {r[9]: (r[1] == "1", int(r[4]), int(r[5])) for r in desktop.rows("windows")}

    def log(name):
        return (desktop.root / f"{name}.log").read_text().splitlines()

    def heard(name, kind):
        return [line for line in log(name) if line.startswith(kind)]

    def touch():
        return [tuple(row) for row in desktop.rows("touch")]

    clock = [1000]

    def finger(step, screen, *words):
        clock[0] += 10
        msg("headless_touch", step, screen, *words, *([str(clock[0])] if step != "frame" else []))

    def at(x, y):
        """A point of the layout as the screen across it gives it, from 0 to 1."""
        return f"{x / WIDTH:.6f}", f"{y / HEIGHT:.6f}"

    desktop.detail = lambda: f"windows: {windows()}, touch: {touch()}"
    assert touch() == [], touch()
    msg("headless_touch", "add", "screen")
    assert touch() == [("touchscreen", "screen", "-")], touch()
    desktop.spawn([probe, "Touch"], log="touch.log")
    desktop.spawn([probe, "--no-touch", "Plain"], log="plain.log")
    desktop.spawn([probe, "--layer", "Panel"], log="panel.log")
    framed = desktop.spawn([window_probe, "--window-only"], log="framed.log",
                  env={"PAW_PROBE_TITLE": "Framed", "PAW_PROBE_SSD": "1"})
    desktop.wait_for(lambda: all("ready" in log(name) for name in ("touch", "plain", "panel")) and
                     {"Touch", "Plain", "Framed"} <= set(windows()), "everything mapped")
    assert windows()["Touch"][1:] == (50, 50) and windows()["Plain"][1:] == (700, 50), windows()

    # A finger on the window: it hears where, in its own coordinates, and takes focus.
    finger("down", "screen", "0", *at(150, 130))
    finger("frame", "screen")
    desktop.wait_for(lambda: heard("touch", "touch") == ["touch down 0 100 80", "touch frame"],
                     "the finger on Touch")
    assert windows()["Touch"][0], windows()
    assert ("point 0", "window", "Touch") in touch(), touch()
    # A second finger, both moving, the first out past the window's edge over another.
    finger("down", "screen", "1", *at(350, 250))
    finger("motion", "screen", "0", *at(170, 140))
    finger("frame", "screen")
    finger("motion", "screen", "0", *at(800, 100))
    finger("frame", "screen")
    finger("up", "screen", "1")
    finger("up", "screen", "0")
    finger("frame", "screen")
    desktop.wait_for(lambda: heard("touch", "touch")[2:] == [
        "touch down 1 300 200", "touch motion 0 120 90", "touch frame", "touch motion 0 750 50",
        "touch frame", "touch up 1", "touch up 0", "touch frame"], "two fingers on Touch")
    assert [row for row in touch() if row[0] != "touchscreen"] == [], touch()
    assert heard("plain", "pointer button") == [], log("plain")

    # On a window without wl_touch the first finger is the pointer and its left button; a
    # second one there does nothing while it is down.
    finger("down", "screen", "2", *at(800, 150))
    finger("down", "screen", "3", *at(900, 200))
    finger("motion", "screen", "2", *at(820, 160))
    desktop.wait_for(lambda: heard("plain", "pointer button") == ["pointer button 272 pressed"] and
                     "pointer motion 120 110" in log("plain"), "the finger as the pointer")
    assert ("pointer", "2") in touch() and windows()["Plain"][0], (touch(), windows())
    finger("up", "screen", "3")
    finger("up", "screen", "2")
    desktop.wait_for(lambda: heard("plain", "pointer button")[1:] == ["pointer button 272 released"],
                     "the pointer's button released")
    assert ("pointer", "2") not in touch()
    # At once: a finger on Touch, and one standing in for the pointer on Plain.
    finger("down", "screen", "4", *at(100, 100))
    finger("down", "screen", "5", *at(750, 100))
    finger("frame", "screen")
    desktop.wait_for(lambda: "touch down 4 50 50" in log("touch") and
                     heard("plain", "pointer button")[2:] == ["pointer button 272 pressed"],
                     "a finger on each")
    finger("up", "screen", "4")
    finger("up", "screen", "5")
    finger("frame", "screen")
    # libinput giving the touch up: the window forgets it.
    finger("down", "screen", "6", *at(200, 200))
    finger("cancel", "screen", "6")
    desktop.wait_for(lambda: log("touch")[-1] == "touch cancel", "the finger cancelled")
    assert [row for row in touch() if row[0] == "point 6"] == [], touch()

    # A panel takes fingers; it holds no keyboard, so the focus stays.
    focused = [title for title, row in windows().items() if row[0]]
    finger("down", "screen", "7", *at(640, 690))
    finger("up", "screen", "7")
    finger("frame", "screen")
    desktop.wait_for(lambda: heard("panel", "touch") == ["touch down 7 640 30", "touch up 7",
                                                         "touch frame"], "a finger on the panel")
    assert [title for title, row in windows().items() if row[0]] == focused, windows()

    # The drag strip along the top of a window the compositor decorates drags it with a finger.
    x, y = windows()["Framed"][1:]
    finger("down", "screen", "8", *at(x + 160, y + 2))
    finger("motion", "screen", "8", *at(x + 210, y + 17))
    finger("motion", "screen", "8", *at(x + 260, y + 32))
    finger("up", "screen", "8")
    desktop.wait_for(lambda: windows()["Framed"][1:] == (x + 100, y + 30), "Framed dragged")
    # A tap on its close light closes it.
    x, y = windows()["Framed"][1:]
    finger("down", "screen", "9", *at(x + 14, y + 14))
    finger("up", "screen", "9")
    assert desktop.reap(framed) == 0  # it closes its window and exits
    desktop.wait_for(lambda: "Framed" not in windows(), "the close light tapped")
    # A tap on the bare desktop puts the pointer there, on nothing.
    finger("down", "screen", "10", *at(1000, 500))
    finger("up", "screen", "10")
    assert ["pointer", "-", "-"] in desktop.rows("seat"), desktop.rows("seat")

    # A built-in panel takes the screen; then the output the device names; touch.output wins.
    msg("headless_output", "add", "eDP-1", "800x600")
    desktop.wait_for(lambda: touch()[0] == ("touchscreen", "screen", "eDP-1"), "mapped to eDP-1")
    finger("down", "screen", "11", "0.5", "0.5")
    finger("up", "screen", "11")
    focused = {row[0]: row[2] for row in desktop.rows("workspaces")}
    assert focused == {"HEADLESS-1": "0", "eDP-1": "1"}, focused
    msg("headless_touch", "add", "side", "HEADLESS-1")
    assert ("touchscreen", "side", "HEADLESS-1") in touch(), touch()
    desktop.reload(CONFIG % 'touch = { output = "HEADLESS-1" },')
    assert ("touchscreen", "screen", "HEADLESS-1") in touch(), touch()
    desktop.reload(CONFIG % 'touch = { output = "desc:nothing like it" },')
    assert ("touchscreen", "screen", "eDP-1") in touch(), touch()
    finger("down", "side", "12", *at(150, 130))
    finger("up", "side", "12")
    desktop.wait_for(lambda: "touch down 12 100 80" in log("touch"), "a finger from the other")

    # With no touchscreen left, clients lose the capability, and the windows their wl_touch.
    msg("headless_touch", "remove", "screen")
    msg("headless_touch", "remove", "side")
    assert touch() == [], touch()
    assert "no such touchscreen" in msg("headless_touch", "down", "side", "1", "0", "0", ok=False)
