# SPDX-License-Identifier: GPL-3.0-or-later
"""Run X11 clients through XWayland on a private headless compositor."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, x11_probe, wayland_probe, example = (str(Path(p).resolve()) for p in sys.argv[1:5])

source = Path(example).read_text()

# Disabled XWayland must not advertise an X display.
with harness.Compositor(compositor,
                        source.replace("xwayland = true", "xwayland = false")) as desktop:
    assert "XWayland listening" not in desktop.log.read_text(), desktop.log.read_text()
    assert "DISPLAY" not in desktop.env
    desktop.stop()

    desktop.start(source)
    # Xwayland starts on demand; the first client's window must still map.
    assert "XWayland ready" not in desktop.log.read_text(), desktop.log.read_text()
    subprocess.run([x11_probe], env=desktop.env, check=True, timeout=30)
    # A second client reuses the running Xwayland; close it from the taskbar.
    client = desktop.spawn([x11_probe, "wait-close"], stdout=subprocess.PIPE, text=True)
    assert client.stdout.readline().strip() == "X11 window mapped and focused"
    assert client.stdout.readline().strip() == "waiting for close"
    subprocess.run([wayland_probe, "--close", "paw-x11-probe"], env=desktop.env, check=True,
                   timeout=30)
    assert desktop.reap(client) == 0
print("XWayland mapping, focus, fullscreen, taskbar close, and disable passed")
