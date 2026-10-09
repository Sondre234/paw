# SPDX-License-Identifier: GPL-3.0-or-later
"""The lid as logind tells of it (LidClosed, from tests/fake_login1.c on a private bus), which
covers a lid closed before paw started, as libinput tells of one only when it knows the
switch to be reliable: a laptop started closed on a dock comes up with its panel off, and the lid
opening, as logind says, brings the panel back."""
import os
from pathlib import Path
import shutil
import signal
import sys

import harness

compositor, fake_login1 = (str(Path(p).resolve()) for p in sys.argv[1:3])
if not shutil.which("dbus-daemon"):
    print("dbus-daemon not found: logind's lid was not driven")
    sys.exit(0)

with harness.Compositor(compositor, "return { xwayland = false }", bus=True,
                        start=False) as desktop:
    root, env, msg, wait_for = desktop.root, desktop.env, desktop.msg, desktop.wait_for
    calls, answers = root / "login1.log", root / "answers"
    calls.touch()
    answers.write_text("LidClosed yes\n")
    env["PAW_LOGIN1_BUS"] = env["DBUS_SESSION_BUS_ADDRESS"]

    def enabled():
        return {r[0] for r in desktop.rows("outputs") if r[1] == "1"}

    def lid():
        return desktop.rows("switches")[0][1]

    login1 = desktop.spawn([fake_login1, env["DBUS_SESSION_BUS_ADDRESS"], str(calls),
                            str(answers)])
    wait_for(lambda: "ready" in calls.read_text(), "the fake logind")
    desktop.start()
    wait_for(lambda: lid() == "closed", "logind's lid closed")
    msg("headless_output", "add", "eDP-1")
    desktop.stays(lambda: enabled() == {"HEADLESS-1"}, "the panel lit up behind the closed lid")
    assert desktop.rows("switches") == [["lid", "closed"]], desktop.rows("switches")

    answers.write_text("LidClosed no\n")
    os.kill(login1.pid, signal.SIGUSR2)
    wait_for(lambda: enabled() == {"HEADLESS-1", "eDP-1"}, "the panel on as the lid opened")
    assert lid() == "open"
    answers.write_text("LidClosed yes\n")
    os.kill(login1.pid, signal.SIGUSR2)
    wait_for(lambda: enabled() == {"HEADLESS-1"}, "the panel off as the lid closed")
print("logind's LidClosed holds the panel off at startup and follows the lid")
