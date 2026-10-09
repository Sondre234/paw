# SPDX-License-Identifier: GPL-3.0-or-later
"""The shell marks a window that asks for attention on the taskbar, from the compositor's
subscription and the foreign-toplevel list, and unmarks it once it has focus: seen as pixels in
the urgent color in the panel's screenshot band."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, shell, probe = (str(Path(p).resolve()) for p in sys.argv[1:4])
grim = sys.argv[4] if len(sys.argv) > 4 else ""
if not grim:
    print("grim is missing: the taskbar's urgent marker was not checked")
    sys.exit(0)

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false, workspaces = 4 },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    windows = { urgent_color = "#ff9e64" },
    shell = { panel_height = 52 },
}"""

with harness.Compositor(compositor, CONFIG, start=False) as desktop:
    root, env, msg = desktop.root, desktop.env, desktop.msg
    env.update(QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software",
               QT_FORCE_STDERR_LOGGING="1", XDG_DATA_HOME=str(root), XDG_DATA_DIRS=str(root),
               DBUS_SESSION_BUS_ADDRESS="disabled:")  # the shell must not use the real session bus
    shell_log = root / "shell.log"

    def is_orange(pixel):
        return pixel[0] > 200 and 120 < pixel[1] < 190 and 60 < pixel[2] < 140

    def orange():
        """How many pixels in the panel's band are the urgent color."""
        shot = harness.grab(grim, env)
        return sum(is_orange(shot.at(x, y))
                   for y in range(shot.height - 52, shot.height) for x in range(shot.width))

    def wait_for(predicate, message):
        desktop.wait_for(predicate, message, timeout=10)

    desktop.start()
    desktop.spawn([shell, "--config", str(desktop.config)], log="shell.log")
    desktop.detail = lambda: shell_log.read_text()[-600:]
    wait_for(lambda: "paw surface rendered: paw taskbar" in shell_log.read_text(),
             "the panel rendered")
    clients = {}
    for name in ("Alpha", "Beta"):
        clients[name] = desktop.spawn(
            [probe, "--commands"], env=dict(PAW_PROBE_TITLE=name,
                                            PAW_PROBE_APP_ID=name.lower()),
            stdin=subprocess.PIPE, text=True)
        wait_for(lambda: name in msg("get", "windows"), f"{name} mapped")
    wait_for(lambda: orange() == 0, "a quiet panel has no urgent color")

    clients["Alpha"].stdin.write("activate\n")
    clients["Alpha"].stdin.flush()
    wait_for(lambda: "alpha" in msg("get", "urgent"), "the compositor marks Alpha")
    wait_for(lambda: orange() >= 20, "the taskbar shows the urgent window")
    marked = orange()

    # The overview frames the thumbnail of the window asking for attention.
    def rims():
        """{title: rim pixel} of each thumbnail's top edge, or None while closed."""
        lines = msg("get", "overview").splitlines()
        if len(lines) < 3:
            return None
        shot = harness.grab(grim, env)
        found = {}
        for line in lines[2:]:
            if not line.startswith("overview-window"):
                continue
            _, x, y, w, h, tail = line.split(" ", 5)
            found[tail.split("\t")[1]] = shot.at(int(x) + int(w) // 2, int(y) + 1)
        return found

    msg("toggle_overview")
    desktop.wait_for(lambda: "paw overview shown" in shell_log.read_text(),
                     "the overview's text", timeout=5)
    desktop.wait_for(lambda: len(rims() or {}) == 2, "two thumbnails")
    wait_for(lambda: (r := rims()) and is_orange(r["Alpha"]) and not is_orange(r["Beta"]),
             "the overview frames only Alpha's thumbnail")
    msg("overview_cancel")

    msg("focus_urgent")
    wait_for(lambda: msg("get", "urgent") == "", "focus clears the mark")
    wait_for(lambda: orange() == 0, "the taskbar's marker goes with it")
print(f"The taskbar marks a window asking for attention ({marked} pixels of the urgent "
      "color) and unmarks it on focus")
