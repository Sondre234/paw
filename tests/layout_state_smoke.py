# SPDX-License-Identifier: GPL-3.0-or-later
"""Subscribers hear each output's tiling layout as `layout NAME LAYOUT`, that of the workspace it
shows, for the tiling bar's layout module: the configured one at first, the next as layout_next
steps to it, one a layout action names, the configured one again on a workspace never switched,
and the one switched to back on the first workspace."""
from pathlib import Path
import sys

import harness

compositor = str(Path(sys.argv[1]).resolve())

CONFIG = """return {
    xwayland = false,
    layout = { tiling = true, tile_layout = "spiral" },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
}"""

with harness.Compositor(compositor, CONFIG) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for
    heard = desktop.subscribe()

    def layout():
        line = heard.last("layout HEADLESS-1 ")
        return line.split(" ")[2] if line else None

    desktop.detail = lambda: f"layouts: {heard.values('layout ')}"
    wait_for(lambda: layout() == "spiral", "the configured layout")
    msg("layout_next")
    wait_for(lambda: layout() == "monocle", "the next layout after spiral")
    msg("output", "HEADLESS-1", "layout_master")
    wait_for(lambda: layout() == "master", "the layout an action on the output names")
    assert desktop.rows("layout")[0][0] == "master", desktop.rows("layout")
    msg("workspace", "2")
    wait_for(lambda: layout() == "spiral", "a workspace never switched in the configured layout")
    msg("workspace", "1")
    wait_for(lambda: layout() == "master", "the first workspace's layout again")
print("Subscribers hear the layout of each output's workspace as it changes")
