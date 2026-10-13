# SPDX-License-Identifier: GPL-3.0-or-later
"""appearance.color_scheme in a login session (a headless compositor with PAW_LOGIN_SESSION=1
stands in for --session): paw runs gsettings to set org.gnome.desktop.interface color-scheme as
the configuration loads and as a profile with another scheme is picked, and leaves it alone
otherwise. A stand-in for gsettings writes down each call."""
import os
import sys

import harness

compositor = sys.argv[1]

CONFIG = """return {
    xwayland = false,
    autostart = { xdg = false }, -- else a login session starts the developer's own entries
    appearance = { color_scheme = "dark" },
    profiles = {
        day = { appearance = { color_scheme = "light" } },
        night = {},
        dusk = { appearance = { background = "#202030" } },
    },
}
"""


def calls_in(desktop, login_session=True, config=CONFIG):
    """The gsettings calls of a compositor started with `config`, and the compositor."""
    tools, calls = desktop.root / "bin", desktop.root / "calls"
    tools.mkdir()
    calls.write_text("")
    script = tools / "gsettings"
    script.write_text(f'#!/bin/sh\necho "$*" >> "{calls}"\n')
    script.chmod(0o755)
    desktop.env.update(PATH=f"{tools}:{os.environ.get('PATH', '/usr/bin:/bin')}")
    if login_session:
        desktop.env["PAW_LOGIN_SESSION"] = "1"
    else:
        desktop.env.pop("PAW_LOGIN_SESSION", None)
    desktop.start(config)
    return lambda: calls.read_text().splitlines()


DARK = "set org.gnome.desktop.interface color-scheme prefer-dark"
LIGHT = "set org.gnome.desktop.interface color-scheme prefer-light"

with harness.Compositor(compositor, start=False) as desktop:
    calls = calls_in(desktop)
    desktop.detail = lambda: f"gsettings calls: {calls()}"
    desktop.wait_for(lambda: calls() == [DARK], "the file's dark scheme at startup")
    desktop.msg("profile", "day")
    desktop.wait_for(lambda: calls() == [DARK, LIGHT], "the day profile's light scheme")
    # A profile that names no scheme keeps the file's, dark, which is told again; a reload or
    # a profile that keeps the scheme says nothing, which the next change shows.
    desktop.msg("profile", "night")
    desktop.wait_for(lambda: calls() == [DARK, LIGHT, DARK], "the file's scheme back")
    desktop.msg("profile", "dusk")
    desktop.reload()
    desktop.msg("profile", "day")
    desktop.wait_for(lambda: len(calls()) >= 4, "the day profile's light scheme again")
    desktop.stays(lambda: calls() == [DARK, LIGHT, DARK, LIGHT], "gsettings ran for no change")

# A nested or headless compositor that is not the login session leaves the desktop's alone.
with harness.Compositor(compositor, start=False) as desktop:
    calls = calls_in(desktop, login_session=False)
    desktop.msg("profile", "day")
    desktop.stays(lambda: calls() == [], "gsettings ran outside a login session", duration=1)

print("A login session told applications the profile's color scheme, and nothing else did")
