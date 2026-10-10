# SPDX-License-Identifier: GPL-3.0-or-later
"""The magnifier on several outputs: an output it cannot magnify (a rotated one) shows 1x without
switching the magnifier off for the others, and the level survives a return to 1x. The pointer
is a virtual one (pointer_probe)."""
from pathlib import Path
import sys

import harness

compositor, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    animations = { enabled = false },
    outputs = {
        primary = "HEADLESS-1",
        order = { "HEADLESS-1", "HEADLESS-2" },
        monitors = {
            ["HEADLESS-1"] = { mode = "1280x720", transform = 1 },
            ["HEADLESS-2"] = { mode = "1280x720" },
        },
    },
    zoom = { step = 2, max = 4, duration = 0 },
}"""

with harness.Compositor(compositor, CONFIG, env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    msg = desktop.msg

    def zoom():
        """(level, target, magnified outputs), levels in thousandths"""
        return tuple(int(n) for n in msg("get", "zoom").splitlines()[0].split("\t"))

    outputs = {row[0]: row for row in desktop.rows("outputs")}
    first, second = outputs["HEADLESS-1"], outputs["HEADLESS-2"]
    assert int(first[2]) < int(second[2]), (first, second)  # 1 is to the left of 2
    # The layout's extent, which the virtual pointer's points are of.
    pointer = desktop.virtual_pointer(
        pointer_probe, max(int(row[2]) + int(row[4]) for row in outputs.values()),
        max(int(row[3]) + int(row[5]) for row in outputs.values()))

    desktop.detail = lambda: f"zoom: {zoom()}\n{msg('get', 'outputs')}"
    # The pointer on the rotated output: it cannot be magnified, and nothing is.
    pointer("move", "100", "100")
    msg("zoom_in")
    desktop.wait_for(lambda: zoom()[:2] == (2000, 2000), "zoomed")
    desktop.wait_for(lambda: "Cannot magnify HEADLESS-1" in desktop.log.read_text(),
                     "the rotated output was tried")
    assert zoom()[2] == 0, zoom()

    # The pointer on the other output: that one is magnified regardless.
    pointer("move", str(int(second[2]) + 300), "100")
    desktop.wait_for(lambda: zoom()[2] == 1, "the second output magnified")

    msg("zoom_reset")
    desktop.wait_for(lambda: zoom() == (1000, 1000, 0), "back to 1x")
print("A rotated output shows 1x without disabling the magnifier elsewhere")
