# SPDX-License-Identifier: GPL-3.0-or-later
"""Window rules' look: `shadow`, `border`, `border_color`, `border_inactive_color` (a gradient
too), `corner_radius` and `controls` (paw's window controls over a window drawing its own frame,
as the probe does) give the windows they match their own, read through `get frames`. Unlike
the actions, they follow the window's title as it changes and the rules as they are reloaded. A
build without the rounded-corners patch rounds nothing, which the test then only checks."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    animations = { enabled = false },
    layout = { tiling = false },
    windows = {
        border_width = 2, border_color = "#7da8ff", border_inactive_color = "#404a5c",
        corner_radius = 10, round = "always", placement = "smart",
        shadow = { enabled = true },
        rules = { %s },
    },
}"""
RULES = """
            { app_id = "^bare$", shadow = false, border = false, corner_radius = 0 },
            { app_id = "^red$", border_color = "#ff0000", border_inactive_color = "#00ff00",
              corner_radius = 4, controls = true },
            { app_id = "^rainbow$", border_color = { "#ff0000", "#0000ff", angle = 45 } },
            { title = "Paused$", shadow = false, border_color = "#ffff00", controls = true },"""

with harness.Compositor(compositor, CONFIG % RULES) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def frames():
        """app_id -> (corner radius, shadow drawn, border drawn as, border colour)."""
        return {r[0]: (int(r[5]), r[6] == "1", r[12], r[13]) for r in desktop.rows("frames")}

    def controls():
        """app_id -> the style of the controls drawn over it, or "none"."""
        return {r[0]: r[3] for r in desktop.rows("frames")}

    desktop.detail = lambda: f"frames: {msg('get', 'frames')}"
    clients = {}

    def launch(app_id, title):
        clients[app_id] = desktop.spawn([probe, "--commands"], stdin=subprocess.PIPE, text=True,
                                        env={"PAW_PROBE_TITLE": title,
                                             "PAW_PROBE_APP_ID": app_id})
        wait_for(lambda: any(w.app_id == app_id and w.focused for w in desktop.windows()),
                 f"{app_id} open and focused")

    def focus(app_id):
        subprocess.run([probe, "--activate", app_id], env=desktop.env, check=True, timeout=30,
                       stdout=subprocess.DEVNULL)
        wait_for(lambda: any(w.app_id == app_id and w.focused for w in desktop.windows()),
                 f"{app_id} focused")

    for app_id in ("plain", "bare", "red", "rainbow"):
        launch(app_id, app_id.capitalize())
    rounded = frames()["plain"][0] == 10  # wlroots with paw's rounded-corners patch

    def radius(value):
        return value if rounded else 0

    # The rules' look, the windows without the focus; the gradient of the focused border makes
    # the rainbow's a gradient however focused.
    wait_for(lambda: frames() == {
        "plain": (radius(10), True, "color", "#404a5cff"),
        "bare": (0, False, "none", "-"),
        "red": (radius(4), True, "color", "#00ff00ff"),
        "rainbow": (radius(10), True, "gradient", "-"),
    }, "the rules' look")
    assert controls() == {"plain": "none", "bare": "none", "red": "flat", "rainbow": "none"}, \
        controls()
    focus("red")
    wait_for(lambda: frames()["red"][3] == "#ff0000ff" and frames()["rainbow"][2] == "gradient",
             "red's own focused border")
    focus("plain")
    wait_for(lambda: frames()["plain"][3] == "#7da8ffff" and frames()["red"][3] == "#00ff00ff",
             "the focused border of windows.border_color, red's own unfocused one")

    # The look follows the title: "... Paused" loses its shadow and takes a yellow border, and
    # gets them back as the title changes again.
    clients["plain"].stdin.write("title Film Paused\n")
    clients["plain"].stdin.flush()
    wait_for(lambda: frames()["plain"] == (radius(10), False, "color", "#ffff00ff"),
             "the look of the title's rule")
    assert controls()["plain"] == "flat", controls()
    clients["plain"].stdin.write("title Film\n")
    clients["plain"].stdin.flush()
    wait_for(lambda: frames()["plain"] == (radius(10), True, "color", "#7da8ffff"),
             "the window's own look back with its title")
    assert controls()["plain"] == "none", controls()

    # A reload applies the rules as they are now: red loses its own colours, bare draws a shadow
    # where windows.shadow is off, and the others none.
    desktop.reload((CONFIG % """
            { app_id = "^bare$", shadow = true, border = false },""").replace(
        "shadow = { enabled = true }", "shadow = { enabled = false }"))
    wait_for(lambda: frames() == {
        "plain": (radius(10), False, "color", "#7da8ffff"),
        "bare": (radius(10), True, "none", "-"),
        "red": (radius(10), False, "color", "#404a5cff"),
        "rainbow": (radius(10), False, "color", "#404a5cff"),
    }, "the reloaded rules' look")
    assert controls()["red"] == "none", controls()
print("Window rules' look applied" + ("" if rounded else " (wlroots without rounded corners)"))
