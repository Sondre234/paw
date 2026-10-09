# SPDX-License-Identifier: GPL-3.0-or-later
"""Stacking among the windows, as `get stacking` lists it front to back: toggle_above keeps a
window over the floating windows and tiles of its output through focus changes, workspace
switches, snapping, groups and sticky, under a fullscreen window of its output, also while
another output has the focus, until a window there is raised; toggled again it goes back among
the others. A window rule's `above = true` keeps a window above as it opens."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = %s, workspaces = 4 },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    animations = { enabled = false },
    windows = { rules = { { title = "^F$", output = "HEADLESS-2" },
                          { title = "^Pinned$", above = true },
                          { title = "^Quiet$", above = true, focus = false } } },
}"""

with harness.Compositor(compositor, CONFIG % "false") as desktop:
    msg = desktop.msg

    def windows():
        """title -> the window's row of `get windows`."""
        return {row[9]: row for row in desktop.rows("windows")}

    def stacking():
        """(title, layer) of every window, front to back."""
        return [(row[1], row[3]) for row in desktop.rows("stacking")]

    def order():
        return [title for title, _ in stacking()]

    def layer(title):
        return dict(stacking())[title]

    def above(title):
        return windows()[title][15] == "1"

    def focused():
        return [title for title, row in windows().items() if row[1] == "1"]

    desktop.detail = lambda: f"stacking: {stacking()}\nwindows: {windows()}"
    clients = {}

    def launch(title):
        clients[title] = desktop.spawn([probe, "--window-only"],
                                       env={"PAW_PROBE_TITLE": title,
                                            "PAW_PROBE_APP_ID": f"app-{title}"})
        desktop.wait_for(lambda: title in windows() and focused() == [title], f"{title} open")

    def focus(title):
        subprocess.run([probe, "--activate", f"app-{title}"], env=desktop.env, check=True,
                       timeout=30, stdout=subprocess.DEVNULL)
        desktop.wait_for(lambda: focused() == [title], f"{title} focused")

    assert "takes no argument" in msg("toggle_above", "1", ok=False)
    for title in "ABC":
        launch(title)
    assert order() == ["C", "B", "A"], stacking()
    assert {layer(t) for t in "ABC"} == {"normal"}, stacking()
    assert len(windows()["A"]) == 16 and not any(above(t) for t in "ABC"), windows()

    # Kept above, A stays over the others as they are focused and raised.
    focus("A")
    msg("toggle_above")
    assert above("A") and layer("A") == "above", stacking()
    focus("B")
    assert order() == ["A", "B", "C"], stacking()
    focus("C")
    assert order() == ["A", "C", "B"], stacking()
    # Two kept above stack among themselves as focus raises them.
    focus("B")
    msg("toggle_above")
    assert order() == ["B", "A", "C"], stacking()
    focus("A")
    assert order() == ["A", "B", "C"], stacking()
    focus("C")
    assert order() == ["A", "B", "C"], stacking()
    # Let go, B comes back over the others it joins.
    focus("B")
    msg("toggle_above")
    assert not above("B") and order() == ["A", "B", "C"], stacking()
    focus("C")
    assert order() == ["A", "C", "B"], stacking()

    # Workspace switches leave it where it is; a window on the other workspace stays under it.
    msg("workspace", "2")
    launch("D")
    msg("workspace", "1")
    assert order()[0] == "A" and layer("A") == "above", stacking()
    # Sticky, it follows the workspaces and stays above.
    focus("A")
    msg("toggle_sticky")
    msg("workspace", "2")
    focus("D")
    assert windows()["A"][13] == "1" and order()[0] == "A", stacking()
    msg("workspace", "1")
    focus("C")
    assert order()[0] == "A", stacking()
    focus("A")
    msg("toggle_sticky")
    assert windows()["A"][13] == "0" and layer("A") == "above", stacking()

    # Snapped, it stays above.
    msg("snap_left")
    focus("C")
    assert order()[0] == "A" and layer("A") == "above", stacking()
    focus("A")
    msg("restore")

    # A fullscreen window of its output covers it, also while another output has the focus,
    # until a window of its output is raised over the fullscreen one.
    focus("C")
    msg("fullscreen")
    assert stacking()[:2] == [("C", "fullscreen"), ("A", "above")], stacking()
    msg("headless_output", "add", "HEADLESS-2")
    desktop.wait_for(lambda: len(desktop.rows("outputs")) == 2, "the second output")
    launch("F")
    assert windows()["F"][10] == "HEADLESS-2", windows()
    assert layer("C") == "fullscreen" and order().index("C") < order().index("A"), stacking()
    focus("B")
    assert order()[:2] == ["A", "B"] and layer("C") == "normal", stacking()
    subprocess.run([probe, "--close", "app-F"], env=desktop.env, check=True, timeout=30,
                   stdout=subprocess.DEVNULL)
    assert desktop.reap(clients.pop("F")) == 0
    msg("headless_output", "remove", "HEADLESS-2")
    desktop.wait_for(lambda: len(desktop.rows("outputs")) == 1, "the second output gone")
    focus("C")
    assert layer("C") == "fullscreen", stacking()
    msg("fullscreen")
    assert layer("C") == "normal" and order()[0] == "A", stacking()

    # A group takes the slot's state: a window joining a group kept above is kept above, and the
    # group stays above as its tabs change.
    focus("A")
    msg("group_toggle")
    launch("E")
    assert windows()["E"][14] == windows()["A"][14] != "0", windows()
    assert above("E") and layer("E") == "above", stacking()
    focus("C")
    assert order()[0] == "E", stacking()
    focus("A")  # the other tab, which takes the slot
    assert windows()["E"][11] == "0", windows()
    assert above("A") and order()[0] == "A", stacking()
    # Let go, every member is.
    msg("toggle_above")
    assert not above("A") and not above("E"), windows()
    msg("ungroup")

    # A rule keeps a window above as it opens, focused or not.
    launch("Pinned")
    assert above("Pinned") and stacking()[0] == ("Pinned", "above"), stacking()
    clients["Quiet"] = desktop.spawn([probe, "--window-only"],
                                     env={"PAW_PROBE_TITLE": "Quiet",
                                          "PAW_PROBE_APP_ID": "app-Quiet"})
    desktop.wait_for(lambda: "Quiet" in windows(), "Quiet open")
    assert focused() == ["Pinned"] and above("Quiet"), windows()
    assert stacking()[:2] == [("Quiet", "above"), ("Pinned", "above")], stacking()
    focus("Pinned")
    assert order()[:2] == ["Pinned", "Quiet"], stacking()

    for title, client in clients.items():
        subprocess.run([probe, "--close", f"app-{title}"], env=desktop.env, check=True,
                       timeout=30, stdout=subprocess.DEVNULL)
        assert desktop.reap(client) == 0
    desktop.wait_for(lambda: not windows(), "every window closed")

