# SPDX-License-Identifier: GPL-3.0-or-later
"""X11 windows that ask for attention (_NET_WM_STATE_DEMANDS_ATTENTION, the urgency flag of
WM_HINTS) while unfocused follow windows.activation like xdg-activation requests do."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, x11_probe, wayland_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])


def settings(activation):
    return f"""return {{
    xwayland = true,
    layout = {{ tiling = false }},
    windows = {{ activation = "{activation}",
                 rules = {{ {{ title = "^Quiet X11", focus = false }} }} }},
}}"""


X11 = "paw-x11-probe"

with harness.Compositor(compositor, settings("urgent")) as desktop:
    msg, rows, wait_for = desktop.msg, desktop.rows, desktop.wait_for

    def urgent():
        return [r[8] for r in rows("urgent")]

    def focused():
        return [r[8] for r in rows("windows") if r[1] == "1"]

    desktop.detail = lambda: f"urgent {urgent()} focused {focused()}"

    def tell(client, command):
        client.stdin.write(command + "\n")
        client.stdin.flush()

    x = desktop.spawn([x11_probe, "commands"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                      text=True)
    assert x.stdout.readline().strip() == "X11 window mapped and focused"
    assert x.stdout.readline().strip() == "waiting for commands"
    wait_for(lambda: focused() == [X11], "the X11 window is focused")
    w = desktop.spawn([wayland_probe, "--commands"], stdin=subprocess.PIPE, text=True)
    wait_for(lambda: focused() == ["paw-probe"], "the Wayland window takes focus")
    assert urgent() == []

    # The state message, then the flag in WM_HINTS, each set and cleared by the client.
    tell(x, "demand")
    wait_for(lambda: urgent() == [X11], "demands attention marks the X11 window")
    assert focused() == ["paw-probe"]
    tell(x, "undemand")
    wait_for(lambda: urgent() == [], "the client withdraws the demand")
    tell(x, "hint")
    wait_for(lambda: urgent() == [X11], "the urgency hint marks it")
    tell(x, "unhint")
    wait_for(lambda: urgent() == [], "the client clears the hint")
    tell(x, "hint")
    wait_for(lambda: urgent() == [X11], "the hint again")
    msg("focus_urgent")
    wait_for(lambda: focused() == [X11] and urgent() == [], "focus_urgent focuses it")

    # It is focused, so asking again changes nothing.
    tell(x, "demand")
    desktop.stays(lambda: urgent() == [], "the focused window became urgent")
    tell(x, "undemand")
    subprocess.run([wayland_probe, "--activate", "paw-probe"], env=desktop.env, check=True,
                   timeout=5, stdout=subprocess.DEVNULL)
    wait_for(lambda: focused() == ["paw-probe"], "back to the Wayland window")

    # A window that asked before it mapped and opens without focus (a rule) is urgent from
    # the start.
    quiet = desktop.spawn([x11_probe, "commands"],
                          env={"PAW_PROBE_URGENT_ON_MAP": "1",
                               "PAW_PROBE_TITLE": "Quiet X11"},
                          stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    assert "X11 window mapped" in quiet.stdout.readline()
    wait_for(lambda: [r[9] for r in rows("urgent")] == ["Quiet X11"],
             "the window that asked before it mapped is urgent")
    assert focused() == ["paw-probe"]
    msg("focus_urgent")
    wait_for(lambda: [r[9] for r in rows("windows") if r[1] == "1"] == ["Quiet X11"] and
             urgent() == [], "and focus_urgent takes it")
    msg("close")
    assert desktop.reap(quiet, timeout=10) == 0
    wait_for(lambda: len(rows("windows")) == 2, "the quiet window closed")
    subprocess.run([wayland_probe, "--activate", "paw-probe"], env=desktop.env, check=True,
                   timeout=5, stdout=subprocess.DEVNULL)
    wait_for(lambda: focused() == ["paw-probe"], "back to the Wayland window")
    msg("focus_urgent")  # nothing urgent left
    assert focused() == ["paw-probe"]

    # Under "focus" the request focuses it; under "ignore" nothing happens.
    desktop.reload(settings("ignore"))
    tell(x, "demand")
    desktop.stays(lambda: urgent() == [] and focused() == ["paw-probe"],
                  "an ignored request did something", duration=.4)
    tell(x, "undemand")
    desktop.reload(settings("focus"))
    tell(x, "demand")
    wait_for(lambda: focused() == [X11] and urgent() == [], "focus policy focuses it")

    w.stdin.close()
    msg("close")
    x.stdin.close()
    assert desktop.reap(x, timeout=10) == 0
print("X11 attention requests follow windows.activation")
