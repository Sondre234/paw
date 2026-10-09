# SPDX-License-Identifier: GPL-3.0-or-later
"""10 bits per channel on headless outputs: a monitor with bit_depth = 10 is drawn in a 10-bit
format and says so in get outputs, one whose output refuses 10 bits (PAW_TEST_REFUSE_10BIT,
as a monitor or renderer without them would) stays at 8 and says why, a wlr-output-management
change keeps the depth, a mirror is drawn in its own, and a reload without bit_depth goes back to
8. The picture is the same either way."""
from pathlib import Path
import shutil
import subprocess
import sys

import harness

compositor, randr_probe = (str(Path(p).resolve()) for p in sys.argv[1:3])
grim = shutil.which("grim")

CONFIG = """return {{
    xwayland = false,
    animations = {{ enabled = false }},
    appearance = {{ background = "#336699" }},
    outputs = {{ monitors = {{
        ["HEADLESS-1"] = {{ {depth} }},
        ["HEADLESS-2"] = {{ {depth} }},
        ["HEADLESS-3"] = {{ mirror = "HEADLESS-1", {depth} }},
    }} }},
}}"""

with harness.Compositor(compositor, CONFIG.format(depth="bit_depth = 10"),
                        env={"WLR_HEADLESS_OUTPUTS": "3",
                             "PAW_TEST_REFUSE_10BIT": "HEADLESS-2"}) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def depths():
        return {r[0]: int(r[12]) for r in desktop.rows("outputs")}

    def randr(*args):
        result = subprocess.run([randr_probe, *args], env=desktop.env, capture_output=True,
                                text=True, timeout=30)
        assert result.returncode == 0, result.stderr
        return result.stdout.split("\n")[:-1]

    desktop.detail = lambda: f"depths: {depths()}"
    wait_for(lambda: depths() == {"HEADLESS-1": 10, "HEADLESS-2": 8, "HEADLESS-3": 10},
             "10 bits where taken")
    assert "HEADLESS-2 cannot be drawn in 10 bits; it stays at 8" in desktop.log.read_text()
    assert "HEADLESS-1 cannot be drawn" not in desktop.log.read_text()

    # Drawn in 10 bits, the picture is the background's colour as at 8; the mirror shows it too.
    if grim:
        shot = harness.grab(grim, desktop.env, "HEADLESS-1")
        assert shot.at(640, 360) == (0x33, 0x66, 0x99), shot.at(640, 360)
    path = desktop.root / "mirror.ppm"

    def mirrored():
        # Nothing to read until the mirror has drawn its first frame.
        if desktop.run("headless_output", "capture", "HEADLESS-3", str(path)).returncode != 0:
            return False
        return harness.Shot(path.read_bytes()).at(640, 360) == (0x33, 0x66, 0x99)
    wait_for(mirrored, "the mirror shows the background")

    # A change through wlr-output-management keeps the depth the configuration gives.
    assert randr("apply", "HEADLESS-1", "scale=2") == ["succeeded"]
    assert depths()["HEADLESS-1"] == 10, depths()

    # Without bit_depth, 8 again.
    desktop.reload(CONFIG.format(depth=""))
    wait_for(lambda: depths() == {"HEADLESS-1": 8, "HEADLESS-2": 8, "HEADLESS-3": 8},
             "8 bits after the reload")
print("Monitors are drawn in 10 bits where asked for and taken, else in 8")
