# SPDX-License-Identifier: GPL-3.0-or-later
"""A tile moved by key onto a monitor that scrolls opens right of the focused column there, even
when it comes in over the first column: only the pointer puts a new column first."""
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
print("scroll enter passed")
