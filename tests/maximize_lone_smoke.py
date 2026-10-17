# SPDX-License-Identifier: GPL-3.0-or-later
"""layout.maximize_lone_on_untile: turning tiling off maximizes a tile alone on its workspace, on
every workspace the toggle reaches, instead of floating it back; restoring it floats it where it
floated before, and turning tiling on tiles it again. Tiles with a neighbour float back as
before, and so does every tile with the setting off."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])
WIDTH, HEIGHT, BORDER = 1280, 720, 2
PROBE_WIDTH, PROBE_HEIGHT = 320, 240  # what a probe draws when the size is left to it

CONFIG = """return {
    xwayland = false,
    layout = { tiling = true, maximize_lone_on_untile = %s },
    windows = { border_width = %d },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "%dx%d" } } },
}"""


def config(maximize):
    return CONFIG % (maximize, BORDER, WIDTH, HEIGHT)


# A lone tile fills the output (the probes draw no panel) inside its border; maximized on a
# monitor that does not tile, a window has no border.
ALONE = (BORDER, BORDER, WIDTH - 2 * BORDER, HEIGHT - 2 * BORDER)
FULL = (0, 0, WIDTH, HEIGHT)

with harness.Compositor(compositor, config("true")) as desktop:
    msg = desktop.msg

    def windows():
        """(tiled, x, y, width, height) per window, oldest first."""
        return [(w.tiled, *w.box) for w in desktop.windows()]

    def floating_back(count):
        """`count` windows, none tiled and each at the size it floated at, not maximized."""
        current = windows()
        return len(current) == count and all(
            not w[0] and w[3:] == (PROBE_WIDTH, PROBE_HEIGHT) for w in current)

    def toggle(state):
        msg("toggle_tiling")
        desktop.wait_for(lambda: msg("get", "tiling") == state + "\n", "tiling " + state)

    desktop.detail = lambda: f"windows: {windows()}"

    desktop.open_window(probe, "one")
    desktop.wait_for(lambda: windows() == [(True, *ALONE)], "one tiled alone")

    # Alone on its workspace, the tile is maximized as tiling goes off. It opened tiled, so it
    # has no floating size: restored, it floats where its tile was, at the size it chooses.
    toggle("off")
    desktop.wait_for(lambda: windows() == [(False, *FULL)], "lone tile maximized")
    msg("restore")
    desktop.wait_for(lambda: windows() == [(False, BORDER, BORDER, PROBE_WIDTH, PROBE_HEIGHT)],
                     "restored where its tile was")
    floating = windows()

    # Tiling on tiles it again; off maximizes it again, and restoring it puts it back where it
    # floated.
    toggle("on")
    desktop.wait_for(lambda: windows() == [(True, *ALONE)], "tiled again")
    toggle("off")
    desktop.wait_for(lambda: windows() == [(False, *FULL)], "maximized again")
    msg("restore")
    desktop.wait_for(lambda: windows() == floating, "floating where it was")

    # Two windows on the workspace: both float back.
    toggle("on")
    desktop.open_window(probe, "two")
    desktop.wait_for(lambda: [w[0] for w in windows()] == [True, True], "two tiles")
    toggle("off")
    desktop.wait_for(lambda: floating_back(2), "both floating back")
    desktop.stays(lambda: floating_back(2), "neither maximized")

    # Toggling tiling reaches every workspace of the monitor: a lone tile on each is maximized.
    toggle("on")
    desktop.wait_for(lambda: [w[0] for w in windows()] == [True, True], "both tiled again")
    msg("move_to_workspace", "2")
    desktop.wait_for(lambda: windows() == [(True, *ALONE)] * 2 and
                     len({w.workspace for w in desktop.windows()}) == 2,
                     "each alone on a workspace of its own")
    toggle("off")
    desktop.wait_for(lambda: windows() == [(False, *FULL)] * 2, "both maximized")

    # Without the setting a lone tile floats back.
    toggle("on")
    desktop.wait_for(lambda: windows() == [(True, *ALONE)] * 2, "both tiled once more")
    desktop.reload(config("false"))
    toggle("off")
    desktop.wait_for(lambda: floating_back(2), "lone tiles floating back without the setting")
    desktop.stays(lambda: floating_back(2), "neither maximized without the setting")
print("A lone tile is maximized as tiling turns off, with layout.maximize_lone_on_untile")
