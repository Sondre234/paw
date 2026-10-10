# SPDX-License-Identifier: GPL-3.0-or-later
"""Snapping by dragging (windows.snap): a floating window dropped with the pointer at the left or
right edge of its monitor fills that half, in a corner that quarter, at the top it is maximized,
a panel along an edge counting as the edge; restoring it puts it back where it floated before
the drag, and dragging a snapped window away gives it its size back. Edges shared with another
monitor do not snap, the monitor under the pointer is the one snapped on, a tile lifted out of a
tiling monitor goes back into the tiling where it is dropped but at the top, and the settings
turn it off, widen the edges and leave the corners to the sides. Driven by a virtual pointer
(pointer_probe); the window asks for the move as a client-decorated one does (wayland_probe with
PAW_PROBE_MOVE)."""
from pathlib import Path
import sys

import harness

compositor, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])
grim = sys.argv[4] if len(sys.argv) > 4 else ""

SCREEN = (1280, 720)
PANEL = 48


def config(snap="", tiling="false", animations="enabled = false", windows=""):
    return """return {
    xwayland = false,
    animations = { %s },
    layout = { tiling = %s, gap = 0 },
    outputs = { order = { "HEADLESS-1", "HEADLESS-2" },
                monitors = { ["HEADLESS-1"] = { mode = "1280x720" },
                             ["HEADLESS-2"] = { mode = "800x600" } } },
    windows = { magnet = { enabled = false }, snap = { %s }, %s },
}""" % (animations, tiling, snap, windows)


