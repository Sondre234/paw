# SPDX-License-Identifier: GPL-3.0-or-later
"""Window groups: group_toggle makes the focused window a group, windows opening next join it
as tabs in one tile, group_next/prev show another member, group_merge_* moves a window into a
neighbour's group, ungroup takes one out, a closing member hands its slot on, and
features.groups = false dissolves every group."""
from pathlib import Path
import shutil
import subprocess
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = true },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    features = { groups = %s },
}"""

with harness.Compositor(compositor, CONFIG % "true") as desktop:
    msg, env = desktop.msg, desktop.env

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

    # B becomes a group of one; the next window joins it and takes its tile.
    slot = rect("B")
    msg("group_toggle")
    desktop.wait_for(lambda: windows()["B"]["group"] != 0, "B grouped")
    group = windows()["B"]["group"]
    assert windows()["A"]["group"] == 0
    open_window("C")
    desktop.wait_for(lambda: windows()["C"]["group"] == group and not windows()["B"]["visible"],
                     "C joined B's group")
    settled("C")
    desktop.wait_for(lambda: rect("C") == slot, "C fills B's slot")
    assert not windows()["B"]["tiled"] and windows()["B"]["group"] == group, windows()
    assert len([w for w in windows().values() if w["visible"]]) == 2, windows()

    # A third tab, then stepping through them and back.
    open_window("D")
    desktop.wait_for(lambda: windows()["D"]["group"] == group and rect("D") == slot,
                     "D joined")
    assert not windows()["C"]["visible"], windows()
    msg("group_next")   # D -> B (tab order B, C, D wraps)
    desktop.wait_for(lambda: focused() == ["B"] and windows()["B"]["visible"] and
                     rect("B") == slot and not windows()["D"]["visible"], "B shown")
    msg("group_prev")   # B -> D
    desktop.wait_for(lambda: focused() == ["D"] and rect("D") == slot, "D shown again")
    msg("group_prev")
    desktop.wait_for(lambda: focused() == ["C"] and rect("C") == slot, "C shown")
    assert windows()["A"]["tiled"] and rect("A")[2] > 0, windows()

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

    # Clicking a tab brings that window forward (where wlrctl can drive the pointer).
    wlrctl = shutil.which("wlrctl")
    if grim and wlrctl:
        x0, y0, width, _ = rect("C")
        third = (width - 6) / 3

        def click_tab(index):
            subprocess.run([wlrctl, "pointer", "move", "-5000", "-5000"], env=env,
                           check=True, timeout=30)
            subprocess.run([wlrctl, "pointer", "move", str(int(x0 + third * (index + .5))),
                            str(int(y0 + 3))], env=env, check=True, timeout=30)
            subprocess.run([wlrctl, "pointer", "click", "left"], env=env, check=True,
                           timeout=30)
        click_tab(0)
        desktop.wait_for(lambda: focused() == ["B"] and rect("B") == slot,
                         "click on the first tab")
        click_tab(2)
        desktop.wait_for(lambda: focused() == ["D"] and rect("D") == slot,
                         "click on the last tab")
        click_tab(1)
        desktop.wait_for(lambda: focused() == ["C"] and rect("C") == slot,
                         "click on the middle tab")

    # The other windows never moved.
    a = rect("A")
    msg("group_next")
    desktop.wait_for(lambda: focused() == ["D"], "D shown")
    assert rect("A") == a

    # Ungroup takes D out into a tile of its own; B and C stay a group.
    msg("ungroup")
    desktop.wait_for(lambda: windows()["D"]["group"] == 0 and windows()["D"]["visible"] and
                     windows()["D"]["tiled"], "D ungrouped")
    desktop.wait_for(lambda: windows()["B"]["group"] == group and
                     windows()["C"]["group"] == group, "B and C still grouped")
    assert focused() == ["D"]
    visible = [t for t, w in windows().items() if w["visible"]]
    assert len(visible) == 3 and "D" in visible, windows()
    d_slot = rect("D")
    assert d_slot != slot and d_slot[2] > 0, (d_slot, slot)

    # Closing the shown member hands the slot to the next tab.
    shown = "C" if windows()["C"]["visible"] else "B"
    other = "B" if shown == "C" else "C"
    slot = rect(shown)  # the slot shrank when D took half of it
    close_window(shown)
    desktop.wait_for(lambda: windows()[other]["visible"] and windows()[other]["tiled"] and
                     rect(other) == slot, "the other tab took the slot")
    # A group of one dissolves when its second-to-last member goes.
    assert windows()[other]["group"] == 0, windows()

    # Merging: D moves into the group of the window beside it (A, to its right).
    assert rect("A")[0] > rect("D")[0], (rect("A"), rect("D"))
    a_slot = rect("A")
    msg("group_merge_right")
    desktop.wait_for(lambda: windows()["D"]["group"] != 0 and
                     windows()["D"]["group"] == windows()["A"]["group"], "D merged")
    assert windows()["D"]["visible"] and not windows()["A"]["visible"], windows()
    assert focused() == ["D"] and rect("D") == a_slot, (focused(), rect("D"), a_slot)
    # Its old slot went to the other window that was there; nothing is left over.
    desktop.wait_for(lambda: rect(other)[3] > 600, f"{other} fills the freed tile")

    # Toggling inside a group dissolves it, the hidden members tile beside the shown.
    msg("group_toggle")
    desktop.wait_for(lambda: all(w["group"] == 0 and w["visible"] and w["tiled"]
                                 for w in windows().values()), "group dissolved")

    # A group of one whose feature is turned off is dissolved on reload.
    msg("group_toggle")
    desktop.wait_for(lambda: any(w["group"] for w in windows().values()), "grouped again")
    desktop.reload(CONFIG % "false")
    desktop.wait_for(lambda: "Configuration reloaded" in desktop.log.read_text(), "reload")
    desktop.wait_for(lambda: not any(w["group"] for w in windows().values()), "groups gone")
    msg("group_toggle")
    msg("group_next")
    assert not any(w["group"] for w in windows().values()), windows()
print("Groups joined, cycled, ungrouped, merged, dissolved, and turned off")
