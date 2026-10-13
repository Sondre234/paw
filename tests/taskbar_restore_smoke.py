# SPDX-License-Identifier: GPL-3.0-or-later
"""A minimized window restored from the taskbar comes to the workspace the focused monitor shows,
and takes a tile there where it tiles, rather than sending the monitor back to the workspace it was
minimized on. A window that is not minimized still brings its own workspace up."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe, window_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

# HEADLESS-1 sits left of HEADLESS-2; the pointer starts on HEADLESS-1, where windows open.
CONFIG = """return {
    xwayland = false,
    layout = { tiling = true, workspaces = 4 },
    outputs = {
        order = { "HEADLESS-1", "HEADLESS-2" },
        monitors = {
            ["HEADLESS-1"] = { mode = "1280x720" },
            ["HEADLESS-2"] = { mode = "1280x720" },
        },
    },
}"""

with harness.Compositor(compositor, CONFIG, env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def windows():
        return {w.title: w for w in desktop.windows()}

    def shown():
        """The workspace each output shows, and the focused output."""
        rows = desktop.rows("workspaces")
        return {row[0]: int(row[1]) for row in rows}, next(r[0] for r in rows if r[2] == "1")

    desktop.detail = lambda: f"windows: {windows()}, shown: {shown()}"

    def control(title, *words):
        subprocess.run([window_probe, title, *words], env=desktop.env, check=True, timeout=30,
                       stdout=subprocess.DEVNULL)

    def minimize(title):
        control(title, "minimize")
        wait_for(lambda: windows()[title].minimized and not windows()[title].visible,
                 f"{title} minimized")

    def tiles_on(output, workspace, titles):
        """Whether exactly `titles` show on that workspace of `output`, each tiled and none
        overlapping another."""
        here = [w for w in windows().values()
                if w.output == output and w.workspace == workspace and w.visible]
        return (sorted(w.title for w in here) == sorted(titles) and all(w.tiled for w in here)
                and harness.disjoint([w.box for w in here]))

    for title in ("A", "B", "C"):
        desktop.open_window(probe, title, f"app-{title.lower()}", focused=True)
    wait_for(lambda: tiles_on("HEADLESS-1", 1, "ABC"), "A, B and C tiled on workspace 1")

    # B minimized on workspace 1 and restored from workspace 2, as the taskbar does: it comes to
    # workspace 2 and takes a tile beside D there, and the monitor stays on workspace 2.
    minimize("B")
    wait_for(lambda: tiles_on("HEADLESS-1", 1, "AC"), "A and C make room")
    msg("workspace", "2")
    desktop.open_window(probe, "D", "app-d", focused=True)
    control("B", "restore")
    wait_for(lambda: windows()["B"].focused and not windows()["B"].minimized, "B restored")
    wait_for(lambda: tiles_on("HEADLESS-1", 2, "BD"), "B tiled beside D on workspace 2")
    assert shown() == ({"HEADLESS-1": 2, "HEADLESS-2": 1}, "HEADLESS-1"), shown()
    assert windows()["A"].workspace == windows()["C"].workspace == 1, windows()

    # A window minimized on a workspace not shown comes the same way when only activated, as
    # other taskbars do.
    minimize("C")
    subprocess.run([probe, "--activate", "app-c"], env=desktop.env, check=True, timeout=30,
                   stdout=subprocess.DEVNULL)
    wait_for(lambda: windows()["C"].focused and tiles_on("HEADLESS-1", 2, "BCD"),
             "C restored as a tile on workspace 2")
    assert shown()[0] == {"HEADLESS-1": 2, "HEADLESS-2": 1}, shown()
    assert windows()["A"].workspace == 1, windows()

    # A window that is not minimized brings its workspace up, as before.
    subprocess.run([probe, "--activate", "app-a"], env=desktop.env, check=True, timeout=30,
                   stdout=subprocess.DEVNULL)
    wait_for(lambda: windows()["A"].focused and shown()[0]["HEADLESS-1"] == 1,
             "activating A shows workspace 1")
    assert windows()["A"].workspace == 1, windows()

    # One minimized on another monitor comes to the focused monitor's workspace.
    minimize("D")
    msg("focus_right")
    wait_for(lambda: shown()[1] == "HEADLESS-2", "HEADLESS-2 focused")
    msg("workspace", "3")
    wait_for(lambda: shown()[0] == {"HEADLESS-1": 1, "HEADLESS-2": 3}, "HEADLESS-2 on workspace 3")
    desktop.open_window(probe, "E", "app-e", focused=True)
    wait_for(lambda: tiles_on("HEADLESS-2", 3, "E"), "E tiled on HEADLESS-2")
    control("D", "restore")
    wait_for(lambda: windows()["D"].focused and tiles_on("HEADLESS-2", 3, "DE"),
             "D restored as a tile beside E on HEADLESS-2")
    assert shown() == ({"HEADLESS-1": 1, "HEADLESS-2": 3}, "HEADLESS-2"), shown()
    assert windows()["B"].workspace == windows()["C"].workspace == 2, windows()
print("Windows restored from the taskbar come to the workspace shown, as tiles")
