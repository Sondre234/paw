# SPDX-License-Identifier: GPL-3.0-or-later
"""Peeking at one window through paw-window-control-v1's set_peek, as the taskbar does while
the pointer rests on a window's picture: the other windows fade as for the desktop peek while
that one shows over them in full, a minimized window and one on a workspace not shown too, where
they are; nothing about any window changes. The peek moves from window to window without the
others coming back in between, and ends with a fade on unset_peek, or as the window closes or is
focused, the session locks, the client lets go of the object or disconnects, or the desktop peek
starts; then everything is as it was, stacked as it was. With grim the screen shows it."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe, window_probe, lock_probe = (str(Path(p).resolve()) for p in sys.argv[1:5])
grim = sys.argv[5] if len(sys.argv) > 5 else ""

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false, workspaces = 4 },
    appearance = { background = '#000000' },
    peek = { opacity = 0.25, duration = 200 },
    animations = { enabled = true, duration = 10 },
}"""
FADED = 250  # a window's opacity while another is peeked at, in thousandths
# What the probe paints: a band along its top, its body below; and the background.
BAND, BODY, BLACK = (0x23, 0x31, 0x4a), (0x41, 0x7b, 0xc4), (0, 0, 0)
# The windows open cascaded, 320 x 240 from (40, 40) on, each 32 pixels further. Points where
# one shows, or would: A's body under B's band, and where only A, C or D is.
CASCADE = {"A": (40, 40), "B": (72, 72), "C": (104, 104), "D": (136, 136)}
UNDER_B, ONLY_A, ONLY_C, ONLY_D = (200, 90), (56, 200), (120, 328), (440, 256)
FADED_BODY = tuple(round(c * FADED / 1000) for c in BODY)

