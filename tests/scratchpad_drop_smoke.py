# SPDX-License-Identifier: GPL-3.0-or-later
"""The scratchpad's drop-down: a window shown slides in from the edge of the usable area along
the bar (up from a bottom bar, down from under a top bar, down from the top without one), fading
in, while focus, the pointer and `get windows` have it in its place at once; hidden, it slides
back out that way, passed over by the pointer, and hides as it ends. Shown and hidden again part
way, it turns back from where it is drawn. Where clips are drawn (PAW_TEST_DROP_CLIP stands in for
the GLES2 renderer) it is cut inside the bar, as deep as its corners reach, and square at its own
monitor's edge when another monitor lies beyond (where it stops without clips is dropdown_tests').
With animations off it shows and hides at once."""
from pathlib import Path
import subprocess
import sys
import time

import harness

compositor, probe, window_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

# The drop in takes twice `open`, the slide out one and a half times `close`: 1.2 and 0.9 s, slow
# enough to look at part way, under ASan too.
CONFIG = """return {
    xwayland = false,
    layout = { tiling = true },
    windows = { border_width = 2, corner_radius = 10, round = "always" },
    outputs = { monitors = {
        ["HEADLESS-1"] = { mode = "1280x720", position = { x = 0, y = 720 } },
        ["HEADLESS-2"] = { mode = "1280x720", position = { x = 0, y = 0 } },
    } },
    animations = { enabled = %s, speed = %s, open = { duration = 600 },
                   close = { duration = 600 } },
    features = { scratchpad = %s },
}"""


def config(enabled=True, speed=1, scratchpad=True):
    return CONFIG % (str(enabled).lower(), speed, str(scratchpad).lower())
BORDER = 2


class Drop:
    """A line of `get scratchpad`."""

    def __init__(self, fields):
        self.title, hidden, drawn, self.drop, self.edge = fields[:5]
        self.hidden, self.drawn = hidden == "1", drawn == "1"
        self.dx, self.dy, self.alpha = (int(v) for v in fields[5:8])
        self.cut = None if fields[8] == "-" else int(fields[8])
        self.box, self.ring = (None if f == "-" else tuple(int(v) for v in f.split(","))
                               for f in (fields[9], fields[11]))
        self.radius = None if fields[10] == "-" else int(fields[10])

    def __repr__(self):
        return repr(self.__dict__)


def run(outputs, env, check):
    """`check(desktop, top)` on a compositor with `outputs` headless outputs, the pointer on
    HEADLESS-1, whose top edge is at `top`: 720 under HEADLESS-2, where there is one."""
    with harness.Compositor(compositor, config(),
                            env={"WLR_HEADLESS_OUTPUTS": str(outputs), **env}) as desktop:
        top = int(next(r for r in desktop.rows("outputs") if r[0] == "HEADLESS-1")[3])
        desktop.msg("headless_pointer", "add", "mouse")
        desktop.msg("headless_pointer", "move", "mouse", "100", str(top + 100))
        check(desktop, top)


def tools(desktop):
    msg = desktop.msg

    def drop(title="S"):
        return next(Drop(r) for r in desktop.rows("scratchpad") if r[0] == title)

    def window(title="S"):
        return next(w for w in desktop.windows() if w.title == title)

    def pid_at(x, y):
        return msg("get", "pid_at", str(x), str(y)).strip()

    def open_scratch(panel=None):
        """U, with a panel along the bottom or the `panel` edge, and S, a tile of HEADLESS-1.
        Returns their processes."""
        env = {"PAW_PROBE_PANEL": panel} if panel else {}
        under = desktop.open_window(probe, "U", args=("--external-control",), env=env)
        shown = desktop.open_window(probe, "S", focused=True)
        desktop.wait_for(lambda: window().tiled and window().output == "HEADLESS-1", "S tiled")
        return under, shown

    def settled():
        return drop().drop == "-"

    desktop.detail = lambda: f"windows: {desktop.windows()}, scratchpad: " \
                             f"{[Drop(r) for r in desktop.rows('scratchpad')]}"
    return msg, drop, window, pid_at, open_scratch, settled


