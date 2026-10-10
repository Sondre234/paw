# SPDX-License-Identifier: GPL-3.0-or-later
"""Windows' Win+P choices with the display_mode action on headless outputs, one of them a laptop's
panel (eDP-1): duplicate mirrors the others onto the panel, internal keeps the panel alone,
external every monitor but it, extend all of them; a choice lasts until a reload. Without a
choice, the action (bound to XF86Display) opens a popup on the focused monitor showing the choice
in force, steps through the four in Windows' order (PC screen only, duplicate, extend, second
screen only) and takes the one shown once the key rests; the arrows step,
Return takes it at once and Escape closes it without. Subscribers hear of the popup. Without a
built-in panel the primary monitor stands in for it, and with one monitor only extend is had."""
from pathlib import Path
import sys

import harness

compositor = str(Path(sys.argv[1]).resolve())

CONFIG = """return {
    xwayland = false,
    animations = { enabled = false },
    idle = { display_off = 0 },
    outputs = { primary = "HEADLESS-2", monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    bindings = { { mods = {}, key = "XF86Display", action = "display_mode" } },
}"""
KEYS = {"display": 227, "escape": 1, "enter": 28, "right": 106, "left": 105}

with harness.Compositor(compositor, CONFIG, env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def outputs():
        """name: (in the layout, power, mirrored) per monitor."""
        return {r[0]: (r[1] == "1", r[10], r[11]) for r in desktop.rows("outputs")}

    def mode():
        """The choice in force, and the popup's choice and output ("-" while closed)."""
        return tuple(desktop.rows("display_mode")[0])

    def press(name):
        msg("headless_keyboard", "key", "keys", str(KEYS[name]), "press")
        msg("headless_keyboard", "key", "keys", str(KEYS[name]), "release")

    ON, OFF = (True, "on", "-"), (False, "off", "-")
    desktop.detail = lambda: f"outputs: {outputs()}, mode: {mode()}"
    msg("headless_output", "add", "eDP-1")
    msg("headless_keyboard", "add", "keys")
    wait_for(lambda: outputs() == {"HEADLESS-1": ON, "HEADLESS-2": ON, "eDP-1": ON}, "three on")
    assert mode() == ("extend", "-", "-"), mode()
    events = desktop.subscribe()

    # The choices by name: the panel is the main monitor, which the others mirror.
    msg("display_mode", "duplicate")
    mirrors = (False, "on", "eDP-1")
    wait_for(lambda: outputs() == {"HEADLESS-1": mirrors, "HEADLESS-2": mirrors, "eDP-1": ON},
             "duplicate")
    assert mode()[0] == "duplicate", mode()
    assert [r[0] for r in desktop.rows("workspaces")] == ["eDP-1"]
    msg("display_mode", "internal")
    wait_for(lambda: outputs() == {"HEADLESS-1": OFF, "HEADLESS-2": OFF, "eDP-1": ON}, "internal")
    assert mode()[0] == "internal", mode()
    msg("display_mode", "external")
    wait_for(lambda: outputs() == {"HEADLESS-1": ON, "HEADLESS-2": ON, "eDP-1": OFF}, "external")
    assert mode()[0] == "external", mode()
    msg("display_mode", "extend")
    wait_for(lambda: outputs() == {"HEADLESS-1": ON, "HEADLESS-2": ON, "eDP-1": ON}, "extend")
    assert "display mode must be" in msg("display_mode", "sideways", ok=False)
    # A choice keeps the monitors' own settings, and lasts until a reload.
    msg("display_mode", "internal")
    desktop.reload()
    wait_for(lambda: outputs() == {"HEADLESS-1": ON, "HEADLESS-2": ON, "eDP-1": ON},
             "a reload brought the configuration back")

    # The popup: the key opens it on the focused monitor showing the choice in force, the next
    # press steps on, and once the key rests the choice shown is taken and the popup closes.
    focused = next(r[0] for r in desktop.rows("workspaces") if r[2] == "1")
    press("display")
    assert mode() == ("extend", "extend", focused), mode()
    wait_for(lambda: f"display-mode {focused} extend extend internal,duplicate,extend,external"
             in events.lines(), "the shell heard of it")
    press("display")
    press("display")
    assert mode() == ("extend", "internal", focused), mode()
    wait_for(lambda: mode() == ("internal", "-", "-"), "the choice shown was taken")
    assert outputs() == {"HEADLESS-1": OFF, "HEADLESS-2": OFF, "eDP-1": ON}, outputs()
    wait_for(lambda: "display-mode-close" in events.lines(), "the shell heard it close")

    # The arrows step either way, wrapping; Return takes the choice at once, Escape none.
    focused = next(r[0] for r in desktop.rows("workspaces") if r[2] == "1")
    press("display")
    press("right")
    press("right")
    assert mode() == ("internal", "extend", focused), mode()
    press("left")
    assert mode() == ("internal", "duplicate", focused), mode()
    press("escape")
    assert mode() == ("internal", "-", "-"), mode()
    desktop.stays(lambda: mode() == ("internal", "-", "-"), "Escape took nothing")
    press("display")
    press("left")
    assert mode()[1] == "external", mode()
    press("enter")
    wait_for(lambda: outputs() == {"HEADLESS-1": ON, "HEADLESS-2": ON, "eDP-1": OFF},
             "Return took external")
    # A choice by name closes the popup without taking what it showed.
    press("display")
    press("right")
    msg("display_mode", "extend")
    assert mode() == ("extend", "-", "-"), mode()
    desktop.stays(lambda: mode() == ("extend", "-", "-"), "the popup's choice was dropped")

    # Without a built-in panel the primary monitor (outputs.primary) is the main one.
    msg("headless_output", "remove", "eDP-1")
    wait_for(lambda: "eDP-1" not in outputs(), "the panel unplugged")
    msg("display_mode", "internal")
    wait_for(lambda: outputs() == {"HEADLESS-1": OFF, "HEADLESS-2": ON}, "the primary alone")
    msg("display_mode", "duplicate")
    wait_for(lambda: outputs() == {"HEADLESS-1": (False, "on", "HEADLESS-2"), "HEADLESS-2": ON},
             "the other mirrors the primary")

    # With one monitor only extend is had, and the popup offers it alone.
    msg("display_mode", "extend")
    msg("headless_output", "remove", "HEADLESS-1")
    wait_for(lambda: list(outputs()) == ["HEADLESS-2"], "one monitor left")
    assert "needs a second monitor" in msg("display_mode", "duplicate", ok=False)
    press("display")
    press("right")
    assert mode() == ("extend", "extend", "HEADLESS-2"), mode()
    wait_for(lambda: "display-mode HEADLESS-2 extend extend extend" in events.lines(),
             "the popup offers extend alone")
    press("escape")
print("display_mode's choices and popup set the monitors up as Windows' Win+P does")
