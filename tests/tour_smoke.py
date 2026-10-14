# SPDX-License-Identifier: GPL-3.0-or-later
"""The queries `paw tour` reads, on a headless compositor: every action and the keys of its
first key binding, and the appearance profiles."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    layout = { workspaces = 4 },
    terminal = { "foot", "--app-id", "a terminal" },
    profile = "day",
    profiles = { day = {}, night = { appearance = { background = "#101020" } } },
    bindings = {
        { mods = { "Super" }, key = "Tab", action = "workspace_back" },
        { mods = { "Super", "Shift" }, key = "minus", action = "move_to_scratchpad" },
    },
}"""

with harness.Compositor(compositor, CONFIG) as desktop:
    msg = desktop.msg

    # What the tour reads.
    actions = {row[0]: row[1] for row in desktop.rows("actions")}
    assert actions["workspace_back"] == "Super + Tab", actions.get("workspace_back")
    assert actions["move_to_scratchpad"] == "Super + Shift + minus"
    assert actions["toggle_tiling"] == "-", "unbound here"
    assert len(actions) > 100 and all(len(row) == 2 for row in desktop.rows("actions"))
    assert desktop.rows("profiles") == [["day", "1"], ["night", "0"]], desktop.rows("profiles")
