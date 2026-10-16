# SPDX-License-Identifier: GPL-3.0-or-later
"""Window rules' idle_inhibit: "always" keeps the machine awake while the window is open, shown or
not, "visible" while it shows, "focus" while it has the focus, "fullscreen" while it is fullscreen
and shows, and "none" refuses the window's own idle inhibitor. The rules follow the window's title
as it changes and are applied anew on a reload; `get idle` lists the windows they name."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, idle_probe, probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

RULES = """
        { app_id = "^always$", idle_inhibit = "always" },
        { app_id = "^visible$", idle_inhibit = "visible" },
        { app_id = "^focus$", idle_inhibit = "focus" },
        { app_id = "^game$", idle_inhibit = "fullscreen" },
        { title = "Playing$", idle_inhibit = "visible" },
        { app_id = "^idle-probe$", title = "^Muted", idle_inhibit = "none" },"""
CONFIG = """return {
    xwayland = false,
    animations = { enabled = false },
    layout = { tiling = true, workspaces = 4 },
    windows = { rules = { %s } },
}"""

with harness.Compositor(compositor, CONFIG % RULES) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def held():
        """Whether anything holds the idle steps off."""
        state = desktop.rows("idle")[0]
        assert state[0] == "state", state
        return state[2] == "1"

    def rules():
        """title -> (whether its rule keeps the machine awake now, the rule's idle_inhibit)."""
        return {r[3]: (r[1] == "1", r[2]) for r in desktop.rows("idle") if r[0] == "rule"}

    def inhibitors():
        return [(r[1] == "1", r[2], r[3]) for r in desktop.rows("idle") if r[0] == "inhibitor"]

    desktop.detail = lambda: f"idle: {msg('get', 'idle')}windows: {msg('get', 'windows')}"
    clients = {}

    def launch(app_id, title):
        clients[app_id] = desktop.spawn([probe, "--commands"], stdin=subprocess.PIPE, text=True,
                                        env={"PAW_PROBE_TITLE": title, "PAW_PROBE_APP_ID": app_id})
        wait_for(lambda: any(w.app_id == app_id and w.focused for w in desktop.windows()),
                 f"{app_id} open and focused")

    def focus(app_id):
        subprocess.run([probe, "--activate", app_id], env=desktop.env, check=True, timeout=30,
                       stdout=subprocess.DEVNULL)
        wait_for(lambda: any(w.app_id == app_id and w.focused for w in desktop.windows()),
                 f"{app_id} focused")

    def holds(expect, why):
        wait_for(lambda: held() == expect, ("held " if expect else "not held ") + why)

    def close(app_id):
        client = clients.pop(app_id)
        client.terminate()
        desktop.reap(client)
        wait_for(lambda: all(w.app_id != app_id for w in desktop.windows()), f"{app_id} closed")

    assert not held() and rules() == {}, msg("get", "idle")

    # "focus": awake while the window has the focus.
    launch("focus", "Focus")
    holds(True, "with the focus window focused")
    assert rules() == {"Focus": (True, "focus")}, rules()
    launch("plain", "Plain")
    holds(False, "with another window focused")
    assert rules() == {"Focus": (False, "focus")}, rules()
    focus("focus")
    holds(True, "with the focus window focused again")
    msg("workspace", "2")
    holds(False, "on another workspace")
    msg("workspace", "1")
    close("focus")
    holds(False, "the focus window closed")

    # "fullscreen": awake while it is fullscreen and shows.
    launch("game", "Game")
    holds(False, "with the game not fullscreen")
    msg("fullscreen")
    holds(True, "with the game fullscreen")
    msg("workspace", "2")
    holds(False, "with the fullscreen game on another workspace")
    msg("workspace", "1")
    holds(True, "with the fullscreen game shown again")
    msg("fullscreen")
    holds(False, "with the game out of fullscreen")
    close("game")

    # "visible": awake while it shows, focused or not.
    launch("visible", "Visible")
    holds(True, "with the visible window shown")
    focus("plain")
    holds(True, "with the visible window shown, not focused")
    focus("visible")
    msg("move_to_workspace", "2")
    holds(False, "with the visible window on another workspace")
    msg("workspace", "2")
    holds(True, "with its workspace shown")
    msg("workspace", "1")
    close("visible")

    # "always": awake while it is open, shown or not.
    launch("always", "Always")
    holds(True, "with the always window open")
    msg("move_to_workspace", "3")
    desktop.stays(lambda: held(), "let go with the always window on another workspace",
                  duration=0.3)
    close("always")
    holds(False, "the always window closed")

    # The rules follow the title: a player whose title says it plays keeps the machine awake.
    launch("player", "Film")
    assert rules() == {} and not held(), rules()
    clients["player"].stdin.write("title Film Playing\n")
    clients["player"].stdin.flush()
    holds(True, "with the player playing")
    assert rules() == {"Film Playing": (True, "visible")}, rules()
    clients["player"].stdin.write("title Film\n")
    clients["player"].stdin.flush()
    holds(False, "with the player stopped")
    close("player")

    # "none": the window's own idle inhibitor is refused, until a reload drops the rule.
    muted = desktop.spawn([idle_probe, "window", "Muted Video"], stdout=subprocess.PIPE, text=True)
    assert muted.stdout.readline().strip() == "ready"
    wait_for(lambda: inhibitors() == [(False, "window", "Muted Video")],
             "the muted window's inhibitor refused")
    assert not held() and rules() == {"Muted Video": (False, "none")}, msg("get", "idle")
    desktop.reload(CONFIG % RULES.replace('idle_inhibit = "none"', 'floating = true'))
    wait_for(lambda: inhibitors() == [(True, "window", "Muted Video")] and held(),
             "the inhibitor applies once the rule is gone")
    assert rules() == {}, rules()
print("Window rules' idle_inhibit keeps the machine awake as it says")
