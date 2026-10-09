# SPDX-License-Identifier: GPL-3.0-or-later
"""A window spawned by a button binding opens centered on the click, kept on screen; one spawned
otherwise, or a later one, is placed as usual. Driven by a virtual pointer (pointer_probe)."""
from pathlib import Path
import sys

import harness

compositor, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

SCREEN = (1280, 720)

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    bindings = {
        { button = "middle", desktop = true, action = "spawn",
          command = { "env", "PAW_PROBE_TITLE=S", "%s", "--window-only" } },
    },
}""" % probe

with harness.Compositor(compositor, CONFIG) as desktop:
    msg = desktop.msg

    def windows():
        """By title: x, y, width, height."""
        return {r[9]: tuple(int(n) for n in r[4:8]) for r in desktop.rows("windows")}

    desktop.detail = lambda: f"windows: {windows()}"
    wait_for = desktop.wait_for
    pointer = desktop.virtual_pointer(pointer_probe, *SCREEN)

    def spawned_at(x, y):
        """Middle-clicks the desktop at (x, y) and returns where S opened, then closes it."""
        pointer("move", str(x), str(y), "click", "middle")
        wait_for(lambda: "S" in windows(), "spawned window mapped")
        place = windows()["S"]
        msg("close")
        wait_for(lambda: "S" not in windows(), "spawned window closed")
        return place

    # Centered on the click.
    assert spawned_at(900, 400) == (740, 280, 320, 240), windows()
    # Near a corner it stays on screen.
    assert spawned_at(1270, 710) == (960, 480, 320, 240), windows()
    assert spawned_at(5, 5) == (0, 0, 320, 240), windows()

    # A window opened otherwise cascades as usual, wherever the pointer is.
    pointer("move", "900", "400")
    desktop.spawn([probe, "--window-only"], env={"PAW_PROBE_TITLE": "O"})
    wait_for(lambda: "O" in windows(), "other window mapped")
    assert windows()["O"][:2] != (740, 280), windows()
print("Windows spawned by a button binding open at the pointer")
