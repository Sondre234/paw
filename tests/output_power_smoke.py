# SPDX-License-Identifier: GPL-3.0-or-later
"""Monitors turned off and on in the layout: a wlr-output-power-management client (as wlopm is)
turns one off and on; it keeps its place, windows, workspace and panel, `get outputs` says it is
off, it draws no frames while off, and a lock does not wait for it. The display_off, display_on
and display_toggle actions do the same from the control socket and bindings, and clients hear
of it. Input turns every monitor on once all of them are off."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe, power_probe, lock_probe, pointer_probe = (str(Path(p).resolve())
                                                              for p in sys.argv[1:6])

LEFTMETA, A, F1, F2 = 125, 30, 59, 60  # evdev key codes

CONFIG = """return {
    xwayland = false,
    animations = { enabled = false },
    bindings = { { mods = { "Super" }, key = "F1", action = "display_toggle", output = "HEADLESS-1" },
                 { key = "F2", action = "display_toggle", output = "HEADLESS-1" } },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" },
                             ["HEADLESS-2"] = { mode = "1280x720" } } },
}"""

with harness.Compositor(compositor, CONFIG, env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def outputs():
        """name: (enabled, x, y, width, height, power) per monitor."""
        return {r[0]: (r[1] == "1", *map(int, r[2:6]), r[10]) for r in desktop.rows("outputs")}

    def windows():
        """(workspace, output, visible) per window."""
        return [(int(r[0]), r[10], r[11] == "1") for r in desktop.rows("windows")]

    def frames():
        return int(desktop.rows("stats")[0][0])

    def power(*words):
        result = subprocess.run([power_probe, *words], env=desktop.env, capture_output=True,
                                text=True, timeout=30)
        assert result.returncode == 0, result.stderr
        return result.stdout.split()

    desktop.detail = lambda: f"outputs: {outputs()}, windows: {windows()}"
    before = outputs()
    assert [state[5] for state in before.values()] == ["on", "on"], before
    assert sorted(power("list")) == sorted(["HEADLESS-1", "on", "HEADLESS-2", "on"])

    # A window and a panel on HEADLESS-2.
    msg("output", "HEADLESS-2", "workspace", "2")
    window = desktop.spawn([probe, "--external-control"])
    wait_for(lambda: windows() == [(2, "HEADLESS-2", True)], "the window opened on HEADLESS-2")
    wait_for(lambda: len(desktop.rows("layers")) == 1, "the panel mapped")
    layers = desktop.rows("layers")

    assert power("set", "HEADLESS-2", "off") == ["off"]
    state = outputs()
    assert state["HEADLESS-2"] == (*before["HEADLESS-2"][:5], "off"), state
    assert state["HEADLESS-1"] == before["HEADLESS-1"], state
    assert windows() == [(2, "HEADLESS-2", True)], windows()
    assert desktop.rows("layers") == layers
    assert {r[0]: r[1] for r in desktop.rows("workspaces")}["HEADLESS-2"] == "2"
    assert sorted(power("list")) == sorted(["HEADLESS-1", "on", "HEADLESS-2", "off"])
    # Asked again, it stays off; a reload leaves it off too.
    assert power("set", "HEADLESS-2", "off") == ["off"]
    desktop.reload()
    assert outputs()["HEADLESS-2"][5] == "off", outputs()

    # With every monitor off, nothing is drawn, however the scene changes.
    assert power("set", "HEADLESS-1", "off") == ["off"]
    drawn = frames()
    msg("output", "HEADLESS-2", "workspace", "1")
    msg("output", "HEADLESS-1", "workspace", "3")
    desktop.stays(lambda: frames() == drawn, "frames were drawn with every monitor off")
    assert windows() == [(2, "HEADLESS-2", False)], windows()

    # A lock does not wait for monitors that are off.
    log = desktop.root / "locker.log"
    log.touch()
    locker = desktop.spawn([lock_probe, "hold", str(log)])
    wait_for(lambda: "locked" in log.read_text(), "the lock held with the monitors off")
    # Turned on while locked, a monitor shows the lock's cover, then the desktop once unlocked.
    assert power("set", "HEADLESS-2", "on") == ["on"]
    wait_for(lambda: frames() > drawn, "HEADLESS-2 drew again")
    locker.terminate()
    assert desktop.reap(locker) == 0
    assert "unlocked" in log.read_text()

    assert power("set", "HEADLESS-1", "on") == ["on"]
    assert outputs() == {name: (*state[:5], "on") for name, state in before.items()}, outputs()
    msg("output", "HEADLESS-2", "workspace", "2")
    assert windows() == [(2, "HEADLESS-2", True)], windows()

    def power_of():
        return {name: state[5] for name, state in outputs().items()}

    # The actions: every monitor, or the one named, and a client watching hears of it.
    watcher = desktop.spawn([power_probe, "watch", "HEADLESS-1"], stdout=subprocess.PIPE,
                            text=True)
    assert watcher.stdout.readline().split() == ["HEADLESS-1", "on"]
    msg("display_off")
    assert power_of() == {"HEADLESS-1": "off", "HEADLESS-2": "off"}, outputs()
    assert watcher.stdout.readline().split() == ["HEADLESS-1", "off"]
    msg("display_on", "HEADLESS-2")
    assert power_of() == {"HEADLESS-1": "off", "HEADLESS-2": "on"}, outputs()
    msg("display_toggle")  # one is on: all go off
    assert power_of() == {"HEADLESS-1": "off", "HEADLESS-2": "off"}, outputs()
    msg("display_toggle")
    assert power_of() == {"HEADLESS-1": "on", "HEADLESS-2": "on"}, outputs()
    assert watcher.stdout.readline().split() == ["HEADLESS-1", "on"]
    msg("output", "HEADLESS-2", "display_off")
    assert power_of() == {"HEADLESS-1": "on", "HEADLESS-2": "off"}, outputs()
    msg("display_toggle", "HEADLESS-2")
    assert power_of() == {"HEADLESS-1": "on", "HEADLESS-2": "on"}, outputs()
    assert "no monitor HDMI-A-1" in msg("display_off", "HDMI-A-1", ok=False)
    assert power_of() == {"HEADLESS-1": "on", "HEADLESS-2": "on"}, outputs()
    # A binding with an output turns that one off and on.
    msg("headless_keyboard", "add", "keys")
    for code, state in ((LEFTMETA, "press"), (F1, "press"), (F1, "release"),
                        (LEFTMETA, "release")):
        msg("headless_keyboard", "key", "keys", str(code), state)
    assert power_of() == {"HEADLESS-1": "off", "HEADLESS-2": "on"}, outputs()
    assert watcher.stdout.readline().split() == ["HEADLESS-1", "off"]
    msg("display_on")
    watcher.terminate()
    desktop.reap(watcher)

    # With every monitor off, input turns them all on, whoever turned them off: a key press,
    # which does nothing else, or the pointer; a key coming up does not. Just after an action
    # turned them off, input leaves them off a moment, while the keys that did it come up.
    def key(code, state="press"):
        msg("headless_keyboard", "key", "keys", str(code), state)

    def all_off():
        return set(power_of().values()) == {"off"}

    msg("display_off")
    key(A)
    key(A, "release")
    desktop.stays(all_off, "input turned the monitors on at once", duration=1.1)
    key(F2)
    assert power_of() == {"HEADLESS-1": "on", "HEADLESS-2": "on"}, outputs()
    key(F2, "release")
    key(F2)  # now the binding runs
    key(F2, "release")
    assert power_of() == {"HEADLESS-1": "off", "HEADLESS-2": "on"}, outputs()
    assert power("set", "HEADLESS-2", "off") == ["off"]
    desktop.stays(all_off, "input turned the monitors on at once", duration=1.1)
    key(A, "release")
    desktop.stays(all_off, "a key coming up turned the monitors on")
    pointer = desktop.virtual_pointer(pointer_probe, 2560, 720)
    pointer("move 100 100")
    wait_for(lambda: power_of() == {"HEADLESS-1": "on", "HEADLESS-2": "on"},
             "the pointer turned the monitors on")
    assert "Input turned the monitors on" in desktop.log.read_text()

    # A monitor out of the layout is off, and no client can turn it on. (The probe's panel goes
    # with it, and the probe with its panel.)
    window.terminate()
    desktop.reap(window)
    desktop.reload(CONFIG.replace('["HEADLESS-2"] = { mode = "1280x720" }',
                                  '["HEADLESS-2"] = { enabled = false }'))
    assert outputs()["HEADLESS-2"][::5] == (False, "off"), outputs()
    assert power("list") == ["HEADLESS-1", "on"]

# Unplugged while off, a monitor's windows move to another as they would otherwise, and they return
# with it, on.
with harness.Compositor(compositor, CONFIG, env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def placed():
        """(output, power of its monitor) per window."""
        power = {r[0]: r[10] for r in desktop.rows("outputs")}
        return [(r[10], power.get(r[10])) for r in desktop.rows("windows")]

    msg("output", "HEADLESS-2", "workspace", "1")
    desktop.spawn([probe, "--window-only"])
    wait_for(lambda: placed() == [("HEADLESS-2", "on")], "the window opened on HEADLESS-2")
    msg("display_off", "HEADLESS-2")
    assert placed() == [("HEADLESS-2", "off")], placed()
    msg("headless_output", "remove", "HEADLESS-2")
    wait_for(lambda: placed() == [("HEADLESS-1", "on")], "the window moved to HEADLESS-1")
    msg("headless_output", "add", "HEADLESS-2")
    wait_for(lambda: placed() == [("HEADLESS-2", "on")], "the window returned with HEADLESS-2")
print("Monitors turned off and on through wlr-output-power-management keep their place")
