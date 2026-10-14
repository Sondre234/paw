# SPDX-License-Identifier: GPL-3.0-or-later
"""A minimized window restored from the taskbar onto a workspace that tiles comes back as a tile
the way a window opening there does: the windows maximized by hand there come back into the
tiling and a fullscreen one leaves fullscreen, instead of the restored window opening over them.
Monitors tile or not each by their own setting, as on a desk where only one tiles until the
others are toggled; where the workspace does not tile, the window floats and a maximized one
stays maximized."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe, window_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

# HEADLESS-1, where the pointer starts and windows open, tiles; HEADLESS-2 right of it does not
# until it is toggled.
CONFIG = """return {
    xwayland = false,
    layout = { workspaces = 4 },
    outputs = {
        order = { "HEADLESS-1", "HEADLESS-2" },
        monitors = {
            ["HEADLESS-1"] = { mode = "1280x720", tiling = true },
            ["HEADLESS-2"] = { mode = "1280x720", tiling = false },
        },
    },
}"""

with harness.Compositor(compositor, CONFIG, env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def windows():
        return {w.title: w for w in desktop.windows()}

    def layers():
        """title -> the layer the window is drawn in (normal, fullscreen, ...)."""
        return {row[1]: row[3] for row in desktop.rows("stacking")}

    def tiling():
        """output -> whether the workspace it shows tiles."""
        return {row[0]: row[4] == "on" for row in desktop.rows("workspaces")}

    desktop.detail = lambda: f"windows: {windows()}\nlayers: {layers()}\ntiling: {tiling()}"

    def control(title, *words):
        subprocess.run([window_probe, title, *words], env=desktop.env, check=True, timeout=30,
                       stdout=subprocess.DEVNULL)

    def activate(title):
        subprocess.run([probe, "--activate", f"app-{title.lower()}"], env=desktop.env,
                       check=True, timeout=30, stdout=subprocess.DEVNULL)
        wait_for(lambda: windows()[title].focused, f"{title} focused")

    def minimize(title):
        control(title, "minimize")
        wait_for(lambda: windows()[title].minimized and not windows()[title].visible,
                 f"{title} minimized")

    def restore(title):
        """As a click on its taskbar button: unset_minimized, then activate."""
        control(title, "restore")
        wait_for(lambda: windows()[title].focused and not windows()[title].minimized,
                 f"{title} restored")

    def tiles_on(output, titles):
        """Whether exactly `titles` show on `output`, each tiled, none overlapping another and
        none fullscreen."""
        here = [w for w in windows().values() if w.output == output and w.visible]
        return (sorted(w.title for w in here) == sorted(titles) and all(w.tiled for w in here)
                and harness.disjoint([w.box for w in here])
                and all(layers()[w.title] == "normal" for w in here))

    for title in "ABC":
        desktop.open_window(probe, title, f"app-{title.lower()}", focused=True)
    wait_for(lambda: tiles_on("HEADLESS-1", "ABC"), "A, B and C tiled on HEADLESS-1")

    # B minimized, then C maximized by hand, floating over A. B restored from the taskbar takes
    # a tile and C comes back into the tiling beside it, rather than B opening over C.
    minimize("B")
    activate("C")
    msg("maximize")
    wait_for(lambda: not windows()["C"].tiled, "C maximized over the tiling")
    restore("B")
    wait_for(lambda: tiles_on("HEADLESS-1", "ABC"), "B tiled, C back in the tiling")

    # The same with C fullscreen: it leaves fullscreen for B's tile.
    minimize("B")
    activate("C")
    msg("fullscreen")
    wait_for(lambda: layers()["C"] == "fullscreen", "C fullscreen")
    restore("B")
    wait_for(lambda: tiles_on("HEADLESS-1", "ABC"), "B tiled, C out of fullscreen")

    # On another monitor, which tiles only once it is toggled: B minimized on HEADLESS-1 comes
    # to HEADLESS-2, the monitor focused, beside D, which was maximized there.
    minimize("B")
    msg("focus_right")
    wait_for(lambda: next(r[0] for r in desktop.rows("workspaces") if r[2] == "1") ==
             "HEADLESS-2", "HEADLESS-2 focused")
    assert not tiling()["HEADLESS-2"], tiling()
    msg("toggle_tiling")
    wait_for(lambda: tiling()["HEADLESS-2"], "HEADLESS-2 tiling")
    desktop.open_window(probe, "D", "app-d", focused=True)
    wait_for(lambda: tiles_on("HEADLESS-2", "D"), "D tiled on HEADLESS-2")
    msg("maximize")
    wait_for(lambda: not windows()["D"].tiled, "D maximized")
    restore("B")
    wait_for(lambda: tiles_on("HEADLESS-2", "BD") and tiles_on("HEADLESS-1", "AC"),
             "B tiled beside D on HEADLESS-2")

    # Where the workspace does not tile, B floats as it comes back and D stays maximized.
    msg("toggle_tiling")
    wait_for(lambda: not tiling()["HEADLESS-2"] and not windows()["D"].tiled,
             "HEADLESS-2 floating")
    activate("D")
    msg("maximize")
    wait_for(lambda: windows()["D"].box == (1280, 0, 1280, 720), "D maximized")
    minimize("B")
    restore("B")
    desktop.stays(lambda: not windows()["B"].tiled and windows()["B"].output == "HEADLESS-2" and
                  windows()["D"].box == (1280, 0, 1280, 720) and not windows()["D"].tiled,
                  "B floating, D still maximized")
print("Restored windows tile as new ones do, past maximized and fullscreen windows")
