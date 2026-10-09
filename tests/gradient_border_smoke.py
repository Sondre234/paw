# SPDX-License-Identifier: GPL-3.0-or-later
"""Gradient borders: with windows.border_color or border_inactive_color a gradient, every border
is drawn in pieces of gradients (`get frames` says "gradient"), following focus, and in rects of
one colour again once neither is; a rounded window's corners round its border too. The pointer
reaches a window through its border's corners inside its geometry, as through a rounded rect's
hole. With grim the screen shows the colours: the focused window's border red at its left, blue
at its right and between the two in the middle, the other's green, a rounded corner clear outside
its arc and coloured on it."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])
grim = sys.argv[3] if len(sys.argv) > 3 else ""

CONFIG = """return {
    xwayland = false,
    appearance = { background = "#000000" },
    layout = { tiling = false },
    mouse = { focus_follows = false },
    animations = { enabled = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    windows = {
        border_width = 6,
        corner_radius = 12,
        round = "%s",
        border_color = %s,
        border_inactive_color = "#00ff00",
        magnet = { enabled = false },
        rules = {
            { title = "^A$", position = { 100, 100 } },
            { title = "^B$", position = { 700, 100 } },
        },
    },
}"""
GRADIENT = '{ "#ff0000", "#0000ff", angle = 0 }'

with harness.Compositor(compositor, CONFIG % ("tiling", GRADIENT)) as desktop:
    msg = desktop.msg

    def windows():
        """title -> (x, y, width, height, focused)."""
        return {r[9]: (int(r[4]), int(r[5]), int(r[6]), int(r[7]), r[1] == "1")
                for r in desktop.rows("windows")}

    def borders():
        """title -> how its border is drawn, and its corners' radius."""
        return {r[1]: (r[12], int(r[5])) for r in desktop.rows("frames")}

    desktop.detail = lambda: f"windows: {windows()}\nframes: {desktop.rows('frames')}"

    def screen():
        desktop.wait_for(lambda: msg("get", "animations").split("\t")[0].strip() == "0",
                         "animations finished")
        return harness.grab(grim, desktop.env)

    clients = {}
    for title in "AB":
        clients[title] = desktop.spawn([probe, "--window-only"],
                                       env={"PAW_PROBE_TITLE": title,
                                            "PAW_PROBE_APP_ID": f"app-{title}"})
        desktop.wait_for(lambda: title in windows() and windows()[title][4], f"{title} focused")
    desktop.wait_for(lambda: borders() == {"A": ("gradient", 0), "B": ("gradient", 0)},
                     "gradient borders on both windows")

    def red(pixel):
        return pixel[0] > 200 and pixel[2] < 50

    def blue(pixel):
        return pixel[2] > 200 and pixel[0] < 50

    def green(pixel):
        return pixel[1] > 200 and pixel[0] < 50 and pixel[2] < 50

    x, y, width, height, _ = windows()["B"]
    left, right, middle = (x - 3, y + height // 2), (x + width + 2, y + height // 2), \
        (x + width // 2, y - 3)
    if shot := screen():
        assert red(shot.at(*left)) and blue(shot.at(*right)), (shot.at(*left), shot.at(*right))
        purple = shot.at(*middle)
        assert 90 < purple[0] < 165 and 90 < purple[2] < 165 and purple[1] < 20, purple
        assert red(shot.at(x - 5, y - 5)), shot.at(x - 5, y - 5)  # a square corner
        ax, ay, _, aheight, _ = windows()["A"]
        assert green(shot.at(ax - 3, ay + aheight // 2)), shot.at(ax - 3, ay + aheight // 2)

    # Focus moves: A's border is the gradient, B's green.
    subprocess.run([probe, "--activate", "app-A"], env=desktop.env, check=True, timeout=30,
                   stdout=subprocess.DEVNULL)
    desktop.wait_for(lambda: windows()["A"][4], "A focused")
    if shot := screen():
        ax, ay, awidth, aheight, _ = windows()["A"]
        assert red(shot.at(ax - 3, ay + aheight // 2)) and \
            blue(shot.at(ax + awidth + 2, ay + aheight // 2)), shot.at(ax - 3, ay + 10)
        assert green(shot.at(*left)) and green(shot.at(*right)), shot.at(*left)

    # Nothing but a border's pixels there, and the window through its corner: the pointer finds
    # no window on its border, and the window just inside its corner.
    assert msg("get", "pid_at", str(left[0]), str(left[1])) == ""
    assert msg("get", "pid_at", str(x + 1), str(y + 1)) == f"{clients['B'].pid}\n"

    # Rounded windows round their borders: clear outside the outer arc, coloured on it, and the
    # window keeps its corner to the pointer.
    desktop.reload(CONFIG % ("always", GRADIENT))
    desktop.wait_for(lambda: borders() == {"A": ("gradient", 12), "B": ("gradient", 12)},
                     "rounded gradient borders")
    assert msg("get", "pid_at", str(x + 1), str(y + 1)) == f"{clients['B'].pid}\n"
    if shot := screen():
        corner = shot.at(x - 5, y - 5)
        assert corner == (0, 0, 0), corner  # past the arc: the background
        assert green(shot.at(x - 3, y + 30)), shot.at(x - 3, y + 30)  # the straight part
        # On the ring, diagonally in from the window's corner, over the window's square corner.
        assert green(shot.at(x + 1, y + 1)), shot.at(x + 1, y + 1)

    # One colour each again: rects.
    desktop.reload(CONFIG % ("tiling", '"#ff0000"'))
    desktop.wait_for(lambda: borders() == {"A": ("color", 0), "B": ("color", 0)},
                     "borders of one colour")
    if shot := screen():
        assert red(shot.at(ax - 3, ay + aheight // 2)) and green(shot.at(*left))

    for title, client in clients.items():
        subprocess.run([probe, "--close", f"app-{title}"], env=desktop.env, check=True,
                       timeout=30, stdout=subprocess.DEVNULL)
        assert desktop.reap(client) == 0
print("Gradient borders follow focus, round with their windows, and give way to rects")
