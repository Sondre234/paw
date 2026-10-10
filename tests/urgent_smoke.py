# SPDX-License-Identifier: GPL-3.0-or-later
"""Windows that ask for attention (xdg-activation) while unfocused: windows.activation decides
between marking them urgent, focusing them and ignoring the request; `get urgent`, the
subscription and focus_urgent report and use the marks; and focusing clears them. The border's
pulse is urgent_border_smoke's."""
from pathlib import Path
import re
import socket
import subprocess
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])


def settings(activation):
    return f"""return {{
    xwayland = false,
    layout = {{ tiling = false }},
    windows = {{ activation = "{activation}" }},
}}"""


with harness.Compositor(compositor, settings("urgent")) as desktop:
    msg, rows, wait_for = desktop.msg, desktop.rows, desktop.wait_for

    def urgent():
        """(app_id, workspace, focused) of each urgent window, longest waiting first."""
        return [(r[8], int(r[0]), r[1] == "1") for r in rows("urgent")]

    def focused():
        return [r[8] for r in rows("windows") if r[1] == "1"]

    desktop.detail = lambda: f"urgent {urgent()} focused {focused()}"

    def start(app_id):
        client = desktop.spawn([probe, "--commands"],
                               env={"PAW_PROBE_APP_ID": app_id,
                                    "PAW_PROBE_TITLE": f"Window {app_id}"},
                               stdin=subprocess.PIPE, text=True)
        wait_for(lambda: focused() == [app_id], f"{app_id} mapped and focused")
        return client

    def ask(client):
        client.stdin.write("activate\n")
        client.stdin.flush()

    class Subscriber:
        """The control socket's state stream: the last complete state received."""

        def __init__(self):
            self.socket = socket.socket(socket.AF_UNIX)
            self.socket.connect(desktop.env["PAW_SOCKET"])
            self.socket.sendall(b"subscribe\n")
            self.socket.settimeout(0.05)
            self.buffer = ""

        def state(self):
            try:
                while data := self.socket.recv(8192):
                    self.buffer += data.decode()
            except socket.timeout:
                pass
            blocks = self.buffer.split("tiling ")
            return "tiling " + blocks[-1] if len(blocks) > 1 else ""

        def received(self, text):
            """Whether text has arrived at any point, after reading what is waiting."""
            self.state()
            return text in self.buffer

    assert "xdg_activation_v1" in subprocess.run(
        [probe, "--globals"], env=desktop.env, capture_output=True, text=True).stdout
    assert urgent() == []
    subscriber = Subscriber()
    wait_for(lambda: "urgent 0" in subscriber.state(), "first state")

    # By default a request marks the window and leaves focus where it is.
    a = start("urgent-a")
    b = start("urgent-b")
    ask(a)
    wait_for(lambda: urgent() == [("urgent-a", 1, False)], "a marked urgent")
    assert focused() == ["urgent-b"]
    wait_for(lambda: "urgent 1\n" in subscriber.state(), "the subscription reports the count")
    # The window switcher marks it too.
    msg("switcher")
    wait_for(lambda: subscriber.received("switcher-window"), "the switcher's list")
    listed = {f[0]: f for f in (l[len("switcher-window "):].split("\t")
                                for l in subscriber.buffer.splitlines()
                                if l.startswith("switcher-window "))}
    assert listed["urgent-a"][5] == "1" and listed["urgent-b"][5] == "0", listed
    msg("switcher_cancel")
    state = subscriber.state()
    assert "urgent-output " in state and re.search(r"urgent-output \S+ 1\n", state), state
    assert re.search(r"urgent-window \S+\t1\turgent-a\tWindow urgent-a\n", state), state

    # Asking again changes nothing; a focused window is never urgent.
    ask(a)
    ask(b)
    desktop.stays(lambda: urgent() == [("urgent-a", 1, False)], "asking again changed it")

    # The window that waited longest is first, and focus_urgent takes them in order.
    c = start("urgent-c")
    ask(b)
    wait_for(lambda: [u[0] for u in urgent()] == ["urgent-a", "urgent-b"], "a then b")
    msg("focus_urgent")
    assert focused() == ["urgent-a"] and [u[0] for u in urgent()] == ["urgent-b"]
    wait_for(lambda: "urgent 1\n" in subscriber.state() and
             "urgent-window" in subscriber.state() and
             "urgent-a" not in subscriber.state().split("urgent-window")[-1],
             "focusing clears a's mark in the stream")
    msg("focus_urgent")
    assert focused() == ["urgent-b"] and urgent() == []
    msg("focus_urgent")  # nothing urgent: nothing happens
    assert focused() == ["urgent-b"]
    wait_for(lambda: "urgent 0\n" in subscriber.state() and
             "urgent-window" not in subscriber.state(), "all clear in the stream")

    # focus_urgent switches workspace, and the indicator's data names the workspace.
    msg("workspace", "2")
    d = start("urgent-d")
    ask(a)
    wait_for(lambda: urgent() == [("urgent-a", 1, False)], "a urgent on workspace 1")
    assert int(msg("get", "workspace")) == 2 and focused() == ["urgent-d"]
    wait_for(lambda: re.search(r"urgent-output \S+ 1\n", subscriber.state()),
             "workspace 1 is urgent while 2 is shown")
    msg("focus_urgent")
    assert int(msg("get", "workspace")) == 1 and focused() == ["urgent-a"]
    assert urgent() == []

    # Focusing a window any other way clears it too.
    msg("workspace", "2")
    ask(a)
    wait_for(lambda: urgent() == [("urgent-a", 1, False)], "a urgent again")
    subprocess.run([probe, "--activate", "urgent-a"], env=desktop.env, check=True, timeout=5,
                   stdout=subprocess.DEVNULL)
    wait_for(lambda: urgent() == [] and focused() == ["urgent-a"],
             "activation from the taskbar clears the mark")

    # A window that closes while urgent leaves nothing behind.
    msg("workspace", "2")
    ask(a)
    wait_for(lambda: len(urgent()) == 1, "a urgent a third time")
    subprocess.run([probe, "--close", "urgent-a"], env=desktop.env, check=True, timeout=5,
                   stdout=subprocess.DEVNULL)
    # The probe exits once its window has closed, which may be before the mark is
    # checked, so only the others must keep running meanwhile.
    harness.wait_for(lambda: urgent() == [], [p for p in desktop.processes if p is not a],
                     "closing clears the mark")
    assert desktop.reap(a, timeout=5) == 0
    wait_for(lambda: "urgent 0\n" in subscriber.state(), "the stream is clear")

    # focus: the request focuses the window and switches workspace; ignore: nothing.
    desktop.reload(settings("focus"))
    wait_for(lambda: "Configuration reloaded" in desktop.log.read_text(), "reload")
    msg("workspace", "1")
    focus_before = focused()
    ask(c)
    wait_for(lambda: focused() == ["urgent-c"], "focus policy focuses the window")
    assert urgent() == [] and focus_before != ["urgent-c"]
    # ... also from another workspace.
    msg("workspace", "4")
    e = start("urgent-e")
    assert int(msg("get", "workspace")) == 4
    ask(c)
    wait_for(lambda: focused() == ["urgent-c"], "focus policy switches to the workspace")
    assert int(msg("get", "workspace")) != 4 and urgent() == []
    desktop.reload(settings("ignore"))
    wait_for(lambda: desktop.log.read_text().count("Configuration reloaded") == 2, "second reload")
    ask(b)
    desktop.stays(lambda: urgent() == [] and focused() == ["urgent-c"],
                  "an ignored request did something", duration=.4)
    desktop.reload(settings("urgent"))
    wait_for(lambda: desktop.log.read_text().count("Configuration reloaded") == 3, "third reload")
    ask(b)
    wait_for(lambda: [u[0] for u in urgent()] == ["urgent-b"], "urgent again after reload")
    msg("focus_urgent")

    # A window hidden in the scratchpad that asks for attention comes back with
    # focus_urgent, on the workspace being shown.
    wait_for(lambda: focused() == ["urgent-b"], "b focused")
    msg("move_to_scratchpad")
    wait_for(lambda: [r[2] for r in rows("windows") if r[8] == "urgent-b"] == ["1"],
             "b hidden in the scratchpad")
    msg("workspace", "3")
    ask(b)
    wait_for(lambda: [u[0] for u in urgent()] == ["urgent-b"], "hidden b is urgent")
    msg("focus_urgent")
    wait_for(lambda: focused() == ["urgent-b"] and urgent() == [], "b shown by focus_urgent")
    assert [r[2] for r in rows("windows") if r[8] == "urgent-b"] == ["0"]
print("Activation requests mark, focus, or are ignored by policy; focus_urgent works")
