# SPDX-License-Identifier: GPL-3.0-or-later
"""The shell and the compositor crossfade to a new appearance profile together: the compositor
holds a picture of the old look while the shell, reloading, draws the new one (its wallpaper
loaded), and lets it fade as soon as the shell says it has (`profile_drawn`), well before it would
give up on it; also when the profile went there and back before the shell reloaded."""
from pathlib import Path
import signal
import sys
import time

import harness

compositor, shell = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {{
    xwayland = false,
    animations = {{ speed = 0.5 }},
    outputs = {{ monitors = {{ ["HEADLESS-1"] = {{ mode = "1280x720" }} }} }},
    notifications = {{ enabled = false }},
    profile = "paper",
    profiles = {{
        paper = {{ appearance = {{ background = "#f2ece0" }},
                   shell = {{ panel_color = "#fbf7f0", text_color = "#2b221c" }} }},
        ink = {{ appearance = {{ background = "#1c2233" }},
                 shell = {{ panel_color = "#262d40", text_color = "#e8e4f0",
                           wallpaper = "{wallpaper}" }} }},
    }},
}}"""

with harness.Compositor(compositor, start=False) as desktop:
    root, env, msg = desktop.root, desktop.env, desktop.msg
    # A wallpaper large enough to take the shell a moment to decode.
    wallpaper = root / "ink.ppm"
    width, height = 2560, 1440
    wallpaper.write_bytes(b"P6\n%d %d\n255\n" % (width, height) + bytes([40, 60, 120]) * (width * height))
    env.update(QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software",
               QT_FORCE_STDERR_LOGGING="1", XDG_DATA_HOME=str(root), XDG_DATA_DIRS=str(root),
               XDG_STATE_HOME=str(root), DBUS_SESSION_BUS_ADDRESS="disabled:")
    desktop.start(CONFIG.format(wallpaper=wallpaper))
    shell_log = root / "shell.log"

    def log():
        return shell_log.read_text()

    def crossfade():
        return {row[0]: tuple(row[1:3]) for row in desktop.rows("crossfade")}

    desktop.detail = lambda: f"crossfade: {crossfade()}\n{log()[-1500:]}"
    shell_process = desktop.spawn([shell, "--config", str(desktop.config)], log="shell.log")
    desktop.wait_for(lambda: "paw surface rendered: paw taskbar" in log(), "the taskbar")

    # Picks a profile, and has the shell read the configuration again, as the compositor does for
    # the shell it starts; the picture of the old look holds until the shell has drawn the new.
    def profile(name):
        drawn = log().count("paw profile drawn: " + name)
        started = time.monotonic()
        msg("profile", name)
        assert crossfade() == {"HEADLESS-1": ("held", name)}, crossfade()
        shell_process.send_signal(signal.SIGHUP)
        desktop.wait_for(lambda: crossfade().get("HEADLESS-1", ("fading",))[0] == "fading",
                         "the picture let go")
        held = time.monotonic() - started
        assert log().count("paw profile drawn: " + name) == drawn + 1, log()
        assert "did not draw profile" not in desktop.log.read_text()
        desktop.wait_for(lambda: crossfade() == {}, "the crossfade over")
        print(f"{name} drawn and released after {held * 1000:.0f} ms")

    profile("ink")
    profile("paper")

    # Switched there and back before the shell reads the configuration again, the shell has the
    # look the compositor waits for already, and says so.
    msg("profile", "ink")
    msg("profile", "paper")
    assert crossfade() == {"HEADLESS-1": ("held", "paper")}, crossfade()
    shell_process.send_signal(signal.SIGHUP)
    desktop.wait_for(lambda: crossfade() == {}, "the crossfade over")
    assert "did not draw profile" not in desktop.log.read_text()
    for message in ("ReferenceError", "TypeError", "is not defined", "Cannot read"):
        assert message not in log(), log()
print("the shell draws a new profile under the compositor's picture and says so")
