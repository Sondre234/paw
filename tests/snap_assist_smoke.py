# SPDX-License-Identifier: GPL-3.0-or-later
"""Snap Assist: after a window snaps into a half or a quarter, from the keyboard or by dragging,
the free slot beside it lists the monitor's other windows, minimized ones too (the overview's
thumbnails, in that slot alone); Return, a click on a thumbnail or overview_confirm puts one there, which brings
Snap Assist back for the next free slot while windows are left to offer; Escape, a click
elsewhere (which goes on to what is there), another binding or action, and the snapped window
closing dismiss it. Not with a single window, on a monitor that tiles, or with
windows.snap.assist = false. Driven by a headless keyboard and a virtual pointer
(pointer_probe)."""
from contextlib import contextmanager
from pathlib import Path
import sys

import harness

compositor, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

SCREEN = (1280, 720)
ESCAPE, RETURN, LEFTMETA, LEFTALT, RIGHT = 1, 28, 125, 56, 106
LEFT, RIGHT_HALF = (0, 0, 640, 720), (640, 0, 640, 720)
TOP_LEFT, TOP_RIGHT = (0, 0, 640, 360), (640, 0, 640, 360)
BOTTOM_LEFT, BOTTOM_RIGHT = (0, 360, 640, 360), (640, 360, 640, 360)


def config(assist="true", tiling="false"):
    return """return {
    xwayland = false,
    animations = { enabled = false },
    overview = { animation = false },
    layout = { tiling = %s, gap = 0 },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    windows = { magnet = { enabled = false }, snap = { assist = %s } },
    bindings = { { mods = { "Super", "Alt" }, key = "Right", action = "snap_cycle_right" } },
}""" % (tiling, assist)


class Desktop:
    """A headless compositor with windows A, B and C (C focused, B before it), each asking to be
    moved when pressed on, a headless keyboard and a virtual pointer."""

    def __init__(self, desktop, titles):
        self.desktop, self.msg = desktop, desktop.msg
        desktop.detail = lambda: f"windows: {self.windows()} overview: {self.msg('get', 'overview')}"
        self.msg("headless_keyboard", "add", "keys")
        self.pointer = desktop.virtual_pointer(pointer_probe, *SCREEN)
        self.clients, self.floating = {}, {}
        for title in titles:
            self.clients[title] = desktop.spawn(
                [probe, "--window-only"],
                env={"PAW_PROBE_TITLE": title, "PAW_PROBE_MOVE": "1"})
            desktop.wait_for(lambda: title in self.windows() and self.windows()[title][4],
                             f"{title} focused")
            self.floating[title] = self.windows()[title][:4]

    def windows(self):
        """By title: x, y, width, height, focused, minimized."""
        return {r[9]: (int(r[4]), int(r[5]), int(r[6]), int(r[7]), r[1] == "1", r[2] == "1")
                for r in self.desktop.rows("windows")}

    def assist(self):
        """None while Snap Assist is closed, else its slot, its selection and the titles it lists
        (most recently used first) with their cells."""
        lines = self.msg("get", "overview").splitlines()
        if not lines[0].startswith("open") or not lines[1].startswith("overview-assist "):
            return None
        words = lines[1].split(" ")
        cells = {}
        for line in lines[2:]:
            if line.startswith("overview-window "):
                x, y, w, h, tail = line.split(" ", 5)[1:]
                cells[tail.split("\t")[1]] = (int(x), int(y), int(w), int(h))
        return {"slot": tuple(int(n) for n in words[4:8]), "selected": int(words[3]),
                "windows": cells}

    def offered(self, slot, titles):
        self.desktop.wait_for(lambda: self.assist() and self.assist()["slot"] == slot,
                              f"Snap Assist in {slot}")
        shown = self.assist()
        assert list(shown["windows"]) == titles, shown
        for x, y, w, h in shown["windows"].values():
            assert slot[0] <= x and x + w <= slot[0] + slot[2], shown
            assert slot[1] <= y and y + h <= slot[1] + slot[3], shown
        return shown

    def closed(self):
        self.desktop.wait_for(lambda: self.assist() is None, "Snap Assist closed")

    def placed(self, title, box):
        self.desktop.wait_for(lambda: self.windows()[title][:4] == tuple(box), f"{title} at {box}")

    def key(self, code, *held):
        for down in held:
            self.msg("headless_keyboard", "key", "keys", str(down), "press")
        self.msg("headless_keyboard", "key", "keys", str(code), "press")
        self.msg("headless_keyboard", "key", "keys", str(code), "release")
        for down in reversed(held):
            self.msg("headless_keyboard", "key", "keys", str(down), "release")

    def click(self, x, y):
        self.pointer("move", str(x), str(y), "click", "left")


