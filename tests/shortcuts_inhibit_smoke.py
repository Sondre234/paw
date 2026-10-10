# SPDX-License-Identifier: GPL-3.0-or-later
"""Keyboard shortcuts inhibitors (keyboard-shortcuts-inhibit-unstable-v1), as virtual machines and
remote desktops ask for: while the window that asked has the keyboard, the keys the bindings take
go to it instead, and the bindings work again as another window has it or the inhibitor goes.
toggle_shortcuts_inhibit, whose binding still runs, turns the focused window's off and on again,
which the on-screen display says; the shell hears once for each window that it took the keys, and
which keys take them back. keyboard.shortcuts_inhibit = false and a window rule's
shortcuts_inhibit = false refuse them, a reload applies a change to either, and an inhibitor
taking effect leaves the binding mode in use. `get shortcuts` shows it all."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false },
    mouse = { focus_follows = false },
    keyboard = { shortcuts_inhibit = %s },
    windows = { rules = { { title = "^Refused$", shortcuts_inhibit = false } } },
    modes = { other = { { key = "Escape", action = "mode", mode = "default" } } },
    bindings = {
        { mods = { "Super" }, key = "t", action = "toggle_tiling" },
        { mods = { "Super" }, key = "grave", action = "focus_last" },
        { mods = { "Super", "Shift" }, key = "Escape", action = "toggle_shortcuts_inhibit" },
    },
}"""
# evdev's codes
SUPER, SHIFT, ESCAPE, T, GRAVE = 125, 42, 1, 20, 41

