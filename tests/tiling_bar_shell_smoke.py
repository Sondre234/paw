# SPDX-License-Identifier: GPL-3.0-or-later
"""The tiling bar in a session (shell.tiling_bar): while the monitor tiles, the panel is the tiling
bar along the top, its surface reserving the bar and its margins, so a tile alone begins below it.
As its settings change the strip follows: along the bottom, thinner, without margins. With
tiling_bar = false the taskbar stays at the bottom while tiling. With layout.tiling_per_workspace,
a workspace that floats has the taskbar and one that tiles the tiling bar, switching with the
workspace shown; and with tiling turned off, a maximized window ends above the taskbar again, or
below the tiling bar with tiling_bar.always."""
from pathlib import Path
import signal
import sys

import harness

compositor, shell, probe = (str(Path(p).resolve()) for p in sys.argv[1:4])


def config(tiling_bar=""):
    return f"""-- Tiling from the start, a workspace at a time.
return {{
    xwayland = false,
    animations = {{ enabled = false }},
    layout = {{ tiling = true, tiling_per_workspace = true }},
    outputs = {{ monitors = {{ ["HEADLESS-1"] = {{ mode = "1280x720" }} }} }},
    notifications = {{ enabled = false }},
    shell = {{
        panel_height = 52,{tiling_bar}
    }},
}}"""


WIDTH, HEIGHT, TASKBAR = 1280, 720, 52
# The tiling bar by default: 34 pixels tall and 6 from the top edge, none towards the windows.
STRIP = 34 + 6

with harness.Compositor(compositor, config(), start=False) as desktop:
    root, env, msg = desktop.root, desktop.env, desktop.msg
    env.update(QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software",
               QT_FORCE_STDERR_LOGGING="1", XDG_DATA_HOME=str(root), XDG_DATA_DIRS=str(root),
               XDG_STATE_HOME=str(root), DBUS_SESSION_BUS_ADDRESS="disabled:")
    desktop.start()
    shell_log = root / "shell.log"
    panel = desktop.spawn([shell, "--config", str(desktop.config)], log="shell.log")
    desktop.wait_for(lambda: "paw surface rendered: paw taskbar" in shell_log.read_text(),
                     "the panel")

    def box(title):
        return [row.box for row in desktop.windows() if row.title == title][0]

    def reconfigure(text):
        """The compositor reads `text`, and the shell it did not start is told as it would be."""
        desktop.reload(text)
        panel.send_signal(signal.SIGHUP)

    desktop.detail = lambda: (f"windows: {[(row.title, row.workspace, row.box) for row in desktop.windows()]}\n"
                              f"{desktop.config.read_text()}\n{shell_log.read_text()[-2000:]}")
    desktop.open_window(probe, "Tile", "tile", focused=True)
    desktop.wait_for(lambda: box("Tile") == (0, STRIP, WIDTH, HEIGHT - STRIP),
                     "the tile below the tiling bar along the top")

    reconfigure(config('\n        tiling_bar = { position = "bottom", height = 28, margin = 0 },'))
    desktop.wait_for(lambda: box("Tile") == (0, 0, WIDTH, HEIGHT - 28),
                     "the tile above a thinner tiling bar along the bottom")
    reconfigure(config("\n        tiling_bar = false,"))
    desktop.wait_for(lambda: box("Tile") == (0, 0, WIDTH, HEIGHT - TASKBAR),
                     "the tile above the taskbar, kept with tiling_bar = false")
    reconfigure(config())
    desktop.wait_for(lambda: box("Tile") == (0, STRIP, WIDTH, HEIGHT - STRIP),
                     "the tiling bar back as the setting goes")

    # Workspace 2 floats: the taskbar there, the tiling bar again back on workspace 1.
    msg("workspace", "2")
    msg("toggle_tiling")
    desktop.open_window(probe, "Float", "float", focused=True)
    msg("maximize")
    desktop.wait_for(lambda: box("Float") == (0, 0, WIDTH, HEIGHT - TASKBAR),
                     "a window maximized above the taskbar on a workspace that floats")
    msg("workspace", "1")
    desktop.wait_for(lambda: box("Tile") == (0, STRIP, WIDTH, HEIGHT - STRIP),
                     "the tiling bar back on the workspace that tiles")

    # Tiling off: the taskbar comes back.
    msg("toggle_tiling")
    desktop.wait_for(lambda: not [row for row in desktop.windows() if row.title == "Tile" and row.tiled],
                     "the tile floating")
    msg("maximize")
    desktop.wait_for(lambda: box("Tile") == (0, 0, WIDTH, HEIGHT - TASKBAR),
                     "a maximized window above the taskbar once tiling is off")
    # With always = true the bar stays without tiling, the maximized window below it.
    reconfigure(config("\n        tiling_bar = { always = true },"))
    desktop.wait_for(lambda: box("Tile") == (0, STRIP, WIDTH, HEIGHT - STRIP),
                     "a maximized window below the tiling bar kept with always = true")

    messages = "\n".join(line for line in shell_log.read_text().splitlines() if not line.startswith("["))
    for message in ("ReferenceError", "TypeError", "is not defined", "Cannot read"):
        assert message not in messages, messages
print("The tiling bar stands in for the taskbar while the monitor tiles, reserving its own strip, "
      "and the taskbar comes back as tiling goes (unless always = true) or with tiling_bar = false")
