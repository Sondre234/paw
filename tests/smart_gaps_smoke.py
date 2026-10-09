# SPDX-License-Identifier: GPL-3.0-or-later
"""layout.smart_gaps: a tiled window alone on its workspace has no gaps and fills the usable area
inside its border, from its first configure on; a second tile brings the gaps back for both, and
they go again as it closes or floats. Turned off, a lone tile keeps them."""
from pathlib import Path
import re
import subprocess
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])
CONFIGURE = re.compile(r"xdg_toplevel#\d+\.configure\((-?\d+), (-?\d+),")
WIDTH, HEIGHT, BORDER, INNER, OUTER = 1280, 720, 2, 8, 10

CONFIG = """return {
    xwayland = false,
    layout = { tiling = true, gap_inner = %d, gap_outer = %d%s },
    windows = { border_width = %d },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "%dx%d" } } },
}"""


def config(extra=""):
    return CONFIG % (INNER, OUTER, extra, BORDER, WIDTH, HEIGHT)


# A lone tile: the whole output (the probes draw no panel) less the border.
ALONE = (BORDER, BORDER, WIDTH - 2 * BORDER, HEIGHT - 2 * BORDER)
EDGE = OUTER + BORDER
# With the gaps: two tiles side by side, gap_outer at the edges and gap_inner between them.
LEFT_EDGE, RIGHT_EDGE = EDGE, WIDTH - EDGE

with harness.Compositor(compositor, config()) as desktop:
    root, msg = desktop.root, desktop.msg

    def windows():
        """(x, y, width, height) per window, oldest first."""
        return [tuple(map(int, row[4:8])) for row in desktop.rows("windows")]

    def focused():
        """The index of the focused window, oldest first."""
        return [row[1] for row in desktop.rows("windows")].index("1")

    def configures(index):
        """The sizes of every xdg_toplevel.configure the `index`th window received."""
        text = (root / f"probe{index}.log").read_text()
        return [(int(m[1]), int(m[2])) for m in CONFIGURE.finditer(text)]

    desktop.detail = lambda: f"windows: {windows()}"

    def launch():
        index = len(desktop.clients)
        process = desktop.spawn([probe, "--window-only"], env={"WAYLAND_DEBUG": "client"},
                                stderr=(root / f"probe{index}.log").open("w"))
        desktop.wait_for(lambda: len(windows()) == index + 1 and
                         configures(index) and configures(index)[-1] == windows()[index][2:],
                         f"window {index} tiled")
        return process

    def gapped(left, right):
        """Whether two tiles stand side by side inside the gaps."""
        return (left[0] == LEFT_EDGE and left[1] == EDGE and right[1] == EDGE and
                right[0] + right[2] == RIGHT_EDGE and left[3] == right[3] == HEIGHT - 2 * EDGE and
                right[0] - (left[0] + left[2]) == INNER + 2 * BORDER)

    # Alone, the first window fills the output, and its first configure already says so.
    first = launch()
    desktop.wait_for(lambda: windows() == [ALONE], "lone tile fills the output")
    assert configures(0)[0] == ALONE[2:], configures(0)

    # A second tile brings the gaps back for both; it too is configured at its tile at once.
    second = launch()
    desktop.wait_for(lambda: len(windows()) == 2 and gapped(*sorted(windows())),
                     "gaps around two tiles")
    assert configures(1)[0] == windows()[1][2:], (configures(1), windows())

    # Moving a tile across the other, which is alone in the tiling meanwhile, keeps the gaps.
    before = windows()
    moved = focused()
    msg("move_left" if before[moved][0] > before[1 - moved][0] else "move_right")
    desktop.wait_for(lambda: windows() == before[::-1], "tiles traded places inside the gaps")

    # Floating the second leaves the first alone in the tiling: no gaps again.
    msg("toggle_floating")
    desktop.wait_for(lambda: windows()[0] == ALONE, "lone tile beside a floating window")
    msg("toggle_floating")
    desktop.wait_for(lambda: gapped(*sorted(windows())), "gaps back as it tiles again")

    # Two windows in monocle each fill the area inside the gaps.
    framed = (EDGE, EDGE, WIDTH - 2 * EDGE, HEIGHT - 2 * EDGE)
    msg("layout_monocle")
    desktop.wait_for(lambda: windows() == [framed, framed], "monocle tiles inside the gaps")
    msg("layout_dwindle")
    desktop.wait_for(lambda: gapped(*sorted(windows())), "dwindle again")

    # Closing the second drops the gaps.
    assert focused() == 1, desktop.rows("windows")
    msg("close")
    assert desktop.reap(second) == 0
    desktop.wait_for(lambda: windows() == [ALONE], "gaps gone with the other tile")

    # A lone column of the scroll layout keeps its width but has no gaps either.
    msg("layout_scroll")
    desktop.wait_for(lambda: windows()[0][2] < WIDTH // 2, "lone column")
    assert windows()[0][1::2] == (BORDER, HEIGHT - 2 * BORDER), windows()
    msg("layout_dwindle")
    desktop.wait_for(lambda: windows() == [ALONE], "dwindle again, alone")

    # Turned off, a lone tile keeps gap_outer.
    desktop.reload(config(", smart_gaps = false"))
    desktop.wait_for(lambda: windows() == [(EDGE, EDGE, WIDTH - 2 * EDGE, HEIGHT - 2 * EDGE)],
                     "lone tile inside the gaps with smart_gaps off")
    desktop.reload(config())
    desktop.wait_for(lambda: windows() == [ALONE], "smart gaps on again")
print("A lone tile has no gaps; two and more keep them; smart_gaps = false keeps them always")
