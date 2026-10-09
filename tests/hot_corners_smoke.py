# SPDX-License-Identifier: GPL-3.0-or-later
"""Hot corners run their request after the pointer has rested in the corner, once per visit;
a pass through does nothing. The pointer is moved with wlrctl, a virtual pointer."""
from pathlib import Path
import shutil
import subprocess
import sys
import time

import harness

compositor = str(Path(sys.argv[1]).resolve())
wlrctl = shutil.which("wlrctl")
if not wlrctl:
    print("wlrctl not found: hot corners not driven")
    sys.exit(0)

with harness.Compositor(compositor, start=False) as desktop:
    flag = desktop.root / "spawned"
    desktop.config.write_text(f"""return {{
    xwayland = false,
    layout = {{ tiling = true, workspaces = 4 }},
    hot_corners = {{
        size = 10, delay = 800,
        top_left = "workspace 2",
        top_right = "workspace 3",
        bottom_left = "spawn touch {flag}",
    }},
}}""")
    msg = desktop.msg

    def workspace():
        return int(msg("get", "workspace").strip())

    def move(dx, dy):
        subprocess.run([wlrctl, "pointer", "move", str(dx), str(dy)], env=desktop.env, check=True,
                       timeout=30)

    desktop.start()
    assert workspace() == 1
    desktop.detail = lambda: f"workspace {workspace()}"

    # Rest in the top-left corner: nothing at first, then the request runs.
    move(-5000, -5000)
    time.sleep(0.1)
    assert workspace() == 1, "ran before the delay"
    desktop.wait_for(lambda: workspace() == 2, "top-left corner")

    # Staying, even moving inside the corner, does not run it again.
    msg("workspace", "1")
    move(3, 3)
    time.sleep(0.2)
    move(-3, -3)
    time.sleep(1.1)
    assert workspace() == 1, "ran twice in one visit"

    # A pass through the corner without resting does nothing.
    move(400, 300)
    move(-5000, -5000)
    time.sleep(0.1)
    move(400, 300)
    time.sleep(1.1)
    assert workspace() == 1, "ran without the delay"

    # Coming back runs it again; the top-right corner runs its own request.
    move(-5000, -5000)
    desktop.wait_for(lambda: workspace() == 2, "top-left corner again")
    move(5000, 0)
    desktop.wait_for(lambda: workspace() == 3, "top-right corner")

    # A corner with a spawn request starts the program.
    assert not flag.exists()
    move(-5000, 5000)
    desktop.wait_for(flag.exists, "spawn from the bottom-left corner")

    # A corner with nothing bound does nothing.
    msg("workspace", "1")
    move(5000, 5000)
    time.sleep(1.1)
    assert workspace() == 1

    # Reloading without corners turns them off.
    desktop.reload("return { xwayland = false, layout = { workspaces = 4 } }")
    move(-5000, -5000)
    move(400, 300)
    move(-5000, -5000)
    time.sleep(1.1)
    assert workspace() == 1, "ran after the reload removed it"
print("Hot corners run after a rest, once per visit")
