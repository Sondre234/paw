# SPDX-License-Identifier: GPL-3.0-or-later
"""Window groups in awkward situations: a group moved to another workspace, tiling switched off
and on, fullscreen, floating groups, the scroll layout, and closing hidden members."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

# A lone tile keeps its gaps, which tell it from a fullscreen window.
CONFIG = """return {
    xwayland = false,
    layout = { tiling = true, smart_gaps = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    features = { groups = %s },
}"""

with harness.Compositor(compositor, CONFIG % "true") as desktop:
    msg = desktop.msg

    def rows():
        """By title: the workspace, then the columns of `get windows` from x on."""
        return {r[9]: (int(r[0]),) for r in desktop.rows("windows")}

    def windows():
        """By title: focused, tiled, x, y, width, height, visible, group."""
        rows = desktop.rows("windows")
        return {r[9]: dict(focused=r[1] == "1", tiled=r[3] == "1", x=int(r[4]), y=int(r[5]),
                           width=int(r[6]), height=int(r[7]), visible=r[11] == "1",
                           group=int(r[14]))
                for r in rows}

    desktop.detail = lambda: f"windows: {windows()}"

    def rect(name):
        w = windows()[name]
        return (w["x"], w["y"], w["width"], w["height"])

    def focused():
        return [t for t, w in windows().items() if w["focused"]]

    clients = {}

    def open_window(title):
        clients[title] = desktop.spawn([probe, "--window-only"],
                                       env={"PAW_PROBE_TITLE": title})
        desktop.wait_for(lambda: title in windows() and windows()[title]["focused"],
                         f"{title} mapped")

    def close_window(title):
        clients[title].kill()
        desktop.reap(clients[title])
        desktop.wait_for(lambda: title not in windows(), f"{title} closed")

    def settled(*names):
        """The windows are visible and tiled, with a size."""
        def check():
            first = [rect(n) for n in names]
            return all(windows()[n]["tiled"] and windows()[n]["visible"] for n in names) and \
                all(r[2] > 0 for r in first)
        desktop.wait_for(check, f"{names} tiled")

    open_window("A")
    open_window("B")
    settled("A", "B")
    msg("group_toggle")
    open_window("C")
    desktop.wait_for(lambda: windows()["C"]["group"] == windows()["B"]["group"] != 0,
                     "C joined")
    group = windows()["B"]["group"]

    # Moving the shown member to another workspace takes the whole group along: the
    # hidden member is on that workspace too and comes forward there.
    msg("move_to_workspace", "2")
    desktop.wait_for(lambda: not windows()["C"]["visible"] and windows()["A"]["visible"],
                     "C left")
    msg("workspace", "2")
    desktop.wait_for(lambda: windows()["C"]["visible"] and not windows()["B"]["visible"],
                     "C on workspace 2")
    msg("group_prev")
    desktop.wait_for(lambda: focused() == ["B"] and windows()["B"]["visible"] and
                     windows()["B"]["tiled"], "B forward on workspace 2")
    assert rows()["B"][0] == 2 and rows()["C"][0] == 2, rows()
    msg("workspace", "1")
    desktop.wait_for(lambda: not windows()["B"]["visible"] and windows()["A"]["visible"],
                     "back on workspace 1")

    # Tiling switched off floats the shown member; the hidden one waits, and comes
    # back into the tiling with it.
    msg("workspace", "2")
    desktop.wait_for(lambda: windows()["B"]["visible"], "on workspace 2 again")
    msg("toggle_tiling")
    desktop.wait_for(lambda: not windows()["B"]["tiled"] and windows()["B"]["visible"],
                     "floated")
    assert not windows()["C"]["visible"] and windows()["C"]["group"] == group
    msg("group_next")
    desktop.wait_for(lambda: focused() == ["C"] and windows()["C"]["visible"] and
                     not windows()["B"]["visible"], "C shown while floating")
    assert not windows()["C"]["tiled"], windows()
    msg("toggle_tiling")
    desktop.wait_for(lambda: windows()["C"]["tiled"] and not windows()["B"]["tiled"] and
                     not windows()["B"]["visible"], "C tiled again, B still hidden")

    # Fullscreen: switching tabs leaves it rather than hiding a fullscreen window.
    msg("fullscreen")
    desktop.wait_for(lambda: rect("C")[2] >= 1270, "C fullscreen")
    msg("group_next")
    desktop.wait_for(lambda: focused() == ["B"] and windows()["B"]["visible"] and
                     not windows()["C"]["visible"], "B shown")
    desktop.wait_for(lambda: rect("B")[2] < 1270, "B not fullscreen")
    msg("group_next")
    desktop.wait_for(lambda: focused() == ["C"] and windows()["C"]["visible"],
                     "C shown again")
    desktop.wait_for(lambda: rect("C")[2] < 1270 and windows()["C"]["tiled"],
                     "C left fullscreen")
    msg("group_next")
    desktop.wait_for(lambda: focused() == ["B"] and windows()["B"]["visible"],
                     "B shown once more")

    # Closing a hidden member leaves the shown one alone, and the group of one goes.
    close_window("C")
    desktop.wait_for(lambda: windows()["B"]["group"] == 0 and windows()["B"]["visible"] and
                     windows()["B"]["tiled"], "B alone")

    # The scrolling layout: a group is one column slot.
    msg("layout_scroll")
    msg("group_toggle")
    open_window("E")
    desktop.wait_for(lambda: windows()["E"]["group"] == windows()["B"]["group"] != 0,
                     "E joined B in the scroll layout")
    desktop.wait_for(lambda: not windows()["B"]["visible"] and windows()["E"]["tiled"],
                     "E shown")
    columns = rect("E")
    msg("group_prev")
    desktop.wait_for(lambda: focused() == ["B"] and rect("B") == columns, "B in E's column")
    close_window("B")
    desktop.wait_for(lambda: windows()["E"]["visible"] and windows()["E"]["group"] == 0,
                     "E left")
print("Groups followed workspaces, tiling, fullscreen, the scroll layout, and closing")
