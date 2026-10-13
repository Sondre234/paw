# SPDX-License-Identifier: GPL-3.0-or-later
"""The rubber band past the first and last workspace: fingers swiped toward a workspace that is
not there move the windows with them against a growing resistance, back and forth without a
jump, on into a real slide the other way; let go, the windows spring back with the move
animation, and fingers that come back meanwhile take them over where they are drawn. With a
slide that only fades they dim instead, and with animations off they come back at once."""
import math
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false, workspaces = 2 },
    mouse = { focus_follows = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    animations = { %s },
}"""
# The spring back takes ten seconds, so that it is still under way as the test looks.
SLOW = "speed = 0.1, move = { duration = 1000 }"
SLIDE = 1280 * 0.08  # a step of the workspace slide, which is also as far as the windows give


def give(pull):
    """How far the windows give with the fingers `pull` steps past the end, in whole pixels as
    the compositor rounds them (half away from 0)."""
    exact = SLIDE * abs(pull) / (abs(pull) + 1)
    return int(math.copysign(math.floor(exact + 0.5), pull))


with harness.Compositor(compositor, CONFIG % SLOW) as desktop:
    msg = desktop.msg

    def workspace():
        return int(msg("get", "workspace"))

    def band():
        """The slide the fingers hold: (target, shown, held windows, how far the windows give
        past the end, their opacity)."""
        rows = desktop.rows("gesture")
        assert len(rows) == 2, rows
        return tuple(int(rows[1][i]) for i in (3, 4, 5, 7, 8))

    def running():
        return int(msg("get", "animations").split("\t")[0])

    desktop.detail = lambda: f"gesture: {desktop.rows('gesture')}"
    desktop.open_window(probe, "One", focused=True)
    msg("headless_pointer", "add", "touchpad")
    msg("headless_pointer", "move", "touchpad", "400", "300")
    clock = [10000]
    travel = [0]  # how far right the fingers are from where the swipe began

    def swipe(step, *words, after=10):
        clock[0] += after
        msg("headless_pointer", "swipe", "touchpad", step, *words, str(clock[0]))

    def to(units, after=10):
        """Moves the fingers to `units` right of where the swipe began."""
        swipe("update", str(units - travel[0]), "0", after=after)
        travel[0] = units

    def begin():
        swipe("begin", "3", after=1000)
        travel[0] = 0

    # Right on the first workspace heads for one before it, which is not there: the window moves
    # right with the fingers, at first as far as the slide would, then less and less.
    begin()
    to(20)
    assert band() == (0, 0, 1, give(20 / 300), 1000), band()
    pulls = (60, 150, 300, 600, 1200)
    gave = []
    for units in pulls:
        to(units)
        gave.append(band()[3])
        assert band() == (0, 0, 1, give(units / 300), 1000), (units, band())
    gains = [(b - a) / (v - u) for a, b, u, v in zip(gave, gave[1:], pulls, pulls[1:])]
    assert all(b < a for a, b in zip(gains, gains[1:])) and gave[-1] < SLIDE, gave
    # Back toward where it began, it comes back the same way, and on past it toward the next
    # workspace the slide begins: nothing jumps on the way.
    to(150)
    assert band()[3] == gave[1], band()
    to(0)
    assert band() == (0, 0, 1, 0, 1000), band()
    to(-60)
    assert band() == (2, 200, 1, 0, 1000), band()
    # Past the end again and let go: it stays on the first workspace, and the window springs
    # back, slowly here.
    to(300, after=100)
    assert band() == (0, 0, 1, give(1), 1000), band()
    swipe("end", after=300)
    assert workspace() == 1 and desktop.rows("gesture")[0][0] == "none"
    assert running() == 1, msg("get", "animations")
    # Fingers coming back take it over where it is drawn, nearly as far as it was let go, and
    # move it on from there.
    begin()
    to(20)
    caught = band()
    assert caught[:3] == (0, 0, 1) and give(1) - 4 <= caught[3] <= give(1), caught
    assert running() == 0, "held, not springing"
    to(50)
    assert caught[3] < band()[3] <= caught[3] + 4, (caught, band())
    # Back the other way it comes back to rest before the slide toward the next workspace
    # begins, as far on as the fingers went past that.
    to(-300)
    target, shown, held, offset, alpha = band()
    assert (target, held, offset, alpha) == (2, 1, 0, 1000) and 0 < shown < 200, band()
    swipe("end", after=300)
    assert workspace() == 1
    desktop.wait_for(lambda: running() == 0, "the slide back done", timeout=30)

    # Past the last workspace the other way, the window gives to the left.
    msg("move_to_workspace", "2")
    msg("workspace", "2")
    begin()
    to(-150)
    assert band() == (0, 0, 1, give(-0.5), 1000), band()
    swipe("end", after=300)
    assert workspace() == 2

    # Animations off: the window still follows the fingers, and is back at once as they lift.
    desktop.reload(CONFIG % "enabled = false")
    assert running() == 0, msg("get", "animations")
    begin()
    to(-300)
    assert band() == (0, 0, 1, give(-1), 1000), band()
    swipe("end", after=300)
    assert workspace() == 2 and running() == 0, msg("get", "animations")

    # A slide that only fades: the window dims instead, and brightens again as it is let go.
    desktop.reload(CONFIG % (SLOW + ", workspace = { distance = 0 }"))
    begin()
    to(-300)
    assert band() == (0, 0, 1, 0, round(1000 * (1 - 0.2 / 1.2))), band()
    swipe("end", after=300)
    assert workspace() == 2 and running() == 1, msg("get", "animations")
