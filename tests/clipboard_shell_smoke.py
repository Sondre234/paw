# SPDX-License-Identifier: GPL-3.0-or-later
"""The clipboard history in a session: the shell keeps what a program copies, the
clipboard_history action opens its popup holding the keyboard, and Down and Enter there copy the
older entry again, which a program then pastes from the shell."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, shell, probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

CONFIG = """return {
    xwayland = false,
    animations = { enabled = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    notifications = { enabled = false },
}"""
DOWN, ENTER = 108, 28  # evdev's KEY_DOWN and KEY_ENTER

with harness.Compositor(compositor, CONFIG, start=False) as desktop:
    root, env, msg = desktop.root, desktop.env, desktop.msg
    env.update(QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software",
               QT_FORCE_STDERR_LOGGING="1", XDG_DATA_HOME=str(root), XDG_DATA_DIRS=str(root),
               XDG_STATE_HOME=str(root), DBUS_SESSION_BUS_ADDRESS="disabled:")
    shell_log = root / "shell.log"

    def log():
        return shell_log.read_text()

    def layer(namespace):
        """Whether the overlay's surface is shown and holds the keyboard, as "1" or "0"; None
        before it has a surface."""
        rows = [row[3:5] for row in desktop.rows("layers") if row[0] == namespace]
        return rows[0] if rows else None

    def key(code):
        for state in ("press", "release"):
            msg("headless_keyboard", "key", "keys", str(code), state)

    def copy(text, name, previous=None):
        """Copies `text` as a program does, and waits until the shell has read it; the program
        that copied `previous` ends as it is replaced."""
        copier = root / name
        process = desktop.spawn([probe, "set", f"text/plain={text}"], log=name)
        if previous:
            assert desktop.reap(previous) == 0
        desktop.wait_for(lambda: "sent text/plain" in copier.read_text(),
                         f"the shell reading {text}", detail=lambda: copier.read_text())
        return process

    def paste(kind="text/plain"):
        return subprocess.run([probe, "get", kind], env=env, capture_output=True,
                              timeout=10).stdout.decode()

    desktop.start()
    msg("headless_keyboard", "add", "keys")
    desktop.spawn([shell, "--config", str(desktop.config)], log="shell.log")
    desktop.detail = lambda: f"layers: {desktop.rows('layers')}\n{log()[-1500:]}"
    desktop.wait_for(lambda: "paw surface rendered: paw taskbar" in log(),
                     "the panel rendered", timeout=30)
    first = copy("first", "first.log")
    second = copy("second", "second.log", first)
    assert paste() == "second", paste()

    # The shell subscribes asynchronously: ask until it has heard.
    for _ in range(10):
        msg("clipboard_history")
        try:
            desktop.wait_for(lambda: layer("paw-clipboard") == ["1", "1"],
                             "the clipboard history open with the keyboard", timeout=1)
            break
        except harness.Timeout:
            pass
    assert "paw clipboard shown on HEADLESS-1" in log(), log()
    key(DOWN)
    key(ENTER)
    assert desktop.reap(second) == 0
    desktop.wait_for(lambda: layer("paw-clipboard") in (None, ["0", "0"], ["1", "0"]),
                     "the clipboard history closed")
    # What a program pastes now comes from the shell, under every name of text.
    desktop.wait_for(lambda: paste() == "first", "the older entry copied again",
                     detail=lambda: paste())
    assert paste("UTF8_STRING") == "first"
    types = subprocess.run([probe, "types"], env=env, capture_output=True,
                           timeout=10).stdout.decode().split()
    assert "text/plain;charset=utf-8" in types, types
print("The clipboard history kept, listed and copied again what a program copied")
