# SPDX-License-Identifier: GPL-3.0-or-later
"""Each of two headless outputs has its own workspaces."""
from pathlib import Path
import signal
import socket
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

# HEADLESS-1 starts at the layout origin, where the pointer is. Making HEADLESS-2 primary
# later moves it under the pointer, so the next window opens there.
CONFIG = """return {
    xwayland = false,
    layout = { workspaces = 4, tiling = true },
    outputs = {
        primary = "%s",
        monitors = { ["HEADLESS-1"] = { mode = "1280x720" },
                     ["HEADLESS-2"] = { mode = "1280x720" } },
    },
}"""

with harness.Compositor(compositor, CONFIG % "HEADLESS-1",
                        env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    msg, log = desktop.msg, desktop.log

    def workspaces():
        return {row[0]: (int(row[1]), row[2] == "1", row[3]) for row in desktop.rows("workspaces")}

    def windows():
        """(workspace, output, visible) per window, in mapping order."""
        return [(int(row[0]), row[10], row[11] == "1") for row in desktop.rows("windows")]

    def current():
        return {name: state[0] for name, state in workspaces().items()}

    # A subscriber sees each output's workspace and those holding windows.
    stream = socket.socket(socket.AF_UNIX)
    stream.connect(desktop.env["PAW_SOCKET"])
    stream.sendall(b"subscribe\n")
    stream.settimeout(.05)
    received = []

    def streamed(line):
        try:
            received.append(stream.recv(65536).decode())
        except socket.timeout:
            pass
        return line in "".join(received).splitlines()

    # Both outputs start on workspace 1.
    assert workspaces() == {"HEADLESS-1": (1, True, "-"), "HEADLESS-2": (1, False, "-")}
    desktop.wait_for(lambda: streamed("output HEADLESS-2 1 - on"), "initial state")
    assert "output HEADLESS-1 1 - on" in "".join(received)
    assert "no such output" in msg("output", "BOGUS-1", "workspace", "2", ok=False)
    assert "needs a name" in msg("output", "HEADLESS-1", ok=False)

    desktop.spawn([probe, "--external-control"])
    desktop.wait_for(lambda: windows() == [(1, "HEADLESS-1", True)],
                     "first window on HEADLESS-1", detail=windows)
    desktop.wait_for(lambda: streamed("output HEADLESS-1 1 1 on"), "occupied stream")

    desktop.config.write_text(CONFIG % "HEADLESS-2")
    desktop.server.send_signal(signal.SIGHUP)
    desktop.wait_for(lambda: "Configuration reloaded" in log.read_text(), "reload")
    desktop.spawn([probe, "--external-control"])
    desktop.wait_for(lambda: len(windows()) == 2, "second window")
    assert windows() == [(1, "HEADLESS-1", True), (1, "HEADLESS-2", True)], windows()
    # Actions follow the focused window's output.
    assert workspaces()["HEADLESS-2"] == (1, True, "1")

    # Switching one output leaves the other alone.
    msg("output", "HEADLESS-1", "workspace", "2")
    assert current() == {"HEADLESS-1": 2, "HEADLESS-2": 1}
    assert windows() == [(1, "HEADLESS-1", False), (1, "HEADLESS-2", True)], windows()
    desktop.wait_for(lambda: streamed("output HEADLESS-1 2 1 on"), "switch stream")
    assert msg("get", "workspace") == "1\n", "reported another output's workspace"

    # Without a named output, actions switch the focused one.
    msg("workspace", "3")
    assert current() == {"HEADLESS-1": 2, "HEADLESS-2": 3}
    assert windows() == [(1, "HEADLESS-1", False), (1, "HEADLESS-2", False)], windows()
    msg("workspace_prev")
    msg("workspace_prev")
    assert current() == {"HEADLESS-1": 2, "HEADLESS-2": 1}
    assert windows()[1] == (1, "HEADLESS-2", True)
    msg("output", "HEADLESS-1", "workspace_prev")
    assert current() == {"HEADLESS-1": 1, "HEADLESS-2": 1}
    assert windows() == [(1, "HEADLESS-1", True), (1, "HEADLESS-2", True)], windows()

    # A window moved to another workspace stays on its output.
    msg("move_to_workspace", "4")
    assert windows() == [(1, "HEADLESS-1", True), (4, "HEADLESS-2", False)], windows()
    assert workspaces()["HEADLESS-2"][2] == "4"
    assert current() == {"HEADLESS-1": 1, "HEADLESS-2": 1}
    msg("output", "HEADLESS-2", "workspace_prev")
    assert current() == {"HEADLESS-1": 1, "HEADLESS-2": 4}
    assert windows() == [(1, "HEADLESS-1", True), (4, "HEADLESS-2", True)], windows()

    # Floating a tile keeps it on the output it is on (see #8), on its workspace.
    msg("focus_right")
    assert workspaces()["HEADLESS-1"][1], "focus did not move to HEADLESS-1"
    msg("toggle_floating")
    assert sorted(windows()) == [(1, "HEADLESS-1", True), (4, "HEADLESS-2", True)], windows()
    msg("toggle_floating")

    # A window placed on another output joins that output's current workspace: turning
    # HEADLESS-1 off moves its tile into HEADLESS-2's tiling.
    desktop.config.write_text((CONFIG % "HEADLESS-2").replace(
        '["HEADLESS-1"] = { mode = "1280x720" }', '["HEADLESS-1"] = { enabled = false }'))
    desktop.server.send_signal(signal.SIGHUP)
    desktop.wait_for(lambda: log.read_text().count("Configuration reloaded") == 2,
                     "second reload")
    desktop.wait_for(lambda: windows() == [(4, "HEADLESS-2", True)] * 2,
                     "tile joined HEADLESS-2's workspace", detail=windows)
    assert workspaces()["HEADLESS-2"][0::2] == (4, "4"), workspaces()
print("Per-output workspaces, targeted actions, and the state stream passed")
