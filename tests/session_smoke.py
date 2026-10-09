# SPDX-License-Identifier: GPL-3.0-or-later
"""session save / restore / list / delete: a saved arrangement of windows (workspaces, tiled or
floating state, layouts, the current workspace) is put back by matching windows on app ID and
title, and `restore NAME launch` starts an application that is gone and places its new window."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = true, workspaces = 4 },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
}"""

with harness.Compositor(compositor, CONFIG, start=False) as desktop:
    msg, env = desktop.msg, desktop.env
    state = desktop.root / "state"
    env.update(XDG_STATE_HOME=str(state), PAW_PROBE_TITLE="Relaunched",
               PAW_PROBE_APP_ID="app-b")

    def windows():
        """By title: workspace, focused, minimized, tiled, x, y, width, height, kept above."""
        rows = desktop.rows("windows")
        return {r[9]: dict(workspace=int(r[0]), focused=r[1] == "1", tiled=r[3] == "1",
                           x=int(r[4]), y=int(r[5]), width=int(r[6]), height=int(r[7]),
                           above=r[15] == "1")
                for r in rows}

    def summary():
        return {t: (w["workspace"], w["tiled"]) for t, w in windows().items()}

    desktop.detail = lambda: f"windows: {windows()}"

    def focus(title):
        subprocess.run([probe, "--activate", f"app-{title.lower()}"], env=env, check=True,
                       timeout=30, stdout=subprocess.DEVNULL)

    clients = {}
    desktop.start()

    assert "usage" in msg("session", ok=False)
    assert "no session named" in msg("session", "restore", "nothing", ok=False)
    assert "session name" in msg("session", "save", "../evil", ok=False)
    assert "session name" in msg("session", "save", "x.new", ok=False)
    assert msg("session", "list") == ""

    for title in ("A", "B", "C"):
        clients[title] = desktop.spawn([probe, "--window-only"],
                                       env={"PAW_PROBE_TITLE": title,
                                            "PAW_PROBE_APP_ID": f"app-{title.lower()}"})
        desktop.wait_for(lambda: title in windows() and windows()[title]["focused"],
                         f"{title} focused")
    desktop.wait_for(lambda: all(w["tiled"] for w in windows().values()), "all tiled")

    # B goes to workspace 2, C floats, A stays; workspace 1 uses the master layout. B and C are
    # kept above.
    msg("toggle_floating")  # C, focused
    desktop.wait_for(lambda: not windows()["C"]["tiled"], "C floats")
    msg("toggle_above")
    msg("layout_master")
    focus("B")
    desktop.wait_for(lambda: windows()["B"]["focused"], "B focused")
    msg("toggle_above")
    msg("move_to_workspace", "2")
    desktop.wait_for(lambda: windows()["B"]["workspace"] == 2, "B on workspace 2")
    msg("workspace", "3")
    assert {t for t, w in windows().items() if w["above"]} == {"B", "C"}, windows()
    saved_state = summary()
    assert saved_state == {"A": (1, True), "B": (2, True), "C": (1, False)}, saved_state
    saved_c = windows()["C"]
    assert msg("get", "layout").split()[0] == "dwindle"  # workspace 3
    out = msg("session", "save", "work")
    assert "saved work: 3 windows" in out, out

    saved = (state / "paw" / "sessions" / "work").read_text()
    assert saved.startswith("paw-session 1\n"), saved
    assert saved.count("\nwindow\t") == 3 and "\nlayout\tHEADLESS-1\t0\t1\t" in saved
    assert "wayland_probe" in saved and "--window-only" in saved, saved
    listing = msg("session", "list").split("\t")
    assert listing[0] == "work" and listing[1] == "3", listing

    # Scramble everything.
    msg("workspace", "1")
    msg("layout_dwindle")
    focus("A")
    desktop.wait_for(lambda: windows()["A"]["focused"], "A focused")
    msg("move_to_workspace", "4")
    desktop.wait_for(lambda: windows()["A"]["workspace"] == 4, "A moved")
    focus("C")
    desktop.wait_for(lambda: windows()["C"]["focused"], "C focused")
    msg("toggle_floating")
    desktop.wait_for(lambda: windows()["C"]["tiled"], "C tiles")
    msg("toggle_above")
    msg("workspace", "4")
    assert summary() != saved_state and not windows()["C"]["above"]

    out = msg("session", "restore", "work")
    assert "restored 3, launched 0, not found 0" in out, out
    desktop.wait_for(lambda: summary() == saved_state, "windows restored")
    assert windows()["C"]["above"] and windows()["B"]["above"] and not windows()["A"]["above"]
    assert msg("get", "workspace").strip() == "3"
    msg("workspace", "1")
    assert msg("get", "layout").split()[0] == "master"
    after = windows()["C"]
    assert (after["x"], after["y"], after["width"], after["height"]) == \
        (saved_c["x"], saved_c["y"], saved_c["width"], saved_c["height"]), (saved_c, after)

    # A window that is gone is not restored, or launched without `launch`.
    clients["B"].kill()
    desktop.reap(clients["B"])
    desktop.wait_for(lambda: "B" not in windows(), "B closed")
    out = msg("session", "restore", "work")
    assert "restored 2, launched 0, not found 1" in out, out
    assert "B" not in windows() and "Relaunched" not in windows()

    # With `launch` its command runs again and the new window takes B's place.
    out = msg("session", "restore", "work", "launch")
    assert "restored 2, launched 1, not found 0" in out, out
    desktop.wait_for(lambda: "Relaunched" in windows(), "B launched again")
    desktop.wait_for(lambda: windows()["Relaunched"]["workspace"] == 2 and
                     windows()["Relaunched"]["tiled"], "launched window placed")
    assert windows()["Relaunched"]["above"], windows()

    assert msg("session", "delete", "work") == ""
    assert msg("session", "list") == ""
    assert "no session named" in msg("session", "delete", "work", ok=False)

    # A window's title and app ID are its own to choose: tabs, newlines and percent signs
    # must not add records to the file or make it unreadable.
    desktop.spawn([probe, "--window-only"],
                  env={"PAW_PROBE_TITLE": "T\t%41\nwindow\tforged\t1",
                       "PAW_PROBE_APP_ID": "odd id%zz\n"})
    desktop.wait_for(lambda: any(t.startswith("T ") for t in windows()), "the odd window mapped")
    count = len(windows())
    out = msg("session", "save", "odd")
    assert f"saved odd: {count} windows" in out, out
    lines = (state / "paw" / "sessions" / "odd").read_text().split("\n")[:-1]
    kinds = [line.split("\t")[0] for line in lines[1:]]
    assert set(kinds) <= {"output", "layout", "window"}, kinds
    assert kinds.count("window") == count, (kinds, count)
    assert not any(line.startswith("window\tforged") for line in lines), lines
    out = msg("session", "restore", "odd")
    assert f"restored {count}," in out, out
print("session save and restore passed")
