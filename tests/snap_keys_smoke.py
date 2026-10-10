# SPDX-License-Identifier: GPL-3.0-or-later
"""Snapping from the keyboard: the quarter actions put the focused window into a quarter of its
monitor, and restore puts it back; the snap_cycle actions step it as Windows' Win+arrows do,
through the halves, the quarters and maximized, back to its own size, on to the next monitor
and down to minimized, and out of fullscreen first."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    animations = { enabled = false },
    layout = { tiling = false, gap = 0 },
    outputs = { order = { "HEADLESS-1", "HEADLESS-2" },
                monitors = { ["HEADLESS-1"] = { mode = "1280x720" },
                             ["HEADLESS-2"] = { mode = "800x600" } } },
}"""

with harness.Compositor(compositor, CONFIG, env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    msg = desktop.msg

    def windows():
        """By title: x, y, width, height, minimized, output."""
        return {w.title: (w.x, w.y, w.width, w.height, w.minimized, w.output)
                for w in desktop.windows()}

    def placed(box, title="W"):
        desktop.wait_for(lambda: windows()[title][:4] == tuple(box), f"{title} at {box}")

    desktop.detail = lambda: f"windows: {windows()}"
    desktop.spawn([probe, "--window-only"], env={"PAW_PROBE_TITLE": "W"})
    desktop.wait_for(lambda: "W" in windows(), "window mapped")
    floating = windows()["W"][:4]
    assert floating[2:] == (320, 240) and windows()["W"][5] == "HEADLESS-1", windows()

    # The quarters, each from the last, and back.
    for action, box in [("snap_top_left", (0, 0, 640, 360)),
                        ("snap_top_right", (640, 0, 640, 360)),
                        ("snap_bottom_left", (0, 360, 640, 360)),
                        ("snap_bottom_right", (640, 360, 640, 360))]:
        msg(action)
        placed(box)
    msg("restore")
    placed(floating)

    # Win+arrows on two monitors side by side, the second 800 x 600 on the right.
    on_second = (1280 + floating[0], floating[1], 320, 240)
    for action, box in [("left", (0, 0, 640, 720)),
                        ("up", (0, 0, 640, 360)),
                        ("up", (0, 0, 1280, 720)),
                        ("up", (0, 0, 1280, 720)),            # maximized already
                        ("down", floating),                    # back to its own size
                        ("right", (640, 0, 640, 720)),
                        ("right", (1280, 0, 400, 600)),        # on to the next monitor
                        ("right", on_second),                  # its own size, on that monitor
                        ("right", (1680, 0, 400, 600)),
                        ("right", (1680, 0, 400, 600)),        # none further right
                        ("left", on_second),
                        ("left", (1280, 0, 400, 600)),
                        ("left", (640, 0, 640, 720)),          # back to the first monitor
                        ("down", (640, 360, 640, 360)),
                        ("left", (0, 360, 640, 360)),          # quarters keep their row
                        ("left", (0, 360, 640, 360)),          # none further left
                        ("up", (0, 0, 640, 720)),
                        ("right", floating)]:
        msg("snap_cycle_" + action)
        placed(box)
    assert windows()["W"][5] == "HEADLESS-1" and not windows()["W"][4], windows()
    msg("snap_cycle_right")
    msg("snap_cycle_down")
    placed((640, 360, 640, 360))
    msg("snap_cycle_down")
    desktop.wait_for(lambda: windows()["W"][4], "a bottom quarter minimizes")

    # Down minimizes a window at its own size; a fullscreen one leaves fullscreen first.
    desktop.spawn([probe, "--window-only"], env={"PAW_PROBE_TITLE": "V"})
    desktop.wait_for(lambda: "V" in windows(), "V mapped")
    msg("fullscreen")
    desktop.wait_for(lambda: windows()["V"][2:4] == (1280, 720), "V fullscreen")
    msg("snap_cycle_right")
    placed((640, 0, 640, 720), "V")
    msg("snap_cycle_left")
    desktop.wait_for(lambda: windows()["V"][2:4] == (320, 240), "V at its own size")
    msg("snap_cycle_down")
    desktop.wait_for(lambda: windows()["V"][4], "V minimized")
print("Snapping from the keyboard passed")
