# SPDX-License-Identifier: GPL-3.0-or-later
"""Window groups: group_toggle makes the focused window a group, windows opening next join it
as tabs in one tile, group_next/prev show another member, group_merge_* moves a window into a
neighbour's group, ungroup takes one out, a closing member hands its slot on, and
features.groups = false dissolves every group. Then groups in awkward situations: a group moved
to another workspace, tiling switched off and on, fullscreen, floating groups, the scroll layout,
and closing hidden members."""
from pathlib import Path
import shutil
import sys

import harness

compositor, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])


def config(groups="true", smart_gaps="true"):
    return """return {
    xwayland = false,
    layout = { tiling = true, smart_gaps = %s },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    features = { groups = %s },
}""" % (smart_gaps, groups)


def session(desktop):
    def windows():
        return {w.title: w for w in desktop.windows()}

    desktop.detail = lambda: f"windows: {windows()}"

    def rect(name):
        return windows()[name].box

    def focused():
        return [t for t, w in windows().items() if w.focused]

    clients = {}

    def open_window(title):
        clients[title] = desktop.open_window(probe, title, focused=True)

    def close_window(title):
        clients[title].kill()
        desktop.reap(clients[title])
        desktop.wait_for(lambda: title not in windows(), f"{title} closed")

    def settled(*names):
        """The windows are visible and tiled, with a size."""
        def check():
            first = [rect(n) for n in names]
            return all(windows()[n].tiled and windows()[n].visible for n in names) and \
                all(r[2] > 0 for r in first)
        desktop.wait_for(check, f"{names} tiled")

    return windows, rect, focused, open_window, close_window, settled


with harness.Compositor(compositor, config()) as desktop:
    msg, env = desktop.msg, desktop.env
    windows, rect, focused, open_window, close_window, settled = session(desktop)

    open_window("A")
    open_window("B")
    settled("A", "B")

    # B becomes a group of one; the next window joins it and takes its tile.
    slot = rect("B")
    msg("group_toggle")
    desktop.wait_for(lambda: windows()["B"].group != 0, "B grouped")
    group = windows()["B"].group
    assert windows()["A"].group == 0
    open_window("C")
    desktop.wait_for(lambda: windows()["C"].group == group and not windows()["B"].visible,
                     "C joined B's group")
    settled("C")
    desktop.wait_for(lambda: rect("C") == slot, "C fills B's slot")
    assert not windows()["B"].tiled and windows()["B"].group == group, windows()
    assert len([w for w in windows().values() if w.visible]) == 2, windows()

    # A third tab, then stepping through them and back.
    open_window("D")
    desktop.wait_for(lambda: windows()["D"].group == group and rect("D") == slot,
                     "D joined")
    assert not windows()["C"].visible, windows()
    msg("group_next")   # D -> B (tab order B, C, D wraps)
    desktop.wait_for(lambda: focused() == ["B"] and windows()["B"].visible and
                     rect("B") == slot and not windows()["D"].visible, "B shown")
    msg("group_prev")   # B -> D
    desktop.wait_for(lambda: focused() == ["D"] and rect("D") == slot, "D shown again")
    msg("group_prev")
    desktop.wait_for(lambda: focused() == ["C"] and rect("C") == slot, "C shown")
    assert windows()["A"].tiled and rect("A")[2] > 0, windows()

    # The strip of tabs is drawn over the group's window: one segment per member, the
    # shown one bright blue (only checked where grim is installed).
    grim = shutil.which("grim")
    if grim:
        shot = harness.grab(grim, env)
        x0, y0, width, _ = rect("C")
        third = (width - 6) / 3
        row = y0 + 3
        colors = [shot.at(x0 + third * (i + .5) + 3 * i, row) for i in range(3)]
        lit = [c for c in colors if c[2] > 200 and c[0] < 160]
        assert len(lit) == 1 and colors[1] == lit[0], colors  # C is the middle tab
        assert shot.at(x0 + third + 1.5, row) != lit[0], "gap painted"
        assert shot.at(x0 + 10, y0 + 20) != lit[0], "strip drawn too tall"

    # Clicking a tab brings that window forward.
    pointer = desktop.virtual_pointer(pointer_probe, 1280, 720)
    x0, y0, width, _ = rect("C")
    third = (width - 6) / 3

    def click_tab(index):
        pointer("move", str(int(x0 + third * (index + .5))), str(int(y0 + 3)), "click", "left")
    click_tab(0)
    desktop.wait_for(lambda: focused() == ["B"] and rect("B") == slot, "click on the first tab")
    click_tab(2)
    desktop.wait_for(lambda: focused() == ["D"] and rect("D") == slot, "click on the last tab")
    click_tab(1)
    desktop.wait_for(lambda: focused() == ["C"] and rect("C") == slot, "click on the middle tab")

    # The other windows never moved.
    a = rect("A")
    msg("group_next")
    desktop.wait_for(lambda: focused() == ["D"], "D shown")
    assert rect("A") == a

    # Ungroup takes D out into a tile of its own; B and C stay a group.
    msg("ungroup")
    desktop.wait_for(lambda: windows()["D"].group == 0 and windows()["D"].visible and
                     windows()["D"].tiled, "D ungrouped")
    desktop.wait_for(lambda: windows()["B"].group == group and
                     windows()["C"].group == group, "B and C still grouped")
    assert focused() == ["D"]
    visible = [t for t, w in windows().items() if w.visible]
    assert len(visible) == 3 and "D" in visible, windows()
    d_slot = rect("D")
    assert d_slot != slot and d_slot[2] > 0, (d_slot, slot)

    # Closing the shown member hands the slot to the next tab.
    shown = "C" if windows()["C"].visible else "B"
    other = "B" if shown == "C" else "C"
    slot = rect(shown)  # the slot shrank when D took half of it
    close_window(shown)
    desktop.wait_for(lambda: windows()[other].visible and windows()[other].tiled and
                     rect(other) == slot, "the other tab took the slot")
    # A group of one dissolves when its second-to-last member goes.
    assert windows()[other].group == 0, windows()

    # Merging: D moves into the group of the window beside it (A, to its right).
    assert rect("A")[0] > rect("D")[0], (rect("A"), rect("D"))
    a_slot = rect("A")
    msg("group_merge_right")
    desktop.wait_for(lambda: windows()["D"].group != 0 and
                     windows()["D"].group == windows()["A"].group, "D merged")
    assert windows()["D"].visible and not windows()["A"].visible, windows()
    assert focused() == ["D"] and rect("D") == a_slot, (focused(), rect("D"), a_slot)
    # Its old slot went to the other window that was there; nothing is left over.
    desktop.wait_for(lambda: rect(other)[3] > 600, f"{other} fills the freed tile")

    # Toggling inside a group dissolves it, the hidden members tile beside the shown.
    msg("group_toggle")
    desktop.wait_for(lambda: all(w.group == 0 and w.visible and w.tiled
                                 for w in windows().values()), "group dissolved")

    # A group of one whose feature is turned off is dissolved on reload.
    msg("group_toggle")
    desktop.wait_for(lambda: any(w.group for w in windows().values()), "grouped again")
    desktop.reload(config(groups="false"))
    desktop.wait_for(lambda: not any(w.group for w in windows().values()), "groups gone")
    msg("group_toggle")
    msg("group_next")
    assert not any(w.group for w in windows().values()), windows()

