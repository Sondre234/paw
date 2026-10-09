# SPDX-License-Identifier: GPL-3.0-or-later
"""resize_left/right/up/down: tiles move the split beside them, floating windows their right or
bottom edge within the output, maximized and fullscreen windows stay put, and
features.keyboard_resize = false turns the actions off."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

GAP = 8
CONFIG = """return {
    xwayland = false,
    layout = { tiling = true, gap = %d },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    features = { keyboard_resize = %s },
}"""

with harness.Compositor(compositor, CONFIG % (GAP, "true")) as desktop:
    msg = desktop.msg

    def windows():
        """(focused, tiled, x, y, width, height) per window, most recently focused last."""
        rows = desktop.rows("windows")
        return [(r[1] == "1", r[3] == "1", *map(int, r[4:8])) for r in rows]

    desktop.detail = lambda: f"windows: {windows()}"

    def focused():
        return next(w for w in windows() if w[0])

    def tiles():
        """The left and right tile, by place."""
        return sorted((w for w in windows() if w[1]), key=lambda w: w[2])

    def split():
        """Where the left tile ends: the split between the tiles, less half the gap."""
        left = tiles()[0]
        return left[2] + left[4]

    def near(value, expected, slack=2):
        return abs(value - expected) <= slack

    for count in (1, 2):
        desktop.spawn([probe, "--external-control"])
        desktop.wait_for(lambda: len(windows()) == count and all(w[1] for w in windows()),
                         f"window {count} tiled")
    desktop.wait_for(lambda: len(tiles()) == 2 and tiles()[0][3] == tiles()[1][3],
                     "two tiles side by side")

    # The arrows move the split between the two tiles that way, whichever is focused:
    # the left tile grows toward its right edge, the right one shrinks from its left.
    start = split()
    msg("resize_right", "80")
    desktop.wait_for(lambda: near(split(), start + 80), "split moved 80 pixels right")
    msg("resize_left")
    desktop.wait_for(lambda: near(split(), start + 40), "split moved back the default 40")
    left, right = tiles()
    assert near(left[2] + left[4] + GAP, right[2]) and near(right[2] + right[4],
                                                           1280 - GAP), tiles()
    # With no split across the other axis, up and down change nothing.
    before = tiles()
    msg("resize_up")
    msg("resize_down", "100")
    desktop.stays(lambda: tiles() == before, "up and down left the tiles alone")
    # Bad sizes are refused.
    for words in (("resize_right", "x"), ("resize_right", "0"),
                  ("resize_right", "40", "40"), ("resize_right", "99999")):
        msg(*words, ok=False)
    # The split stops short of the output's edge.
    msg("resize_right", "4000")
    desktop.wait_for(lambda: near(split(), 1280 * 0.9 - GAP // 2),
                     "split limited to 90%")

    # Floating: the right and bottom edges move that way, inside the output.
    msg("toggle_floating")
    desktop.wait_for(lambda: not focused()[1], "focused window floating")
    _, _, x, y, width, height = focused()
    msg("resize_right", "50")
    msg("resize_down", "30")
    desktop.wait_for(lambda: focused()[2:] == (x, y, width + 50, height + 30),
                     "floating window grew right and down")
    msg("resize_left", "20")
    msg("resize_up", "10")
    desktop.wait_for(lambda: focused()[2:] == (x, y, width + 30, height + 20),
                     "floating window shrank from the right and bottom")
    msg("resize_right", "4000")
    msg("resize_down", "4000")
    desktop.wait_for(lambda: focused()[2] + focused()[4] == 1280 - GAP and
                     focused()[2:4] == (x, y), "floating window stopped at the output's edge")
    bottom = focused()[3] + focused()[5]
    assert bottom <= 720 - GAP, focused()
    msg("resize_left", "4000")
    msg("resize_up", "4000")
    desktop.wait_for(lambda: focused()[2:] == (x, y, 64, 64), "floating window kept a minimum")
    # Sticky windows float, and resize as such.
    msg("toggle_sticky")
    msg("resize_right", "36")
    desktop.wait_for(lambda: focused()[2:] == (x, y, 100, 64), "sticky window resized")
    msg("toggle_sticky")
    desktop.wait_for(lambda: not focused()[1], "unstuck window still floating")

    # Maximized and fullscreen windows stay as they are.
    msg("maximize")
    desktop.wait_for(lambda: focused()[4] > 1000, "maximized")
    before = focused()
    for action in ("resize_left", "resize_right", "resize_up", "resize_down"):
        msg(action, "60")
    desktop.stays(lambda: focused() == before, "the maximized window kept its size")
    msg("restore")
    desktop.wait_for(lambda: focused()[4] < 1000, "restored")
    msg("fullscreen")
    desktop.wait_for(lambda: focused()[4] == 1280, "fullscreen")
    before = focused()
    msg("resize_left", "60")
    desktop.stays(lambda: focused() == before, "the fullscreen window kept its size")
    msg("fullscreen")
    desktop.wait_for(lambda: focused()[4] < 1280, "left fullscreen")

    # Turned off, the actions still parse but do nothing, for tiles and floating windows.
    desktop.reload(CONFIG % (GAP, "false"))
    before = focused()
    msg("resize_right", "80")
    desktop.stays(lambda: focused() == before, "turned off, the floating window kept its size")
    msg("toggle_floating")
    desktop.wait_for(lambda: len(tiles()) == 2, "tiled again")
    start = split()
    msg("resize_right", "80")
    desktop.stays(lambda: split() == start, "turned off, the split stayed")
    # Turned back on, the same request works again.
    desktop.reload(CONFIG % (GAP, "true"))
    msg("resize_left", "80")
    desktop.wait_for(lambda: near(split(), start - 80), "resizing back on")
print("Keyboard resizing of tiles and floating windows, and its toggle, passed")
