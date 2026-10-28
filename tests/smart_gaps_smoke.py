# SPDX-License-Identifier: GPL-3.0-or-later
"""layout.smart_gaps: a tiled window alone on its workspace has no gaps and fills the usable area
inside its border, from its first configure on; a second tile brings the gaps back for both, and
they go again as it closes or floats, and follow gap_inner, gap_outer and border_width as they are
reloaded. Turned off, a lone tile keeps them. layout.lone_margin frames a lone tile instead."""
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

with harness.Compositor(compositor, config()) as desktop:
    root, msg = desktop.root, desktop.msg

    def windows():
        """(x, y, width, height) per window, oldest first."""
        return [w.box for w in desktop.windows()]

    def focused():
        """The index of the focused window, oldest first."""
        return [w.focused for w in desktop.windows()].index(True)

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

    def gapped(left, right, inner=INNER, outer=OUTER, border=BORDER):
        """Whether two tiles stand side by side inside the gaps: gap_outer at the edges and
        gap_inner between them, their borders inside."""
        edge = outer + border
        return (left[0] == edge and left[1] == edge and right[1] == edge and
                right[0] + right[2] == WIDTH - edge and left[3] == right[3] == HEIGHT - 2 * edge and
                right[0] - (left[0] + left[2]) == inner + 2 * border)

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

    # Other gaps and borders, reloaded, reach the tiles open: without borders, and without an
    # inner gap.
    for inner, outer, border in ((10, 0, 0), (0, 12, 5)):
        desktop.reload(CONFIG % (inner, outer, "", border, WIDTH, HEIGHT))
        desktop.wait_for(lambda: gapped(*sorted(windows()), inner, outer, border),
                         f"gap_inner {inner}, gap_outer {outer} and border_width {border}")
    desktop.reload(config())
    desktop.wait_for(lambda: gapped(*sorted(windows())), "the gaps as before")

    # Closing the second drops the gaps.
    assert focused() == 1, desktop.windows()
    msg("close")
    assert desktop.reap(second) == 0
    desktop.wait_for(lambda: windows() == [ALONE], "gaps gone with the other tile")

    # A lone column of the scroll layout fills the screen; with fill_lone off it keeps its width,
    # and narrowed it has no gaps either.
    msg("layout_scroll")
    desktop.reload(config(", scroll = { fill_lone = false }"))
    desktop.wait_for(lambda: windows()[0][2] == WIDTH // 2 - 2 * BORDER, "lone column at its width")
    assert windows()[0][1::2] == (BORDER, HEIGHT - 2 * BORDER), windows()
    desktop.reload(config())
    desktop.wait_for(lambda: windows() == [ALONE], "lone column filling the screen")
    msg("column_narrow")
    desktop.wait_for(lambda: windows()[0][2] == WIDTH * 9 // 10 - 2 * BORDER, "lone column")
    assert windows()[0][1::2] == (BORDER, HEIGHT - 2 * BORDER), windows()
    msg("layout_dwindle")
    desktop.wait_for(lambda: windows() == [ALONE], "dwindle again, alone")

    # Turned off, a lone tile keeps gap_outer.
    desktop.reload(config(", smart_gaps = false"))
    desktop.wait_for(lambda: windows() == [(EDGE, EDGE, WIDTH - 2 * EDGE, HEIGHT - 2 * EDGE)],
                     "lone tile inside the gaps with smart_gaps off")
    desktop.reload(config())
    desktop.wait_for(lambda: windows() == [ALONE], "smart gaps on again")

    # lone_margin keeps a lone tile that far from the edges, whatever smart_gaps says; a second
    # tile brings the usual gaps back. Too large a margin still leaves a quarter of the screen.
    MARGIN = 48
    for smart in ("", ", smart_gaps = false"):
        desktop.reload(config(f", lone_margin = {MARGIN}{smart}"))
        desktop.wait_for(lambda: windows() == [(MARGIN + BORDER, MARGIN + BORDER,
                                                WIDTH - 2 * (MARGIN + BORDER),
                                                HEIGHT - 2 * (MARGIN + BORDER))],
                         f"lone tile inside lone_margin{smart}")
    second = launch()
    desktop.wait_for(lambda: len(windows()) == 2 and gapped(*sorted(windows())),
                     "gaps, not the margin, around two tiles")
    msg("close")
    assert desktop.reap(second) == 0
    # 400 fits across 1280 but not down 720, which keeps a quarter of its height.
    desktop.reload(config(", lone_margin = 400"))
    desktop.wait_for(lambda: windows() == [(400 + BORDER, HEIGHT * 3 // 8 + BORDER,
                                            WIDTH - 800 - 2 * BORDER, HEIGHT // 4 - 2 * BORDER)],
                     "a quarter of the output's height left")
    # By side, the sides left out 0.
    desktop.reload(config(", lone_margin = { top = 10, right = 200, left = 100 }"))
    desktop.wait_for(lambda: windows() == [(100 + BORDER, 10 + BORDER,
                                            WIDTH - 300 - 2 * BORDER, HEIGHT - 10 - 2 * BORDER)],
                     "lone tile inside lone_margin by side")
print("A lone tile has no gaps; two and more keep them; smart_gaps = false keeps them always; "
      "lone_margin frames a lone tile")
