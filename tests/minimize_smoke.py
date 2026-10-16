# SPDX-License-Identifier: GPL-3.0-or-later
"""Minimizing into the taskbar button and restoring out of it. A taskbar says where each window's
button is on each output (the window control's set_rectangle, or wlr-foreign-toplevel's on a
panel's surface); a copy of a window minimizing shrinks into its button on the window's own
output, and a window restored grows out of it, both while input already has the window where it
ends up; the minimize action does the same. Minimizing turned back half way goes back from where
it is drawn, and a window a peek shows already (the taskbar's picture's, the switcher's) comes back
where it is. Without a button the window goes to the middle of the panel's strip, or shrinks where
it is. Windows in the scratchpad come and go its own way. Animations off, or the session locked,
nothing animates; a window closing as it minimizes
or is restored leaves nothing behind."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe, window_probe, lock_probe, pointer_probe = (str(Path(p).resolve())
                                                              for p in sys.argv[1:6])

# HEADLESS-2, right of HEADLESS-1, is scaled: 640 by 360 logical pixels. Minimizing takes 900 ms
# along a straight line in time, long enough to look at it on its way.
CONFIG = """return {
    xwayland = false,
    animations = { minimize = { duration = 900, curve = "linear" } },
    peek = { duration = 1500 },
    outputs = {
        order = { "HEADLESS-1", "HEADLESS-2" },
        monitors = {
            ["HEADLESS-1"] = { mode = "1280x720" },
            ["HEADLESS-2"] = { mode = "1280x720", scale = 2 },
        },
    },
}"""

with harness.Compositor(compositor, CONFIG, env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def windows():
        return {w.title: w for w in desktop.windows()}

    def animations():
        """Running animations, and the trees stacked for windows (closing copies too)."""
        running, trees, _ = (int(n) for n in msg("get", "animations").split())
        return running, trees

    def minimize_state():
        """{id: [(output, x, y, w, h), ...]} of the rectangles, the one that counts first,
        {(id, kind): (at, (x, y, w, h), target)} of the folds, and {(id, kind): at} of how far
        into its box each fold set off from."""
        rects, folds, set_off = {}, {}, {}
        for row in desktop.rows("minimize"):
            if row[0] == "rect":
                rects.setdefault(int(row[1]), []).append((row[2], *map(int, row[3:7])))
            else:
                folds[(int(row[1]), row[2])] = (int(row[3]), tuple(map(int, row[4:8])), row[8])
                set_off[(int(row[1]), row[2])] = int(row[13])
        return rects, folds, set_off

    def folds():
        return minimize_state()[1]

    desktop.detail = lambda: f"windows: {windows()}, minimize: {minimize_state()}"

    def window_id(title):
        return int(subprocess.run([window_probe, title, "id"], env=desktop.env, check=True,
                                  timeout=30, capture_output=True, text=True).stdout)

    class Taskbar:
        """window_probe's taskbar: rectangles that last while it runs, and minimizing and
        restoring as the shell does, on the same connection."""

        def __init__(self):
            self.process = desktop.spawn([window_probe, "A", "taskbar"], stdin=subprocess.PIPE,
                                         stdout=subprocess.PIPE, text=True)
            self.answered()

        def answered(self):
            assert self.process.stdout.readline().strip() == "ok", "the taskbar did not answer"

        def __call__(self, *words):
            self.process.stdin.write(" ".join(str(word) for word in words) + "\n")
            self.process.stdin.flush()
            self.answered()

        def finish(self):
            self.process.stdin.close()
            assert desktop.reap(self.process) == 0

    def settled():
        """No fold left, nothing running, and no copy stacked among the windows."""
        running, trees = animations()
        return not folds() and running == 0 and trees == len(windows())

    desktop.open_window(probe, "A", "app-a", focused=True)
    a = window_id("A")
    taskbar = Taskbar()

    # Rectangles by output, the last one set first; one of no size takes that output's away.
    taskbar("rect", "A", "HEADLESS-1", 600, 680, 40, 40)
    taskbar("rect", "A", "HEADLESS-2", 10, 320, 40, 40)
    assert minimize_state()[0] == {a: [("HEADLESS-2", 10, 320, 40, 40),
                                       ("HEADLESS-1", 600, 680, 40, 40)]}, minimize_state()
    taskbar("rect", "A", "HEADLESS-1", 620, 680, 40, 40)
    taskbar("rect", "A", "HEADLESS-2", 0, 0, 0, 0)
    assert minimize_state()[0] == {a: [("HEADLESS-1", 620, 680, 40, 40)]}, minimize_state()
    # A rectangle is cut to its output (HEADLESS-2 is 640 by 360); one off it, or on an output
    # that is not there, is no rectangle, however large the numbers.
    taskbar("rect", "A", "NOPE-1", 0, 0, 40, 40)
    taskbar("rect", "A", "HEADLESS-2", 600, 340, 2147483000, 2147483000)
    assert minimize_state()[0] == {a: [("HEADLESS-2", 600, 340, 40, 20),
                                       ("HEADLESS-1", 620, 680, 40, 40)]}, minimize_state()
    taskbar("rect", "A", "HEADLESS-2", 2147483000, -2147483000, 2147483000, 2147483000)
    assert minimize_state()[0] == {a: [("HEADLESS-1", 620, 680, 40, 40)]}, minimize_state()

    # Minimizing: the window is minimized and out of the way of input at once, while a copy of
    # it shrinks into its button on HEADLESS-1.
    taskbar("minimize", "A")
    assert windows()["A"].minimized and not windows()["A"].visible, windows()
    at, box, target = folds()[(a, "minimize")]
    assert box == (620, 680, 40, 40) and target == "button", folds()
    running, trees = animations()
    assert running >= 1 and trees == len(windows()) + 1, animations()
    wait_for(lambda: (a, "minimize") in folds() and folds()[(a, "minimize")][0] >= 300,
             "A a third of the way into its button")
    wait_for(settled, "the copy gone into the button")

    # Restoring: focused and where it was at once, growing out of its button, from all the way
    # in, however far it has come by the time the test looks.
    taskbar("restore", "A")
    assert windows()["A"].focused and windows()["A"].visible, windows()
    _, state, set_off = minimize_state()
    box, target = state[(a, "restore")][1:]
    assert box == (620, 680, 40, 40) and target == "button" and set_off[(a, "restore")] == 1000, \
        (state, set_off)
    wait_for(settled, "A back in its place")
    assert not windows()["A"].minimized

    # Turned back on its way in, it comes back out from where it is drawn: the copy goes, and
    # the window takes over from as far in as the copy was.
    taskbar("minimize", "A")
    wait_for(lambda: (a, "minimize") in folds() and folds()[(a, "minimize")][0] >= 300,
             "A on its way in")
    taskbar("restore", "A")
    _, state, set_off = minimize_state()
    assert (a, "minimize") not in state and 300 <= set_off[(a, "restore")] < 1000, (state, set_off)
    wait_for(settled, "A back out")

    # Minimized again on its way in, as showing the desktop asks of every window, it goes on in,
    # and turned back, it still comes out from as far in as it was.
    taskbar("minimize", "A")
    taskbar("minimize", "A")
    wait_for(lambda: (a, "minimize") in folds() and folds()[(a, "minimize")][0] >= 300,
             "A on its way in, minimized twice")
    taskbar("restore", "A")
    _, state, set_off = minimize_state()
    assert (a, "minimize") not in state and 300 <= set_off[(a, "restore")] < 1000, (state, set_off)
    wait_for(settled, "A back out again")

    # Restoring takes over from minimizing turned back at once too.
    taskbar("minimize", "A")
    taskbar("restore", "A")
    assert (a, "minimize") not in folds(), folds()
    wait_for(settled, "A back at once")

    # A window peeked at, as the taskbar's picture and the switcher do, is shown where it is
    # already: restored, it stays there rather than coming out of its button; and so does one
    # still fading out of a peek.
    class Peeker:
        def __init__(self):
            self.process = desktop.spawn([window_probe, "A", "peek"], stdin=subprocess.PIPE,
                                         stdout=subprocess.PIPE, text=True)
            self.answered()

        def answered(self):
            assert self.process.stdout.readline().strip() == "ok", "the peeker did not answer"

        def __call__(self, request):
            self.process.stdin.write(f"{request} A\n")
            self.process.stdin.flush()
            self.answered()

        def finish(self):
            self.process.stdin.close()
            assert desktop.reap(self.process) == 0

    taskbar("minimize", "A")
    wait_for(settled, "A minimized to peek at")
    peeker = Peeker()
    taskbar("restore", "A")
    assert windows()["A"].visible and not windows()["A"].minimized, windows()
    desktop.stays(lambda: not folds(), "A restored where the peek shows it")
    # Peeked at and restored still on its way in, its copy goes at once.
    taskbar("minimize", "A")
    peeker("peek")
    taskbar("restore", "A")
    desktop.stays(lambda: not folds(), "no copy left going into the button")
    taskbar("minimize", "A")
    wait_for(settled, "A minimized again")

    def peek_at_a():
        """(peeked at, drawn, how far it shows in thousandths) of A."""
        row = next(row for row in desktop.rows("window_peek") if row[5] == "A")
        return row[0] == "1", row[1] == "1", int(row[3])

    wait_for(lambda: peek_at_a() == (False, False, 0), "the first peek over")
    peeker("peek")
    wait_for(lambda: peek_at_a() == (True, True, 1000), "A peeked at")
    peeker("unpeek")  # fading out, over peek.duration
    assert peek_at_a()[1], "A no longer shown as its peek fades"
    taskbar("restore", "A")
    desktop.stays(lambda: not folds(), "A restored where the peek's fade shows it")
    peeker.finish()
    wait_for(settled, "the peek over")

    # The window switcher peeks at its selected window once it rests, a minimized one where it
    # was: confirmed while that shows it, the window stays there; confirmed before any peek, it
    # grows out of its button.
    events = desktop.subscribe()

    def switcher_opened():
        """Opens the switcher on A, the only window, and waits for its list."""
        events.forget()
        msg("switcher")
        wait_for(lambda: any(line.startswith("switcher-window ") and "\tA\t" in line
                             for line in events.lines("switcher-window ")), "the switcher's list")

    wait_for(lambda: peek_at_a() == (False, True, 0), "the taskbar's peek over")
    taskbar("minimize", "A")
    wait_for(settled, "A minimized for the switcher")
    # With the pointer off where A comes back, confirming takes the pointer there, which lands
    # what glides but not A growing out of its button.
    pointer = desktop.virtual_pointer(pointer_probe, 1920, 720)
    pointer("move", "1000", "600")
    switcher_opened()
    msg("switcher_confirm")
    assert [kind for _, kind in folds()] == ["restore"], folds()
    wait_for(settled, "A out of its button")
    taskbar("minimize", "A")
    wait_for(settled, "A minimized for the switcher's peek")
    switcher_opened()
    msg("switcher_peek")
    wait_for(lambda: peek_at_a() == (True, True, 1000), "A peeked at by the switcher")
    msg("switcher_confirm")
    wait_for(lambda: windows()["A"].focused and not windows()["A"].minimized,
             "A restored from the switcher")
    desktop.stays(lambda: not folds(), "A restored where the switcher's peek shows it")
    wait_for(lambda: peek_at_a() == (False, True, 0), "the switcher's peek over")

    # The minimize action does what the taskbar does, to the focused window.
    assert windows()["A"].focused, windows()
    msg("minimize")
    assert windows()["A"].minimized, windows()
    assert folds()[(a, "minimize")][1:] == ((620, 680, 40, 40), "button"), folds()
    wait_for(settled, "A minimized by the action")
    msg("minimize")  # nothing focused: nothing to minimize
    taskbar("restore", "A")
    wait_for(settled, "A restored again")

    # The scratchpad hides and brings back its windows its own way: neither into nor out of a
    # button, from the taskbar too.
    desktop.open_window(probe, "S", "app-s", focused=True)
    wait_for(settled, "S opened")
    msg("move_to_scratchpad")
    wait_for(lambda: windows()["S"].scratchpad and windows()["S"].minimized, "S in the scratchpad")
    desktop.stays(lambda: not folds(), "S hidden in the scratchpad its own way")
    taskbar("restore", "S")
    wait_for(lambda: windows()["S"].visible, "S shown from the scratchpad")
    desktop.stays(lambda: not folds(), "S shown from the scratchpad its own way")
    msg("scratchpad_show")
    wait_for(lambda: windows()["S"].minimized, "S hidden again")
    desktop.stays(lambda: not folds(), "S hidden again its own way")
    taskbar("restore", "A")
    wait_for(lambda: windows()["A"].focused and settled(), "A focused again")

    # On another output, the button there counts: A moves to the scaled HEADLESS-2.
    subprocess.run([window_probe, "A", "output", "HEADLESS-2"], env=desktop.env, check=True,
                   timeout=30)
    wait_for(lambda: windows()["A"].output == "HEADLESS-2", "A on HEADLESS-2")
    wait_for(settled, "A moved")
    taskbar("rect", "A", "HEADLESS-2", 10, 300, 40, 40)
    taskbar("minimize", "A")
    assert folds()[(a, "minimize")][1:] == ((1290, 300, 40, 40), "button"), folds()
    wait_for(settled, "A minimized on HEADLESS-2")
    taskbar("restore", "A")
    wait_for(settled, "A restored")

    # Without a button there, nor a panel, the window shrinks where it is: to a third of its
    # size, a little below its middle.
    taskbar("rect", "A", "HEADLESS-2", 0, 0, 0, 0)
    w = windows()["A"]
    taskbar("minimize", "A")
    width, height = w.width // 3, w.height // 3
    assert folds()[(a, "minimize")][1:] == (
        (w.x + (w.width - width) // 2, w.y + (w.height - height) // 2 + w.height // 6, width,
         height), "place"), (folds(), w)
    wait_for(settled, "A shrunk in place")
    taskbar("restore", "A")
    wait_for(settled, "A grown in place")

    # The rectangles go with the object that set them.
    taskbar("rect", "A", "HEADLESS-2", 10, 300, 40, 40)
    taskbar("destroy", "A")
    assert minimize_state()[0] == {}, minimize_state()
    taskbar.finish()

    # A panel's own rectangle, wlr-foreign-toplevel's set_rectangle on its surface: the panel
    # along the bottom of an output, 48 tall, has the button 4 down from its top. A window
    # without a button goes to the middle of the panel's strip.
    desktop.spawn([probe, "--external-control"],
                  env={"PAW_PROBE_TITLE": "P", "PAW_PROBE_BUTTON": "100 4 40 40"})
    wait_for(lambda: "P" in windows() and any(row[0] == "paw-test-panel"
                                              for row in desktop.rows("layers")),
             "the panel and its window")
    panel = next(row[1] for row in desktop.rows("layers") if row[0] == "paw-test-panel")
    # Where that output is in the layout, and its logical size.
    left, width, height = {"HEADLESS-1": (0, 1280, 720), "HEADLESS-2": (1280, 640, 360)}[panel]

    def onto_panel(title):
        subprocess.run([window_probe, title, "output", panel], env=desktop.env, check=True,
                       timeout=30)
        wait_for(lambda: windows()[title].output == panel, f"{title} by the panel")
        wait_for(settled, f"{title} settled")

    p = window_id("P")
    wait_for(lambda: minimize_state()[0].get(p) == [(panel, 100, height - 44, 40, 40)],
             "the panel's rectangle in output coordinates")
    onto_panel("P")
    subprocess.run([window_probe, "P", "minimize"], env=desktop.env, check=True, timeout=30)
    assert folds()[(p, "minimize")][1:] == ((left + 100, height - 44, 40, 40), "button"), folds()
    wait_for(settled, "P minimized into the panel's button")
    desktop.open_window(probe, "B", "app-b", focused=True)
    b = window_id("B")
    onto_panel("B")
    subprocess.run([window_probe, "B", "minimize"], env=desktop.env, check=True, timeout=30)
    assert folds()[(b, "minimize")][1:] == (
        (left + width // 2 - 24, height - 48, 48, 48), "panel"), folds()

    # Closing as it minimizes: the copy goes on into the panel and then goes, leaving nothing.
    closing = desktop.open_window(probe, "C", "app-c", focused=True)
    wait_for(settled, "C opened")
    subprocess.run([window_probe, "C", "minimize"], env=desktop.env, check=True, timeout=30)
    assert animations()[0] >= 1
    closing.terminate()
    desktop.reap(closing)
    wait_for(lambda: "C" not in windows(), "C closed")
    wait_for(settled, "C's copy gone")
    # And as it is restored: it closes from where it is drawn.
    closing = desktop.open_window(probe, "D", "app-d", focused=True)
    wait_for(settled, "D opened")
    subprocess.run([window_probe, "D", "minimize"], env=desktop.env, check=True, timeout=30)
    wait_for(settled, "D minimized")
    subprocess.run([window_probe, "D", "restore"], env=desktop.env, check=True, timeout=30)
    assert any(kind == "restore" for _, kind in folds()), folds()
    closing.terminate()
    desktop.reap(closing)
    wait_for(lambda: "D" not in windows(), "D closed")
    wait_for(settled, "D gone")

    # Animations off: minimizing and restoring are instant.
    desktop.reload(CONFIG.replace("animations = {", "animations = { enabled = false,"))
    subprocess.run([window_probe, "P", "restore"], env=desktop.env, check=True, timeout=30)
    desktop.stays(lambda: not folds() and animations()[0] == 0, "nothing folds")
    assert windows()["P"].visible and not windows()["P"].minimized
    subprocess.run([window_probe, "P", "minimize"], env=desktop.env, check=True, timeout=30)
    desktop.stays(lambda: not folds() and animations()[0] == 0, "nothing folds")
    # minimize = { duration = 0 } turns it off alone.
    desktop.reload(CONFIG.replace("duration = 900", "duration = 0"))
    subprocess.run([window_probe, "P", "restore"], env=desktop.env, check=True, timeout=30)
    desktop.stays(lambda: not folds() and animations()[0] == 0, "nothing folds")

# Behind the lock screen nothing folds: a window minimized meanwhile is minimized at once. (The
# locker covers one output, so this session has one.)
ONE = CONFIG.replace('''            ["HEADLESS-2"] = { mode = "1280x720", scale = 2 },
''', "")
with harness.Compositor(compositor, ONE) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def windows():
        return {w.title: w for w in desktop.windows()}

    def folds():
        return [row for row in desktop.rows("minimize") if row[0] == "fold"]

    def settled():
        running, trees, _ = (int(n) for n in msg("get", "animations").split())
        return not folds() and running == 0 and trees == len(windows())

    desktop.open_window(probe, "E", "app-e", focused=True)
    wait_for(settled, "E opened")
    locker = desktop.spawn([lock_probe, "hold", str(desktop.root / "locker.log")])
    wait_for(lambda: "Session locked" in desktop.log.read_text(), "the session locked")
    subprocess.run([window_probe, "E", "minimize"], env=desktop.env, check=True, timeout=30)
    wait_for(lambda: windows()["E"].minimized, "E minimized while locked")
    desktop.stays(lambda: not folds(), "nothing folds behind the lock screen")
    locker.terminate()
    assert desktop.reap(locker) == 0
    wait_for(lambda: "Session unlocked" in desktop.log.read_text(), "the session unlocked")
    subprocess.run([window_probe, "E", "restore"], env=desktop.env, check=True, timeout=30)
    assert [row[2] for row in folds()] == ["restore"], folds()
    wait_for(settled, "E restored once unlocked")

# Minimized again on its way out while its tile glides (a window opening beside it), the copy
# takes over from where the window is drawn, glide and all. Everything a quarter as fast, the
# glide and the fold slow enough to look at between two requests.
TILES = CONFIG.replace("animations = { minimize = { duration = 900, curve = \"linear\" } },",
                       "animations = { speed = 0.25, minimize = { duration = 900, curve = \"linear\" },"
                       " move = { duration = 1000, curve = \"linear\" } },\n"
                       "    layout = { tiling = true },").replace(
    '''            ["HEADLESS-2"] = { mode = "1280x720", scale = 2 },
''', "")
assert "tiling = true" in TILES and "speed = 0.25" in TILES
with harness.Compositor(compositor, TILES) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def windows():
        return {w.title: w for w in desktop.windows()}

    def folds():
        """kind -> (at, box folded into, drawn box), for the only window that folds."""
        return {row[2]: (int(row[3]), tuple(map(int, row[4:8])), tuple(map(int, row[9:13])))
                for row in desktop.rows("minimize") if row[0] == "fold"}

    def settled():
        running, trees, _ = (int(n) for n in msg("get", "animations").split())
        return not folds() and running == 0 and trees == len(windows())

    desktop.detail = lambda: f"windows: {windows()}, folds: {folds()}"
    desktop.open_window(probe, "B", "app-b", focused=True)
    desktop.open_window(probe, "G", "app-g", focused=True)
    wait_for(lambda: settled() and windows()["G"].tiled, "G tiled beside B")
    left = windows()["G"].x
    taskbar = desktop.spawn([window_probe, "G", "taskbar"], stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE, text=True)

    def tell(*words):
        if words:
            taskbar.stdin.write(" ".join(words) + "\n")
            taskbar.stdin.flush()
        assert taskbar.stdout.readline().strip() == "ok", "the taskbar did not answer"

    tell()
    tell("minimize", "G")
    wait_for(settled, "G minimized")
    tell("restore", "G")
    wait_for(lambda: "restore" in folds() and folds()["restore"][0] <= 700, "G on its way out")
    desktop.open_window(probe, "C", "app-c", focused=True)
    wait_for(lambda: windows()["G"].x != left, "G moved aside for C")
    drawn = folds()["restore"][2]
    tell("minimize", "G")
    taken = folds()["minimize"][2]
    assert all(abs(a - b) <= 60 for a, b in zip(drawn, taken)), (drawn, taken)
    tell("restore", "G")
    taskbar.stdin.close()
    assert desktop.reap(taskbar) == 0

# A window changing size as it is maximized or restored (a morph): minimized on its way, its copy
# shrinks from the size and place it is drawn at; restored and then maximized or restored again
# as it grows out of its button, the change of size takes over from its place.
MORPHS = ONE.replace("animations = { minimize = { duration = 900, curve = \"linear\" } },",
                     "animations = { duration = 700, speed = 0.5, late_frame_ms = 0,"
                     " minimize = { duration = 900, curve = \"linear\" } },")
assert "speed = 0.5" in MORPHS
with harness.Compositor(compositor, MORPHS) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def windows():
        return {w.title: w for w in desktop.windows()}

    def folds():
        """kind -> (at, box folded into, drawn box, box unfolded), for the only window that
        folds."""
        return {row[2]: (int(row[3]), tuple(map(int, row[4:8])), tuple(map(int, row[9:13])),
                         tuple(map(int, row[14:18])))
                for row in desktop.rows("minimize") if row[0] == "fold"}

    def drawn():
        """Where W is drawn (x, y, width, height), and what its change of size does."""
        row = next(r for r in desktop.rows("drawn") if r[1] == "W")
        return tuple(int(n) for n in row[2:6]), row[8]

    def settled():
        running, trees, _ = (int(n) for n in msg("get", "animations").split())
        return not folds() and running == 0 and trees == len(windows())

    desktop.detail = lambda: f"windows: {windows()}, folds: {folds()}, drawn: {drawn()}"
    desktop.open_window(probe, "W", "app-w", focused=True)
    wait_for(settled, "W opened")
    start = drawn()[0]
    msg("maximize")
    full = windows()["W"].box
    wait_for(lambda: start[2] + 200 < drawn()[0][2] < 1000, "W growing to fill the output")
    before = drawn()[0]
    msg("minimize")
    # The copy sets off from where W is drawn as it is minimized: on its way from `before` to
    # filling the output, however far it has grown since the test looked, and not there yet.
    taken = folds()["minimize"][3]
    assert all(min(b, f) <= t <= max(b, f) for b, f, t in zip(before, full, taken)) and \
        taken[2] < full[2], (before, taken, full)
    wait_for(settled, "W minimized")
    subprocess.run([window_probe, "W", "restore"], env=desktop.env, check=True, timeout=30)
    assert "restore" in folds(), folds()
    msg("restore")
    assert "restore" not in folds() and drawn()[1] != "rest", (folds(), drawn())
    wait_for(lambda: settled() and drawn()[1] == "rest", "W back to its own size")
    assert drawn()[0][2:] == start[2:], (drawn(), start)
