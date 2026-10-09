# SPDX-License-Identifier: GPL-3.0-or-later
"""The overview with the pointer: the hot corner opens it, pointing selects, a click picks, a
middle click closes a window, the wheel pages workspaces, a click on the strip shows and then
switches to a workspace, dragging a thumbnail onto the strip moves the window, and a click on
the backdrop closes it. Driven by a virtual pointer (pointer_probe)."""
from pathlib import Path
import sys
import time

import harness

compositor, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false, workspaces = 4 },
    overview = { animation = false, gap = 20, hot_corner = "top-left" },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
}"""
SCREEN = (1280, 720)

with harness.Compositor(compositor, CONFIG) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def windows():
        """By title: workspace, focused, visible."""
        rows = desktop.rows("windows")
        return {r[9]: dict(workspace=int(r[0]), focused=r[1] == "1", visible=r[11] == "1",
                           width=int(r[6]), height=int(r[7])) for r in rows}

    def focused():
        return next((t for t, w in windows().items() if w["focused"]), None)

    def overview():
        """The state line, the thumbnails (x, y, width, height, title) and strip cells."""
        lines = msg("get", "overview").splitlines()
        state = lines[0].split()[0]
        if state == "closed":
            return state, None, [], []
        head = lines[1].split(" ", 10)
        info = dict(output=head[1], count=int(head[2]), selected=int(head[3]),
                    viewed=int(head[4]), strip=int(head[5]),
                    area=tuple(int(n) for n in head[6:10]), filter=head[10])
        thumbs, cells = [], []
        for line in lines[2:]:
            kind, x, y, w, h, tail = line.split(" ", 5)
            fields = tail.split("\t")
            if kind == "overview-window":
                thumbs.append((int(x), int(y), int(w), int(h), fields[1]))
            else:
                cells.append((int(x), int(y), int(w), int(h), int(fields[0]), int(fields[1])))
        return state, info, thumbs, cells

    def titles():
        return [t[4] for t in overview()[2]]

    desktop.detail = lambda: f"windows: {windows()} overview: {overview()}"

    def center(rect):
        return str(rect[0] + rect[2] // 2), str(rect[1] + rect[3] // 2)

    clients = {}

    def open_window(title):
        clients[title] = desktop.spawn([probe, "--window-only"],
                                       env=dict(PAW_PROBE_TITLE=title,
                                                PAW_PROBE_APP_ID="zz"))
        wait_for(lambda: title in windows(), f"{title} mapped")
        wait_for(lambda: focused() == title, f"{title} focused")

    pointer = desktop.virtual_pointer(pointer_probe, *SCREEN)
    open_window("A")
    open_window("B")
    msg("workspace", "2")
    open_window("C")
    msg("workspace", "1")
    wait_for(lambda: focused() == "B", "B focused on workspace 1")

    # Entering the hot corner opens the overview; staying in it does not reopen it.
    pointer("move", "640", "360")
    assert overview()[0] == "closed"
    pointer("move", "0", "0")
    wait_for(lambda: overview()[0] == "open", "the hot corner opens it")
    assert titles() == ["B", "A"], titles()

    # Cancelled with the pointer still in the corner, it does not reopen on moving there.
    msg("overview_cancel")
    pointer("move", "1", "1")
    time.sleep(0.2)
    assert overview()[0] == "closed", "it reopened without the pointer leaving the corner"

    # A click on the backdrop closes it, changing nothing.
    pointer("move", "640", "360", "move", "0", "0")
    wait_for(lambda: overview()[0] == "open", "reopened from the corner")
    pointer("move", "640", "690", "click", "left")
    wait_for(lambda: overview()[0] == "closed", "a click on the backdrop closes it")
    assert focused() == "B"

    # Pointing selects; a click picks.
    pointer("move", "640", "360", "move", "0", "0")
    wait_for(lambda: overview()[0] == "open", "reopened from the corner")
    _, _, thumbs, cells = overview()
    pointer("move", *center(thumbs[1]))
    wait_for(lambda: overview()[1]["selected"] == 1, "pointing selects")
    pointer("click", "left")
    wait_for(lambda: focused() == "A", "a click focuses the thumbnail's window")
    wait_for(lambda: overview()[0] == "closed", "and closes the overview")

    # The wheel pages workspaces; the strip shows one, then goes there.
    msg("toggle_overview")
    pointer("move", "640", "360", "scroll", "15")
    wait_for(lambda: overview()[1]["viewed"] == 2, "the wheel shows workspace 2")
    assert titles() == ["C"]
    pointer("scroll", "-15")
    wait_for(lambda: overview()[1]["viewed"] == 1, "and back")
    _, _, _, cells = overview()
    pointer("move", *center(cells[2]), "click", "left")
    wait_for(lambda: overview()[1]["viewed"] == 3, "a click on the strip shows a workspace")
    assert msg("get", "workspace").strip() == "1"
    pointer("click", "left")
    wait_for(lambda: msg("get", "workspace").strip() == "3", "a second click goes there")
    wait_for(lambda: overview()[0] == "closed", "closing")
    msg("workspace", "1")

    # Dragging a thumbnail onto a workspace in the strip moves the window there.
    msg("toggle_overview")
    _, _, thumbs, cells = overview()
    names = titles()
    dragged = names[0]
    pointer("move", *center(thumbs[0]), "press", "left")
    x, y = center(thumbs[0])
    pointer("move", str(int(x) + 40), str(int(y) - 40))
    pointer("move", *center(cells[3]), "sleep", "50")
    pointer("release", "left")
    wait_for(lambda: windows()[dragged]["workspace"] == 4, "the drop moved the window")
    assert overview()[0] == "open" and titles() == [n for n in names if n != dragged]
    assert focused() != dragged or windows()[dragged]["visible"] is False
    # Releasing over nothing puts it back.
    remaining = titles()[0]
    _, _, thumbs, _ = overview()
    pointer("move", *center(thumbs[0]), "press", "left")
    x, y = center(thumbs[0])
    pointer("move", str(int(x) + 60), str(int(y) + 20), "release", "left")
    time.sleep(0.2)
    assert overview()[0] == "open" and windows()[remaining]["workspace"] == 1

    # A middle click closes a window, which leaves the grid.
    _, _, thumbs, _ = overview()
    closing = clients.pop(remaining)
    desktop.clients.remove(closing)
    pointer("move", *center(thumbs[0]), "click", "middle")
    wait_for(lambda: remaining not in windows(), "the middle click closed the window")
    wait_for(lambda: overview()[1]["count"] == 0, "and it left the grid")
    closing.wait(timeout=30)
print("Overview pointer passed")
