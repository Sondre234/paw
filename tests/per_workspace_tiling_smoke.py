# SPDX-License-Identifier: GPL-3.0-or-later
"""With layout.tiling_per_workspace, toggling tiling changes only the current workspace, windows
moved between workspaces tile or float as their new workspace does, and turning the setting off
puts every workspace back on the output's setting."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])


def config(per_workspace=True):
    return f"""return {{
    xwayland = false,
    layout = {{ tiling = true, tiling_per_workspace = {str(per_workspace).lower()} }},
}}"""


with harness.Compositor(compositor, config()) as desktop:
    msg = desktop.msg

    def windows():
        """(workspace, tiled) per window, oldest first."""
        rows = desktop.rows("windows")
        return [(int(row[0]), row[3] == "1") for row in rows]

    desktop.detail = lambda: f"windows: {windows()}, tiling: {msg('get', 'tiling')}"

    def launch():
        count = len(windows()) + 1
        desktop.spawn([probe, "--external-control"])
        desktop.wait_for(lambda: len(windows()) == count, f"window {count} mapped")

    launch()
    launch()
    desktop.wait_for(lambda: windows() == [(1, True)] * 2, "tiled on workspace 1")

    # Workspace 2 starts as the output is, and toggles on its own.
    msg("workspace", "2")
    launch()
    desktop.wait_for(lambda: windows()[2] == (2, True), "tiled on workspace 2")
    msg("toggle_tiling")
    desktop.wait_for(lambda: windows()[2] == (2, False), "floating on workspace 2")
    assert msg("get", "tiling") == "off\n"
    assert windows()[:2] == [(1, True)] * 2, windows()
    msg("workspace", "1")
    assert msg("get", "tiling") == "on\n", "workspace 1 should still tile"

    # A tile moved to workspace 2 floats there; a window moved back tiles again.
    msg("move_to_workspace", "2")
    desktop.wait_for(lambda: sorted(windows()) == [(1, True), (2, False), (2, False)],
                     "moved tile floating on workspace 2")
    msg("workspace", "2")
    msg("move_to_workspace", "1")
    desktop.wait_for(lambda: sorted(windows()) == [(1, True), (1, True), (2, False)],
                     "moved window tiled on workspace 1")

    # Without the setting every workspace follows the output, and toggling changes all.
    desktop.reload(config(per_workspace=False))
    desktop.wait_for(lambda: windows() == [(w, True) for w, _ in windows()],
                     "every workspace tiled by the output's setting")
    msg("toggle_tiling")
    desktop.wait_for(lambda: not any(tiled for _, tiled in windows()), "every window floating")
print("Per-workspace tiling toggles, moves, and reloads passed")
