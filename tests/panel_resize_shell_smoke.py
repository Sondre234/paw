# SPDX-License-Identifier: GPL-3.0-or-later
"""Resizing the taskbar by its inner edge in a session, as on Windows with the taskbar unlocked
(shell.panel_draggable): over the bar's top edge the pointer shows a resize cursor, and a press
there (a real button press, driven by pointer_probe) has the compositor take the pointer
(panel_drag resize). As it is dragged every monitor's bar follows, and the maximized windows on
both monitors make room at once. Let go, the configuration file's shell table gets the new
panel_height, the rest of it as it was. The height stays within 24 to 100, and Escape during a drag
puts the old one back. A floating bar's end sets the margin beyond it, and its outer edge the
margin on its side, trading it for the height; a margin given as one number is written out as
four. Down a side the drag changes the bar's width. Locked (panel_draggable false), the edge is
the bar's like the rest of it."""
from pathlib import Path
import signal
import sys

import harness

compositor, shell, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:5])

CONFIG = """-- The taskbar is resized by its inner edge.
return {
    xwayland = false,
    layout = { tiling = false },
    outputs = {
        order = { "HEADLESS-1", "HEADLESS-2" },
        monitors = {
            ["HEADLESS-1"] = { mode = "1280x720" },
            ["HEADLESS-2"] = { mode = "1280x720" },
        },
    },
    notifications = { enabled = false },
    shell = {%s
        panel_height = %d,
        panel_draggable = %s, -- as on Windows with the taskbar unlocked
        panel_dock = false,
    },
}"""
WIDTH, HEIGHT = 1280, 720
ESCAPE = 1


def config(height, draggable="true", left=False, margin=None):
    """CONFIG with the bar `height` tall, along the left edge with `left`, `margin` (Lua) around it,
    as the shell writes it."""
    return CONFIG % (('\n        panel_position = "left",' if left else "") +
                     (f"\n        panel_margin = {margin}," if margin else ""), height, draggable)


