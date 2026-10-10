# SPDX-License-Identifier: GPL-3.0-or-later
"""Drive workspaces through the control socket; taskbar switching is workspace_back_smoke's."""
from pathlib import Path
import sys

import harness

compositor, probe, example = (str(Path(p).resolve()) for p in sys.argv[1:4])

source = Path(example).read_text().replace("xwayland = true", "xwayland = false")

with harness.Compositor(compositor, source) as desktop:
    msg = desktop.msg

    def windows():
        return {w.app_id: (w.workspace, w.focused) for w in desktop.windows()}

    assert msg("get", "workspace") == "1\n"
    assert "needs a workspace from 1 to 4" in msg("workspace", "9", ok=False)
    assert "unknown action" in msg("bogus", ok=False)
    assert "takes no argument" in msg("close", "2", ok=False)

    window = desktop.spawn([probe, "--external-control"])
    desktop.wait_for(lambda: windows().get("paw-probe") == (1, True),
                     "window focused on workspace 1")

    msg("move_to_workspace", "2")
    assert windows()["paw-probe"] == (2, False), windows()
    assert msg("get", "workspace") == "1\n"
    msg("workspace", "2")
    assert msg("get", "workspace") == "2\n"
    assert windows()["paw-probe"] == (2, True), windows()
    msg("workspace_next")
    assert msg("get", "workspace") == "3\n"
    assert windows()["paw-probe"] == (2, False)
    msg("workspace_prev")
    msg("workspace_prev")
    assert msg("get", "workspace") == "1\n"

    # Keyboard window actions target only the current workspace.
    msg("workspace", "1")
    msg("close")
    assert window.poll() is None, "close reached a window on another workspace"
    msg("workspace", "2")
    msg("close")
    assert desktop.reap(window) == 0
    assert windows() == {}
print("Workspaces, control socket, and scoped actions passed")
