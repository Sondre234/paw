# SPDX-License-Identifier: GPL-3.0-or-later
"""Power saving without an idle daemon (`idle`): after so long without input the screens dim (with
grim, the screen shows it), the monitors turn off and the screen locks, each once; input undoes the
dimming and turns on the monitors it turned off, the key that does it doing nothing else. An idle
inhibitor holds every step off, one on a window only while the window shows (not on another
workspace, minimized, on a monitor turned off or behind the lock), the steps on battery apply
while a fake /sys/class/power_supply says the machine runs on it, and an ext-idle-notify-v1 client
(as swayidle is) hears of idleness as before. Keep awake holds every step off too, and has the
client hear the seat is inhibited, until turned off."""
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

import harness

compositor, idle_probe, lock_probe, pointer_probe, window_probe = (
    str(Path(p).resolve()) for p in sys.argv[1:6])
grim = sys.argv[6] if len(sys.argv) > 6 else ""

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

    STEPS = ("dim", "display_off", "lock", "suspend")

    def idle():
        rows = desktop.rows("idle")
        state = rows[0]
        assert state[0] == "state", rows
        found = {"idle_ms": int(state[1]), "held": state[2] == "1", "battery": state[3] == "1",
                 "dim_level": int(state[4]), "inhibitors": int(state[5]),
                 "applying": int(state[6]), "keep_awake": state[7] == "1"}
        found.update({row[0]: (int(row[1]), row[2] == "1") for row in rows[1:]
                      if row[0] in STEPS})
        return found

    def inhibitors():
        """Each idle inhibitor: whether it applies, and what its surface belongs to."""
        return [(row[1] == "1", row[2], row[3]) for row in desktop.rows("idle")
                if row[0] == "inhibitor"]

    def done(step):
        return idle()[step][1]

    def power_of():
        return {row[0]: row[10] for row in desktop.rows("outputs")}

    def all_on():
        return set(power_of().values()) == {"on"}

    def all_off():
        return set(power_of().values()) == {"off"}

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
    key = desktop.keyboard()
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
    # On a surface that is no window nor panel, of which nothing says it is hidden: it applies.
    assert inhibitors() == [(True, "other", "-")], inhibitors()
    assert (idle()["inhibitors"], idle()["applying"]) == (1, 1), idle()
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
    assert inhibitors() == [] and idle()["inhibitors"] == 0, idle()

    # An inhibitor on a window applies only while the window shows, as idle-inhibit-unstable-v1
    # says: on another workspace, minimized, on a monitor turned off or behind the lock, the
    # screens dim as without it; shown again, it holds the steps and they brighten.
    desktop.reload(config("{ display_off = 0 }"))
    key(A)
    video = spawn_probe("window", "Video")
    wait_for(lambda: inhibitors() == [(True, "window", "Video")] and idle()["held"],
             "the window's inhibitor applies")
    desktop.reload(config("{ dim = 1, display_off = 0 }"))
    desktop.stays(lambda: not done("dim"), "the screens dimmed under a window's inhibitor",
                  duration=1.5)

    def released(why):
        wait_for(lambda: inhibitors() == [(False, "window", "Video")] and not idle()["held"],
                 "the inhibitor let go " + why)
        assert (idle()["inhibitors"], idle()["applying"]) == (1, 0), idle()
        wait_for(lambda: done("dim"), "the screens dimmed " + why)

    def holds(why):
        wait_for(lambda: inhibitors() == [(True, "window", "Video")] and idle()["held"],
                 "the inhibitor applied again " + why)
        state = idle()
        assert state["dim_level"] == 0 and not state["dim"][1], state

    def video_window():
        return next(w for w in desktop.windows() if w.title == "Video")

    msg("move_to_workspace", "2")
    released("with the window on another workspace")
    msg("workspace", "2")
    holds("with its workspace shown")
    msg("minimize")
    released("with the window minimized")
    subprocess.run([window_probe, "Video", "restore"], env=desktop.env, check=True, timeout=30)
    holds("with the window restored")
    shown_on = video_window().output
    msg("output", shown_on, "display_off")
    released("with its monitor off")
    msg("display_on")
    holds("with its monitor on")
    locks = log.read_text().count("locked")
    msg("lock")
    wait_for(lambda: log.read_text().count("locked") > locks, "the screen locked")
    released("behind the lock")
    os.kill(int(log.read_text().split()[-1]), signal.SIGTERM)
    holds("once unlocked")
    video.terminate()
    desktop.reap(video)
    wait_for(lambda: inhibitors() == [] and not idle()["held"], "the window's inhibitor gone")

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

    # Keep awake holds every step off and tells idle daemons the seat is inhibited (the client
    # resumes with no input), as an inhibitor would, and subscribers hear it; turned off, the
    # time without input counts from then.
    events = desktop.subscribe()
    assert heard() == "idled"
    assert not idle()["keep_awake"]
    msg("keep_awake_on")
    assert heard() == "resumed"
    state = idle()
    assert state["held"] and state["keep_awake"] and state["applying"] == 0, state
    wait_for(lambda: events.last("keep-awake ") == "keep-awake on", "subscribers heard keep awake")
    desktop.stays(lambda: not done("dim") and idle()["dim_level"] == 0,
                  "the screens dimmed while kept awake", duration=1.5)
    msg("keep_awake_on")  # on already: nothing changes
    msg("keep_awake_toggle")
    assert heard() == "idled"
    wait_for(lambda: done("dim"), "the screens dimmed once keep awake went off")
    assert not idle()["keep_awake"] and events.last("keep-awake ") == "keep-awake off"
print("Idle steps dim, turn off, lock, are held by inhibitors shown and keep awake, and follow "
      "the power source"
      + ("" if grim else " (pixels not checked: grim missing)"))
