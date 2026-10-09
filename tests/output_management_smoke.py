# SPDX-License-Identifier: GPL-3.0-or-later
"""wlr-output-management: a client (as wlr-randr is) lists the outputs, tests and applies changes,
and a reload of the Lua configuration takes them back."""
from pathlib import Path
import signal
import subprocess
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

with harness.Compositor(compositor, "return { xwayland = false }",
                        env={"WLR_HEADLESS_OUTPUTS": "3"}) as desktop:
    def outputs():
        return {row[0]: (row[1] == "1", int(row[2]), int(row[3]), int(row[4]), int(row[5]),
                         float(row[6]), int(row[7])) for row in desktop.rows("outputs")}

    def randr(*args):
        result = subprocess.run([probe, *args], env=desktop.env, capture_output=True, text=True,
                                timeout=30)
        assert result.returncode == 0, result.stderr
        return result.stdout.split("\n")[:-1]

    heads = {line.split()[0]: line.split() for line in randr("list")}
    assert sorted(heads) == ["HEADLESS-1", "HEADLESS-2", "HEADLESS-3"], heads
    assert all(head[1] == "1" for head in heads.values()), heads
    before = outputs()

    # A test changes nothing.
    assert randr("test", "HEADLESS-2", "scale=2") == ["succeeded"]
    assert outputs() == before

    assert randr("apply", "HEADLESS-2", "scale=2", "transform=1") == ["succeeded"]
    state = outputs()
    assert state["HEADLESS-2"][5:7] == (2.0, 1), state
    # The change reaches clients listing the heads afterwards.
    heads = {line.split()[0]: line.split() for line in randr("list")}
    assert heads["HEADLESS-2"][4:6] == ["2", "1"], heads

    assert randr("apply", "HEADLESS-1", "x=3000", "y=40") == ["succeeded"]
    heads = {line.split()[0]: line.split() for line in randr("list")}
    assert sorted(heads) == ["HEADLESS-1", "HEADLESS-2", "HEADLESS-3"], heads
    state = outputs()
    assert state["HEADLESS-1"][2] == 40 and state["HEADLESS-1"][1] > state["HEADLESS-2"][1], state

    assert randr("apply", "HEADLESS-3", "enabled=0") == ["succeeded"]
    assert outputs()["HEADLESS-3"][0] is False
    assert randr("apply", "HEADLESS-3", "enabled=1") == ["succeeded"]
    assert outputs()["HEADLESS-3"][0] is True

    # Turning every output off is refused.
    assert randr("apply", "HEADLESS-1", "enabled=0") == ["succeeded"]
    assert randr("apply", "HEADLESS-2", "enabled=0") == ["succeeded"]
    assert randr("test", "HEADLESS-3", "enabled=0") == ["failed"]
    assert sum(enabled for enabled, *_ in outputs().values()) == 1

    # A reload restores what the configuration says.
    desktop.server.send_signal(signal.SIGHUP)
    desktop.wait_for(lambda: "Configuration reloaded" in desktop.log.read_text(), "reload")
    state = outputs()
    assert all(row[0] and row[5:7] == (1.0, 0) for row in state.values()), state
print("Output management test, apply, and reload passed")