# The awkward situations. A lone tile keeps its gaps, which tell it from a fullscreen window.
with harness.Compositor(compositor, config(smart_gaps="false")) as desktop:
    msg = desktop.msg
    windows, rect, focused, open_window, close_window, settled = session(desktop)

    open_window("A")
    open_window("B")
    settled("A", "B")
    msg("group_toggle")
    open_window("C")
    desktop.wait_for(lambda: windows()["C"].group == windows()["B"].group != 0,
                     "C joined")
    group = windows()["B"].group

    # Moving the shown member to another workspace takes the whole group along: the
    # hidden member is on that workspace too and comes forward there.
    msg("move_to_workspace", "2")
    desktop.wait_for(lambda: not windows()["C"].visible and windows()["A"].visible,
                     "C left")
    msg("workspace", "2")
    desktop.wait_for(lambda: windows()["C"].visible and not windows()["B"].visible,
                     "C on workspace 2")
    msg("group_prev")
    desktop.wait_for(lambda: focused() == ["B"] and windows()["B"].visible and
                     windows()["B"].tiled, "B forward on workspace 2")
    assert windows()["B"].workspace == 2 and windows()["C"].workspace == 2, windows()
    msg("workspace", "1")
    desktop.wait_for(lambda: not windows()["B"].visible and windows()["A"].visible,
                     "back on workspace 1")

    # Tiling switched off floats the shown member; the hidden one waits, and comes
    # back into the tiling with it.
    msg("workspace", "2")
    desktop.wait_for(lambda: windows()["B"].visible, "on workspace 2 again")
    msg("toggle_tiling")
    desktop.wait_for(lambda: not windows()["B"].tiled and windows()["B"].visible,
                     "floated")
    assert not windows()["C"].visible and windows()["C"].group == group
    msg("group_next")
    desktop.wait_for(lambda: focused() == ["C"] and windows()["C"].visible and
                     not windows()["B"].visible, "C shown while floating")
    assert not windows()["C"].tiled, windows()
    msg("toggle_tiling")
    desktop.wait_for(lambda: windows()["C"].tiled and not windows()["B"].tiled and
                     not windows()["B"].visible, "C tiled again, B still hidden")

    # Fullscreen: switching tabs leaves it rather than hiding a fullscreen window.
    msg("fullscreen")
    desktop.wait_for(lambda: rect("C")[2] >= 1270, "C fullscreen")
    msg("group_next")
    desktop.wait_for(lambda: focused() == ["B"] and windows()["B"].visible and
                     not windows()["C"].visible, "B shown")
    desktop.wait_for(lambda: rect("B")[2] < 1270, "B not fullscreen")
    msg("group_next")
    desktop.wait_for(lambda: focused() == ["C"] and windows()["C"].visible,
                     "C shown again")
    desktop.wait_for(lambda: rect("C")[2] < 1270 and windows()["C"].tiled,
                     "C left fullscreen")
    msg("group_next")
    desktop.wait_for(lambda: focused() == ["B"] and windows()["B"].visible,
                     "B shown once more")

    # Closing a hidden member leaves the shown one alone, and the group of one goes.
    close_window("C")
    desktop.wait_for(lambda: windows()["B"].group == 0 and windows()["B"].visible and
                     windows()["B"].tiled, "B alone")

    # The scrolling layout: a group is one column slot.
    msg("layout_scroll")
    msg("group_toggle")
    open_window("E")
    desktop.wait_for(lambda: windows()["E"].group == windows()["B"].group != 0,
                     "E joined B in the scroll layout")
    desktop.wait_for(lambda: not windows()["B"].visible and windows()["E"].tiled,
                     "E shown")
    columns = rect("E")
    msg("group_prev")
    desktop.wait_for(lambda: focused() == ["B"] and rect("B") == columns, "B in E's column")
    close_window("B")
    desktop.wait_for(lambda: windows()["E"].visible and windows()["E"].group == 0,
                     "E left")
print("Groups joined, cycled, ungrouped, merged, dissolved, turned off, and followed "
      "workspaces, tiling, fullscreen, the scroll layout, and closing")
