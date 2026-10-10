# SPDX-License-Identifier: GPL-3.0-or-later
"""windows.controls = "traffic_lights": the controls of a server-decorated window are three
circles at its top-left, shown while the pointer is near them; close, minimize and fullscreen
act on a click, the pointer between and below them reaches the window, and a reload switches
back to the flat strip at the top-right. With grim, the lights are coloured for the focused
window and grey for another. Driven by a virtual pointer (pointer_probe)."""
from pathlib import Path
import sys

import harness

compositor, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])
grim = sys.argv[4] if len(sys.argv) > 4 else ""

SCREEN = (1280, 720)
# Centres of the close, minimize and fullscreen lights from the window's top-left corner.
CLOSE, MINIMIZE, FULLSCREEN = (14, 14), (34, 14), (54, 14)


def config(controls):
    return """return {
    xwayland = false,
    layout = { tiling = false },
    mouse = { focus_follows = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    windows = { magnet = { enabled = false }, placement = "smart", controls = "%s" },
}""" % controls


with harness.Compositor(compositor, config("traffic_lights")) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def windows():
        return {w.title: w for w in desktop.windows()}

    def frames():
        return {r[1]: r for r in desktop.rows("frames")}

    def where(title):
        window = windows()[title]
        return window.x, window.y

    desktop.detail = lambda: msg("get", "windows") + msg("get", "frames")
    pointer = desktop.virtual_pointer(pointer_probe, *SCREEN)

    def point(title, offset):
        x, y = where(title)
        return str(x + offset[0]), str(y + offset[1])

    def open_window(title):
        process = desktop.spawn([probe, "--window-only"],
                                env={"PAW_PROBE_TITLE": title, "PAW_PROBE_SSD": "1"})
        wait_for(lambda: title in windows() and title in frames(), f"{title} mapped")
        return process

    open_window("A")
    assert frames()["A"][3:5] == ["traffic_lights", "0"], frames()["A"]
    # They show while the pointer is near the top-left corner, and hide when it leaves.
    pointer("move", *point("A", (30, 30)))
    wait_for(lambda: frames()["A"][4] == "1", "lights shown near the corner")
    pointer("move", *point("A", (200, 150)))
    wait_for(lambda: frames()["A"][4] == "0", "lights hidden away from the corner")

    # Below the lights the pointer reaches the window: a click there focuses it, where a click
    # on what is not a window would focus the desktop.
    second = open_window("B")
    assert windows()["B"].focused, "the new window has focus"
    pointer("move", *point("A", (40, 25)))
    wait_for(lambda: frames()["A"][4] == "1", "A's lights shown")
    pointer("click", "left")
    wait_for(lambda: windows()["A"].focused, "a click below the lights reached the window")

    # With grim: red, yellow and green on the focused window, grey on another.
    if grim:
        def light(title):
            pointer("move", *point(title, (CLOSE[0], CLOSE[1] + 9)))
            wait_for(lambda: frames()[title][4] == "1", f"{title}'s lights shown")
            lx, ly = where(title)
            return harness.grab(grim, desktop.env).at(lx + CLOSE[0] + 3, ly + CLOSE[1] - 2)

        r, g, b = light("A")
        assert r > 200 and g < 130 and b < 130, ("focused close light", (r, g, b))
        r, g, b = light("B")
        assert abs(r - g) < 30 and abs(g - b) < 40, ("unfocused close light", (r, g, b))

    # A click on a light acts on the window: minimize, then close.
    pointer("move", *point("A", MINIMIZE), "click", "left")
    wait_for(lambda: windows()["A"].minimized, "minimize light minimized A")
    pointer("move", *point("B", CLOSE), "click", "left")
    # The client exits as its window closes; reaped first, its exit fails no wait.
    assert desktop.reap(second) == 0
    wait_for(lambda: "B" not in windows(), "close light closed B")

    # Fullscreen fills the screen, and the light there brings the window back.
    third = open_window("C")
    pointer("move", *point("C", FULLSCREEN), "click", "left")
    wait_for(lambda: windows()["C"].box[2:] == (1280, 720), "fullscreen light filled the screen")
    pointer("move", "100", "300", "move", *(str(n) for n in FULLSCREEN), "click", "left")
    wait_for(lambda: windows()["C"].box[2:] != (1280, 720), "fullscreen light restored C")

    # A reload switches to the flat strip at the top-right, whose last button closes.
    desktop.reload(config("flat"))
    wait_for(lambda: frames()["C"][3] == "flat", "reload switched the controls")
    width = windows()["C"].width
    pointer("move", *point("C", (100, 100)), "move", *point("C", (width - 20, 20)), "click", "left")
    assert desktop.reap(third) == 0
    wait_for(lambda: "C" not in windows(), "flat close button closed C")
print("Traffic lights show near their corner, act on a click, and give way to the flat strip"
      + ("" if grim else " (pixels not checked: grim missing)"))
