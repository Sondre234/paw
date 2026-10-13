# SPDX-License-Identifier: GPL-3.0-or-later
"""Dragging the taskbar to another edge in a session, as on Windows with the taskbar unlocked
(shell.panel_draggable): a press on the bar's empty space that moves (a real button press, driven by
pointer_probe) has the compositor take the pointer (panel_drag), and the popover shows where the bar
would go. Let go nearest the left edge, the bar moves there: the configuration file's shell table
gains panel_position = "left", the rest of it as it was, and a maximized window begins right of the
bar. Dragged back nearest the bottom edge, the setting is changed to "bottom" and the bar returns.
Escape during a drag leaves the bar where it is."""
from pathlib import Path
import sys

import harness

compositor, shell, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:5])

CONFIG = """-- The taskbar is dragged by its empty space.
return {
    xwayland = false,
    layout = { tiling = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    notifications = { enabled = false },
    shell = {
        panel_height = 52,
        panel_draggable = true, -- as on Windows with the taskbar unlocked
    },
}"""
WIDTH, HEIGHT, BAR = 1280, 720, 52
ESCAPE = 1


def configured(position):
    """CONFIG with shell.panel_position written into it as the shell writes it."""
    return CONFIG.replace("    shell = {\n", f'    shell = {{\n        panel_position = "{position}",\n')


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

    def seat():
        return {row[0]: (row[1], row[2]) for row in desktop.rows("seat")}

    def popover():
        """Whether the popover is up."""
        return ["paw-popover", "HEADLESS-1", "3", "1"] in [row[:4] for row in desktop.rows("layers")]

    def box():
        return [row.box for row in desktop.windows() if row.title == "Big"][0]

    desktop.detail = lambda: (f"seat: {seat()}, layers: {desktop.rows('layers')}, "
                              f"windows: {[(row.title, row.box) for row in desktop.windows()]}\n"
                              f"{desktop.config.read_text()}\n{shell_log.read_text()[-2000:]}")
    desktop.open_window(probe, "Big", "big", focused=True)
    msg("maximize")
    desktop.wait_for(lambda: box() == (0, 0, WIDTH, HEIGHT - BAR), "the window maximized above the bar")
    pointer = desktop.virtual_pointer(pointer_probe, WIDTH, HEIGHT)

    def drag(start, past, to):
        """Presses at `start` on the bar and moves to `past`, far enough to drag it: the
        compositor takes the pointer, no surface having it, and the popover comes up to show
        where the bar would go. Then moves on to `to`, holding the button there."""
        pointer("move", *start, "press", "left", "move", *past)
        desktop.wait_for(lambda: popover() and seat().get("pointer") == ("-", "-"), "the drag under way")
        pointer("move", *to)

    # Along the bottom, dragged nearest the left edge.
    row = str(HEIGHT - BAR // 2)
    drag(("600", row), ("640", row), ("20", "360"))
    pointer("release", "left")
    desktop.wait_for(lambda: desktop.config.read_text() == configured("left"),
                     "panel_position = \"left\" written into the shell table")
    desktop.wait_for(lambda: box() == (BAR, 0, WIDTH - BAR, HEIGHT), "the window beside the bar on the left")
    desktop.wait_for(lambda: not popover(), "the popover gone after the drop")
    assert "paw taskbar moved to the left edge" in shell_log.read_text()

    # Along the left, dragged back nearest the bottom edge.
    column = str(BAR // 2)
    drag((column, "400"), (column, "440"), ("640", "700"))
    pointer("release", "left")
    desktop.wait_for(lambda: desktop.config.read_text() == configured("bottom"),
                     "panel_position changed to \"bottom\"")
    desktop.wait_for(lambda: box() == (0, 0, WIDTH, HEIGHT - BAR), "the window above the bar again")

    # Escape leaves it where it is.
    keys = desktop.keyboard()
    drag(("600", row), ("640", row), ("1260", "360"))
    desktop.wait_for(lambda: seat().get("keyboard") == ("layer", "paw-popover"),
                     "the popover holding the keyboard, for Escape")
    keys(ESCAPE)
    desktop.wait_for(lambda: not popover(), "the popover gone after Escape")
    pointer("release", "left")
    desktop.stays(lambda: desktop.config.read_text() == configured("bottom") and
                  box() == (0, 0, WIDTH, HEIGHT - BAR), "Escape moved the bar", duration=.5)
    assert "moved to the right edge" not in shell_log.read_text()

    messages = "\n".join(line for line in shell_log.read_text().splitlines() if not line.startswith("["))
    for message in ("ReferenceError", "TypeError", "is not defined", "Cannot read", "Could not move"):
        assert message not in messages, messages
print("The taskbar dragged by its empty space moves to the edge it is let go nearest, writing "
      "shell.panel_position into the configuration; Escape leaves it")