with harness.Compositor(compositor, CONFIG % "true") as desktop:
    msg = desktop.msg

    def log(name):
        return (desktop.root / f"{name}.log").read_text().splitlines()

    def heard(name, prefix):
        return [line for line in log(name) if line.startswith(prefix)]

    def focused():
        return next((w.title for w in desktop.windows() if w.focused), None)

    def shortcuts():
        """Whether the keys go to the focused window, and each inhibitor as (state, focused,
        window title)."""
        rows = desktop.rows("shortcuts")
        assert rows[0][0] == "inhibited", rows
        return rows[0][1] == "1", [(r[1], r[2] == "1", r[4]) for r in rows[1:]]

    def tiling():
        return msg("get", "tiling").strip() == "on"

    def press(*codes):
        """Presses the keys in order, then releases them the other way round."""
        for code in codes:
            msg("headless_keyboard", "key", "keys", str(code), "press")
        for code in reversed(codes):
            msg("headless_keyboard", "key", "keys", str(code), "release")

    def window(title, *options, **spawn):
        process = desktop.spawn([probe, "--keys", "--no-gestures", "--no-tablet", *options,
                                 title], log=f"{title}.log", **spawn)
        desktop.wait_for(lambda: "ready" in log(title) and focused() == title,
                         f"{title} mapped and focused")
        return process

    desktop.detail = lambda: f"shortcuts: {shortcuts()}, focused: {focused()}"
    msg("headless_keyboard", "add", "keys")
    assert shortcuts() == (False, []), shortcuts()
    notices = desktop.subscribe()

    def told(*titles):
        return [f"notice Shortcuts go to {title}\tSuper + Shift + Escape gives them back"
                for title in titles]

    # The window that asks has the shortcuts while it has the keyboard: Super + T reaches it, and
    # tiling stays as it was.
    vm = window("VM", "--inhibit")
    desktop.wait_for(lambda: heard("VM", "shortcuts") == ["shortcuts active"],
                     "the inhibitor active")
    assert shortcuts() == (True, [("active", True, "VM")]), shortcuts()
    press(SUPER, T)
    desktop.wait_for(lambda: heard("VM", "key ") == [f"key {SUPER} pressed", f"key {T} pressed",
                                                    f"key {T} released", f"key {SUPER} released"],
                     "Super + T in the window")
    assert not tiling()
    desktop.wait_for(lambda: notices.lines("notice ") == told("VM"), "the user told")

    # Another window has the keyboard: the bindings are back, and its keys stay the compositor's.
    other = window("Other")
    assert shortcuts() == (False, [("active", False, "VM")]), shortcuts()
    press(SUPER, T)
    desktop.wait_for(tiling, "Super + T toggling tiling")
    assert f"key {T} pressed" not in log("Other"), log("Other")
    msg("toggle_tiling")
    # Back to the window that asked, by a binding it does not hold yet: it holds them again.
    press(SUPER, GRAVE)
    desktop.wait_for(lambda: focused() == "VM" and shortcuts()[0], "the inhibitor again")
    press(SUPER, T)
    desktop.wait_for(lambda: heard("VM", f"key {T}")[-2:] == [f"key {T} pressed",
                                                              f"key {T} released"],
                     "Super + T in the window again")
    assert not tiling()
    # The inhibitor taking effect leaves a binding mode, so that the bindings it holds are the
    # ones outside any.
    msg("focus_last")
    desktop.wait_for(lambda: focused() == "Other", "Other focused")
    msg("mode", "other")
    assert msg("get", "mode").strip() == "other"
    msg("focus_last")
    desktop.wait_for(lambda: msg("get", "mode").strip() == "default", "the mode left")
    assert shortcuts()[0] and focused() == "VM", shortcuts()

    # Its binding still runs: it turns the inhibitor off, which the window hears, and the bindings
    # are back; the on-screen display says so. Escape does not reach the window.
    osd = desktop.subscribe()
    press(SUPER, SHIFT, ESCAPE)
    desktop.wait_for(lambda: heard("VM", "shortcuts")[-1] == "shortcuts inactive",
                     "the inhibitor turned off")
    assert shortcuts() == (False, [("off", True, "VM")]), shortcuts()
    assert f"key {ESCAPE} pressed" not in log("VM"), log("VM")
    desktop.wait_for(lambda: osd.lines("osd ") == ["osd HEADLESS-1 -1 Shortcuts back to the desktop"],
                     "the on-screen display saying so")
    press(SUPER, T)
    desktop.wait_for(tiling, "Super + T with the inhibitor off")
    msg("toggle_tiling")
    # It stays off as the window loses the keyboard and gets it back, until turned on again.
    msg("focus_last")
    msg("focus_last")
    desktop.wait_for(lambda: focused() == "VM", "VM focused again")
    assert shortcuts() == (False, [("off", True, "VM")]), shortcuts()
    press(SUPER, SHIFT, ESCAPE)
    desktop.wait_for(lambda: heard("VM", "shortcuts")[-1] == "shortcuts active",
                     "the inhibitor turned on again")
    assert shortcuts() == (True, [("active", True, "VM")]), shortcuts()
    desktop.wait_for(lambda: osd.lines("osd ")[1:] == ["osd HEADLESS-1 -1 Shortcuts go to VM"],
                     "the on-screen display naming the window")
    press(SUPER, T)
    desktop.wait_for(lambda: heard("VM", f"key {T}")[-2:] == [f"key {T} pressed",
                                                              f"key {T} released"],
                     "Super + T in the window once more")
    assert not tiling()
    # The control socket's action does the same.
    msg("toggle_shortcuts_inhibit")
    assert shortcuts() == (False, [("off", True, "VM")]), shortcuts()
    msg("toggle_shortcuts_inhibit")
    assert shortcuts() == (True, [("active", True, "VM")]), shortcuts()
    desktop.wait_for(lambda: heard("VM", "shortcuts")[-2:] == ["shortcuts inactive",
                                                               "shortcuts active"],
                     "the window hearing both")
    # The user heard of it once, however often it took the keys again.
    assert notices.lines("notice ") == told("VM"), notices.lines("notice ")

    # The window gone, the bindings are back.
    vm.terminate()
    desktop.reap(vm)
    desktop.wait_for(lambda: shortcuts() == (False, []), "the inhibitor gone")
    press(SUPER, T)
    desktop.wait_for(tiling, "Super + T once the window is gone")
    msg("toggle_tiling")

    # A window rule refuses a window: its client never hears it is active, and the binding does
    # not turn it on.
    refused = window("Refused", "--inhibit")
    desktop.wait_for(lambda: shortcuts() == (False, [("refused", True, "Refused")]),
                     "the rule refusing the inhibitor")
    press(SUPER, T)
    desktop.wait_for(tiling, "Super + T in a refused window")
    msg("toggle_tiling")
    press(SUPER, SHIFT, ESCAPE)
    msg("toggle_shortcuts_inhibit")
    assert shortcuts() == (False, [("refused", True, "Refused")]), shortcuts()
    assert heard("Refused", "shortcuts") == [], log("Refused")
    assert f"key {T} pressed" not in log("Refused"), log("Refused")
    refused.terminate()
    desktop.reap(refused)

    # keyboard.shortcuts_inhibit = false refuses every one; reloaded on, they are active, and off
    # again inactive, as their clients hear.
    desktop.reload(CONFIG % "false")
    commands = window("Asking", "--commands", stdin=subprocess.PIPE, text=True)
    commands.stdin.write("inhibit\n")
    commands.stdin.flush()
    desktop.wait_for(lambda: shortcuts() == (False, [("refused", True, "Asking")]),
                     "keyboard.shortcuts_inhibit = false refusing")
    press(SUPER, T)
    desktop.wait_for(tiling, "Super + T with inhibitors refused")
    msg("toggle_tiling")
    desktop.reload(CONFIG % "true")
    desktop.wait_for(lambda: heard("Asking", "shortcuts") == ["shortcuts active"],
                     "the inhibitor active once allowed")
    assert shortcuts() == (True, [("active", True, "Asking")]), shortcuts()
    desktop.wait_for(lambda: notices.lines("notice ") == told("VM", "Asking"), "the user told again")
    desktop.reload(CONFIG % "false")
    desktop.wait_for(lambda: heard("Asking", "shortcuts") == ["shortcuts active",
                                                              "shortcuts inactive"],
                     "the inhibitor inactive once refused")
    assert shortcuts() == (False, [("refused", True, "Asking")]), shortcuts()
    # Let go by its client, it is gone.
    desktop.reload(CONFIG % "true")
    commands.stdin.write("release\n")
    commands.stdin.flush()
    desktop.wait_for(lambda: shortcuts() == (False, []), "the inhibitor let go")
    commands.stdin.close()
    assert notices.lines("notice ") == told("VM", "Asking"), notices.lines("notice ")
print("Keyboard shortcuts inhibitors take the bindings' keys while focused, and are refused, passed")