def corner(desktop):
    """How far in the corners of S reach: its radius and border, 0 where wlroots lacks rounded
    clips and windows stay square."""
    radius = int(next(r for r in desktop.rows("frames") if r[1] == "S")[5])
    return radius + BORDER if radius else 0


def bottom_bar(desktop, top):
    msg, drop, window, pid_at, open_scratch, settled = tools(desktop)
    under, shown = open_scratch()
    rounded = corner(desktop)

    # A floating place of its own, away from its tile.
    msg("toggle_floating")
    desktop.wait_for(lambda: not window().tiled, "S floating")
    for _ in range(3):
        msg("move_right")
    msg("toggle_floating")
    desktop.wait_for(lambda: window().tiled and
                     msg("get", "animations").split("\t")[0] == "0", "S tiled again")
    tile = window()

    # Hidden: minimized and the focus gone at once, but drawn while it slides down out of sight,
    # straight from its tile rather than its floating place, and the pointer passes over it to U.
    msg("move_to_scratchpad")
    s, d = window(), drop()
    assert s.minimized and not s.focused and window("U").focused, desktop.windows()
    assert d.drop == "out" and d.edge == "bottom" and d.hidden and d.drawn and d.cut is None, d
    assert s.x != tile.x and s.x + d.dx == tile.x and 0 <= s.y + d.dy - tile.y < 60, (s, d, tile)
    assert pid_at(s.x + s.width // 2, s.y + s.height // 2) == str(under.pid)
    assert pid_at(tile.x + 10, tile.y + 10) == str(under.pid)
    desktop.wait_for(lambda: drop().dy > d.dy + 30 and drop().dx == d.dx, "sliding down")
    desktop.wait_for(lambda: settled() and not drop().drawn and drop().dy == 0,
                     "hidden once out of sight")

    # Shown: in its place, focused and under the pointer at once, drawn below the bar's top
    # edge (cut there by nothing: pixman draws no clips), faded out, coming up.
    msg("scratchpad_show")
    s, d = window(), drop()
    assert s.focused and not s.minimized and s.visible, s
    cut = top + 720 - 48 + min(48, rounded)
    assert d.drop == "in" and d.edge == "bottom" and d.cut is None and d.dx == 0, d
    assert cut - (s.y - BORDER) - 200 < d.dy <= cut - (s.y - BORDER), (d, s, cut)
    assert d.alpha < 500, d
    assert pid_at(s.x + s.width // 2, s.y + s.height // 2) == str(shown.pid)
    assert int(msg("get", "animations").split("\t")[0]) >= 1
    # Up, fast first, fading in over the first part; at rest it is drawn where it is.
    seen = [drop()]
    while seen[-1].drop == "in":
        seen.append(drop())
    assert all(a.dy >= b.dy for a, b in zip(seen, seen[1:])), [d.dy for d in seen]
    assert all(a.alpha <= b.alpha for a, b in zip(seen, seen[1:])), [d.alpha for d in seen]
    assert seen[-1].dy == 0 and seen[-1].alpha == 1000 and seen[-1].drawn, seen[-1]
    assert window() == s, (window(), s)

    # Hidden again half way in, it turns back from where it is drawn; shown again half way out,
    # it turns back again.
    msg("scratchpad_show")
    desktop.wait_for(lambda: settled() and not drop().drawn, "hidden")
    msg("scratchpad_show")
    start = drop().dy

    def turn():
        """Shows or hides S again: how it is drawn just before and just after. It moves on
        from where it was, as far as the slides go in the time between the two looks (the
        quickest, sliding out, ends at three times its mean speed) and never back at rest."""
        began = time.monotonic()
        before = drop()
        msg("scratchpad_show")
        after = drop()
        reach = 3 * start / 900 * (time.monotonic() - began) * 1000 + 20
        assert abs(after.dy - before.dy) <= reach and after.dy > before.dy / 2, \
            (before, after, reach)
        return before, after

    desktop.wait_for(lambda: drop().dy < start / 2, "half way in")
    before, after = turn()
    assert before.drop == "in" and after.drop == "out", (before, after)
    desktop.wait_for(lambda: drop().dy > (before.dy + start) / 2, "on its way out again")
    before, after = turn()
    assert before.drop == "out" and after.drop == "in", (before, after)
    desktop.wait_for(lambda: settled() and drop().dy == 0 and drop().drawn, "in place")
    assert window().focused

    # Peeked at from the taskbar's picture of it as it slides out (slowly, 3 s, so as not to
    # end meanwhile), and activated there as a click on the picture does: it turns back and comes
    # in, and the pointer finds it in its place.
    desktop.reload(config(speed=0.3))
    msg("scratchpad_show")
    desktop.wait_for(lambda: drop().drop == "out" and drop().dy > 5, "sliding out")
    peeker = desktop.spawn([window_probe, "S", "peek"], stdin=subprocess.PIPE,
                           stdout=subprocess.PIPE, text=True)
    assert peeker.stdout.readline().strip() == "ok", "the probe did not peek"
    assert drop().drop == "out", drop()
    subprocess.run([window_probe, "S", "restore"], env=desktop.env, check=True, timeout=30,
                   stdout=subprocess.DEVNULL)
    s, d = window(), drop()
    assert s.focused and not s.minimized and d.drop == "in" and d.drawn, (s, d)
    assert pid_at(s.x + s.width // 2, s.y + s.height // 2) == str(shown.pid)
    peeker.stdin.close()
    assert desktop.reap(peeker) == 0
    desktop.reload(config())
    desktop.wait_for(lambda: settled() and drop().dy == 0 and drop().alpha == 1000, "in place")

    # Animations off: shown and hidden at once.
    desktop.reload(config(enabled=False))
    msg("scratchpad_show")
    assert settled() and not drop().drawn and window().minimized, drop()
    msg("scratchpad_show")
    assert settled() and drop().drawn and drop().dy == 0 and window().focused, drop()
    assert msg("get", "animations").split("\t")[0] == "0"

    # The scratchpad turned off as S slides out: it is back in its place at once, out of the
    # scratchpad, there for the pointer.
    desktop.reload(config(speed=0.3))
    msg("scratchpad_show")
    desktop.wait_for(lambda: drop().drop == "out" and drop().dy > 5, "sliding out")
    desktop.reload(config(speed=0.3, scratchpad=False))
    s = window()
    assert not s.scratchpad and not s.minimized and s.visible, s
    assert not [r for r in desktop.rows("scratchpad") if r[0] == "S"], desktop.rows("scratchpad")
    assert pid_at(s.x + s.width // 2, s.y + s.height // 2) == str(shown.pid)


def top_bar(desktop, top):
    """With clips, under a bar along the top."""
    msg, drop, window, pid_at, open_scratch, settled = tools(desktop)
    open_scratch(panel="top")
    rounded = corner(desktop)
    msg("move_to_scratchpad")
    desktop.wait_for(lambda: settled() and not drop().drawn, "hidden")
    msg("scratchpad_show")
    s, d = window(), drop()
    assert d.drop == "in" and d.edge == "top" and d.dx == 0, d
    if not rounded:
        # This wlroots has no rounded clips: nothing is cut.
        assert d.cut is None, d
        desktop.wait_for(settled, "shown")
        return
    cut = top + 48 - rounded
    assert d.cut == cut and d.dy >= cut - (s.y + s.height + BORDER), (d, s, cut)
    def follow(phase):
        """Samples the drop until it ends: the content and the border's rounded rect are cut
        along the line while they cross it, and not where they are past it. Returns the last
        sample."""
        crossing = None
        while (d := drop()).drop == phase:
            drawn = s.y + d.dy
            if d.box[3] > 1 and drawn < cut:
                crossing = d
                assert d.box[:3] == (s.x, cut, s.width) and d.radius == rounded - BORDER, (d, s)
                assert d.box[1] + d.box[3] == drawn + s.height, (d, s)
            elif drawn >= cut:
                assert d.box == (s.x, drawn, s.width, s.height), (d, s)
            if d.ring[3] > 0 and drawn - BORDER < cut:
                assert d.ring[1] == cut and d.ring[1] + d.ring[3] == drawn + s.height + BORDER, d
            elif drawn - BORDER >= cut:
                assert d.ring == (s.x - BORDER, drawn - BORDER) + frame, (d, s)
        assert crossing, f"seen crossing the cut on the way {phase}"
        return d

    # At rest: clipped to its rounded corners alone, its border whole.
    frame = (s.width + 2 * BORDER, s.height + 2 * BORDER)
    whole = ((s.x, s.y, s.width, s.height), rounded - BORDER, (s.x - BORDER, s.y - BORDER) + frame)
    d = follow("in")
    assert d.cut is None and d.dy == 0 and d.drawn and (d.box, d.radius, d.ring) == whole, d
    # Out the same way, cut the same way, and hidden at the end, its clips whole again.
    msg("scratchpad_show")
    assert drop().drop == "out" and drop().cut == cut, drop()
    d = follow("out")
    assert not d.drawn and (d.box, d.radius, d.ring) == whole, d


def monitor_above(desktop, top):
    """No bar and HEADLESS-2 above: cut at the monitor's top, or, where this wlroots has no
    rounded clips, stopping there."""
    msg, drop, window, pid_at, open_scratch, settled = tools(desktop)
    desktop.open_window(probe, "S", focused=True)
    desktop.wait_for(lambda: window().output == "HEADLESS-1", "S on HEADLESS-1")
    msg("move_to_scratchpad")
    desktop.wait_for(lambda: settled() and not drop().drawn, "hidden")
    msg("scratchpad_show")
    s, d = window(), drop()
    assert d.drop == "in" and d.edge == "top", d
    rounded = corner(desktop)
    if rounded:
        # The whole way, from wholly above the monitor's top: well past where it would stop.
        assert top == 720 and d.cut == top and d.dy < top - (s.y - BORDER) - 100, (d, s)
        # No bar hides the cut, so crossing it, it is cut square, its border's rounded rect
        # left out, rather than show rounded corners and a border along the monitor's edge.
        crossing = None
        while (d := drop()).drop == "in":
            drawn = s.y + d.dy
            if d.box[3] > 1 and drawn < top:
                crossing = d
                assert d.box[1] == top and d.radius == 0 and d.ring[2:] == (0, 0), (d, s)
            elif drawn - BORDER >= top:
                assert d.radius == rounded - BORDER and \
                    d.ring[2:] == (s.width + 2 * BORDER, s.height + 2 * BORDER), (d, s)
        assert crossing, "seen crossing the cut"
    else:
        assert top == 720 and d.cut is None and top - (s.y - BORDER) <= d.dy < 0, (d, s)
    desktop.wait_for(lambda: settled() and drop().dy == 0, "in place")
    # Hidden, it slides out absent for the pointer, also over where it slides to.
    target = top - (s.y + s.height + BORDER) if rounded else top - (s.y - BORDER)
    msg("scratchpad_show")
    assert pid_at(s.x + 10, s.y + target + 20) == "" and drop().drop == "out", drop()
    desktop.wait_for(lambda: settled() and not drop().drawn, "hidden")


run(1, {}, bottom_bar)
run(1, {"PAW_TEST_DROP_CLIP": "1"}, top_bar)
run(2, {"PAW_TEST_DROP_CLIP": "1"}, monitor_above)
print("Scratchpad windows dropped in and out, turned back, and were cut at the bar")
