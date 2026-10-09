# SPDX-License-Identifier: GPL-3.0-or-later
"""Window swallowing with an X11 window: the process of an XWayland window comes from its
_NET_WM_PID, so an X11 application started from a Wayland terminal takes the terminal's tile and
gives it back when it closes."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, x11_probe, wayland_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

CONFIG = """return {
    layout = { tiling = true },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    windows = { swallow = { enabled = true, terminals = { "swallow-term" } } },
}"""

with harness.Compositor(compositor, CONFIG) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def windows():
        """app_id -> (tiled, x, y, width, height, visible, focused)."""
        rows = {}
        for f in desktop.rows("windows"):
            rows[f[8]] = (f[3] == "1", int(f[4]), int(f[5]), int(f[6]), int(f[7]), f[11] == "1",
                          f[1] == "1")
        return rows

    terminal = desktop.spawn([wayland_probe, "--window-only"],
                             env={"PAW_PROBE_APP_ID": "swallow-term",
                                  "PAW_PROBE_SPAWN_PROGRAM": x11_probe})
    wait_for(lambda: "swallow-term" in windows(), "terminal mapped")
    desktop.spawn([wayland_probe, "--window-only"], env={"PAW_PROBE_APP_ID": "plain"})
    wait_for(lambda: "plain" in windows(), "neighbour mapped")
    msg("focus_last")  # back to the terminal, as if the user had it in front of them
    wait_for(lambda: windows()["swallow-term"][6], "terminal focused")
    slot = windows()["swallow-term"]

    terminal.send_signal(10)  # SIGUSR1: start the X11 client from the terminal
    wait_for(lambda: "paw-x11-probe" in windows(), "X11 window mapped", timeout=30)
    wait_for(lambda: not windows()["swallow-term"][5], "terminal hidden")
    child = windows()["paw-x11-probe"]
    assert child[0] and child[1:3] == slot[1:3] and child[5], (child, slot)
    swallow = msg("get", "swallow")
    assert "swallow-term\t1\t0\tpaw-x11-probe" in swallow, swallow
    assert "paw-x11-probe\t0\t1\tswallow-term" in swallow, swallow

    # Closing the X11 window gives the terminal its tile back.
    subprocess.run([wayland_probe, "--close", "paw-x11-probe"], env=desktop.env, check=True,
                   timeout=30, stdout=subprocess.DEVNULL)
    wait_for(lambda: "paw-x11-probe" not in windows(), "X11 window closed", timeout=30)
    back = windows()["swallow-term"]
    assert back[5] and back[0] and back[1:3] == slot[1:3], (back, slot)
print("An X11 window swallows the terminal it was started from")
