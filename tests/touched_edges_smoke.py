# SPDX-License-Identifier: GPL-3.0-or-later
"""The edges of an output's usable area a window shown there reaches, which subscribers hear as
`touched NAME EDGES` and a floating bar docks along: none for a window in the middle; every one
for a maximized window, its bottom the top of the panel there; the top, bottom and left for one
snapped to the left half without gaps (none with them); every one for a tile alone with smart
gaps, none with gaps around it; and none once the window is on another workspace."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    animations = { enabled = false },
    layout = { tiling = false, gap = %d, smart_gaps = %s },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    windows = { rules = { { title = "^one$", position = "center", size = { 400, 300 } } } },
}"""

with harness.Compositor(compositor, CONFIG % (0, "true")) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for
    heard = desktop.subscribe()

    def touched():
        line = heard.last("touched HEADLESS-1 ")
        return line.split(" ")[2] if line else None

    desktop.detail = lambda: f"touched: {heard.values('touched ')} windows: {desktop.windows()}"

    # The probe reserves a panel 48 pixels tall along the bottom and opens its window.
    desktop.spawn([probe, "--external-control"], env={"PAW_PROBE_TITLE": "one"})
    wait_for(lambda: [w.title for w in desktop.windows()] == ["one"], "the window")
    wait_for(lambda: touched() == "-", "a window in the middle reaching no edge")

    msg("maximize")
    wait_for(lambda: touched() == "top,bottom,left,right", "a maximized window reaching every edge")
    window = desktop.windows()[0]
    assert window.box[3] == 720 - 48, window  # the panel's strip is not the usable area's
    msg("restore")
    wait_for(lambda: touched() == "-", "restored to the middle")

    msg("snap_left")
    wait_for(lambda: touched() == "top,bottom,left", "snapped to the left half")
    msg("restore")
    wait_for(lambda: touched() == "-", "restored again")
    desktop.reload(CONFIG % (20, "true"))
    msg("snap_left")
    wait_for(lambda: desktop.windows()[0].box[:2] == (20, 20), "snapped inside the gaps")
    wait_for(lambda: touched() == "-", "snapped inside the gaps, reaching no edge")
    msg("restore")

    # Tiling: a tile alone fills the area without gaps (smart gaps); with them, it reaches none.
    msg("toggle_tiling")
    wait_for(lambda: touched() == "top,bottom,left,right", "a tile alone, without gaps")
    desktop.reload(CONFIG % (20, "false"))
    wait_for(lambda: touched() == "-", "a tile with gaps around it")

    # Moved to another workspace, nothing is shown there to reach an edge.
    desktop.reload(CONFIG % (20, "true"))
    wait_for(lambda: touched() == "top,bottom,left,right", "a tile alone again")
    msg("move_to_workspace", "2")
    wait_for(lambda: touched() == "-", "the workspace left empty")
print("Windows reaching the usable area's edges are told to subscribers")
