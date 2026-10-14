# SPDX-License-Identifier: GPL-3.0-or-later
"""The queries `paw tour` reads, on a headless compositor: every action and the keys of its
first key binding, the appearance profiles, and the terminal; and the caption it would show,
refused without the shell."""
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
    assert desktop.rows("terminal") == [["foot", "--app-id", "a terminal"]], \
        desktop.rows("terminal")

    # A caption needs the shell to show it.
    assert "no shell" in msg("caption", "5", "Title\tBody\tSuper + S", ok=False)
    for bad in (["caption"], ["caption", "0", "Title"], ["caption", "601", "Title"],
                ["caption", "5"], ["caption", "soon", "Title"], ["caption", "5", "\tBody"]):
        assert "usage" in msg(*bad, ok=False), bad
    msg("caption", "hide")
