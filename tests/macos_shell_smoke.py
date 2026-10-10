# SPDX-License-Identifier: GPL-3.0-or-later
"""The shell's macOS style in a session: the menu bar on the top layer and the dock at the bottom
each reserve their strip, so that a maximized window sits between them; the dock's surface takes
the pointer only over the dock, so that a press in the room above it for an icon to bounce in
reaches the window under it; and switching to a taskbar profile and back lays the shell out anew,
the menu bar going and coming with its strip."""
from pathlib import Path
import signal
import sys

import harness

compositor, shell, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:5])

# A font of 13 pixels makes a menu bar 28 tall (Theme.menuBarHeight); the dock's strip is its
# height and the margin below it. The "plain" profile is the taskbar, along the bottom.
CONFIG = """return {
    xwayland = false,
    layout = { tiling = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    notifications = { enabled = false },
    profile = "mac",
    profiles = {
        mac = { shell = { style = "macos", font_size = 13, panel_height = 64,
                          panel_margin = { bottom = 6 }, panel_radius = 20 } },
        plain = { shell = { panel_height = 52 } },
    },
}"""
WIDTH, HEIGHT, MENU_BAR, DOCK, TASKBAR = 1280, 720, 28, 70, 52

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
        return desktop.windows()

    # The windows are listed by focus, so the maximized one is known by where it is.
    def maximized(where):
        return [w for w in windows() if list(w.box) == where]

    menu_bar, panel = ("paw-menubar", "HEADLESS-1"), ("paw-panel", "HEADLESS-1")
    desktop.detail = lambda: f"layers: {layers()}, windows: {windows()}\n{log()[-1500:]}"
    shell_process = desktop.spawn([shell, "--config", str(desktop.config)], log="shell.log")

    # Picks a profile, and has the shell read the configuration again, as the compositor does for
    # the shell it starts.
    def profile(name):
        msg("profile", name)
        shell_process.send_signal(signal.SIGHUP)

    desktop.wait_for(lambda: "paw surface rendered: paw menu bar" in log() and
                     "paw surface rendered: paw taskbar" in log(), "the menu bar and the dock")
    desktop.wait_for(lambda: layers().get(menu_bar) == ["2", "1"] and layers().get(panel) == ["2", "1"],
                     "the menu bar and the dock on the top layer")

    # A window maximized fills the output between the menu bar's strip and the dock's.
    desktop.spawn([probe, "--window-only"])
    desktop.wait_for(lambda: len(windows()) == 1, "a window")
    msg("maximize")
    between = [0, MENU_BAR, WIDTH, HEIGHT - MENU_BAR - DOCK]
    desktop.wait_for(lambda: maximized(between), "the window maximized between the bars")

    # A second window takes the focus; a press on the first, under the room the dock's surface
    # leaves above it for an icon to bounce in, reaches it.
    desktop.spawn([probe, "--window-only"])
    desktop.wait_for(lambda: len(windows()) == 2 and not maximized(between)[0].focused,
                     "a second window focused")
    pointer = desktop.virtual_pointer(pointer_probe, WIDTH, HEIGHT)
    pointer("move", "100", str(HEIGHT - DOCK - 10), "click", "left")
    desktop.wait_for(lambda: maximized(between)[0].focused, "the press above the dock reaching the window")

    # A taskbar profile: the menu bar goes with its strip, and the window grows into it.
    profile("plain")
    # One reading of the layers: the menu bar may go between two.
    desktop.wait_for(lambda: layers().get(menu_bar, ["", "0"])[1] == "0",
                     "the menu bar gone with the taskbar profile")
    desktop.wait_for(lambda: maximized([0, 0, WIDTH, HEIGHT - TASKBAR]), "the window maximized above the taskbar")
    assert layers().get(panel) == ["2", "1"], layers()
    # And the macOS one again: the menu bar is back and the window between the bars once more.
    profile("mac")
    desktop.wait_for(lambda: layers().get(menu_bar) == ["2", "1"], "the menu bar back")
    desktop.wait_for(lambda: maximized(between), "the window between the bars again")

    for message in ("ReferenceError", "TypeError", "is not defined", "Cannot read"):
        assert message not in log(), log()
print("the macOS style: a menu bar and a dock reserving their strips, a press beside the dock "
      "reaching the window under it, and switching to a taskbar profile and back")
