# SPDX-License-Identifier: GPL-3.0-or-later
"""workspace_back returns each monitor to its previous workspace, and
features.workspace_back_and_forth makes `workspace N` for the shown workspace do the same."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    layout = { workspaces = 4 },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" },
                             ["HEADLESS-2"] = { mode = "1280x720" } } },
    features = { workspace_back_and_forth = %s },
}"""

with harness.Compositor(compositor, CONFIG % "false",
                        env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    msg = desktop.msg

    def current():
        rows = desktop.rows("workspaces")
        return {row[0]: int(row[1]) for row in rows}

    def on(output, *words):
        msg("output", output, *words)
        return current()[output]

    def windows():
        return {w.app_id: (w.workspace, w.output, w.visible) for w in desktop.windows()}

    desktop.wait_for(lambda: len(current()) == 2, "two outputs")
    assert "takes no argument" in msg("workspace_back", "2", ok=False)

    # Nothing to go back to yet.
    assert on("HEADLESS-1", "workspace_back") == 1
    assert on("HEADLESS-1", "workspace", "3") == 3
    assert on("HEADLESS-1", "workspace_back") == 1
    assert on("HEADLESS-1", "workspace_back") == 3
    # Without the feature, naming the shown workspace stays there.
    assert on("HEADLESS-1", "workspace", "3") == 3
    # workspace_next and workspace_prev count as switches too.
    assert on("HEADLESS-1", "workspace_next") == 4
    assert on("HEADLESS-1", "workspace_back") == 3
    # Each monitor keeps its own.
    assert on("HEADLESS-2", "workspace", "2") == 2
    assert on("HEADLESS-2", "workspace_back") == 1
    assert current() == {"HEADLESS-1": 3, "HEADLESS-2": 1}, current()
    # Without "output", the focused monitor (the one switched last) goes back.
    msg("workspace_back")
    assert current() == {"HEADLESS-1": 3, "HEADLESS-2": 2}, current()

    # Activating a window from the taskbar switches its monitor, and counts as well.
    window = desktop.spawn([probe, "--external-control"])
    desktop.wait_for(lambda: "paw-probe" in windows(), "window mapped")
    _, home, _ = windows()["paw-probe"]
    msg("move_to_workspace", "4")
    assert on(home, "workspace", "1") == 1
    assert windows()["paw-probe"] == (4, home, False), windows()
    subprocess.run([probe, "--activate", "paw-probe"], env=desktop.env, check=True,
                   timeout=30, stdout=subprocess.DEVNULL)
    desktop.wait_for(lambda: current()[home] == 4, "taskbar switch")
    assert on(home, "workspace_back") == 1
    assert on(home, "workspace_back") == 4

    # With the feature, `workspace N` for the shown workspace goes back instead.
    desktop.reload(CONFIG % "true")
    assert on("HEADLESS-1", "workspace", "2") == 2
    assert on("HEADLESS-1", "workspace", "2") == 3
    assert on("HEADLESS-1", "workspace", "3") == 2
    assert on("HEADLESS-1", "workspace", "1") == 1
    assert on("HEADLESS-1", "workspace", "1") == 2
    # An unknown feature is an error: the default configuration, without the feature,
    # stands in until it is fixed.
    desktop.reload(CONFIG.replace("workspace_back_and_forth", "bogus") % "true")
    assert "unknown feature 'bogus'" in desktop.log.read_text()
    assert on("HEADLESS-1", "workspace", "2") == 2

    window.kill()
    desktop.reap(window)
print("workspace_back and workspace back-and-forth passed")
