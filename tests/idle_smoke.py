# SPDX-License-Identifier: GPL-3.0-or-later
"""Power saving without an idle daemon (`idle`): after so long without input the screens dim (with
grim, the screen shows it), the monitors turn off and the screen locks, each once; input undoes the
dimming and turns on the monitors it turned off, the key that does it doing nothing else. An idle
inhibitor holds every step off, the steps on battery apply while a fake /sys/class/power_supply
says the machine runs on it, and an ext-idle-notify-v1 client (as swayidle is) hears of idleness
as before."""
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

import harness

compositor, idle_probe, lock_probe, pointer_probe = (str(Path(p).resolve())
                                                     for p in sys.argv[1:5])
grim = sys.argv[5] if len(sys.argv) > 5 else ""

A, F2 = 30, 60  # evdev key codes

CONFIG = """return {{
    xwayland = false,
    animations = {{ enabled = false }},
    appearance = {{ background = "#808080" }},
    power = {{ lock_command = {{ [[{locker}]], "hold", [[{log}]] }} }},
    idle = {idle},
    bindings = {{ {{ key = "F2", action = "display_toggle", output = "HEADLESS-1" }} }},
}}"""


def supplies(root, battery):
    """A fake sysfs whose laptop runs on battery, or on mains."""
    for name, fields in (("BAT0", {"type": "Battery",
                                   "status": "Discharging" if battery else "Charging"}),
                         ("AC", {"type": "Mains", "online": "0" if battery else "1"})):
        directory = root / "class" / "power_supply" / name
        directory.mkdir(parents=True, exist_ok=True)
        for field, value in fields.items():
            (directory / field).write_text(value + "\n")


