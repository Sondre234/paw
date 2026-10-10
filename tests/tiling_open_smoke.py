# SPDX-License-Identifier: GPL-3.0-or-later
"""Windows opening into the tiling get their tile size before their first buffer, and each
reflow sends one configure to the tiles that change and none to the rest. Also: a taskbar
maximize request leaves a fullscreen tile alone."""
from pathlib import Path
import re
import subprocess
import sys

import harness

compositor, probe, example = (str(Path(p).resolve()) for p in sys.argv[1:4])
CONFIGURE = re.compile(r"xdg_toplevel#\d+\.configure\((-?\d+), (-?\d+),")

with harness.Compositor(compositor, Path(example).read_text()
                        .replace("xwayland = true", "xwayland = false")
                        .replace("tiling = false", "tiling = true"), start=False) as desktop:
    root, env = desktop.root, desktop.env
    env.pop("WAYLAND_DEBUG", None)

    def windows():
        """(x, y, width, height) per window, oldest first."""
        return [w.box for w in desktop.windows()]

    def configures(index):
        """The sizes of every xdg_toplevel.configure the `index`th window received."""
        text = (root / f"probe{index}.log").read_text()
        return [(int(m[1]), int(m[2])) for m in CONFIGURE.finditer(text)]

    desktop.detail = lambda: (f"windows: {windows()}, configures: "
                              f"{[configures(i) for i in range(len(desktop.clients))]}")

    def launch():
        index = len(desktop.clients)
        desktop.spawn([probe, "--window-only"], env={"WAYLAND_DEBUG": "client"},
                      stderr=(root / f"probe{index}.log").open("w"))
        desktop.wait_for(lambda: len(windows()) == index + 1 and
                         all(configures(i) and configures(i)[-1] == windows()[i][2:]
                             for i in range(index + 1)), f"window {index} tiled")
        # Let any late configure arrive before counting.
        subprocess.run([probe, "--globals"], env=env, capture_output=True, timeout=30)
        return [configures(i) for i in range(index + 1)]

    desktop.start()

    counts = []
    for opened in range(3):
        seen = launch()
        boxes = windows()
        # The first configure already carries the tile, so the first buffer fits it;
        # the second only activates the window.
        assert seen[opened][0] == boxes[opened][2:], (opened, seen, boxes)
        assert len(seen[opened]) <= 2, (opened, seen)
        if counts:
            added = [len(sizes) - count for sizes, count in zip(seen, counts)]
            # Each tile that changed hears about it once; unchanged tiles not at all.
            for index, (sizes, new) in enumerate(zip(seen, added)):
                changed = previous[index] != boxes[index]
                assert new == (1 if changed or index == opened - 1 else 0), \
                    (opened, index, seen, previous, boxes)
        if opened == 2:
            # The third window split the second (focused) one and left the first alone.
            assert boxes[0] == previous[0], (previous, boxes)
        counts = [len(sizes) for sizes in seen]
        previous = boxes

    # A taskbar maximize request leaves a fullscreen window alone, as a client's does.
    output = desktop.run("get", "outputs").stdout
    desktop.msg("fullscreen")
    full = [(0, 0, *map(int, re.search(r"(\d+)x(\d+)", output).groups()))]
    desktop.wait_for(lambda: windows()[2] == full[0], "focused window fullscreen")
    subprocess.run([probe, "--maximize", "paw-probe"], env=env, check=True,
                   capture_output=True, timeout=30)
    subprocess.run([probe, "--globals"], env=env, capture_output=True, timeout=30)
    assert windows()[2] == full[0], windows()
    desktop.msg("fullscreen")
    desktop.wait_for(lambda: windows() == previous, "tile restored after fullscreen")
print("New tiles configured once at their size, unchanged tiles left alone")
