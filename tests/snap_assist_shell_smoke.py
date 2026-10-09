# SPDX-License-Identifier: GPL-3.0-or-later
"""The shell draws Snap Assist's text as it draws the overview's: its overlay shows while Snap
Assist is open on its output, puts each window's title in the free slot and leaves the search box
out (light pixels on a screenshot), and takes no input, so the pointer reaches the window beside
the slot through it and a click there goes on to that window."""
from pathlib import Path
import shutil
import sys

import harness

compositor, shell, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:5])

CONFIG = """return {
    xwayland = false,
    animations = { enabled = false },
    layout = { tiling = false, gap = 0 },
    overview = { animation = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    windows = { magnet = { enabled = false } },
}"""

with harness.Compositor(compositor, CONFIG, start=False) as desktop:
    root, env, msg = desktop.root, desktop.env, desktop.msg
    env.update(QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software",
               QT_FORCE_STDERR_LOGGING="1", XDG_DATA_HOME=str(root), XDG_DATA_DIRS=str(root),
               DBUS_SESSION_BUS_ADDRESS="disabled:")  # the shell must not use the real session bus
    shell_log = root / "shell.log"

    def wait_for(predicate, message):
        desktop.wait_for(predicate, message, timeout=30)

    def seat():
        return {row[0]: row[1:] for row in desktop.rows("seat")}

    desktop.start()
    desktop.spawn([shell, "--config", str(desktop.config)], log="shell.log")
    desktop.detail = lambda: f"{msg('get', 'overview')} {seat()} {shell_log.read_text()[-600:]}"
    wait_for(lambda: "paw surface rendered: paw taskbar" in shell_log.read_text(),
             "the panel rendered")
    for title in ("Terminal", "Notes", "Editor"):
        desktop.spawn([probe, "--window-only"],
                      env=dict(PAW_PROBE_TITLE=title, PAW_PROBE_APP_ID="zz"))
        wait_for(lambda: title in msg("get", "windows"), f"{title} mapped")
    pointer = desktop.virtual_pointer(pointer_probe, 1280, 720)

    # The shell subscribes asynchronously: snap until it has heard.
    def opened():
        msg("snap_left")
        try:
            desktop.wait_for(lambda: "paw overview shown" in shell_log.read_text(),
                             "overlay", timeout=1)
            return True
        except harness.Timeout:
            msg("overview_cancel")
            msg("restore")
            return False
    for _ in range(10):
        if opened():
            break
    assert "paw overview shown on HEADLESS-1" in shell_log.read_text()
    assert msg("get", "overview").splitlines()[1].startswith("overview-assist HEADLESS-1 2 0 640 0"),\
        msg("get", "overview")

    # The overlay takes no input: over the snapped window, the pointer is on the window.
    pointer("move", "300", "400")
    wait_for(lambda: seat().get("pointer", [""])[0] == "window", "the pointer on the window")

    grim = shutil.which("grim")
    if grim:
        def light_pixels(x, y, w, h, shot, level=150):
            return sum(1 for j in range(y, y + h) for i in range(x, x + w)
                       if min(shot.at(i, j)) > level)

        def titled():
            shot = harness.grab(grim, env)
            lines = msg("get", "overview").splitlines()[2:]
            windows = [l.split(" ", 5) for l in lines if l.startswith("overview-window")]
            assert len(windows) == 2, lines
            for _, x, y, w, h, _ in windows:
                x, y, w, h = int(x), int(y), int(w), int(h)
                assert x >= 640, lines
                if not light_pixels(x + 24, y + h - 24, w - 30, 24, shot) > 20:
                    return False
            # No search box where the overview has it, over the top of the screen's middle.
            return light_pixels(440, 8, 400, 32, shot, 90) == 0
        wait_for(titled, "titles in the slot and no search box")
        print("Text checked")

    # A click beside the slot dismisses Snap Assist and reaches the window under it.
    pointer("click", "left")
    wait_for(lambda: "paw overview hidden" in shell_log.read_text(), "overlay hidden")
    assert msg("get", "overview").startswith("closed"), msg("get", "overview")
print("Snap Assist's shell passed")
