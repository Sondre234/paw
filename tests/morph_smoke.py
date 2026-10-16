# SPDX-License-Identifier: GPL-3.0-or-later
"""A floating window maximized, restored, snapped (from the keyboard and dropped at an edge),
resized and moved by the keyboard, or made fullscreen glides to its new place and size together:
`get windows` says where it is going at once (and the size the client has drawn), `get
animations` counts the change, and `get drawn`
shows it on its way, the client's picture stretched to the size drawn and the frame drawn around
it, fading as the window becomes maximized and back as it is restored. Reversed half way, it
goes back from where it is drawn. A client slow to draw the new size is waited for, stretched to
it, and what it drew before crossfades into what it draws; one that does not draw it in time is
drawn as it last drew. Closing, moving to another workspace, going fullscreen and turning
animations off part way leave nothing behind, and with animations off a window takes its place
at once. Driven by the control socket and a virtual pointer (pointer_probe)."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

SCREEN = (1280, 720)
FULL = (0, 0, 1280, 720)
BORDER = 2
# Long enough that the test sees each change on its way, under AddressSanitizer too, where a
# query takes some 30 ms; and no frame there, however late, lands an animation.
DURATION = 700


def config(enabled=True, speed=1, windows=""):
    return f"""return {{
    xwayland = false,
    animations = {{ enabled = {str(enabled).lower()}, duration = {DURATION}, late_frame_ms = 0,
                    speed = {speed}, open = {{ duration = 0 }}, close = {{ duration = 0 }},
                    focus = {{ duration = 0 }} }},
    layout = {{ tiling = false, gap = 0 }},
    outputs = {{ monitors = {{ ["HEADLESS-1"] = {{ mode = "1280x720" }} }} }},
    windows = {{ border_width = {BORDER}, magnet = {{ enabled = false }}, {windows} }},
}}"""


def inside(slot):
    """A slot's window, inside its border."""
    x, y, w, h = slot
    return x + BORDER, y + BORDER, w - 2 * BORDER, h - 2 * BORDER


def between(a, b, value):
    return min(a, b) < value < max(a, b)


def between_or_at(start, end, box):
    """Whether each of `box`'s values lies from `start`'s to `end`'s, either included."""
    return all(min(s, e) <= v <= max(s, e) for s, e, v in zip(start, end, box))


def on_the_way(start, end, box):
    """Whether `box` lies on the way from `start` to `end`: strictly between them where they are
    far apart, else at either or between."""
    return all(between(s, e, v) if abs(e - s) > 8 else min(s, e) <= v <= max(s, e)
               for s, e, v in zip(start, end, box))


LEFT = inside((0, 0, 640, 720))

