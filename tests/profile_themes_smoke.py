# SPDX-License-Identifier: GPL-3.0-or-later
"""appearance.cursor_theme, cursor_size and icon_theme picked with a profile: the compositor
draws the cursor in the profile's theme and size (`get cursor_theme`), tells subscribers, and
gives programs it starts afterwards $XCURSOR_THEME and $XCURSOR_SIZE; a theme that is not
installed is skipped, saying so. A login session (PAW_LOGIN_SESSION=1) also sets gsettings'
cursor-theme, cursor-size and icon-theme, and puts back the values it replaced as a profile
without them is picked and as it ends, resetting a key that held its default. Cursor themes of
the test's own, on $XCURSOR_PATH, and a stand-in for gsettings that keeps the values set and
writes down each call."""
import os
from pathlib import Path
import struct
import subprocess
import sys

import harness

compositor = sys.argv[1]
x11_probe = str(Path(sys.argv[2]).resolve()) if len(sys.argv) > 2 else ""

CONFIG = """return {
    xwayland = false,
    autostart = { xdg = false }, -- else a login session starts the developer's own entries
    profile = "plain",
    profiles = {
        plain = {},
        themed = { appearance = { cursor_theme = "paw-profile", cursor_size = 32,
                                  icon_theme = "paw-icons" } },
        missing = { appearance = { cursor_theme = "paw-no-such-theme",
                                   icon_theme = "paw-no-such-icons" } },
    },
}
"""


def cursor(path, size):
    """An Xcursor file holding one square image of `size` pixels."""
    pixels = struct.pack("<I", 0xff336699) * size * size
    image = struct.pack("<9I", 36, 0xfffd0002, size, 1, size, size, 1, 1, 0) + pixels
    path.write_bytes(struct.pack("<4sIII", b"Xcur", 16, 0x10000, 1) +
                     struct.pack("<III", 0xfffd0002, size, 16 + 12) + image)


def prepare(desktop, login_session):
    """Cursor themes, an icon theme, a gsettings stand-in and a script that writes down the
    environment it starts with; returns the calls gsettings got, the values it holds and that
    environment."""
    root = desktop.root
    for theme in ("paw-base", "paw-profile", "paw-user"):
        (root / "cursors" / theme / "cursors").mkdir(parents=True)
        cursor(root / "cursors" / theme / "cursors" / "default", 24)
    (root / "data/icons/paw-icons").mkdir(parents=True)
    (root / "data/icons/paw-icons/index.theme").write_text("[Icon Theme]\nName=paw\n")
    tools, calls, store = root / "bin", root / "calls", root / "gsettings"
    tools.mkdir()
    calls.write_text("")
    # The values the user set, a file per key: a cursor theme and an icon theme, but no cursor
    # size. A key without one holds its default, which is all gsettings' memory backend has.
    store.mkdir()
    (store / "cursor-theme").write_text("'paw-user'\n")
    (store / "icon-theme").write_text("'UserIcons'\n")
    (tools / "gsettings").write_text(f"""#!/bin/sh
case "$3" in cursor-size) default=24 ;; *) default="'Adwaita'" ;; esac
[ "$GSETTINGS_BACKEND" = memory ] && {{ echo "$default"; exit; }}
case "$1" in
    get) cat "{store}/$3" 2>/dev/null || echo "$default" ;;
    set) echo "$4" > "{store}/$3" ;;
    reset) rm -f "{store}/$3" ;;
esac
echo "$*" >> "{calls}"
""")
    (tools / "gsettings").chmod(0o755)
    seen = root / "environment"
    (tools / "show-environment").write_text(
        f'#!/bin/sh\nprintf "%s\\n" "$XCURSOR_THEME" "$XCURSOR_SIZE" '
        f'"${{PAW_USER_ICON_THEME-unset}}" > "{seen}.new" && mv "{seen}.new" "{seen}"\n')
    (tools / "show-environment").chmod(0o755)
    desktop.env.update(PATH=f"{tools}:{os.environ.get('PATH', '/usr/bin:/bin')}",
                       XCURSOR_PATH=str(root / "cursors"), XCURSOR_THEME="paw-base",
                       XCURSOR_SIZE="24", XDG_DATA_HOME=str(root / "data"),
                       XDG_DATA_DIRS=str(root / "none"))
    desktop.env.pop("PAW_USER_ICON_THEME", None)
    if login_session:
        desktop.env["PAW_LOGIN_SESSION"] = "1"
    else:
        desktop.env.pop("PAW_LOGIN_SESSION", None)

    def environment():
        """$XCURSOR_THEME, $XCURSOR_SIZE and $PAW_USER_ICON_THEME of a program started now."""
        seen.unlink(missing_ok=True)
        desktop.msg("spawn", str(tools / "show-environment"))
        desktop.wait_for(seen.exists, "the program that shows its environment")
        return seen.read_text().splitlines()

    def values():
        """The values gsettings holds that were set, by key."""
        return {path.name: path.read_text().strip() for path in store.iterdir()}

    return lambda: calls.read_text().splitlines(), values, environment


