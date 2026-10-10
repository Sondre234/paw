# SPDX-License-Identifier: GPL-3.0-or-later
"""Magnetic edges: a floating window dragged near the edge of the output, of the area a panel
leaves free or of another window lands on it and stays held, a guide line shows the edge, the
bypass modifier and windows.magnet turn it off, and dropping at the top still maximizes.
Driven by a virtual pointer and keyboard (pointer_probe); the window asks for the move the way a
client-decorated one does (wayland_probe with PAW_PROBE_MOVE)."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

grim = sys.argv[4] if len(sys.argv) > 4 else ""
SCREEN = (1280, 720)
PANEL = 48


def config(magnet=""):
    return """return {
    xwayland = false,
    layout = { tiling = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    windows = { magnet = { %s } },
}""" % magnet


with harness.Compositor(compositor, config()) as desktop:
    msg = desktop.msg

    def windows():
        """By title: x, y, width, height, tiled."""
        return {w.title: (w.x, w.y, w.width, w.height, w.tiled)
                for w in desktop.windows()}

    def where():
        return windows()["W"][:2]

    def guides():
        """(shown, x, y, width, height) of the vertical and the horizontal line."""
        rows = [tuple(int(n) for n in row) for row in desktop.rows("guides")]
        return rows

    desktop.detail = lambda: f"windows: {windows()}"
    wait_for = desktop.wait_for
    pointer = desktop.virtual_pointer(pointer_probe, *SCREEN)

    # A panel along the bottom (with a window of its own, "P"), and the window to drag.
    desktop.open_window(probe, "P", args=("--external-control",))
    client = desktop.open_window(probe, "W", env={"PAW_PROBE_MOVE": "1"})
    px, py, pw, ph, _ = windows()["P"]
    ww, wh = windows()["W"][2:4]
    assert (ww, wh) == (320, 240), windows()

    def moving():
        """Whether a window is being moved with the pointer."""
        return next(r for r in desktop.rows("snap") if r[0] == "zone")[6] == "1"

    def drag():
        """Presses on W, and waits for the move it asks for."""
        wx, wy = where()
        pointer("move", str(wx + 100), str(wy + 100), "press", "left")
        wait_for(moving, "the press turned into a move")

    def to(grab_offset, x, y):
        """Moves the pointer so the window would be at (x, y) without magnetism."""
        pointer("move", str(x + grab_offset), str(y + grab_offset))

    def release():
        pointer("release", "left")

    # The output's left edge holds it (5 px away), and it stays 12 px out and no more.
    drag()
    to(100, 5, 200)
    assert where() == (0, 200), where()
    assert guides()[0] == (1, -1, 200, 3, 240) and guides()[1][0] == 0, guides()
    if grim:  # the guide is drawn over the window's edge: those pixels differ from inside
        shot = harness.grab(grim, desktop.env)
        assert shot.at(0, 300) != shot.at(8, 300), (shot.at(0, 300), shot.at(8, 300))
    to(100, 11, 200)
    assert where() == (0, 200), where()
    to(100, 13, 200)
    assert where() == (13, 200), where()
    assert guides()[0][0] == 0, guides()
    if grim:
        shot = harness.grab(grim, desktop.env)
        assert shot.at(14, 300) == shot.at(22, 300), (shot.at(14, 300), shot.at(22, 300))
    # The right edge, and the panel's edge along the bottom (the window ends at 672).
    to(100, SCREEN[0] - 320 - 5, 200)
    assert where() == (SCREEN[0] - 320, 200), where()
    assert guides()[0] == (1, SCREEN[0] - 1, 200, 3, 240), guides()
    to(100, 600, SCREEN[1] - PANEL - 240 - 6)
    assert where() == (600, SCREEN[1] - PANEL - 240), where()
    assert guides()[1] == (1, 600, SCREEN[1] - PANEL - 1, 320, 3), guides()
    assert guides()[0][0] == 0, guides()
    # Far from anything, it goes where the pointer says.
    to(100, 600, 350)
    assert where() == (600, 350), where()
    assert guides()[0][0] == 0 and guides()[1][0] == 0, guides()

    # Another window's edges: it touches P's right edge, and lines up with its top.
    to(100, px + pw + 7, py + 6)
    assert where() == (px + pw, py), (where(), windows())
    assert guides()[0][0] == 1 and guides()[1][0] == 1, guides()
    # Far below P there is nothing to hold on to (P ends at py + ph).
    to(100, px + pw + 7, py + ph + 100)
    assert where() == (px + pw + 7, py + ph + 100), where()

    # With Shift held the drag is free; without, it lands again.
    pointer("key", "shift", "down")
    to(100, 5, 200)
    assert where() == (5, 200), where()
    assert guides()[0][0] == 0, guides()
    pointer("key", "shift", "up")
    to(100, 6, 200)
    assert where() == (0, 200), where()
    release()
    desktop.stays(lambda: where() == (0, 200) and guides()[0][0] == 0, "the drop moved on",
                  detail=lambda: f"{where()}, {guides()}")

    # Dropping at the top of the screen still maximizes.
    drag()
    pointer("move", "640", "0")
    release()
    wait_for(lambda: windows()["W"][2] > 1000, "dropped at the top: maximized")
    assert windows()["W"][3] > 600 and not windows()["W"][4], windows()
    msg("restore")
    wait_for(lambda: windows()["W"][2] == 320, "restored")

    # A shorter distance, or none, changes what is caught; the guides can be left out.
    desktop.reload(config("distance = 4"))
    drag()
    to(100, 5, 200)
    assert where() == (5, 200), where()
    to(100, 3, 200)
    assert where() == (0, 200), where()
    release()
    desktop.reload(config("distance = 30, guides = false, bypass = \"none\""))
    drag()
    to(100, 25, 380)
    assert where() == (0, 380), where()
    assert guides()[0][0] == 0, guides()
    pointer("key", "shift", "down")  # no bypass modifier is set
    to(100, 26, 380)
    assert where() == (0, 380), where()
    pointer("key", "shift", "up")
    release()
    desktop.reload(config("enabled = false"))
    drag()
    to(100, 3, 200)
    assert where() == (3, 200), where()
    assert guides()[0][0] == 0, guides()
    release()

    # Resizing: the edges being pulled stick to the same lines, the others stay put.
    desktop.reload(config())
    client.terminate()
    desktop.reap(client, timeout=5)
    wait_for(lambda: "W" not in windows(), "window closed")
    resizer = desktop.open_window(probe, "W", env={"PAW_PROBE_RESIZE": "bottom_right"},
                                  stdout=subprocess.PIPE, text=True)
    wx, wy = where()

    def size():
        return windows()["W"][2:4]

    def resized(width, height):
        wait_for(lambda: size() == (width, height), f"resized to {width}x{height}")

    # Press on the bottom right corner (one pixel inside), and pull it near the corner
    # of the free area: it lands on the output's right edge and above the panel.
    pointer("move", str(wx + 319), str(wy + 239), "press", "left")
    assert resizer.stdout.readline().strip() == "resizing"
    pointer("move", str(SCREEN[0] - 5 - 1), str(SCREEN[1] - PANEL - 6 - 1))
    resized(SCREEN[0] - wx, SCREEN[1] - PANEL - wy)
    assert guides()[0][0] == 1 and guides()[1][0] == 1, guides()
    pointer("move", str(SCREEN[0] - 40 - 1), str(SCREEN[1] - PANEL - 40 - 1))
    resized(SCREEN[0] - wx - 40, SCREEN[1] - PANEL - wy - 40)
    assert guides()[0][0] == 0 and guides()[1][0] == 0, guides()
    pointer("key", "shift", "down")
    pointer("move", str(SCREEN[0] - 5 - 1), str(SCREEN[1] - PANEL - 6 - 1))
    resized(SCREEN[0] - wx - 5, SCREEN[1] - PANEL - wy - 6)
    pointer("key", "shift", "up")
    pointer("release", "left")
    assert windows()["W"][:2] == (wx, wy), windows()  # the corner pulled the far edges only
print("Magnetic edges hold a dragged window, show guides, and obey their settings"
      + ("" if grim else " (pixels not checked: grim missing)"))
