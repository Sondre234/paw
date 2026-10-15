# SPDX-License-Identifier: GPL-3.0-or-later
"""Bindings of the wheel (`button = "wheel_up"`): the default configuration's Super + wheel pages
through the workspaces of the monitor under the pointer, wrapping around, a notch a step however
the turn comes, and the window under the pointer does not get the turn; without Super, or with
`action = "none"`, it does; zoom.scroll_modifier takes the wheel first. Driven by a virtual
pointer and keyboard (pointer_probe) over an input_probe window."""
from pathlib import Path
import sys

import harness

compositor, input_probe, pointer_probe, default_config = (
    str(Path(p).resolve()) for p in sys.argv[1:5])

SCREEN = (1280 * 2, 720)


def config(extra=""):
    return """return {
    version = 1,
    extends = "default",
    xwayland = false,
    animations = { enabled = false },
    outputs = { order = { "HEADLESS-1", "HEADLESS-2" },
                monitors = { ["HEADLESS-1"] = { mode = "1280x720" },
                             ["HEADLESS-2"] = { mode = "1280x720" } } },
    mouse = { focus_follows = false },
    %s
}""" % extra


with harness.Compositor(compositor, config(),
                        env={"PAW_DEFAULT_CONFIG": default_config,
                             "WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    msg = desktop.msg
    pointer = desktop.virtual_pointer(pointer_probe, *SCREEN)

    def shown():
        """Each monitor's workspace, and whether it is the focused one."""
        return {row[0]: (int(row[1]), row[2] == "1") for row in desktop.rows("workspaces")}

    pointer("move", "640", "360")
    probe = desktop.spawn([input_probe, "Wheel"], log="input.log")
    heard = lambda: (desktop.root / "input.log").read_text().splitlines()
    desktop.wait_for(lambda: "ready" in heard(), "the window drawn")
    window = desktop.windows()[0]
    assert window.output == "HEADLESS-1", window
    x, y, width, height = window.box
    pointer("move", str(x + width // 2), str(y + height // 2))
    desktop.wait_for(lambda: any(line.startswith("pointer enter") for line in heard()),
                     "the pointer over the window")
    turns = lambda: [line for line in heard() if line.startswith("pointer axis")]
    desktop.detail = lambda: f"shown: {shown()} heard: {turns()}"

    # Without Super the window scrolls.
    pointer("scroll", "-15")
    desktop.wait_for(lambda: turns() == ["pointer axis 0 -15"], "the window scrolled")
    assert shown() == {"HEADLESS-1": (1, True), "HEADLESS-2": (1, False)}, shown()

    # With it, a notch up shows the next workspace, and the window hears nothing of it.
    pointer("key", "super", "down", "scroll", "-15")
    desktop.wait_for(lambda: shown()["HEADLESS-1"] == (2, True), "Super + wheel up: workspace 2")
    pointer("scroll", "15")
    desktop.wait_for(lambda: shown()["HEADLESS-1"] == (1, True), "Super + wheel down: back to 1")
    assert turns() == ["pointer axis 0 -15"], turns()

    # Past the first workspace it wraps around to the last, and back.
    pointer("scroll", "15")
    desktop.wait_for(lambda: shown()["HEADLESS-1"] == (4, True), "wrapped to workspace 4")
    pointer("scroll", "-15")
    desktop.wait_for(lambda: shown()["HEADLESS-1"] == (1, True), "wrapped back to workspace 1")

    # Parts of a notch add up to one; turning the other way starts afresh.
    pointer("scroll", "-5", "scroll", "-5")
    desktop.stays(lambda: shown()["HEADLESS-1"] == (1, True), "two thirds of a notch switched")
    pointer("scroll", "-5")
    desktop.wait_for(lambda: shown()["HEADLESS-1"] == (2, True), "three thirds made a notch")
    pointer("scroll", "-10", "scroll", "5", "scroll", "5")
    desktop.stays(lambda: shown()["HEADLESS-1"] == (2, True), "a turn back kept the turn before")
    pointer("scroll", "5")
    desktop.wait_for(lambda: shown()["HEADLESS-1"] == (1, True), "a notch back")
    # Two notches at once are two steps.
    pointer("scroll", "-30")
    desktop.wait_for(lambda: shown()["HEADLESS-1"] == (3, True), "two notches, two steps")
    pointer("scroll", "30")
    desktop.wait_for(lambda: shown()["HEADLESS-1"] == (1, True), "two notches back")

    # On the other monitor it pages that one, and the focus stays where it is.
    pointer("move", str(1280 + 640), "360", "scroll", "-15")
    desktop.wait_for(lambda: shown() == {"HEADLESS-1": (1, True), "HEADLESS-2": (2, False)},
                     "Super + wheel paged the monitor under the pointer")
    assert desktop.windows()[0].focused, desktop.windows()
    pointer("scroll", "15", "key", "super", "up")
    desktop.wait_for(lambda: shown()["HEADLESS-2"] == (1, False), "back to workspace 1")

    # Zoom's modifier takes the wheel first.
    pointer("move", str(x + width // 2), str(y + height // 2))
    desktop.reload(config("zoom = { scroll_modifier = 'Super', duration = 0 },"))
    pointer("key", "super", "down", "scroll", "-15", "key", "super", "up")
    desktop.wait_for(lambda: msg("get", "zoom").split()[:2] == ["1250", "1250"],
                     "Super + wheel zoomed in", detail=lambda: msg("get", "zoom"))
    assert shown()["HEADLESS-1"] == (1, True), shown()
    msg("zoom_reset")

    # "none" hands the wheel back to the window.
    desktop.reload(config("bindings = { { mods = { 'Super' }, button = 'wheel_up', "
                          "action = 'none' } },"))
    pointer("key", "super", "down", "scroll", "-15", "key", "super", "up")
    desktop.wait_for(lambda: turns()[1:] == ["pointer axis 0 -15"], "the window scrolled")
    assert shown()["HEADLESS-1"] == (1, True), shown()
print("Wheel bindings passed")
