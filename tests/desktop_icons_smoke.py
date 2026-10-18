# SPDX-License-Identifier: GPL-3.0-or-later
"""The desktop_icons action hands the shell a "desktop-icons toggle" line on the control
socket's stream, which shows or hides its desktop icons (shell.desktop_icons); `paw msg
desktop_icons` does it. The shell's part is shell_ui's."""
from pathlib import Path
import sys

import harness

compositor = str(Path(sys.argv[1]).resolve())

with harness.Compositor(compositor, "return { version = 1, xwayland = false }\n") as desktop:
    subscriber = desktop.subscribe()
    desktop.wait_for(lambda: "tiling " in subscriber.text(), "the first state",
                     detail=subscriber.text)
    for count in (1, 2):
        desktop.msg("desktop_icons")
        desktop.wait_for(lambda: subscriber.values("desktop-icons ") == ["toggle"] * count,
                         f"desktop_icons sent on, {count} times",
                         detail=lambda: subscriber.values("desktop-icons "))
    # It takes no argument.
    assert "takes no argument" in desktop.msg("desktop_icons", "on", ok=False)
print("The desktop_icons action reached the shell")
