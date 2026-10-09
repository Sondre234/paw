# SPDX-License-Identifier: GPL-3.0-or-later
"""focus_last focuses the window focused before the current one, flipping between two windows
when repeated, also across workspaces."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false, workspaces = 4 },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
}"""

with harness.Compositor(compositor, CONFIG) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def windows():
        rows = desktop.rows("windows")
        return {r[9]: dict(workspace=int(r[0]), focused=r[1] == "1", minimized=r[2] == "1")
                for r in rows}

    def focused():
        return next((t for t, w in windows().items() if w["focused"]), None)

    def workspace():
        return int(msg("get", "workspace"))

    desktop.detail = lambda: f"windows: {windows()}"

    msg("focus_last")  # no windows: nothing happens
    for title in ("A", "B", "C"):
        desktop.spawn([probe, "--window-only"], env={"PAW_PROBE_TITLE": title})
        wait_for(lambda: focused() == title, f"{title} focused")

    # C was focused after B: the history is C, B, A.
    msg("focus_last")
    wait_for(lambda: focused() == "B", "back to B")
    msg("focus_last")
    wait_for(lambda: focused() == "C", "flip back to C")
    msg("focus_last")
    wait_for(lambda: focused() == "B", "flip to B again")

    # Across workspaces: B moves to workspace 3, where focus_last follows it.
    msg("move_to_workspace", "3")
    wait_for(lambda: windows()["B"]["workspace"] == 3, "B on workspace 3")
    wait_for(lambda: focused() != "B", "B lost focus")
    first = focused()
    msg("focus_last")
    wait_for(lambda: focused() == "B" and workspace() == 3, "B, on its workspace")
    msg("focus_last")
    wait_for(lambda: focused() == first and workspace() == 1, "back to the first")
print("focus_last passed")
