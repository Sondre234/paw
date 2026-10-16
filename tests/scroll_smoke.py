# SPDX-License-Identifier: GPL-3.0-or-later
"""The scrolling layout: windows open in columns right of the focused one, or first with the
pointer put on the first column, the view follows focus, columns change width, stack and unstack,
and the layout survives a change of layout."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = true, tile_layout = "scroll", gap = 0,
               scroll = { follow = "edge", width = 0.5, presets = { 0.5, 1.0 } } },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
}"""
SCREEN_WIDTH = 1280
# A window floating over the first column, as a launcher's menu might lie there.
OVER = """windows = { rules = { { app_id = "^over$", floating = true, size = { 300, 300 },
                              position = { 20, 100 } } } },"""

with harness.Compositor(compositor, CONFIG) as desktop:
    msg = desktop.msg

    def windows():
        """(focused, tiled, x, y, width, height) per window, left to right, then top down."""
        return sorted((w.focused, w.tiled, *w.box) for w in desktop.windows())

    def by_position():
        return sorted(windows(), key=lambda w: (w[2], w[3]))

    def focused():
        found = [w for w in windows() if w[0]]
        assert len(found) == 1, windows()
        return found[0][2:]

    def visible(rect):
        return rect[0] >= 0 and rect[0] + rect[2] <= SCREEN_WIDTH

    desktop.detail = lambda: f"windows: {windows()}"
    # The pointer rests at the right: over the first column, a new window would open left of it.
    msg("headless_pointer", "add", "mouse")
    msg("headless_pointer", "move", "mouse", "1279", "300")

    def act(action, predicate, message):
        msg(action)
        desktop.wait_for(predicate, message)

    for count in (1, 2, 3):
        desktop.spawn([probe, "--external-control"])
        desktop.wait_for(lambda: len(windows()) == count and all(w[1] for w in windows()),
                         f"window {count} tiled")
    assert msg("get", "layout").split()[0] == "scroll", msg("get", "layout")

    # Three half-width columns on a strip; the newest is focused and in view.
    desktop.wait_for(lambda: {w[4] for w in windows()} == {SCREEN_WIDTH // 2}
                     and len({w[2] for w in windows()}) == 3, "three half columns")
    full_height = windows()[0][5]
    assert 0 < full_height <= 720, full_height

    def columns():
        return [w[2] for w in by_position()]

    # Edge follow scrolled just far enough to show the newest column at the right.
    assert columns() == [-640, 0, 640] and focused()[0] == 640, windows()

    # scroll_left focuses the column to the left; the view stays while that column is
    # on screen, and scrolls back once the first column is reached.
    act("scroll_left", lambda: focused()[0] == 0 and columns() == [-640, 0, 640],
        "scroll_left")
    act("scroll_left", lambda: focused()[0] == 0 and columns() == [0, 640, 1280],
        "scroll_left to the first column")
    # Nothing further left.
    msg("scroll_left")
    assert focused()[0] == 0 and columns() == [0, 640, 1280], windows()
    act("scroll_right", lambda: focused()[0] == 640 and columns() == [0, 640, 1280],
        "scroll_right")

    # Cycling the width goes to the next preset, wrapping.
    act("column_cycle_width", lambda: focused()[2] == SCREEN_WIDTH,
        "column_cycle_width to full width")
    assert visible(focused()), focused()
    act("column_cycle_width", lambda: focused()[2] == SCREEN_WIDTH // 2,
        "column_cycle_width wraps")
    act("column_widen", lambda: focused()[2] > SCREEN_WIDTH // 2, "column_widen")
    act("column_narrow", lambda: focused()[2] == SCREEN_WIDTH // 2, "column_narrow")

    # center_column puts the focused column in the middle of the screen.
    act("center_column", lambda: focused()[0] == (SCREEN_WIDTH - focused()[2]) // 2,
        "center_column")

    # consume_left stacks the focused window under the one on its left, and the
    # column of windows shares the height; expel puts it back in a column.
    act("consume_left", lambda: len({w[2] for w in windows()}) == 2, "consume_left")
    column = [w for w in windows() if w[2] == focused()[0]]
    assert len(column) == 2 and column[0][5] == column[1][5], windows()
    assert column[0][5] * 2 <= full_height and column[0][4] == column[1][4], windows()
    act("expel", lambda: len({w[2] for w in windows()}) == 3
        and all(w[5] == full_height for w in windows()), "expel")

    # Switching the layout away and back keeps a column per window.
    msg("layout_dwindle")
    desktop.wait_for(lambda: msg("get", "layout").split()[0] == "dwindle", "layout_dwindle")
    desktop.wait_for(lambda: {w[4] for w in windows()} != {SCREEN_WIDTH // 2}
                     or len({w[2] for w in windows()}) != 3, "dwindle arranged")
    msg("layout_scroll")
    desktop.wait_for(lambda: msg("get", "layout").split()[0] == "scroll", "layout_scroll")
    desktop.wait_for(lambda: len({w[2] for w in windows()}) == 3, "columns again")

    # Closing every window leaves an empty workspace, and a new one starts over.
    for window in list(desktop.clients):
        window.kill()
        desktop.reap(window)
    desktop.wait_for(lambda: not windows(), "windows closed")
    desktop.spawn([probe, "--external-control"])
    desktop.wait_for(lambda: len(windows()) == 1 and windows()[0][1], "window after closing")
    assert visible(focused()), focused()

    # With the pointer over the first column a new window opens left of it, as the new first
    # column; over another column it opens right of the focused one.
    def strip():
        return [w.title for w in sorted(desktop.windows(), key=lambda w: w.x) if w.tiled]

    def focused_title():
        return next(w.title for w in desktop.windows() if w.focused)

    def point(x):
        msg("headless_pointer", "move", "mouse", str(x), "300")

    def pointer_on():
        return next(tuple(row[1:3]) for row in desktop.rows("seat") if row[0] == "pointer")

    old = strip()[0]
    point(100)
    desktop.open_window(probe, "front", focused=True)
    desktop.wait_for(lambda: strip() == ["front", old] and focused()[0] == 0, "front first")
    point(SCREEN_WIDTH - 1)  # over the second column, which takes focus
    desktop.open_window(probe, "third", focused=True)
    desktop.wait_for(lambda: strip() == ["front", old, "third"], "third right of the second")
    point(100)  # over the second column again, the view having scrolled to the third
    desktop.wait_for(lambda: focused_title() == old, "second column focused")
    desktop.open_window(probe, "after", focused=True)
    desktop.wait_for(lambda: strip() == ["front", old, "after", "third"],
                     "after right of the second")

    # The column under the pointer counts, not the focused one: with focus not following the
    # pointer, the first column is still found under it.
    desktop.reload(CONFIG.replace("xwayland = false,",
                                  "xwayland = false, mouse = { focus_follows = false }, " + OVER))
    msg("scroll_left")
    msg("scroll_left")
    desktop.wait_for(lambda: focused_title() == "front" and focused()[0] == 0, "front in view")
    msg("scroll_right")
    desktop.wait_for(lambda: focused_title() == old, "second column focused by key")
    point(100)
    desktop.stays(lambda: focused_title() == old, "focus stays on the second column")
    desktop.open_window(probe, "lead", focused=True)
    desktop.wait_for(lambda: strip() == ["lead", "front", old, "after", "third"]
                     and focused()[0] == 0, "lead first")

    # Only a tile of the strip that the user put the pointer on counts. With it on a window
    # floating over the first column a new window opens right of the focused column, and still
    # does once that window has gone from under the pointer, as a launcher's menu goes.
    over = desktop.open_window(probe, "over", "over", focused=True)
    point(101)
    desktop.wait_for(lambda: pointer_on() == ("window", "over"), "the pointer on over")
    desktop.open_window(probe, "beside", focused=True)
    desktop.wait_for(lambda: strip()[:3] == ["lead", "beside", "front"],
                     "beside right of the first column")
    over.kill()
    desktop.reap(over)
    desktop.wait_for(lambda: all(w.title != "over" for w in desktop.windows()), "over closed")
    desktop.open_window(probe, "gone", focused=True)
    desktop.wait_for(lambda: "gone" in strip() and strip()[0] == "lead", "gone not first")
    # Back to the first column, which went out of view, and the pointer onto it.
    msg("scroll_left")
    msg("scroll_left")
    desktop.wait_for(lambda: focused_title() == "lead" and focused()[0] == 0, "lead in view")
    point(102)
    desktop.open_window(probe, "start", focused=True)
    desktop.wait_for(lambda: strip()[:2] == ["start", "lead"], "start first")

    # A key moving the focus onto the first column brings the pointer along (focus_follows):
    # paw put it there, not the user, so a new window opens right of the focused column.
    desktop.reload(CONFIG)
    msg("scroll_right")
    desktop.wait_for(lambda: focused_title() == "lead", "the second column focused by key")
    msg("scroll_left")
    desktop.wait_for(lambda: focused_title() == "start" and pointer_on() == ("window", "start"),
                     "the pointer brought onto the first column")
    desktop.open_window(probe, "keyed", focused=True)
    desktop.wait_for(lambda: strip()[:3] == ["start", "keyed", "lead"],
                     "keyed right of the first column")
print("Scroll layout passed")
