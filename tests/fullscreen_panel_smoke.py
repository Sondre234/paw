# SPDX-License-Identifier: GPL-3.0-or-later
"""A fullscreen window covers the output except the bars, which stay shown whether it has focus
or not. A new window opens in front of it while it stays fullscreen, unless the new window
tiles: then it takes it out of fullscreen instead of opening under it."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

with harness.Compositor(compositor, "return { xwayland = false }") as desktop:
    msg = desktop.msg

    def windows():
        return desktop.windows()

    def focused():
        return next((w.title for w in windows() if w.focused), None)

    def fullscreen():
        # Each probe's panel reserves 48 pixels along the bottom.
        area = (0, 0, int(size[0]), int(size[1]) - 48 * len(desktop.clients))
        return [w.title for w in windows() if w.box == area]

    def front():
        """The title of the window drawn in front of the others, and its layer."""
        row = desktop.rows("stacking")[0]
        return row[1], row[3]

    def panels():
        return [row[3] == "1" for row in desktop.rows("layers") if row[0] == "paw-test-panel"]

    desktop.detail = lambda: f"windows: {windows()}, panels: {panels()}"
    wait_for = desktop.wait_for
    # Bars are updated as each frame is drawn; give a few frames the chance to undo it.
    stays = desktop.stays

    def shown():
        found = panels()
        return len(found) == len(desktop.clients) and all(found)

    _, _, _, _, width, height, *_ = msg("get", "outputs").split("\t")
    size = [width, height]

    def probe_window(title, count):
        desktop.spawn([probe, "--external-control"], env={"PAW_PROBE_TITLE": title})
        wait_for(lambda: len(windows()) == count and focused() == title and shown(),
                 f"window {title} and its panel")

    # Each probe reserves a test panel along the bottom and opens a window.
    probe_window("one", 1)
    probe_window("two", 2)
    msg("fullscreen")
    wait_for(lambda: fullscreen() == ["two"], "fullscreen above the panels")
    stays(shown, "fullscreen hid the panels")

    msg("cycle")
    wait_for(lambda: fullscreen() == ["two"] and focused() == "one", "focus on the other window")
    stays(shown, "the panels hid behind an unfocused fullscreen window")

    msg("workspace", "2")
    wait_for(shown, "panels on a workspace without the fullscreen window")
    msg("workspace", "1")
    stays(shown, "panels hidden on the fullscreen window's workspace")

    # Without tiling a new window opens in front of the fullscreen one, which stays fullscreen
    # behind it.
    msg("cycle")
    wait_for(lambda: focused() == "two" and front() == ("two", "fullscreen"),
             "the fullscreen window focused in front")
    probe_window("three", 3)
    wait_for(lambda: front() == ("three", "normal"), "the new window in front")
    stays(lambda: fullscreen() == ["two"] and shown(), "the new window ended fullscreen")

    desktop.clients.pop().kill()
    wait_for(lambda: len(windows()) == 2 and focused() == "two" and
             front() == ("two", "fullscreen") and fullscreen() == ["two"] and shown(),
             "the fullscreen window in front again")
    msg("fullscreen")
    wait_for(lambda: not fullscreen() and shown(), "panels back after leaving fullscreen")

    # A new tile would open under it, so with tiling it leaves fullscreen first.
    msg("fullscreen")
    wait_for(lambda: fullscreen() == ["two"] and shown(), "fullscreen again")
    msg("toggle_tiling")
    wait_for(lambda: all(w.tiled for w in windows()) and fullscreen() == ["two"],
             "both windows tiled, one fullscreen")
    probe_window("three", 3)
    wait_for(lambda: not fullscreen(), "the new tile ended fullscreen")
print("Fullscreen windows leave the bars shown, focused or not, stay fullscreen under new "
      "windows and leave it for new tiles")
