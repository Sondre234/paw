# SPDX-License-Identifier: GPL-3.0-or-later
"""Windows without focus fade toward windows.dim_inactive and back; the state follows the
configuration and focus, and (with grim) the screen shows it."""
from pathlib import Path
import signal
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])
grim = sys.argv[3] if len(sys.argv) > 3 else ""

BODY = (0x41, 0x7b, 0xc4)  # what the probe paints below its title bar


def settings(dim, duration, animations=True):
    return f"""return {{
    xwayland = false,
    layout = {{ tiling = true, gap = 4 }},
    windows = {{ dim_inactive = {dim}, dim_duration = {duration} }},
    animations = {{ enabled = {str(animations).lower()}, duration = 10 }},
}}"""


with harness.Compositor(compositor, settings(0.4, 700)) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def dims():
        """(focused, current, target, has node) per window, most recently focused first."""
        return [tuple(int(n) for n in row) for row in desktop.rows("dim")]

    def windows():
        return desktop.rows("windows")

    desktop.detail = lambda: f"dim: {dims()}, windows: {windows()}"

    def settled():
        return msg("get", "animations").split("\t")[0].strip() == "0"

    def body_pixel(focused):
        """The screen colour in the middle of the focused (or unfocused) window's body."""
        for row in windows():
            if (row[1] == "1") == focused:
                x, y, w, h = (int(n) for n in row[4:8])
                return harness.grab(grim, desktop.env).at(x + w // 2, y + h // 2) if grim else None

    assert dims() == []

    # A lone window has focus: never dimmed, and it has no node to draw.
    desktop.spawn([probe, "--external-control"])
    wait_for(lambda: len(dims()) == 1, "first window")
    assert dims() == [(1, 0, 0, 0)], dims()

    # The second takes focus; the first fades to 0.4 without jumping there.
    second = desktop.spawn([probe, "--external-control"])
    wait_for(lambda: len(dims()) == 2 and dims()[0][0] == 1, "second window focused")
    wait_for(lambda: 0 < dims()[1][1] < 400, "dimming in progress")
    assert dims()[1][2] == 400 and dims()[1][3] == 1, dims()
    assert dims()[0] == (1, 0, 0, 0), dims()
    wait_for(lambda: dims()[1][1] == 400, "dimmed")
    wait_for(settled, "opening animations over")
    focused, dimmed = body_pixel(True), body_pixel(False)
    if grim:
        assert focused == BODY, focused
        # Black at 40% over the body colour.
        assert all(abs(got - round(want * 0.6)) <= 2 for got, want in zip(dimmed, BODY)), \
            (dimmed, [round(w * 0.6) for w in BODY])

    # Focus moves: the roles swap, again through the middle.
    # The new tile opened on the left: focus moves right, then back left.
    msg("focus_right")
    wait_for(lambda: dims()[0][0] == 1 and dims()[1][0] == 0, "focus moved")
    wait_for(lambda: 0 < dims()[1][1] < 400, "second window dimming")
    wait_for(lambda: dims()[0] == (1, 0, 0, 0) and dims()[1][1] == 400, "roles swapped")
    if grim:
        assert body_pixel(True) == BODY

    # Reloading with no dimming fades it out and removes the nodes.
    desktop.config.write_text(settings(0, 700))
    desktop.server.send_signal(signal.SIGHUP)
    wait_for(lambda: all(d[2] == 0 for d in dims()), "target cleared")
    wait_for(lambda: all(d[1:] == (0, 0, 0) for d in dims()), "faded out and removed")
    if grim:
        assert body_pixel(False) == BODY

    # With animations off the change is immediate.
    desktop.config.write_text(settings(0.5, 700, animations=False))
    desktop.server.send_signal(signal.SIGHUP)
    wait_for(lambda: sorted(d[1:] for d in dims()) == [(0, 0, 0), (500, 500, 1)],
             "immediate dimming")
    msg("focus_left")
    wait_for(lambda: dims()[0][0] == 1 and sorted(d[1:] for d in dims()) ==
             [(0, 0, 0), (500, 500, 1)], "immediate focus change")

    # A zero duration is immediate too, and windows leaving take their node along.
    desktop.config.write_text(settings(0.25, 0))
    desktop.server.send_signal(signal.SIGHUP)
    wait_for(lambda: sorted(d[1:] for d in dims()) == [(0, 0, 0), (250, 250, 1)],
             "zero duration")
    second.terminate()
    desktop.reap(second)
    wait_for(lambda: dims() == [(1, 0, 0, 0)], "the remaining window is not dimmed")
print("Dimming fades in and out, follows focus and configuration"
      + ("" if grim else " (pixels not checked: grim missing)"))
