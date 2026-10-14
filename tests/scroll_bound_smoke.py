# SPDX-License-Identifier: GPL-3.0-or-later
"""A tile of the scrolling layout keeps to its monitor: its columns lie on a strip wider than the
monitor, but nothing of one is drawn on the monitor beside it. A column at rest wholly past the
edge hides (its content is not drawn) and the pointer passes it by there, reaching the other
monitor's window; one crossing the edge is cut there, square, where clips are drawn
(PAW_TEST_DROP_CLIP stands in for the GLES2 renderer), and is there to point at only on its own
monitor. Gliding past the edge, it is cut every frame and hides once it rests. Leaving the
scrolling layout, the tiles are bound by nothing again."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

# HEADLESS-1 scrolls, HEADLESS-2 lies right of it; columns are half of HEADLESS-1, 640 wide.
CONFIG = """return {
    xwayland = false,
    layout = { tiling = true, gap = 0, lone_margin = 0,
               outputs = { ["HEADLESS-1"] = { tile_layout = "scroll" } },
               scroll = { follow = "%s", width = 0.5 } },
    windows = { border_width = 2, corner_radius = 10 },
    outputs = { monitors = {
        ["HEADLESS-1"] = { mode = "1280x720", position = { x = 0, y = 0 } },
        ["HEADLESS-2"] = { mode = "1280x720", position = { x = 1280, y = 0 } },
    } },
    animations = { enabled = %s, move = { duration = 900 } },
}"""
EDGE = 1280  # HEADLESS-1's right edge, HEADLESS-2's left


def config(follow="edge", animated=False):
    return CONFIG % (follow, str(animated).lower())


class Bound:
    """A line of `get bounds`."""

    def __init__(self, fields):
        self.title, bound, drawn, clip, radius = fields
        self.bound = None if bound == "-" else tuple(int(v) for v in bound.split(","))
        self.drawn = drawn == "1"
        self.clip = None if clip == "-" else tuple(int(v) for v in clip.split(","))
        self.radius = None if radius == "-" else int(radius)

    def __repr__(self):
        return repr(self.__dict__)


def run(animated, check):
    with harness.Compositor(compositor, config(animated=animated),
                            env={"WLR_HEADLESS_OUTPUTS": "2", "PAW_TEST_DROP_CLIP": "1"}) as desktop:
        msg = desktop.msg

        def window(title):
            return next(w for w in desktop.windows() if w.title == title)

        def bounds():
            return {b.title: b for b in (Bound(r) for r in desktop.rows("bounds"))}

        def pid_at(x, y):
            return msg("get", "pid_at", str(x), str(y)).strip()

        desktop.detail = lambda: f"windows: {desktop.windows()}, bounds: {bounds()}"
        msg("headless_pointer", "add", "mouse")
        msg("headless_pointer", "move", "mouse", "1900", "300")
        pids = {"N": desktop.open_window(probe, "N", focused=True).pid}
        desktop.wait_for(lambda: window("N").output == "HEADLESS-2", "N on HEADLESS-2")
        msg("headless_pointer", "move", "mouse", "300", "300")
        for title in ("A", "B", "C"):
            pids[title] = desktop.open_window(probe, title, f"app-{title.lower()}",
                                              focused=True).pid
            desktop.wait_for(lambda: window(title).output == "HEADLESS-1", f"{title} scrolls")
        # The view shows B and C; A lies past the left edge, where no monitor is.
        desktop.wait_for(lambda: window("C").x + window("C").width < EDGE, "C in view")
        rounded = int(next(r for r in desktop.rows("frames") if r[1] == "A")[5]) > 0
        check(desktop, window, bounds, pid_at, {k: str(v) for k, v in pids.items()}, rounded)


def focus_a(desktop):
    subprocess.run([probe, "--activate", "app-a"], env=desktop.env, check=True, timeout=5,
                   stdout=subprocess.DEVNULL)


def at_rest(desktop, window, bounds, pid_at, pids, rounded):
    msg = desktop.msg
    desktop.wait_for(lambda: not bounds()["A"].drawn, "A hidden past the left edge")
    assert bounds()["A"].bound == (0, 0, 1280, 720), bounds()
    assert bounds()["N"].bound is None and bounds()["N"].drawn, bounds()

    # Edge: focusing A shows A and B, which puts C wholly on HEADLESS-2's side of the edge.
    focus_a(desktop)
    desktop.wait_for(lambda: window("A").focused and window("A").x < 640, "A in view")
    desktop.wait_for(lambda: window("C").x >= EDGE, "C past the edge")
    assert window("C").output == "HEADLESS-1", desktop.windows()
    desktop.wait_for(lambda: not bounds()["C"].drawn, "C hidden past the edge")
    assert bounds()["A"].drawn and bounds()["B"].drawn, bounds()
    # Where C lies, the pointer finds the other monitor's window.
    assert pid_at(window("C").x + 100, 300) == pids["N"], pid_at(window("C").x + 100, 300)
    assert pid_at(700, 300) == pids["B"]

    # Center: focusing B centers it, so that C crosses the edge and A the left one.
    desktop.reload(config("center"))
    msg("focus_right")
    desktop.wait_for(lambda: window("B").focused and 300 < window("B").x < 340, "B centered")
    c, a = window("C"), window("A")
    assert c.x < EDGE < c.x + c.width and a.x < 0 < a.x + a.width, desktop.windows()
    desktop.wait_for(lambda: bounds()["C"].drawn and bounds()["A"].drawn, "C and A shown")
    # C is there to point at on its own monitor, and past the edge the other window is.
    assert pid_at(EDGE - 20, 300) == pids["C"], pid_at(EDGE - 20, 300)
    assert pid_at(EDGE + 20, 300) == pids["N"], pid_at(EDGE + 20, 300)
    if rounded:
        # Cut at the edges, square; B, inside, keeps its rounded clip.
        b = window("B")
        assert bounds()["C"].clip == (c.x, c.y, EDGE - c.x, c.height), bounds()
        assert bounds()["C"].radius == 0, bounds()
        assert bounds()["A"].clip == (0, a.y, a.x + a.width, a.height), bounds()
        assert bounds()["A"].radius == 0, bounds()
        assert bounds()["B"].clip == (b.x, b.y, b.width, b.height), bounds()
        assert bounds()["B"].radius == 10, bounds()

    # Another layout binds nothing: every tile is drawn whole again.
    msg("layout_dwindle")
    desktop.wait_for(lambda: all(b.bound is None and b.drawn for b in bounds().values()),
                     "nothing bound")
    if rounded:
        for title in "ABC":
            w = window(title)
            assert bounds()[title].clip == (w.x, w.y, w.width, w.height), bounds()
            assert bounds()[title].radius == 10, bounds()


def gliding(desktop, window, bounds, pid_at, pids, rounded):
    # Focusing A glides A in from the left and C out over the right edge: drawn on its own
    # monitor only, all the way, and hidden once it rests past the edge.
    focus_a(desktop)
    samples = []

    def sample():
        samples.append(bounds())
        return not samples[-1]["C"].drawn

    desktop.wait_for(sample, "C hidden once it glided past the edge")
    if rounded:
        for drawn in samples:
            c = drawn["C"]
            if c.drawn and c.clip:
                assert c.clip[0] + c.clip[2] <= EDGE, samples
            a = drawn["A"]
            if a.drawn and a.clip:
                assert a.clip[0] >= 0, samples
        cut = [s["C"] for s in samples if s["C"].drawn and s["C"].clip and s["C"].radius == 0]
        assert cut, f"C was never seen cut at the edge: {samples}"
    assert window("C").x >= EDGE and window("C").output == "HEADLESS-1", desktop.windows()


run(False, at_rest)
run(True, gliding)
print("scroll tiles keep to their monitor")
