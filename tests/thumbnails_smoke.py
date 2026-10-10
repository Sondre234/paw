# SPDX-License-Identifier: GPL-3.0-or-later
"""The taskbar's pictures of windows in a session: resting the pointer on a minimized window's
button shows a card above it, in the popover, with the window's picture, which the shell copied
from the compositor through the window control's capture source, scaled down by the compositor
to the picture's size; resting on the picture peeks at the window, which the compositor then
shows where it is, still minimized; leaving takes it all away, and the source with it. The copy
starts halfway into the delay and is in before the card opens, so that the card opens on the
picture: the shell's Wayland requests and events, which WAYLAND_DEBUG prints, say in which order.
The window is minimized, so its colours on the screen can only be the picture's, or the peek's.
Without grim the pixels are left out."""
from pathlib import Path
import re
import subprocess
import sys

import harness

compositor, shell, probe, window_probe, pointer_probe = (str(Path(p).resolve())
                                                          for p in sys.argv[1:6])
grim = sys.argv[6] if len(sys.argv) > 6 else ""

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    notifications = { enabled = false },
    shell = { panel_height = 52, thumbnails = { delay = 800 } },
}"""
WIDTH, HEIGHT, BAR = 1280, 720, 52
BODY = (0x41, 0x7b, 0xc4)  # the probe's window below its top band
# Where the card opens: above the window's button, near the bar's start.
AREA = (0, HEIGHT - BAR - 300, 600, HEIGHT - BAR)
# The middle of the picture on the card, which the output's edge keeps from centring over the
# button; and of the window's body, 320 x 240 at (40, 40), well clear of the card.
PICTURE, WINDOW = (141, 572), (200, 160)

with harness.Compositor(compositor, CONFIG, start=False) as desktop:
    root, env = desktop.root, desktop.env
    env.update(QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software",
               QT_FORCE_STDERR_LOGGING="1", XDG_DATA_HOME=str(root), XDG_DATA_DIRS=str(root),
               XDG_STATE_HOME=str(root), DBUS_SESSION_BUS_ADDRESS="disabled:")
    desktop.start()
    # Not one of the logs printed should the test fail, which would be mostly the protocol's.
    shell_log = root / "shell-protocol.log"

    def log():
        return shell_log.read_text(errors="replace")

    def messages():
        """What the shell printed but the protocol."""
        return "\n".join(line for line in log().splitlines() if not line.startswith("["))

    def layers():
        return {(row[0], row[1]): row[2:4] for row in desktop.rows("layers")}

    def windows():
        return desktop.windows()

    def body_pixels():
        """How many pixels above the bar have the window's colour, or None without grim."""
        shot = harness.grab(grim, env)
        if shot is None:
            return None
        left, top, right, bottom = AREA
        return sum(1 for y in range(top, bottom, 2) for x in range(left, right, 2)
                   if all(abs(a - b) <= 2 for a, b in zip(shot.at(x, y), BODY)))

    def body_at(*points):
        """Whether each point has the window's colour, or None without grim."""
        shot = harness.grab(grim, env)
        if shot is None:
            return None
        return [all(abs(a - b) <= 2 for a, b in zip(shot.at(*point), BODY)) for point in points]

    def peek():
        """Whether the window is peeked at and drawn, and how far it shows (thousandths)."""
        peeked, drawn, _, shown = desktop.rows("window_peek")[0][:4]
        return peeked == "1", drawn == "1", int(shown)

    desktop.detail = lambda: f"layers: {layers()}, windows: {windows()}\n{messages()[-1500:]}"
    with shell_log.open("w") as output:
        desktop.spawn([shell, "--config", str(desktop.config)], env={"WAYLAND_DEBUG": "client"},
                      stdout=output, stderr=subprocess.STDOUT)
    desktop.wait_for(lambda: "paw surface rendered: paw taskbar" in log(), "the panel")
    desktop.spawn([probe, "--window-only"])
    desktop.wait_for(lambda: len(windows()) == 1 and not windows()[0].minimized, "the window")
    subprocess.run([window_probe, "paw protocol probe", "minimize"], env=env, check=True,
                   timeout=30, stdout=subprocess.DEVNULL)
    desktop.wait_for(lambda: windows()[0].minimized, "the window minimized")
    popover = ("paw-popover", "HEADLESS-1")
    assert popover not in layers(), layers()
    assert not body_pixels(), "the window's colour on the screen before any picture"

    # The window's button follows the start button.
    pointer = desktop.virtual_pointer(pointer_probe, WIDTH, HEIGHT)
    pointer("move", "85", str(HEIGHT - BAR // 2))
    desktop.wait_for(lambda: layers().get(popover) == ["3", "1"], "the card in the popover")
    # The shell asked for the window's capture source and had a frame of it before it made the
    # popover's surface for the card.
    protocol = log()
    request = re.search(r"\.get_(?:scaled_)?capture_source\(", protocol)
    asked = request.start() if request else -1
    copied = re.search(r"ext_image_copy_capture_frame_v1#\d+\.ready\(", protocol)
    card = re.search(r"get_layer_surface\(.*\"paw-popover\"", protocol)
    assert card and 0 <= asked < card.start(), "the window's picture not asked for ahead of its card"
    assert copied and copied.start() < card.start(), "the window's picture not in ahead of its card"
    # Every other pixel of the 133 x 64 body, at least, in a picture shown at its own size.
    desktop.wait_for(lambda: (pixels := body_pixels()) is None or pixels > 1500,
                     "the window's picture on the card")
    assert windows()[0].minimized, windows()
    # The compositor scales the 320 x 240 window down to the picture's 240 x 150 box itself, and
    # the shell follows it in one session while the card is open.
    desktop.wait_for(lambda: desktop.rows("pictures") ==
                     [["240x150", "200x150", "1", "paw protocol probe"]],
                     "the picture's source, scaled to its box")

    # Resting on the picture peeks at the window: the compositor shows it where it is, while it
    # stays minimized, and the card stays over it all.
    assert body_at(PICTURE, WINDOW) in (None, [True, False]), "the picture is not where expected"
    assert peek() == (False, False, 0), peek()
    pointer("move", *map(str, PICTURE))
    desktop.wait_for(lambda: peek() == (True, True, 1000), "the window peeked at")
    assert windows()[0].minimized and layers().get(popover) == ["3", "1"], windows()
    desktop.wait_for(lambda: body_at(PICTURE, WINDOW) in (None, [True, True]),
                     "the window shown where it is")

    pointer("move", "900", "300")
    desktop.wait_for(lambda: popover not in layers(), "the card gone once the pointer left")
    desktop.wait_for(lambda: peek() == (False, False, 0), "the peek over")
    assert windows()[0].minimized, windows()
    assert not body_pixels() and body_at(WINDOW) in (None, [False]), \
        "the picture or the window left on the screen"
    desktop.wait_for(lambda: not desktop.rows("pictures"), "the picture's source gone with the card")

    for message in ("ReferenceError", "TypeError", "is not defined", "Cannot read"):
        assert message not in messages(), messages()
print("taskbar thumbnails: a minimized window's picture on the card above its button, and a peek "
      "at the window from it" + ("" if grim else " (no grim: no pixels)"))
