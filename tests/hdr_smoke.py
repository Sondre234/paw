# SPDX-License-Identifier: GPL-3.0-or-later
"""HDR on headless outputs, which have no EDID: without hdr anywhere nothing changes and
color-management-v1 is not offered; a monitor asking for HDR whose EDID does not offer BT.2020
with PQ stays SDR and says why, and so does one that offers them (PAW_TEST_HDR stands in for
its EDID) with a renderer that cannot convert colours, as pixman and GLES cannot. With
PAW_TEST_VULKAN=1 and a GPU, the Vulkan renderer offers color-management-v1, and the test of
HDR, which the headless backend refuses (it takes no colour space), leaves the monitor SDR and
drawn as before: not run by default, as it needs a GPU. Driving a monitor in HDR needs a real
one."""
import os
from pathlib import Path
import sys

import harness

compositor = str(Path(sys.argv[1]).resolve())

CONFIG = """return {{
    xwayland = false,
    animations = {{ enabled = false }},
    appearance = {{ background = "#336699" }},
    outputs = {{ monitors = {{
        ["HEADLESS-1"] = {{ {hdr} }},
        ["HEADLESS-2"] = {{ mirror = "HEADLESS-1" }},
    }} }},
}}"""


def outputs(desktop):
    """name: (bits, colours) per monitor."""
    return {r[0]: (int(r[12]), r[13]) for r in desktop.rows("outputs")}


# Without hdr anywhere: SDR, and no colour management offered.
with harness.Compositor(compositor, CONFIG.format(hdr=""),
                        env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    desktop.wait_for(lambda: outputs(desktop) == {"HEADLESS-1": (8, "sdr"),
                                                  "HEADLESS-2": (8, "sdr")}, "SDR")
    assert "color-management" not in desktop.log.read_text()
    assert "stays SDR" not in desktop.log.read_text()

# Asked for, on a monitor without HDR in its EDID: SDR, and why.
with harness.Compositor(compositor, CONFIG.format(hdr="hdr = true"),
                        env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    desktop.wait_for(lambda: outputs(desktop)["HEADLESS-1"] == (8, "sdr"), "SDR")
    log = desktop.log.read_text()
    assert "HEADLESS-1 stays SDR: the monitor does not offer BT.2020 with PQ" in log, log
    assert "color-management-v1 is not offered: the renderer cannot convert colours" in log

# On a monitor that offers it, with pixman: SDR, for the renderer.
with harness.Compositor(compositor, CONFIG.format(hdr="hdr = true"),
                        env={"WLR_HEADLESS_OUTPUTS": "2",
                             "PAW_TEST_HDR": "HEADLESS-1"}) as desktop:
    desktop.wait_for(lambda: outputs(desktop)["HEADLESS-1"] == (8, "sdr"), "SDR")
    log = desktop.log.read_text()
    assert "HEADLESS-1 stays SDR: the renderer cannot convert colours" in log, log

if os.environ.get("PAW_TEST_VULKAN") != "1":
    print("HDR stays off where it cannot be had (the Vulkan part needs PAW_TEST_VULKAN=1)")
    sys.exit(0)

with harness.Compositor(compositor, CONFIG.format(hdr="hdr = true"),
                        env={"WLR_HEADLESS_OUTPUTS": "2", "WLR_RENDERER": "vulkan",
                             "PAW_TEST_HDR": "HEADLESS-1,HEADLESS-2"}) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for
    desktop.detail = lambda: f"outputs: {outputs(desktop)}"
    # The renderer converts colours, so applications may describe theirs; the headless backend
    # takes no colour space, so the test of HDR fails and the monitor stays as it was, SDR.
    wait_for(lambda: "HEADLESS-1 stays SDR: its test refused HDR" in desktop.log.read_text(),
             "HDR refused by the test")
    assert "Offering color-management-v1" in desktop.log.read_text()
    assert outputs(desktop)["HEADLESS-1"] == (8, "sdr"), outputs(desktop)
    # Its picture is drawn as before, as its mirror shows.
    path = desktop.root / "mirror.ppm"

    def mirrored():
        if desktop.run("headless_output", "capture", "HEADLESS-2", str(path)).returncode != 0:
            return False
        colour = harness.Shot(path.read_bytes()).at(640, 360)
        return all(abs(a - b) <= 2 for a, b in zip(colour, (0x33, 0x66, 0x99)))
    wait_for(mirrored, "the picture drawn in SDR")
    assert "HEADLESS-2 stays SDR" not in desktop.log.read_text()
    desktop.reload(CONFIG.format(hdr=""))
    wait_for(lambda: outputs(desktop)["HEADLESS-1"] == (8, "sdr"), "SDR after the reload")
print("HDR stays off where it cannot be had, saying why, and colour management needs Vulkan")
