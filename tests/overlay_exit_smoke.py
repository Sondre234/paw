# SPDX-License-Identifier: GPL-3.0-or-later
"""The shell's overlays fade out as they close without keeping the keyboard or the pointer from
what is under them. Slowed down to a tenth: the command palette stays on screen for a moment after
Escape closes it, but no longer holds the keyboard, which the window under it has back at once,
and it is gone once it has faded; opened again while it fades, it comes back with the keyboard.
The window switcher fades out the same way after it is cancelled."""
from pathlib import Path
import sys
import time

import harness

compositor, shell, probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false },
    animations = { speed = 0.1 },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    notifications = { enabled = false },
}"""
ESCAPE = 1  # evdev's KEY_ESC

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

    def focused():
        return [row[9] for row in desktop.rows("windows") if row[1] == "1"]

    def escape():
        for state in ("press", "release"):
            msg("headless_keyboard", "key", "keys", str(ESCAPE), state)

    desktop.start()
    msg("headless_keyboard", "add", "keys")
    desktop.spawn([shell, "--config", str(desktop.config)], log="shell.log")
    desktop.detail = lambda: f"layers: {desktop.rows('layers')}\n{log()[-1500:]}"
    desktop.wait_for(lambda: "paw surface rendered: paw taskbar" in log(),
                     "the panel rendered", timeout=30)
    for title in ("Under", "Beside"):
        desktop.spawn([probe, "--window-only"],
                      env=dict(PAW_PROBE_TITLE=title, PAW_PROBE_APP_ID=title.lower()))
        desktop.wait_for(lambda: focused() == [title], f"{title} focused")

    # The shell subscribes asynchronously: ask until it has heard.
    for _ in range(10):
        msg("palette")
        try:
            desktop.wait_for(lambda: layer("paw-palette") == ["1", "1"],
                             "the palette open with the keyboard", timeout=1)
            break
        except harness.Timeout:
            pass
    assert focused() == [], desktop.detail()
    # Closed before it has come in at all, it would go at once with nothing to fade.
    desktop.stays(lambda: layer("paw-palette") == ["1", "1"], "the palette coming in")
    escape()
    desktop.wait_for(lambda: layer("paw-palette") == ["1", "0"] and focused() == ["Beside"],
                     "the palette fading out, the window under it with the keyboard again")
    # Opened again while it fades, it comes back with the keyboard; closed again, it goes.
    msg("palette")
    desktop.wait_for(lambda: "paw palette kept on HEADLESS-1" in log() and
                     layer("paw-palette") == ["1", "1"], "the palette kept as it was going")
    assert "paw palette hidden" not in log(), log()
    escape()
    desktop.wait_for(lambda: "paw palette hidden on HEADLESS-1" in log() and
                     layer("paw-palette") in (None, ["0", "0"]) and focused() == ["Beside"],
                     "the palette hidden once it has faded")

    # The switcher, cancelled, fades out over the windows before its surface goes.
    msg("switcher")
    desktop.wait_for(lambda: "paw switcher shown on HEADLESS-1" in log() and
                     (layer("paw-switcher") or ["0"])[0] == "1", "the switcher shown")
    desktop.stays(lambda: layer("paw-switcher")[0] == "1", "the switcher coming in")
    cancelled = time.monotonic()
    msg("switcher_cancel")
    desktop.wait_for(lambda: "paw switcher hidden on HEADLESS-1" in log(),
                     "the switcher hidden once it has faded")
    # A fast duration at a tenth of the speed is over a second.
    assert time.monotonic() - cancelled > 0.6, "the switcher went without fading"
    assert focused() == ["Beside"], focused()
print("Overlay exits passed")
