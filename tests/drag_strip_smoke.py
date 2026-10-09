# SPDX-License-Identifier: GPL-3.0-or-later
"""windows.drag_strip: dragging the top of a server-decorated window moves it, within the strip
the setting gives (6 pixels by default), and lower down the press reaches the application.
Driven by a virtual pointer (pointer_probe)."""
from pathlib import Path
import sys

import harness

compositor, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

SCREEN = (1280, 720)


def config(strip=None):
    return """return {
    xwayland = false,
    layout = { tiling = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    windows = { magnet = { enabled = false }, %s },
}""" % ("drag_strip = %d," % strip if strip is not None else "")


with harness.Compositor(compositor, config()) as desktop:
    msg = desktop.msg

    def where():
        return {r[9]: (int(r[4]), int(r[5])) for r in desktop.rows("windows")}["W"]

    desktop.detail = lambda: msg("get", "windows")
    pointer = desktop.virtual_pointer(pointer_probe, *SCREEN)

    def drag_from(depth):
        """Drags 100 pixels right from `depth` pixels below W's top; returns how far it went."""
        x, y = where()
        pointer("move", str(x + 100), str(y + depth), "press", "left",
                "move", str(x + 150), str(y + depth), "move", str(x + 200), str(y + depth),
                "release", "left")
        return where()[0] - x

    desktop.spawn([probe, "--window-only"],
                  env={"PAW_PROBE_TITLE": "W", "PAW_PROBE_SSD": "1"})
    desktop.wait_for(lambda: "W" in msg("get", "windows"), "window mapped")

    # The default strip: its top 6 pixels move it, 20 pixels down does not.
    assert drag_from(3) == 100, where()
    assert drag_from(20) == 0, where()

    # A deeper strip catches the press 20 pixels down.
    desktop.reload(config(24))
    assert drag_from(20) == 100, where()
    assert drag_from(30) == 0, where()

    # 0 turns it off.
    desktop.reload(config(0))
    assert drag_from(2) == 0, where()
print("The drag strip moves windows by their top, as deep as windows.drag_strip says")
