# SPDX-License-Identifier: GPL-3.0-or-later
"""The window switcher peeks at its selected window, as Windows' Alt+Tab does: once the shell
says it has drawn the switcher (switcher_peek) and the selection has rested half a second, the
other windows fade as for the taskbar's peek while the selected one shows over them in full, a
minimized window where it was and one on another workspace over the workspace shown; from then on
the peek follows the selection at once, the others staying faded. Confirming focuses the window
peeked at where it shows, the others coming back around it; choosing another, cancelling, or the
selected window closing behave too, and the session locking ends the peek at once. The others
fade on every monitor, wherever the selected window is. Without the shell's word, or with
peek.switcher off, nothing peeks; without animations it all happens at once, the wait before the
first peek kept."""
from pathlib import Path
import subprocess
import sys
import time

import harness

compositor, probe, window_probe, lock_probe = (str(Path(p).resolve()) for p in sys.argv[1:5])
grim = sys.argv[5] if len(sys.argv) > 5 else ""

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false, workspaces = 4 },
    appearance = { background = '#000000' },
    peek = { opacity = 0.25, duration = 200 },
    animations = { enabled = true, duration = 10 },
    windows = { rules = { { title = "^F$", output = "HEADLESS-2" } } },
    bindings = { { mods = { "Alt" }, key = "Tab", action = "switcher" } },
}"""
FADED = 250  # a window's opacity while another is peeked at, in thousandths
REST = 0.5  # seconds the selection rests before the first peek
ALT, TAB = 56, 15  # evdev's key codes
# What the probe paints: a band along its top, its body below; and the background.
BAND, BODY, BLACK = (0x23, 0x31, 0x4a), (0x41, 0x7b, 0xc4), (0, 0, 0)
FADED_BODY = tuple(round(c * FADED / 1000) for c in BODY)
# The windows open cascaded, 320 x 240 from (40, 40) on, each 32 pixels further, in the order
# D, C, A, B: A's body under B's band, and points where only A or the minimized C shows, or would.
CASCADE = {"D": (40, 40), "C": (72, 72), "A": (104, 104), "B": (136, 136)}
UNDER_B, ONLY_A, ONLY_C = (200, 154), (120, 330), (88, 250)

with harness.Compositor(compositor, CONFIG) as desktop:
    msg = desktop.msg
    events = desktop.subscribe()

    def windows():
        """title -> the window (workspace, focus, place, visibility)."""
        return {w.title: w for w in desktop.windows()}

    def peeks():
        """title -> (peeked, drawn, stacked at, shown through a peek, opacity)."""
        return {row[5]: tuple(int(n) for n in row[:5]) for row in desktop.rows("window_peek")}

    def fade():
        """How far the windows have faded for a peek (thousandths), and whether the desktop's."""
        return tuple(int(n) for n in msg("get", "peek").splitlines()[0].split("\t")[:2])

    def peeked():
        """The title of the window peeked at, or None."""
        return next((title for title, row in peeks().items() if row[0]), None)

    def stacking(rows, leaving):
        """The windows' titles from the bottom up, but for `leaving`."""
        return [title for _, title in sorted((row[2], title) for title, row in rows.items()
                                             if title != leaving)]

    desktop.detail = lambda: f"fade: {fade()}\npeeks: {peeks()}\nwindows: {windows()}"
    clients = {}

    def launch(title):
        clients[title] = desktop.open_window(probe, title, f"app-{title}")

    def control(title, *words):
        subprocess.run([window_probe, title, *words], env=desktop.env, check=True,
                       timeout=30, stdout=subprocess.DEVNULL)

    def screen(under_b, only_a, only_c):
        """With grim, the screen has these colours at the points named so; a colour of None is
        left out."""
        shot = harness.grab(grim, desktop.env)
        if shot is None:
            return
        for point, expected in zip((UNDER_B, ONLY_A, ONLY_C), (under_b, only_a, only_c)):
            got = shot.at(*point)
            assert expected is None or all(abs(a - b) <= 2 for a, b in zip(got, expected)), \
                (point, got, expected)

    def settled():
        return msg("get", "animations").split("\t")[0].strip() == "0"

    # Each opens focused, so that the switcher lists them B, A, C, D and selects A.
    for title in "DCAB":
        launch(title)
    assert {title: (w.x, w.y) for title, w in windows().items()} == CASCADE, windows()
    # C minimized, D on workspace 2, which the output does not show; B focused, over A.
    control("C", "minimize")
    control("D", "workspace", "2")
    desktop.wait_for(lambda: windows()["C"].minimized and windows()["D"].workspace == 2 and
                     windows()["B"].focused, "C minimized, D on workspace 2, B focused")
    desktop.wait_for(settled, "the windows settled")
    before, stacked = windows(), peeks()
    assert {title: row[1] for title, row in stacked.items()} == {"A": 1, "B": 1, "C": 0, "D": 0}, \
        stacked
    screen(BAND, BODY, BLACK)

    def as_before():
        """Every window as it was before the peeks: placed, focused, stacked and drawn."""
        return windows() == before and peeks() == stacked and fade() == (0, 0)

    def peeking(title):
        """`title` peeked at and shown over the others in full, they faded."""
        now = peeks()
        top = len(now) - 1
        return (fade() == (1000, 0) and now.get(title) == (1, 1, top, 1000, 1000) and
                all(row[0] == 0 and row[3] == 0 and (row[4] == FADED if row[1] else True)
                    for other, row in now.items() if other != title))

    def listed():
        """The titles of the switcher's windows and the selected one's place, as last announced
        to subscribers; None before a whole list has come."""
        lines = events.lines("switcher ", "switcher-window ")
        heads = [i for i, line in enumerate(lines) if line.startswith("switcher ")]
        if not heads:
            return None
        head = heads[-1]
        selected, count = (int(n) for n in lines[head].split(" ")[2:4])
        if len(lines) < head + 1 + count:
            return None
        return [line.split("\t")[1] for line in lines[head + 1:head + 1 + count]], selected

    def open_switcher():
        """Opens the switcher; returns its windows' titles, the selected one's first."""
        events.forget()
        msg("switcher")
        desktop.wait_for(listed, "the switcher's list",
                         detail=lambda: f"events: {events.lines('switcher')}")
        titles, selected = listed()
        return titles[selected:] + titles[:selected]

    # The switcher lists B, A, C and D, A selected. Without the shell drawing it nothing peeks.
    order = open_switcher()
    assert order == ["A", "C", "D", "B"], order
    desktop.stays(as_before, "nothing peeked before the switcher showed", duration=REST + .3)

    # Shown, A peeks once the selection has rested: over B at once, in full, the others fading.
    # Nothing else changes; the switcher stays open.
    msg("switcher_cancel")
    rested = time.monotonic()
    order = open_switcher()
    msg("switcher_peek")
    desktop.wait_for(lambda: peeked() == "A", "A peeked at")
    assert time.monotonic() - rested >= REST - .01, "the peek came before the selection rested"
    assert peeks()["A"][4] == 1000, "the window peeked at dipped"
    desktop.wait_for(lambda: peeking("A"), "A shown alone")
    assert windows() == before, windows()
    screen(BODY, BODY, BLACK)

    # The peek follows the selection at once, the others staying faded: to the minimized C where
    # it was, and to D on workspace 2 over workspace 1. Neither changes.
    msg("switcher")
    assert peeked() == "C", peeks()
    desktop.stays(lambda: fade() == (1000, 0), "the others faded while the peek moves")
    desktop.wait_for(lambda: peeking("C"), "C shown alone")
    assert stacking(peeks(), "C") == stacking(stacked, "C"), "A not back in its place"
    screen(None, FADED_BODY, BODY)
    msg("switcher")
    assert peeked() == "D", peeks()
    desktop.wait_for(lambda: peeking("D"), "D shown alone")
    assert windows() == before and msg("get", "workspace").strip() == "1", windows()

    # Asking again while it peeks changes nothing; cancelling fades the others back and D out,
    # and leaves all as it was.
    msg("switcher_peek")
    assert peeking("D"), peeks()
    msg("switcher_cancel")
    desktop.wait_for(lambda: 0 < fade()[0] < 1000, "the others fading back")
    desktop.wait_for(as_before, "all as before the switcher")
    screen(BAND, BODY, BLACK)
    error = desktop.run("switcher_peek")
    assert error.returncode != 0 and "not open" in error.stdout + error.stderr, error

    # Tab pressed on before the first peek puts it off until the selection rests again.
    open_switcher()
    msg("switcher_peek")
    for _ in range(3):  # through C and D to B
        rested = time.monotonic()
        msg("switcher")
        assert peeked() is None, "a peek before the selection rested"
    desktop.wait_for(lambda: peeked() == "B", "B peeked at")
    assert time.monotonic() - rested >= REST - .01, "the peek came before the selection rested"
    msg("switcher_cancel")
    desktop.wait_for(as_before, "all as before the switcher")

    # Alt+Tab held: releasing Alt focuses the window peeked at where it shows, raising it in
    # full while the others come back around it.
    press = desktop.keyboard()
    press.down(ALT)
    press(TAB)
    msg("switcher_peek")
    desktop.wait_for(lambda: peeking("A"), "A peeked at from Alt+Tab")
    press.up(ALT)
    desktop.wait_for(lambda: windows()["A"].focused, "A focused on releasing Alt")
    top = len(stacked) - 1
    assert peeks()["A"][:3] == (0, 1, top) and peeks()["A"][4] == 1000, \
        "A not in front in full as the peek ended"
    desktop.wait_for(lambda: fade() == (0, 0) and
                     all(row[4] == 1000 and row[3] == 0 for row in peeks().values()),
                     "the others back")
    assert windows()["A"].box == before["A"].box, (windows()["A"], before["A"])
    screen(BODY, BODY, BLACK)

    # Confirming a minimized window peeked at restores it where the peek showed it.
    order = open_switcher()
    msg("switcher_peek")
    while order[0] != "C":
        msg("switcher")
        order = order[1:] + order[:1]
    desktop.wait_for(lambda: peeking("C"), "C peeked at")
    msg("switcher_confirm")
    desktop.wait_for(lambda: windows()["C"].focused and not windows()["C"].minimized,
                     "C focused and restored")
    assert peeks()["C"][4] == 1000, "C dipped as the peek ended"
    assert windows()["C"].box == before["C"].box, (windows()["C"], before["C"])
    desktop.wait_for(lambda: fade() == (0, 0) and peeked() is None, "the others back")

    # Another window chosen than the one peeked at (a click on its card): the peek ends as it is
    # focused, the others and it coming back.
    order = open_switcher()
    msg("switcher_peek")
    desktop.wait_for(lambda: peeked() == order[0], "the selection peeked at")
    titles, _ = listed()
    msg("switcher_confirm", str(titles.index("B") + 1))
    desktop.wait_for(lambda: windows()["B"].focused, "B focused")
    assert peeked() is None, peeks()
    desktop.wait_for(lambda: fade() == (0, 0) and
                     all(row[4] == 1000 and row[3] == 0 for row in peeks().values()),
                     "all back in full")

    # The selected window closing: the peek moves at once to the window selected in its place.
    # (Were it the focused one, the window focused in its place would end the peek.)
    launch("E")
    control("B", "restore")
    desktop.wait_for(lambda: windows()["B"].focused, "B focused again")
    order = open_switcher()
    assert order[0] == "E", order
    msg("switcher_peek")
    desktop.wait_for(lambda: peeking("E"), "E peeked at")
    events.forget()
    subprocess.run([probe, "--close", "app-E"], env=desktop.env, check=True, timeout=30,
                   stdout=subprocess.DEVNULL)
    assert desktop.reap(clients.pop("E")) == 0
    desktop.wait_for(listed, "the list without E")
    titles, selected = listed()
    assert "E" not in titles, titles
    desktop.wait_for(lambda: peeked() == titles[selected], "the next window peeked at")
    desktop.wait_for(lambda: peeking(titles[selected]), f"{titles[selected]} shown alone")
    msg("switcher_cancel")
    desktop.wait_for(lambda: fade() == (0, 0) and peeked() is None, "the others back")

    # The same when the window was focused too: focus goes to the window selected in its place,
    # and the peek still moves on to it.
    launch("G")
    order = open_switcher()
    msg("switcher_peek")
    while order[0] != "G":
        msg("switcher")
        order = order[1:] + order[:1]
    desktop.wait_for(lambda: peeking("G") and windows()["G"].focused, "the focused G peeked at")
    events.forget()
    subprocess.run([probe, "--close", "app-G"], env=desktop.env, check=True, timeout=30,
                   stdout=subprocess.DEVNULL)
    assert desktop.reap(clients.pop("G")) == 0
    desktop.wait_for(listed, "the list without G")
    titles, selected = listed()
    desktop.wait_for(lambda: windows()[titles[selected]].focused, "the selection focused")
    desktop.wait_for(lambda: peeking(titles[selected]), f"{titles[selected]} peeked at")
    msg("switcher_cancel")
    desktop.wait_for(lambda: fade() == (0, 0) and peeked() is None, "the others back")

    # Something else ending the peek, here the desktop peek, has the next selection rest again
    # before it peeks.
    order = open_switcher()
    msg("switcher_peek")
    desktop.wait_for(lambda: peeking(order[0]), f"{order[0]} peeked at")
    msg("peek_toggle")
    desktop.wait_for(lambda: fade() == (1000, 1) and peeked() is None, "the desktop peek")
    msg("peek_toggle")
    desktop.wait_for(lambda: fade() == (0, 0), "the desktop peek over")
    rested = time.monotonic()
    msg("switcher")
    assert peeked() is None, "the peek came back at once"
    desktop.wait_for(lambda: peeked() == order[1], f"{order[1]} peeked at")
    assert time.monotonic() - rested >= REST - .01, "the peek came before the selection rested"
    msg("switcher_cancel")
    desktop.wait_for(lambda: fade() == (0, 0) and peeked() is None, "the others back")

    # A second monitor: the others fade on both, the switcher on the one with the focus, and a
    # window on the other peeks there.
    msg("headless_output", "add", "HEADLESS-2", "800x600")
    launch("F")
    desktop.wait_for(lambda: windows()["F"].output == "HEADLESS-2" and windows()["F"].focused,
                     "F focused on HEADLESS-2")
    order = open_switcher()
    msg("switcher_peek")
    desktop.wait_for(lambda: peeking(order[0]), f"{order[0]} peeked at")
    assert windows()[order[0]].output == "HEADLESS-1" and peeks()["F"][4] == FADED, peeks()
    while order[0] != "F":
        msg("switcher")
        order = order[1:] + order[:1]
    desktop.wait_for(lambda: peeking("F"), "F peeked at on HEADLESS-2")
    msg("switcher_cancel")
    desktop.wait_for(lambda: fade() == (0, 0) and peeked() is None, "the others back")

    # The peek at the selection as the session locks.
    order = open_switcher()
    msg("switcher_peek")
    desktop.wait_for(lambda: peeking(order[0]), f"{order[0]} peeked at")

    # Locking ends the peek at once with the switcher.
    locker = desktop.spawn([lock_probe, "hold", str(desktop.root / "locker.log")])
    desktop.wait_for(lambda: "Session locked" in desktop.log.read_text(), "the session locked")
    assert fade() == (0, 0) and peeked() is None and \
        all(row[4] == 1000 and row[3] == 0 for row in peeks().values()), peeks()
    assert desktop.run("switcher_peek").returncode != 0, "switcher_peek taken while locked"
    locker.terminate()
    assert desktop.reap(locker) == 0
    desktop.wait_for(lambda: "Session unlocked" in desktop.log.read_text(), "the session unlocked")

    # Without animations the peek comes as late, but whole at once, and goes at once.
    desktop.reload(CONFIG.replace("enabled = true", "enabled = false"))
    rested = time.monotonic()
    order = open_switcher()
    msg("switcher_peek")
    desktop.wait_for(lambda: peeked() is not None, "a peek without animations")
    assert time.monotonic() - rested >= REST - .01, "the peek came before the selection rested"
    assert peeking(order[0]), peeks()
    msg("switcher")
    assert peeking(order[1]), peeks()
    msg("switcher_cancel")
    assert fade() == (0, 0) and peeked() is None, peeks()

    # peek.switcher = false: nothing peeks.
    desktop.reload(CONFIG.replace("duration = 200 }", "duration = 200, switcher = false }"))
    open_switcher()
    msg("switcher_peek")
    desktop.stays(lambda: fade() == (0, 0) and peeked() is None, "a peek with peek.switcher off",
                  duration=REST + .3)
    msg("switcher_cancel")
print("The switcher's selected window peeks once the shown switcher's selection rests, follows "
      "it, and every way the switcher closes ends it smoothly"
      + ("" if grim else " (pixels not checked: grim missing)"))
