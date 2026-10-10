# SPDX-License-Identifier: GPL-3.0-or-later
"""A saved session keeps the columns of the scrolling layout: which tiles share a column, in
what order, and the width of each column."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = true, tile_layout = "scroll", gap = 0,
               scroll = { follow = "edge", width = 0.5, step = 0.1 } },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
}"""

with harness.Compositor(compositor, CONFIG, start=False) as desktop:
    msg, env = desktop.msg, desktop.env
    state = desktop.root / "state"
    env["XDG_STATE_HOME"] = str(state)

    def windows():
        return {w.title: dict(focused=w.focused, tiled=w.tiled, x=w.x, y=w.y,
                              width=w.width, height=w.height) for w in desktop.windows()}

    def shape():
        """Widths, heights and the arrangement relative to window A, whatever the view shows."""
        w = windows()
        return {t: (v["x"] - w["A"]["x"], v["y"], v["width"], v["height"]) for t, v in w.items()}

    desktop.detail = lambda: f"windows: {windows()}"

    def focus(title):
        subprocess.run([probe, "--activate", f"app-{title.lower()}"], env=env, check=True,
                       timeout=5, stdout=subprocess.DEVNULL)
        desktop.wait_for(lambda: windows()[title]["focused"], f"{title} focused")

    desktop.start()

    for title in ("A", "B", "C"):
        desktop.open_window(probe, title, f"app-{title.lower()}", focused=True)
    desktop.wait_for(lambda: all(w["tiled"] for w in windows().values()), "all tiled")

    # Columns: [A] [B over C]; A is wider than the others.
    focus("C")
    msg("consume_left")
    focus("A")
    msg("column_widen")
    desktop.wait_for(lambda: windows()["A"]["width"] == 768, "A widened")
    desktop.wait_for(lambda: windows()["B"]["x"] == windows()["C"]["x"] and
                     windows()["C"]["y"] > windows()["B"]["y"], "C stacked under B")
    saved = shape()
    assert saved["B"][3] == saved["C"][3] == 360 and saved["B"][2] == 640, saved

    out = msg("session", "save", "columns")
    assert "saved columns: 3 windows" in out, out
    text = (state / "paw" / "sessions" / "columns").read_text()
    layout_line = [l for l in text.splitlines() if l.startswith("layout\t")]
    assert len(layout_line) == 1 and layout_line[0].endswith("\t0.6000,0.5000"), text

    # Scramble: C leaves its stack, A gets another width, B is widened.
    focus("C")
    msg("expel")
    focus("A")
    msg("column_cycle_width")
    focus("B")
    msg("column_widen")
    desktop.wait_for(lambda: shape() != saved, "arrangement changed")
    assert shape()["C"][0] != shape()["B"][0]

    out = msg("session", "restore", "columns")
    assert "restored 3, launched 0, not found 0" in out, out
    desktop.wait_for(lambda: shape() == saved, "columns restored")
    assert msg("session", "delete", "columns") == ""
print("session scroll columns passed")
