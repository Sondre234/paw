# SPDX-License-Identifier: GPL-3.0-or-later
"""Window opacity follows the rules through focus, title changes, and reloads, and the rules
are matched only when one of their inputs changes, not on every commit."""
from pathlib import Path
import signal
import subprocess
import sys

import harness

compositor, client, probe = (str(Path(p).resolve()) for p in sys.argv[1:4])


def settings(alpha):
    return f"""return {{
    xwayland = false,
    animations = {{ enabled = false }},
    layout = {{ tiling = true, workspaces = 1 }},
    outputs = {{ monitors = {{ ["HEADLESS-1"] = {{ mode = "1280x720" }} }} }},
    windows = {{ border_width = 2, inactive_opacity = 0.5, rules = {{
        {{ app_id = "^solid$", opacity = 1, inactive_opacity = 1 }},
        {{ title = "^changed$", opacity = {alpha} }},
    }} }},
}}"""


with harness.Compositor(compositor, settings(0.75)) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def opacities():
        """By title: focused, opacity."""
        rows = desktop.rows("opacities")
        return {r[1]: (r[2] == "1", float(r[3])) for r in rows}

    def rule_matches():
        return int(msg("get", "stats").split("\n")[0].split()[6])

    desktop.detail = lambda: f"opacities: {opacities()}"

    def open_window(app_id, title):
        window = desktop.spawn([client, "--app-id", app_id, "--title", title])
        wait_for(lambda: title in opacities(), f"{title} opens")
        return window

    def activate(app_id):
        subprocess.run([probe, "--activate", app_id], env=desktop.env, check=True, timeout=30)

    open_window("plain", "first")
    wait_for(lambda: opacities()["first"] == (True, 1.0), "focused window is opaque")
    second = open_window("retitled", "second")
    wait_for(lambda: opacities()["second"] == (True, 1.0) and
             opacities()["first"] == (False, 0.5), "focus moves the dimming")
    open_window("solid", "third")
    wait_for(lambda: opacities()["third"] == (True, 1.0) and
             opacities()["second"] == (False, 0.5), "third focused")
    msg("focus_next")  # somebody else; the solid window keeps full opacity
    wait_for(lambda: not opacities()["third"][0], "focus left the solid window")
    assert opacities()["third"][1] == 1.0, opacities()

    # A title change re-matches the rules.
    second.send_signal(signal.SIGUSR1)
    wait_for(lambda: "changed" in opacities(), "title changes")
    activate("retitled")
    wait_for(lambda: opacities()["changed"] == (True, 0.75), "the title's rule applies")

    # A window that commits on every frame does not have the rules matched each time.
    desktop.spawn([client, "--animate", "--app-id", "plain", "--title", "busy"])
    wait_for(lambda: "busy" in opacities(), "busy window opens")
    wait_for(lambda: opacities()["busy"] == (True, 1.0), "busy window focused")
    commits = lambda: int(msg("get", "stats").split("\n")[0].split()[3])
    before, commits_before = rule_matches(), commits()
    wait_for(lambda: commits() >= commits_before + 100, "busy window commits")
    assert rule_matches() - before < 5, "rules matched on every commit"

    # A reload with different rules takes effect for windows that did not change.
    activate("retitled")
    wait_for(lambda: opacities()["changed"] == (True, 0.75), "changed focused again")
    desktop.reload(settings(0.6))
    wait_for(lambda: opacities()["changed"] == (True, 0.6), "reload applied")
print("opacity_rules_smoke passed")
