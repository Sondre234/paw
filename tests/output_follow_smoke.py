# SPDX-License-Identifier: GPL-3.0-or-later
"""The layout starts at 0, 0, and windows move with their output when the layout changes."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

# The pointer starts at the origin, on the first output in `order`, so the window opens there.
CONFIG = """return {
    xwayland = false,
    layout = { tiling = false },
    outputs = {
        order = { %s },
        monitors = { ["HEADLESS-1"] = { mode = "1280x720" },
                     ["HEADLESS-2"] = { mode = "1280x720", %s } },
    },
}"""

with harness.Compositor(compositor, CONFIG % ('"HEADLESS-1", "HEADLESS-2"', ""),
                        env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:

    def outputs():
        return {row[0]: tuple(int(value) for value in row[2:6]) for row in desktop.rows("outputs")}

    def window():
        """x, y, and output of the only window."""
        windows = desktop.windows()
        return (windows[0].x, windows[0].y, windows[0].output) if len(windows) == 1 else None

    desktop.spawn([probe, "--external-control"])
    desktop.wait_for(lambda: window() is not None and window()[2] == "HEADLESS-1",
                     "window opened on HEADLESS-1")
    x, y, _ = window()
    assert x < 1280, window()

    # Swapping the order moves HEADLESS-1 right by 1280; its window goes along.
    desktop.reload(CONFIG % ('"HEADLESS-2", "HEADLESS-1"', ""))
    assert outputs()["HEADLESS-1"] == (1280, 0, 1280, 720), outputs()
    assert window() == (x + 1280, y, "HEADLESS-1"), window()

    # An output placed left of the origin shifts the whole layout right, since X11
    # windows get no input at negative coordinates.
    desktop.reload(CONFIG % ('"HEADLESS-1"', "position = { x = -1280, y = -100 }"))
    assert outputs() == {"HEADLESS-2": (0, 0, 1280, 720),
                         "HEADLESS-1": (1280, 100, 1280, 720)}, outputs()
    assert window() == (x + 1280, y + 100, "HEADLESS-1"), window()
print("Layout origin and windows following their output passed")
