# SPDX-License-Identifier: GPL-3.0-or-later
"""A tile moved by key onto a monitor that scrolls opens right of the focused column there, even
when it comes in over the first column: only the pointer puts a new column first, and only on
that monitor's strip."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

# HEADLESS-1 tiles with dwindle, HEADLESS-2 right of it scrolls with half-width columns.
CONFIG = """return {
    xwayland = false,
    layout = { tiling = true, gap = 0,
               outputs = { ["HEADLESS-2"] = { tile_layout = "scroll" } },
               scroll = { follow = "edge", width = 0.5 } },
    windows = { rules = { { app_id = "^ruled$", output = "HEADLESS-2" } } },
    outputs = { monitors = {
        ["HEADLESS-1"] = { mode = "1280x720", position = { x = 0, y = 0 } },
        ["HEADLESS-2"] = { mode = "1280x720", position = { x = 1280, y = 0 } },
    } },
}"""

with harness.Compositor(compositor, CONFIG, env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    msg = desktop.msg

    def window(title):
        return next(w for w in desktop.windows() if w.title == title)

    def strip():
        return [w.title for w in sorted(desktop.windows(), key=lambda w: w.x)
                if w.output == "HEADLESS-2"]

    desktop.detail = lambda: f"windows: {desktop.windows()}"
    msg("headless_pointer", "add", "mouse")
    msg("headless_pointer", "move", "mouse", "1400", "300")  # over HEADLESS-2's first column
    desktop.open_window(probe, "first", focused=True)
    desktop.wait_for(lambda: window("first").x == 1280, "first column at the left of HEADLESS-2")
    msg("headless_pointer", "move", "mouse", "300", "300")
    desktop.open_window(probe, "moved", focused=True)
    desktop.wait_for(lambda: window("moved").output == "HEADLESS-1", "moved on HEADLESS-1")

    # It comes in by HEADLESS-2's left edge, over the first column, and still lands right of it.
    msg("move_right")
    desktop.wait_for(lambda: strip() == ["first", "moved"], "moved right of the first column")

    # The pointer on the first column of HEADLESS-1, which scrolls too, puts nothing first on
    # HEADLESS-2: a window a rule opens there goes right of the column focused there.
    msg("headless_pointer", "move", "mouse", "301", "300")
    desktop.open_window(probe, "back", focused=True)
    desktop.wait_for(lambda: window("back").output == "HEADLESS-1", "back on HEADLESS-1")
    msg("layout_scroll")
    desktop.wait_for(lambda: msg("get", "layout", "HEADLESS-1").split()[0] == "scroll",
                     "HEADLESS-1 scrolls")
    msg("headless_pointer", "move", "mouse", "302", "300")  # onto back, its first column
    desktop.open_window(probe, "ruled", "ruled", focused=True)
    desktop.wait_for(lambda: len(strip()) == 3 and strip()[0] == "first", "ruled not first")
print("scroll enter passed")
