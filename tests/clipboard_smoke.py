# SPDX-License-Identifier: GPL-3.0-or-later
"""The shell's clipboard history against a private headless compositor: clipboard_test follows
what clipboard_probe copies through ext-data-control-v1, and hands back what it restores."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe, test = (str(Path(p).resolve()) for p in sys.argv[1:4])

with harness.Compositor(compositor, "return { xwayland = false }") as desktop:
    subprocess.run([test], env={**desktop.env, "PAW_CLIPBOARD_PROBE": probe,
                                "QT_QPA_PLATFORM": "offscreen"}, check=True, timeout=90)
print("The clipboard history kept, left out and restored what was copied")
