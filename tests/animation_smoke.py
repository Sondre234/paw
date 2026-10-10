# SPDX-License-Identifier: GPL-3.0-or-later
"""Open, reflow, and close windows with animations on; each animation ends and leaves nothing."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])


# Long enough that the test sees each animation running, under ASan too, where a query takes
# some 30 ms.
DURATION = 400


def settings(enabled):
    return f"""return {{
    xwayland = false,
    layout = {{ tiling = true }},
    windows = {{ border_width = 2, inactive_opacity = 0.8 }},
    animations = {{ enabled = {str(enabled).lower()}, duration = {DURATION} }},
}}"""


with harness.Compositor(compositor, settings(True)) as desktop:
    msg = desktop.msg

    def animations():
        """(running animations, window trees stacked in the scene, focus fades (opacity and
        border color) in flight), from one query: a predicate looks at them first, at the
        moment it matters, and at the windows after."""
        return tuple(int(n) for n in msg("get", "animations").split("\t"))

    def state():
        return animations()[:2]

    def windows():
        return desktop.rows("windows")

    desktop.detail = lambda: f"animations: {state()}, windows: {windows()}"
    wait_for = desktop.wait_for

    def launch():
        return desktop.spawn([probe, "--external-control"])

    def close(window):
        window.terminate()
        desktop.reap(window)

    assert state() == (0, 0), state()

    # Opening: the window is placed at once, while its animation runs.
    first = launch()
    wait_for(lambda: state()[0] == 1 and len(windows()) == 1, "first window opening")
    wait_for(lambda: state() == (0, 1), "opening animation finished")

    # A second tile opens and the first glides aside; both end.
    second = launch()
    # The first window lost focus, so its opacity and border fade meanwhile.
    wait_for(lambda: (a := animations())[0] == 2 and a[2] >= 1 and len(windows()) == 2,
             "open, glide and focus fade running")
    wait_for(lambda: animations() == (0, 2, 0), "open, glide and focus fade finished")

    # Closing leaves a copy behind for the animation, which then goes, and the remaining
    # tile glides back.
    close(second)
    wait_for(lambda: state() == (2, 2) and len(windows()) == 1, "closing copy and glide")
    wait_for(lambda: state() == (0, 1), "closing copy removed")
    close(first)
    wait_for(lambda: not windows() and state()[1] <= 1, "last window closed")
    wait_for(lambda: state() == (0, 0), "nothing left behind")

    # Switching workspace slides a copy of the old windows away and brings the new ones
    # in; input never waits for either.
    left = launch()
    wait_for(lambda: len(windows()) == 1 and state() == (0, 1), "window on workspace 1")
    msg("workspace", "2")
    assert msg("get", "workspace") == "2\n"
    wait_for(lambda: state() == (1, 2), "copy sliding away")
    wait_for(lambda: state() == (0, 1), "copy gone")
    right = launch()
    wait_for(lambda: len(windows()) == 2 and state() == (0, 2), "window on workspace 2")
    msg("workspace", "1")
    wait_for(lambda: state() == (2, 3), "one window out, one in")
    assert msg("get", "workspace") == "1\n"
    wait_for(lambda: state() == (0, 2), "slide finished")
    close(left)
    close(right)
    wait_for(lambda: not windows() and state()[0] == 0, "workspace windows closed")
    wait_for(lambda: state() == (0, 0), "nothing left behind")

    # Fullscreen toggles glide the window, and leaving lands it at its old place.
    floating = launch()
    wait_for(lambda: len(windows()) == 1 and state() == (0, 1), "window for fullscreen")
    place = windows()[0][4:8]
    msg("fullscreen")
    wait_for(lambda: state()[0] >= 1, "fullscreen glide running")
    wait_for(lambda: state()[0] == 0, "fullscreen glide finished")
    msg("fullscreen")
    wait_for(lambda: state()[0] >= 1, "glide back running")
    wait_for(lambda: state()[0] == 0 and windows()[0][4:8] == place, "back at its place")
    close(floating)
    wait_for(lambda: not windows() and state() == (0, 0), "fullscreen window closed")

    # Turning animations off ends those running.
    windows_open = [launch(), launch()]
    wait_for(lambda: len(windows()) == 2, "two windows")
    desktop.reload(settings(False))
    wait_for(lambda: state() == (0, 2), "reload ends animations")
    for window in windows_open:
        close(window)
    wait_for(lambda: state() == (0, 0), "closed without animations")

    # Quitting mid-animation frees the copies.
    desktop.reload(settings(True))
    window = launch()
    wait_for(lambda: len(windows()) == 1, "window before quitting")
    close(window)
    wait_for(lambda: state()[0] >= 1, "closing animation before quitting")
    desktop.stop()
print("Opening, glide, closing snapshots, disabling, and quitting mid-animation passed")
