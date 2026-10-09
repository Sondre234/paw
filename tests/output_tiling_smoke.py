# SPDX-License-Identifier: GPL-3.0-or-later
"""Tiled windows follow their output across two outputs of different sizes and scales: when
their output is disabled they join the other one's tiling, fullscreen fits it, and turning
tiling off leaves them floating inside it."""
from pathlib import Path
import signal
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

# HEADLESS-1 is 2048x1152 logical, like a 1440p monitor at 1.25; HEADLESS-2 is smaller and
# sits to its right unless placed. The pointer starts at 0, 0.
def config(primary=None, first="position = { x = 0, y = 0 }", second=""):
    return f"""return {{
    xwayland = false,
    layout = {{ tiling = true }},
    outputs = {{{f' primary = "{primary}",' if primary else ""}
        monitors = {{
            ["HEADLESS-1"] = {{ mode = "2560x1440", scale = 1.25, {first} }},
            ["HEADLESS-2"] = {{ mode = "1600x900", {second} }},
        }},
    }},
}}"""


with harness.Compositor(compositor, config(), env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    msg, log = desktop.msg, desktop.log

    def windows():
        """(focused, tiled, x, y, width, height) per window, oldest first."""
        return [(r[1] == "1", r[3] == "1", *map(int, r[4:8])) for r in desktop.rows("windows")]

    def outputs():
        return {r[0]: (r[1] == "1", *map(int, r[2:6])) for r in desktop.rows("outputs")}

    desktop.detail = lambda: f"windows: {windows()}, outputs: {outputs()}"

    def inside(box, area):
        x, y, width, height = box
        return (x >= area[0] and y >= area[1] and x + width <= area[0] + area[2] and
                y + height <= area[1] + area[3])

    reloads = 0

    def reload(text):
        global reloads
        reloads += 1
        desktop.config.write_text(text)
        desktop.server.send_signal(signal.SIGHUP)
        desktop.wait_for(lambda: log.read_text().count("Configuration reloaded") == reloads,
                         "reload")

    def launch():
        desktop.spawn([probe, "--external-control"])

    assert msg("get", "tiling") == "on\n"
    assert outputs()["HEADLESS-1"] == (True, 0, 0, 2048, 1152), outputs()

    # Two tiles on the large output, where the pointer starts.
    for count in (1, 2):
        launch()
        desktop.wait_for(lambda: len(windows()) == count and all(w[1] for w in windows()),
                         f"window {count} tiled")
    large = outputs()["HEADLESS-1"][1:]
    desktop.wait_for(lambda: all(inside(w[2:], large) for w in windows()) and
                     harness.disjoint([w[2:] for w in windows()]), "two tiles on the large output")

    # Moving the large output far below takes the pointer, now off every output, to
    # the nearest point of the small one: the next window tiles there.
    reload(config(first="position = { x = 0, y = 3000 }"))
    small = outputs()["HEADLESS-2"][1:]
    assert small == (2048, 0, 1600, 900), outputs()
    launch()
    desktop.wait_for(lambda: len(windows()) == 3 and windows()[2][1] and
                     inside(windows()[2][2:], small), "third window tiled on the small output")

    # Disabling the large output moves its tiles into the small output's tiling. The
    # only output left starts at the origin, far from the tiles at y = 3000, so no
    # leftover coordinates happen to fit.
    reload(config(first="enabled = false", second="position = { x = 3000, y = 200 }"))
    state = outputs()
    assert state["HEADLESS-1"][0] is False and \
        state["HEADLESS-2"] == (True, 0, 0, 1600, 900), state
    small = state["HEADLESS-2"][1:]
    desktop.wait_for(lambda: all(w[1] and inside(w[2:], small) for w in windows()) and
                     harness.disjoint([w[2:] for w in windows()]),
                     "tiles of the disabled output rejoined the small output's tiling")

    # Fullscreen covers exactly the output the window is on now, above each probe's
    # 48-pixel panel.
    msg("fullscreen")
    above_panels = lambda: (*small[:3], small[3] - 48 * len(windows()))
    desktop.wait_for(lambda: [w[2:] for w in windows() if w[0]] == [above_panels()],
                     "fullscreen fits the small output")
    msg("fullscreen")
    desktop.wait_for(lambda: all(w[1] and inside(w[2:], small) for w in windows()) and
                     harness.disjoint([w[2:] for w in windows()]),
                     "left fullscreen back into its tile")

    # Leaving the tiling keeps every window floating inside the output it is on now,
    # at its floating size.
    msg("toggle_tiling")
    desktop.wait_for(lambda: not any(w[1] for w in windows()) and
                     all(inside(w[2:], small) and w[4:] == (320, 240) for w in windows()),
                     "floating windows stayed on the small output")
print("Tiles moved between outputs of different sizes, fullscreen, and restore passed")
