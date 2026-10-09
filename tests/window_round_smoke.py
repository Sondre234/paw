# SPDX-License-Identifier: GPL-3.0-or-later
"""windows.round: by default only windows on a monitor with tiling on are rounded; "always"
rounds floating windows elsewhere too, but not one that draws a shadow of its own around its
geometry (a client-side frame), nor a fullscreen or maximized window. Read through `get frames`;
a build without the rounded-corners patch rounds nothing, which the test then only checks."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])


def config(round_mode):
    return """return {
    xwayland = false,
    layout = { tiling = false },
    windows = { corner_radius = 10, round = "%s", placement = "smart" },
}""" % round_mode


with harness.Compositor(compositor, config("tiling")) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def radii():
        return {r[1]: int(r[5]) for r in desktop.rows("frames")}

    desktop.detail = lambda: msg("get", "frames")

    # A window with its own shadow, one without and without server-side decorations, and one
    # the compositor decorates, focused as it opens last.
    for title, env in (("own-shadow", {"PAW_PROBE_SHADOW": "12"}), ("plain", {}),
                       ("decorated", {"PAW_PROBE_SSD": "1"})):
        desktop.spawn([probe, "--window-only"], env={"PAW_PROBE_TITLE": title, **env})
        wait_for(lambda: title in radii(), f"{title} mapped")
    square = {"own-shadow": 0, "plain": 0, "decorated": 0}
    wait_for(lambda: radii() == square, "floating windows are square by default")

    # Tiling rounds every window, its own shadow or not.
    msg("toggle_tiling")
    wait_for(lambda: len(set(radii().values())) == 1, "tiles alike")
    supported = radii()["decorated"] == 10
    if supported:
        msg("toggle_tiling")
        wait_for(lambda: radii() == square, "square again without tiling")

        # "always" rounds floating windows too, but for the one with a shadow of its own.
        desktop.reload(config("always"))
        rounded = {"own-shadow": 0, "plain": 10, "decorated": 10}
        wait_for(lambda: radii() == rounded, "rounded floating windows")
        # Fullscreen and maximized to the edges, the window is square.
        msg("fullscreen")
        wait_for(lambda: radii()["decorated"] == 0, "fullscreen window square")
        msg("fullscreen")
        wait_for(lambda: radii()["decorated"] == 10, "rounded again out of fullscreen")
        msg("maximize")
        wait_for(lambda: radii()["decorated"] == 0, "maximized window square")
        msg("restore")
        wait_for(lambda: radii()["decorated"] == 10, "rounded again restored")
        desktop.reload(config("tiling"))
        wait_for(lambda: radii() == square, "reload back to rounding tiles only")
print("windows.round rounds what it says"
      + ("" if supported else " (wlroots without rounded corners: nothing is rounded)"))
