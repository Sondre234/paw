# SPDX-License-Identifier: GPL-3.0-or-later
"""move_to_workspace_next, _prev and _empty take the focused window to the workspace beside its
own, wrapping around, or to the first empty one, and the monitor shows it there."""
from pathlib import Path
import sys

import harness

compositor, probe, example = (str(Path(p).resolve()) for p in sys.argv[1:4])

source = Path(example).read_text().replace("xwayland = true", "xwayland = false")

KEY_LEFTCTRL, KEY_LEFTALT, KEY_LEFT, KEY_LEFTMETA = 29, 56, 105, 125

with harness.Compositor(compositor, source) as desktop:
    msg = desktop.msg

    def where():
        """The shown workspace, and each window's title: (workspace, focused)."""
        return (int(msg("get", "workspace")),
                {w.title: (w.workspace, w.focused) for w in desktop.windows()})

    assert "takes no argument" in msg("move_to_workspace_next", "2", ok=False)

    desktop.open_window(probe, "a", focused=True)
    desktop.open_window(probe, "b", focused=True)
    assert where() == (1, {"a": (1, False), "b": (1, True)}), where()

    # The view goes along, and the window keeps the focus there.
    msg("move_to_workspace_next")
    assert where() == (2, {"a": (1, False), "b": (2, True)}), where()

    # The default binding: Super + Ctrl + Alt + Left.
    press = desktop.keyboard()
    press(KEY_LEFTMETA, KEY_LEFTCTRL, KEY_LEFTALT, KEY_LEFT)
    desktop.wait_for(lambda: where() == (1, {"a": (1, False), "b": (1, True)}),
                     "Super + Ctrl + Alt + Left took the window back", detail=where)

    # Both ways wrap around, as workspace_next and workspace_prev do.
    msg("move_to_workspace_prev")
    assert where() == (4, {"a": (1, False), "b": (4, True)}), where()
    msg("move_to_workspace_next")
    assert where() == (1, {"a": (1, False), "b": (1, True)}), where()

    # The first workspace with no windows on it, the window itself counting where it is.
    msg("move_to_workspace_empty")
    assert where() == (2, {"a": (1, False), "b": (2, True)}), where()
    msg("move_to_workspace_empty")
    assert where() == (3, {"a": (1, False), "b": (3, True)}), where()
    desktop.open_window(probe, "c", focused=True)
    msg("move_to_workspace_empty")
    assert where() == (2, {"a": (1, False), "b": (3, False), "c": (2, True)}), where()
    desktop.open_window(probe, "d", focused=True)
    msg("move_to_workspace_empty")
    full = (4, {"a": (1, False), "b": (3, False), "c": (2, False), "d": (4, True)})
    assert where() == full, where()

    # With a window on every workspace, there is none to go to.
    msg("move_to_workspace_empty")
    assert where() == full, where()
print("Moving a window to the next, previous and an empty workspace passed")
