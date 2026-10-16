# SPDX-License-Identifier: GPL-3.0-or-later
"""A window rule's `scratchpad = true` opens the window hidden in the scratchpad, floating at the
rule's size and taking neither the focus nor a tile, whatever else the rule says of how it
shows; scratchpad_show drops it down. With features.scratchpad = false the rule is ignored."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    animations = { enabled = false },
    layout = { tiling = true },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    features = { scratchpad = %s },
    windows = { rules = {
        { app_id = "^dropterm$", scratchpad = true, size = { 800, 400 }, fullscreen = true },
    } },
}"""

with harness.Compositor(compositor, CONFIG % "true") as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def windows():
        return {w.title: w for w in desktop.windows()}

    desktop.detail = lambda: f"windows: {msg('get', 'windows')}"

    def hidden(title):
        w = windows().get(title)
        return w and w.scratchpad and w.minimized and not w.visible and not w.focused

    desktop.open_window(probe, "A", focused=True)
    desktop.spawn([probe, "--window-only"],
                  env={"PAW_PROBE_TITLE": "Drop", "PAW_PROBE_APP_ID": "dropterm"})
    wait_for(lambda: hidden("Drop"), "the dropterm opened hidden in the scratchpad")
    a = windows()["A"]
    assert a.focused and a.tiled and a.width > 1100, windows()
    assert not windows()["Drop"].tiled, windows()

    # Shown, it drops down in the middle of the output at the rule's size, not fullscreen.
    msg("scratchpad_show")
    wait_for(lambda: windows()["Drop"].focused and windows()["Drop"].visible, "the dropterm shown")
    assert windows()["Drop"].box == ((1280 - 800) // 2, (720 - 400) // 2, 800, 400), windows()
    msg("scratchpad_show")
    wait_for(lambda: hidden("Drop") and windows()["A"].focused, "the dropterm hidden again")

    # Without the scratchpad, the rule's scratchpad is ignored: the window opens as without it.
    desktop.reload(CONFIG % "false")
    desktop.open_window(probe, "Drop 2", "dropterm", focused=True)
    wait_for(lambda: not windows()["Drop 2"].scratchpad and windows()["Drop 2"].visible,
             "the second dropterm opened shown")
print("A window rule's scratchpad opens the window hidden there")
