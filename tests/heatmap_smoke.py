# SPDX-License-Identifier: GPL-3.0-or-later
"""The pointer heatmap: each output counts, in a grid of cells 8 logical pixels square, how long
the pointer rested in each cell and how often it was clicked there. A resting pointer heats its
cell up to a limit (PAW_TEST_HEATMAP_REST_MS here), a pointer passing along warms its path a
little, clicks count apart, nothing counts while the session is locked, a grid follows its
output's size, keeping its heat where it was. The pointer is a virtual one (pointer_probe)."""
from pathlib import Path
import sys

import harness

compositor, pointer_probe, lock_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])
SCREEN = (1280, 720)
REST_MS = 1500


def config(mode="1280x720"):
    return f"""return {{
    xwayland = false,
    idle = {{ display_off = 0 }},
    outputs = {{ monitors = {{ ["HEADLESS-1"] = {{ mode = "{mode}" }} }} }},
}}"""


def heatmap(desktop, *words):
    """{output: (columns, rows, cell, box, rows of counts)} from `get heatmap WORDS`."""
    lines, grids = desktop.rows("heatmap", *words), {}
    while lines:
        name, kind, columns, rows, cell, *box = lines.pop(0)
        columns, rows = int(columns), int(rows)
        counts = [[int(n) for n in line] for line in lines[:rows]]
        assert all(len(line) == columns for line in counts), name
        grids[name] = (columns, rows, int(cell), tuple(int(n) for n in box), counts)
        del lines[:rows]
    return grids


def cell(desktop, x, y, kind="motion", output="HEADLESS-1"):
    """The count of the cell at logical x, y of `output`."""
    counts = heatmap(desktop, output, kind)[output][4]
    return counts[y // 8][x // 8]


def total(desktop, kind="motion"):
    return sum(sum(map(sum, grid[4])) for grid in heatmap(desktop, kind).values())


with harness.Compositor(compositor, config(),
                        env={"PAW_TEST_HEATMAP_REST_MS": str(REST_MS)}) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for
    grids = heatmap(desktop)
    assert list(grids) == ["HEADLESS-1"], grids
    columns, rows, size, box, counts = grids["HEADLESS-1"]
    assert (columns, rows, size, box) == (160, 90, 8, (0, 0, 1280, 720)), grids
    assert not any(map(any, counts)), "nothing counted before the pointer moved"
    assert heatmap(desktop, "clicks")["HEADLESS-1"][:4] == (160, 90, 8, (0, 0, 1280, 720))
    msg("get", "heatmap", "HEADLESS-9", ok=False)
    msg("get", "heatmap", "motion", "clicks", "more", ok=False)

    pointer = desktop.virtual_pointer(pointer_probe, *SCREEN)

    # A resting pointer heats its cell, as far as its rest has come; moving on keeps it.
    pointer("move", "100", "100")
    wait_for(lambda: cell(desktop, 100, 100) >= 400, "the resting pointer's cell warming")
    pointer("move", "1000", "600")
    rested = cell(desktop, 100, 100)
    assert 400 <= rested <= REST_MS, rested
    desktop.stays(lambda: cell(desktop, 100, 100) == rested, "a cell left behind warming")
    # Resting past the limit counts no more than the limit.
    wait_for(lambda: cell(desktop, 1000, 600) >= REST_MS, "a rest reaching the limit")
    desktop.stays(lambda: cell(desktop, 1000, 600) == REST_MS, "a rest counted past the limit",
                  duration=.5)

    # A pointer passing along warms its path a little, much less than where it rested.
    for x in range(20, 1260, 40):
        pointer("move", str(x), "300", "sleep", "5")
    pointer("move", "640", "360")
    path = heatmap(desktop)["HEADLESS-1"][4][300 // 8]
    warm = [count for count in path if count]
    assert len(warm) >= 25 and max(warm) < 200, path
    hottest = max(max(line) for line in heatmap(desktop)["HEADLESS-1"][4])
    assert hottest == REST_MS, hottest

    # Clicks count apart from the rest, where the pointer is.
    pointer("click", "left", "click", "left", "click", "right")
    assert cell(desktop, 640, 360, "clicks") == 3
    assert total(desktop, "clicks") == 3
    pointer("move", "8", "712", "press", "left", "release", "left")
    assert cell(desktop, 8, 712, "clicks") == 1 and total(desktop, "clicks") == 4

    # Nothing counts while the session is locked; what was counted stays.
    locker = desktop.spawn([lock_probe, "hold", str(desktop.root / "locker.log")])
    wait_for(lambda: "Session locked" in desktop.log.read_text(), "the session locked")
    before = total(desktop), total(desktop, "clicks")
    pointer("move", "300", "300", "sleep", "200", "click", "left", "move", "320", "300")
    desktop.stays(lambda: (total(desktop), total(desktop, "clicks")) == before,
                  "counted while locked")
    locker.terminate()
    assert desktop.reap(locker) == 0
    wait_for(lambda: "Session unlocked" in desktop.log.read_text(), "the session unlocked")

    # A smaller mode: the grid shrinks with it, its heat where it was on the screen.
    clicks = total(desktop, "clicks")
    desktop.reload(config("640x360"))
    wait_for(lambda: heatmap(desktop)["HEADLESS-1"][0] == 80, "the grid following the mode")
    columns, rows, size, box, counts = heatmap(desktop)["HEADLESS-1"]
    assert (columns, rows, box) == (80, 45, (0, 0, 640, 360)), (columns, rows, box)
    assert counts[50 // 8][50 // 8] >= rested, "the rested cell moved with the screen"
    assert total(desktop, "clicks") == clicks, "the clicks kept"
    assert cell(desktop, 320, 180, "clicks") == 3

    # A second output has a grid of its own at its place in the layout.
    added = msg("headless_output", "add", "HEADLESS-2", "800x600").strip()
    assert added == "HEADLESS-2", added
    wait_for(lambda: "HEADLESS-2" in heatmap(desktop), "the second output's grid")
    columns, rows, size, box, counts = heatmap(desktop)["HEADLESS-2"]
    assert (columns, rows, box) == (100, 75, (640, 0, 800, 600)), (columns, rows, box)
    assert list(heatmap(desktop, "HEADLESS-2")) == ["HEADLESS-2"]
