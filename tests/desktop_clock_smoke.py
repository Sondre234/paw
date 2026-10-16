# SPDX-License-Identifier: GPL-3.0-or-later
"""The desktop_clock action hands the shell a "desktop-clock toggle" line on the control
socket's stream, which shows or hides its desktop clock (shell.desktop_clock); `paw msg
desktop_clock` does it. The shell's part is shell_ui's."""
from pathlib import Path
import sys

import harness

compositor = str(Path(sys.argv[1]).resolve())

with harness.Compositor(compositor, "return { version = 1, xwayland = false }\n") as desktop:
    subscriber = desktop.subscribe()
    desktop.wait_for(lambda: "tiling " in subscriber.text(), "the first state",
                     detail=subscriber.text)
    for count in (1, 2):
        desktop.msg("desktop_clock")
        desktop.wait_for(lambda: subscriber.values("desktop-clock ") == ["toggle"] * count,
                         f"desktop_clock sent on, {count} times",
                         detail=lambda: subscriber.values("desktop-clock "))
    # It takes no argument.
    assert "takes no argument" in desktop.msg("desktop_clock", "on", ok=False)
print("The desktop_clock action reached the shell")
