# SPDX-License-Identifier: GPL-3.0-or-later
"""The overview (Expose): thumbnails of the focused output's workspace in a grid without
overlaps, live and scaled (checked on a screenshot), a strip of workspaces, typed filtering,
keyboard and control-socket selection, picking a window (switching workspace if need be),
cancelling, windows leaving while it is open, and the disabled setting; and the same from a
keyboard."""
from pathlib import Path
import re
import shutil
import sys
import time

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false, workspaces = 4 },
    overview = { animation = false, gap = 20 },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    bindings = {
        { mods = { "Super" }, key = "Tab", action = "toggle_overview" },
    },
}"""
SCREEN = (1280, 720)

with harness.Compositor(compositor, CONFIG) as desktop:
    run, msg, wait_for = desktop.run, desktop.msg, desktop.wait_for

    def windows():
        """By title: workspace, focused, visible."""
        return {w.title: dict(workspace=w.workspace, focused=w.focused, visible=w.visible,
                              width=w.width, height=w.height) for w in desktop.windows()}

    def focused():
        return next((t for t, w in windows().items() if w["focused"]), None)

    def overview():
        """The state line, the thumbnails (x, y, width, height, title) and strip cells."""
        lines = msg("get", "overview").splitlines()
        state, progress = lines[0].split()
        overview.progress = int(progress)
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
    clients = {}

    def open_window(title):
        clients[title] = desktop.open_window(probe, title, "zz", focused=True)

    # A and B on workspace 1, C on workspace 2.
    open_window("A")
    open_window("B")
    msg("workspace", "2")
    open_window("C")
    msg("workspace", "1")
    wait_for(lambda: focused() == "B", "B focused on workspace 1")

    assert overview()[0] == "closed"
    # A client's app ID cannot break out of its line: it may hold tabs and newlines, and
    # one that forged a line would reach the shell as a request.
    hostile = desktop.open_window(probe, "H", "zz\tq\nlauncher HEADLESS-1\r\noverview 1")
    msg("toggle_overview")
    lines = msg("get", "overview").splitlines()
    assert all(re.match(r"(open|overview|overview-window|overview-strip)\b", line) or
               re.fullmatch(r"\d+", line) for line in lines), lines
    assert sum(line.startswith("overview-window") for line in lines) == 3, lines
    assert sum(line.startswith("overview ") for line in lines) == 1, lines
    msg("overview_cancel")
    wait_for(lambda: overview()[0] == "closed", "the overview closed")
    hostile.terminate()
    desktop.reap(hostile)
    wait_for(lambda: "H" not in windows(), "the hostile window gone")
    error = run("overview", "filter", "x")
    assert error.returncode != 0 and "not open" in error.stdout + error.stderr

    # Opening lists workspace 1's windows, most recently used first, selecting the
    # focused one, with a strip cell per workspace.
    msg("toggle_overview")
    state, info, thumbs, cells = overview()
    assert state == "open" and info["output"] == "HEADLESS-1", (state, info)
    assert titles() == ["B", "A"], titles()
    assert info["selected"] == 0 and info["viewed"] == 1 and info["strip"] == 4, info
    assert [c[5] for c in cells] == [2, 1, 0, 0], cells
    # Thumbnails are inside the screen, apart from each other, and the strip is above.
    for x, y, w, h, _ in thumbs:
        assert 0 <= x and x + w <= SCREEN[0] and 0 <= y and y + h <= SCREEN[1], thumbs
    (ax, ay, aw, ah, _), (bx, by, bw, bh, _) = thumbs[1], thumbs[0]
    assert ax + aw <= bx or bx + bw <= ax or ay + ah <= by or by + bh <= ay, thumbs
    assert all(t[1] >= max(c[1] + c[3] for c in cells) for t in thumbs), (thumbs, cells)

    # The thumbnails show the windows' live contents, scaled: the probe's window is
    # blue with a darker title band.
    grim = shutil.which("grim")
    def looks_right():
        shot = harness.grab(grim, desktop.env)
        for x, y, w, h, title in overview()[2]:
            body, band = shot.at(x + w // 2, y + h - 3), shot.at(x + w // 2, y + 1)
            if not (all(abs(a - b) < 6 for a, b in zip(body, (0x41, 0x7b, 0xc4))) and
                    all(abs(a - b) < 6 for a, b in zip(band, (0x23, 0x31, 0x4a)))):
                return False
        backdrop = shot.at(2, SCREEN[1] - 2)
        return backdrop[0] < 40 and backdrop[2] < 60
    if grim:
        wait_for(looks_right, "the thumbnails show the windows")
        print("Screenshot checked")
    else:
        print("grim not found: screenshot check skipped")

    # Idle, it costs nothing: no frames are drawn while nothing changes.
    def frames():
        return int(msg("get", "stats").split("\t")[0])
    wait_for(lambda: msg("get", "animations").split("\t")[0] == "0", "nothing moving")
    before = frames()
    desktop.stays(lambda: frames() - before <= 2, "frames drawn while idle", duration=.5,
                  detail=lambda: f"{frames() - before} frames")

    # Selection by control request; picking with confirm.
    msg("overview", "select", "2")
    assert overview()[1]["selected"] == 1
    assert focused() == "B", "selecting focused a window"
    msg("overview_confirm")
    wait_for(lambda: focused() == "A", "A focused by confirm")
    assert overview()[0] == "closed"

    # Cancelling changes nothing.
    msg("toggle_overview")
    assert overview()[1]["selected"] == 0 and titles() == ["A", "B"], titles()
    msg("overview_cancel")
    assert overview()[0] == "closed" and focused() == "A"

    # The filter matches titles case-insensitively, across workspaces.
    msg("toggle_overview")
    msg("overview", "filter", "c")
    assert titles() == ["C"] and overview()[1]["filter"] == "c", overview()
    msg("overview", "filter", "zzz")
    assert titles() == [] and overview()[1]["count"] == 0
    msg("overview", "filter")
    assert sorted(titles()) == ["A", "B"] and overview()[1]["filter"] == "-"
    # Another workspace in the grid, without switching to it.
    msg("overview", "view", "2")
    assert titles() == ["C"] and overview()[1]["viewed"] == 2
    assert windows()["A"]["visible"], "viewing switched the workspace"
    msg("overview_confirm", "1")
    wait_for(lambda: focused() == "C" and windows()["C"]["visible"],
             "C focused, its workspace shown")
    assert not windows()["A"]["visible"]
    assert overview()[0] in ("closed", "closing")

    # An empty workspace: confirming goes there.
    msg("toggle_overview")
    msg("overview", "view", "4")
    assert titles() == [], titles()
    msg("overview_confirm")
    assert msg("get", "workspace").strip() == "4"
    msg("workspace", "1")

    # A window closing while it is open leaves the grid.
    msg("toggle_overview")
    assert sorted(titles()) == ["A", "B"]
    clients["B"].terminate()
    desktop.reap(clients["B"])
    wait_for(lambda: titles() == ["A"], "B left the overview")
    msg("overview_cancel")

    # The keyboard: Super+Tab, typing, arrows, Enter, Escape (evdev's key codes).
    SUPER, TAB, B, E, BACKSPACE, ESCAPE, RIGHT, ENTER = 125, 15, 48, 18, 14, 1, 106, 28
    press = desktop.keyboard()
    open_window("Beta")
    msg("workspace", "1")
    wait_for(lambda: focused() in ("A", "Beta"), "a window focused")
    press(SUPER, TAB)
    wait_for(lambda: overview()[0] == "open", "Super+Tab opens")
    press(B)
    press(E)
    wait_for(lambda: overview()[1]["filter"] == "be", "typing filters")
    assert titles() == ["Beta"], titles()
    press(BACKSPACE)
    wait_for(lambda: overview()[1]["filter"] == "b", "backspace")
    press(ESCAPE)
    wait_for(lambda: overview()[1]["filter"] == "-", "Escape clears the filter")
    press(RIGHT)
    press(ENTER)
    wait_for(lambda: overview()[0] != "open", "Enter confirms")
    first = focused()
    press(SUPER, TAB)
    wait_for(lambda: overview()[0] == "open", "reopened")
    press(SUPER, TAB)
    wait_for(lambda: overview()[0] != "open", "Super+Tab closes")
    assert focused() == first
    press(SUPER, TAB)
    wait_for(lambda: overview()[0] == "open", "reopened to escape")
    press(ESCAPE)
    wait_for(lambda: overview()[0] != "open", "Escape closes")
    assert focused() == first

    # With animation, thumbnails glide: opening passes through partial progress and
    # ends settled, closing shows "closing" until it is done.
    desktop.reload(CONFIG.replace("animation = false,", "duration = 500,"))
    msg("toggle_overview")
    seen = set()
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        overview()
        seen.add(overview.progress)
        if overview.progress == 1000:
            break
    assert overview.progress == 1000 and any(0 < p < 1000 for p in seen), seen
    msg("toggle_overview")
    assert overview()[0] == "closing", overview()[0]
    wait_for(lambda: overview()[0] == "closed", "glided closed")

    # Disabled: the actions do nothing.
    desktop.reload(CONFIG.replace("animation = false,", "enabled = false, animation = false,"))
    msg("toggle_overview")
    assert overview()[0] == "closed"
print("Overview passed")