@contextmanager
def session(titles=("A", "B", "C"), **settings):
    with harness.Compositor(compositor, config(**settings)) as desktop:
        yield Desktop(desktop, titles)


# A single window: nothing to offer.
with session(("A",)) as d:
    d.msg("snap_left")
    d.placed("A", LEFT)
    assert d.assist() is None, d.msg("get", "overview")

# Return puts the selected window (the most recently used) into the free half; with both halves
# taken Snap Assist goes, and the window picked has the focus.
with session() as d:
    d.msg("snap_left")
    d.placed("C", LEFT)
    assert d.offered(RIGHT_HALF, ["B", "A"])["selected"] == 0
    d.key(RETURN)
    d.placed("B", RIGHT_HALF)
    d.closed()
    assert d.windows()["B"][4] and d.windows()["A"][:4] == d.floating["A"], d.windows()

# Escape dismisses it and changes nothing; a binding dismisses it and runs (Super+Alt+Right
# gives the left half its own size back); an action through the socket too, and one snapping
# brings it back beside the new slot.
with session() as d:
    d.msg("snap_left")
    d.offered(RIGHT_HALF, ["B", "A"])
    d.key(ESCAPE)
    d.closed()
    assert d.windows()["C"][:4] == LEFT and d.windows()["B"][:4] == d.floating["B"], d.windows()
    d.msg("snap_right")
    d.placed("C", RIGHT_HALF)
    d.offered(LEFT, ["B", "A"])
    d.key(RIGHT, LEFTMETA, LEFTALT)  # snap_cycle_right: from the right half on to no monitor
    d.closed()
    d.msg("snap_left")
    d.offered(RIGHT_HALF, ["B", "A"])
    d.msg("snap_top_left")
    d.placed("C", TOP_LEFT)
    d.offered(TOP_RIGHT, ["B", "A"])
    d.msg("overview_cancel")
    d.closed()

# A click elsewhere dismisses it and goes on to what is there; a click on a thumbnail puts that
# window into the slot.
with session() as d:
    d.msg("snap_left")
    d.offered(RIGHT_HALF, ["B", "A"])
    d.click(300, 300)
    d.closed()
    d.desktop.wait_for(lambda: "\twindow\tC" in d.msg("get", "seat"), "the pointer on C")
    d.msg("snap_right")
    x, y, w, h = d.offered(LEFT, ["B", "A"])["windows"]["A"]
    d.click(x + w // 2, y + h // 2)
    d.placed("A", LEFT)
    d.closed()
    assert d.windows()["A"][4], d.windows()

# A minimized window is offered too, and comes back into the slot.
with session() as d:
    d.msg("snap_cycle_down")  # C, at its own size, minimized; B has the focus
    d.desktop.wait_for(lambda: d.windows()["C"][5] and d.windows()["B"][4], "C minimized")
    d.msg("snap_left")
    d.placed("B", LEFT)
    d.offered(RIGHT_HALF, ["C", "A"])
    d.key(RETURN)
    d.placed("C", RIGHT_HALF)
    assert not d.windows()["C"][5] and d.windows()["C"][4], d.windows()

# Quarters fill in turn: beside, then below; with no window left to offer, it stays away.
with session() as d:
    d.msg("snap_top_left")
    d.placed("C", TOP_LEFT)
    d.offered(TOP_RIGHT, ["B", "A"])
    d.msg("overview_confirm")
    d.placed("B", TOP_RIGHT)
    d.offered(BOTTOM_RIGHT, ["A"])
    d.msg("overview_confirm")
    d.placed("A", BOTTOM_RIGHT)
    d.closed()

# Dropping a window at an edge offers the rest too; the snapped window closing takes it away.
with session() as d:
    x, y = d.windows()["C"][:2]
    d.pointer("move", str(x + 100), str(y + 20), "press", "left")
    d.desktop.wait_for(lambda: d.desktop.rows("snap")[0][6] == "1", "C moved")
    d.pointer("move", "0", "400", "release", "left")
    d.placed("C", LEFT)
    d.offered(RIGHT_HALF, ["B", "A"])
    d.clients["C"].terminate()
    d.desktop.reap(d.clients["C"], timeout=5)
    d.closed()

# Off, or on a monitor that tiles, nothing is offered.
with session(assist="false") as d:
    d.msg("snap_left")
    d.placed("C", LEFT)
    d.desktop.stays(lambda: d.assist() is None, "no Snap Assist")
with session(tiling="true") as d:
    d.msg("snap_left")
    d.desktop.wait_for(lambda: d.windows()["C"][2] < 1280 and d.windows()["C"][0] == 0,
                       "C snapped")
    d.desktop.stays(lambda: d.assist() is None, "no Snap Assist on a tiling monitor")
print("Snap Assist offers the other windows beside a snapped one")
