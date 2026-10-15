# SPDX-License-Identifier: GPL-3.0-or-later
"""A window moved with the pointer comes along as its monitor switches workspace, still moved: from
workspace_next and from Super + the wheel. A tile's place in the tiling it leaves closes and it
tiles on the new workspace as it is dropped; a window floating on purpose floats there. Driven by
a virtual pointer and keyboard (pointer_probe), Alt + drag."""
from pathlib import Path
import sys

import harness

compositor, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

SCREEN = (1280, 720)
LEFT, RIGHT, WHOLE = (0, 0, 640, 720), (640, 0, 640, 720), (0, 0, 1280, 720)

CONFIG = """return {
    xwayland = false,
    animations = { enabled = false },
    layout = { gap = 0, tiling = true, workspaces = 3 },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    windows = { magnet = { enabled = false } },
    mouse = { focus_follows = false },
    bindings = {
        { mods = { "Super" }, button = "wheel_up", action = "workspace_next" },
        { mods = { "Super" }, button = "wheel_down", action = "workspace_prev" },
    },
}"""

with harness.Compositor(compositor, CONFIG) as desktop:
    msg = desktop.msg

    def windows():
        """By title: x, y, width, height, tiled, workspace, visible."""
        return {w.title: (*w.box, w.tiled, w.workspace, w.visible) for w in desktop.windows()}

    def workspace():
        return int(msg("get", "workspace"))

    def moving():
        return next(r for r in desktop.rows("snap") if r[0] == "zone")[6] == "1"

    desktop.detail = lambda: f"workspace {workspace()}, moving {moving()}: {windows()}"
    pointer = desktop.virtual_pointer(pointer_probe, *SCREEN)

    def press(title):
        """Alt + press in the middle of a window, and a few pixels' move: the move begins."""
        x, y, width, height = windows()[title][:4]
        pointer("key", "alt", "down")
        pointer("move", str(x + width // 2), str(y + height // 2), "press", "left")
        pointer("key", "alt", "up")
        desktop.wait_for(moving, f"{title} is moved")
        pointer("move", str(x + width // 2 + 10), str(y + height // 2 + 5))

    def release():
        pointer("release", "left")
        desktop.wait_for(lambda: not moving(), "the move ends")

    def open_tile(title):
        desktop.open_window(probe, title, focused=True)
        desktop.wait_for(lambda: windows()[title][4], f"{title} tiled")

    msg("workspace", "2")
    open_tile("C")
    msg("workspace", "1")
    pointer("move", "1200", "360")  # each new tile on the right of the last
    open_tile("A")
    open_tile("B")
    assert windows()["A"][:4] == LEFT and windows()["B"][:4] == RIGHT, windows()

    # A tile lifted and taken to the next workspace: still moved, its place on workspace 1
    # closes, and dropped it tiles beside C.
    press("A")
    msg("workspace_next")
    assert workspace() == 2 and moving(), desktop.detail()
    desktop.wait_for(lambda: windows()["A"][5:] == (2, True), "A on workspace 2")
    pointer("move", "300", "360")
    release()
    desktop.wait_for(lambda: windows()["A"][4:6] == (True, 2) and windows()["C"][4] and
                     {windows()["A"][:4], windows()["C"][:4]} == {LEFT, RIGHT},
                     "A tiled beside C on workspace 2")
    msg("workspace", "1")
    desktop.wait_for(lambda: windows()["B"][:4] == WHOLE, "B fills the place A left")
    msg("workspace", "2")

    # Floating on purpose, it floats on the workspace Super + the wheel takes it to, where the
    # pointer goes on moving it.
    msg("toggle_floating")
    desktop.wait_for(lambda: not windows()["A"][4], "A floats")
    press("A")
    x, y = windows()["A"][:2]
    pointer("key", "super", "down", "scroll", "-15", "key", "super", "up")
    desktop.wait_for(lambda: workspace() == 3 and windows()["A"][5:] == (3, True),
                     "Super + wheel took A to workspace 3")
    assert moving(), desktop.detail()
    px, py = x + windows()["A"][2] // 2, y + windows()["A"][3] // 2
    pointer("move", str(px + 60), str(py + 40))
    desktop.wait_for(lambda: windows()["A"][:2] == (x + 60, y + 40),
                     "A followed the pointer on workspace 3")
    release()
    assert windows()["A"][4:] == (False, 3, True), windows()
    assert windows()["C"][:4] == WHOLE, windows()

print("A window moved with the pointer comes along to the workspace shown")
