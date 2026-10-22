# SPDX-License-Identifier: GPL-3.0-or-later
"""Processes left running without a window: a client whose connection breaks while its process
goes on (as CLion's did when the compositor restarted under it) is listed by `get windowless`
with its window's app ID, and subscribers hear it with the state, until it ends; `windowless kill
PID` ends it. A login session hands it to the next one, which lists it while it runs: a process
handed over that has ended, or whose PID another process has taken since, is not."""
import os
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

with harness.Compositor(compositor, "return { xwayland = false }",
                        env={"PAW_LOGIN_SESSION": "1"}) as desktop:
    msg = desktop.msg

    def windowless():
        return desktop.rows("windowless")

    def last_state(subscriber):
        """The lines of the last state the subscriber heard."""
        text = subscriber.text()
        return ("tiling " + text[:text.rfind("\n") + 1].split("tiling ")[-1]).splitlines()

    def open_window(title, app_id, commands=True):
        """A window of `app_id`, whose process takes commands on its standard input."""
        client = desktop.spawn([probe, *(["--commands"] if commands else ["--window-only"])],
                               env={"PAW_PROBE_TITLE": title, "PAW_PROBE_APP_ID": app_id},
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        desktop.wait_for(lambda: title in [w.title for w in desktop.windows()], f"{title} open")
        return client

    def disconnect(client):
        client.stdin.write("disconnect\n")
        client.stdin.flush()
        assert client.stdout.readline() == "disconnected\n"

    subscriber = desktop.subscribe()
    ide = open_window("CLion", "jetbrains-clion")
    assert windowless() == [], windowless()
    disconnect(ide)
    clion = [str(ide.pid), "jetbrains-clion"]
    desktop.wait_for(lambda: windowless() == [clion], "CLion listed without its window",
                     detail=windowless)
    desktop.wait_for(lambda: f"windowless {ide.pid} jetbrains-clion" in last_state(subscriber),
                     "subscribers told of it", detail=lambda: last_state(subscriber))

    # One that ends leaves the list by itself.
    other = open_window("Other", "paw-other")
    disconnect(other)
    desktop.wait_for(lambda: len(windowless()) == 2, "the other listed too", detail=windowless)
    other.stdin.close()
    assert desktop.reap(other) == 0
    desktop.wait_for(lambda: windowless() == [clion], "the other gone as it ended",
                     detail=windowless)

    # The next session knows CLion, still running, but not a window's process that ended with
    # the compositor, nor a PID handed over for a process that started at another time.
    kept = open_window("Kept", "paw-kept", commands=False)
    desktop.stop()
    desktop.reap(kept)
    handed = desktop.root / "paw-windowless"
    assert f"{ide.pid}\t" in handed.read_text(), handed.read_text()
    with handed.open("a") as file:
        file.write(f"{os.getpid()}\t1\tpaw-reused\n1\t1\tinit\n")
    desktop.start()
    subscriber = desktop.subscribe()
    desktop.wait_for(lambda: windowless() == [clion], "CLion listed after the restart",
                     detail=windowless)

    msg("windowless", "kill", str(ide.pid))
    assert desktop.reap(ide) == -9
    desktop.wait_for(lambda: windowless() == [], "the killed process gone", detail=windowless)
    desktop.wait_for(lambda: not any(line.startswith("windowless ")
                                     for line in last_state(subscriber)),
                     "subscribers told it is gone", detail=lambda: last_state(subscriber))
    assert "no process without a window" in msg("windowless", "kill", str(ide.pid), ok=False)
    assert "usage" in msg("windowless", "kill", ok=False)
print("A process left without a window is listed until it ends or is killed, across a restart")
