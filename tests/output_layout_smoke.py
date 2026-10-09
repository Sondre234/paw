# SPDX-License-Identifier: GPL-3.0-or-later
"""layout.outputs gives each output its own layout defaults: they apply to workspaces not set by
hand, reach windows already open when the configuration is reloaded, and never override a
layout chosen with an action."""
from pathlib import Path
import signal
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])


def config(first=""):
    return f"""return {{
    xwayland = false,
    layout = {{ tiling = true, gap = 0, outputs = {{ {first} }} }},
    outputs = {{
        primary = "HEADLESS-1",
        monitors = {{
            ["HEADLESS-1"] = {{ mode = "1280x720" }},
            ["HEADLESS-2"] = {{ mode = "1280x720" }},
        }},
    }},
}}"""


with harness.Compositor(compositor, config(), env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    msg, log = desktop.msg, desktop.log

    def layout(output, workspace=None):
        """(layout, ratio, master count) of a workspace; the current one by default."""
        words = ["get", "layout", output] + ([str(workspace)] if workspace else [])
        name, ratio, count = msg(*words).split()
        return name, float(ratio), int(count)

    def widths():
        return sorted(int(r[6]) for r in desktop.rows("windows"))

    desktop.detail = lambda: f"widths: {widths()}"

    reloads = 0

    def reload(text):
        global reloads
        reloads += 1
        desktop.config.write_text(text)
        desktop.server.send_signal(signal.SIGHUP)
        desktop.wait_for(lambda: log.read_text().count("Configuration reloaded") == reloads,
                         "reload")

    assert layout("HEADLESS-1") == ("dwindle", 0.55, 1)
    for count in (1, 2):
        desktop.spawn([probe, "--external-control"])
        desktop.wait_for(lambda: len(widths()) == count, f"window {count} mapped")
    assert widths() == [640, 640], widths()

    # A reload reaches workspaces that are already arranged and hold windows.
    reload(config("['HEADLESS-1'] = { tile_layout = 'master', master_ratio = 0.6 }"))
    assert layout("HEADLESS-1") == ("master", 0.6, 1)
    assert layout("HEADLESS-1", 3) == ("master", 0.6, 1)
    assert layout("HEADLESS-2") == ("dwindle", 0.55, 1)
    desktop.wait_for(lambda: widths() == [512, 768], "master ratio applied to open windows")

    # An action's choice stays; the other workspaces follow the next reload.
    msg("output", "HEADLESS-1", "layout_monocle")
    assert layout("HEADLESS-1")[0] == "monocle"
    reload(config("['HEADLESS-1'] = { tile_layout = 'spiral', master_count = 2 },"
                  "['HEADLESS-2'] = { master_ratio = 0.7 }"))
    assert layout("HEADLESS-1") == ("monocle", 0.55, 2), layout("HEADLESS-1")
    assert layout("HEADLESS-1", 2) == ("spiral", 0.55, 2)
    assert layout("HEADLESS-2") == ("dwindle", 0.7, 1)

    # Removing an entry returns its workspaces to the global defaults.
    reload(config())
    assert layout("HEADLESS-1", 2) == ("dwindle", 0.55, 1)
    assert layout("HEADLESS-1")[0] == "monocle"
    assert layout("HEADLESS-2") == ("dwindle", 0.55, 1)
    desktop.wait_for(lambda: widths() == [1280, 1280], "monocle fills the output")

    bad = desktop.run("get", "layout", "NOPE")
    assert bad.returncode != 0 or "error" in bad.stdout + bad.stderr
print("Per-output layout defaults, reloads, and manual choices passed")
