# SPDX-License-Identifier: GPL-3.0-or-later
"""Apply outputs.monitors to three headless outputs, then change them on reload."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

FIRST = """return {
    xwayland = false,
    outputs = {
        primary = "HEADLESS-2",
        monitors = {
            ["HEADLESS-1"] = { mode = "1600x900@60", position = { x = -1280, y = 100 },
                               scale = 1.25 },
            ["HEADLESS-2"] = { mode = "1920x1080@144", position = { x = 0, y = 0 } },
            ["HEADLESS-3"] = { mode = "1080x1920", transform = 1 },
        },
    },
}"""
# HEADLESS-2 turns off and the rest fall back to the side-by-side row.
SECOND = """return {
    xwayland = false,
    outputs = {
        order = { "HEADLESS-3", "HEADLESS-1" },
        monitors = { ["HEADLESS-2"] = { enabled = false } },
    },
}"""
# Disabling every output keeps the last one on.
THIRD = """return {
    xwayland = false,
    outputs = { monitors = { ["HEADLESS-1"] = { enabled = false },
                             ["HEADLESS-2"] = { enabled = false },
                             ["HEADLESS-3"] = { enabled = false } } },
}"""

with harness.Compositor(compositor, FIRST, env={"WLR_HEADLESS_OUTPUTS": "3"}) as desktop:
    log = desktop.log

    def outputs():
        return {row[0]: (row[1] == "1", int(row[2]), int(row[3]), int(row[4]), int(row[5]),
                         float(row[6]), int(row[7]), row[8]) for row in desktop.rows("outputs")}

    def advertised():
        result = subprocess.run([probe, "--globals"], env=desktop.env, capture_output=True,
                                text=True, timeout=30, check=True)
        return result.stdout.split().count("wl_output")

    state = outputs()
    # 1600x900 at 1.25 is 1280x720 logical; the unpositioned rotated output follows
    # the rightmost positioned one. The layout shifts right by 1280 so it starts at 0:
    # X11 windows get no input at negative coordinates.
    assert state["HEADLESS-1"] == (True, 0, 100, 1280, 720, 1.25, 0,
                                   "1600x900@60.000"), state
    assert state["HEADLESS-2"] == (True, 1280, 0, 1920, 1080, 1.0, 0,
                                   "1920x1080@144.000"), state
    assert state["HEADLESS-3"] == (True, 3200, 0, 1920, 1080, 1.0, 1,
                                   "1080x1920@0.000"), state
    assert advertised() == 3

    desktop.reload(SECOND)
    state = outputs()
    assert state["HEADLESS-2"][0] is False, state
    # Headless outputs have no modes to fall back to, so HEADLESS-3 keeps its custom
    # mode; it only loses the rotation.
    assert state["HEADLESS-3"][:5] == (True, 0, 0, 1080, 1920), state
    assert state["HEADLESS-1"][:5] == (True, 1080, 0, 1600, 900), state
    assert state["HEADLESS-1"][5:7] == (1.0, 0), state
    assert advertised() == 2, "a disabled output is still advertised"

    desktop.reload(THIRD)
    state = outputs()
    assert sum(enabled for enabled, *_ in state.values()) == 1, state
    assert "it is the only output" in log.read_text()
    assert advertised() == 1

    desktop.reload(FIRST)
    state = outputs()
    assert all(enabled for enabled, *_ in state.values()), state
    assert state["HEADLESS-2"][1:3] == (1280, 0), state
    assert advertised() == 3
print("Output modes, scale, transform, positions, and disabling passed")
