# SPDX-License-Identifier: GPL-3.0-or-later
"""A fullscreen window covers the output except the bars, which stay shown whether it has focus
or not, and a new window takes it out of fullscreen instead of opening over it."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

with harness.Compositor(compositor, "return { xwayland = false }") as desktop:
    msg = desktop.msg

    def windows():
        return desktop.rows("windows")

    def focused():
        return next((i for i, row in enumerate(windows()) if row[1] == "1"), None)

    def fullscreen():
        # Each probe's panel reserves 48 pixels along the bottom.
        area = ["0", "0", size[0], str(int(size[1]) - 48 * len(desktop.clients))]
        return [i for i, row in enumerate(windows()) if row[4:8] == area]

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

    def probe_window(count):
        desktop.spawn([probe, "--external-control"])
        wait_for(lambda: len(windows()) == count and focused() is not None and shown(),
                 f"window {count} and its panel")

    # Each probe reserves a test panel along the bottom and opens a window.
    probe_window(1)
    probe_window(2)
    msg("fullscreen")
    wait_for(lambda: fullscreen() == [focused()], "fullscreen above the panels")
    stays(shown, "fullscreen hid the panels")

    msg("cycle")
    wait_for(lambda: len(fullscreen()) == 1 and focused() not in fullscreen(),
             "focus on the other window")
    stays(shown, "the panels hid behind an unfocused fullscreen window")

    msg("workspace", "2")
    wait_for(shown, "panels on a workspace without the fullscreen window")
    msg("workspace", "1")
    stays(shown, "panels hidden on the fullscreen window's workspace")

    # A new window would open over it, so it leaves fullscreen instead.
    probe_window(3)
    assert not fullscreen(), windows()

    desktop.clients.pop().kill()
    wait_for(lambda: len(windows()) == 2, "third window closed")
    msg("fullscreen")
    wait_for(lambda: len(fullscreen()) == 1 and shown(), "fullscreen again")
    msg("fullscreen")
    wait_for(lambda: not fullscreen() and shown(),
             "panels back after leaving fullscreen")
print("Fullscreen windows leave the bars shown, focused or not; new windows end fullscreen")
