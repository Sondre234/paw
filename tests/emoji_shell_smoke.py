# SPDX-License-Identifier: GPL-3.0-or-later
"""The emoji picker in a session: the emoji_picker action opens it holding the keyboard over a
window, a search typed on the keyboard finds an emoji, and Enter closes the picker and has the
compositor type the emoji into the window, which has the keyboard again."""
from pathlib import Path
import sys

import harness

compositor, shell, probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

CONFIG = """return {
    xwayland = false,
    animations = { enabled = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    notifications = { enabled = false },
}"""
# evdev's codes for the letters of "rocket", and Enter.
KEYS = {"r": 19, "o": 24, "c": 46, "k": 37, "e": 18, "t": 20}
ENTER = 28

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

    def layer(namespace):
        """Whether the overlay's surface is shown and holds the keyboard, as "1" or "0"; None
        before it has a surface."""
        rows = [row[3:5] for row in desktop.rows("layers") if row[0] == namespace]
        return rows[0] if rows else None

    def focused():
        return [w.title for w in desktop.windows() if w.focused]

    def key(code):
        for state in ("press", "release"):
            msg("headless_keyboard", "key", "keys", str(code), state)

    desktop.start()
    msg("headless_keyboard", "add", "keys")
    desktop.spawn([shell, "--config", str(desktop.config)], log="shell.log")
    desktop.detail = lambda: f"layers: {desktop.rows('layers')}\n{log()[-1500:]}"
    desktop.wait_for(lambda: "paw surface rendered: paw taskbar" in log(),
                     "the panel rendered", timeout=30)
    desktop.spawn([probe, "--window-only"],
                  env={"PAW_PROBE_TYPED": str(typed), "PAW_PROBE_TITLE": "Editor"})
    desktop.wait_for(lambda: focused() == ["Editor"], "the window focused")

    # The shell subscribes asynchronously: ask until it has heard.
    for _ in range(10):
        msg("emoji_picker")
        try:
            desktop.wait_for(lambda: layer("paw-emoji") == ["1", "1"],
                             "the emoji picker open with the keyboard", timeout=1)
            break
        except harness.Timeout:
            pass
    assert "paw emoji shown on HEADLESS-1" in log(), log()
    for letter in "rocket":
        key(KEYS[letter])
    # The search is the picker's, not the window's.
    desktop.stays(lambda: typed.read_text() == "", "nothing typed into the window yet")
    key(ENTER)
    desktop.wait_for(lambda: typed.read_text(encoding="utf-8") == "🚀", "the rocket typed",
                     detail=lambda: typed.read_text(encoding="utf-8"))
    assert focused() == ["Editor"], focused()
    desktop.wait_for(lambda: layer("paw-emoji") in (None, ["0", "0"], ["1", "0"]),
                     "the emoji picker closed")
    # What was picked is the first of the recent ones, kept with the session's state.
    recent = (root / "paw" / "emoji").read_text(encoding="utf-8").splitlines()
    assert recent[:2] == ["tone 0", "🚀"], recent
print("The emoji picker typed what it picked into the window")
