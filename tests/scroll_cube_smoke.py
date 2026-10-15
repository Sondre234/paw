# SPDX-License-Identifier: GPL-3.0-or-later
"""The scrolling layout's cube (layout.scroll.cube): as focus moves the view along the strip, its
tiles are drawn on the sides of a square turning a quarter, the screen as it was turning away and
the screen as it will be coming round, foreshortened, smaller further off and fainter turned
away, while `get windows` and the pointer have them where they rest at once; then everything is
drawn where it rests. A move under way moved again turns on from where it is drawn; switching
workspaces ends the turn; columns moving without the view, windows opening and the setting off
glide as any tile does."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = true, tile_layout = "scroll", gap = 0,
               scroll = { follow = "edge", width = 0.5, cube = %s } },
    windows = { border_width = 2 },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    animations = { cube = { duration = 1000, curve = "linear" } },
}"""
WIDTH, COLUMN = 1280, 640


def config(cube=True):
    return CONFIG % str(cube).lower()


with harness.Compositor(compositor, config()) as desktop:
    msg = desktop.msg

    def window(title):
        return next(w for w in desktop.windows() if w.title == title)

    def cube():
        """The turn (output, workspace, elapsed, duration, angle, start) or None, and its tiles by
        title: (posed, shown, x, width, scale, fade)."""
        rows = desktop.rows("cube")
        head = rows[0]
        assert head[0] == "cube", rows
        turn = None if head[1] == "-" else (head[1], int(head[2]), *map(int, head[3:]))
        tiles = {r[2]: (r[3] == "1", r[4] == "1", *map(int, r[5:])) for r in rows[1:]}
        return turn, tiles

    def turning():
        return cube()[0] is not None

    desktop.detail = lambda: f"windows: {desktop.windows()}, cube: {cube()}"
    # The pointer rests at the right: over the first column, a new window would open left of it.
    msg("headless_pointer", "add", "mouse")
    msg("headless_pointer", "move", "mouse", "1279", "300")
    # Opening windows moves the view, but they open as windows do, each in a column of its own.
    for title in ("A", "B", "C"):
        desktop.open_window(probe, title, focused=True)
        desktop.stays(lambda: not turning(), f"{title} opens without a turn")
    desktop.wait_for(lambda: window("A").x == -COLUMN + 2 and window("C").x == COLUMN + 2,
                     "B and C in view")

    # focus_left to B moves no view; to A it moves a column left, and the square turns.
    msg("focus_left")
    desktop.stays(lambda: not turning(), "the view stays for B")
    msg("focus_left")
    desktop.wait_for(turning, "the turn starts")
    # Where the windows rest, at once.
    assert window("A").x == 2 and window("B").x == COLUMN + 2, desktop.windows()
    turn, tiles = cube()
    assert turn[:2] == ("HEADLESS-1", 1) and turn[3] == 1000, turn
    assert set(tiles) == {"A", "B", "C"} and all(t[0] for t in tiles.values()), tiles

    def mid_turn():
        """Half a turn, the corner nearest the eye in the middle: A and B foreshortened on the
        side coming round, B and C on the side turning away."""
        turn, tiles = cube()
        return turn and 300 <= turn[2] <= 700 and tiles["A"][1] and tiles["C"][1]

    desktop.wait_for(mid_turn, "half way round", timeout=5)
    turn, tiles = cube()
    a, b, c = tiles["A"], tiles["B"], tiles["C"]
    assert turn[4] > 0, f"turning back to the left: {turn}"
    for name, tile in tiles.items():
        assert 0 < tile[3] < COLUMN + 4, f"{name} foreshortened: {tile}"
        assert tile[4] <= 1000 and 0 <= tile[5] <= 600, f"{name}: {tile}"
    assert a[2] < b[2] < c[2] and 0 <= a[2] and c[2] + c[3] <= WIDTH, tiles
    # What lies at the view's edges is further off than what is near its middle.
    assert a[4] < 1000 and c[4] < 1000 and a[5] > 0, tiles
    desktop.wait_for(lambda: not turning(), "the turn ends", timeout=5)
    assert window("A").x == 2 and window("C").x == WIDTH + 2, desktop.windows()
    assert next(r for r in desktop.rows("bounds") if r[0] == "C")[2] == "0", "C rests past the edge"

    # A move under way moved again turns on, from where it is drawn: back right to B and C.
    msg("focus_right")
    desktop.stays(lambda: not turning(), "B is in view")
    msg("focus_right")
    desktop.wait_for(lambda: (cube()[0] or (0, 0, 0))[2] >= 200, "turning right", timeout=5)
    first = cube()[0]
    assert first[4] < 0, f"turning to the right: {first}"
    msg("focus_left")
    msg("focus_left")
    desktop.wait_for(lambda: window("A").x == 2, "back to A")
    again = cube()[0]
    assert again and again[2] < first[2] + 500, f"the turn goes on: {first} {again}"
    assert again[4] > 0, f"turning back: {again}"
    desktop.wait_for(lambda: not turning(), "the turn back ends", timeout=5)

    # Another workspace ends a turn at once.
    msg("focus_right")
    msg("focus_right")
    desktop.wait_for(turning, "turning to C")
    msg("workspace", "2")
    desktop.wait_for(lambda: not turning(), "the turn ends with the workspace")
    msg("workspace", "1")
    desktop.wait_for(lambda: window("C").x == COLUMN + 2, "C rests in view")

    # Columns moving without the view glide as tiles do: C traded with B, the view staying.
    msg("move_left")
    desktop.stays(lambda: not turning(), "trading columns turns nothing")
    desktop.wait_for(lambda: window("C").x == 2 and window("B").x == COLUMN + 2, "C left of B")

    # Off, the view glides.
    desktop.reload(config(cube=False))
    msg("focus_left")
    msg("focus_left")
    desktop.wait_for(lambda: window("A").x == 2, "A in view")
    desktop.stays(lambda: not turning(), "no turn with the cube off")
