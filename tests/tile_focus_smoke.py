# SPDX-License-Identifier: GPL-3.0-or-later
"""focus_left/right/up/down moves keyboard focus between neighbouring tiles, and
move_left/right/up/down swaps the focused tile with its neighbour, in both axes."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = true },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
}"""

DIRECTIONS = {"left": (True, -1), "right": (True, 1), "up": (False, -1), "down": (False, 1)}

with harness.Compositor(compositor, CONFIG) as desktop:
    msg = desktop.msg

    def windows():
        """(focused, tiled, x, y, width, height) per window, most recently focused last."""
        return [(w.focused, w.tiled, *w.box) for w in desktop.windows()]

    desktop.detail = lambda: f"windows: {windows()}"

    def focused():
        """The focused tile, by its place: the listing is in focus order, so its index
        changes with focus."""
        return next(tuple(w[2:]) for w in windows() if w[0])

    def settled():
        current = windows()
        return all(w[1] for w in current) and harness.disjoint([w[2:] for w in current])

    def toward(start, direction):
        """The tile focus_<direction> should pick from the tile at `start`: those level with
        it first, then the nearest by centre, as the compositor documents."""
        horizontal, sign = DIRECTIONS[direction]
        fx, fy, fw, fh = start
        cx, cy = fx + fw / 2, fy + fh / 2
        best = None
        for (_, _, x, y, w, h) in windows():
            if (x, y, w, h) == start:
                continue
            ox, oy = x + w / 2, y + h / 2
            if ((ox - cx) if horizontal else (oy - cy)) * sign <= 0:
                continue
            level = (y < fy + fh and fy < y + h) if horizontal else (x < fx + fw and fx < x + w)
            key = (not level, ((ox - cx) ** 2 + (oy - cy) ** 2) ** .5)
            if best is None or key < best[0]:
                best = (key, (x, y, w, h))
        return None if best is None else best[1]

    for count in (1, 2, 3, 4):
        desktop.spawn([probe, "--external-control"])
        desktop.wait_for(lambda: len(windows()) == count and settled(), f"window {count} tiled")

    # Four tiles always have neighbours in both axes, so every direction gets used.
    moved = {True: False, False: False}
    for direction in ["left", "up", "right", "down", "left", "down", "right", "up"] * 2:
        start = focused()
        target = toward(start, direction)
        msg(f"focus_{direction}")
        if target is None:
            assert focused() == start, (direction, windows())
        else:
            desktop.wait_for(lambda: focused() == target, f"focus_{direction} to {target}")
            moved[DIRECTIONS[direction][0]] = True
    assert all(moved.values()), f"focus never crossed an axis: {moved}"

    # move_<direction> trades places with the neighbour: the focused tile ends up on
    # that side of where it was, still focused, and no tile covers another.
    swapped = {True: False, False: False}
    for direction in ["left", "up", "right", "down"] * 2:
        start = focused()
        if toward(start, direction) is None:
            continue
        horizontal, sign = DIRECTIONS[direction]
        axis = 0 if horizontal else 1
        msg(f"move_{direction}")
        desktop.wait_for(lambda: settled() and len([w for w in windows() if w[0]]) == 1 and
                         (focused()[axis] - start[axis]) * sign > 0,
                         f"move_{direction} swapped the tile")
        swapped[horizontal] = True
    assert all(swapped.values()), f"no swap along an axis: {swapped}"
print("Keyboard focus and swapping between tiles passed")
