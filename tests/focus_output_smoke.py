# SPDX-License-Identifier: GPL-3.0-or-later
"""focus_left/right, as sway's focus: with no window that way, focus moves on to the next
output, so an empty output can be reached from the keyboard and new windows open there."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

# HEADLESS-1 sits left of HEADLESS-2.
CONFIG = """return {
    xwayland = false,
    layout = { tiling = false },
    outputs = {
        order = { "HEADLESS-1", "HEADLESS-2" },
        monitors = {
            ["HEADLESS-1"] = { mode = "1280x720" },
            ["HEADLESS-2"] = { mode = "1280x720" },
        },
    },
}"""

with harness.Compositor(compositor, CONFIG, env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    msg = desktop.msg

    def windows():
        """(focused, output) per window, by output (one window per output here)."""
        return sorted(((r[1] == "1", r[10]) for r in desktop.rows("windows")), key=lambda w: w[1])

    def focused_output():
        return next(r[0] for r in desktop.rows("workspaces") if r[2] == "1")

    desktop.detail = lambda: f"windows: {windows()}, output: {focused_output()}"

    def open_window():
        count = len(windows())
        desktop.spawn([probe, "--external-control"])
        desktop.wait_for(lambda: len(windows()) == count + 1, f"window {count + 1} mapped")

    open_window()
    desktop.wait_for(lambda: windows() == [(True, "HEADLESS-1")], "A focused on HEADLESS-1")

    # No window to the right: focus moves to the empty HEADLESS-2, and the next window
    # opens there.
    msg("focus_right")
    desktop.wait_for(lambda: focused_output() == "HEADLESS-2" and not windows()[0][0],
                     "empty HEADLESS-2 focused")
    open_window()
    desktop.wait_for(lambda: windows()[1] == (True, "HEADLESS-2"), "B focused on HEADLESS-2")

    # Between windows on the two outputs, as before.
    msg("focus_left")
    desktop.wait_for(lambda: windows() == [(True, "HEADLESS-1"), (False, "HEADLESS-2")],
                     "A focused")
    msg("focus_right")
    desktop.wait_for(lambda: windows() == [(False, "HEADLESS-1"), (True, "HEADLESS-2")],
                     "B focused")
    # Past the last output nothing happens.
    msg("focus_right")
    assert windows()[1][0] and focused_output() == "HEADLESS-2", windows()

    # From an empty output with nothing focused, back to the window on the other.
    desktop.clients.pop().kill()
    desktop.wait_for(lambda: windows() == [(True, "HEADLESS-1")], "B closed")
    msg("focus_right")
    desktop.wait_for(lambda: focused_output() == "HEADLESS-2" and not windows()[0][0],
                     "empty HEADLESS-2 focused again")
    msg("focus_left")
    desktop.wait_for(lambda: windows() == [(True, "HEADLESS-1")], "A focused from empty output")
print("Focus moved between windows and onto an empty output")
