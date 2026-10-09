# SPDX-License-Identifier: GPL-3.0-or-later
"""The taskbar's popups, in the popover's layer surface over the output: a right click on the bar
maps it on the overlay layer with the keyboard, a press on the bar while a popup is open switches
popups in one press, and a press beside the popups closes them without reaching the window under
it, which gets the keyboard back. The windows of a stacked button, listed on hover, leave the
keyboard where it is. The bar's own surface keeps its size throughout. With grim, the popups are
seen where they open."""
from pathlib import Path
import sys

import harness

compositor, shell, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:5])
grim = sys.argv[5] if len(sys.argv) > 5 else ""

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    notifications = { enabled = false },
    shell = { panel_height = 52 },
}"""
WIDTH, HEIGHT, BAR = 1280, 720, 52
PANEL = (21, 30, 44)  # shell.panel_color, which popups are drawn in
# A point of the bar menu, opened at x 700: in its last entry, between the icon and the text.
MENU = (738, HEIGHT - BAR - 8 - 20)

with harness.Compositor(compositor, CONFIG, start=False) as desktop:
    root, env, msg = desktop.root, desktop.env, desktop.msg
    env.update(QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software",
               QT_FORCE_STDERR_LOGGING="1", XDG_DATA_HOME=str(root), XDG_DATA_DIRS=str(root),
               XDG_STATE_HOME=str(root), DBUS_SESSION_BUS_ADDRESS="disabled:")
    desktop.start()
    shell_log = root / "shell.log"

    def log():
        return shell_log.read_text()

    def layers():
        return {(row[0], row[1]): row[2:4] for row in desktop.rows("layers")}

    def windows():
        return desktop.rows("windows")

    def focused():
        return [row for row in windows() if row[1] == "1"]

    def panel_colour(x, y):
        """Whether x, y is in a popup's colour, exactly (the desktop's gradient comes close), or
        None without grim, which cannot tell."""
        shot = harness.grab(grim, env)
        return None if shot is None else all(abs(a - b) <= 2 for a, b in zip(shot.at(x, y), PANEL))

    def drawn(x, y):
        return panel_colour(x, y) is not False

    def not_drawn(x, y):
        return panel_colour(x, y) is not True

    desktop.detail = lambda: f"layers: {layers()}, windows: {windows()}\n{log()[-1500:]}"
    desktop.spawn([shell, "--config", str(desktop.config)], log="shell.log")
    desktop.wait_for(lambda: "paw surface rendered: paw taskbar" in log(), "the panel")
    panel = ("paw-panel", "HEADLESS-1")
    assert layers()[panel] == ["2", "1"], layers()

    # A window that a press reaching it would start dragging.
    desktop.spawn([probe, "--window-only"], env={"PAW_PROBE_MOVE": "1"})
    desktop.wait_for(lambda: len(focused()) == 1, "the window focused")
    window = windows()[0][4:8]
    x, y, w, h = (int(n) for n in window)
    pointer = desktop.virtual_pointer(pointer_probe, WIDTH, HEIGHT)
    bar_y = HEIGHT - BAR // 2

    # A right click on the bar's empty space opens its menu in the popover, which takes the
    # keyboard from the window.
    pointer("move", "700", str(bar_y), "click", "right")
    popover = ("paw-popover", "HEADLESS-1")
    desktop.wait_for(lambda: layers().get(popover) == ["3", "1"] and not focused(),
                     "the popover on the overlay layer with the keyboard")
    assert "paw popover shown on HEADLESS-1" in log()
    desktop.wait_for(lambda: drawn(*MENU), "the bar menu drawn above the bar")

    # The start button, pressed while the menu is open, opens the launcher at once: the bar's
    # strip is not the popover's.
    pointer("move", "30", str(bar_y), "click", "left")
    desktop.wait_for(lambda: drawn(200, 300) and not_drawn(*MENU),
                     "the launcher in place of the bar menu")
    assert layers()[popover] == ["3", "1"] and not focused(), layers()

    # And a right click on the bar, the bar menu again.
    pointer("move", "700", str(bar_y), "click", "right")
    desktop.wait_for(lambda: drawn(*MENU) and not_drawn(200, 300),
                     "the bar menu in place of the launcher")

    # A press beside the menu, over the window, closes it and does not reach the window: it
    # would have been dragged along.
    beside = (x + w // 2, y + h // 2)
    pointer("move", str(beside[0]), str(beside[1]), "press", "left")
    pointer("move", str(beside[0] + 60), str(beside[1] + 40), "release", "left")
    desktop.wait_for(lambda: "paw popover hidden on HEADLESS-1" in log(), "the popover hidden")
    desktop.wait_for(lambda: len(focused()) == 1, "the keyboard back with the window")
    assert windows()[0][4:8] == window, (window, windows())
    desktop.wait_for(lambda: layers().get(popover, ["3", "0"])[1] == "0" and
                     not_drawn(*MENU), "the popover unmapped")
    assert layers()[panel] == ["2", "1"], layers()
    desktop.stays(lambda: windows()[0][4:8] == window, "the window moved after the popover closed")

    # A second window of the application stacks its taskbar button. Resting on it lists both in
    # the popover, which leaves the keyboard with the window; leaving it hides them.
    desktop.spawn([probe, "--window-only"])
    desktop.wait_for(lambda: len(windows()) == 2 and len(focused()) == 1, "a second window")
    pointer("move", "85", str(bar_y))
    desktop.wait_for(lambda: layers().get(popover) == ["3", "1"] and drawn(85, HEIGHT - BAR - 8 - 3),
                     "the windows of the stacked button listed")
    desktop.stays(lambda: len(focused()) == 1, "the list took the keyboard")
    pointer("move", "900", "300")
    desktop.wait_for(lambda: popover not in layers() and not_drawn(85, HEIGHT - BAR - 8 - 3),
                     "the list hidden once the pointer left")

    for message in ("ReferenceError", "TypeError", "is not defined", "Cannot read"):
        assert message not in log(), log()
print("the popover: overlay layer, keyboard, a bar button switching popups, a press beside "
      "closing them, a list on hover" + ("" if grim else " (no grim: no pixels)"))