# On a monitor that tiles, a tile kept above stays a tile, over the others.
with harness.Compositor(compositor, CONFIG % "true") as desktop:
    msg = desktop.msg

    def windows():
        return {row[9]: row for row in desktop.rows("windows")}

    def stacking():
        return [(row[1], row[3]) for row in desktop.rows("stacking")]

    desktop.detail = lambda: f"stacking: {stacking()}\nwindows: {windows()}"

    def focus(title):
        subprocess.run([probe, "--activate", f"app-{title}"], env=desktop.env, check=True,
                       timeout=30, stdout=subprocess.DEVNULL)
        desktop.wait_for(lambda: windows()[title][1] == "1", f"{title} focused")

    for title in "ABC":
        desktop.spawn([probe, "--window-only"], env={"PAW_PROBE_TITLE": title,
                                                     "PAW_PROBE_APP_ID": f"app-{title}"})
        desktop.wait_for(lambda: title in windows() and windows()[title][3] == "1",
                         f"{title} tiled")
    focus("B")
    msg("toggle_above")
    assert windows()["B"][3] == "1" and stacking()[0] == ("B", "above"), stacking()
    focus("A")
    assert stacking() == [("B", "above"), ("A", "normal"), ("C", "normal")], stacking()
    focus("B")
    msg("toggle_above")

    # layout.floating_above_tiles off, as by default: a tile brought forward goes over a
    # floating window.
    focus("C")
    msg("toggle_floating")
    focus("A")
    assert stacking() == [("A", "normal"), ("C", "normal"), ("B", "normal")], stacking()
    # On, floating windows come up over every tile, keeping their order, and stay there.
    desktop.reload(CONFIG % "true, floating_above_tiles = true")
    assert stacking() == [("C", "floating"), ("A", "normal"), ("B", "normal")], stacking()
    focus("B")
    assert stacking() == [("C", "floating"), ("B", "normal"), ("A", "normal")], stacking()
    # Kept above, a tile is over them still.
    msg("toggle_above")
    assert stacking()[:2] == [("B", "above"), ("C", "floating")], stacking()
    msg("toggle_above")
    # A tile floating goes up among them, a floating window tiling down among the tiles.
    msg("toggle_floating")
    assert stacking() == [("B", "floating"), ("C", "floating"), ("A", "normal")], stacking()
    focus("C")
    msg("toggle_floating")
    assert windows()["C"][3] == "1", windows()
    assert stacking() == [("B", "floating"), ("C", "normal"), ("A", "normal")], stacking()
    # Off again, the floating windows go back among the tiles, in front.
    desktop.reload(CONFIG % "true")
    assert stacking() == [("B", "normal"), ("C", "normal"), ("A", "normal")], stacking()
    focus("A")
    assert stacking()[0] == ("A", "normal"), stacking()
print("Windows kept above stay over the others, under fullscreen, and go back when let go; "
      "floating windows stay over tiles with layout.floating_above_tiles")
