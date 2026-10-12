# SPDX-License-Identifier: GPL-3.0-or-later
"""A floating window between two outputs belongs to the one its middle is on: when the other
starts tiling, the window moves fully onto its own output rather than staying under the tiles,
and one whose middle is on the output that starts tiling joins its tiling."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

# Rules place the probes, 320x240, from the left edge of HEADLESS-1, where the pointer is: "into"
# 1000 to 1320, its middle on HEADLESS-1; "over" 1140 to 1460, its middle on HEADLESS-2; "away"
# wholly on HEADLESS-2.
CONFIG = """return {
    xwayland = false,
    animations = { enabled = false },
    layout = { tiling = false },
    outputs = { order = { "HEADLESS-1", "HEADLESS-2" },
                monitors = { ["HEADLESS-1"] = { mode = "1280x720" },
                             ["HEADLESS-2"] = { mode = "1280x720" } } },
    windows = { rules = {
        { app_id = "^into$", size = { 320, 240 }, position = { 1000, 100 } },
        { app_id = "^over$", size = { 320, 240 }, position = { 1140, 100 } },
        { app_id = "^away$", size = { 320, 240 }, position = { 1600, 300 } },
    } },
}"""

with harness.Compositor(compositor, CONFIG, env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    msg = desktop.msg

    def windows():
        """By app ID: tiled, output, (x, y, width, height)."""
        return {w.app_id: (w.tiled, w.output, w.box) for w in desktop.windows()}

    desktop.detail = lambda: f"windows: {windows()}"

    for app_id in ("into", "over", "away"):
        desktop.spawn([probe, "--external-control"], env={"PAW_PROBE_APP_ID": app_id})
        desktop.wait_for(lambda: app_id in windows(), f"{app_id} mapped")
    desktop.wait_for(lambda: windows() == {
        "into": (False, "HEADLESS-1", (1000, 100, 320, 240)),
        "over": (False, "HEADLESS-2", (1140, 100, 320, 240)),
        "away": (False, "HEADLESS-2", (1600, 300, 320, 240)),
    }, "the probes float where the rules put them")

    msg("output", "HEADLESS-1", "toggle_tiling")
    desktop.wait_for(lambda: windows()["into"][:2] == (True, "HEADLESS-1"),
                     "into joined the tiling of HEADLESS-1")

    def home(box):
        x, y, width, height = box
        return x >= 1280 and x + width <= 2560 and (y, width, height) == (100, 320, 240)

    desktop.wait_for(lambda: windows()["over"][:2] == (False, "HEADLESS-2") and
                     home(windows()["over"][2]), "over moved wholly onto HEADLESS-2")
    assert windows()["away"] == (False, "HEADLESS-2", (1600, 300, 320, 240)), windows()
print("Windows between outputs join the tiling or move home passed")
