# SPDX-License-Identifier: GPL-3.0-or-later
"""Touchpad swipes the compositor takes (`gestures`), from a headless pointer: three fingers
sideways move between workspaces, the windows held part of the way through their slide as the
fingers go and the step finishing past half way or with a flick, going back otherwise and at the
first and last workspace; three fingers up and down open and close the overview with them. Other
swipes reach the window under the pointer, one that waited for its direction from its beginning,
and none the compositor takes does. A swipe bound to a request runs it as the fingers lift; with
`invert` the directions swap; with gestures off every swipe is the window's."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false, workspaces = 3 },
    mouse = { focus_follows = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    %s
}"""

with harness.Compositor(compositor, CONFIG % "") as desktop:
    msg = desktop.msg

    def workspace():
        return int(msg("get", "workspace"))

    def gesture():
        """The swipe under way: (mode, fingers, direction, progress in thousandths), and with
        workspaces following it (output, from, target, shown, held windows, copies)."""
        rows = desktop.rows("gesture")
        swipe = (rows[0][0], int(rows[0][1]), rows[0][2], int(rows[0][3]))
        return swipe, tuple(rows[1][1:2] + [int(n) for n in rows[1][2:]]) if len(rows) > 1 else None

    def overview():
        state, progress = msg("get", "overview").splitlines()[0].split()
        return state, int(progress)

    def log(name):
        return (desktop.root / f"{name}.log").read_text().splitlines()

    def swipes(name):
        return [line for line in log(name) if line.startswith("swipe")]

    def point(x, y):
        """Moves the pointer, and waits until the window there hears of it (after the overview
        the pointer is on nothing until it moves)."""
        before = {name: len(log(name)) for name in ("one", "two")}
        msg("headless_pointer", "move", "touchpad", str(x), str(y))
        desktop.wait_for(lambda: any(len(log(name)) > before[name] for name in before),
                         f"the pointer at {x}, {y}")

    desktop.detail = lambda: f"gesture: {desktop.rows('gesture')}\n{log('one')}"
    # A window on each of the first two workspaces, the pointer over the first.
    desktop.spawn([probe, "One"], log="one.log")
    desktop.wait_for(lambda: "ready" in log("one"), "One mapped")
    msg("workspace", "2")
    desktop.spawn([probe, "Two"], log="two.log")
    desktop.wait_for(lambda: "ready" in log("two"), "Two mapped")
    msg("workspace", "1")
    msg("headless_pointer", "add", "touchpad")
    point(240, 190)
    assert any(line.startswith("pointer enter") for line in log("one")), log("one")
    clock = [10000]

    def swipe(step, *words, after=10):
        """A swipe event `after` milliseconds after the last."""
        clock[0] += after
        msg("headless_pointer", "swipe", "touchpad", step, *words, str(clock[0]))

    # Below the threshold the swipe has no direction yet, and nothing moves.
    swipe("begin", "3")
    swipe("update", "-6", "1")
    assert gesture() == (("waiting", 3, "none", 0), None), gesture()
    # Past it, the workspaces follow the fingers: One held a fifth of the way out, Two coming in.
    swipe("update", "-54", "-1")
    assert gesture() == (("workspace", 3, "left", 200), ("HEADLESS-1", 1, 2, 200, 1, 1)), gesture()
    swipe("update", "-60", "0", after=100)
    assert gesture()[1] == ("HEADLESS-1", 1, 2, 400, 1, 1), gesture()
    # Back past where they began, toward the previous workspace, which there is none of: no
    # copies, and the windows give a little.
    swipe("update", "150", "0", after=100)
    assert gesture() == (("workspace", 3, "left", -100), ("HEADLESS-1", 1, 0, 0, 1, 0)), gesture()
    # Lifted past half way toward the next, slowly: workspace 2, its window focused.
    swipe("update", "-210", "0", after=200)
    assert gesture()[1] == ("HEADLESS-1", 1, 2, 600, 1, 1), gesture()
    swipe("end", after=200)
    assert workspace() == 2 and gesture() == (("none", 0, "none", 0), None), gesture()
    desktop.wait_for(lambda: msg("get", "animations").split("\t")[0] == "0", "the slide done")
    assert [w.title for w in desktop.windows() if w.focused] == ["Two"], desktop.windows()
    # Not half way, slowly: back to workspace 2.
    swipe("begin", "3", after=1000)
    swipe("update", "40", "0")
    swipe("update", "60", "0", after=300)
    swipe("end", after=300)
    assert workspace() == 2
    # A flick: a short, fast swipe goes on to workspace 1.
    swipe("begin", "3", after=1000)
    swipe("update", "30", "0")
    swipe("update", "30", "0")
    swipe("end", after=5)
    assert workspace() == 1
    # Past half way but flicked back: it stays.
    swipe("begin", "3", after=1000)
    swipe("update", "-200", "0", after=200)
    swipe("update", "40", "0", after=20)
    swipe("end", after=5)
    assert workspace() == 1
    # Given up by libinput, it goes back.
    swipe("begin", "3", after=1000)
    swipe("update", "-250", "0", after=200)
    swipe("cancel", after=200)
    assert workspace() == 1
    # At the last workspace there is no next one: the windows give and come back.
    msg("workspace", "3")
    swipe("begin", "3", after=1000)
    swipe("update", "-290", "0", after=200)
    assert gesture()[1] == ("HEADLESS-1", 3, 0, 0, 0, 0), gesture()
    swipe("end", after=200)
    assert workspace() == 3
    msg("workspace", "1")
    desktop.wait_for(lambda: msg("get", "animations").split("\t")[0] == "0", "the slide done")
    assert swipes("one") == [], swipes("one")

    # Three fingers up open the overview with them, and down close it.
    swipe("begin", "3", after=1000)
    swipe("update", "0", "-20")
    assert gesture()[0][0] == "overview" and overview()[0] == "open", (gesture(), overview())
    swipe("update", "0", "-100", after=100)
    assert overview() == ("open", 400), overview()
    swipe("update", "0", "-60", after=100)
    swipe("end", after=200)
    desktop.wait_for(lambda: overview() == ("open", 1000), "the overview open")
    swipe("begin", "3", after=1000)
    swipe("update", "0", "60")
    assert overview() == ("open", 800), overview()
    swipe("update", "0", "30", after=100)  # under half way: it opens again
    swipe("end", after=200)
    desktop.wait_for(lambda: overview() == ("open", 1000), "the overview open again")
    # Sideways while it is open, the overview shows the next workspace.
    swipe("begin", "3", after=1000)
    swipe("update", "-200", "0", after=100)
    swipe("end", after=200)
    assert msg("get", "overview").splitlines()[1].split()[4] == "2", msg("get", "overview")
    assert workspace() == 1
    swipe("begin", "3", after=1000)
    swipe("update", "0", "200", after=100)
    swipe("end", after=200)
    desktop.wait_for(lambda: overview()[0] == "closed", "the overview closed")
    # Up halfway and back down: it never opens.
    swipe("begin", "3", after=1000)
    swipe("update", "0", "-100", after=100)
    swipe("update", "0", "80", after=200)
    swipe("end", after=200)
    desktop.wait_for(lambda: overview()[0] == "closed", "the overview closed again")
    assert swipes("one") == [], swipes("one")

    # Four fingers are the window's; so is a swipe of three fingers that is too short to have a
    # direction, whole.
    point(250, 200)
    swipe("begin", "4", after=1000)
    swipe("update", "-30", "0")
    swipe("end")
    desktop.wait_for(lambda: swipes("one") == ["swipe begin 4", "swipe update -30.0 0.0",
                                               "swipe end 0"], "four fingers passed on")
    swipe("begin", "3", after=1000)
    swipe("update", "5", "5")
    swipe("end")
    desktop.wait_for(lambda: swipes("one")[3:] == ["swipe begin 3", "swipe update 5.0 5.0",
                                                   "swipe end 0"], "a short swipe passed on")
    assert workspace() == 1 and overview()[0] == "closed"

    # Swipes of their own: a direction no swipe has reaches the window from its beginning, the
    # movement it waited through at once; a request runs as the fingers lift past half way.
    desktop.reload(CONFIG % """gestures = { distance = 200, swipes = {
        { fingers = 3, direction = "left", action = "workspace 3" },
        { fingers = 4, direction = "down", action = "move_to_workspace 2" } } },""")
    before = len(swipes("one"))
    swipe("begin", "3", after=1000)
    swipe("update", "0", "-10")
    swipe("update", "0", "-10")
    swipe("update", "0", "-4")
    swipe("end")
    desktop.wait_for(lambda: swipes("one")[before:] == ["swipe begin 3", "swipe update 0.0 -20.0",
                                                        "swipe update 0.0 -4.0", "swipe end 0"],
                     "an unbound direction passed on")
    swipe("begin", "3", after=1000)
    swipe("update", "-60", "0", after=100)
    swipe("end", after=200)
    assert workspace() == 1  # not half way
    swipe("begin", "3", after=1000)
    swipe("update", "-120", "0", after=100)
    swipe("end", after=200)
    assert workspace() == 3
    msg("workspace", "1")
    assert [w.title for w in desktop.windows() if w.focused] == ["One"], desktop.windows()
    swipe("begin", "4", after=1000)
    swipe("update", "0", "150", after=100)
    swipe("end", after=200)
    assert {w.title: w.workspace for w in desktop.windows()} == {"One": 2, "Two": 2}, \
        desktop.windows()

    # Inverted, fingers going right count as a swipe left.
    desktop.reload(CONFIG % "gestures = { invert = true },")
    swipe("begin", "3", after=1000)
    swipe("update", "250", "0", after=200)
    assert gesture()[0] == ("workspace", 3, "left", 1000 * 250 // 300), gesture()
    swipe("end", after=200)
    assert workspace() == 2

    # Off, every swipe is the window's, under the pointer.
    desktop.reload(CONFIG % "gestures = { enabled = false },")
    point(240, 190)
    before = {name: len(swipes(name)) for name in ("one", "two")}
    swipe("begin", "3", after=1000)
    swipe("update", "-250", "0", after=200)
    swipe("end", after=200)
    assert workspace() == 2 and gesture()[0][0] == "none"
    desktop.wait_for(lambda: sum(len(swipes(name)) - before[name] for name in before) == 3,
                     "the swipe passed on with gestures off")
