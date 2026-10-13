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
# The spring back takes ten seconds, so that it is still under way as the test looks; on a busy
# machine a late tick lands nothing.
SLOW = "speed = 0.1, late_frame_ms = 1000, move = { duration = 1000 }"
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

    def drawn(title="One"):
        """How far the window is drawn from where it rests (x, y) and its opacity's factor."""
        for row in desktop.rows("drawn"):
            if row[0] == title:
                return tuple(int(n) for n in row[1:])
        raise AssertionError(f"no window {title}")

    def held():
        """The band the fingers hold, checked against where the window is drawn."""
        now = band()
        assert drawn() == (now[3], 0, now[4]), (now, drawn())
        return now

    def output_frames():
        """The frames each monitor has drawn, by name."""
        return {name: int(frames) for name, frames in desktop.rows("output_frames")}

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
    assert held() == (0, 0, 1, give(20 / 300), 1000), band()
    pulls = (60, 150, 300, 600, 1200)
    gave = []
    for units in pulls:
        to(units)
        gave.append(band()[3])
        assert held() == (0, 0, 1, give(units / 300), 1000), (units, band())
    gains = [(b - a) / (v - u) for a, b, u, v in zip(gave, gave[1:], pulls, pulls[1:])]
    assert all(b < a for a, b in zip(gains, gains[1:])) and gave[-1] < SLIDE, gave
    # Back toward where it began, it comes back the same way, and on past it toward the next
    # workspace the slide begins: nothing jumps on the way.
    to(150)
    assert held()[3] == gave[1], band()
    to(0)
    assert held() == (0, 0, 1, 0, 1000), band()
    to(-60)
    assert band() == (2, 200, 1, 0, 1000), band()
    # Past the end again and let go: it stays on the first workspace, and the window springs
    # back, slowly here.
    to(300, after=100)
    assert held() == (0, 0, 1, give(1), 1000), band()
    swipe("end", after=300)
    assert workspace() == 1 and desktop.rows("gesture")[0][0] == "none"
    assert running() == 1, msg("get", "animations")
    frames = output_frames()["HEADLESS-1"]
    desktop.wait_for(lambda: drawn()[0] <= give(1) - 3, "springing back")
    assert output_frames()["HEADLESS-1"] > frames, "the spring drew nothing"
    # Fingers coming back take it over where it is drawn, and move it on from there.
    where = drawn()[0]
    begin()
    to(20)
    caught = held()
    assert caught[:3] == (0, 0, 1) and where - 2 <= caught[3] <= where, (where, caught)
    assert running() == 0, "held, not springing"
    to(50)
    assert caught[3] < held()[3] <= caught[3] + 4, (caught, band())
    # Back the other way it comes back to rest before the slide toward the next workspace
    # begins, as far on as the fingers went past that: a whole step back from where they took
    # it over (as far past the end as its offset then says) and the 20 units they pulled first.
    to(-300)
    target, shown, holds, offset, alpha = band()
    pulled = caught[3] / (SLIDE - caught[3])
    assert (target, holds, offset, alpha) == (2, 1, 0, 1000), band()
    assert abs(shown - 1000 * (1 + 20 / 300 - pulled)) <= 25, (caught, band())
    swipe("end", after=300)
    assert workspace() == 1
    desktop.wait_for(lambda: running() == 0, "the slide back done", timeout=30)

    # Past the last workspace the other way, the window gives to the left.
    msg("move_to_workspace", "2")
    msg("workspace", "2")
    begin()
    to(-150)
    assert held() == (0, 0, 1, give(-0.5), 1000), band()
    swipe("end", after=300)
    assert workspace() == 2

    # Animations off: the window still follows the fingers, and is back at once as they lift.
    desktop.reload(CONFIG % "enabled = false")
    assert running() == 0, msg("get", "animations")
    begin()
    to(-300)
    assert held() == (0, 0, 1, give(-1), 1000), band()
    swipe("end", after=300)
    assert workspace() == 2 and running() == 0 and drawn() == (0, 0, 1000), drawn()

    # A slide that only fades: the window dims instead, and brightens again as it is let go;
    # fingers coming back meanwhile take it over as dim as it is.
    desktop.reload(CONFIG % (SLOW + ", workspace = { distance = 0 }"))
    begin()
    to(-300)
    dim = round(1000 * (1 - 0.2 / 1.2))
    assert held() == (0, 0, 1, 0, dim), band()
    swipe("end", after=300)
    assert workspace() == 2 and running() == 1, msg("get", "animations")
    desktop.wait_for(lambda: drawn()[2] >= dim + 5, "brightening")
    where = drawn()[2]
    begin()
    to(-20)
    caught = held()
    assert caught[:4] == (0, 0, 1, 0) and where <= caught[4] <= where + 3, (where, caught)
    to(-60)
    assert held()[4] < caught[4], band()  # dimmer again as the fingers pull on
    swipe("end", after=300)
    desktop.reload(CONFIG % "enabled = false")  # the brightening ends at once
    assert running() == 0 and drawn() == (0, 0, 1000), drawn()

    # A step with nowhere to go, as the panel's wheel asks for past either end, nudges the windows
    # of that output the way the slide would take them, and back; with a slide that only fades,
    # nothing moves.
    msg("workspace_nudge", "HEADLESS-1", "next")
    assert running() == 0 and drawn() == (0, 0, 1000), drawn()
    desktop.reload(CONFIG % SLOW)
    frames = output_frames()["HEADLESS-1"]
    msg("workspace_nudge", "HEADLESS-1", "next")
    assert running() == 1, msg("get", "animations")
    # Out to the left as far as fingers a third of a step past the end would pull it (25.6
    # pixels), and back to rest, drawn all the way.
    seen = []

    def over():
        seen.append(drawn()[0])
        return running() == 0

    desktop.wait_for(over, "the nudge over")
    assert drawn() == (0, 0, 1000) and max(seen) <= 0 and -26 <= min(seen) <= -23, seen
    assert output_frames()["HEADLESS-1"] >= frames + 20, (frames, output_frames())
    # Nudged the other way on its way out, it turns there from where it is.
    msg("workspace_nudge", "HEADLESS-1", "next")
    desktop.wait_for(lambda: drawn()[0] <= -10, "nudged left")
    msg("workspace_nudge", "HEADLESS-1", "prev")
    desktop.wait_for(lambda: drawn()[0] >= 10, "turned to the right")
    desktop.wait_for(lambda: running() == 0, "the nudges over")
    # A window opening during a nudge opens as it would, not landed at once as late.
    msg("workspace_nudge", "HEADLESS-1", "next")
    desktop.wait_for(lambda: drawn()[0] <= -20, "nudged again")
    desktop.open_window(probe, "Two")
    assert running() == 2 and drawn("Two")[2] < 1000, (msg("get", "animations"), drawn("Two"))
    assert "usage" in msg("workspace_nudge", "HEADLESS-1", ok=False)
    assert "usage" in msg("workspace_nudge", "HEADLESS-1", "up", ok=False)
    assert "usage" in msg("workspace_nudge", "HEADLESS-1", "next", "now", ok=False)
    assert "no such output" in msg("workspace_nudge", "HDMI-9", "next", ok=False)
    desktop.reload(CONFIG % "enabled = false")
    msg("workspace_nudge", "HEADLESS-1", "prev")
    assert running() == 0, msg("get", "animations")
    # workspace_next and workspace_prev wrap around, so only a single workspace nudges.
    desktop.reload((CONFIG % SLOW).replace("workspaces = 2", "workspaces = 1"))
    assert workspace() == 1
    msg("workspace_next")
    assert workspace() == 1 and running() == 2, msg("get", "animations")
    desktop.wait_for(lambda: drawn()[0] < 0 and drawn("Two")[0] < 0, "both nudged left")

    # Frames come only where something changes: the nudge brings this monitor's, and another
    # monitor draws what the pointer passing over it changed and nothing more meanwhile.
    desktop.reload(CONFIG % SLOW)
    msg("headless_output", "add", "eDP-1", "800x600")
    desktop.wait_for(lambda: "eDP-1" in output_frames(), "a second monitor")

    msg("workspace_nudge", "HEADLESS-1", "next")
    here, there = output_frames()["HEADLESS-1"], output_frames()["eDP-1"]
    msg("headless_pointer", "move", "touchpad", str(1280 + 400), "300")
    desktop.wait_for(lambda: output_frames()["eDP-1"] > there, "the pointer drawn on eDP-1")
    there = output_frames()["eDP-1"]
    desktop.wait_for(lambda: output_frames()["HEADLESS-1"] >= here + 10, "the nudge drawn")
    assert running() == 2, msg("get", "animations")
    desktop.stays(lambda: output_frames()["eDP-1"] <= there + 2, "eDP-1 drawing nothing new",
                  duration=0.5)
