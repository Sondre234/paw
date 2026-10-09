# SPDX-License-Identifier: GPL-3.0-or-later
"""Tiling layouts: layout_next and layout_<name> switch a workspace between dwindle,
master-stack, spiral and monocle; master_grow, master_more and promote work in master-stack."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = true, master_ratio = 0.6 },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
}"""

with harness.Compositor(compositor, CONFIG) as desktop:
    msg = desktop.msg

    def windows():
        """(focused, tiled, x, y, width, height) per window."""
        rows = desktop.rows("windows")
        return [(r[1] == "1", r[3] == "1", *map(int, r[4:8])) for r in rows]

    def rects():
        return sorted(w[2:] for w in windows())

    def layout():
        return msg("get", "layout").split()

    desktop.detail = lambda: f"windows: {windows()}, layout: {layout()}"

    def in_layout(name):
        msg(f"layout_{name}")
        desktop.wait_for(lambda: layout()[0] == name, f"layout {name}")

    def master_column():
        """The tiles at the left edge, and the others."""
        items = rects()
        left = min(r[0] for r in items)
        return [r for r in items if r[0] == left], [r for r in items if r[0] != left]

    for count in (1, 2, 3):
        desktop.spawn([probe, "--external-control"])
        desktop.wait_for(lambda: len(windows()) == count and all(w[1] for w in windows()),
                         f"window {count} tiled")
    assert layout() == ["dwindle", "0.60", "1"], layout()

    # Master-stack: one master on the left, the others stacked in the right column.
    in_layout("master")
    desktop.wait_for(lambda: len(master_column()[0]) == 1 and len(master_column()[1]) == 2,
                     "master and stack")
    (master,), stack = master_column()
    assert master[2] > stack[0][2] and stack[0][2] == stack[1][2], (master, stack)
    assert stack[0][0] == stack[1][0] and harness.disjoint(rects()), rects()
    assert abs(master[3] - (stack[0][3] + stack[1][3])) < 40, (master, stack)

    msg("master_grow")
    desktop.wait_for(lambda: master_column()[0][0][2] > master[2], "master_grow")
    msg("master_shrink")
    desktop.wait_for(lambda: master_column()[0][0][2] == master[2], "master_shrink")
    msg("master_more")
    desktop.wait_for(lambda: len(master_column()[0]) == 2 and len(master_column()[1]) == 1,
                     "master_more")
    assert layout()[2] == "2", layout()
    msg("master_less")
    desktop.wait_for(lambda: len(master_column()[0]) == 1, "master_less")

    # promote gives the focused tile, moved into the stack first, the master column.
    if next(w[2:] for w in windows() if w[0]) == master:
        msg("focus_next")
        desktop.wait_for(lambda: next(w[2:] for w in windows() if w[0]) != master, "focus_next")
    msg("promote")
    desktop.wait_for(lambda: next(w[2:] for w in windows() if w[0]) == master_column()[0][0],
                     "promote")

    # Spiral: disjoint tiles, the first taking the left of the area.
    in_layout("spiral")
    desktop.wait_for(lambda: len(master_column()[0]) == 1 and harness.disjoint(rects()),
                     "spiral")

    # Monocle: every tile covers the same area, and focus_next steps between them.
    # The tiles look alike there, so the focused one is found in master-stack.
    def slot():
        in_layout("master")
        desktop.wait_for(lambda: len(master_column()[0]) == 1 and harness.disjoint(rects()),
                         "master")
        return next(w[2:] for w in windows() if w[0])

    start = slot()
    in_layout("monocle")
    desktop.wait_for(lambda: len(set(rects())) == 1, "monocle")
    assert len([w for w in windows() if w[0]]) == 1
    msg("focus_next")
    after = slot()
    assert after != start, "focus_next did not move the focus"
    in_layout("monocle")
    msg("focus_prev")
    assert slot() == start, "focus_prev did not move back"
    in_layout("monocle")

    # Back to dwindle, and layout_next / layout_prev walk the cycle.
    in_layout("dwindle")
    desktop.wait_for(lambda: len(set(rects())) == 3 and harness.disjoint(rects()),
                     "dwindle again")
    msg("layout_next")
    desktop.wait_for(lambda: layout()[0] == "master", "layout_next")
    msg("layout_prev")
    desktop.wait_for(lambda: layout()[0] == "dwindle", "layout_prev")

    # Another workspace keeps its own layout.
    msg("workspace", "2")
    desktop.wait_for(lambda: layout()[0] == "dwindle", "second workspace")
    in_layout("monocle")
    msg("workspace", "1")
    desktop.wait_for(lambda: layout()[0] == "dwindle", "first workspace kept its layout")
print("Tiling layouts passed")