def theme(desktop):
    return desktop.rows("cursor_theme")


with harness.Compositor(compositor, start=False) as desktop:
    calls, _, environment = prepare(desktop, login_session=False)
    desktop.start(CONFIG)
    subscriber = desktop.subscribe()
    desktop.detail = lambda: f"cursor theme {theme(desktop)}, heard {subscriber.lines()}"
    # The session's own theme and size, as paw started.
    assert theme(desktop) == [["paw-base", "24"]], theme(desktop)
    desktop.wait_for(lambda: subscriber.last("cursor-theme ") == "cursor-theme 24 paw-base",
                     "subscribers told the session's cursor theme")
    assert environment() == ["paw-base", "24", "unset"]
    # The profile's, at its size, to subscribers and programs started from now on.
    desktop.msg("profile", "themed")
    desktop.wait_for(lambda: theme(desktop) == [["paw-profile", "32"]], "the profile's cursor")
    desktop.wait_for(lambda: subscriber.last("cursor-theme ") == "cursor-theme 32 paw-profile",
                     "subscribers told the profile's cursor theme")
    assert desktop.rows("cursor_size") == [["32"]], desktop.rows("cursor_size")
    assert environment() == ["paw-profile", "32", "unset"]
    # One that is not installed is skipped, saying so: the session's own comes back.
    desktop.msg("profile", "missing")
    desktop.wait_for(lambda: theme(desktop) == [["paw-base", "24"]], "the session's cursor back")
    assert "The cursor theme paw-no-such-theme is not installed; skipping it for paw-base" \
        in desktop.log.read_text()
    assert environment() == ["paw-base", "24", "unset"]
    # Only a login session tells applications through gsettings.
    assert calls() == [], calls()

with harness.Compositor(compositor, start=False) as desktop:
    calls, values, environment = prepare(desktop, login_session=True)
    desktop.detail = lambda: f"gsettings calls: {calls()}"
    desktop.start(CONFIG)
    desktop.stays(lambda: calls() == [], "gsettings ran for a profile without themes")
    desktop.msg("profile", "themed")
    TOLD = ["get org.gnome.desktop.interface cursor-theme",
            "set org.gnome.desktop.interface cursor-theme 'paw-profile'",
            "get org.gnome.desktop.interface cursor-size",
            "set org.gnome.desktop.interface cursor-size 32",
            "get org.gnome.desktop.interface icon-theme",
            "set org.gnome.desktop.interface icon-theme 'paw-icons'"]
    desktop.wait_for(lambda: sorted(calls()) == sorted(TOLD), "the profile's themes told")
    # A shell started now finds the user's own icon theme in $PAW_USER_ICON_THEME.
    assert environment() == ["paw-profile", "32", "UserIcons"]
    # A reload keeping them tells nothing again.
    desktop.reload()
    desktop.stays(lambda: len(calls()) == len(TOLD), "gsettings ran for no change")
    # A profile without them puts back what the user had, the size they never set by a reset
    # (not its default); a theme that is not installed is not told.
    desktop.msg("profile", "missing")
    BACK = ["set org.gnome.desktop.interface cursor-theme 'paw-user'",
            "reset org.gnome.desktop.interface cursor-size",
            "set org.gnome.desktop.interface icon-theme 'UserIcons'"]
    USER = {"cursor-theme": "'paw-user'", "icon-theme": "'UserIcons'"}
    desktop.wait_for(lambda: sorted(calls()[len(TOLD):]) == sorted(BACK),
                     "the user's own themes back")
    assert values() == USER, values()
    desktop.stays(lambda: len(calls()) == len(TOLD) + len(BACK),
                  "gsettings told a theme that is not installed")
    assert environment() == ["paw-base", "24", "unset"]
    # Picked again, they are told again, the user's own read anew (they may have changed them
    # meanwhile); as the session ends, those go back.
    desktop.msg("profile", "themed")
    desktop.wait_for(lambda: sorted(calls()[len(TOLD) + len(BACK):]) == sorted(TOLD),
                     "the themes told again")
    count = len(calls())
    desktop.stop()
    assert sorted(calls()[count:]) == sorted(BACK), calls()[count:]
    assert values() == USER, values()

