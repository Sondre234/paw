# SPDX-License-Identifier: GPL-3.0-or-later
"""Random operations on the overview while windows come and go, with a fixed seed: opening and
closing it, filtering, selecting, viewing workspaces, confirming, and switching, moving,
minimizing and closing windows. The compositor must survive, and end with the overview closed
and every window still listed."""
from pathlib import Path
import random
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])
seed = int(sys.argv[3]) if len(sys.argv) > 3 else 20260928
steps = int(sys.argv[4]) if len(sys.argv) > 4 else 300

CONFIG = """return {
    xwayland = false,
    layout = { tiling = true, workspaces = 4 },
    overview = { animation = true, duration = 30 },
    outputs = { order = { "HEADLESS-1", "HEADLESS-2" },
                monitors = { ["HEADLESS-1"] = { mode = "1280x720" },
                             ["HEADLESS-2"] = { mode = "800x600" } } },
}"""

with harness.Compositor(compositor, CONFIG, env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    run = desktop.run
    clients = []
    rng = random.Random(seed)
    words = ["a", "b", "term", "zz", "note", "x", "e", ""]
    counter = 0

    def spawn():
        global counter
        counter += 1
        clients.append(desktop.spawn([probe, "--window-only"],
                                     env=dict(PAW_PROBE_TITLE=f"win{counter}",
                                              PAW_PROBE_APP_ID="zz")))

    for _ in range(4):
        spawn()
    operations = [
        lambda: run("toggle_overview"),
        lambda: run("toggle_overview"),
        lambda: run("overview", "filter", rng.choice(words) + rng.choice(words)),
        lambda: run("overview", "select", str(rng.randint(1, 8))),
        lambda: run("overview", "view", str(rng.randint(1, 4))),
        lambda: run("overview_confirm", *([str(rng.randint(1, 6))] * rng.randint(0, 1))),
        lambda: run("overview_cancel"),
        lambda: run("workspace", str(rng.randint(1, 4))),
        lambda: run("output", rng.choice(["HEADLESS-1", "HEADLESS-2"]), "toggle_overview"),
        lambda: run("move_to_workspace", str(rng.randint(1, 4))),
        lambda: run("close"),
        lambda: run("toggle_tiling"),
        lambda: run("get", "overview"),
        lambda: spawn(),
        lambda: spawn(),
    ]
    for step in range(steps):
        rng.choice(operations)()
        assert desktop.server.poll() is None, f"the compositor died at step {step}"
        if len(clients) > 12:
            clients.pop(0).terminate()
    run("overview_cancel")
    # The windows come and go, so these waits watch only the compositor.
    harness.wait_for(lambda: run("get", "overview").stdout.startswith("closed"),
                     [desktop.server], "the overview closed")
    harness.wait_for(lambda: len(run("get", "windows").stdout.splitlines()) ==
                     sum(client.poll() is None for client in clients), [desktop.server],
                     "every live client's window listed")
print(f"Overview fuzz passed ({steps} steps, seed {seed})")
