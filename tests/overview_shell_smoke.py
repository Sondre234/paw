# SPDX-License-Identifier: GPL-3.0-or-later
"""The shell draws the overview's text over the compositor's thumbnails: it shows an overlay
while the overview is open on its output, hides it after, and the overlay puts each window's
title and the search box on screen (found as light pixels on a screenshot)."""
from pathlib import Path
import shutil
import sys

import harness

compositor, shell, probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false, workspaces = 4, workspace_names = { "web", "code" } },
    overview = { animation = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
}"""

with harness.Compositor(compositor, CONFIG, start=False) as desktop:
    root, env, msg = desktop.root, desktop.env, desktop.msg
    env.update(QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software",
               QT_FORCE_STDERR_LOGGING="1", XDG_DATA_HOME=str(root), XDG_DATA_DIRS=str(root),
               DBUS_SESSION_BUS_ADDRESS="disabled:")  # the shell must not use the real session bus
    shell_log = root / "shell.log"

    def wait_for(predicate, message):
        desktop.wait_for(predicate, message, timeout=30)

    desktop.start()
    desktop.spawn([shell, "--config", str(desktop.config)], log="shell.log")
    desktop.detail = lambda: shell_log.read_text()[-600:]
    wait_for(lambda: "paw surface rendered: paw taskbar" in shell_log.read_text(),
             "the panel rendered")
    for title in ("Terminal", "Notes"):
        desktop.spawn([probe, "--window-only"],
                      env=dict(PAW_PROBE_TITLE=title, PAW_PROBE_APP_ID="zz"))
        wait_for(lambda: title in msg("get", "windows"), f"{title} mapped")

    # The shell subscribes asynchronously: open until it has heard.
    def opened():
        msg("toggle_overview")
        try:
            desktop.wait_for(lambda: "paw overview shown" in shell_log.read_text(),
                             "overlay", timeout=1)
            return True
        except harness.Timeout:
            msg("overview_cancel")
            return False
    for _ in range(10):
        if opened():
            break
    assert "paw overview shown on HEADLESS-1" in shell_log.read_text()

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
                if not light_pixels(x + 24, y + h - 24, w - 30, 24, shot) > 20:
                    return False
            # The search box: its placeholder text is light on the panel colour.
            return light_pixels(440, 8, 400, 32, shot, 90) > 20
        wait_for(titled, "titles and the search box drawn")
        print("Text checked")
    msg("overview_cancel")
    wait_for(lambda: "paw overview hidden" in shell_log.read_text(), "overlay hidden")
print("Overview shell passed")
