# SPDX-License-Identifier: GPL-3.0-or-later
"""Lifting a window moved with the pointer (windows.lift): once the pointer has taken it a few
pixels it grows a touch about the point it was taken by and its shadow grows softer and further
off, while where the window is (and so the pointer's hold on it) does not change; it stays lifted
without anything running, sets down onto an edge its magnet holds and lifts again as it is pulled
away, and settles back as it is dropped. A printed shadow moves further off instead. Animations
off lift it at once; resizing, and windows.lift = false, do not. Driven by a virtual pointer
(pointer_probe); the window asks for the move as a client-decorated one does (wayland_probe with
PAW_PROBE_MOVE)."""
from pathlib import Path
import sys

import harness

compositor, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])
grim = sys.argv[4] if len(sys.argv) > 4 else ""

SCREEN = (1280, 720)
SOFT = "enabled = true, blur = 30, offset = 10"
PRINTED = "enabled = true, blur = 0, offset = { 7, 7 }"


def config(animations="speed = 0.3", shadow=SOFT, lift="true"):
    return """return {
    xwayland = false,
    animations = { %s },
    layout = { tiling = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    windows = { round = "always", lift = %s, shadow = { %s } },
}""" % (animations, lift, shadow)


def session(desktop):
    def windows():
        """By title: x, y, width, height."""
        return {w.title: w.box for w in desktop.windows()}

    def lift():
        """By title: how far up and the scale drawn (thousandths), and the point it grows about."""
        return {r[0]: tuple(int(n) for n in r[1:5]) for r in desktop.rows("lift")}

    def shadow(title):
        """The box W's shadow covers, from its top-left corner."""
        row = next(r for r in desktop.rows("frames") if r[1] == title)
        return tuple(int(n) for n in row[8:12])

    def moving():
        return next(r for r in desktop.rows("snap") if r[0] == "zone")[6] == "1"

    desktop.detail = lambda: f"windows: {windows()} lift: {lift()}"
    pointer = desktop.virtual_pointer(pointer_probe, *SCREEN)

    def press(title, dx=100, dy=20):
        """Presses on the window, which asks to be moved, and waits for the move."""
        x, y = windows()[title][:2]
        pointer("move", str(x + dx), str(y + dy), "press", "left")
        desktop.wait_for(moving, "the window is moved")
        return x + dx, y + dy

    def release():
        pointer("release", "left")
        desktop.wait_for(lambda: not moving(), "the move ends")

    return windows, lift, shadow, pointer, press, release