with harness.Compositor(compositor, config(52), start=False,
                        env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    root, env, msg = desktop.root, desktop.env, desktop.msg
    env.update(QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software",
               QT_FORCE_STDERR_LOGGING="1", XDG_DATA_HOME=str(root), XDG_DATA_DIRS=str(root),
               XDG_STATE_HOME=str(root), DBUS_SESSION_BUS_ADDRESS="disabled:")
    desktop.start()
    shell_log = root / "shell.log"
    panels = desktop.spawn([shell, "--config", str(desktop.config)], log="shell.log")
    desktop.wait_for(lambda: shell_log.read_text().count("paw surface rendered: paw taskbar") >= 2,
                     "both monitors' bars")

    def seat():
        return {row[0]: (row[1], row[2]) for row in desktop.rows("seat")}

    def cursor():
        return msg("get", "cursor_image").strip()

    def boxes():
        return {row.title: row.box for row in desktop.windows()}

    def maximized(height, left=False):
        """Where the two maximized windows are with the bar `height` thick along the bottom of
        each monitor, or along the left."""
        if left:
            return {"Big": (height, 0, WIDTH - height, HEIGHT),
                    "Other": (WIDTH + height, 0, WIDTH - height, HEIGHT)}
        return {"Big": (0, 0, WIDTH, HEIGHT - height), "Other": (WIDTH, 0, WIDTH, HEIGHT - height)}

    def edge_cursor(x, y, away=20):
        """The cursor at (x, y), the pointer moved there afresh from `away` pixels along x: the
        bar's surface may take a moment to take up a new size, and Qt sets the cursor for what it
        finds under the pointer as it moves."""
        pointer("move", str(int(x) + away), str(y), "move", str(x), str(y))
        return cursor()

    def press_edge(x, y, image="ns-resize"):
        """Presses the left button at (x, y) on the bar's edge, once the bar is there."""
        desktop.wait_for(lambda: edge_cursor(x, y) == image, f"the resize cursor at {x}, {y}")
        pointer("press", "left")

    desktop.detail = lambda: (f"seat: {seat()}, cursor: {cursor()}, windows: {boxes()}\n"
                              f"{desktop.config.read_text()}\n{shell_log.read_text()[-2000:]}")
    # A maximized window on each monitor, opened where the pointer is.
    pointer = desktop.virtual_pointer(pointer_probe, 2 * WIDTH, HEIGHT)
    keys = desktop.keyboard()
    pointer("move", "640", "300")
    desktop.open_window(probe, "Big", "big", focused=True)
    msg("maximize")
    pointer("move", str(WIDTH + 640), "300")
    desktop.open_window(probe, "Other", "other", focused=True)
    msg("maximize")
    desktop.wait_for(lambda: boxes() == maximized(52), "the windows maximized above the bars")

    # Over the bar's top edge the pointer shows a resize cursor, and off it not.
    desktop.wait_for(lambda: edge_cursor(640, HEIGHT - 52 + 1) == "ns-resize",
                     "the resize cursor over the bar's edge")
    pointer("move", "640", str(HEIGHT - 52 + 20))
    desktop.wait_for(lambda: cursor() != "ns-resize", "no resize cursor inside the bar")

    # Dragged up 20 pixels, both bars are 20 taller and both windows make room as it goes; let go,
    # panel_height = 72 is written into the shell table.
    press_edge(640, HEIGHT - 52 + 1)
    pointer("move", "640", str(HEIGHT - 72 + 1))
    desktop.wait_for(lambda: boxes() == maximized(72), "the windows making room during the drag")
    assert seat().get("pointer") == ("-", "-") and cursor() == "ns-resize", (seat(), cursor())
    assert desktop.config.read_text() == config(52), "written before it was let go"
    pointer("release", "left")
    desktop.wait_for(lambda: desktop.config.read_text() == config(72), "panel_height = 72 written")
    desktop.stays(lambda: boxes() == maximized(72), "the windows moved once it was let go")
    assert "paw taskbar resized to 72 pixels" in shell_log.read_text()

    # No taller than 100, no shorter than 24.
    press_edge(640, HEIGHT - 72 + 1)
    pointer("move", "640", "100")
    desktop.wait_for(lambda: boxes() == maximized(100), "the bar at its tallest")
    pointer("move", "640", str(HEIGHT - 5))
    desktop.wait_for(lambda: boxes() == maximized(24), "the bar at its shortest")
    pointer("release", "left")
    desktop.wait_for(lambda: desktop.config.read_text() == config(24), "panel_height = 24 written")

    # Escape puts the old height back, and nothing is written.
    press_edge(640, HEIGHT - 24 + 1)
    pointer("move", "640", str(HEIGHT - 60 + 1))
    desktop.wait_for(lambda: boxes() == maximized(60), "the bar dragged taller")
    desktop.wait_for(lambda: seat().get("keyboard") == ("layer", "paw-popover"),
                     "the popover holding the keyboard, for Escape")
    keys(ESCAPE)
    desktop.wait_for(lambda: boxes() == maximized(24), "the old height back after Escape")
    pointer("release", "left")
    desktop.stays(lambda: desktop.config.read_text() == config(24) and boxes() == maximized(24),
                  "Escape resized the bar", duration=.5)

    # Floating 4 pixels in from every edge, 40 tall: its left end dragged 36 pixels in moves it 40
    # in from the left, the windows staying where they are; panel_margin, one number, is written
    # out as four.
    desktop.reload(config(40, margin="4"))
    panels.send_signal(signal.SIGHUP)
    desktop.wait_for(lambda: boxes() == maximized(48), "the windows above the floating bars")
    press_edge(5, HEIGHT - 24, "ew-resize")
    desktop.wait_for(lambda: seat().get("pointer") == ("-", "-"), "the compositor holding the pointer")
    pointer("move", "41", str(HEIGHT - 24))
    pointer("release", "left")
    four = "{ top = 4, right = 4, bottom = 4, left = %d }"
    desktop.wait_for(lambda: desktop.config.read_text() == config(40, margin=four % 40),
                     "panel_margin written out with the left one 40")
    desktop.stays(lambda: boxes() == maximized(48), "the windows moved by the bar's end")
    # Its outer edge dragged up gives the margin below the height, the windows staying; dragged
    # down past the output's edge, it stops there, the bar 4 taller.
    press_edge(640, HEIGHT - 6)
    desktop.wait_for(lambda: seat().get("pointer") == ("-", "-"), "the compositor holding the pointer")
    pointer("move", "640", str(HEIGHT - 16))
    desktop.stays(lambda: boxes() == maximized(48), "the windows moved by the bar's outer edge",
                  duration=.3)
    pointer("move", "640", str(HEIGHT - 1))
    assert desktop.config.read_text() == config(40, margin=four % 40), "written before it was let go"
    pointer("release", "left")
    desktop.wait_for(lambda: desktop.config.read_text() ==
                     config(44, margin="{ top = 4, right = 4, bottom = 0, left = 40 }"),
                     "panel_height = 44 and no bottom margin written together")
    desktop.stays(lambda: boxes() == maximized(48), "the windows moved by the bar's outer edge")
    assert "paw taskbar resized to 44 pixels, its margins 4 4 0 40" in shell_log.read_text()

    # Down the left side, its right edge changes its width.
    desktop.reload(config(24, left=True))
    panels.send_signal(signal.SIGHUP)
    desktop.wait_for(lambda: boxes() == maximized(24, left=True), "the windows beside the bars")
    press_edge(22, 360, "ew-resize")
    pointer("move", "82", "360")
    desktop.wait_for(lambda: boxes() == maximized(84, left=True), "the bars dragged wider")
    pointer("release", "left")
    desktop.wait_for(lambda: desktop.config.read_text() == config(84, left=True),
                     "panel_height = 84 written")

    # Locked, the edge neither shows a resize cursor nor resizes the bar.
    desktop.reload(config(84, "false", left=True))
    panels.send_signal(signal.SIGHUP)
    desktop.wait_for(lambda: edge_cursor(82, 360, away=-20) not in ("ew-resize", "none", "client"),
                     "the bar's own cursor over its edge once locked")
    pointer("press", "left", "move", "300", "360")
    desktop.stays(lambda: boxes() == maximized(84, left=True) and
                  desktop.config.read_text() == config(84, "false", left=True), "a locked bar resized")
    pointer("release", "left")

    messages = "\n".join(line for line in shell_log.read_text().splitlines() if not line.startswith("["))
    for message in ("ReferenceError", "TypeError", "is not defined", "Cannot read", "Could not resize"):
        assert message not in messages, messages
print("The taskbar's inner edge resizes every bar as it is dragged, the windows making room, and "
      "writes shell.panel_height as it is let go, within 24 to 100; Escape puts the old size back, "
      "and a locked bar is not resized")
