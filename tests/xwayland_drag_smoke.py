# SPDX-License-Identifier: GPL-3.0-or-later
"""Drag and drop while XWayland runs. Its window manager hears of every drag that starts and takes
it to have a data source (wlroots 0.20 reads through a NULL one), so a drag of nothing, which a
window may start within itself, is turned down there rather than crashing the compositor; one
with a data source goes as anywhere else."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, x11_probe, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:5])

CONFIG = """return {
    xwayland = true,
    layout = { tiling = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
}"""

with harness.Compositor(compositor, CONFIG) as desktop:
    # XWayland starts on demand: an X11 client brings up its window manager.
    subprocess.run([x11_probe], env=desktop.env, check=True, timeout=30)
    desktop.wait_for(lambda: "XWayland ready" in desktop.log.read_text(), "XWayland ready")

    def seat():
        return {row[0]: (row[1], row[2]) for row in desktop.rows("seat")}

    def log(name):
        return (desktop.root / f"{name}.log").read_text()

    def centre(title):
        window = next(w for w in desktop.windows() if w.title == title)
        return str(window.x + window.width // 2), str(window.y + window.height // 2)

    pointer = desktop.virtual_pointer(pointer_probe, 1280, 720)
    desktop.open_window(probe, "Bare", focused=True, log="bare.log",
                        env={"PAW_PROBE_DRAG": "bare"})
    # The first for a serial no press gave, the second from the press: both turned down.
    pointer("move", *centre("Bare"), "press", "left")
    desktop.wait_for(lambda: "bare drag asked" in log("bare"), "the first bare drag asked for")
    pointer("release", "left", "press", "left")
    desktop.wait_for(lambda: log("bare").count("bare drag asked") == 2, "the second asked for")
    assert "drag" not in seat(), seat()
    assert "Drag without a data source turned down" in desktop.log.read_text()
    pointer("release", "left")

    desktop.open_window(probe, "Source", focused=True, log="source.log",
                        env={"PAW_PROBE_DRAG": "source"})
    pointer("move", *centre("Source"), "press", "left")
    desktop.wait_for(lambda: "drag" in seat(), "the drag with a data source under way")
    pointer("move", "1200", "650", "release", "left")
    desktop.wait_for(lambda: "drag cancelled" in log("source"), "the drag dropped on nothing")
print("A drag of nothing is turned down while XWayland runs; one with a data source goes")
