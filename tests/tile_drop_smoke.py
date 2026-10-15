# SPDX-License-Identifier: GPL-3.0-or-later
"""A tile moved with the pointer keeps its place in the tiling, which the snap preview shows: the
other tiles stay where they are as it is lifted, and once the pointer rests a moment on another
tile they glide aside to open the place a drop there would give it, without the clients being
asked for a new size at every motion; dropped, it tiles there. Leaving the tiling, for a monitor
that floats or the top edge that maximizes, closes its place; a drag cut short does too. The
layouts that keep a list open it at the end, and the scrolling layout keeps no place. A grab
another starts meanwhile (an X11 window asking to be moved, with x11_probe) ends it first. Driven
by a virtual pointer and keyboard (pointer_probe), Alt + drag."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])
x11_probe = str(Path(sys.argv[4]).resolve()) if len(sys.argv) > 4 else ""

SCREEN = (1280 + 800, 720)


def config(animations="enabled = false", layout="dwindle", xwayland="false"):
    return """return {
    xwayland = %s,
    animations = { %s },
    layout = { gap = 0, tile_layout = "%s", master_ratio = 0.5 },
    outputs = { order = { "HEADLESS-1", "HEADLESS-2" },
                monitors = { ["HEADLESS-1"] = { mode = "1280x720", tiling = true },
                             ["HEADLESS-2"] = { mode = "800x600", tiling = false } } },
    windows = { magnet = { enabled = false } },
    mouse = { focus_follows = false },
}""" % (xwayland, animations, layout)


def session(desktop):
    def windows():
        """By title: x, y, width, height, tiled, output."""
        return {w.title: (*w.box, w.tiled, w.output) for w in desktop.windows()}

    def preview():
        """Whether the snap preview shows, and where it is drawn."""
        row = next(r for r in desktop.rows("snap") if r[0] == "preview")
        return row[1] == "1", tuple(int(n) for n in row[2:6])

    def moving():
        return next(r for r in desktop.rows("snap") if r[0] == "zone")[6] == "1"

    def configures():
        return int(desktop.rows("stats")[0][5])

    desktop.detail = lambda: f"windows: {windows()} preview: {preview()}"
    pointer = desktop.virtual_pointer(pointer_probe, *SCREEN)

    def press(title, dx=100, dy=100):
        """Alt + press on a window, and a few pixels' move: the move begins."""
        x, y = windows()[title][:2]
        pointer("key", "alt", "down")
        pointer("move", str(x + dx), str(y + dy), "press", "left")
        pointer("key", "alt", "up")
        desktop.wait_for(moving, f"{title} is moved")
        pointer("move", str(x + dx + 10), str(y + dy + 5))

    def release():
        pointer("release", "left")
        desktop.wait_for(lambda: not moving(), "the move ends")

    def tiles(*titles):
        """Opens tiles in turn, each on the right of the last (the pointer there)."""
        pointer("move", "1200", "360")
        for title in titles:
            desktop.spawn([probe, "--window-only"], env={"PAW_PROBE_TITLE": title})
            desktop.wait_for(lambda: title in windows() and windows()[title][4], f"{title} tiled")

    return windows, preview, configures, pointer, press, release, tiles


LEFT, RIGHT = (0, 0, 640, 720), (640, 0, 640, 720)

