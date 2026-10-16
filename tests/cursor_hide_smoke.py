# SPDX-License-Identifier: GPL-3.0-or-later
"""mouse.hide_when_typing hides the cursor as a key is typed into a window, until the pointer moves,
clicks or scrolls; a modifier alone and a key a binding takes leave it. mouse.hide_after hides it
once the pointer has rested that long. A screenshot's hiding is a reason of its own: either ending
leaves the cursor hidden while the other holds. Turning the settings off shows it again."""
from pathlib import Path
import sys

import harness

compositor, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false },
    mouse = { focus_follows = false, hide_when_typing = %s, hide_after = %d },
    bindings = { { mods = { "Super" }, key = "p", action = "palette" } },
}"""
# evdev's codes
A, SHIFT, SUPER, P = 30, 42, 125, 25

with harness.Compositor(compositor, CONFIG % ("true", 0)) as desktop:
    msg = desktop.msg

    def cursor():
        return msg("get", "cursor").strip()

    desktop.detail = lambda: f"cursor: {cursor()}, image: {msg('get', 'cursor_image').strip()}"
    desktop.open_window(probe, "Typing", focused=True)
    press = desktop.keyboard()
    pointer = desktop.virtual_pointer(pointer_probe, 1280, 720)
    x, y, w, h = desktop.windows()[0].box
    pointer(f"move {x + w // 2} {y + h // 2}")
    assert cursor() == "shown", cursor()

    # Typed into the window, the key hides the cursor; the pointer moving shows it again.
    press(A)
    assert cursor() == "hidden", cursor()
    assert msg("get", "cursor_image") == "none\n"
    pointer(f"move {x + w // 2 + 5} {y + h // 2}")
    assert cursor() == "shown", cursor()
    # So do a click and the wheel.
    for used in ("click left", "scroll 15"):
        press(A)
        assert cursor() == "hidden", cursor()
        pointer(used)
        assert cursor() == "shown", (used, cursor())

    # Shift alone, Super alone and Super + P (a binding's) leave it.
    press(SHIFT)
    press(SUPER)
    press(SUPER, P)
    desktop.stays(lambda: cursor() == "shown", "the cursor shown after modifiers and a binding")

    # A screenshot's hiding and typing's are apart: the pointer moving ends typing's alone, and
    # "cursor show" the screenshot's alone.
    press(A)
    msg("cursor", "hide")
    pointer(f"move {x + w // 2} {y + h // 2}")
    assert cursor() == "hidden", "the screenshot's hiding outlasts the pointer moving"
    msg("cursor", "show")
    assert cursor() == "shown", cursor()
    msg("cursor", "hide")
    press(A)
    msg("cursor", "show")
    assert cursor() == "hidden", "typing's hiding outlasts the screenshot"
    pointer(f"move {x + w // 2 + 5} {y + h // 2}")
    assert cursor() == "shown", cursor()

    # Turned off, it shows a cursor it hid, and typing hides it no more.
    press(A)
    desktop.reload(CONFIG % ("false", 0))
    assert cursor() == "shown", cursor()
    press(A)
    desktop.stays(lambda: cursor() == "shown", "typing hides nothing with the setting off")

    # hide_after: the pointer resting a second hides it, and moving shows it until the next rest.
    desktop.reload(CONFIG % ("false", 1))
    desktop.wait_for(lambda: cursor() == "hidden", "the cursor hidden after a rest")
    pointer(f"move {x + w // 2} {y + h // 2}")
    assert cursor() == "shown", cursor()
    desktop.stays(lambda: cursor() == "shown", "shown while the pointer has just moved", .5)
    desktop.wait_for(lambda: cursor() == "hidden", "hidden after the next rest")
    desktop.reload(CONFIG % ("false", 0))
    assert cursor() == "shown", cursor()
