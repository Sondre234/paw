# SPDX-License-Identifier: GPL-3.0-or-later
"""XDG autostart in a login session (a headless compositor with PAW_LOGIN_SESSION=1 stands in
for --session): the entries of temporary XDG directories are started after `startup`, the user's
file hiding the system's of the same name, with Exec split and Path honoured, and the others are
skipped for the reason `get autostart` gives. A reload starts nothing again, and a compositor that
is not a login session, or autostart.xdg = false, starts none."""
import os
from pathlib import Path
import sys

import harness

compositor = str(Path(sys.argv[1]).resolve())

CONFIG = """return {
    xwayland = false,
    startup = { { "record", "startup" } },
    autostart = { exclude = { "excluded.desktop" } },
}"""

with harness.Compositor(compositor, CONFIG, start=False) as desktop:
    root, env = desktop.root, desktop.env
    tools, log = root / "bin", root / "started.log"
    tools.mkdir()
    log.touch()
    for name in ("record", "dunst"):
        # Records its name, its working directory and its arguments.
        (tools / name).write_text(f'#!/bin/sh\necho "{name} $PWD $*" >> "$RECORD_LOG"\n')
        (tools / name).chmod(0o755)
    user, system, extra = root / "config", root / "sys", root / "sys2"
    work = root / "work"
    work.mkdir()

    def entry(directory, name, *lines):
        (directory / "autostart").mkdir(parents=True, exist_ok=True)
        text = "[Desktop Entry]\nType=Application\nName=" + name + "\n" + "\n".join(lines) + "\n"
        (directory / "autostart" / name).write_text(text)

    entry(user, "mine.desktop", "Exec=record mine %U")
    entry(user, "both.desktop", "Exec=record user-copy")
    entry(system, "both.desktop", "Exec=record system-copy")
    entry(extra, "both.desktop", "Exec=record extra-copy")
    entry(user, "hidden.desktop", "Hidden=true")
    entry(system, "hidden.desktop", "Exec=record hidden")
    entry(system, "gnome.desktop", "Exec=record gnome", "OnlyShowIn=GNOME;")
    entry(system, "ours.desktop", "Exec=record ours", "OnlyShowIn=GNOME;paw;")
    entry(system, "notours.desktop", "Exec=record notours", "NotShownIn=paw;")
    entry(system, "disabled.desktop", "Exec=record disabled", "X-GNOME-Autostart-enabled=false")
    entry(system, "tryexec.desktop", "Exec=record tryexec", "TryExec=no-such-program-at-all")
    entry(system, "excluded.desktop", "Exec=record excluded")
    entry(extra, "path.desktop", "Exec=record in-path", f"Path={work}")
    entry(extra, "quoted.desktop", "Exec=record \"two words\" 'single quoted' 50%%")
    entry(extra, "missing.desktop", "Exec=no-such-program-at-all --flag")
    entry(extra, "terminal.desktop", "Exec=record terminal", "Terminal=true")
    entry(extra, "dunst.desktop", "Exec=dunst")
    env.update(XDG_CONFIG_HOME=str(user), XDG_CONFIG_DIRS=f"{system}:relative:{extra}",
               PATH=f"{tools}:{os.environ.get('PATH', '/usr/bin:/bin')}",
               RECORD_LOG=str(log), PAW_LOGIN_SESSION="1")

    def started():
        return sorted(log.read_text().splitlines())

    def report():
        return {row[0]: row[1:] for row in desktop.rows("autostart")}

    desktop.detail = lambda: f"started: {started()}; report: {desktop.msg('get', 'autostart')}"
    desktop.start()
    expected = sorted([
        f"record {os.getcwd()} startup",
        f"record {os.getcwd()} mine",
        f"record {os.getcwd()} user-copy",
        f"record {os.getcwd()} ours",
        f"record {work} in-path",
        f"record {os.getcwd()} two words single quoted 50%",
        # Nothing here is the shell's notification daemon: a headless compositor runs no shell.
        f"dunst {os.getcwd()} ",
    ])
    desktop.wait_for(lambda: started() == expected, "the autostart entries started")
    rows = report()
    assert sorted(rows) == sorted([
        "both.desktop", "disabled.desktop", "dunst.desktop", "excluded.desktop", "gnome.desktop",
        "hidden.desktop", "mine.desktop", "missing.desktop", "notours.desktop", "ours.desktop",
        "path.desktop", "quoted.desktop", "terminal.desktop", "tryexec.desktop"]), rows
    assert rows["mine.desktop"] == ["started", "record mine",
                                    str(user / "autostart/mine.desktop")], rows
    assert rows["both.desktop"] == ["started", "record user-copy",
                                    str(user / "autostart/both.desktop")], rows
    assert rows["quoted.desktop"][:2] == ["started", "record two words single quoted 50%"], rows
    assert rows["path.desktop"][:2] == ["started", "record in-path"], rows
    assert rows["hidden.desktop"] == ["skipped", "Hidden=true",
                                      str(user / "autostart/hidden.desktop")], rows
    assert rows["gnome.desktop"][:2] == ["skipped", "OnlyShowIn=GNOME;"], rows
    assert rows["notours.desktop"][:2] == ["skipped", "NotShownIn=paw;"], rows
    assert rows["disabled.desktop"][:2] == ["skipped", "X-GNOME-Autostart-enabled=false"], rows
    assert rows["tryexec.desktop"][:2] == [
        "skipped", "TryExec no-such-program-at-all is not installed"], rows
    assert rows["excluded.desktop"][:2] == ["skipped", "excluded by autostart.exclude"], rows
    assert rows["terminal.desktop"][0] == "skipped", rows
    assert rows["missing.desktop"][:2] == [
        "failed", "cannot launch no-such-program-at-all: No such file or directory"], rows
    log_text = desktop.log.read_text()
    assert "Autostart missing.desktop: cannot launch no-such-program-at-all" in log_text, log_text
    assert "XDG autostart: started 6, skipped 7" in log_text, log_text

    # A reload starts nothing again.
    desktop.reload()
    desktop.stays(lambda: started() == expected, "a reload started something again")
    desktop.stop()

    # Not a login session: autostart is the host's business.
    log.write_text("")
    del env["PAW_LOGIN_SESSION"]
    desktop.start()
    desktop.wait_for(lambda: started() == [f"record {os.getcwd()} startup"], "startup alone")
    desktop.stays(lambda: len(started()) == 1, "a session that is not a login session autostarted")
    assert desktop.msg("get", "autostart") == ""
    desktop.stop()

    # autostart.xdg = false.
    log.write_text("")
    env["PAW_LOGIN_SESSION"] = "1"
    desktop.start(CONFIG.replace("autostart = {", "autostart = { xdg = false,"))
    desktop.wait_for(lambda: started() == [f"record {os.getcwd()} startup"], "startup alone")
    desktop.stays(lambda: len(started()) == 1, "autostart ran with xdg = false")
    assert desktop.msg("get", "autostart") == ""
print("autostart_smoke passed")
