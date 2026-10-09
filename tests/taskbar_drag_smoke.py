# SPDX-License-Identifier: GPL-3.0-or-later
"""Dragging over the taskbar in a session, as on Windows: a drag from a window (a real button
press, driven by pointer_probe, on a window probe that drags a line of text) resting half a second
on another window's button brings that window forward, a minimized one restored and one on another
workspace with its workspace; resting on a stack's button opens the card of its windows' pictures
in the popover, which the drag reaches as it maps, and resting on a picture brings that window
forward. A drag only crossing the buttons does nothing, and the card closes once the drag has left
it and its button. Dropped on the bar, the drag is cancelled, the shell having taken nothing: the
drag's source hears no target but none. Carried on to the window brought forward, it is dropped
there, and that window has the keyboard once the drag ends."""
from pathlib import Path
import subprocess
import sys
import time

import harness

compositor, shell, probe, window_probe, pointer_probe = (str(Path(p).resolve())
                                                          for p in sys.argv[1:6])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false, workspaces = 2 },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    notifications = { enabled = false },
    shell = { panel_height = 52 },
}"""
WIDTH, HEIGHT, BAR = 1280, 720, 52
# The windows' buttons, icons only, follow the start button in the order the windows opened: their
# middles, along the middle of the bar. Then bare bar, where no button is.
BUTTONS = [str(77 + 44 * i) for i in range(4)]
ROW, BARE = str(HEIGHT - BAR // 2), "600"
# Where the card of a stack of two opens, over the last button but kept inside the output: its
# first picture.
FIRST_PICTURE = ("140", "600")

with harness.Compositor(compositor, CONFIG, start=False) as desktop:
    root, env, msg = desktop.root, desktop.env, desktop.msg
    env.update(QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software",
               QT_FORCE_STDERR_LOGGING="1", XDG_DATA_HOME=str(root), XDG_DATA_DIRS=str(root),
               XDG_STATE_HOME=str(root), DBUS_SESSION_BUS_ADDRESS="disabled:")
    desktop.start()
    shell_log = root / "shell.log"
    desktop.spawn([shell, "--config", str(desktop.config)], log="shell.log")
    desktop.wait_for(lambda: "paw surface rendered: paw taskbar" in shell_log.read_text(),
                     "the panel")

    def windows():
        """{title: (workspace, focused, minimized, centre)} of every window."""
        found = {}
        for row in desktop.rows("windows"):
            x, y, w, h = (int(n) for n in row[4:8])
            found[row[9]] = (int(row[0]), row[1] == "1", row[2] == "1", (str(x + w // 2), str(y + h // 2)))
        return found

    def focused():
        return [title for title, window in windows().items() if window[1]]

    def workspace():
        return msg("get", "workspace").strip()

    def seat():
        return {row[0]: (row[1], row[2]) for row in desktop.rows("seat")}

    def card():
        """Whether the popover, which shows the card, is up."""
        return ["paw-popover", "HEADLESS-1", "3", "1", "0"] in desktop.rows("layers")

    def log(name):
        return (root / f"{name}.log").read_text()

    desktop.detail = lambda: (f"windows: {windows()}, seat: {seat()}, layers: {desktop.rows('layers')}\n"
                              f"{log('source')}{shell_log.read_text()[-1500:]}")
    for title, app_id, role in (("Source", "source", "source"), ("Target", "target", "target"),
                                ("Away", "away", ""), ("Stack one", "stack", ""),
                                ("Stack two", "stack", "")):
        desktop.spawn([probe, "--window-only"], log=f"{title.lower().replace(' ', '-')}.log",
                      env={"PAW_PROBE_TITLE": title, "PAW_PROBE_APP_ID": app_id,
                           "PAW_PROBE_DRAG": role})
        desktop.wait_for(lambda: title in windows(), f"{title} mapped")
    for request in (["Target", "minimize"], ["Away", "workspace", "2"]):
        subprocess.run([window_probe, *request], env=env, check=True, timeout=30,
                       stdout=subprocess.DEVNULL)

    def activate(app_id):
        subprocess.run([probe, "--activate", app_id], env=env, check=True, timeout=30,
                       stdout=subprocess.DEVNULL)

    activate("source")
    desktop.wait_for(lambda: focused() == ["Source"] and windows()["Target"][2] and
                     windows()["Away"][0] == 2, "Source focused, Target minimized, Away away")
    pointer = desktop.virtual_pointer(pointer_probe, WIDTH, HEIGHT)
    pointer("move", *windows()["Source"][3], "press", "left")
    desktop.wait_for(lambda: "drag" in seat(), "the drag under way")

    # Crossing the buttons on the way elsewhere brings nothing forward.
    pointer("move", BUTTONS[1], ROW, "move", BUTTONS[2], ROW, "move", BUTTONS[3], ROW,
            "move", BARE, ROW)
    assert seat()["drag"] == ("layer", "paw-panel"), seat()
    desktop.stays(lambda: focused() == ["Source"] and windows()["Target"][2] and not card(),
                  "a drag crossing the buttons brought a window forward", duration=.8)

    def rest(x, y, done, message):
        """Rests the drag at x, y until `done`, which must take half a second."""
        pointer("move", x, y)
        started = time.monotonic()
        desktop.wait_for(done, message)
        assert time.monotonic() - started > .4, f"{message} at once"

    rest(BUTTONS[1], ROW, lambda: focused() == ["Target"] and not windows()["Target"][2],
         "the minimized window brought forward")
    rest(BUTTONS[2], ROW, lambda: focused() == ["Away"] and workspace() == "2",
         "the window on another workspace brought forward, with its workspace")
    rest(BUTTONS[3], ROW, card, "the card of the stack's windows")
    assert focused() == ["Away"], focused()
    # The drag reaches the popover that came up for the card, and stays there.
    pointer("move", *FIRST_PICTURE)
    desktop.wait_for(lambda: seat().get("drag") == ("layer", "paw-popover"),
                     "the drag over the card")
    rest(*FIRST_PICTURE, lambda: focused() == ["Stack one"] and workspace() == "1",
         "the first picture's window brought forward")
    assert card(), "the card closed under the drag"
    # Gone from the card and its button, the drag leaves the card to close.
    pointer("move", "900", "300")
    desktop.wait_for(lambda: not card(), "the card closed once the drag left it")

    # Dropped on the bar, nothing takes it.
    pointer("move", BARE, ROW, "release", "left")
    desktop.wait_for(lambda: "drag cancelled" in log("source"), "the drop on the bar cancelled")
    assert "drag target text" not in log("source") and "drag dropped" not in log("source"), log("source")
    desktop.wait_for(lambda: seat() == {"keyboard": ("window", "Stack one"),
                                        "pointer": ("layer", "paw-panel")},
                     "the keyboard with the window brought forward, the pointer on the bar")

    # Brought forward, a window takes the drop carried on to it.
    activate("source")
    desktop.wait_for(lambda: focused() == ["Source"], "Source focused again")
    pointer("move", *windows()["Source"][3], "press", "left")
    desktop.wait_for(lambda: "drag" in seat(), "the second drag under way")
    rest(BUTTONS[1], ROW, lambda: focused() == ["Target"], "Target brought forward")
    pointer("move", *windows()["Target"][3])
    desktop.wait_for(lambda: "drag target text/plain" in log("source"), "Target taking the drag")
    pointer("release", "left")
    desktop.wait_for(lambda: "drop received paw drag" in log("target"), "the text dropped on Target")
    desktop.wait_for(lambda: "drag finished" in log("source"), "the drag finished")
    desktop.wait_for(lambda: seat()["keyboard"] == ("window", "Target"), "the keyboard with Target")

    messages = "\n".join(line for line in shell_log.read_text().splitlines() if not line.startswith("["))
    for message in ("ReferenceError", "TypeError", "is not defined", "Cannot read"):
        assert message not in messages, messages
print("A drag resting on a taskbar button brings its window forward, or its stack's card to rest "
      "on a picture; crossing does nothing, and the bar refuses the drop")
