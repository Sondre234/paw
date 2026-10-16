# SPDX-License-Identifier: GPL-3.0-or-later
"""A popup whose parent is the xdg_surface of a window that went, its toplevel role destroyed and
the surface kept, which xdg-shell allows: the compositor draws nothing under the window that
went, carries on, and shuts down cleanly."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

with harness.Compositor(compositor, "return { xwayland = false }") as desktop:
    client = desktop.spawn([probe, "--commands"], env={"PAW_PROBE_TITLE": "Parent"},
                           stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    desktop.wait_for(lambda: [w.title for w in desktop.windows()] == ["Parent"], "Parent open")
    client.stdin.write("orphan_popup\n")
    client.stdin.flush()
    assert client.stdout.readline() == "orphan popup made\n"
    desktop.wait_for(lambda: not desktop.windows(), "Parent gone with its toplevel role")
    assert desktop.server.poll() is None, "the compositor ended at the popup"
print("A popup of a surface whose window went is not drawn under it")
