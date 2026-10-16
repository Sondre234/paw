# SPDX-License-Identifier: GPL-3.0-or-later
"""mouse.hide_when_typing hides the cursor as a key is typed into a window, until the pointer moves,
clicks or scrolls; a modifier alone and a key a binding takes leave it. mouse.hide_after hides it
once the pointer has rested that long. A screenshot's hiding is a reason of its own: either ending
leaves the cursor hidden while the other holds. Turning the settings off shows it again. A window's
own cursor image comes back without the pointer leaving and entering it anew, and a window that
locks the pointer (a game) or set no cursor image has nothing hidden."""
from pathlib import Path
import sys

import harness

compositor, probe, pointer_probe, input_probe = (str(Path(p).resolve()) for p in sys.argv[1:5])

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

    # A window with a cursor image of its own: hidden by typing, it comes back as the pointer
    # moves, without the pointer leaving the window and entering it anew to have it set again.
    desktop.reload(CONFIG % ("true", 0))

    def log(title):
        return (desktop.root / f"{title}.log").read_text().splitlines()

    def heard(title, prefix):
        return sum(line.startswith(prefix) for line in log(title))

    def window(title, *options):
        desktop.spawn([input_probe, "--no-gestures", "--no-touch", "--no-tablet", *options, title],
                      log=f"{title}.log")
        desktop.wait_for(lambda: "ready" in log(title) and
                         any(w.title == title and w.focused for w in desktop.windows()),
                         f"{title} mapped and focused")
        x, y, w, h = next(w.box for w in desktop.windows() if w.title == title)
        pointer(f"move {x + w // 2} {y + h // 2}")
        desktop.wait_for(lambda: heard(title, "pointer enter") == 1, f"the pointer in {title}")
        return x + w // 2, y + h // 2

    def moved_inside(title, x, y):
        """Moves the pointer within `title`, and says whether it left and entered again."""
        enters, leaves, motions = (heard(title, what) for what in
                                   ("pointer enter", "pointer leave", "pointer motion"))
        pointer(f"move {x} {y}")
        desktop.wait_for(lambda: heard(title, "pointer motion") > motions, f"motion in {title}")
        return heard(title, "pointer enter") != enters or heard(title, "pointer leave") != leaves

    x, y = window("Own cursor", "--cursor")
    desktop.wait_for(lambda: msg("get", "cursor_image").strip() == "client", "its own cursor")
    press(A)
    assert cursor() == "hidden", cursor()
    assert not moved_inside("Own cursor", x + 5, y), "the pointer left and entered it again"
    assert cursor() == "shown" and msg("get", "cursor_image").strip() == "client", cursor()

    # A window that set no cursor image, as a game or a video playing does: nothing to hide.
    x, y = window("No cursor", "--no-cursor")
    press(A)
    desktop.stays(lambda: cursor() == "shown", "a cursor the window took away was hidden")
    assert not moved_inside("No cursor", x + 5, y), "the pointer left and entered it again"

    # A game that locks the pointer, with an image of its own: typing and resting hide nothing.
    x, y = window("Locked", "--cursor", "--lock")
    desktop.wait_for(lambda: "pointer locked" in log("Locked"), "the pointer locked")
    press(A)
    desktop.stays(lambda: cursor() == "shown", "the cursor hid over a locked pointer")
    desktop.reload(CONFIG % ("true", 1))
    desktop.stays(lambda: cursor() == "shown", "the cursor hid at rest over a locked pointer", 1.5)
    assert not moved_inside("Locked", x, y + 5), "the pointer left and entered it again"

