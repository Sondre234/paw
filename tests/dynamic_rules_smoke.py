# SPDX-License-Identifier: GPL-3.0-or-later
"""Dynamic window rules: a rule with `dynamic = true` holds a window to its `floating`, `sticky`
and `above` while the window's title and app ID match it, as they change, and gives back what the
window had once they no longer do; a change by hand meanwhile stays. One matching as the window
opens holds it from the start, a static rule acts as the window opens only, opacity rules follow
the title either way, and reloading the rules lets go of what they no longer hold."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

RULES = """
        { title = "Meeting", dynamic = true, floating = true, above = true },
        { app_id = "^app-share$", title = "^Sharing", dynamic = true, sticky = true },
        { title = "^Video", above = true },
        { title = "Faded", dynamic = true, opacity = 0.5 },"""
CONFIG = """return {
    xwayland = false,
    layout = { tiling = true, workspaces = 4 },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    animations = { enabled = false },
    windows = { rules = { %s } },
}"""

with harness.Compositor(compositor, CONFIG % RULES) as desktop:
    msg = desktop.msg

    def windows():
        """app_id -> (title, tiled, sticky, above)."""
        return {r[8]: (r[9], r[3] == "1", r[13] == "1", r[15] == "1")
                for r in desktop.rows("windows")}

    def held(app_id):
        """What the dynamic rules decide of floating, sticky and above, and what they hold."""
        return {r[0]: tuple(r[2:6]) for r in desktop.rows("dynamic_rules")}[app_id]

    def opacity(app_id):
        return {r[0]: float(r[3]) for r in desktop.rows("opacities")}[app_id]

    desktop.detail = lambda: (f"windows: {windows()}\n"
                              f"dynamic rules: {desktop.rows('dynamic_rules')}")
    clients = {}

    def launch(app_id, title):
        clients[app_id] = desktop.spawn([probe, "--commands"], stdin=subprocess.PIPE, text=True,
                                        env={"PAW_PROBE_TITLE": title,
                                             "PAW_PROBE_APP_ID": app_id})
        desktop.wait_for(lambda: app_id in windows(), f"{app_id} open")

    def retitle(app_id, title, expect):
        """Gives the window a new title; done once the window is as `expect` says."""
        clients[app_id].stdin.write(f"title {title}\n")
        clients[app_id].stdin.flush()
        desktop.wait_for(lambda: windows()[app_id] == (title, *expect),
                         f"{app_id} titled {title} as {expect}")

    def focus(app_id):
        subprocess.run([probe, "--activate", app_id], env=desktop.env, check=True, timeout=30,
                       stdout=subprocess.DEVNULL)

    launch("app-mail", "Inbox")
    launch("app-other", "Other")
    desktop.wait_for(lambda: windows()["app-mail"] == ("Inbox", True, False, False), "a tile")
    assert held("app-mail") == ("-", "-", "-", "-"), held("app-mail")

    # Matching, it floats over the others; no longer matching, it tiles again, let go.
    retitle("app-mail", "Meeting with Robin", (False, False, True))
    assert held("app-mail") == ("on", "-", "on", "floating,above"), held("app-mail")
    assert desktop.rows("stacking")[0][1:4:2] == ["Meeting with Robin", "above"]
    retitle("app-mail", "Inbox", (True, False, False))
    assert held("app-mail") == ("-", "-", "-", "-"), held("app-mail")

    # Let go by hand while held, it stays so through more titles the rule matches, and as the
    # rule stops matching; what the rule still held is given back.
    retitle("app-mail", "Meeting", (False, False, True))
    focus("app-mail")
    desktop.wait_for(lambda: {r[8]: r[1] for r in desktop.rows("windows")}["app-mail"] == "1",
                     "app-mail focused")
    msg("toggle_above")
    assert windows()["app-mail"] == ("Meeting", False, False, False)
    assert held("app-mail") == ("on", "-", "on", "floating"), held("app-mail")
    retitle("app-mail", "Meeting again", (False, False, False))
    retitle("app-mail", "Inbox", (True, False, False))

    # A static rule acts as a window opens only.
    retitle("app-mail", "Video call", (True, False, False))
    desktop.stays(lambda: not windows()["app-mail"][3], "a static rule applied on a new title")

    # A dynamic rule matching as the window opens holds it from the start.
    launch("app-call", "Meeting at noon")
    desktop.wait_for(lambda: windows()["app-call"] == ("Meeting at noon", False, False, True),
                     "the call opened floating and above")
    assert held("app-call") == ("on", "-", "on", "floating,above"), held("app-call")
    retitle("app-call", "Call ended", (True, False, False))

    # Sticky, by app ID and title together; let go, it tiles again as before.
    launch("app-share", "Desk")
    desktop.wait_for(lambda: windows()["app-share"] == ("Desk", True, False, False), "tiled")
    retitle("app-share", "Sharing your screen", (False, True, False))
    msg("workspace", "2")
    assert {r[8]: r[0] for r in desktop.rows("windows")}["app-share"] == "2", windows()
    msg("workspace", "1")
    retitle("app-share", "Desk", (True, False, False))

    # Opacity rules follow the title, dynamic or not.
    retitle("app-other", "Faded out", (True, False, False))
    desktop.wait_for(lambda: opacity("app-other") < 0.6, "the window faded by its rule")
    retitle("app-other", "Other", (True, False, False))
    desktop.wait_for(lambda: opacity("app-other") == 1, "the window opaque again")

    # A reload without the rule lets go of what it held; with it, it holds the window again.
    retitle("app-mail", "Meeting", (False, False, True))
    desktop.reload(CONFIG % RULES.replace('dynamic = true, floating', 'floating'))
    desktop.wait_for(lambda: windows()["app-mail"] == ("Meeting", True, False, False),
                     "let go as the rule became static")
    desktop.reload(CONFIG % RULES)
    desktop.wait_for(lambda: windows()["app-mail"] == ("Meeting", False, False, True),
                     "held again by the dynamic rule")
    desktop.reload((CONFIG % RULES).replace("xwayland = false,",
                                            "xwayland = false, features = { window_rules = false },"))
    desktop.wait_for(lambda: windows()["app-mail"] == ("Meeting", True, False, False),
                     "let go with features.window_rules off")

    for app_id, client in clients.items():
        client.stdin.close()
        subprocess.run([probe, "--close", app_id], env=desktop.env, check=True, timeout=30,
                       stdout=subprocess.DEVNULL)
        assert desktop.reap(client) == 0
print("Dynamic rules hold windows while their titles match, and let go as they stop")
