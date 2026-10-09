# SPDX-License-Identifier: GPL-3.0-or-later
"""The window switcher's pictures of windows in a session: as the switcher opens, each of its
cards asks for its window's picture, which the shell copies from the compositor through the
window control's capture source, scaled down by the compositor, and shows it. Two windows share a
title, which the switcher's lines would not tell apart; their numbers do, so each card pictures
its own window. Closing the switcher lets the sources go. The windows are on a workspace not
shown, so their colours on the screen can only be the pictures'. Without grim the pixels are left
out."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, shell, probe, window_probe = (str(Path(p).resolve()) for p in sys.argv[1:5])
grim = sys.argv[5] if len(sys.argv) > 5 else ""

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    notifications = { enabled = false },
    shell = { panel_height = 52 },
}"""
BODY = (0x41, 0x7b, 0xc4)  # the probe's window below its top band
# Where the switcher opens: in the middle of the output above the bar.
AREA = (300, 100, 980, 620)

with harness.Compositor(compositor, CONFIG, start=False) as desktop:
    root, env = desktop.root, desktop.env
    env.update(QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software",
               QT_FORCE_STDERR_LOGGING="1", XDG_DATA_HOME=str(root), XDG_DATA_DIRS=str(root),
               XDG_STATE_HOME=str(root), DBUS_SESSION_BUS_ADDRESS="disabled:")
    desktop.start()
    shell_log = root / "shell.log"

    def log():
        return shell_log.read_text()

    def layers():
        return {(row[0], row[1]): row[2:4] for row in desktop.rows("layers")}

    def windows():
        """By app id: whether the window is shown, and its title."""
        return {row[8]: (row[11] == "1", row[9]) for row in desktop.rows("windows")}

    def body_pixels():
        """How many pixels around the switcher have the windows' colour, or None without grim."""
        shot = harness.grab(grim, env)
        if shot is None:
            return None
        left, top, right, bottom = AREA
        return sum(1 for y in range(top, bottom, 2) for x in range(left, right, 2)
                   if all(abs(a - b) <= 2 for a, b in zip(shot.at(x, y), BODY)))

    desktop.detail = lambda: f"layers: {layers()}, windows: {windows()}\n{log()[-1500:]}"
    desktop.spawn([shell, "--config", str(desktop.config)], log="shell.log")
    desktop.wait_for(lambda: "paw surface rendered: paw taskbar" in log(), "the panel")

    # Two windows, each moved to the second workspace while its title is its own, then given
    # the same title.
    clients = {}
    for app_id, title in (("twin-a", "Twin"), ("twin-b", "Other")):
        clients[app_id] = desktop.spawn([probe, "--commands"], stdin=subprocess.PIPE, text=True,
                                        env={"PAW_PROBE_APP_ID": app_id,
                                             "PAW_PROBE_TITLE": title})
        desktop.wait_for(lambda: app_id in windows(), f"{app_id} mapped")
        subprocess.run([window_probe, title, "workspace", "2"], env=env, check=True, timeout=30,
                       stdout=subprocess.DEVNULL)
        desktop.wait_for(lambda: not windows()[app_id][0], f"{app_id} on the second workspace")
    clients["twin-b"].stdin.write("title Twin\n")
    clients["twin-b"].stdin.flush()
    desktop.wait_for(lambda: windows()["twin-b"][1] == "Twin", "the second window named Twin")
    switcher = ("paw-switcher", "HEADLESS-1")
    assert not body_pixels(), "the windows' colour on the screen before any picture"

    # Each card asks for its own window's picture, at twice the pictures' 150 pixels' height
    # wide, and follows it in one session while the switcher is open.
    desktop.msg("switcher")
    desktop.wait_for(lambda: layers().get(switcher, ["", "0"])[1] == "1", "the switcher shown")

    def sources():
        return [(row[0], row[1].split("x")[1], row[2], row[3]) for row in desktop.rows("pictures")]

    desktop.wait_for(lambda: sources() == [("300x188", "188", "1", "Twin")] * 2,
                     "a picture's source for each window, scaled to the switcher's box",
                     detail=lambda: f"pictures: {desktop.rows('pictures')}")
    # Two windows' bodies, every other pixel of each, in pictures 150 pixels tall.
    desktop.wait_for(lambda: (pixels := body_pixels()) is None or pixels > 3000,
                     "the windows' pictures on the switcher's cards",
                     detail=lambda: f"{body_pixels()} pixels of the windows' colour")
    assert not any(shown for shown, _ in windows().values()), windows()

    desktop.msg("switcher_cancel")
    desktop.wait_for(lambda: "paw switcher hidden on HEADLESS-1" in log(),
                     "the switcher hidden once it has faded")
    desktop.wait_for(lambda: not desktop.rows("pictures"), "the pictures' sources gone with it")
    assert not body_pixels(), "the pictures left on the screen"

    for message in ("ReferenceError", "TypeError", "is not defined", "Cannot read"):
        assert message not in log(), log()
print("switcher pictures: two windows of one title out of sight, each on its own card"
      + ("" if grim else " (no grim: no pixels)"))
