# SPDX-License-Identifier: GPL-3.0-or-later
"""`get pid_at X Y`: the process of the window drawn at a layout point, nothing over bare
desktop, and an error without two numbers."""
from pathlib import Path
import sys

import harness

compositor, probe, example = (str(Path(p).resolve()) for p in sys.argv[1:4])

source = Path(example).read_text().replace("xwayland = true", "xwayland = false")

with harness.Compositor(compositor, source) as desktop:
    msg = desktop.msg
    assert "usage" in msg("get", "pid_at", "1", ok=False)
    assert msg("get", "pid_at", "-5000", "-5000") == ""

    client = desktop.spawn([probe, "--external-control"])
    desktop.wait_for(lambda: msg("get", "windows").count("\n") == 1, "window mapped")
    row = msg("get", "windows").split("\t")
    x, y, width, height = (int(value) for value in row[4:8])
    middle = (str(x + width // 2), str(y + height // 2))
    desktop.wait_for(lambda: msg("get", "pid_at", *middle) == f"{client.pid}\n",
                     "the probe's pid at the middle of its window")

    msg("close")
    assert desktop.reap(client) == 0
    desktop.wait_for(lambda: msg("get", "pid_at", *middle) == "",
                     "nothing at the closed window's place")
print("get pid_at names the process of the window under a point")
