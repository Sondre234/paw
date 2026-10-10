# SPDX-License-Identifier: GPL-3.0-or-later
"""Peek fades every window toward the desktop while a key is held (or until toggled), and the
state follows the configuration; with grim the screen shows it."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])
grim = sys.argv[3] if len(sys.argv) > 3 else ""

BODY = (0x41, 0x7b, 0xc4)  # what the probe paints below its title bar
F9 = 67  # its evdev key code


def settings(opacity, duration, animations=True):
    return f"""return {{
    xwayland = false,
    layout = {{ tiling = true, gap = 4 }},
    appearance = {{ background = '#000000' }},
    peek = {{ opacity = {opacity}, duration = {duration} }},
    animations = {{ enabled = {str(animations).lower()}, duration = 10 }},
    bindings = {{ {{ mods = {{}}, key = 'F9', action = 'peek' }} }},
}}"""


with harness.Compositor(compositor, settings(0.25, 400)) as desktop:
    msg = desktop.msg

    def peek():
        """(progress in thousandths, peeking, held by a key)"""
        return tuple(int(n) for n in msg("get", "peek").splitlines()[0].split("\t"))

    def windows():
        return desktop.rows("windows")

    desktop.detail = lambda: f"peek: {peek()}, windows: {windows()}"

    def body_pixel():
        row = windows()[0]
        x, y, w, h = (int(n) for n in row[4:8])
        return harness.grab(grim, desktop.env).at(x + w // 2, y + h // 2)

    assert peek() == (0, 0, 0), peek()
    desktop.spawn([probe, "--external-control"])
    desktop.wait_for(lambda: len(windows()) == 1, "window")
    desktop.wait_for(lambda: msg("get", "animations").split("\t")[0].strip() == "0",
                     "opening animation over")
    if grim:
        assert body_pixel() == BODY, body_pixel()

    # Toggling from the control socket fades in over the duration, without jumping.
    msg("peek_toggle")
    desktop.wait_for(lambda: 0 < peek()[0] < 1000 and peek()[1] == 1, "fade in progress")
    desktop.wait_for(lambda: peek() == (1000, 1, 0), "peeking")
    if grim:
        # Black background: the body colour at 25%.
        got = body_pixel()
        assert all(abs(g - round(w * 0.25)) <= 3 for g, w in zip(got, BODY)), got
    msg("peek_toggle")
    desktop.wait_for(lambda: peek() == (0, 0, 0), "fade out")
    if grim:
        assert body_pixel() == BODY, body_pixel()

    # A held key: down peeks, up ends it.
    msg("headless_keyboard", "add", "keys")
    msg("headless_keyboard", "key", "keys", str(F9), "press")
    desktop.wait_for(lambda: peek()[1:] == (1, 1), "held key peeks")
    msg("headless_keyboard", "key", "keys", str(F9), "release")
    desktop.wait_for(lambda: peek() == (0, 0, 0), "released key ends it")

    # Immediate with animations off or a zero duration.
    for immediate in (settings(0.25, 400, animations=False), settings(0.25, 0)):
        desktop.reload(immediate)
        msg("peek_toggle")
        assert peek() == (1000, 1, 0), peek()
        msg("peek_toggle")
        assert peek() == (0, 0, 0), peek()
print("Peek fades windows out and back, held or toggled"
      + ("" if grim else " (pixels not checked: grim missing)"))
