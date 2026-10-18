# SPDX-License-Identifier: GPL-3.0-or-later
"""The run launcher in a session: the run action opens it holding the keyboard, a command typed on
the keyboard and run with Ctrl+Enter runs in the home folder, the launcher closes and gives the
keyboard back, and the command is remembered with the session's state."""
from pathlib import Path
import sys

import harness

compositor, shell = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    animations = { enabled = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    notifications = { enabled = false },
}"""
# evdev's codes for the letters of "touch ran", the space, Ctrl and Enter.
KEYS = {"t": 20, "o": 24, "u": 22, "c": 46, "h": 35, " ": 57, "r": 19, "a": 30, "n": 49}
CTRL, ENTER = 29, 28

with harness.Compositor(compositor, CONFIG, start=False) as desktop:
    root, env, msg = desktop.root, desktop.env, desktop.msg
    # A home of the test's own, which the command runs in.
    env.update(QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software",
               QT_FORCE_STDERR_LOGGING="1", XDG_DATA_HOME=str(root), XDG_DATA_DIRS=str(root),
               XDG_STATE_HOME=str(root), DBUS_SESSION_BUS_ADDRESS="disabled:", HOME=str(root))
    shell_log = root / "shell.log"

    def log():
        return shell_log.read_text()

    def layer(namespace):
        """Whether the overlay's surface is shown and holds the keyboard, as "1" or "0"; None
        before it has a surface."""
        rows = [row[3:5] for row in desktop.rows("layers") if row[0] == namespace]
        return rows[0] if rows else None

    desktop.start()
    key = desktop.keyboard()
    desktop.spawn([shell, "--config", str(desktop.config)], log="shell.log")
    desktop.detail = lambda: f"layers: {desktop.rows('layers')}\n{log()[-1500:]}"
    desktop.wait_for(lambda: "paw surface rendered: paw taskbar" in log(),
                     "the panel rendered", timeout=30)

    # The shell subscribes asynchronously: ask until it has heard.
    for _ in range(10):
        msg("run")
        try:
            desktop.wait_for(lambda: layer("paw-run") == ["1", "1"],
                             "the run launcher open with the keyboard", timeout=1)
            break
        except harness.Timeout:
            pass
    assert "paw run shown on HEADLESS-1" in log(), log()
    for letter in "touch ran":
        key(KEYS[letter])
    key.down(CTRL)
    key(ENTER)
    key.up(CTRL)
    desktop.wait_for(lambda: (root / "ran").exists(), "the command run in the home folder")
    desktop.wait_for(lambda: layer("paw-run") in (None, ["0", "0"], ["1", "0"]),
                     "the run launcher closed")
    # Remembered with the session's state: the command, once, and when.
    history = (root / "paw" / "run-history").read_text().splitlines()
    assert history and history[0].split("\t")[:2] == ["touch ran", "1"], history
print("The run launcher ran the command typed into it")