with harness.Compositor(compositor, config()) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def windows():
        return {w.title: w for w in desktop.windows()}

    def drawn():
        """By title: where the window is drawn (x, y, width, height), the size its surface is
        drawn at, what its change of size does, and how much of its frame shows (of 1000)."""
        return {r[1]: (tuple(int(n) for n in r[2:6]), (int(r[6]), int(r[7])), r[8], int(r[9]))
                for r in desktop.rows("drawn")}

    def animations():
        """Running animations, and window trees stacked in the scene."""
        return tuple(int(n) for n in msg("get", "animations").split("\t")[:2])

    def resting(title, box):
        """Drawn at `box` and at rest, its surface at that size, nothing running."""
        return drawn()[title][:3] == (box, box[2:], "rest") and animations()[0] == 0

    def growing(title, start, end, stretched=True):
        """Where it is drawn while on its way from `start` to `end`, both size and place, its
        surface stretched to the size drawn, else None: a caller checks more of that same look
        rather than look again later, when it may have moved on."""
        box, surface, state, _ = drawn()[title]
        on_its_way = (state == "moving" and on_the_way(start, end, box) and
                      (not stretched or (abs(surface[0] - box[2]) <= 1 and
                                         abs(surface[1] - box[3]) <= 1)))
        return box if on_its_way and animations()[0] >= 1 else None

    desktop.detail = lambda: (f"windows: {msg('get', 'windows')}drawn: {msg('get', 'drawn')}"
                              f"animations: {msg('get', 'animations')}")

    # Maximizing: the window grows into the whole output from where it floats, its picture
    # stretched, while `get windows` says where it goes at once; the frame fades out on the way.
    desktop.open_window(probe, "W", focused=True)
    wait_for(lambda: resting("W", windows()["W"].box), "W open")
    start = windows()["W"].box
    assert drawn()["W"][3] == 1000, drawn()
    msg("maximize")
    assert windows()["W"].box[:2] == FULL[:2], windows()
    wait_for(lambda: growing("W", start, FULL), "W growing to fill the output")
    wait_for(lambda: 0 < drawn()["W"][3] < 1000, "its frame fading out")
    wait_for(lambda: resting("W", FULL) and drawn()["W"][3] == 0, "W maximized, frameless")

    # Restoring, reversed half way: it goes back from where it is drawn, never from either end.
    msg("restore")
    assert windows()["W"].box[:2] == start[:2], windows()
    wait_for(lambda: (box := growing("W", FULL, start)) and
             between(start[2] + 150, FULL[2] - 150, box[2]), "W shrinking")
    wait_for(lambda: 0 < drawn()["W"][3] < 1000, "its frame fading in")
    before = drawn()["W"][0]
    msg("maximize")
    after = drawn()["W"][0]
    assert between(start[2], FULL[2], after[2]), (before, after)
    assert abs(after[2] - before[2]) < (FULL[2] - start[2]) // 3, (before, after)
    wait_for(lambda: resting("W", FULL), "W maximized again")
    msg("restore")
    wait_for(lambda: resting("W", start) and drawn()["W"][3] == 1000, "W restored, framed")

    # From the keyboard, a snap, a resize and a move glide too; a move alone keeps its size.
    msg("snap_left")
    wait_for(lambda: growing("W", start, LEFT), "W gliding into the left half")
    wait_for(lambda: resting("W", LEFT), "W snapped")
    assert drawn()["W"][3] == 1000, drawn()  # a snapped window keeps its frame
    msg("restore")
    wait_for(lambda: resting("W", start), "W back")
    msg("resize_right", "200")
    wider = (start[0], start[1], start[2] + 200, start[3])
    wait_for(lambda: growing("W", start, wider), "W widening")
    wait_for(lambda: resting("W", wider), "W wider")
    msg("move_right")
    moved = windows()["W"].box
    assert moved[0] > start[0] and moved[2:] == wider[2:], windows()
    wait_for(lambda: (box := drawn()["W"][0])[2:] == wider[2:] and between(start[0], moved[0],
                                                                            box[0]),
             "W gliding to the right edge at its size")
    wait_for(lambda: resting("W", moved), "W at the right edge")
    start = moved

    # A client slow to draw its new size: the window waits stretched to it, then what the
    # client drew before crossfades into what it draws.
    slow = desktop.spawn([probe, "--commands"], env={"PAW_PROBE_TITLE": "S"},
                         stdin=subprocess.PIPE, text=True)
    wait_for(lambda: "S" in windows() and windows()["S"].focused, "S open")
    wait_for(lambda: resting("S", windows()["S"].box), "S at rest")
    start_s = windows()["S"].box

    def tell(*lines):
        """Lines for the slow window, then a title they are done by once it shows."""
        for line in lines:
            slow.stdin.write(line + "\n")
        slow.stdin.flush()

    tell("hold", "title S held")
    wait_for(lambda: "S held" in windows(), "S holding its configures")
    msg("maximize")
    wait_for(lambda: drawn()["S held"][:3] == (FULL, FULL[2:], "waiting"),
             "S waiting, its old picture stretched to the output")
    tell("answer")
    wait_for(lambda: drawn()["S held"][2] == "fading", "S crossfading into its new picture")
    wait_for(lambda: resting("S held", FULL), "S maximized")

    # One that does not draw in time eases back to what it last drew, at its new place.
    tell("hold", "title S still")
    wait_for(lambda: "S still" in windows(), "S holding again")
    msg("restore")
    assert windows()["S still"].box[:2] == start_s[:2], windows()
    wait_for(lambda: drawn()["S still"][:3] == (start_s, start_s[2:], "waiting"),
             "S waiting, shrunk to its old size")
    wait_for(lambda: drawn()["S still"][2] == "moving" and
             between(start_s[2], FULL[2], drawn()["S still"][0][2]),
             "S easing back to the size it last drew")
    wait_for(lambda: resting("S still", (*start_s[:2], *FULL[2:])),
             "S drawn as it last drew, where it now is")
    tell("answer")
    wait_for(lambda: resting("S still", start_s), "S restored once it draws")

    # One that acknowledges its new size with what it drew before and draws it later, as some
    # video players do: the window goes on to the new size and waits there, rather than turn
    # back to the old one, and crossfades into the new picture once it comes.
    tell("hold", "ack", "title S late")
    wait_for(lambda: "S late" in windows(), "S to acknowledge early")
    msg("maximize")
    wait_for(lambda: drawn()["S late"][:3] == (FULL, FULL[2:], "waiting"),
             "S waiting at the new size, past its early acknowledgement")
    tell("answer")
    wait_for(lambda: drawn()["S late"][2] == "fading", "S crossfading into its new picture")
    wait_for(lambda: resting("S late", FULL), "S maximized")

    # Closing part way leaves nothing behind.
    msg("restore")
    wait_for(lambda: drawn()["S late"][2] == "moving", "S shrinking")
    slow.stdin.close()
    slow.terminate()
    desktop.reap(slow)
    wait_for(lambda: "S late" not in windows() and animations() == (0, 1), "S gone, cleanly")

    # Going fullscreen part way: it goes on from where it is drawn to the whole output, and
    # back into its slot.
    msg("snap_left")
    wait_for(lambda: drawn()["W"][2] == "moving" and
             between(start[3] + 60, LEFT[3] - 60, drawn()["W"][0][3]), "W snapping")
    before = drawn()["W"][0]
    msg("fullscreen")
    after = drawn()["W"][0]
    # Snapping and fullscreen both take it left, up and wider here: it goes on from before.
    assert between_or_at(before, FULL, after) and after != FULL, (before, after)
    assert windows()["W"].box[:2] == FULL[:2], windows()
    wait_for(lambda: resting("W", FULL) and drawn()["W"][3] == 0, "W fullscreen")
    msg("fullscreen")
    wait_for(lambda: growing("W", FULL, LEFT, stretched=False), "W shrinking into its slot")
    wait_for(lambda: resting("W", LEFT), "W in its slot")

    # Moved to another workspace part way, it goes on hidden to its end; turning animations off
    # part way lands it at once.
    msg("restore")
    wait_for(lambda: drawn()["W"][2] == "moving", "W restoring")
    msg("move_to_workspace", "2")
    assert drawn()["W"][2] == "moving" and not windows()["W"].visible, drawn()
    wait_for(lambda: animations()[0] == 0 and drawn()["W"][2] == "rest",
             "W at rest, hidden")
    msg("workspace", "2")
    wait_for(lambda: resting("W", start), "W at rest on workspace 2")
    msg("maximize")
    wait_for(lambda: drawn()["W"][2] == "moving", "W maximizing")
    desktop.reload(config(enabled=False))
    wait_for(lambda: animations()[0] == 0 and drawn()["W"][2] == "rest", "landed at once")
    wait_for(lambda: resting("W", FULL), "W maximized")

    # Without animations a window takes its place at once.
    msg("restore")
    assert animations()[0] == 0 and drawn()["W"][2] == "rest", drawn()
    wait_for(lambda: resting("W", start) and drawn()["W"][3] == 1000, "W restored at once")

