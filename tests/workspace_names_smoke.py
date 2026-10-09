# SPDX-License-Identifier: GPL-3.0-or-later
"""Workspaces named in layout.workspace_names are reached by name from the control socket; an
unknown name is refused."""
from pathlib import Path
import sys

import harness

compositor = str(Path(sys.argv[1]).resolve())

CONFIG = """return {
    xwayland = false,
    layout = { workspaces = 4, workspace_names = { "web", "code", "", "chat room" } },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
}"""

with harness.Compositor(compositor, CONFIG) as desktop:

    def msg(*words, ok=True):
        result = desktop.msg(*words, ok=ok)
        return result.strip() if ok else result

    assert msg("get", "workspace") == "1"
    msg("workspace", "code")
    assert msg("get", "workspace") == "2"
    msg("workspace", "chat", "room")
    assert msg("get", "workspace") == "4"
    msg("workspace", "web")
    assert msg("get", "workspace") == "1"
    msg("workspace", "3")
    assert msg("get", "workspace") == "3"
    assert "workspace name" in msg("workspace", "nope", ok=False)
    assert "workspace name" in msg("workspace", ok=False)
    assert "workspace name" in msg("move_to_workspace", "9", ok=False)
    assert msg("get", "workspace") == "3"
print("workspace names passed")
