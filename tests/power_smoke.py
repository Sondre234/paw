# SPDX-License-Identifier: GPL-3.0-or-later
"""The power actions against a fake logind (tests/fake_login1.c) on a private bus, a dbus-daemon
this test starts and kills: the compositor reaches it through PAW_LOGIN1_BUS and never the
machine's own logind, and a headless compositor without that variable has no logind at all."""
import os
from pathlib import Path
import re
import shutil
import signal
import sys

import harness

compositor, fake_login1, probe, lock_probe = (str(Path(p).resolve()) for p in sys.argv[1:5])
if not shutil.which("dbus-daemon"):
    print("dbus-daemon not found: the power actions were not driven")
    sys.exit(0)

# close_timeout is LONG wherever the windows close, so that one slow to go under load is not
# taken for one that stays, and SHORT where one stays and the test waits that out.
CONFIG = """return {{
    xwayland = false,
    power = {{ lock_command = {locker}, lock_before_sleep = {before}, close_windows = {close},
              close_timeout = {timeout}, force = {force} }},
}}"""
LONG, SHORT = 10000, 1000
# The time a locker gets to lock (5 s outside tests), waited out once.
LOCK_TIMEOUT = 2

with harness.Compositor(compositor, bus=True, start=False,
                        env={"PAW_TEST_LOCK_TIMEOUT_MS": str(LOCK_TIMEOUT * 1000)}) as desktop:
    root, config, env, msg = desktop.root, desktop.config, desktop.env, desktop.msg
    calls, answers = root / "login1.log", root / "answers"
    calls.touch()
    # The locker writes "locked PID" and "unlocked" to the same log as the fake logind, so the
    # order of locking and sleeping shows in one place.
    LOCKER = f'{{ [[{lock_probe}]], "hold", [[{calls}]] }}'
    WAITING = f'{{ [[{lock_probe}]], "hold", [[{calls}]], "wait" }}'  # locks on SIGUSR1
    config.write_text(CONFIG.format(locker=LOCKER, before="true", close="true", force="false",
                                    timeout=LONG))
    answers.write_text("CanReboot challenge\nCanHibernate na\n")
    env.pop("PAW_LOGIN1_BUS", None)
    address = env["DBUS_SESSION_BUS_ADDRESS"]
    wait_for = desktop.wait_for
    desktop.detail = lambda: f"logind: {calls.read_text().splitlines()}"

    def power():
        """What `get power` says of each action, and of the one pending."""
        return dict(desktop.rows("power"))

    def answers_are(**expected):
        return lambda: all(power()[name] == value for name, value in expected.items())

    def logged(since=0):
        """What the fake logind and the locker recorded after the first `since` lines."""
        return calls.read_text().splitlines()[1 + since:]

    def inhibitors():
        """How many delay inhibitors the fake logind holds."""
        lines = logged()
        return lines.count("Inhibit sleep delay") - lines.count("release sleep delay")

    def reconfigure(locker=None, before="true", close="true", force="false", timeout=LONG):
        desktop.reload(CONFIG.format(locker=locker or LOCKER, before=before, close=close,
                                     force=force, timeout=timeout))

    def window(title, refuse=False):
        """A probe window; one that refuses to close says so in its output file."""
        extra = {"PAW_PROBE_TITLE": title}
        if refuse:
            extra["PAW_PROBE_REFUSE_CLOSE"] = "1"
        with (root / f"{title}.out").open("w") as out:
            return desktop.spawn([probe, "--external-control"], env=extra, stdout=out)

    def windows():
        return msg("get", "windows").count("\n")

    def closed(*clients):
        for client in clients:
            assert desktop.reap(client) == 0

    def waiting(mark):
        """The pid of a locker started after the first `mark` lines and waiting to lock."""
        def started():
            return [line for line in logged(mark) if line.startswith("waiting ")]
        wait_for(started, "the locker started")
        return int(started()[0].split()[1])

    def locked_and(mark, *expected):
        """The locker locked, and logind went through `expected`, in that order."""
        def check():
            lines = logged(mark)
            return (any(line.startswith("locked ") for line in lines) and
                    tuple(line for line in lines
                          if line.split()[0] not in ("locked", "waiting")) == expected)
        return check

    def unlock():
        """Has the locker let go; returns once the session is unlocked."""
        pid = int(re.findall(r"^locked (\d+)$", calls.read_text(), re.M)[-1])
        os.kill(pid, signal.SIGTERM)
        wait_for(lambda: logged()[-1:] == ["unlocked"], "unlocking")
        wait_for(lambda: "Session unlocked" in desktop.log.read_text(), "the session unlocked")

    login1 = desktop.spawn([fake_login1, address, str(calls), str(answers)])
    wait_for(lambda: "ready" in calls.read_text(), "the fake logind")

    # Headless, and not told of a bus, the compositor leaves logind alone.
    desktop.start()
    assert power() == {"lock": "yes", "poweroff": "unavailable", "reboot": "unavailable",
                       "suspend": "unavailable", "hibernate": "unavailable",
                       "logout": "yes", "pending": "-"}, power()
    assert "PAW_LOGIN1_BUS" in desktop.log.read_text()
    # Subscribers (the shell) hear which actions may run.
    listener = desktop.subscribe()
    wait_for(lambda: listener.last("power ") == "power lock,logout",
             "the actions subscribers hear")
    assert "logind is out of reach" in msg("poweroff", ok=False)
    assert "takes no argument" in msg("reboot", "now", ok=False)
    # Logging out needs no logind: the windows close, and the session ends as with quit.
    last = window("last")
    wait_for(lambda: windows() == 1, "a window")
    msg("logout")
    closed(last)
    assert desktop.server.wait(timeout=30) == 0, desktop.log.read_text()
    desktop.stop()
    text = desktop.log.read_text()
    assert text.index("Every window has closed") < text.index("Logging out"), text
    assert logged() == [], logged()

    env["PAW_LOGIN1_BUS"] = address
    desktop.start()
    # What logind allows arrives soon after startup, and a delay inhibitor is taken so
    # that a sleep waits for the screen to lock.
    wait_for(answers_are(poweroff="yes", reboot="challenge", suspend="yes",
                         hibernate="na", pending="-"), "logind's answers")
    wait_for(lambda: inhibitors() == 1, "the delay inhibitor")
    subscriber = desktop.subscribe()
    wait_for(lambda: subscriber.last("power ")
             == "power lock,suspend,reboot,poweroff,logout", "the actions subscribers hear")

    # The locker locks the session; while it is locked, only queries answer.
    assert power()["lock"] == "yes", power()
    mark = len(logged())
    msg("lock")
    wait_for(lambda: [line.split()[0] for line in logged(mark)] == ["locked"], "locking")
    assert "the session is locked" in msg("reboot", ok=False)
    assert power()["pending"] == "-", power()
    unlock()
    # Without a locker, or with one not installed, there is nothing to lock with, nor a
    # reason to hold off sleep.
    reconfigure(locker="{}")
    assert power()["lock"] == "no", power()
    assert "no screen locker: power.lock_command is not set" in msg("lock", ok=False)
    wait_for(lambda: inhibitors() == 0, "the delay inhibitor released")
    wait_for(lambda: subscriber.last("power ") == "power suspend,reboot,poweroff,logout",
             "subscribers told there is nothing to lock with")
    reconfigure(locker='{ "paw-no-such-locker", "-f" }')
    assert "no screen locker: paw-no-such-locker is not installed" in msg(
        "lock", ok=False)
    reconfigure()
    assert power()["lock"] == "yes", power()
    wait_for(lambda: inhibitors() == 1, "the delay inhibitor taken again")

    # Power off and reboot go to logind, which may ask for a password (interactive).
    mark = len(logged())
    msg("poweroff")
    wait_for(lambda: logged(mark) == ["PowerOff true"], "power off")
    wait_for(answers_are(pending="-"), "logind's reply to power off")
    msg("reboot")
    wait_for(lambda: logged(mark)[1:] == ["Reboot true"], "reboot")
    wait_for(answers_are(pending="-"), "logind's reply to reboot")

    # Power off asks every window to close, and logind only once they have gone.
    first, second = window("first"), window("second")
    wait_for(lambda: windows() == 2, "two windows")
    mark = len(logged())
    msg("poweroff")
    closed(first, second)
    wait_for(lambda: logged(mark) == ["PowerOff true"], "power off once the windows closed")
    text = desktop.log.read_text()
    assert text.rindex("Every window has closed") < text.rindex(
        "Asking logind to power off"), text
    # A window that stays open (an application asking whether to save) cancels a reboot
    # once close_timeout has passed, and is left alone; nothing else runs meanwhile.
    reconfigure(timeout=SHORT)
    stubborn, other = window("stubborn", refuse=True), window("other")
    wait_for(lambda: windows() == 2, "two windows")
    mark = len(logged())
    msg("reboot")
    assert power()["pending"] == "reboot closing", power()
    assert "reboot is already under way" in msg("suspend", ok=False)
    closed(other)
    wait_for(lambda: "Reboot cancelled: 1 window is still open (paw-probe)"
             in desktop.log.read_text(), "the reboot cancelled")
    assert power()["pending"] == "-" and logged(mark) == [], (power(), logged(mark))
    assert stubborn.poll() is None and windows() == 1
    assert (root / "stubborn.out").read_text() == "close refused\n"
    # With power.force it goes ahead after close_timeout all the same.
    reconfigure(force="true", timeout=SHORT)
    mark = len(logged())
    msg("reboot")
    wait_for(lambda: logged(mark) == ["Reboot true"], "the reboot forced")
    assert "1 window is still open (paw-probe); going ahead" in (
        desktop.log.read_text())
    # Without power.close_windows the windows are not asked.
    reconfigure(close="false")
    mark = len(logged())
    msg("poweroff")
    wait_for(lambda: logged(mark) == ["PowerOff true"], "power off at once")
    assert (root / "stubborn.out").read_text() == "close refused\n" * 2
    stubborn.kill()
    desktop.reap(stubborn)
    wait_for(lambda: windows() == 0, "the stubborn window gone")
    reconfigure()

    # Suspend locks the screen, and only once the lock holds asks logind, which sends the
    # machine to sleep (the delay inhibitor let go at once, the lock holding) and wakes
    # it; the inhibitor is taken again for the next sleep. This locker waits to be told
    # to lock.
    SLEPT = ("prepare", "release sleep delay", "sleep", "wake", "Inhibit sleep delay")
    reconfigure(locker=WAITING)
    mark = len(logged())
    msg("suspend")
    pid = waiting(mark)
    assert power()["pending"] == "suspend locking", power()
    assert len(logged(mark)) == 1, logged(mark)  # logind not asked yet
    os.kill(pid, signal.SIGUSR1)
    wait_for(locked_and(mark, "Suspend true", *SLEPT), "suspend")
    wait_for(answers_are(pending="-"), "logind's reply to suspend")
    text = desktop.log.read_text()
    assert text.rindex("Session locked") < text.rindex("Asking logind to suspend"), text
    unlock()

    # A sleep something else asks for (the lid, an idle daemon) waits for the lock too.
    mark = len(logged())
    login1.send_signal(signal.SIGUSR1)
    pid = waiting(mark)
    assert logged(mark)[:2] == ["external suspend", "prepare"], logged(mark)
    assert "sleep" not in logged(mark), logged(mark)
    os.kill(pid, signal.SIGUSR1)
    wait_for(locked_and(mark, "external suspend", *SLEPT), "the outside suspend")
    unlock()
    # A locker that crashed leaves the screen covered and locked; before a sleep another
    # takes its place. (Locked, the compositor takes a reload only from SIGHUP.)
    reconfigure(locker=f'{{ [[{lock_probe}]], "abandon" }}')
    msg("lock")
    wait_for(lambda: "Lock client vanished" in desktop.log.read_text(),
             "the locker gone")
    config.write_text(CONFIG.format(locker=LOCKER, before="true", close="true",
                                    force="false", timeout=LONG))
    reloads = desktop.log.read_text().count("Configuration reloaded")
    desktop.server.send_signal(signal.SIGHUP)
    wait_for(lambda: desktop.log.read_text().count("Configuration reloaded") > reloads,
             "the reload")
    mark = len(logged())
    login1.send_signal(signal.SIGUSR1)
    wait_for(locked_and(mark, "external suspend", *SLEPT), "the suspend after a crash")
    unlock()
    # A sleep from elsewhere while a suspend of ours waits for the lock makes that one
    # unneeded: the machine sleeps once, not again after waking.
    reconfigure(locker=WAITING)
    mark = len(logged())
    msg("suspend")
    pid = waiting(mark)
    login1.send_signal(signal.SIGUSR1)
    wait_for(lambda: "prepare" in logged(mark), "the outside suspend")
    wait_for(lambda: power()["pending"] == "-", "our suspend given up")
    os.kill(pid, signal.SIGUSR1)
    wait_for(locked_and(mark, "external suspend", *SLEPT), "one sleep")
    unlock()
    assert "Suspend true" not in logged(mark), logged(mark)
    # and one locker, not a second for the second reason to lock
    assert len([line for line in logged(mark) if line.startswith("waiting ")]) == 1

    # Without power.lock_before_sleep it just sleeps, whoever asks.
    reconfigure(before="false")
    wait_for(lambda: inhibitors() == 0, "the delay inhibitor released")
    mark = len(logged())
    msg("suspend")
    wait_for(lambda: logged(mark) == ["Suspend true", "prepare", "sleep", "wake"],
             "suspend without locking")
    wait_for(answers_are(pending="-"), "logind's reply to suspend")
    mark = len(logged())
    login1.send_signal(signal.SIGUSR1)
    wait_for(lambda: logged(mark) == ["external suspend", "prepare", "sleep", "wake"],
             "the outside suspend without locking")

    # A locker that never locks holds the suspend back, and then cancels it.
    reconfigure(locker='{ "true" }')
    wait_for(lambda: inhibitors() == 1, "the delay inhibitor taken again")
    mark = len(logged())
    msg("suspend")
    assert power()["pending"] == "suspend locking", power()
    wait_for(lambda: f"Suspend cancelled: the screen did not lock within {LOCK_TIMEOUT} seconds"
             in desktop.log.read_text(), "the suspend cancelled")
    assert power()["pending"] == "-" and logged(mark) == [], (power(), logged(mark))
    reconfigure()

    # What logind refuses is not asked for; a reload asks what it allows afresh.
    assert "logind does not allow hibernate here (CanHibernate: na)" in msg(
        "hibernate", ok=False)
    answers.write_text("CanPowerOff na\nCanReboot no\n")
    msg("reload")
    wait_for(answers_are(poweroff="na", reboot="no", hibernate="yes"), "the new answers")
    wait_for(lambda: subscriber.last("power ") == "power lock,suspend,hibernate,logout",
             "subscribers told of the new answers")
    # power_menu asks the shell for its menu on the monitor under the pointer.
    msg("power_menu")
    wait_for(lambda: subscriber.last("power-menu ") == "power-menu HEADLESS-1",
             "the shell asked for its power menu")
    assert "logind does not allow power off here (CanPowerOff: na)" in msg(
        "poweroff", ok=False)
    assert "logind does not allow reboot here (CanReboot: no)" in msg("reboot", ok=False)
    mark = len(logged())
    msg("hibernate")
    wait_for(locked_and(mark, "Hibernate true", *SLEPT), "hibernate")
    wait_for(answers_are(pending="-"), "logind's reply to hibernate")
    unlock()

    # A call logind turns down is reported, and leaves nothing pending.
    answers.write_text("fail Reboot\n")
    msg("reload")
    wait_for(answers_are(poweroff="yes", reboot="yes"), "the answers back to yes")
    mark = len(logged())
    msg("reboot")
    wait_for(lambda: "Reboot failed: Access denied by the fake logind"
             in desktop.log.read_text(), "the refusal reported")
    assert logged(mark) == ["Reboot true"], logged(mark)
    assert power()["pending"] == "-", power()
    # The shell hears what went wrong, and why each cancelled action was cancelled.
    reported = [line[len("power-error "):] for line in subscriber.lines("power-error ")]
    assert reported == [
        "Reboot cancelled: 1 window is still open (paw-probe)",
        f"Suspend cancelled: the screen did not lock within {LOCK_TIMEOUT} seconds",
        "Reboot failed: Access denied by the fake logind"], reported
    desktop.stop()
    wait_for(lambda: inhibitors() == 0, "the delay inhibitor released on exit")
print("The compositor locks with its locker, and asks only the logind it is given, which "
      "powers off, reboots, suspends and hibernates when it allows them")
