# SPDX-License-Identifier: GPL-3.0-or-later
"""packaging/paw-session with a stand-in paw (and dbus-run-session) on PATH: it passes
its arguments on after --session, logs to $XDG_STATE_HOME/paw/session.log keeping the last
log as session.log.old, and starts a session bus only where there is none."""
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile

script = str(Path(sys.argv[1]).resolve())

with tempfile.TemporaryDirectory(prefix="paw-session-") as directory:
    root = Path(directory)
    tools = root / "bin"
    tools.mkdir()
    # Only the stand-ins and the few programs the script uses are on PATH.
    for name in ("dirname", "mkdir", "mv", "date"):
        (tools / name).symlink_to(shutil.which(name))
    (tools / "paw").write_text(
        '#!/bin/sh\necho "paw $* bus=${DBUS_SESSION_BUS_ADDRESS:-none}"\n'
        'echo "to stderr" >&2\nexit 3\n')
    bus_starter = tools / "dbus-run-session"
    bus_starter.write_text('#!/bin/sh\n[ "$1" = -- ] && shift\n'
                           'DBUS_SESSION_BUS_ADDRESS=unix:path=/private exec "$@"\n')
    for program in (tools / "paw", bus_starter):
        program.chmod(0o755)
    state = root / "state"
    log = state / "paw/session.log"
    runtime = root / "run"
    runtime.mkdir()

    def run(*arguments, **variables):
        env = {"PATH": str(tools), "HOME": str(root / "home"), "XDG_STATE_HOME": str(state),
               "XDG_RUNTIME_DIR": str(runtime), **variables}
        env = {name: value for name, value in env.items() if value is not None}
        result = subprocess.run([script, *arguments], env=env, capture_output=True, text=True,
                                timeout=30)
        assert result.returncode == 3, (result.returncode, result.stdout, result.stderr)
        assert not result.stdout and not result.stderr, (result.stdout, result.stderr)
        return log.read_text().splitlines()

    # With a bus, paw runs on it, with the arguments; both its outputs reach the log.
    lines = run("--config", "my init.lua", DBUS_SESSION_BUS_ADDRESS="unix:path=/user")
    assert lines[0].startswith("paw-session: starting "), lines
    assert lines[1:] == ["paw --session --config my init.lua bus=unix:path=/user",
                         "to stderr"], lines

    # Without one, dbus-run-session starts one; the last log is kept.
    first = log.read_text()
    lines = run()
    assert lines[1] == "paw --session bus=unix:path=/private", lines
    assert (state / "paw/session.log.old").read_text() == first

    # A user bus at $XDG_RUNTIME_DIR/bus serves as it is.
    user_bus = socket.socket(socket.AF_UNIX)
    user_bus.bind(str(runtime / "bus"))
    assert run()[1] == "paw --session bus=none"
    user_bus.close()
    (runtime / "bus").unlink()

    # Without dbus-run-session, paw runs all the same.
    bus_starter.unlink()
    assert run()[1] == "paw --session bus=none"

    # Without XDG_STATE_HOME the log is in ~/.local/state.
    run(XDG_STATE_HOME=None)
    assert (root / "home/.local/state/paw/session.log").exists()

    # A paw found beside the script, not on PATH, as an installed one is.
    beside = root / "installed"
    beside.mkdir()
    shutil.copy(script, beside / "paw-session")
    (tools / "paw").rename(beside / "paw")
    script = str(beside / "paw-session")
    assert run()[1] == "paw --session bus=none"

    # A log directory that cannot be made leaves the output where it was going.
    blocked = root / "blocked"
    blocked.write_text("")
    env = {"PATH": str(tools), "XDG_STATE_HOME": str(blocked)}
    result = subprocess.run([script], env=env, capture_output=True, text=True, timeout=30)
    assert result.returncode == 3 and "paw --session" in result.stdout, result
    print("paw-session passes its arguments on, logs, and starts a bus only where needed")
