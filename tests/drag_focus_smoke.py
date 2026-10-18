# SPDX-License-Identifier: GPL-3.0-or-later
"""Drag and drop with a real button press, driven by a virtual pointer (pointer_probe): one
window probe drags a line of text from its window, another takes it. The drag is over the window
it began in at once, before the pointer moves, so that one let go there is dropped there; off a
window over the bare desktop it is over nothing. A window activated while the drag lasts, through
the taskbar protocol as the shell brings one forward under a drag resting on its button, comes
forward at once: restored onto the workspace shown if minimized, else with its workspace shown.
Once the drag ends it has the keyboard, which the drag held, and the pointer is on what lies under
it at once rather than once it next moves. Dropped where nothing takes it, the drag is cancelled.
A drag of nothing (no data source, as within one window) that the compositor turns down leaves the
seat as it was, and is let go (under the sanitizers, the compositor's clean exit shows it was
freed)."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe, window_probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:5])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false, workspaces = 2 },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
}"""

with harness.Compositor(compositor, CONFIG) as desktop:
    env, msg = desktop.env, desktop.msg

    def windows():
        """{title: (workspace, focused, minimized, visible, centre, top middle)} of every
        window."""
        found = {}
        for row in desktop.windows():
            x, y, w, h = row.box
            found[row.title] = (row.workspace, row.focused, row.minimized, row.visible,
                                (x + w // 2, y + h // 2), (x + w // 2, y + 2))
        return found

    def seat():
        """{"keyboard": (KIND, NAME), "pointer": ..., "drag": ... while one is under way}."""
        return {row[0]: (row[1], row[2]) for row in desktop.rows("seat")}

    def log(name):
        return (desktop.root / f"{name}.log").read_text()

    desktop.detail = lambda: f"windows: {windows()}, seat: {seat()}\n{log('source')}{log('target')}"
    # Target leaves its frame to the compositor, which moves it by the strip along its top.
    for title, role, frame in (("Source", "source", {}), ("Target", "target", {"PAW_PROBE_SSD": "1"}),
                               ("Away", "", {})):
        desktop.spawn([probe, "--window-only"], log=f"{title.lower()}.log",
                      env={"PAW_PROBE_TITLE": title, "PAW_PROBE_APP_ID": title.lower(),
                           "PAW_PROBE_DRAG": role, **frame})
        desktop.wait_for(lambda: title in windows(), f"{title} mapped")
    for request in (["Target", "minimize"], ["Away", "workspace", "2"]):
        subprocess.run([window_probe, *request], env=env, check=True, timeout=30,
                       stdout=subprocess.DEVNULL)
    desktop.wait_for(lambda: windows()["Target"][2] and windows()["Away"][0] == 2,
                     "Target minimized and Away on workspace 2")

    def activate(app_id):
        """What the taskbar does to bring a window forward."""
        subprocess.run([probe, "--activate", app_id], env=env, check=True, timeout=30,
                       stdout=subprocess.DEVNULL)

    activate("source")
    desktop.wait_for(lambda: windows()["Source"][1], "Source focused")
    pointer = desktop.virtual_pointer(pointer_probe, 1280, 720)
    pointer("move", *map(str, windows()["Source"][4]), "press", "left")
    desktop.wait_for(lambda: "drag" in seat(), "the drag under way")
    assert seat()["keyboard"] == ("window", "Source"), seat()
    desktop.wait_for(lambda: seat()["drag"] == ("window", "Source"),
                     "the drag over the window it began in before the pointer moved")

    # Mid-drag, a window on another workspace comes forward, its workspace with it.
    activate("away")
    desktop.wait_for(lambda: windows()["Away"][1] and windows()["Away"][3] and
                     msg("get", "workspace").strip() == "2", "Away brought forward mid-drag")
    # And a minimized one, onto the workspace shown, over the windows there.
    activate("target")
    desktop.wait_for(lambda: windows()["Target"][1] and not windows()["Target"][2] and
                     windows()["Target"][0] == 2 and msg("get", "workspace").strip() == "2",
                     "Target restored mid-drag")
    # The drag goes on onto it, its top too, where a press would move it, and it takes the
    # text as it is dropped.
    pointer("move", *map(str, windows()["Target"][5]))
    desktop.wait_for(lambda: seat().get("drag") == ("window", "Target"), "the drag over Target's top")
    pointer("move", *map(str, windows()["Target"][4]))
    desktop.wait_for(lambda: seat().get("drag") == ("window", "Target"), "the drag over Target")
    desktop.wait_for(lambda: "drag target text/plain" in log("source"), "Target taking the drag")
    # Off it over the bare desktop, it is over nothing, where letting go would drop it nowhere;
    # back on it, it is Target's again.
    taken = log("source").count("drag target text/plain")
    pointer("move", "1000", "600")
    desktop.wait_for(lambda: seat().get("drag") == ("-", "-"), "the drag off Target over the bare desktop")
    pointer("move", *map(str, windows()["Target"][4]))
    desktop.wait_for(lambda: seat().get("drag") == ("window", "Target") and
                     log("source").count("drag target text/plain") > taken, "the drag back over Target")
    assert seat()["keyboard"] == ("window", "Source"), "the keyboard left Source during the drag"
    pointer("release", "left")
    desktop.wait_for(lambda: "drop received paw drag" in log("target"), "the text dropped")
    desktop.wait_for(lambda: "drag finished" in log("source"), "the drag finished")
    assert "drag dropped" in log("source") and "drag cancelled" not in log("source"), log("source")
    # The window brought forward has the keyboard now, and the pointer is on it.
    desktop.wait_for(lambda: seat() == {"keyboard": ("window", "Target"),
                                        "pointer": ("window", "Target")},
                     "the keyboard and the pointer given to Target once the drag ended")

    # Dropped on the bare desktop, nothing takes it: the drag is cancelled.
    activate("source")
    desktop.wait_for(lambda: windows()["Source"][1], "Source focused again")
    pointer("move", *map(str, windows()["Source"][4]), "press", "left")
    desktop.wait_for(lambda: "drag" in seat(), "the second drag under way")
    pointer("move", "1000", "600")
    desktop.wait_for(lambda: seat().get("drag") == ("-", "-"), "the drag over the bare desktop")
    pointer("release", "left")
    desktop.wait_for(lambda: "drag cancelled" in log("source"), "the drag cancelled")
    assert log("target").count("drop received") == 1, log("target")
    assert seat() == {"keyboard": ("window", "Source"), "pointer": ("-", "-")}, seat()

    # A drag of nothing, first for a serial no press gave, which is turned down.
    typed = desktop.root / "typed.txt"
    typed.write_text("")
    desktop.spawn([probe, "--window-only"], log="bare.log",
                  env={"PAW_PROBE_TITLE": "Bare", "PAW_PROBE_APP_ID": "bare",
                       "PAW_PROBE_DRAG": "bare", "PAW_PROBE_TYPED": str(typed)})
    desktop.wait_for(lambda: "Bare" in windows() and windows()["Bare"][1], "Bare focused")
    pointer("move", *map(str, windows()["Bare"][4]), "press", "left")
    desktop.wait_for(lambda: "bare drag asked" in log("bare"), "the first bare drag asked for")
    assert seat() == {"keyboard": ("window", "Bare"), "pointer": ("window", "Bare")}, seat()
    pointer("release", "left")
    # The keyboard still types into the window: the drag let go of the grab it was started with.
    msg("type", "a")
    desktop.wait_for(lambda: typed.read_text() == "a", "a key typed after the drag turned down")
    # The next, from a real press, is under way until the button is let go.
    pointer("press", "left")
    desktop.wait_for(lambda: log("bare").count("bare drag asked") == 2, "the second asked for")
    assert "drag" in seat(), seat()
    pointer("release", "left")
    desktop.wait_for(lambda: seat() == {"keyboard": ("window", "Bare"),
                                        "pointer": ("window", "Bare")}, "the bare drag over")
    msg("type", "b")
    desktop.wait_for(lambda: typed.read_text() == "ab", "a key typed after the bare drag")
print("A window brought forward during a drag comes forward at once and has the keyboard once "
      "it ends; a drop on a window delivers, one on the desktop is cancelled; a drag of nothing "
      "turned down leaves the seat as it was")
