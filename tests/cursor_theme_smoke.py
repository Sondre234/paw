# SPDX-License-Identifier: GPL-3.0-or-later
"""The cursor theme and size the desktop's settings name: without $XCURSOR_THEME, paw takes them
from gsettings (org.gnome.desktop.interface), else from GTK's settings.ini, when the theme is
installed, and exports them to what it starts."""
import os
import shutil
import sys

import harness

compositor = sys.argv[1]


def exported(gsettings=None, settings_ini=None, installed=("TestCursors",), env=None,
             real_gsettings=True):
    """XCURSOR_THEME and XCURSOR_SIZE as a startup command sees them, with `gsettings` (a dict)
    in a keyfile, `settings_ini` as gtk-3.0's settings.ini and the themes `installed`."""
    with harness.Compositor(compositor, start=False) as desktop:
        root = desktop.root
        for name in ("XCURSOR_THEME", "XCURSOR_SIZE", "XCURSOR_PATH"):
            desktop.env.pop(name, None)
        config, data, tools = root / "config", root / "data", root / "bin"
        (config / "glib-2.0/settings").mkdir(parents=True)
        (config / "gtk-3.0").mkdir()
        tools.mkdir()
        if gsettings:
            (config / "glib-2.0/settings/keyfile").write_text(
                "[org/gnome/desktop/interface]\n" +
                "".join(f"{key}={value}\n" for key, value in gsettings.items()))
        if settings_ini:
            (config / "gtk-3.0/settings.ini").write_text("[Settings]\n" + settings_ini)
        for theme in installed:
            (data / "icons" / theme / "cursors").mkdir(parents=True)
        if not real_gsettings:
            # A gsettings without the schema, as where GNOME's settings are not installed.
            (tools / "gsettings").write_text("#!/bin/sh\nexit 1\n")
            (tools / "gsettings").chmod(0o755)
        desktop.env.update(XDG_CONFIG_HOME=str(config), XDG_DATA_HOME=str(data),
                           GSETTINGS_BACKEND="keyfile",
                           PATH=f"{tools}:{os.environ.get('PATH', '/usr/bin:/bin')}",
                           **(env or {}))
        record, seen = root / "record", root / "seen"
        record.write_text('#!/bin/sh\necho "$XCURSOR_THEME $XCURSOR_SIZE" > "$1"\n')
        record.chmod(0o755)
        desktop.start(f'return {{ xwayland = false, startup = {{ {{ "{record}", "{seen}" }} }} }}')
        desktop.wait_for(lambda: seen.exists() and seen.read_text(), "the startup command")
        return seen.read_text().split()


if shutil.which("gsettings"):
    # gsettings' theme and size, the theme installed in $XDG_DATA_HOME/icons.
    assert exported({"cursor-theme": "'TestCursors'", "cursor-size": "32"}) == \
        ["TestCursors", "32"]
    # A theme that is not installed is passed over for Adwaita.
    assert exported({"cursor-theme": "'Missing'", "cursor-size": "32"}) == ["Adwaita", "24"]
    # gsettings comes before settings.ini.
    assert exported({"cursor-theme": "'TestCursors'"}, "gtk-cursor-theme-name=Other\n",
                    installed=("TestCursors", "Other")) == ["TestCursors", "24"]
# Without GNOME's settings, GTK's settings.ini.
assert exported(settings_ini="gtk-cursor-theme-name = TestCursors\ngtk-cursor-theme-size=40\n",
                real_gsettings=False) == ["TestCursors", "40"]
# $XCURSOR_THEME and $XCURSOR_SIZE win.
assert exported({"cursor-theme": "'TestCursors'", "cursor-size": "32"},
                env={"XCURSOR_THEME": "Adwaita", "XCURSOR_SIZE": "48"}) == ["Adwaita", "48"]
print("The cursor theme followed gsettings, then settings.ini, unless set or not installed")
