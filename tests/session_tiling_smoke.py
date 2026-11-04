# SPDX-License-Identifier: GPL-3.0-or-later
"""A saved session keeps each workspace's tiling: restored, every tile goes back to its slot with
the splits' ratios and the directions split_toggle kept, a window `restore ... launch` starts again
takes its slot as it opens rather than the end of the tiling, and a workspace that floats or tiles
on its own (layout.tiling_per_workspace) does so again."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = true, tiling_per_workspace = true, workspaces = 3, gap = 0 },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
}"""

with harness.Compositor(compositor, CONFIG, start=False) as desktop:
    msg, env = desktop.msg, desktop.env
    state = desktop.root / "state"
    # What the probe the compositor launches again calls itself: B, which it stands in for.
    env.update(XDG_STATE_HOME=str(state), PAW_PROBE_TITLE="B", PAW_PROBE_APP_ID="app-b")

    def boxes():
        return {w.title: (w.workspace, w.tiled, w.x, w.y, w.width, w.height)
                for w in desktop.windows()}

    desktop.detail = lambda: f"windows: {boxes()}"

    def focus(title):
        subprocess.run([probe, "--activate", f"app-{title.lower()}"], env=env, check=True,
                       timeout=30, stdout=subprocess.DEVNULL)
        desktop.wait_for(lambda: any(w.title == title and w.focused for w in desktop.windows()),
                         f"{title} focused")

    desktop.start()
    clients = {title: desktop.open_window(probe, title, f"app-{title.lower()}", focused=True)
               for title in "ABCD"}
    desktop.wait_for(lambda: all(b[1] for b in boxes().values()), "all tiled")

    def tiled_area():
        return sum(b[4] * b[5] for t, b in boxes().items() if b[0] == 1 and b[1])

    # D's split turned by hand and A resized.
    before = boxes()
    focus("D")
    msg("split_toggle")
    desktop.wait_for(lambda: boxes() != before, "D's split turned")
    turned = boxes()
    focus("A")
    msg("resize_left", "128")
    desktop.wait_for(lambda: boxes()["A"] != turned["A"], "A resized")
    # Workspace 2 floats on its own, with E on it.
    msg("workspace", "2")
    msg("toggle_tiling")
    desktop.open_window(probe, "E", "app-e", focused=True)
    msg("workspace", "1")
    saved = boxes()
    assert not saved["E"][1] and saved != before, saved

    out = msg("session", "save", "desk")
    assert "saved desk: 5 windows" in out, out
    text = (state / "paw" / "sessions" / "desk").read_text()
    trees = [line.split("\t") for line in text.splitlines() if line.startswith("tree\t")]
    assert len(trees) == 1 and trees[0][1:3] == ["HEADLESS-1", "0"], text
    nodes = trees[0][3].split()
    assert len(nodes) == 7 and sum(n[0] in "hv" for n in nodes) == 1, nodes
    assert "\noutput\tHEADLESS-1\t0\t1\t-0\n" in text, text

    # Scrambled: A and B trade places, C and D side by side again, workspace 2 tiles.
    focus("A")
    msg("swap_next")
    focus("D")
    msg("split_toggle")
    msg("workspace", "2")
    msg("toggle_tiling")
    msg("workspace", "1")
    desktop.wait_for(lambda: boxes()["E"][1] and boxes()["A"][2:4] != saved["A"][2:4],
                     "scrambled")

    out = msg("session", "restore", "desk")
    assert "restored 5, launched 0, not found 0" in out, out
    desktop.wait_for(lambda: boxes() == saved, "every tile back in its slot, E floating")

    # B gone, its sibling takes its half. Launched again, its window takes B's slot back, not
    # the second half of the last tile as a new window would.
    clients["B"].kill()
    desktop.reap(clients["B"])
    desktop.wait_for(lambda: "B" not in boxes() and tiled_area() == 1280 * 720, "B closed")
    out = msg("session", "restore", "desk", "launch")
    assert "restored 4, launched 1, not found 0" in out, out
    desktop.wait_for(lambda: boxes() == saved, "the launched window in B's slot")
print("session tiling passed")
