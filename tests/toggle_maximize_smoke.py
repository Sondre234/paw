# SPDX-License-Identifier: GPL-3.0-or-later
"""toggle_maximize maximizes the focused window below the panels, as a drop at the top edge does,
and gives a maximized one its own size back: a floating window, a snapped one (maximized, not
restored, from a half), a fullscreen one (out of fullscreen first), a tile (back into the tiling
it floated out of) and one floated on purpose where windows tile (floating again)."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    animations = { enabled = false },
    layout = { tiling = false, gap = 0 },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
}"""

MAXIMIZED = (0, 0, 1280, 672)  # above P's 48 pixel panel along the bottom

with harness.Compositor(compositor, CONFIG) as desktop:
    msg = desktop.msg

    def window(title="W"):
        return next(w for w in desktop.windows() if w.title == title)

    def placed(box, what):
        desktop.wait_for(lambda: window().box == tuple(box), what)

    desktop.detail = lambda: f"windows: {desktop.windows()}"
    desktop.open_window(probe, "P", args=("--external-control",))
    desktop.open_window(probe, "W", focused=True)
    floating = window().box
    assert floating[2:] == (320, 240), desktop.windows()

    msg("toggle_maximize")
    placed(MAXIMIZED, "maximized below the panel")
    msg("toggle_maximize")
    placed(floating, "its own size back")

    msg("snap_left")
    placed((0, 0, 640, 672), "snapped left")
    msg("toggle_maximize")
    placed(MAXIMIZED, "maximized from the half")
    msg("toggle_maximize")
    placed(floating, "its own size back, not the half")

    # Fullscreen from a binding has the same place and size; the window is told it is fullscreen.
    def fullscreen():
        return next(row[3] for row in desktop.rows("stacking") if row[1] == "W") == "fullscreen"

    msg("fullscreen")
    desktop.wait_for(fullscreen, "W fullscreen")
    msg("toggle_maximize")
    desktop.wait_for(lambda: not fullscreen() and window().box == MAXIMIZED,
                     "out of fullscreen and maximized")
    msg("toggle_maximize")
    placed(floating, "its own size back after fullscreen")

    # Where windows tile, a tile floats as it is maximized and goes back into the tiling.
    msg("toggle_tiling")
    desktop.wait_for(lambda: all(w.tiled for w in desktop.windows()), "both tiled")
    tile = window().box
    msg("toggle_maximize")
    desktop.wait_for(lambda: not window().tiled and window().box == MAXIMIZED,
                     "the tile maximized, floating")
    msg("toggle_maximize")
    desktop.wait_for(lambda: window().tiled and window().box == tile, "back in its tile")

    # One floated on purpose floats again at its own size.
    msg("toggle_floating")
    desktop.wait_for(lambda: not window().tiled, "W floating")
    own = window().box
    msg("toggle_maximize")
    placed(MAXIMIZED, "the floating window maximized")
    msg("toggle_maximize")
    placed(own, "the floating window at its own size again")
    assert not window().tiled, desktop.windows()
print("toggle_maximize maximizes and gives windows their size or tile back")
