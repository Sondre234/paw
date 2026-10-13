# SPDX-License-Identifier: GPL-3.0-or-later
"""mouse.middle_drag: in a listed window a middle press that travels moves the window and never
reaches the application, while one let go in place (or after a small wobble) reaches it as a
click, to paste. With a modifier held, or in a window not listed, the press goes straight through.
Driven by a virtual pointer (pointer_probe) over an input_probe window."""
from pathlib import Path
import sys

import harness

compositor, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

SCREEN = (1280, 720)
BTN_MIDDLE = 274


def config(apps):
    return """return {
    xwayland = false,
    layout = { tiling = false },
    mouse = { middle_drag = { %s } },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    windows = { magnet = { enabled = false } },
}""" % ", ".join('"%s"' % app for app in apps)


with harness.Compositor(compositor, config(["input-probe"])) as desktop:
    msg = desktop.msg

    def where():
        return {w.title: (w.x, w.y) for w in desktop.windows()}["W"]

    def buttons():
        """The middle button's events the window heard."""
        lines = (desktop.root / "w.log").read_text().splitlines()
        return [line for line in lines if line.startswith(f"pointer button {BTN_MIDDLE} ")]

    desktop.detail = lambda: f"{msg('get', 'windows')}\n{buttons()}"
    desktop.spawn([probe, "W"], log="w.log")
    desktop.wait_for(lambda: "W" in msg("get", "windows") and
                     "ready" in (desktop.root / "w.log").read_text(), "window mapped")
    pointer = desktop.virtual_pointer(pointer_probe, *SCREEN)

    CLICK = [f"pointer button {BTN_MIDDLE} pressed", f"pointer button {BTN_MIDDLE} released"]

    def middle(*moves, keys=(), pastes):
        """Presses the middle button 100, 100 into W, moves by each (dx, dy) and lets go,
        checking that the window heard a click (`pastes`) or no middle button at all; returns
        how far W went."""
        x, y = where()
        heard = len(buttons())
        words = []
        for key in keys:
            words += ["key", key, "down"]
        words += ["move", str(x + 100), str(y + 100), "press", "middle"]
        for dx, dy in moves:
            words += ["move", str(x + 100 + dx), str(y + 100 + dy)]
        words += ["release", "middle"]
        for key in keys:
            words += ["key", key, "up"]
        pointer(*words)
        if pastes:
            desktop.wait_for(lambda: buttons()[heard:] == CLICK, "the click reached the window")
        else:
            desktop.stays(lambda: buttons()[heard:] == [], "no middle button for the window")
        x2, y2 = where()
        return x2 - x, y2 - y

    # A click in place pastes; so does one that wobbles a few pixels.
    assert middle(pastes=True) == (0, 0)
    assert middle((3, 2), pastes=True) == (0, 0)

    # A drag moves the window, and the application never hears the button.
    assert middle((50, 0), (100, 40), pastes=False) == (100, 40), where()

    # With a modifier held the press is the application's (kitty: Shift passes mouse reporting).
    assert middle((100, 0), keys=["shift"], pastes=True) == (0, 0)

    # A window not listed gets every middle press.
    desktop.reload(config(["kitty"]))
    assert middle((100, 0), pastes=True) == (0, 0)

    # "*" lists every window.
    desktop.reload(config(["*"]))
    assert middle((-60, 0), pastes=False) == (-60, 0), where()
print("A middle drag moves listed windows, and a middle click still reaches them")
