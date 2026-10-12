# SPDX-License-Identifier: GPL-3.0-or-later
"""A tile maximized by hand floats over the tiling, but a new window opening on its workspace
brings it back in, as it would a fullscreen one, instead of covering it. Turning tiling off and
on again also brings back windows that float only because they were maximized, and so it does
after a session restore put such a window back."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe, example = (str(Path(p).resolve()) for p in sys.argv[1:4])

with harness.Compositor(compositor, Path(example).read_text()
                        .replace("xwayland = true", "xwayland = false")
                        .replace("tiling = false", "tiling = true")) as desktop:
    msg = desktop.msg

    def windows():
        """(tiled, x, y, width, height) per window, oldest first."""
        return [(w.tiled, *w.box) for w in desktop.windows()]

    def all_tiled(count):
        current = windows()
        return (len(current) == count and all(w[0] for w in current) and
                harness.disjoint([w[1:] for w in current]))

    desktop.detail = lambda: f"windows: {windows()}"

    def launch():
        desktop.spawn([probe, "--window-only"], stderr=subprocess.DEVNULL)

    assert msg("get", "tiling") == "on\n"

    launch()
    desktop.wait_for(lambda: all_tiled(1), "first window tiled")
    msg("maximize")
    desktop.wait_for(lambda: not windows()[0][0], "maximizing floats the tile")

    # The new window tiles beside the maximized one instead of opening over it.
    launch()
    desktop.wait_for(lambda: all_tiled(2), "maximized window back in the tiling")

    # Turning tiling off and on brings a maximized window back too.
    msg("maximize")
    desktop.wait_for(lambda: sum(w[0] for w in windows()) == 1, "second window maximized")
    msg("toggle_tiling")
    desktop.wait_for(lambda: msg("get", "tiling") == "off\n", "tiling off")
    msg("toggle_tiling")
    desktop.wait_for(lambda: all_tiled(2), "both tiled after tiling is on again")

    # A session saved with a window maximized over the tiling puts it back maximized, still
    # floating only for that: it comes back into the tiling as tiling turns on again.
    msg("maximize")
    desktop.wait_for(lambda: sum(w[0] for w in windows()) == 1, "maximized before saving")
    maximized = windows()
    assert "saved last: 2 windows" in msg("session", "save", "last")
    msg("toggle_tiling")
    msg("toggle_tiling")
    desktop.wait_for(lambda: all_tiled(2), "back in the tiling before restoring")
    assert "restored 2," in msg("session", "restore", "last")
    desktop.wait_for(lambda: windows() == maximized, "maximized again by the restore")
    msg("toggle_tiling")
    desktop.wait_for(lambda: msg("get", "tiling") == "off\n", "tiling off after the restore")
    msg("toggle_tiling")
    desktop.wait_for(lambda: all_tiled(2), "both tiled after the restore and tiling on again")
print("A maximized tile comes back into the tiling")
