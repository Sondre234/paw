# SPDX-License-Identifier: GPL-3.0-or-later
"""The sheet of keyboard shortcuts in a session: its binding opens it holding the keyboard over a
window, typing goes to its search rather than to the window, the binding again closes it, and so
does Escape after `paw msg shortcuts` opened it, the window having the keyboard back each time."""
from pathlib import Path
import sys

import harness

compositor, shell, probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

CONFIG = """return {
    xwayland = false,
    animations = { enabled = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    notifications = { enabled = false },
    bindings = {
        { mods = { "Super" }, key = "slash", action = "shortcuts" },
        { mods = { "Super" }, key = "c", action = "close", description = "Close it" },
    },
}"""
# evdev's codes: Super, /, Escape, and the letters of "close".
SUPER, SLASH, ESCAPE = 125, 53, 1
KEYS = {"c": 46, "l": 38, "o": 24, "s": 31, "e": 18}

with harness.Compositor(compositor, CONFIG, start=False) as desktop:
    root, env, msg = desktop.root, desktop.env, desktop.msg
    env.update(QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software",
               QT_FORCE_STDERR_LOGGING="1", XDG_DATA_HOME=str(root), XDG_DATA_DIRS=str(root),
               XDG_STATE_HOME=str(root), DBUS_SESSION_BUS_ADDRESS="disabled:")
    shell_log = root / "shell.log"
    typed = root / "typed.txt"
    typed.write_text("")

    def log():
        return shell_log.read_text()

    def layer():
        """Whether the sheet's surface is shown and holds the keyboard, as "1" or "0"; None
        before it has a surface."""
        rows = [row[3:5] for row in desktop.rows("layers") if row[0] == "paw-shortcuts"]
        return rows[0] if rows else None

    def closed():
        return layer() in (None, ["0", "0"], ["1", "0"])

    def focused():
        return [w.title for w in desktop.windows() if w.focused]

    desktop.start()
    key = desktop.keyboard()
    desktop.spawn([shell, "--config", str(desktop.config)], log="shell.log")
    desktop.detail = lambda: f"layers: {desktop.rows('layers')}\n{log()[-1500:]}"
    desktop.wait_for(lambda: "paw surface rendered: paw taskbar" in log(),
                     "the panel rendered", timeout=30)
    desktop.spawn([probe, "--window-only"],
                  env={"PAW_PROBE_TYPED": str(typed), "PAW_PROBE_TITLE": "Editor"})
    desktop.wait_for(lambda: focused() == ["Editor"], "the window focused")

    # The shell subscribes asynchronously: press until it has heard.
    for _ in range(10):
        key(SUPER, SLASH)
        try:
            desktop.wait_for(lambda: layer() == ["1", "1"],
                             "the sheet open with the keyboard", timeout=1)
            break
        except harness.Timeout:
            pass
    assert "paw shortcuts shown on HEADLESS-1" in log(), log()
    for letter in "close":
        key(KEYS[letter])
    # The search is the sheet's, not the window's.
    desktop.stays(lambda: typed.read_text() == "", "nothing typed into the window")
    key(SUPER, SLASH)
    desktop.wait_for(closed, "the sheet closed by its binding again")
    desktop.wait_for(lambda: focused() == ["Editor"], "the window with the keyboard again")

    msg("shortcuts")
    desktop.wait_for(lambda: layer() == ["1", "1"], "the sheet open from paw msg")
    key(ESCAPE)
    desktop.wait_for(closed, "the sheet closed by Escape")
    assert focused() == ["Editor"], focused()
    assert typed.read_text() == "", typed.read_text()
print("The sheet of shortcuts opened and closed by its binding, paw msg and Escape")
