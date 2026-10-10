# SPDX-License-Identifier: GPL-3.0-or-later
"""The last session (session.restore), in a login session: a headless compositor with
PAW_LOGIN_SESSION=1 stands in for --session, on temporary XDG directories. It saves itself as
`last` as it ends, by quit or log out (before its windows are asked to close), and the next one puts
that back once startup and autostart have started what they start: with "windows", the default,
the windows those open again go where they were and nothing else starts; with "launch" the
programs of the other windows start again, and no second copy of one that startup or autostart
started; with "off", and in a session that is not a login session, nothing is saved or restored."""
import os
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])


def config(restore=None):
    return """return {
    xwayland = false,
    layout = { workspaces = 4 },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    startup = { { "app-a" } },
""" + (f'    session = {{ restore = "{restore}" }},\n' if restore else "") + "}"


with harness.Compositor(compositor, config(), start=False) as desktop:
    root, env, msg = desktop.root, desktop.env, desktop.msg
    tools = root / "bin"
    tools.mkdir()
    for name, title in (("app-a", "A"), ("app-d", "D")):
        # A program startup or autostart starts: its window has its own app ID and title.
        (tools / name).write_text(f'#!/bin/sh\nPAW_PROBE_APP_ID={name} '
                                  f'PAW_PROBE_TITLE={title} exec "{probe}" --window-only\n')
        (tools / name).chmod(0o755)
    (root / "config/autostart").mkdir(parents=True)
    (root / "config/autostart/d.desktop").write_text(
        "[Desktop Entry]\nType=Application\nName=D\nExec=app-d\n")
    (root / "sys").mkdir()
    env.update(XDG_CONFIG_HOME=str(root / "config"), XDG_CONFIG_DIRS=str(root / "sys"),
               PATH=f"{tools}:{os.environ.get('PATH', '/usr/bin:/bin')}",
               PAW_LOGIN_SESSION="1",
               # What the probes the compositor launches again call themselves.
               PAW_PROBE_APP_ID="app-b", PAW_PROBE_TITLE="B")
    last = root / "state/paw/sessions/last"

    def windows():
        """By title: workspace and the place it floats at."""
        return {w.title: dict(workspace=w.workspace, x=w.x, y=w.y) for w in desktop.windows()}

    def titles():
        return sorted(w.title for w in desktop.windows())

    desktop.detail = lambda: f"windows: {windows()}"

    def open_window(title):
        return desktop.open_window(probe, title, f"app-{title.lower()}")

    def move(title, workspace):
        subprocess.run([probe, "--activate", f"app-{title.lower()}"], env=env, check=True,
                       timeout=30, stdout=subprocess.DEVNULL)
        msg("move_to_workspace", str(workspace))
        desktop.wait_for(lambda: windows()[title]["workspace"] == workspace,
                         f"{title} on workspace {workspace}")

    def ended(clients):
        """The compositor ended by itself; the clients the test started go with it."""
        assert desktop.server.wait(timeout=30) == 0, desktop.log.read_text()
        desktop.stop()
        for client in clients:
            desktop.reap(client)

    def restored(line):
        return f"Restored the last session: {line}" in desktop.log.read_text()

    # The first login has no last session. startup opens A and autostart D; B and C are opened
    # by hand, and everything spread over the workspaces.
    desktop.start()
    desktop.wait_for(lambda: titles() == ["A", "D"], "startup's and autostart's windows")
    assert "Restored the last session" not in desktop.log.read_text()
    clients = [open_window("B"), open_window("C")]
    move("A", 3)
    move("D", 4)
    move("B", 2)
    saved = windows()
    msg("workspace", "2")
    # Quit saves it as it is.
    msg("quit")
    ended(clients)
    text = desktop.log.read_text()
    assert "Saved the session as last: 4 windows" in text, text
    assert last.read_text().count("\nwindow\t") == 4

    # restore = "windows": what startup and autostart open again goes where it was, the
    # workspace shown is the one that was; B and C, opened by hand, are not started.
    desktop.start()
    desktop.wait_for(lambda: restored("0 windows placed, 2 waited for from startup and "
                                      "autostart, 0 launched, 2 not found"), "the restore")
    desktop.wait_for(lambda: titles() == ["A", "D"], "A and D open again")
    desktop.wait_for(lambda: windows()["A"]["workspace"] == 3 and
                     windows()["D"]["workspace"] == 4, "A and D where they were")
    assert (windows()["A"]["x"], windows()["A"]["y"]) == (saved["A"]["x"], saved["A"]["y"]), \
        (saved, windows())
    assert msg("get", "workspace").strip() == "2"
    desktop.stays(lambda: titles() == ["A", "D"], "a window opened by hand was started")
    # B is opened by hand again; logging out saves before the windows are asked to close.
    clients = [open_window("B")]
    move("B", 2)
    msg("logout")
    ended(clients)
    text = desktop.log.read_text()
    assert "Saved the session as last: 3 windows" in text, text
    assert text.index("Saved the session as last") < text.index("Closing every window"), text

    # restore = "launch": B's program starts again and B goes back to workspace 2, while A and D,
    # which startup and autostart start, are waited for rather than started twice.
    desktop.start(config("launch"))
    desktop.wait_for(lambda: restored("0 windows placed, 2 waited for from startup and "
                                      "autostart, 1 launched, 0 not found"), "the restore")
    desktop.wait_for(lambda: titles() == ["A", "B", "D"], "A, B and D open again")
    desktop.wait_for(lambda: windows()["B"]["workspace"] == 2 and
                     windows()["A"]["workspace"] == 3 and windows()["D"]["workspace"] == 4,
                     "A, B and D where they were")
    desktop.stays(lambda: titles() == ["A", "B", "D"], "a program was started twice")
    msg("quit")
    ended([])

    # restore = "off" neither restores nor saves.
    before = last.read_text()
    desktop.start(config("off"))
    desktop.wait_for(lambda: titles() == ["A", "D"], "startup's and autostart's windows")
    clients = [open_window("C")]
    desktop.stays(lambda: titles() == ["A", "C", "D"], "something was restored")
    msg("quit")
    ended(clients)
    text = desktop.log.read_text()
    assert "Restored the last session" not in text and "Saved the session" not in text, text
    assert last.read_text() == before

    # Nor does a session that is not a login session, whatever session.restore says.
    del env["PAW_LOGIN_SESSION"]
    desktop.start(config("launch"))
    desktop.wait_for(lambda: titles() == ["A"], "startup's window")
    desktop.stays(lambda: titles() == ["A"], "something was restored")
    msg("quit")
    ended([])
    text = desktop.log.read_text()
    assert "Restored the last session" not in text and "Saved the session" not in text, text
    assert last.read_text() == before
print("session_restore_smoke passed")