with harness.Compositor(compositor, start=False, env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for
    log, sysfs = desktop.root / "locker.log", desktop.root / "sys"
    log.touch()
    supplies(sysfs, battery=False)
    desktop.env["PAW_SYSFS"] = str(sysfs)

    def config(idle):
        return CONFIG.format(locker=lock_probe, log=log, idle=idle)

    def idle():
        rows = desktop.rows("idle")
        state = rows[0]
        assert state[0] == "state", rows
        found = {"idle_ms": int(state[1]), "held": state[2] == "1", "battery": state[3] == "1",
                 "dim_level": int(state[4])}
        found.update({row[0]: (int(row[1]), row[2] == "1") for row in rows[1:]})
        return found

    def done(step):
        return idle()[step][1]

    def power_of():
        return {row[0]: row[10] for row in desktop.rows("outputs")}

    def all_on():
        return set(power_of().values()) == {"on"}

    def all_off():
        return set(power_of().values()) == {"off"}

    def key(code):
        msg("headless_keyboard", "key", "keys", str(code), "press")
        msg("headless_keyboard", "key", "keys", str(code), "release")

    def screen():
        """The colour in the middle of HEADLESS-1, or None without grim."""
        shot = harness.grab(grim, desktop.env, "HEADLESS-1")
        return shot.at(shot.width // 2, shot.height // 2) if shot else None

    def spawn_probe(*words):
        process = desktop.spawn([idle_probe, *words], stdout=subprocess.PIPE, text=True)
        assert process.stdout.readline().strip() == "ready"
        return process

    desktop.detail = lambda: f"idle: {idle()}, outputs: {power_of()}"
    desktop.start(config("{ dim = 1, display_off = 2, lock = 3 }"))
    msg("headless_keyboard", "add", "keys")
    pointer = desktop.virtual_pointer(pointer_probe, 2560, 720)
    state = idle()
    assert (state["dim"], state["display_off"], state["lock"], state["suspend"]) == (
        (1000, False), (2000, False), (3000, False), (0, False)), state
    assert not state["held"] and not state["battery"] and state["dim_level"] == 0, state
    assert screen() in (None, (128, 128, 128)), screen()

    # The screens dim; input brightens them, and counting starts again.
    wait_for(lambda: done("dim"), "the screens dimmed")
    assert idle()["dim_level"] == 500 and all_on(), idle()
    assert screen() in (None, (64, 64, 64)), screen()
    key(A)
    state = idle()
    assert state["dim_level"] == 0 and not state["dim"][1] and state["idle_ms"] < 1000, state
    wait_for(all_on, "the monitors on after input")
    assert screen() in (None, (128, 128, 128)), screen()

    # The monitors turn off, dimmed, and the screen locks while they are off: nothing shows,
    # so the lock holds at once.
    wait_for(lambda: done("display_off"), "the monitors went off")
    assert all_off() and idle()["dim_level"] == 500, idle()
    wait_for(lambda: "locked" in log.read_text(), "the screen locked")
    assert done("lock") and all_off()
    # The pointer turns them on, showing the lock; every step counts again.
    pointer("move 100 100")
    wait_for(all_on, "the pointer turned the monitors on")
    state = idle()
    assert state["dim_level"] == 0 and not any(state[step][1] for step in (
        "dim", "display_off", "lock", "suspend")), state
    os.kill(int(log.read_text().split()[1]), signal.SIGTERM)
    wait_for(lambda: "Session unlocked" in desktop.log.read_text(), "the session unlocked")

    # An idle inhibitor (a video playing) holds every step off; once it goes, the time without
    # input counts from then.
    desktop.reload(config("{ display_off = 0 }"))
    key(A)
    inhibitor = spawn_probe("inhibit")
    wait_for(lambda: idle()["held"], "the inhibitor holds the steps")
    desktop.reload(config("{ dim = 1, display_off = 2, lock = 2 }"))
    desktop.stays(lambda: not any(idle()[step][1] for step in ("dim", "display_off", "lock")),
                  "a step was taken while inhibited", duration=2.5)
    assert all_on() and idle()["idle_ms"] >= 2500, idle()
    inhibitor.terminate()
    desktop.reap(inhibitor)
    released = time.monotonic()
    wait_for(lambda: not idle()["held"], "the inhibitor gone")
    wait_for(lambda: done("dim"), "the screens dimmed after the inhibitor went")
    assert time.monotonic() - released >= 0.9, "the time without input did not start again"

    # The key that turns the monitors on does nothing else; the monitors it did not turn off
    # stay as they are.
    desktop.reload(config("{ display_off = 2 }"))
    key(A)
    assert idle()["dim"] == (1000, False), idle()
    wait_for(all_off, "the monitors went off")
    key(F2)
    wait_for(all_on, "F2 turned the monitors on")
    desktop.stays(all_on, "F2 ran its binding too")
    key(F2)  # now it runs: HEADLESS-1 goes off by hand
    assert power_of() == {"HEADLESS-1": "off", "HEADLESS-2": "on"}, power_of()
    key(A)
    desktop.stays(lambda: power_of()["HEADLESS-1"] == "off", "input turned on a monitor "
                  "turned off by hand")
    msg("display_on")

    # On battery the battery's steps apply; back on mains, the others.
    supplies(sysfs, battery=True)
    desktop.reload(config("{ display_off = 30, battery = { display_off = 1 } }"))
    key(A)
    state = idle()
    assert state["battery"] and state["display_off"] == (1000, False), state
    wait_for(all_off, "the monitors went off on battery")
    supplies(sysfs, battery=False)
    key(A)
    wait_for(all_on, "a key turned the monitors on")
    state = idle()
    assert not state["battery"] and state["display_off"] == (30000, False), state
    desktop.stays(all_on, "the battery's steps applied on mains", duration=1.5)

    # An ext-idle-notify-v1 client hears of idleness and input as before, beside the steps;
    # display_off = 0 leaves the monitors (and the dimming) to it.
    desktop.reload(config("{ display_off = 0 }"))
    key(A)
    state = idle()
    assert state["dim"] == (0, False) and state["display_off"] == (0, False), state
    watcher = spawn_probe("notify", "300")

    def heard():
        return watcher.stdout.readline().strip()

    assert heard() == "idled"
    key(A)
    assert heard() == "resumed"
    desktop.stays(all_on, "the monitors went off with display_off = 0", duration=1)
    assert heard() == "idled"
    desktop.reload(config("{ dim = 1 }"))
    key(A)
    assert heard() == "resumed"
    wait_for(lambda: done("dim"), "the screens dimmed beside the client")
    assert heard() == "idled"
    key(A)
    assert heard() == "resumed"
    assert idle()["dim_level"] == 0
print("Idle steps dim, turn off, lock, are held by inhibitors and follow the power source"
      + ("" if grim else " (pixels not checked: grim missing)"))
