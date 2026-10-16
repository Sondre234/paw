# SPDX-License-Identifier: GPL-3.0-or-later
"""mouse.shake_to_find: shaking the pointer quickly from side to side grows the cursor to three
times its size for a moment, then back to its size and its image; moving across the screen,
strokes too slow or too narrow, and a shake with a button held grow nothing, and neither does a
shake with the setting off. `get cursor_size` says the size it is drawn at, and with grim the
screen shows the cursor drawn that large."""
from pathlib import Path
import sys

import harness

compositor, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:3])
grim = sys.argv[3] if len(sys.argv) > 3 else ""

CONFIG = """return {
    xwayland = false,
    appearance = { background = "#000000" },
    mouse = { shake_to_find = %s },
}"""


def shake(x=400, width=300, strokes=7):
    """A line of moves from side to side, quick as the probe sends them, after a rest that has the
    compositor start counting strokes afresh."""
    return "sleep 150 " + " ".join(f"move {x + width * (i % 2)} 360" for i in range(strokes))


with harness.Compositor(compositor, CONFIG % "true") as desktop:
    msg = desktop.msg

    def size():
        return int(msg("get", "cursor_size"))

    def image():
        return msg("get", "cursor_image").strip()

    def drawn():
        """How many pixels the cursor covers on the black desktop, as grim -c shows it."""
        shot = harness.grab(grim, desktop.env, cursor=True)
        return sum(1 for i in range(0, len(shot.pixels), 3) if any(shot.pixels[i:i + 3]))

    base = size()  # $XCURSOR_SIZE, 24 unless the environment says otherwise
    desktop.detail = lambda: f"size: {size()} of {base}, image: {image()}"
    pointer = desktop.virtual_pointer(pointer_probe, 1280, 720)
    pointer("move 640 360")
    shown = image()
    covered = drawn() if grim else 0

    # Moving across the screen and back, a slow back and forth (as stroking the desktop cat) and
    # strokes too narrow grow nothing.
    pointer("sleep 150 move 100 100 sleep 150 move 1200 600 sleep 150 move 100 100")
    pointer("sleep 150 " + " ".join(f"move {600 + 30 * (i % 2)} 360 sleep 60" for i in range(8)))
    pointer(shake(width=30, strokes=10))
    desktop.stays(lambda: size() == base, "the cursor at its size after ordinary motion")

    # A shake grows it to three times its size (src/shake.c's tests have it grow smoothly), still
    # naming the image it shows, and it comes back to that size and image about a second later.
    pointer(shake())
    desktop.wait_for(lambda: size() == 3 * base, "the cursor grown three times by the shake")
    assert image() == shown, image()
    if grim:
        grown = drawn()
        assert grown > 4 * covered, (grown, covered)
    desktop.wait_for(lambda: size() == base, "the cursor back to its size", timeout=5)
    assert image() == shown, image()
    if grim:
        assert drawn() < 2 * covered, (drawn(), covered)

    # Shaking on keeps it grown past the time one shake does.
    pointer(shake())
    desktop.wait_for(lambda: size() == 3 * base, "grown again")
    for _ in range(6):
        pointer(shake(x=410).removeprefix("sleep 150 ") + " sleep 50")
    assert size() == 3 * base, "still grown while the shaking goes on"
    desktop.wait_for(lambda: size() == base, "back once the shaking stops", timeout=5)

    # A button held makes it a drag, not a shake.
    pointer("move 400 360 press left " + shake() + " release left")
    desktop.stays(lambda: size() == base, "no growing with a button held")

    # Turned off, a shake grows nothing.
    desktop.reload(CONFIG % "false")
    pointer(shake())
    desktop.stays(lambda: size() == base, "no growing with shake_to_find off")
