# SPDX-License-Identifier: GPL-3.0-or-later
"""A desktop's surface on the background layer that takes the keyboard on demand, as the shell's
does while it shows its icons, takes it as a window does: a click on it gives it the keyboard,
with the modifiers held there before the click, and the pointer merely passing over it never
does; focus following the mouse onto a window gives the keyboard back to the windows. A press on it
holds the pointer until it is let go, wherever the pointer goes meanwhile. Bindings
still run while it has it. Switching to a workspace without windows leaves it the keyboard, and
one with a window gives it to the window; a launcher holding the keyboard over it, and the lock,
give it back as they go."""
from pathlib import Path
import sys

import harness

compositor, wayland_probe, input_probe, pointer_probe, lock_probe = (
    str(Path(p).resolve()) for p in sys.argv[1:6])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    bindings = {
        { mods = { "Super" }, key = "1", action = "workspace", workspace = 1 },
        { mods = { "Super" }, key = "2", action = "workspace", workspace = 2 },
    },
}"""
# evdev's codes
SUPER, CTRL, KEY_1, KEY_2, KEY_A, BTN_LEFT = 125, 29, 2, 3, 30, 272

with harness.Compositor(compositor, CONFIG) as desktop:
    log = desktop.root / "desktop.log"

    def heard():
        return log.read_text().splitlines()

    def seat():
        return {row[0]: (row[1], row[2]) for row in desktop.rows("seat")}

    def keyboard():
        return seat()["keyboard"]

    def layers():
        return {row[0]: row[3:] for row in desktop.rows("layers")}

    def windows():
        return {w.title: w for w in desktop.windows()}

    desktop.detail = lambda: f"seat: {seat()}, layers: {layers()}, heard: {heard()[-12:]}"
    press = desktop.keyboard()
    desktop.spawn([input_probe, "--background", "--on-demand", "--keys", "--no-gestures",
                   "--no-tablet", "desktop"], log="desktop.log")
    desktop.wait_for(lambda: "ready" in heard() and layers().get("desktop") == ["1", "0", "on_demand"],
                     "the desktop shown, taking the keyboard on demand")
    desktop.open_window(wayland_probe, "A", focused=True)
    pointer = desktop.virtual_pointer(pointer_probe, 1280, 720)
    a = windows()["A"].box
    bare = (20, 20)
    assert not (a[0] <= bare[0] < a[0] + a[2] and a[1] <= bare[1] < a[1] + a[3]), a
    on_a = (a[0] + a[2] // 2, a[1] + a[3] // 2)

    # Passing over the desktop leaves the window the keyboard.
    pointer("move", *map(str, bare))
    desktop.stays(lambda: keyboard() == ("window", "A") and windows()["A"].focused,
                  "the window kept the keyboard as the pointer went onto the desktop")
    assert "keyboard enter" not in heard()

    # A click gives the desktop the keyboard, the modifiers held reaching it before the click.
    press.down(CTRL)
    pointer("click", "left")
    desktop.wait_for(lambda: keyboard() == ("layer", "desktop") and not windows()["A"].focused,
                     "a click on the desktop gave it the keyboard")
    press.up(CTRL)
    lines = heard()
    entered = lines.index("keyboard enter")
    clicked = lines.index(f"pointer button {BTN_LEFT} pressed", entered)
    assert "modifiers 4" in lines[entered:clicked], lines[entered:]
    press(KEY_A)
    desktop.wait_for(lambda: f"key {KEY_A} pressed" in heard(), "a key reached the desktop")

    # Bindings still run. A workspace without windows leaves the desktop the keyboard; one with a
    # window gives it to the window.
    press(SUPER, KEY_2)
    desktop.wait_for(lambda: desktop.msg("get", "workspace").strip() == "2", "workspace 2 shown")
    desktop.stays(lambda: keyboard() == ("layer", "desktop"),
                  "the desktop kept the keyboard on a workspace without windows")
    assert f"key {KEY_2} pressed" not in heard(), heard()
    press(SUPER, KEY_1)
    desktop.wait_for(lambda: keyboard() == ("window", "A") and windows()["A"].focused,
                     "the workspace with a window gave it the keyboard")

    # Focus following the mouse onto a window gives it the keyboard back.
    pointer("click", "left")
    desktop.wait_for(lambda: keyboard() == ("layer", "desktop"), "the desktop clicked again")
    pointer("move", *map(str, on_a))
    desktop.wait_for(lambda: keyboard() == ("window", "A") and windows()["A"].focused,
                     "moving onto the window gave it the keyboard")
    assert heard()[-1] != "keyboard enter", heard()

    # A press on the desktop holds the pointer until it is let go, as a selection band dragged off
    # it needs: over the window the desktop hears the motion and the release, the keyboard staying
    # with it, and the window gets the pointer, and with it the keyboard, only then.
    pointer("move", *map(str, bare), "press", "left")
    desktop.wait_for(lambda: keyboard() == ("layer", "desktop") and
                     heard()[-1] == f"pointer button {BTN_LEFT} pressed", "the desktop pressed")
    pressed = len(heard())
    pointer("move", *map(str, on_a))
    desktop.wait_for(lambda: f"pointer motion {on_a[0]} {on_a[1]}" in heard()[pressed:],
                     "the desktop hearing the pointer over the window")
    assert seat() == {"keyboard": ("layer", "desktop"), "pointer": ("layer", "desktop")}, seat()
    assert "pointer leave" not in heard()[pressed:], heard()[pressed:]
    pointer("release", "left")
    desktop.wait_for(lambda: seat() == {"keyboard": ("window", "A"), "pointer": ("window", "A")},
                     "the window given the pointer and the keyboard once let go")
    since = heard()[pressed:]
    assert since.index(f"pointer button {BTN_LEFT} released") < since.index("pointer leave"), since

    # A launcher taking the keyboard over the desktop, and the lock, give it back as they go.
    pointer("move", *map(str, bare), "click", "left")
    desktop.wait_for(lambda: keyboard() == ("layer", "desktop"), "the desktop clicked once more")
    search = desktop.spawn([input_probe, "--text-input", "--overlay", "Search"])
    desktop.wait_for(lambda: keyboard() == ("layer", "Search"), "the launcher took the keyboard")
    search.terminate()
    desktop.reap(search)
    desktop.wait_for(lambda: keyboard() == ("layer", "desktop"),
                     "the desktop got the keyboard back from the launcher")
    locker = desktop.spawn([lock_probe, "hold", str(desktop.root / "locker.log")])
    desktop.wait_for(lambda: keyboard() == ("other", "-"), "the lock took the keyboard")
    locker.terminate()
    assert desktop.reap(locker) == 0
    desktop.wait_for(lambda: keyboard() == ("layer", "desktop") and not windows()["A"].focused,
                     "the desktop got the keyboard back from the lock")
print("The desktop took the keyboard on a click and gave it back to the windows")
