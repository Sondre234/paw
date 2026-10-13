# SPDX-License-Identifier: GPL-3.0-or-later
"""The shell and the compositor crossfade to a new appearance profile together: the compositor
holds a picture of the old look while the shell, reloading, draws the new one, and lets it fade
once the shell says it has (`profile_drawn`): by then the taskbar has its new height, so that a
maximized window has made room for it. Also when the profile went there and back before the shell
reloaded. The compositor would give up on the shell after half a minute here, so that it is the
shell that lets the picture go."""
from pathlib import Path
import signal
import sys
import time

import harness

compositor, probe, shell = (str(Path(p).resolve()) for p in sys.argv[1:4])

CONFIG = """return {{
    xwayland = false,
    layout = {{ tiling = false }},
    animations = {{ speed = 0.5 }},
    outputs = {{ monitors = {{ ["HEADLESS-1"] = {{ mode = "1280x720" }} }} }},
    notifications = {{ enabled = false }},
    profile = "paper",
    profiles = {{
        paper = {{ appearance = {{ background = "#f2ece0" }},
                   shell = {{ panel_color = "#fbf7f0", text_color = "#2b221c", panel_height = 40 }} }},
        ink = {{ appearance = {{ background = "#1c2233" }},
                 shell = {{ style = "macos", panel_color = "#262d40", text_color = "#e8e4f0",
                           font_size = 13, panel_height = 64, panel_margin = {{ bottom = 6 }},
                           wallpaper = "{wallpaper}" }} }},
    }},
}}"""
# Where a maximized window goes: above the taskbar 40 high, or between the macOS style's menu bar
# (28 high with a font of 13) and its dock (64 high and 6 off the bottom).
MAXIMIZED = {"paper": (0, 0, 1280, 680), "ink": (0, 28, 1280, 720 - 28 - 70)}

with harness.Compositor(compositor, start=False,
                        env={"PAW_TEST_CROSSFADE_HOLD_MS": "30000"}) as desktop:
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

    def maximized():
        return [w.box for w in desktop.windows()]

    def drawn(name):
        return desktop.log.read_text().count(f"The shell drew profile {name}; crossfading")

    desktop.detail = lambda: f"crossfade: {crossfade()}, windows: {maximized()}\n{log()[-1500:]}"
    shell_process = desktop.spawn([shell, "--config", str(desktop.config)], log="shell.log")
    desktop.wait_for(lambda: "paw surface rendered: paw taskbar" in log(), "the taskbar")
    desktop.open_window(probe, "big")
    msg("maximize")
    desktop.wait_for(lambda: maximized() == [MAXIMIZED["paper"]],
                     "the window maximized above the taskbar")

    def menu_bar():
        return [row[3] for row in desktop.rows("layers") if row[0] == "paw-menubar"] == ["1"]

    # Picks a profile, and has the shell read the configuration again, as the compositor does for
    # the shell it starts. The picture of the old look holds until the shell has drawn the new
    # one: the macOS style's menu bar shown, a surface of its own the compositor configures
    # first, or gone, and the window between the bars or above the taskbar, before it fades.
    def profile(name):
        before = drawn(name)
        started = time.monotonic()
        msg("profile", name)
        assert crossfade() == {"HEADLESS-1": ("held", name)}, crossfade()
        shell_process.send_signal(signal.SIGHUP)
        boxes = []

        def released():
            state = crossfade().get("HEADLESS-1", ("fading",))[0]
            boxes.append((maximized(), menu_bar()))
            return state == "fading"
        desktop.wait_for(released, "the picture let go")
        assert boxes[-1] == ([MAXIMIZED[name]], name == "ink"), boxes
        assert drawn(name) == before + 1, desktop.log.read_text()
        held = time.monotonic() - started
        desktop.wait_for(lambda: crossfade() == {}, "the crossfade over")
        print(f"{name} drawn and released after {held * 1000:.0f} ms")

    profile("ink")
    profile("paper")

    # Switched there and back before the shell reads the configuration again, the shell has the
    # look the compositor waits for already, and says so.
    before = drawn("paper")
    msg("profile", "ink")
    msg("profile", "paper")
    assert crossfade() == {"HEADLESS-1": ("held", "paper")}, crossfade()
    shell_process.send_signal(signal.SIGHUP)
    desktop.wait_for(lambda: crossfade() == {}, "the crossfade over")
    assert drawn("paper") == before + 1, desktop.log.read_text()
    for message in ("ReferenceError", "TypeError", "is not defined", "Cannot read"):
        assert message not in log(), log()
print("the shell draws a new profile under the compositor's picture and says so")
