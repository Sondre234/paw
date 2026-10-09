# SPDX-License-Identifier: GPL-3.0-or-later
"""layout.scroll.follow: with "center" the view centers the column focus moves to, with "edge"
it moves only as far as needed, and with "never" focus alone does not move it."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = true, tile_layout = "scroll", gap = 0,
               scroll = { follow = "%s", width = 0.5 } },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
}"""

# After three 640-wide columns the view shows the last two (B at 0, C at 640, A at -640).
# Focusing A then puts it at: 0 (edge: just brought into view), 320 (center), and -640 (never).
EXPECTED = {"edge": 0, "center": 320, "never": -640}

for follow, expected in EXPECTED.items():
    with harness.Compositor(compositor, CONFIG % follow) as desktop:

        def windows():
            return {r[9]: (r[1] == "1", int(r[4])) for r in desktop.rows("windows")}

        desktop.detail = lambda: f"{follow}: {windows()}"

        for title in ("A", "B", "C"):
            desktop.spawn([probe, "--window-only"],
                          env={"PAW_PROBE_TITLE": title,
                               "PAW_PROBE_APP_ID": f"app-{title.lower()}"})
            desktop.wait_for(lambda: title in windows() and windows()[title][0],
                             f"{title} focused")
        # New windows move the view in every mode: C is the rightmost column, in view.
        desktop.wait_for(lambda: windows()["C"][1] + 640 == 1280 or follow == "center",
                         "view shows the new window")
        subprocess.run([probe, "--activate", "app-a"], env=desktop.env, check=True, timeout=5,
                       stdout=subprocess.DEVNULL)
        desktop.wait_for(lambda: windows()["A"][0], "A focused")
        # Let a running glide end, then check where A sits.
        desktop.wait_for(lambda: windows()["A"][1] == expected, f"A at {expected} with {follow}")
print("scroll follow modes passed")
