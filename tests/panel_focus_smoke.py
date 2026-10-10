# SPDX-License-Identifier: GPL-3.0-or-later
"""A click on a panel keeps the keyboard where it is, even on another monitor than the focused
window's: the taskbar must see that window as focused to minimize it on a click, as Windows does.
A click on the bare desktop there still takes the focus."""
from pathlib import Path
import sys

import harness

compositor, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false },
    outputs = { order = { "HEADLESS-1", "HEADLESS-2" },
                monitors = { ["HEADLESS-1"] = { mode = "1280x720" },
                             ["HEADLESS-2"] = { mode = "800x600" } } },
}"""
LAYOUT = (2080, 720)

with harness.Compositor(compositor, CONFIG, env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    msg = desktop.msg

    def windows():
        return {w.title: dict(focused=w.focused, output=w.output) for w in desktop.windows()}

    def focused():
        return next((t for t, w in windows().items() if w["focused"]), None)

    desktop.detail = lambda: f"windows: {windows()}"
    pointer = desktop.virtual_pointer(pointer_probe, *LAYOUT)

    # The probe's window opens on the first monitor and its 48 pixel bottom panel on the
    # second.
    desktop.spawn([probe, "--external-control"], env={"PAW_PROBE_TITLE": "A"})
    desktop.wait_for(lambda: focused() == "A", "A focused")
    assert windows()["A"]["output"] == "HEADLESS-1", windows()

    pointer("move", str(1280 + 400), str(600 - 20), "click", "left")
    # The panel takes no input, so nothing tells when the click is through but a later
    # request on the same connection; the control socket answers after it.
    msg("get", "windows")
    assert focused() == "A", f"a click on the panel took the focus: {windows()}"

    pointer("move", str(1280 + 400), str(200), "click", "left")
    desktop.wait_for(lambda: focused() is None, "a click on the bare desktop takes the focus")
print("Panel focus passed")
