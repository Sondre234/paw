# SPDX-License-Identifier: GPL-3.0-or-later
"""Tiling is a setting of each output: outputs.monitors overrides layout.tiling, toggling one
output leaves the others alone, and a reload applies only tiling settings that changed."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])


# The pointer starts at the origin, on HEADLESS-1 until HEADLESS-2 is made primary, which moves
# HEADLESS-2 under it, so the next window opens there.
def config(primary="HEADLESS-1", first="tiling = true", second=""):
    return f"""return {{
    xwayland = false,
    layout = {{ tiling = false }},
    outputs = {{
        primary = "{primary}",
        monitors = {{
            ["HEADLESS-1"] = {{ mode = "1280x720", {first} }},
            ["HEADLESS-2"] = {{ mode = "1280x720", {second} }},
        }},
    }},
}}"""


with harness.Compositor(compositor, config(), env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    msg = desktop.msg

    def tiling():
        """Whether each output tiles, from `get workspaces`."""
        return {row[0]: row[4] == "on" for row in desktop.rows("workspaces")}

    def windows():
        """(tiled, output) per window, oldest first."""
        return [(w.tiled, w.output) for w in desktop.windows()]

    desktop.detail = lambda: f"windows: {windows()}, tiling: {tiling()}"

    def launch():
        desktop.spawn([probe, "--external-control"])

    # HEADLESS-1 has its own setting; HEADLESS-2 follows layout.tiling.
    assert tiling() == {"HEADLESS-1": True, "HEADLESS-2": False}, tiling()
    assert msg("get", "tiling") == "on\n"
    for count in (1, 2):
        launch()
        desktop.wait_for(lambda: len(windows()) == count, f"window {count} mapped")
    desktop.wait_for(lambda: windows() == [(True, "HEADLESS-1")] * 2, "tiled on HEADLESS-1")

    # A window opening on HEADLESS-2 floats there. The reload changes no tiling
    # setting, so neither output changes.
    desktop.reload(config(primary="HEADLESS-2"))
    launch()
    desktop.wait_for(lambda: len(windows()) == 3, "window 3 mapped")
    desktop.wait_for(lambda: windows()[2] == (False, "HEADLESS-2"), "floating on HEADLESS-2")
    assert windows()[:2] == [(True, "HEADLESS-1")] * 2, windows()
    assert msg("get", "tiling") == "off\n", "get tiling should follow the focused output"

    # Toggling one output leaves the other as it was.
    msg("output", "HEADLESS-2", "toggle_tiling")
    desktop.wait_for(lambda: windows()[2] == (True, "HEADLESS-2"), "tiled on HEADLESS-2")
    assert tiling() == {"HEADLESS-1": True, "HEADLESS-2": True}, tiling()
    assert windows()[:2] == [(True, "HEADLESS-1")] * 2, windows()
    msg("output", "HEADLESS-1", "toggle_tiling")
    desktop.wait_for(lambda: windows() == [(False, "HEADLESS-1")] * 2 + [(True, "HEADLESS-2")],
                     "HEADLESS-1 floating, HEADLESS-2 still tiled")

    # Toggled outputs keep their state while their setting stays the same...
    desktop.reload(config(primary="HEADLESS-2", second="tiling = false"))
    assert tiling() == {"HEADLESS-1": False, "HEADLESS-2": True}, tiling()
    # ...and follow it when it changes.
    desktop.reload(config(primary="HEADLESS-2", first="tiling = false", second="tiling = false"))
    desktop.reload(config(primary="HEADLESS-2", first="tiling = true", second="tiling = false"))
    desktop.wait_for(lambda: windows() == [(True, "HEADLESS-1")] * 2 + [(True, "HEADLESS-2")],
                     "HEADLESS-1 tiled again by its changed setting")
    desktop.reload(config(primary="HEADLESS-2", first="tiling = true", second="tiling = true"))
    assert tiling() == {"HEADLESS-1": True, "HEADLESS-2": True}, tiling()
    desktop.reload(config(primary="HEADLESS-2", first="tiling = true", second="tiling = false"))
    desktop.wait_for(lambda: windows()[2] == (False, "HEADLESS-2"),
                     "HEADLESS-2 floating by its changed setting")
    assert windows()[:2] == [(True, "HEADLESS-1")] * 2, windows()
print("Per-output tiling settings, toggles, and reloads passed")
