# SPDX-License-Identifier: GPL-3.0-or-later
"""The overview on a second monitor: it lists only that monitor's windows, in the monitor's own
coordinates, draws on that monitor (checked on a screenshot), and takes pointer clicks at the
monitor's place in the layout."""
from pathlib import Path
import shutil
import sys

import harness

compositor, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false, workspaces = 3 },
    overview = { animation = false },
    outputs = { order = { "HEADLESS-1", "HEADLESS-2" },
                monitors = { ["HEADLESS-1"] = { mode = "1280x720" },
                             ["HEADLESS-2"] = { mode = "800x600" } } },
    windows = { rules = { { title = "^(C|D)$", output = "HEADLESS-2" } } },
}"""
LAYOUT = (2080, 720)

with harness.Compositor(compositor, CONFIG, env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    msg = desktop.msg

    def windows():
        return {w.title: dict(focused=w.focused, output=w.output) for w in desktop.windows()}

    def focused():
        return next((t for t, w in windows().items() if w["focused"]), None)

    def overview():
        lines = msg("get", "overview").splitlines()
        thumbs = []
        for line in lines[2:]:
            if line.startswith("overview-window"):
                _, x, y, w, h, tail = line.split(" ", 5)
                thumbs.append((int(x), int(y), int(w), int(h), tail.split("\t")[1]))
        overview.area = tuple(int(n) for n in lines[1].split(" ")[6:10]) if len(lines) > 1 else None
        return lines[0].split()[0], lines[1].split(" ")[1] if len(lines) > 1 else None, thumbs

    desktop.detail = lambda: f"windows: {windows()} overview: {overview()}"
    pointer = desktop.virtual_pointer(pointer_probe, *LAYOUT)

    # A's client also puts a 48 pixel panel on the second monitor: the overview keeps clear of it.
    for title in ("A", "C", "D"):
        desktop.spawn([probe, "--external-control" if title == "A" else "--window-only"],
                      env={"PAW_PROBE_TITLE": title, "PAW_PROBE_APP_ID": "zz"})
        desktop.wait_for(lambda: title in windows() and focused() == title, f"{title} focused")
    assert windows()["C"]["output"] == "HEADLESS-2", windows()

    msg("output", "HEADLESS-2", "toggle_overview")
    state, where, thumbs = overview()
    assert state == "open" and where == "HEADLESS-2", (state, where)
    assert sorted(t[4] for t in thumbs) == ["C", "D"], thumbs
    for x, y, w, h, _ in thumbs:
        assert 0 <= x and x + w <= 800 and 0 <= y and y + h <= 600, thumbs

    grim = shutil.which("grim")
    if grim:
        def drawn():
            shot = harness.grab(grim, desktop.env, "HEADLESS-2")
            return all(all(abs(a - b) < 6 for a, b in
                           zip(shot.at(x + w // 2, y + h - 3), (0x41, 0x7b, 0xc4)))
                       for x, y, w, h, _ in overview()[2])
        desktop.wait_for(drawn, "thumbnails on the second monitor")
        print("Screenshot checked")

    # The thumbnails keep clear of the panel, and a click on the panel closes the overview
    # (and goes on to the panel; the probe panel takes no input to prove that).
    x, y, w, h = overview.area
    assert (w, h) == (800, 600 - 48) and y in (0, 48), overview.area
    assert all(y <= t[1] and t[1] + t[3] <= y + h for t in thumbs), (thumbs, overview.area)
    panel_y = 600 - 20 if y == 0 else 20
    pointer("move", str(1280 + 400), str(panel_y), "click", "left")
    desktop.wait_for(lambda: overview()[0] == "closed", "a click on the panel closes it")
    msg("output", "HEADLESS-2", "toggle_overview")

    # A click at the monitor's place in the layout picks the window there.
    x, y, w, h, title = thumbs[0]
    pointer("move", str(1280 + x + w // 2), str(y + h // 2), "click", "left")
    desktop.wait_for(lambda: focused() == title, f"{title} focused by the click")
    desktop.wait_for(lambda: overview()[0] == "closed", "closed by the click")

    # The first monitor's overview lists its own window only.
    msg("output", "HEADLESS-1", "toggle_overview")
    _, where, thumbs = overview()
    assert where == "HEADLESS-1" and [t[4] for t in thumbs] == ["A"], (where, thumbs)
print("Overview outputs passed")
