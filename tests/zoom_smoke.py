# SPDX-License-Identifier: GPL-3.0-or-later
"""The magnifier: zoom_in and zoom_out ease the level, the output shows the scene enlarged around
the pointer (checked with grim where the capture sees what the output shows), zoom_reset returns
to 1x, and Super+scroll steps when configured. The pointer, and Super, are a virtual pointer's
and keyboard's (pointer_probe)."""
from pathlib import Path
import sys

import harness

compositor, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])
grim = sys.argv[4] if len(sys.argv) > 4 else ""

BACKGROUND = (0x00, 0x00, 0x00)
BODY = (0x41, 0x7b, 0xc4)


def settings(duration=400, extra=""):
    return f"""return {{
    xwayland = false,
    layout = {{ tiling = true, gap = 100, smart_gaps = false }},
    appearance = {{ background = '#000000' }},
    animations = {{ enabled = true, duration = 10 }},
    zoom = {{ step = 2, max = 4, duration = {duration}{extra} }},
}}"""


with harness.Compositor(compositor, settings()) as desktop:
    env, msg = desktop.env, desktop.msg

    def zoom():
        """(level, target, magnified outputs), levels in thousandths"""
        return tuple(int(n) for n in msg("get", "zoom").splitlines()[0].split("\t"))

    def windows():
        return desktop.rows("windows")

    desktop.detail = lambda: f"zoom: {zoom()}, windows: {windows()}"
    pointer = desktop.virtual_pointer(pointer_probe, 1280, 720)

    assert zoom() == (1000, 1000, 0), zoom()
    desktop.spawn([probe, "--external-control"])
    desktop.wait_for(lambda: len(windows()) == 1, "window")
    desktop.wait_for(lambda: msg("get", "animations").split("\t")[0].strip() == "0",
                     "opening animation over")
    x0, y0, w, h = (int(n) for n in windows()[0][4:8])
    assert x0 >= 50 and y0 >= 50, (x0, y0)

    # The pointer 50 px inside the window's left edge, half way down.
    px, py = x0 + 50, y0 + h // 2
    pointer("move", str(px), str(py))
    probe_at = (x0 - 25, py)  # outside the window unless magnified 2x about the pointer
    if grim:
        assert harness.grab(grim, env).at(*probe_at) == BACKGROUND

    # Zooming in eases: in between, then arrived at 2x.
    msg("zoom_in")
    desktop.wait_for(lambda: 1000 < zoom()[0] < 2000 and zoom()[1] == 2000, "easing in")
    desktop.wait_for(lambda: zoom() == (2000, 2000, 1), "zoomed 2x")
    if grim:
        # The window's left edge is now 100 px further left than the pointer's 50.
        shot = harness.grab(grim, env)
        assert shot.at(*probe_at) == BODY, shot.at(*probe_at)

    # More steps stop at zoom.max; out again; reset.
    msg("zoom_in")
    msg("zoom_in")
    desktop.wait_for(lambda: zoom()[:2] == (4000, 4000), "limited to the maximum")
    msg("zoom_out")
    desktop.wait_for(lambda: zoom()[:2] == (2000, 2000), "one step out")
    msg("zoom_reset")
    desktop.wait_for(lambda: zoom() == (1000, 1000, 0), "back to 1x")
    if grim:
        assert harness.grab(grim, env).at(*probe_at) == BACKGROUND

    # A zero duration steps at once; the wheel needs the modifier configured here.
    desktop.reload(settings(0, ", scroll_modifier = 'Super'"))
    msg("zoom_in")
    assert zoom()[:2] == (2000, 2000), zoom()
    msg("zoom_reset")
    desktop.wait_for(lambda: zoom() == (1000, 1000, 0), "reset")

    # Scrolling without the modifier leaves the zoom alone; with Super held it steps
    # (up zooms in, down zooms out).
    pointer("scroll", "-15")
    desktop.stays(lambda: zoom()[:2] == (1000, 1000), "the wheel zoomed without Super")
    pointer("key", "super", "down", "scroll", "-15")
    desktop.wait_for(lambda: zoom()[:2] == (2000, 2000), "wheel up zooms in")
    pointer("scroll", "15", "key", "super", "up")
    desktop.wait_for(lambda: zoom()[:2] == (1000, 1000), "wheel down zooms out")
print("Zoom eases, magnifies around the pointer, and resets"
      + ("" if grim else " (pixels not checked: grim missing)"))
