# SPDX-License-Identifier: GPL-3.0-or-later
"""A laptop's lid, with headless switches and outputs: closing it while another monitor is on
turns the built-in panel (eDP-1) off, its windows moving to the other monitor as when it is
unplugged, and opening it brings the panel and its windows back; with no other monitor the panel
stays on, and a monitor coming or going changes that. A lid already closed as its switch appears
(as libinput reports a reliable one at startup) holds the panel off, and a panel appearing behind a
closed lid stays off. outputs.lid = "ignore" leaves the panel on, bindings run on switches' changes, and
opening the lid counts as input."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {{
    xwayland = false,
    animations = {{ enabled = false }},
    outputs = {{ lid = "{lid}" }},
    idle = {idle},
    windows = {{ rules = {{
        {{ title = "^on the panel$", output = "eDP-1", workspace = 2 }},
        {{ title = "^on the monitor$", output = "HEADLESS-1", workspace = 1 }},
    }} }},
    bindings = {{
        {{ switch = "tablet", state = "on", action = "workspace", workspace = 3 }},
        {{ switch = "lid", state = "open", action = "workspace", workspace = 2 }},
    }},
}}"""


def config(lid="clamshell", idle="{ display_off = 0 }"):
    return CONFIG.format(lid=lid, idle=idle)


with harness.Compositor(compositor, config()) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def outputs():
        """name: (enabled, power) per monitor."""
        return {r[0]: (r[1] == "1", r[10]) for r in desktop.rows("outputs")}

    def enabled():
        return {name for name, (on, _) in outputs().items() if on}

    def windows():
        """(title, output, workspace) per window, by title."""
        return sorted((w.title, w.output, w.workspace) for w in desktop.windows())

    def switches():
        return desktop.rows("switches")

    def window(title, output, workspace):
        """A window the rules open there."""
        desktop.spawn([probe, "--window-only"], env={"PAW_PROBE_TITLE": title})
        wait_for(lambda: (title, output, workspace) in windows(), f"{title} opened")

    def plug(name):
        msg("headless_output", "add", name)

    def unplug(name):
        msg("headless_output", "remove", name)
        wait_for(lambda: name not in outputs(), f"{name} unplugged")

    desktop.detail = lambda: f"outputs: {outputs()}, windows: {windows()}, switches: {switches()}"
    plug("eDP-1")
    wait_for(lambda: enabled() == {"HEADLESS-1", "eDP-1"}, "the laptop's panel")
    window("on the panel", "eDP-1", 2)
    window("on the monitor", "HEADLESS-1", 1)
    msg("headless_switch", "add", "lid", "lid")
    assert switches() == [["lid", "open"], ["lid", "0", "0"]], switches()

    # Closed: the panel leaves the layout, and its window moves over, keeping its workspace.
    msg("headless_switch", "toggle", "lid", "on")
    assert switches() == [["lid", "closed"], ["lid", "1", "0"]], switches()
    assert enabled() == {"HEADLESS-1"}, outputs()
    wait_for(lambda: windows() == [("on the monitor", "HEADLESS-1", 1),
                                   ("on the panel", "HEADLESS-1", 2)], "the window moved over")
    assert "The lid is closed" in desktop.log.read_text()
    # Open: the panel and its window come back, and the lid's binding runs.
    msg("headless_switch", "toggle", "lid", "off")
    assert enabled() == {"HEADLESS-1", "eDP-1"}, outputs()
    wait_for(lambda: windows() == [("on the monitor", "HEADLESS-1", 1),
                                   ("on the panel", "eDP-1", 2)], "the window returned")
    assert msg("get", "workspace").split() == ["2"]

    # Without another monitor the panel stays on; one plugged in turns it off, and unplugged
    # again brings it back (the session does not end with its last monitor in the layout gone).
    unplug("HEADLESS-1")
    msg("headless_switch", "toggle", "lid", "on")
    desktop.stays(lambda: enabled() == {"eDP-1"}, "the only monitor went off")
    plug("HEADLESS-1")
    wait_for(lambda: enabled() == {"HEADLESS-1"}, "the panel off with another monitor")
    wait_for(lambda: {w[1] for w in windows()} == {"HEADLESS-1"}, "the windows moved over")
    unplug("HEADLESS-1")
    wait_for(lambda: enabled() == {"eDP-1"}, "the panel back without another monitor")
    plug("HEADLESS-1")
    wait_for(lambda: enabled() == {"HEADLESS-1"}, "the panel off again")

    # A lid closed as its switch appears, as libinput reports one at startup, holds the panel off;
    # so does a panel appearing behind it.
    msg("headless_switch", "remove", "lid")
    wait_for(lambda: enabled() == {"HEADLESS-1", "eDP-1"}, "the panel on without a lid switch")
    assert switches() == [["lid", "open"]], switches()
    msg("headless_switch", "add", "closed", "lid", "on")
    wait_for(lambda: enabled() == {"HEADLESS-1"}, "the panel off behind a closed lid")
    unplug("eDP-1")
    plug("eDP-1")
    assert outputs()["eDP-1"] == (False, "off"), outputs()
    desktop.stays(lambda: enabled() == {"HEADLESS-1"}, "a panel lit up behind the closed lid")

    # outputs.lid = "ignore" leaves the panel alone.
    desktop.reload(config(lid="ignore"))
    assert enabled() == {"HEADLESS-1", "eDP-1"}, outputs()
    desktop.reload(config())
    assert enabled() == {"HEADLESS-1"}, outputs()

    # Opening the lid is input: the monitors the idle steps turned off come on with the panel.
    desktop.reload(config(idle="{ display_off = 1 }"))
    msg("headless_keyboard", "add", "keys")  # input, to start counting from now
    msg("headless_keyboard", "key", "keys", "30", "press")
    msg("headless_keyboard", "key", "keys", "30", "release")
    wait_for(lambda: outputs()["HEADLESS-1"] == (True, "off"), "the monitor went off when idle")
    msg("headless_switch", "toggle", "closed", "off")
    wait_for(lambda: outputs() == {"HEADLESS-1": (True, "on"), "eDP-1": (True, "on")},
             "both on as the lid opened")
    desktop.reload(config())

    # Tablet mode's binding.
    msg("headless_switch", "add", "convertible", "tablet")
    msg("headless_switch", "toggle", "convertible", "on")
    assert msg("get", "workspace").split() == ["3"]
    assert switches()[-1] == ["convertible", "0", "1"], switches()
    msg("workspace", "1")
    msg("headless_switch", "toggle", "convertible", "off")
    desktop.stays(lambda: msg("get", "workspace").split() == ["1"], "tablet mode's end ran a binding")
    assert "headless_switch add" in msg("headless_switch", "add", "x", "keypad", ok=False)
print("The lid turns the laptop's panel off and on with another monitor, and switches run bindings")
