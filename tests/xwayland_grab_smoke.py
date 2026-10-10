# SPDX-License-Identifier: GPL-3.0-or-later
"""An X11 window's active keyboard grab (XGrabKeyboard), which Xwayland passes on through
xwayland-keyboard-grab-unstable-v1, holds the bindings' keys as a keyboard shortcuts inhibitor
does: while the window has the keyboard they reach it, toggle_shortcuts_inhibit takes them back
and gives them to it again, a window rule refuses it, and it ends as the client lets go. Only
Xwayland sees the global."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, x11_probe, wayland_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

CONFIG = """return {
    xwayland = true,
    layout = { tiling = false },
    mouse = { focus_follows = false },
    windows = { rules = { { title = "^Refused", shortcuts_inhibit = false } } },
    bindings = {
        { mods = { "Super" }, key = "t", action = "toggle_tiling" },
        { mods = { "Super", "Shift" }, key = "Escape", action = "toggle_shortcuts_inhibit" },
    },
}"""
# evdev's codes
SUPER, SHIFT, ESCAPE, T = 125, 42, 1, 20

with harness.Compositor(compositor, CONFIG) as desktop:
    msg = desktop.msg

    def log(name):
        return (desktop.root / f"{name}.log").read_text().splitlines()

    def focused():
        return next((w.title for w in desktop.windows() if w.focused), None)

    def shortcuts():
        """Whether the keys go to the focused window, and each inhibitor or grab as (kind, state,
        focused, window title)."""
        rows = desktop.rows("shortcuts")
        assert rows[0][0] == "inhibited", rows
        return rows[0][1] == "1", [(r[0], r[1], r[2] == "1", r[4]) for r in rows[1:]]

    def tiling():
        return msg("get", "tiling").strip() == "on"

    def press(*codes):
        """Presses the keys in order, then releases them the other way round."""
        for code in codes:
            msg("headless_keyboard", "key", "keys", str(code), "press")
        for code in reversed(codes):
            msg("headless_keyboard", "key", "keys", str(code), "release")

    def x11(title):
        process = desktop.spawn([x11_probe, "commands"], log=f"{title}.log",
                                env={"PAW_PROBE_TITLE": title}, stdin=subprocess.PIPE,
                                text=True)
        desktop.wait_for(lambda: "waiting for commands" in log(title) and focused() == title,
                         f"{title} mapped and focused")
        return process

    def tell(process, command):
        process.stdin.write(command + "\n")
        process.stdin.flush()

    desktop.detail = lambda: f"shortcuts: {shortcuts()}, focused: {focused()}"
    msg("headless_keyboard", "add", "keys")

    # A Wayland client is not offered the global.
    globals_ = subprocess.run([wayland_probe, "--globals"], env=desktop.env, capture_output=True,
                              text=True, timeout=10, check=True).stdout.split()
    assert "zwp_keyboard_shortcuts_inhibit_manager_v1" in globals_, globals_
    assert "zwp_xwayland_keyboard_grab_manager_v1" not in globals_, globals_

    # Without a grab the bindings run, and the window hears no keys.
    vm = x11("VM")
    press(SUPER, T)
    desktop.wait_for(tiling, "Super + T toggling tiling")
    msg("toggle_tiling")
    assert not [line for line in log("VM") if line.startswith("key ")], log("VM")

    # Grabbing, the window has the keys while it has the keyboard.
    tell(vm, "grab")
    desktop.wait_for(lambda: "grab taken" in log("VM"), "the grab taken")
    desktop.wait_for(lambda: shortcuts() == (True, [("grab", "active", True, "VM")]),
                     "the grab holding the keys")
    press(SUPER, T)
    desktop.wait_for(lambda: [line for line in log("VM") if line.startswith("key ")] ==
                     [f"key {SUPER} pressed", f"key {T} pressed", f"key {T} released",
                      f"key {SUPER} released"], "Super + T in the X11 window")
    assert not tiling()

    # The binding that takes them back, and gives them back.
    press(SUPER, SHIFT, ESCAPE)
    desktop.wait_for(lambda: shortcuts() == (False, [("grab", "off", True, "VM")]),
                     "the grab turned off")
    press(SUPER, T)
    desktop.wait_for(tiling, "Super + T with the grab off")
    msg("toggle_tiling")
    press(SUPER, SHIFT, ESCAPE)
    desktop.wait_for(lambda: shortcuts() == (True, [("grab", "active", True, "VM")]),
                     "the grab turned on again")

    # Let go, the bindings are back.
    tell(vm, "ungrab")
    desktop.wait_for(lambda: shortcuts() == (False, []), "the grab gone")
    press(SUPER, T)
    desktop.wait_for(tiling, "Super + T once the grab is gone")
    msg("toggle_tiling")

    # A window rule refuses a grab as it refuses an inhibitor.
    refused = x11("Refused")
    tell(refused, "grab")
    desktop.wait_for(lambda: shortcuts() == (False, [("grab", "refused", True, "Refused")]),
                     "the rule refusing the grab")
    press(SUPER, T)
    desktop.wait_for(tiling, "Super + T in a window whose grab is refused")
    vm.stdin.close()
    refused.stdin.close()
print("X11 windows' keyboard grabs hold the bindings' keys as inhibitors do, passed")