with harness.Compositor(compositor, CONFIG) as desktop:
    msg = desktop.msg

    def windows():
        """title -> the window (workspace, focus, place, visibility)."""
        return {w.title: w for w in desktop.windows()}

    def peeks():
        """title -> (peeked, drawn, stacked at, shown through a peek, opacity)."""
        return {row[5]: tuple(int(n) for n in row[:5]) for row in desktop.rows("window_peek")}

    def fade():
        """How far the windows have faded for a peek (thousandths), and whether the desktop's."""
        return tuple(int(n) for n in msg("get", "peek").splitlines()[0].split("\t")[:2])

    desktop.detail = lambda: f"fade: {fade()}\npeeks: {peeks()}\nwindows: {windows()}"
    clients = {}

    def launch(title):
        clients[title] = desktop.open_window(probe, title, f"app-{title}")

    def control(title, *words):
        subprocess.run([window_probe, title, *words], env=desktop.env, check=True,
                       timeout=30, stdout=subprocess.DEVNULL)

    def close(title):
        subprocess.run([probe, "--close", f"app-{title}"], env=desktop.env, check=True,
                       timeout=30, stdout=subprocess.DEVNULL)
        assert desktop.reap(clients.pop(title)) == 0
        desktop.wait_for(lambda: title not in windows(), f"{title} closed")

    def screen(under_b, only_a, only_c, only_d):
        """With grim, the screen has these colours at the points named so; a colour of None is
        left out."""
        shot = harness.grab(grim, desktop.env)
        if shot is None:
            return
        for point, expected in zip((UNDER_B, ONLY_A, ONLY_C, ONLY_D),
                                   (under_b, only_a, only_c, only_d)):
            got = shot.at(*point)
            assert expected is None or all(abs(a - b) <= 2 for a, b in zip(got, expected)), \
                (point, got, expected)

    for title in "ABCD":
        launch(title)
    assert {title: (w.x, w.y) for title, w in windows().items()} == CASCADE, windows()
    # C minimized, D on workspace 2, which the output does not show: neither is drawn.
    control("C", "minimize")
    control("D", "workspace", "2")
    desktop.wait_for(lambda: windows()["C"].minimized and windows()["D"].workspace == 2,
                     "C minimized, D on workspace 2")
    desktop.wait_for(lambda: msg("get", "animations").split("\t")[0].strip() == "0",
                     "the windows settled")
    before, stacked = windows(), peeks()
    assert {title: row[1] for title, row in stacked.items()} == {"A": 1, "B": 1, "C": 0, "D": 0}, \
        stacked
    assert all(row[0] == 0 and row[3] == 0 and row[4] == 1000 for row in stacked.values()), stacked
    top = len(stacked) - 1
    screen(BAND, BODY, BLACK, BLACK)

    def as_before():
        """Every window as it was before the peeks: placed, focused, stacked and drawn."""
        return windows() == before and peeks() == stacked and fade() == (0, 0)

    def peeking(title):
        """`title` peeked at and shown over the others in full, they faded."""
        now = peeks()
        return (fade() == (1000, 0) and now[title] == (1, 1, top, 1000, 1000) and
                all(row[0] == 0 and row[3] == 0 and
                    (row[4] == FADED if stacked[other][1] else row[1] == 0)
                    for other, row in now.items() if other != title))

    class Peeker:
        """A window probe peeking at windows as it is told, one request at a time."""

        def __init__(self, title):
            self.process = desktop.spawn([window_probe, title, "peek"], stdin=subprocess.PIPE,
                                         stdout=subprocess.PIPE, text=True)
            self.answered()

        def answered(self):
            assert self.process.stdout.readline().strip() == "ok", "the probe did not answer"

        def __call__(self, request, title):
            self.process.stdin.write(f"{request} {title}\n")
            self.process.stdin.flush()
            self.answered()

        def finish(self):
            self.process.stdin.close()
            assert desktop.reap(self.process) == 0

    # A, under B: it comes over B at once in full, its place among the others kept, while they
    # fade over peek.duration. Nothing else changes: B keeps the focus.
    peeker = Peeker("A")
    assert peeks()["A"][:4] == (1, 1, top, 1000), peeks()
    desktop.wait_for(lambda: 0 < fade()[0] < 1000, "the others fading")
    assert peeks()["A"][4] == 1000, "the window peeked at dipped as the others faded"
    desktop.wait_for(lambda: peeking("A"), "A peeked at")
    assert windows() == before, windows()
    screen(BODY, BODY, BLACK, BLACK)

    # To the minimized C, which shows where it was over the others while A goes back among them,
    # and the others stay faded as the peek moves over. C stays minimized.
    peeker("peek", "C")
    desktop.stays(lambda: fade() == (1000, 0), "the others faded while the peek moves",
                  duration=.4)
    desktop.wait_for(lambda: peeking("C"), "C peeked at")
    assert peeks()["A"][2] == stacked["A"][2], "A not back in its place"
    assert windows() == before, windows()
    screen(None, FADED_BODY, BODY, BLACK)

    # To D on workspace 2, shown over workspace 1 where it is; C hides again once faded out.
    peeker("peek", "D")
    desktop.wait_for(lambda: peeking("D"), "D peeked at")
    assert windows() == before and msg("get", "workspace").strip() == "1", windows()
    screen(None, FADED_BODY, BLACK, BODY)

    # Peeking at the same window again, or ending the peek through another window's object,
    # changes nothing.
    peeker("peek", "D")
    peeker("unpeek", "B")
    assert peeking("D"), peeks()

    # Ending it fades the others back and D out, and leaves all as it was.
    peeker("unpeek", "D")
    desktop.wait_for(lambda: 0 < fade()[0] < 1000, "the others fading back")
    desktop.wait_for(as_before, "all as before the peeks")
    screen(BAND, BODY, BLACK, BLACK)

    # Without animations it all happens at once.
    desktop.reload(CONFIG.replace("enabled = true", "enabled = false"))
    peeker("peek", "C")
    assert peeking("C"), peeks()
    peeker("peek", "A")
    assert peeking("A"), peeks()
    peeker("unpeek", "A")
    assert as_before(), peeks()
    desktop.reload(CONFIG)

    # The desktop peek takes over from a peek at a window, which goes back among the others.
    peeker("peek", "C")
    desktop.wait_for(lambda: peeking("C"), "C peeked at")
    msg("peek_toggle")
    desktop.wait_for(lambda: fade() == (1000, 1) and peeks()["C"][:2] == (0, 0) and
                     all(row[4] == FADED for title, row in peeks().items() if row[1]),
                     "the desktop peek in its place")
    msg("peek_toggle")
    desktop.wait_for(as_before, "all as before once the desktop peek ended")

    # A window closing as it is peeked at ends the peek; the others are as they were before it
    # opened, the focus back where it was.
    launch("E")
    peeker("peek", "E")
    desktop.wait_for(lambda: peeks()["E"][:2] == (1, 1) and fade()[0] == 1000, "E peeked at")
    close("E")
    desktop.wait_for(as_before, "all as before once E closed")
    # Its object, inert now, peeks at nothing.
    peeker("peek", "E")
    assert as_before(), peeks()

    # Locking ends it at once; while locked a peek is not taken up.
    peeker("peek", "D")
    desktop.wait_for(lambda: peeking("D"), "D peeked at")
    locker = desktop.spawn([lock_probe, "hold", str(desktop.root / "locker.log")])
    desktop.wait_for(lambda: "Session locked" in desktop.log.read_text(), "the session locked")
    assert as_before(), peeks()
    peeker("peek", "C")
    assert as_before(), peeks()
    locker.terminate()
    assert desktop.reap(locker) == 0
    desktop.wait_for(lambda: "Session unlocked" in desktop.log.read_text(),
                     "the session unlocked")

    # Destroying the object that asked ends it, and so does its client disconnecting.
    peeker("peek", "C")
    desktop.wait_for(lambda: peeking("C"), "C peeked at")
    peeker("destroy", "C")
    desktop.wait_for(as_before, "all as before once the object went")
    peeker("peek", "D")
    desktop.wait_for(lambda: peeking("D"), "D peeked at")
    peeker.process.kill()
    desktop.reap(peeker.process)
    desktop.wait_for(as_before, "all as before once the client went")

    # Focusing the window peeked at, as a click on its picture does, shows it for good where
    # it was, in front of the others as they fade back; and the peek ends.
    peeker = Peeker("C")
    desktop.wait_for(lambda: peeking("C"), "C peeked at")
    subprocess.run([probe, "--activate", "app-C"], env=desktop.env, check=True, timeout=30,
                   stdout=subprocess.DEVNULL)
    desktop.wait_for(lambda: windows()["C"].focused and not windows()["C"].minimized, "C focused and restored")
    assert peeks()["C"][:3] == (0, 1, top), peeks()
    assert peeks()["C"][4] == 1000, "C dipped as the peek ended"
    assert windows()["C"].box == before["C"].box, (windows()["C"], before["C"])
    desktop.wait_for(lambda: fade() == (0, 0) and
                     all(row[4] == 1000 and row[3] == 0 for row in peeks().values()),
                     "the others back")
    screen(BAND, BODY, BODY, BLACK)
    peeker("unpeek", "C")  # ended already
    assert fade() == (0, 0)
    peeker.finish()
print("A window peeked at shows alone over the others, minimized or on another workspace too, "
      "and every way the peek ends leaves the windows as they were"
      + ("" if grim else " (pixels not checked: grim missing)"))
