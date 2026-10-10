# SPDX-License-Identifier: GPL-3.0-or-later
"""Hot corners run their request after the pointer has rested in the corner, once per visit;
a pass through does nothing. The pointer is a virtual one (pointer_probe)."""
from pathlib import Path
import sys

import harness

compositor, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:3])
SCREEN = (1280, 720)
DELAY = .4  # hot_corners.delay, in seconds
# Long enough after the pointer reaches a corner for a request that is not to run to have run.
QUIET = DELAY + .2

with harness.Compositor(compositor, start=False) as desktop:
    flag = desktop.root / "spawned"
    desktop.config.write_text(f"""return {{
    xwayland = false,
    layout = {{ tiling = true, workspaces = 4 }},
    outputs = {{ monitors = {{ ["HEADLESS-1"] = {{ mode = "{SCREEN[0]}x{SCREEN[1]}" }} }} }},
    hot_corners = {{
        size = 10, delay = {int(DELAY * 1000)},
        top_left = "workspace 2",
        top_right = "workspace 3",
        bottom_left = "spawn touch {flag}",
    }},
}}""")
    msg = desktop.msg

    def workspace():
        return int(msg("get", "workspace").strip())

    desktop.start()
    pointer = desktop.virtual_pointer(pointer_probe, *SCREEN)
    assert workspace() == 1
    desktop.detail = lambda: f"workspace {workspace()}"
    right, bottom = str(SCREEN[0] - 1), str(SCREEN[1] - 1)

    # Rest in the top-left corner: nothing at first, then the request runs.
    pointer("move", "0", "0")
    desktop.stays(lambda: workspace() == 1, "ran before the delay", duration=.1)
    desktop.wait_for(lambda: workspace() == 2, "top-left corner")

    # Staying, even moving inside the corner, does not run it again.
    msg("workspace", "1")
    pointer("move", "3", "3", "move", "0", "0")
    desktop.stays(lambda: workspace() == 1, "ran twice in one visit", duration=QUIET)

    # A pass through the corner without resting does nothing.
    pointer("move", "400", "300", "move", "0", "0", "move", "400", "300")
    desktop.stays(lambda: workspace() == 1, "ran without the delay", duration=QUIET)

    # Coming back runs it again; the top-right corner runs its own request.
    pointer("move", "0", "0")
    desktop.wait_for(lambda: workspace() == 2, "top-left corner again")
    pointer("move", right, "0")
    desktop.wait_for(lambda: workspace() == 3, "top-right corner")

    # A corner with a spawn request starts the program.
    assert not flag.exists()
    pointer("move", "0", bottom)
    desktop.wait_for(flag.exists, "spawn from the bottom-left corner")

    # A corner with nothing bound does nothing.
    msg("workspace", "1")
    pointer("move", right, bottom)
    desktop.stays(lambda: workspace() == 1, "ran from a corner with nothing bound",
                  duration=QUIET)

    # Reloading without corners turns them off.
    desktop.reload("return { xwayland = false, layout = { workspaces = 4 } }")
    pointer("move", "0", "0", "move", "400", "300", "move", "0", "0")
    desktop.stays(lambda: workspace() == 1, "ran after the reload removed it", duration=QUIET)
print("Hot corners run after a rest, once per visit")
