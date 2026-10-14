# SPDX-License-Identifier: GPL-3.0-or-later
"""The caption end to end, from `paw msg caption` on a headless compositor to the shell: its card
shows on the focused monitor, a new caption takes its place, and it goes when its time is up or
when hidden."""
from pathlib import Path
import sys

import harness

compositor, shell = (str(Path(p).resolve()) for p in sys.argv[1:3])

with harness.Compositor(compositor, "return { xwayland = false }", start=False) as desktop:
    root, env = desktop.root, desktop.env
    env.update(QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software", QT_FORCE_STDERR_LOGGING="1",
               XDG_DATA_HOME=str(root), XDG_DATA_DIRS=str(root),
               DBUS_SESSION_BUS_ADDRESS="disabled:")
    shell_log = root / "shell.log"
    desktop.start()

    def log():
        return shell_log.read_text()

    def shown():
        """Whether the caption's layer surface is shown."""
        return any(row[0] == "paw-caption" and row[3] == "1" for row in desktop.rows("layers"))

    desktop.detail = lambda: log()[-1500:]
    desktop.spawn([shell, "--config", str(desktop.config)], log="shell.log")
    desktop.wait_for(lambda: "paw surface rendered: paw taskbar" in log(), "the panel rendered",
                     timeout=30)
    # The shell subscribes as it starts; until then the caption is refused.
    desktop.wait_for(lambda: desktop.run("caption", "60", "Tiling\tOne key tiles it.\tSuper + S")
                     .returncode == 0, "the shell took the caption")
    desktop.wait_for(lambda: "paw caption shown on HEADLESS-1" in log() and shown(),
                     "the caption shown")
    # Another takes its place, and goes when its time is up.
    desktop.msg("caption", "1", "Workspaces")
    desktop.stays(lambda: shown(), "the caption stayed for the next", duration=0.5)
    desktop.wait_for(lambda: "paw caption hidden on HEADLESS-1" in log() and not shown(),
                     "the caption gone after its second")
    # Hidden by hand.
    desktop.msg("caption", "60", "Night light\tThe screen warms.")
    desktop.wait_for(lambda: shown(), "the caption shown again")
    desktop.msg("caption", "hide")
    desktop.wait_for(lambda: not shown(), "the caption hidden")
