# SPDX-License-Identifier: GPL-3.0-or-later
"""Sticky windows: shown on every workspace of their output, and unstuck by moving them to one
workspace or by turning features.sticky off."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe, example = (str(Path(p).resolve()) for p in sys.argv[1:4])

source = (Path(example).read_text().replace("xwayland = true", "xwayland = false")
          .replace("tiling = false,", "tiling = true,", 1))
assert "sticky = true," in source

with harness.Compositor(compositor, source) as desktop:
    msg = desktop.msg

    def window():
        """(workspace, focused, tiled, visible, sticky) of the only window."""
        rows = desktop.rows("windows")
        assert len(rows) == 1 and len(rows[0]) == 16, rows
        row = rows[0]
        return (int(row[0]), row[1] == "1", row[3] == "1", row[11] == "1", row[13] == "1")

    def workspace():
        return int(msg("get", "workspace"))

    assert "takes no argument" in msg("toggle_sticky", "1", ok=False)

    client = desktop.spawn([probe, "--external-control"])
    desktop.wait_for(lambda: msg("get", "windows").count("\n") == 1 and window()[2],
                     "window tiled on workspace 1")
    assert window() == (1, True, True, True, False), window()

    # A sticky tile floats and follows every workspace switch, keeping focus.
    msg("toggle_sticky")
    assert window() == (1, True, False, True, True), window()
    msg("workspace", "2")
    assert workspace() == 2
    assert window() == (2, True, False, True, True), window()
    msg("workspace_next")
    assert window() == (3, True, False, True, True), window()
    assert msg("get", "workspaces").split("\t")[3] == "3", msg("get", "workspaces")

    # Activating it from the taskbar leaves the workspace alone.
    subprocess.run([probe, "--activate", "paw-probe"], env=desktop.env, check=True,
                   timeout=30, stdout=subprocess.DEVNULL)
    assert workspace() == 3 and window() == (3, True, False, True, True), window()

    # Unsticking it leaves it on this workspace, tiled again as before.
    msg("toggle_sticky")
    assert window() == (3, True, True, True, False), window()
    msg("workspace", "1")
    assert window() == (3, False, True, False, False), window()
    msg("workspace", "3")

    # Moving a sticky window to a workspace unsticks it there.
    msg("toggle_sticky")
    msg("workspace", "2")
    assert window() == (2, True, False, True, True), window()
    msg("move_to_workspace", "4")
    assert window() == (4, False, True, False, False), window()
    assert workspace() == 2
    msg("workspace", "4")
    assert window() == (4, True, True, True, False), window()

    # Turning the feature off unsticks windows where they are, and disables the action.
    msg("toggle_sticky")
    msg("workspace", "1")
    assert window() == (1, True, False, True, True), window()
    desktop.reload(source.replace("sticky = true,", "sticky = false,"))
    assert "Configuration reloaded" in desktop.log.read_text()
    assert window() == (1, True, True, True, False), window()
    msg("toggle_sticky")
    assert window() == (1, True, True, True, False), window()
    msg("workspace", "2")
    assert window() == (1, False, True, False, False), window()

    # And back on, the action works again.
    desktop.reload(source)
    msg("workspace", "1")
    msg("toggle_sticky")
    assert window() == (1, True, False, True, True), window()

    msg("close")
    assert desktop.reap(client) == 0
    assert msg("get", "windows") == ""
print("Sticky windows follow workspaces, unstick on move, and obey features.sticky")
