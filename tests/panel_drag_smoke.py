# SPDX-License-Identifier: GPL-3.0-or-later
"""Dragging the taskbar to another edge (panel_drag.c): `panel_drag` takes the pointer while its
left button is held, and is refused otherwise. The panel pressed hears the pointer leave, and
neither it nor a window under the pointer hears anything more until the drag is over; the shell
hears the output under the pointer and the edge of it nearest the pointer whenever either changes,
and where the button is let go. `panel_drag cancel` ends a drag without a drop, and the button let
go after it reaches no one. `panel_drag resize EDGE`, for the bar's inner edge, takes the pointer
the same way and tells the shell the pointer's distance from that edge of the output it began on,
wherever the pointer goes, and where it is let go."""
from pathlib import Path
import sys

import harness

compositor, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
}"""
PANEL = 60  # input_probe --layer's height

with harness.Compositor(compositor, CONFIG) as desktop:
    msg = desktop.msg
    shell = desktop.subscribe(shell=True)

    def log(name):
        return (desktop.root / f"{name}.log").read_text()

    def heard(name):
        return [line for line in log(name).splitlines() if line.startswith("pointer")]

    def drags():
        return shell.lines("panel-")

    desktop.detail = lambda: f"shell heard: {drags()}\npanel: {heard('panel')}\nwindow: {heard('window')}"
    desktop.spawn([probe, "--no-gestures", "--layer", "Panel"], log="panel.log")
    desktop.spawn([probe, "--no-gestures", "Window"], log="window.log")
    desktop.wait_for(lambda: "ready" in log("panel") and "ready" in log("window"), "the panel and the window")
    # The panel and the window on the first output, then a second output to its right.
    msg("headless_output", "add", "HEADLESS-2", "800x600")
    outputs = {row[0]: tuple(int(value) for value in row[2:6]) for row in desktop.rows("outputs")}
    assert outputs == {"HEADLESS-1": (0, 0, 1280, 720), "HEADLESS-2": (1280, 0, 800, 600)}, outputs
    pointer = desktop.virtual_pointer(pointer_probe, 2080, 720)

    # Without the left button held there is nothing to drag.
    pointer("move", "640", str(720 - PANEL // 2))
    assert "left button is not held" in msg("panel_drag", ok=False)
    pointer("press", "right")
    assert "left button is not held" in msg("panel_drag", ok=False)
    pointer("release", "right")

    # Pressed on the panel, then dragged: the shell hears each new edge, and the drop.
    pointer("press", "left")
    desktop.wait_for(lambda: "pointer button 272 pressed" in heard("panel"), "the press on the panel")
    msg("panel_drag")
    desktop.wait_for(lambda: heard("panel")[-1:] == ["pointer leave"], "the panel left")
    assert drags() == ["panel-drag HEADLESS-1 bottom"], drags()
    assert "busy" in msg("panel_drag", ok=False)
    window_before = len(heard("window"))
    pointer("move", "640", "650", "move", "40", "360")
    desktop.wait_for(lambda: drags()[-1:] == ["panel-drag HEADLESS-1 left"], "the left edge")
    pointer("move", "640", "20", "move", "2060", "300")
    desktop.wait_for(lambda: drags()[-1:] == ["panel-drag HEADLESS-2 right"], "the other output's right edge")
    assert drags() == ["panel-drag HEADLESS-1 bottom", "panel-drag HEADLESS-1 left",
                       "panel-drag HEADLESS-1 top", "panel-drag HEADLESS-2 right"], drags()
    assert len(heard("window")) == window_before, heard("window")
    pointer("release", "left")
    desktop.wait_for(lambda: drags()[-1:] == ["panel-dropped HEADLESS-2 right"], "the drop")
    assert "pointer button 272 released" not in heard("panel"), heard("panel")

    # Cancelled, it is not dropped, and the button let go reaches no one.
    shell.forget()
    pointer("move", "640", str(720 - PANEL // 2), "press", "left")
    msg("panel_drag")
    pointer("move", "20", "300")
    desktop.wait_for(lambda: drags()[-1:] == ["panel-drag HEADLESS-1 left"], "the second drag")
    msg("panel_drag", "cancel")
    desktop.wait_for(lambda: drags()[-1:] == ["panel-drag-cancelled"], "the cancel")
    pointer("release", "left")
    desktop.stays(lambda: not shell.lines("panel-dropped"), "a cancelled drag was dropped")
    assert "pointer button 272 released" not in heard("panel") + heard("window"), heard("window")
    assert "usage" in msg("panel_drag", "sideways", ok=False)
    assert "usage" in msg("panel_drag", "resize", "middle", ok=False)

    # The inner edge pressed and dragged: the shell hears the pointer's distance from the bottom
    # of the output it began on as it changes, on the other output too, and where it is let go.
    shell.forget()
    assert "left button is not held" in msg("panel_drag", "resize", "bottom", ok=False)
    pointer("move", "640", str(720 - PANEL + 2), "press", "left")
    desktop.wait_for(lambda: heard("panel")[-1:] == ["pointer button 272 pressed"], "the press on the edge")
    msg("panel_drag", "resize", "bottom")
    desktop.wait_for(lambda: heard("panel")[-1:] == ["pointer leave"], "the panel left for the resize")
    assert drags() == [f"panel-resize HEADLESS-1 {PANEL - 2}"], drags()
    assert "busy" in msg("panel_drag", "resize", "bottom", ok=False)
    pointer("move", "640", "620")
    desktop.wait_for(lambda: drags()[-1:] == ["panel-resize HEADLESS-1 100"], "the edge dragged up")
    pointer("move", "1500", "300")
    desktop.wait_for(lambda: drags()[-1:] == ["panel-resize HEADLESS-1 420"], "the edge dragged onto the other output")
    pointer("release", "left")
    desktop.wait_for(lambda: drags()[-1:] == ["panel-resized HEADLESS-1 420"], "the resize let go")
    assert "pointer button 272 released" not in heard("panel"), heard("panel")

    # Along a side, the distance from that side; cancelled, it is not let go.
    shell.forget()
    pointer("move", "1270", str(720 - PANEL // 2), "press", "left")
    msg("panel_drag", "resize", "right")
    desktop.wait_for(lambda: drags() == ["panel-resize HEADLESS-1 10"], "a resize from the right")
    pointer("move", "1200", str(720 - PANEL // 2))
    desktop.wait_for(lambda: drags()[-1:] == ["panel-resize HEADLESS-1 80"], "the right edge dragged in")
    msg("panel_drag", "cancel")
    desktop.wait_for(lambda: drags()[-1:] == ["panel-drag-cancelled"], "the resize cancelled")
    pointer("release", "left")
    desktop.stays(lambda: not shell.lines("panel-resized"), "a cancelled resize was let go")
print("panel_drag takes the pointer while the left button is held, tells the shell the edge "
      "nearest it, or for a resize the distance from the bar's edge, and where it is let go, and "
      "is cancelled on request")
