# SPDX-License-Identifier: GPL-3.0-or-later
"""The shell under a finger and a pen: with a touchscreen plugged in, Qt binds wl_touch, and a tap
on the start button opens the launcher in the popover, which a tap beside it closes, as clicks
do; a drawing tablet's pen does the same through tablet-v2."""
from pathlib import Path
import sys

import harness

compositor, shell = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    notifications = { enabled = false },
    shell = { panel_height = 52 },
}"""
WIDTH, HEIGHT, BAR = 1280, 720, 52

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

    desktop.detail = lambda: f"layers: {layers()}, touch: {desktop.rows('touch')}\n{log()[-1500:]}"
    msg("headless_touch", "add", "screen")
    desktop.spawn([shell, "--config", str(desktop.config)], log="shell.log")
    desktop.wait_for(lambda: "paw surface rendered: paw taskbar" in log(), "the panel")
    clock = [1000]

    def tap(x, y, finger, surface):
        """A finger down and up at x, y, which must reach `surface` (as get touch has it) by
        wl_touch rather than by the pointer standing in."""
        clock[0] += 60
        msg("headless_touch", "down", "screen", str(finger), f"{x / WIDTH:.6f}",
            f"{y / HEIGHT:.6f}", str(clock[0]))
        msg("headless_touch", "frame", "screen")
        assert [f"point {finger}", *surface] in desktop.rows("touch"), desktop.rows("touch")
        clock[0] += 60
        msg("headless_touch", "up", "screen", str(finger), str(clock[0]))
        msg("headless_touch", "frame", "screen")

    # The start button: the launcher opens in the popover, which holds the keyboard.
    tap(30, HEIGHT - BAR // 2, 1, ["layer", "paw-panel"])
    popover = ("paw-popover", "HEADLESS-1")
    desktop.wait_for(lambda: layers().get(popover) == ["3", "1"] and
                     "paw popover shown on HEADLESS-1" in log(), "the launcher opened")
    # A tap beside it, on the desktop, closes it.
    tap(1100, 200, 2, ["layer", "paw-popover"])
    desktop.wait_for(lambda: "paw popover hidden on HEADLESS-1" in log(),
                     "the launcher closed")

    # A pen's tip on the start button, as the shell's own tablet input.
    msg("headless_tablet", "add", "wacom")

    def pen(x, y, surface):
        """The pen's tip down and up at x, y, over `surface` as a tablet tool."""
        msg("headless_tablet", "in", "wacom", "pen", f"{x / WIDTH:.6f}", f"{y / HEIGHT:.6f}")
        assert ["tool", "pen", "1", *surface] in desktop.rows("tablet"), desktop.rows("tablet")
        msg("headless_tablet", "tip", "wacom", "pen", "down")
        msg("headless_tablet", "tip", "wacom", "pen", "up")
        msg("headless_tablet", "out", "wacom", "pen")

    shown = log().count("paw popover shown on HEADLESS-1")
    pen(30, HEIGHT - BAR // 2, ["layer", "paw-panel"])
    desktop.wait_for(lambda: log().count("paw popover shown on HEADLESS-1") > shown,
                     "the launcher opened by the pen")
    hidden = log().count("paw popover hidden on HEADLESS-1")
    pen(1100, 200, ["layer", "paw-popover"])
    desktop.wait_for(lambda: log().count("paw popover hidden on HEADLESS-1") > hidden,
                     "the launcher closed by the pen")
