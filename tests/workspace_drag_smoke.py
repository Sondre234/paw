# SPDX-License-Identifier: GPL-3.0-or-later
"""A window moved with the pointer comes along as its monitor switches workspace, still moved: from
workspace_next, from Super + the wheel, from a touchpad swipe and from keys, moved by Alt + drag,
a middle drag or its title bar. A tile's place in the tiling it leaves closes and it tiles on the
new workspace as it is dropped; a window floating on purpose floats there. Driven by a virtual
pointer and keyboard (pointer_probe), a headless keyboard, and a headless touchpad for the
swipe."""
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
    mouse = { focus_follows = false, middle_drag = { "paw-probe" } },
    bindings = {
        { mods = { "Super" }, button = "wheel_up", action = "workspace_next" },
        { mods = { "Super" }, button = "wheel_down", action = "workspace_prev" },
        { mods = { "Super", "Ctrl" }, key = "Right", action = "workspace_next" },
        { mods = { "Super" }, key = "3", action = "workspace", workspace = 3 },
    },
}"""

KEY_3, KEY_LEFTCTRL, KEY_RIGHT, KEY_LEFTMETA = 4, 29, 106, 125

with harness.Compositor(compositor, CONFIG) as desktop:
    msg = desktop.msg

    def windows():
        """By title: x, y, width, height, tiled, workspace, visible."""
        return {w.title: (*w.box, w.tiled, w.workspace, w.visible) for w in desktop.windows()}

    def workspace():
        return int(msg("get", "workspace"))

    def moving():
        return next(r for r in desktop.rows("snap") if r[0] == "zone")[6] == "1"

    def focused():
        return next((w.title for w in desktop.windows() if w.focused), None)

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

    # A touchpad's swipe to the previous workspace takes it along too.
    msg("headless_pointer", "add", "touchpad")
    press("A")
    clock = [10000]

    def swipe(step, *words, after=10):
        clock[0] += after
        msg("headless_pointer", "swipe", "touchpad", step, *words, str(clock[0]))

    swipe("begin", "3")
    swipe("update", "120", "0")
    # The slide holds no window of workspace 3 (A stays under the pointer) and brings C in.
    held = desktop.rows("gesture")[1]
    assert held[2:4] == ["3", "2"] and held[5:7] == ["0", "1"], held
    swipe("update", "300", "0", after=200)
    swipe("end", after=200)
    desktop.wait_for(lambda: workspace() == 2 and windows()["A"][5:] == (2, True),
                     "the swipe took A to workspace 2")
    assert moving(), desktop.detail()
    release()
    assert windows()["A"][4:] == (False, 2, True), windows()

    # Moved by a middle drag, it comes along as a key (Super + 3) switches, keeping the focus.
    keys = desktop.keyboard()
    x, y, width, height = windows()["A"][:4]
    px, py = x + width // 2, y + height // 2
    pointer("move", str(px), str(py), "press", "middle", "move", str(px + 20), str(py + 10))
    desktop.wait_for(moving, "the middle drag moves A")
    keys(KEY_LEFTMETA, KEY_3)
    desktop.wait_for(lambda: workspace() == 3 and windows()["A"][5:] == (3, True),
                     "Super + 3 took A to workspace 3")
    assert moving() and focused() == "A", desktop.detail()
    pointer("move", str(px + 60), str(py + 40))
    desktop.wait_for(lambda: windows()["A"][:2] == (x + 60, y + 40),
                     "A followed the pointer on workspace 3")
    pointer("release", "middle")
    desktop.wait_for(lambda: not moving(), "the middle drag ends")
    assert windows()["A"][4:] == (False, 3, True), windows()

    # A tile moved by its title bar (the client asking for the move), carried round from the
    # last workspace to the first by Super + Ctrl + Right, tiles beside B as it is dropped there.
    desktop.open_window(probe, "T", focused=True, env={"PAW_PROBE_MOVE": "1"})
    desktop.wait_for(lambda: windows()["T"][4], "T tiled")
    x, y, width, height = windows()["T"][:4]
    px, py = x + width // 2, y + height // 2
    pointer("move", str(px), str(py), "press", "left", "move", str(px + 10), str(py + 5))
    desktop.wait_for(moving, "the title bar's drag moves T")
    keys(KEY_LEFTMETA, KEY_LEFTCTRL, KEY_RIGHT)
    desktop.wait_for(lambda: workspace() == 1 and windows()["T"][5:] == (1, True),
                     "Super + Ctrl + Right took T round to workspace 1")
    assert moving() and focused() == "T", desktop.detail()
    pointer("move", "1000", "360")
    release()
    desktop.wait_for(lambda: windows()["T"][4:6] == (True, 1) and
                     windows()["T"][:4] == RIGHT and windows()["B"][:4] == LEFT,
                     "T tiled beside B on workspace 1")
    msg("workspace", "3")
    assert windows()["A"][4:] == (False, 3, True), windows()
print("A window moved with the pointer comes along to the workspace shown")