def session(desktop, layout):
    msg = desktop.msg

    def windows():
        """By title: x, y, width, height, tiled, output."""
        return {w.title: (*w.box, w.tiled, w.output) for w in desktop.windows()}

    def snap():
        """The zone, its slot and whether a window is being moved."""
        row = next(r for r in desktop.rows("snap") if r[0] == "zone")
        return row[1], tuple(int(n) for n in row[2:6]), row[6] == "1"

    def preview():
        """Whether the preview shows, where it is drawn, how far it has faded in (of 1000) and
        the radius of its corners."""
        row = next(r for r in desktop.rows("snap") if r[0] == "preview")
        return row[1] == "1", tuple(int(n) for n in row[2:6]), int(row[6]), int(row[7])
    snap.preview = preview

    desktop.detail = lambda: f"windows: {windows()} snap: {snap()}"
    pointer = desktop.virtual_pointer(pointer_probe, *layout)

    def press(title):
        """Presses near the top of the window, which asks to be moved, and waits for the move."""
        x, y, w, h = windows()[title][:4]
        pointer("move", str(x + min(100, w // 2)), str(y + min(20, h // 2)), "press", "left")
        desktop.wait_for(lambda: snap()[2], "the window is moved")

    def to(x, y, zone):
        pointer("move", str(x), str(y))
        desktop.wait_for(lambda: snap()[0] == zone, f"zone {zone} at {x}, {y}")
        return snap()[1]

    def release():
        pointer("release", "left")
        desktop.wait_for(lambda: not snap()[2], "the move ends")

    def placed(title, box):
        desktop.wait_for(lambda: windows()[title][:4] == tuple(box), f"{title} at {box}")

    return windows, snap, pointer, press, to, release, placed


# One monitor, with a panel along the bottom of the first (the probe's "P").
with harness.Compositor(compositor, config(), env={"WLR_HEADLESS_OUTPUTS": "1"}) as desktop:
    windows, snap, pointer, press, to, release, placed = session(desktop, SCREEN)
    desktop.open_window(probe, "P", args=("--external-control",))
    desktop.open_window(probe, "W", env={"PAW_PROBE_MOVE": "1"})
    W, H = SCREEN[0], SCREEN[1] - PANEL  # the area the panel leaves
    assert windows()["W"][2:4] == (320, 240), windows()

    # The left edge: the left half, below which nothing snaps; then back where it was.
    start = windows()["W"][:4]
    press("W")
    to(640, 360, "none")
    assert to(3, 360, "left") == (0, 0, W // 2, H)
    to(20, 360, "none")
    assert to(7, 360, "left") == (0, 0, W // 2, H)
    release()
    placed("W", (0, 0, W // 2, H))
    assert snap()[0] == "none", snap()
    desktop.msg("restore")
    placed("W", start)

    # The right edge, the top, and the corners, the panel counting as the bottom edge.
    for x, y, zone, box in [(W - 1, 300, "right", (W // 2, 0, W // 2, H)),
                            (640, 0, "maximize", (0, 0, W, H)),
                            (0, 0, "top_left", (0, 0, W // 2, H // 2)),
                            (W - 1, 2, "top_right", (W // 2, 0, W // 2, H // 2)),
                            (1, SCREEN[1] - 10, "bottom_left", (0, H // 2, W // 2, H // 2)),
                            (W - 3, H - 4, "bottom_right", (W // 2, H // 2, W // 2, H // 2))]:
        press("W")
        assert to(x, y, zone) == box, (zone, snap())
        release()
        placed("W", box)
        desktop.msg("restore")
        placed("W", start)

    # The bottom edge alone snaps nothing.
    press("W")
    to(640, SCREEN[1] - 10, "none")
    to(640, 300, "none")
    release()
    assert windows()["W"][2:4] == (320, 240), windows()

    # Dragging a snapped window away gives it its size back at once, and it floats where it is
    # dropped.
    press("W")
    to(0, 360, "left")
    release()
    placed("W", (0, 0, W // 2, H))
    press("W")
    desktop.wait_for(lambda: windows()["W"][2:4] == (320, 240), "its size back")
    to(600, 300, "none")
    release()
    assert windows()["W"][2:4] == (320, 240), windows()

    # Further from the edge with a larger distance; without corners the sides take them.
    desktop.reload(config("distance = 30, corners = false"))
    press("W")
    assert to(25, 300, "left") == (0, 0, W // 2, H)
    to(0, 0, "left")
    to(W - 1, SCREEN[1] - 1, "right")
    to(640, 20, "maximize")
    to(640, 40, "none")
    release()

    # Off, nothing snaps, the top included.
    desktop.reload(config("enabled = false"))
    press("W")
    pointer("move", "640", "0", "move", "0", "300")
    assert snap()[0] == "none", snap()
    release()
    assert windows()["W"][2:4] == (320, 240), windows()

# Two monitors side by side, the second shorter: their shared edge does not snap where the other
# continues, and the monitor under the pointer is the one snapped on.
LAYOUT = (2080, 720)
with harness.Compositor(compositor, config(), env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    windows, snap, pointer, press, to, release, placed = session(desktop, LAYOUT)
    desktop.open_window(probe, "W", env={"PAW_PROBE_MOVE": "1"})
    assert windows()["W"][5] == "HEADLESS-1", windows()

    press("W")
    to(1279, 300, "none")      # beside the second monitor
    to(1279, 650, "right")     # below it, where the edge is the first's alone
    to(1280, 300, "none")      # across, on the second's left edge
    assert to(1280 + 799, 300, "right") == (1280 + 400, 0, 400, 600)
    release()
    placed("W", (1280 + 400, 0, 400, 600))
    assert windows()["W"][5] == "HEADLESS-2", windows()
    press("W")
    assert to(1280 + 400, 0, "maximize") == (1280, 0, 800, 600)
    release()
    placed("W", (1280, 0, 800, 600))
    press("W")
    assert to(0, 400, "left") == (0, 0, 640, 720)
    release()
    placed("W", (0, 0, 640, 720))
    assert windows()["W"][5] == "HEADLESS-1", windows()

# A tiling monitor takes a tile dragged to its edge back into its tiling, where the pointer is;
# the top still maximizes it.
with harness.Compositor(compositor, config(tiling="true"),
                       env={"WLR_HEADLESS_OUTPUTS": "1"}) as desktop:
    windows, snap, pointer, press, to, release, placed = session(desktop, SCREEN)
    for title in ("A", "B"):
        desktop.spawn([probe, "--window-only"], env={"PAW_PROBE_TITLE": title})
        desktop.wait_for(lambda: title in windows() and windows()[title][4], f"{title} tiled")
    right = "A" if windows()["A"][0] > 0 else "B"

    def alt_press(title):
        pointer("key", "alt", "down")
        press(title)
        pointer("key", "alt", "up")

    alt_press(right)
    to(2, 400, "none")
    release()
    desktop.wait_for(lambda: windows()[right][4] and windows()[right][0] == 0,
                     "the tile dropped at the left edge tiled on the left")
    alt_press(right)
    assert to(640, 0, "maximize") == (0, 0, 1280, 720)
    release()
    placed(right, (0, 0, 1280, 720))
    assert not windows()[right][4], windows()

# The preview, with animations slowed down to watch it: it eases out of the window into the slot,
# glides along the edge to a corner, fades out as the pointer leaves the edge, and goes at once as
# the window is dropped. Its corners are those of a window there.
SLOW = "speed = 0.3, move = { duration = 300 }, close = { duration = 300 }"
with harness.Compositor(compositor, config(animations=SLOW, windows='round = "always"'),
                        env={"WLR_HEADLESS_OUTPUTS": "1"}) as desktop:
    windows, snap, pointer, press, to, release, placed = session(desktop, SCREEN)
    preview = snap.preview
    desktop.open_window(probe, "W", env={"PAW_PROBE_MOVE": "1"})
    assert preview()[0] is False, preview()
    left = (0, 0, 640, 720)
    press("W")
    to(640, 360, "none")
    assert preview()[0] is False, preview()
    assert to(0, 360, "left") == left
    desktop.wait_for(lambda: preview()[0], "the preview shows")
    shown, drawn, opacity, radius = preview()
    assert opacity < 1000 and drawn != left, preview()
    desktop.wait_for(lambda: preview()[1:3] == (left, 1000), "the preview in the slot")
    if grim:  # the preview tints the desktop it covers
        tinted = harness.grab(grim, desktop.env).at(20, 700)
    assert to(0, 0, "top_left") == (0, 0, 640, 360)
    assert preview()[0] and preview()[2] == 1000, preview()
    desktop.wait_for(lambda: preview()[1] == (0, 0, 640, 360), "the preview in the corner")
    to(640, 360, "none")
    desktop.wait_for(lambda: preview()[0] and preview()[2] < 1000, "the preview fading out")
    assert preview()[1] == (0, 0, 640, 360), preview()
    desktop.wait_for(lambda: not preview()[0], "the preview faded out")
    if grim:
        assert harness.grab(grim, desktop.env).at(20, 700) != tinted, tinted
    to(0, 360, "left")
    desktop.wait_for(lambda: preview()[1:3] == (left, 1000), "the preview in the slot again")
    release()
    assert not preview()[0], preview()
    placed("W", left)
    frames = {r[1]: int(r[5]) for r in desktop.rows("frames")}
    assert radius == frames["W"], (radius, frames)
    desktop.msg("restore")
    desktop.wait_for(lambda: windows()["W"][2:4] == (320, 240), "W restored")

    # Without the preview the zone still snaps.
    desktop.reload(config(snap="preview = false", animations=SLOW))
    press("W")
    to(1279, 360, "right")
    assert not preview()[0], preview()
    release()
    placed("W", (640, 0, 640, 720))
print("Windows dropped at the edges snap to halves, quarters and the whole monitor, with a preview"
      + ("" if grim else " (pixels not checked: grim missing)"))
