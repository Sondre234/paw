# SPDX-License-Identifier: GPL-3.0-or-later
"""Tiled windows keep gap_outer at the edges, gap_inner between them, and their border inside."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])


def settings(inner, outer, border):
    return f"""return {{
    xwayland = false,
    layout = {{ tiling = true, gap_inner = {inner}, gap_outer = {outer} }},
    windows = {{ border_width = {border} }},
}}"""


with harness.Compositor(compositor, settings(4, 20, 3)) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def boxes():
        return sorted(w.box for w in desktop.windows())

    desktop.detail = lambda: f"windows: {boxes()}"

    def laid_out(inner, outer, border):
        found = boxes()
        if len(found) != 2:
            return False
        (lx, ly, lw, lh), (rx, ry, rw, rh) = found
        edge = outer + border
        # Each probe reserves a test panel along the bottom, so the height is not checked.
        return (lx == edge and ly == edge and ry == edge and lh == rh and
                rx + rw == width - edge and rx - (lx + lw) == inner + 2 * border)

    _, _, x, y, width, height, *_ = msg("get", "outputs").split("\t")
    assert (int(x), int(y)) == (0, 0)
    width = int(width)
    for _ in range(2):
        desktop.spawn([probe, "--external-control"])
    wait_for(lambda: laid_out(4, 20, 3), "gaps and borders around two tiles")

    desktop.reload(settings(10, 0, 0))
    wait_for(lambda: laid_out(10, 0, 0), "reloaded gaps without borders")

    desktop.reload(settings(0, 12, 5))
    wait_for(lambda: laid_out(0, 12, 5), "reloaded outer gap and thicker border")
print("Inner and outer gaps, borders, and their reload passed")