# A session that does not end in order (killed here, as a crash or the machine stopping first
# ends one) leaves what it replaced on record, and the next login session puts that back as it
# starts, before it reads the desktop's cursor from gsettings. A theme gsettings already holds
# as the profile names it, left there by a session that kept no record (#32), is not taken for
# the user's own: it goes back to its default, and a shell started meanwhile hears that the user
# has no icon theme of their own there, to fall back on GTK's settings.ini.
with harness.Compositor(compositor, start=False) as desktop:
    calls, values, environment = prepare(desktop, login_session=True)
    desktop.detail = lambda: f"gsettings calls: {calls()}, holding {values()}"
    for name in ("XCURSOR_THEME", "XCURSOR_SIZE"):
        desktop.env.pop(name)
    (desktop.root / "gsettings/icon-theme").write_text("'paw-icons'\n")
    record = desktop.root / "state/paw/replaced-settings"
    desktop.start(CONFIG.replace('profile = "plain"', 'profile = "themed"'))
    desktop.wait_for(lambda: values() == {"cursor-theme": "'paw-profile'", "cursor-size": "32",
                                          "icon-theme": "'paw-icons'"},
                     "the profile's themes told")
    assert environment() == ["paw-profile", "32", ""]
    desktop.server.kill()
    desktop.server.wait()
    desktop.server = None
    assert record.exists()
    count = len(calls())
    desktop.start(CONFIG)
    assert sorted(calls()[count:count + 3]) == [
        "reset org.gnome.desktop.interface cursor-size",
        "reset org.gnome.desktop.interface icon-theme",
        "set org.gnome.desktop.interface cursor-theme 'paw-user'"], calls()[count:]
    assert values() == {"cursor-theme": "'paw-user'"}, values()
    assert theme(desktop) == [["paw-user", "24"]], theme(desktop)
    assert "The last session ended without putting back" in desktop.log.read_text()
    assert not record.exists()

# X11 windows get the theme's arrow: a switch before XWayland runs is given it as it is ready, and
# one while an X11 window shows at once.
with harness.Compositor(compositor, start=False) as desktop:
    prepare(desktop, login_session=False)
    desktop.start(CONFIG.replace("xwayland = false", "xwayland = true"))
    if not x11_probe or "DISPLAY" not in desktop.env:
        print("XWayland is not available: the X11 part is skipped")
    else:
        desktop.msg("profile", "themed")
        desktop.wait_for(lambda: theme(desktop) == [["paw-profile", "32"]], "the profile's cursor")
        x11 = desktop.spawn([x11_probe, "commands"], env={"PAW_PROBE_TITLE": "X11"},
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        assert x11.stdout.readline().strip() == "X11 window mapped and focused"
        desktop.msg("profile", "plain")
        desktop.wait_for(lambda: theme(desktop) == [["paw-base", "24"]], "the session's cursor")
        desktop.msg("profile", "themed")
        desktop.wait_for(lambda: theme(desktop) == [["paw-profile", "32"]], "the profile's again")
        assert "Cannot set xwm cursor" not in desktop.log.read_text()

print("A profile's cursor theme drew the cursor, reached programs and, in a login session, "
      "gsettings, which got the user's own back")