with harness.Compositor(compositor, config(), env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    windows, preview, configures, pointer, press, release, tiles = session(desktop)
    wait_for = desktop.wait_for
    tiles("A", "B")
    assert windows()["A"][:4] == LEFT and windows()["B"][:4] == RIGHT, windows()

    # Lifted, A keeps its place: B stays where it is, and the preview shows the place.
    press("A")
    wait_for(lambda: preview() == (True, LEFT), "the preview on A's place")
    desktop.stays(lambda: windows()["B"][:4] == RIGHT, "B moved as A was lifted")
    assert not windows()["A"][4], windows()

    # Resting on B's right half, B moves aside to the left and A's place opens on the right;
    # moving about there asks the clients for nothing more.
    pointer("move", "1100", "360")
    wait_for(lambda: windows()["B"][:4] == LEFT, "B aside")
    wait_for(lambda: preview() == (True, RIGHT), "the preview on the new place")
    asked = configures()
    for x, y in ((1000, 200), (1200, 600), (900, 300), (700, 650), (1150, 100)):
        pointer("move", str(x), str(y))
    desktop.stays(lambda: configures() == asked and windows()["B"][:4] == LEFT,
                  "moving about the new place asked for more", duration=.4,
                  detail=lambda: f"{configures() - asked} configures, {windows()}")
    # Back over B, now on the left: A's place moves back to the left.
    pointer("move", "100", "360")
    wait_for(lambda: windows()["B"][:4] == RIGHT, "B back on the right")
    wait_for(lambda: preview() == (True, LEFT), "the preview back on the left")
    # Dropped over B's right half again, A tiles there.
    pointer("move", "1100", "360")
    wait_for(lambda: windows()["B"][:4] == LEFT, "B aside again")
    release()
    wait_for(lambda: windows()["A"][:5] == (*RIGHT, True), "A tiled on the right")
    assert not preview()[0], preview()

    # Dropped at once, before the tiles have moved, it goes where the pointer is all the same.
    press("A")
    pointer("move", "100", "360")
    release()
    wait_for(lambda: windows()["A"][:5] == (*LEFT, True) and windows()["B"][:4] == RIGHT,
             "A dropped at once on the left")

    # Taken to the monitor that floats, its place closes, and it floats there.
    press("A")
    pointer("move", "1700", "300")
    wait_for(lambda: windows()["B"][:4] == (0, 0, 1280, 720), "B filling the monitor")
    wait_for(lambda: not preview()[0], "no preview over the floating monitor")
    release()
    wait_for(lambda: windows()["A"][5] == "HEADLESS-2" and not windows()["A"][4],
             "A floating on HEADLESS-2")

    # Brought back over B, it joins the tiling there.
    press("A")
    pointer("move", "300", "360")
    wait_for(lambda: windows()["B"][:4] == RIGHT, "B aside for A")
    release()
    wait_for(lambda: windows()["A"][:5] == (*LEFT, True), "A tiled again")

    # At the top edge it is to be maximized: its place closes, and it floats maximized.
    press("A")
    pointer("move", "640", "0")
    wait_for(lambda: windows()["B"][:4] == (0, 0, 1280, 720), "B filling for the top edge")
    release()
    wait_for(lambda: windows()["A"][:5] == (0, 0, 1280, 720, False), "A maximized")

    # A drag cut short (the window sent to another workspace) closes its place too.
    desktop.msg("toggle_floating")  # A tiles again; B and A share the monitor
    wait_for(lambda: windows()["A"][4] and windows()["B"][4], "both tiled")
    press("B")
    desktop.msg("move_to_workspace", "2")
    wait_for(lambda: not desktop.rows("snap")[0][6] == "1", "the move cut short")
    wait_for(lambda: windows()["A"][:4] == (0, 0, 1280, 720), "A fills the place B left")

# With animations, the tiles glide aside.
with harness.Compositor(compositor, config(animations="speed = 0.3"),
                        env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    windows, preview, configures, pointer, press, release, tiles = session(desktop)
    tiles("A", "B")
    desktop.wait_for(lambda: desktop.rows("animations")[0][0] == "0", "settled")
    press("A")
    pointer("move", "1100", "360")
    desktop.wait_for(lambda: windows()["B"][:4] == LEFT, "B aside")
    assert int(desktop.rows("animations")[0][0]) > 0, desktop.rows("animations")
    release()
    desktop.wait_for(lambda: windows()["A"][:5] == (*RIGHT, True), "A tiled on the right")

# A tile taken while it glides to a new place grows about the point the pointer holds it by, of
# the tile where it is, not where the glide draws it.
with harness.Compositor(compositor, config(animations="speed = 0.1"),
                        env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    windows, preview, configures, pointer, press, release, tiles = session(desktop)
    tiles("A", "B")
    desktop.wait_for(lambda: desktop.rows("animations")[0][0] == "0", "settled")
    focused = next(w.title for w in desktop.windows() if w.focused)
    before = windows()[focused][0]
    desktop.msg("swap_next")
    desktop.wait_for(lambda: windows()[focused][0] != before, "the tiles trading places")
    x, y = windows()[focused][:2]
    pointer("key", "alt", "down")
    pointer("move", str(x + 100), str(y + 100), "press", "left")
    pointer("key", "alt", "up")
    pointer("move", str(x + 130), str(y + 120))
    desktop.wait_for(lambda: any(r[0] == focused for r in desktop.rows("lift")), "lifted")
    assert int(desktop.rows("animations")[0][0]) > 0, "the glide is over already"
    wx, wy = windows()[focused][:2]
    anchor = next(tuple(int(n) for n in r[6:8]) for r in desktop.rows("lift") if r[0] == focused)
    assert anchor == (x + 130 - wx, y + 120 - wy), (anchor, (x, y), (wx, wy))
    release()

# The master layout opens the place at the end of its list; the scrolling layout keeps none.
with harness.Compositor(compositor, config(layout="master"),
                        env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    windows, preview, configures, pointer, press, release, tiles = session(desktop)
    tiles("A", "B", "C")
    master = windows()["A"][:4]
    assert master == LEFT, windows()
    press("A")
    desktop.stays(lambda: windows()["B"][:4] != (0, 0, 640, 720), "the stack moved")
    pointer("move", "1000", "600")
    desktop.wait_for(lambda: windows()["B"][:4] == LEFT, "B the master, A's place at the end")
    desktop.wait_for(lambda: preview() == (True, (640, 360, 640, 360)), "the place at the end")
    release()
    desktop.wait_for(lambda: windows()["A"][:5] == (640, 360, 640, 360, True), "A last")
    desktop.msg("layout_scroll")
    desktop.wait_for(lambda: windows()["A"][2] != 640 or windows()["A"][1] != 480, "scrolling")
    # Lifting the column on the screen's left edge, the one beside it closes up at once.
    taken = next(t for t, box in windows().items() if box[0] < 100 < box[0] + box[2])
    others = [t for t in windows() if t != taken]
    before = {title: box[:4] for title, box in windows().items()}
    press(taken, dx=100 - windows()[taken][0])
    desktop.wait_for(lambda: any(windows()[t][:4] != before[t] for t in others),
                     "the scrolling layout closes up as the tile is lifted")
    release()

# A grab another starts while it is under way ends first, as if let go: an X11 window asking to be
# moved while a tile is (a held button lets it) leaves the tile set down and out of the tiling, and
# closing the tile's window then leaves nothing of it in the tiling.
with harness.Compositor(compositor, config(xwayland="true"),
                        env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    if not x11_probe or "DISPLAY" not in desktop.env:
        print("XWayland is not available: the second grab is not tested")
    else:
        windows, preview, configures, pointer, press, release, tiles = session(desktop)

        def lift():
            return {r[0]: int(r[1]) for r in desktop.rows("lift")}

        pointer("move", "1200", "360")
        tile = desktop.open_window(probe, "A", env={"PAW_PROBE_MOVE": "1"})
        desktop.wait_for(lambda: windows()["A"][4], "A tiled")
        x11 = desktop.spawn([x11_probe, "commands"], env={"PAW_PROBE_TITLE": "X"},
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        assert x11.stdout.readline().strip() == "X11 window mapped and focused"
        assert x11.stdout.readline().strip() == "waiting for commands"
        desktop.wait_for(lambda: "X" in windows() and windows()["X"][4], "X tiled")
        # A press on A, which asks to be moved, and a move: A is lifted and holds its place.
        ax, ay = windows()["A"][:2]
        pointer("move", str(ax + 100), str(ay + 100), "press", "left")
        desktop.wait_for(lambda: next(r for r in desktop.rows("snap") if r[0] == "zone")[6] == "1",
                         "A is moved")
        pointer("move", str(ax + 130), str(ay + 120))
        desktop.wait_for(lambda: lift().get("A") == 1000, "A lifted")
        # X asks to be moved: A's move ends, A set down and out of the tiling; X, moved instead,
        # holds the whole tiling, where it goes back as the button is let go.
        x11.stdin.write("move\n")
        x11.stdin.flush()
        desktop.wait_for(lambda: "A" not in lift() and not windows()["A"][4],
                         "A set down and floating")
        pointer("move", str(ax + 140), str(ay + 130))
        desktop.wait_for(lambda: preview() == (True, (0, 0, 1280, 720)), "X's place the monitor")
        pointer("release", "left")
        desktop.wait_for(lambda: windows()["X"][:5] == (0, 0, 1280, 720, True), "X tiled again")
        # A's window closing, and the tiling arranged again, touch nothing of it.
        tile.terminate()
        desktop.reap(tile)
        desktop.wait_for(lambda: "A" not in windows(), "A closed")
        tiles("B")
        desktop.wait_for(lambda: windows()["X"][2] == 640, "X and B side by side")