# Dropping a window at an edge: it glides from where it was dropped into the slot, as the
# preview fades under it, and Snap Assist opens beside it.
with harness.Compositor(compositor, config()) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def windows():
        return {w.title: w for w in desktop.windows()}

    def drawn():
        return {r[1]: (tuple(int(n) for n in r[2:6]), (int(r[6]), int(r[7])), r[8])
                for r in desktop.rows("drawn")}

    def zone():
        row = next(r for r in desktop.rows("snap") if r[0] == "zone")
        return row[1], row[6] == "1"

    desktop.detail = lambda: f"windows: {msg('get', 'windows')}drawn: {msg('get', 'drawn')}"
    pointer = desktop.virtual_pointer(pointer_probe, *SCREEN)
    desktop.open_window(probe, "A")
    desktop.open_window(probe, "D", focused=True, env={"PAW_PROBE_MOVE": "1"})
    wait_for(lambda: drawn()["D"][2] == "rest", "D open")
    x, y, w, h = windows()["D"].box
    pointer("move", str(x + 100), str(y + 20), "press", "left")
    wait_for(lambda: zone()[1], "D moved by the pointer")
    pointer("move", "300", "300", "move", "1", "360")
    wait_for(lambda: zone()[0] == "left", "the left half offered")
    dropped = windows()["D"].box
    pointer("release", "left")
    wait_for(lambda: windows()["D"].box == LEFT, "D placed in the left half at once")
    wait_for(lambda: drawn()["D"][2] == "moving" and
             between(dropped[3], LEFT[3], drawn()["D"][0][3]), "D gliding into the slot")
    wait_for(lambda: drawn()["D"][:3] == (LEFT, LEFT[2:], "rest"), "D snapped")
    overview = msg("get", "overview").splitlines()
    assert overview[0].startswith("open") and overview[1].startswith("overview-assist "), overview
    msg("overview_cancel")

    # Dragged off its slot, it shrinks back to its own size under the pointer.
    pointer("move", "300", "20", "press", "left")
    wait_for(lambda: zone()[1], "D moved again")
    wait_for(lambda: windows()["D"].box[2:] == (w, h), "D given its size back")
    wait_for(lambda: drawn()["D"][2] == "moving" and drawn()["D"][0][2] > w, "D shrinking")
    pointer("move", "600", "300", "release", "left")
    wait_for(lambda: drawn()["D"][2] == "rest" and drawn()["D"][0][2:] == (w, h), "D floating")

    # A resize with the pointer begun as a window changes size lands the change first, slowed
    # down here to seconds.
    desktop.reload(config(speed=0.1))
    desktop.open_window(probe, "R", focused=True, env={"PAW_PROBE_RESIZE": "right"})
    wait_for(lambda: drawn()["R"][2] == "rest", "R open")
    msg("snap_right")
    msg("overview_cancel")  # Snap Assist
    wait_for(lambda: drawn()["R"][2] == "moving", "R gliding into the right half")
    x, y, w, h = windows()["R"].box
    pointer("move", str(x + w // 2), str(y + h // 2), "press", "left")
    wait_for(lambda: drawn()["R"][2] == "rest", "R at rest for its resize", timeout=3)
    pointer("release", "left")

# Leaving fullscreen for a slot that moved meanwhile (a panel came along the bottom), slowed down
# to see it on its way: it sets off from the whole output, where it is drawn, not from the old
# slot too, however far it has come by the time the test looks.
RULES = 'rules = { { title = "^P$", focus = false } },'
with harness.Compositor(compositor, config(windows=RULES)) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def windows():
        return {w.title: w for w in desktop.windows()}

    def drawn():
        return {r[1]: (tuple(int(n) for n in r[2:6]), r[8]) for r in desktop.rows("drawn")}

    desktop.detail = lambda: f"windows: {msg('get', 'windows')}drawn: {msg('get', 'drawn')}"
    desktop.open_window(probe, "W", focused=True)
    corner = inside((0, 360, 640, 360))
    msg("snap_bottom_left")
    wait_for(lambda: drawn()["W"] == (corner, "rest"), "W in the bottom left quarter")
    msg("fullscreen")
    wait_for(lambda: drawn()["W"] == (FULL, "rest"), "W fullscreen")
    desktop.spawn([probe, "--external-control"], env={"PAW_PROBE_TITLE": "P"})
    above_panel = (0, 0, 1280, 720 - 48)
    wait_for(lambda: "P" in windows() and drawn()["W"][0] == above_panel, "W above the panel")
    assert windows()["W"].focused, windows()
    desktop.reload(config(speed=0.1, windows=RULES))
    msg("fullscreen")
    row = next(r for r in desktop.rows("drawn") if r[1] == "W")
    box, state, set_off = tuple(int(n) for n in row[2:6]), row[8], (int(row[11]), int(row[12]))
    quarter = inside((0, 336, 640, 336))
    assert state == "moving" and set_off == above_panel[:2] and \
        between_or_at(above_panel, quarter, box), (box, state, set_off)
    wait_for(lambda: windows()["W"].box == quarter, "W in the moved quarter")
# A window drawing its own shadow while it floats and none maximized, as GTK's do: paw draws no
# shadow nor rounded corners for it on the way to maximized and back, as it draws none at either
# end, though the window drops its shadow part way.
OWN = 'round = "always", corner_radius = 12, shadow = { enabled = true },'
with harness.Compositor(compositor, config(windows=OWN)) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def frames():
        """By title: the radius of the corners paw rounds, and whether it draws a shadow."""
        return {r[1]: (int(r[5]), r[6] == "1") for r in desktop.rows("frames")}

    def state():
        return next(r[8] for r in desktop.rows("drawn") if r[1] == "G")

    desktop.detail = lambda: f"frames: {msg('get', 'frames')}drawn: {msg('get', 'drawn')}"
    desktop.open_window(probe, "G", focused=True,
                        env={"PAW_PROBE_SHADOW": "12", "PAW_PROBE_SHADOW_FLOATING": "1"})
    wait_for(lambda: frames().get("G") == (0, False) and state() == "rest", "G floating")
    for action in ("maximize", "restore"):
        msg(action)
        seen = set()

        def done():
            seen.add(frames()["G"])
            return state() == "rest" and int(msg("get", "animations").split("\t")[0]) == 0

        wait_for(done, f"G {action}d")
        assert seen == {(0, False)}, (action, seen)
print("Windows glide to their new place and size together, wait for slow clients, and turn back")
