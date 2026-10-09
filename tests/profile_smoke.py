# SPDX-License-Identifier: GPL-3.0-or-later
"""`paw msg profile NAME|next|prev` saves the chosen appearance profile and reloads with it;
an unknown name is refused, and a restart keeps the choice."""
from pathlib import Path
import re
import sys

import harness

compositor = str(Path(sys.argv[1]).resolve())

CONFIG = """return {
    xwayland = false,
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    profile = "dark",
    profiles = {
        dark = {},
        light = { appearance = { background = "#f2f4f8" }, shell = { accent = "#3366cc" } },
        sepia = { windows = { border_color = "#704214" } },
    },
}"""

with harness.Compositor(compositor, CONFIG, start=False) as desktop:
    state = desktop.root / "state"
    saved = state / "paw" / "profile"
    desktop.env["XDG_STATE_HOME"] = str(state)
    msg = desktop.msg

    def reloads():
        return re.findall(r"Configuration reloaded: .*?(?: \(profile (\S+)\))?$",
                          desktop.log.read_text(), re.M)

    def picks(*words, expect):
        before = len(reloads())
        msg("profile", *words)
        desktop.wait_for(lambda: len(reloads()) > before, "reload")
        assert reloads()[-1] == expect, (words, reloads())
        assert saved.read_text() == expect + "\n", saved.read_text()

    desktop.start()
    assert not saved.exists()
    picks("light", expect="light")
    picks("next", expect="sepia")
    picks("next", expect="dark")  # wraps, in name order
    picks("prev", expect="sepia")
    assert "no profile nope" in msg("profile", "nope", ok=False)
    assert "takes one profile name" in msg("profile", ok=False)
    assert "takes one profile name" in msg("profile", "a", "b", ok=False)
    assert saved.read_text() == "sepia\n"
    desktop.stop()

    # A restart starts with the saved profile, not the configuration's `profile`.
    desktop.start()
    msg("reload")
    desktop.wait_for(lambda: reloads(), "reload")
    assert reloads()[-1] == "sepia", reloads()
    desktop.stop()

    # Without profiles there is nothing to pick.
    desktop.start("return { xwayland = false }")
    assert "has no profiles" in msg("profile", "next", ok=False)
print("profiles passed")