with harness.Compositor(compositor, config(), env={"WLR_HEADLESS_OUTPUTS": "1"}) as desktop:
    windows, lift, shadow, pointer, press, release = session(desktop)
    wait_for = desktop.wait_for
    desktop.open_window(probe, "W", env={"PAW_PROBE_MOVE": "1"})
    wx, wy, ww, wh = windows()["W"]
    rest = shadow("W")
    assert rest[2] > ww and rest[3] > wh and rest[1] + rest[3] - wh > -rest[1], rest

    # A press alone, and a move of a pixel or two, lift nothing: a click on a title bar.
    px, py = press("W")
    pointer("move", str(px + 2), str(py + 1))
    desktop.stays(lambda: not lift(), "lifted by a click")
    # Taken further, it lifts about the point the pointer holds it by.
    pointer("move", str(px + 40), str(py + 30))
    wait_for(lambda: "W" in lift(), "W lifts")
    wait_for(lambda: lift()["W"][0] == 1000, "W up")
    up, scale, cx, cy = lift()["W"]
    assert 1015 <= scale <= 1025 and (cx, cy) == (100, 20), lift()
    # Where the window is does not change: the pointer holds it where it took it.
    assert windows()["W"] == (wx + 40, wy + 30, ww, wh), windows()
    # The shadow reaches further all round, furthest below.
    lifted = shadow("W")
    assert (lifted[0] < rest[0] and lifted[1] < rest[1] and
            lifted[0] + lifted[2] > rest[0] + rest[2] and
            lifted[1] + lifted[3] - (rest[1] + rest[3]) > rest[1] - lifted[1]), (rest, lifted)
    # Held up, nothing runs and nothing is drawn.
    frames = lambda: int(desktop.rows("stats")[0][0])
    drawn = frames()
    desktop.stays(lambda: lift()["W"][0] == 1000 and frames() == drawn, "idle while lifted",
                  detail=lambda: f"{frames() - drawn} frames, {lift()}")
    assert desktop.rows("animations")[0][0] == "0", desktop.rows("animations")

    # Its magnet holding it against the output's left edge, it sets down; pulled away, it lifts.
    pointer("move", str(100 + 5), str(py + 30))
    wait_for(lambda: windows()["W"][0] == 0, "held by the left edge")
    wait_for(lambda: "W" not in lift(), "set down onto the edge")
    assert shadow("W") == rest, shadow("W")
    pointer("move", str(100 + 60), str(py + 30))
    wait_for(lambda: lift().get("W", (0,))[0] == 1000, "lifted again away from the edge")

    # Dropped, it settles back where it is. Lifted, it was drawn larger: past its right edge it
    # covered what it does not after.
    if grim:
        lifted_shot = harness.grab(grim, desktop.env)
    release()
    wait_for(lambda: 0 < lift().get("W", (0,))[0] < 1000, "settling")
    wait_for(lambda: not lift(), "settled")
    assert windows()["W"][:2] == (60, wy + 30) and shadow("W") == rest, (windows(), shadow("W"))
    if grim:
        settled = harness.grab(grim, desktop.env)
        x, y = 60 + ww + 2, wy + 30 + wh // 2
        assert lifted_shot.at(x, y) != settled.at(x, y), (lifted_shot.at(x, y), settled.at(x, y))
        x = 60 + ww - 10  # inside it, the same
        assert lifted_shot.at(x, y) == settled.at(x, y), (lifted_shot.at(x, y), settled.at(x, y))

    # Resizing does not lift.
    pointer("key", "alt", "down")
    x, y, w, h = windows()["W"]
    pointer("move", str(x + w - 20), str(y + h - 20), "press", "right")
    pointer("key", "alt", "up")
    pointer("move", str(x + w + 40), str(y + h + 30))
    wait_for(lambda: windows()["W"][2] > w, "W resized")
    desktop.stays(lambda: not lift(), "lifted by a resize")
    pointer("release", "right")

# A printed shadow (no blur, offset) moves further off, as crisp.
with harness.Compositor(compositor, config(animations="enabled = false", shadow=PRINTED),
                        env={"WLR_HEADLESS_OUTPUTS": "1"}) as desktop:
    windows, lift, shadow, pointer, press, release = session(desktop)
    desktop.open_window(probe, "W", env={"PAW_PROBE_MOVE": "1"})
    wx, wy, ww, wh = windows()["W"]
    assert shadow("W") == (0, 0, ww + 7, wh + 7), shadow("W")
    px, py = press("W")
    # Without animations it is up at once, and down at once.
    pointer("move", str(px + 30), str(py + 30))
    desktop.wait_for(lambda: lift().get("W", (0,))[0] == 1000, "W lifted at once")
    assert shadow("W") == (0, 0, ww + 10, wh + 10), shadow("W")
    release()
    desktop.wait_for(lambda: not lift(), "W down at once")
    assert shadow("W") == (0, 0, ww + 7, wh + 7), shadow("W")

# windows.lift = false, and shadows off: a window moved is lifted without its shadow, or not.
for setting, expected in (("true", True), ("false", False)):
    with harness.Compositor(compositor, config(shadow="enabled = false", lift=setting),
                            env={"WLR_HEADLESS_OUTPUTS": "1"}) as desktop:
        windows, lift, shadow, pointer, press, release = session(desktop)
        desktop.open_window(probe, "W", env={"PAW_PROBE_MOVE": "1"})
        px, py = press("W")
        pointer("move", str(px + 30), str(py + 30))
        if expected:
            desktop.wait_for(lambda: lift().get("W", (0,))[0] == 1000, "lifted without a shadow")
        else:
            desktop.stays(lambda: not lift(), "lifted with windows.lift = false")
        release()
        desktop.wait_for(lambda: not lift(), "nothing lifted after the drop")
