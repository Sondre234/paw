# SPDX-License-Identifier: GPL-3.0-or-later
"""Toggle dwindle tiling through the control socket and watch real windows follow it."""
from pathlib import Path
import socket
import sys

import harness

compositor, probe, example = (str(Path(p).resolve()) for p in sys.argv[1:4])
GAP = 8

with harness.Compositor(compositor, Path(example).read_text()
                        .replace("xwayland = true", "xwayland = false")) as desktop:
    msg, env = desktop.msg, desktop.env

    def windows():
        """(workspace, focused, tiled, x, y, width, height) per window, oldest first."""
        rows = desktop.rows("windows")
        return [(int(r[0]), r[1] == "1", r[3] == "1", *map(int, r[4:8])) for r in rows]

    desktop.detail = lambda: f"windows: {windows()}"

    def boxes(workspace=1):
        return sorted(w[3:] for w in windows() if w[0] == workspace)

    def side_by_side(left, right):
        return (left[1] == right[1] and left[3] == right[3] and
                left[0] + left[2] + GAP == right[0])

    events = b""

    def received(line):
        global events
        subscriber.setblocking(False)
        try:
            while chunk := subscriber.recv(4096):
                events += chunk
        except BlockingIOError:
            pass
        return line.encode() in events

    assert msg("get", "tiling") == "off\n"
    subscriber = socket.socket(socket.AF_UNIX)
    subscriber.connect(env["PAW_SOCKET"])
    subscriber.sendall(b"subscribe\n")
    desktop.wait_for(lambda: received("ok\ntiling off\nworkspace 1\n"), "subscription state")

    def launch():
        return desktop.spawn([probe, "--external-control"])

    launch()
    desktop.wait_for(lambda: len(windows()) == 1, "first window")
    assert not windows()[0][2], "tiled while tiling is off"
    floating = boxes()[0]
    assert floating[2:] == (320, 240), floating

    msg("toggle_tiling")
    desktop.wait_for(lambda: received("tiling on\n"), "tiling on event")
    assert msg("get", "tiling") == "on\n"
    desktop.wait_for(lambda: windows()[0][2] and boxes()[0][2] > 320,
                     "single window fills the output")
    full = boxes()[0]
    assert full[0] == 0 and full[1] == 0, full  # a lone tile has no gaps (layout.smart_gaps)

    # A new window splits the focused one; both share the height.
    launch()
    desktop.wait_for(lambda: len(boxes()) == 2 and side_by_side(*boxes()),
                     "second window tiled beside the first")
    left, right = boxes()
    assert left[0] == GAP and right[0] + right[2] == full[0] + full[2] - GAP, boxes()
    assert all(w[2] for w in windows())

    # Moving a tile to another workspace gives the space back and tiles it there.
    msg("move_to_workspace", "2")
    # Every probe adds a test panel, so compare across the width only.
    def fills(workspace=1):
        found = boxes(workspace)
        return len(found) == 1 and found[0][0] == 0 and found[0][2] == full[2]
    desktop.wait_for(fills, "remaining window refilled")
    assert [w[2] for w in windows() if w[0] == 2] == [True]
    msg("workspace", "2")
    desktop.wait_for(lambda: fills(2), "moved window fills workspace 2")
    msg("workspace", "1")

    launch()
    desktop.wait_for(lambda: len(boxes()) == 2 and side_by_side(*boxes()),
                     "third window tiled on workspace 1")
    launch()
    desktop.wait_for(lambda: len(boxes()) == 3 and all(w[2] for w in windows()) and
                     harness.disjoint(boxes()),
                     "fourth window split a tile")

    # Floating the focused window returns it to its floating size and reflows the rest.
    msg("toggle_floating")
    desktop.wait_for(lambda: sorted(b[2:] for b in boxes())[0] == (320, 240) and
                     sum(w[2] for w in windows() if w[0] == 1) == 2,
                     "focused window floats")
    msg("toggle_floating")
    desktop.wait_for(lambda: sum(w[2] for w in windows() if w[0] == 1) == 3,
                     "window tiled again")

    msg("toggle_tiling")
    desktop.wait_for(lambda: received("tiling off\n"), "tiling off event")
    desktop.wait_for(lambda: not any(w[2] for w in windows()) and
                     all(b[2:] == (320, 240) for b in boxes() + boxes(2)),
                     "floating sizes restored")

    # Toggling faster than the clients answer must not save a tile as the floating size.
    floating = boxes() + boxes(2)
    msg("toggle_tiling")
    desktop.wait_for(lambda: all(w[2] for w in windows()) and
                     all(b[2:] != (320, 240) for b in boxes() + boxes(2)), "tiled again")
    # Off and on within one dispatch: no client has committed its floating size yet.
    burst = [socket.socket(socket.AF_UNIX) for _ in range(2)]
    for connection in burst:
        connection.connect(env["PAW_SOCKET"])
    for connection in burst:
        connection.sendall(b"toggle_tiling\n")
    for connection in burst:
        connection.recv(64)
        connection.close()
    msg("toggle_tiling")
    desktop.wait_for(lambda: not any(w[2] for w in windows()) and
                     boxes() + boxes(2) == floating,
                     "floating geometry kept across rapid toggles")

    subscriber.close()
    msg("workspace", "2")  # Notifying a closed subscriber must not hurt the server.
print("Tiling toggle, dwindle splits, directional focus, workspaces, floating, and "
      "subscription passed")
