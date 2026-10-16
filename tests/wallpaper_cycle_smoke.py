# SPDX-License-Identifier: GPL-3.0-or-later
"""The wallpaper_next and wallpaper_prev actions hand the shell a "wallpaper-cycle next" or
"wallpaper-cycle prev" line on the control socket's stream, which steps its wallpaper slideshow
(shell.wallpaper_cycle); `paw msg wallpaper_next` does it. The shell's part is shell_ui's."""
from pathlib import Path
import sys

import harness

compositor = str(Path(sys.argv[1]).resolve())

with harness.Compositor(compositor, "return { version = 1, xwayland = false }\n") as desktop:
    subscriber = desktop.subscribe()
    desktop.wait_for(lambda: "tiling " in subscriber.text(), "the first state",
                     detail=subscriber.text)
    expected = []
    for action, verb in (("wallpaper_next", "next"), ("wallpaper_prev", "prev"),
                         ("wallpaper_next", "next")):
        desktop.msg(action)
        expected.append(verb)
        desktop.wait_for(lambda: subscriber.values("wallpaper-cycle ") == expected,
                         f"{action} sent on", detail=lambda: subscriber.values("wallpaper-cycle "))
    # They take no argument.
    assert "takes no argument" in desktop.msg("wallpaper_next", "2", ok=False)
    assert "takes no argument" in desktop.msg("wallpaper_prev", "2", ok=False)
print("The wallpaper_next and wallpaper_prev actions reached the shell")
