# SPDX-License-Identifier: GPL-3.0-or-later
"""The idle suspend step against a fake logind (tests/fake_login1.c) on a private bus: after
`idle.suspend` seconds without input, the monitors having gone off before, the screen locks and
logind is asked to suspend, as the suspend action does; when the machine wakes up the monitors
come on again and the steps count from then."""
import os
from pathlib import Path
import re
import shutil
import signal
import sys

import harness

compositor, fake_login1, lock_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])
if not shutil.which("dbus-daemon"):
    print("dbus-daemon not found: the idle suspend step was not driven")
    sys.exit(0)

CONFIG = """return {{
    xwayland = false,
    power = {{ lock_command = {{ [[{locker}]], "hold", [[{log}]] }} }},
    idle = {{ display_off = 1, suspend = 2 }},
}}"""

with harness.Compositor(compositor, bus=True, start=False) as desktop:
    root, env, wait_for = desktop.root, desktop.env, desktop.wait_for
    calls, answers, sysfs = root / "login1.log", root / "answers", root / "sys"
    calls.touch()
    answers.write_text("")
    (sysfs / "class" / "power_supply").mkdir(parents=True)
    env["PAW_SYSFS"] = str(sysfs)
    env["PAW_LOGIN1_BUS"] = env["DBUS_SESSION_BUS_ADDRESS"]
    desktop.config.write_text(CONFIG.format(locker=lock_probe, log=calls))
    desktop.detail = lambda: f"logind: {calls.read_text().splitlines()}"

    def logged():
        return calls.read_text().splitlines()

    desktop.spawn([fake_login1, env["DBUS_SESSION_BUS_ADDRESS"], str(calls), str(answers)])
    wait_for(lambda: "ready" in logged(), "the fake logind")
    desktop.start()
    wait_for(lambda: dict(desktop.rows("power"))["suspend"] == "yes", "logind's answers")

    # The monitors go off, then the screen locks and the machine suspends and wakes up.
    wait_for(lambda: "wake" in logged(), "the machine slept and woke up")
    # With every monitor off nothing shows, so the lock holds as soon as it starts and logind is
    # asked at once, maybe before the locker has drawn (and logged) anything.
    wait_for(lambda: "locked" in [line.split()[0] for line in logged()], "the locker locked")
    events = [line.split()[0] for line in logged()]
    assert events.index("Suspend") < events.index("wake"), events
    wait_for(lambda: "Turned on the monitors the idle steps turned off" in desktop.log.read_text(),
             "the monitors on after waking up")
    text = desktop.log.read_text()
    order = ["Idle: turning the monitors off", "Idle: suspending", "Session locked",
             "Asking logind to suspend", "The machine woke up",
             "Turned on the monitors the idle steps turned off"]
    assert [text.index(line) for line in order] == sorted(text.index(line) for line in order), text
    os.kill(int(re.findall(r"^locked (\d+)$", calls.read_text(), re.M)[-1]), signal.SIGTERM)
    wait_for(lambda: "Session unlocked" in desktop.log.read_text(), "the session unlocked")
print("The idle suspend step locks, suspends through logind, and wakes the monitors after")
