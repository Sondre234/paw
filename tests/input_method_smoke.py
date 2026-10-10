# SPDX-License-Identifier: GPL-3.0-or-later
"""Input methods, as fcitx5 and ibus are: the compositor relays between an application's text
input (text-input-unstable-v3, `text_input_probe`) and the input method (input-method-unstable-v2,
`input_method_probe`). The text input of the window with the keyboard activates the input method
as it is enabled, and deactivates it as it is disabled, and tells it its surrounding text, content
type and text cursor; the input method's preedit,
committed text and deletions go back to it. Its keyboard grab gets the keys no binding takes, and
what it passes on through its virtual keyboard reaches the window. Its popup sits under the text
cursor, kept on the output, above it where there is no room below, drawn over the window and under
the overlays. Focus moving between windows, either client going away and the session locking leave
both sides consistent, and a second input method is told it is unavailable. `get input_method`
shows it all."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, text_probe, method_probe, lock_probe, input_probe = (
    str(Path(p).resolve()) for p in sys.argv[1:6])
grim = sys.argv[6] if len(sys.argv) > 6 else ""

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false },
    mouse = { focus_follows = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    windows = { rules = {
        { title = "^Writer$", position = { 100, 100 } },
        { title = "^Other$", position = { 600, 100 } },
    } },
    bindings = { { mods = { "Super" }, key = "t", action = "toggle_tiling" } },
}"""
# evdev's codes
SUPER, T, A = 125, 20, 30
# text-input-v3's spellcheck hint, normal purpose and "other" change cause
SPELLCHECK, NORMAL, OTHER = 2, 0, 1
UTF8 = {"encoding": "utf-8"}
# The probes' colours: the text input's window, the input method's popup, input_probe's panel
PAPER, POPUP, PANEL = (0xf4, 0xf1, 0xe8), (0x2a, 0x5b, 0xd7), (0x6a, 0x8f, 0x3c)

with harness.Compositor(compositor, CONFIG) as desktop:
    msg = desktop.msg

    def log(name):
        return (desktop.root / f"{name}.log").read_text(**UTF8).splitlines()

    def since(name, mark):
        return log(name)[mark:]

    def focused():
        return next((r[9] for r in desktop.rows("windows") if r[1] == "1"), None)

    def origin(title):
        """Where the window is in the layout."""
        return next((int(r[4]), int(r[5])) for r in desktop.rows("windows") if r[9] == title)

    def state():
        """(connected, active, grabbing), the text inputs as (enabled, served, surface's window)
        and the popups as (shown, x, y, width, height)."""
        rows = desktop.rows("input_method")
        assert rows[0][0] == "input_method", rows
        method = tuple(field == "1" for field in rows[0][1:])
        inputs = [(r[1] == "1", r[2] == "1", r[4]) for r in rows if r[0] == "text_input"]
        popups = [(r[1] == "1", *map(int, r[2:])) for r in rows if r[0] == "popup"]
        return method, inputs, popups

    def served():
        """The window or panel of the text input the input method serves, or None."""
        return next((name for enabled, active, name in state()[1] if active), None)

    def tell(process, command):
        process.stdin.write(command + "\n")
        process.stdin.flush()

    def press(*codes):
        """Presses the keys in order, then releases them the other way round."""
        for code in codes:
            msg("headless_keyboard", "key", "keys", str(code), "press")
        for code in reversed(codes):
            msg("headless_keyboard", "key", "keys", str(code), "release")

    def window(title):
        process = desktop.spawn([text_probe, title], log=f"{title}.log", stdin=subprocess.PIPE,
                                text=True, **UTF8)
        desktop.wait_for(lambda: "ready" in log(title) and focused() == title,
                         f"{title} mapped and focused")
        return process

    def method(name):
        process = desktop.spawn([method_probe], log=f"{name}.log", stdin=subprocess.PIPE,
                                text=True, **UTF8)
        desktop.wait_for(lambda: "ready" in log(name), f"{name} connected")
        return process

    def activated(name, mark, surrounding="hello"):
        """What the input method hears as it is activated for a text input. One that connects
        while a text input waits is activated as it connects: its "ready" (counted in `mark`) can
        come after the activation as well as before it."""
        heard = [line for line in log(name) if line != "ready"]
        return heard[mark - 1:][:5] == [
            "activate", f"surrounding {surrounding} {len(surrounding)} {len(surrounding)}",
            f"cause {OTHER}", f"content_type {SPELLCHECK} {NORMAL}", "done"]

    desktop.detail = lambda: f"input method: {state()}, focused: {focused()}"
    msg("headless_keyboard", "add", "keys")

    # Without an input method, the text input waits.
    writer = window("Writer")
    assert state() == ((False, False, False), [(False, False, "-")], []), state()
    assert "enter" not in log("Writer"), log("Writer")

    # One connects: the text input enters the window with the keyboard and enables itself, which
    # activates the input method with the text input's state.
    ime = method("ime")
    desktop.wait_for(lambda: activated("ime", 1), "the input method activated")
    assert log("Writer")[-1] == "enter", log("Writer")
    assert state() == ((True, True, False), [(True, True, "Writer")], []), state()

    # What the input method commits reaches the text input: text, a preedit, a deletion.
    mark = len(log("Writer"))
    tell(ime, "commit 你好")
    desktop.wait_for(lambda: since("Writer", mark) == ["commit 你好", "done"], "the commit")
    tell(ime, "preedit ni 0 2")
    desktop.wait_for(lambda: since("Writer", mark)[2:] == ["preedit ni 0 2", "done"],
                     "the preedit")
    tell(ime, "commit 你")  # the preedit goes as the text comes
    desktop.wait_for(lambda: since("Writer", mark)[4:] == ["commit 你", "done"],
                     "the commit replacing the preedit")
    tell(ime, "delete 1 0")
    desktop.wait_for(lambda: since("Writer", mark)[6:] == ["delete 1 0", "done"],
                     "the deletion")
    # The text input's changes reach the input method.
    mark = len(log("ime"))
    tell(writer, "surrounding hello world")
    desktop.wait_for(lambda: since("ime", mark) == [
        "surrounding hello world 11 11", f"cause {OTHER}",
        f"content_type {SPELLCHECK} {NORMAL}", "done"], "the new surrounding text")
    # Disabled, as a text field loses focus, the text input turns the input method off; enabled,
    # on again.
    mark = len(log("ime"))
    tell(writer, "disable")
    desktop.wait_for(lambda: since("ime", mark) == ["deactivate", "done"], "the text input off")
    assert state()[:2] == ((True, False, False), [(False, False, "Writer")]), state()
    tell(writer, "enable")
    desktop.wait_for(lambda: activated("ime", mark + 2, "hello world"), "the text input on")
    assert state()[:2] == ((True, True, False), [(True, True, "Writer")]), state()

    # Its keyboard grab gets the keys no binding takes; a binding still runs, its key never
    # reaching the grab, and the window hears none of them.
    tell(ime, "grab")
    desktop.wait_for(lambda: "keymap" in log("ime") and state()[0] == (True, True, True),
                     "the keyboard grabbed")
    mark = len(log("ime"))
    press(A)
    desktop.wait_for(lambda: [line for line in since("ime", mark) if line.startswith("key ")] ==
                     [f"key {A} pressed", f"key {A} released"], "a key in the grab")
    press(SUPER, T)
    desktop.wait_for(lambda: msg("get", "tiling").strip() == "on", "the binding run")
    desktop.wait_for(lambda: [line for line in since("ime", mark) if line.startswith("key ")][2:]
                     == [f"key {SUPER} pressed", f"key {SUPER} released"],
                     "Super in the grab, T not")
    msg("toggle_tiling")
    assert not [line for line in log("Writer") if line.startswith("key ")], log("Writer")
    # What it passes on through its virtual keyboard reaches the window, not the grab again.
    mark = len(log("ime"))
    tell(ime, "forward 30")
    desktop.wait_for(lambda: [line for line in log("Writer") if line.startswith("key ")] ==
                     [f"key {A} pressed", f"key {A} released"], "the key passed on")
    assert not [line for line in since("ime", mark) if line.startswith("key ")], log("ime")
    tell(ime, "release")
    desktop.wait_for(lambda: state()[0] == (True, True, False), "the grab released")
    press(A)
    desktop.wait_for(lambda: [line for line in log("Writer") if line.startswith("key ")][2:] ==
                     [f"key {A} pressed", f"key {A} released"], "a key in the window again")

    # Its popup sits under the text cursor (at 40, 50 in the window, 2 by 20), and the input
    # method hears where the cursor is from it.
    x, y = origin("Writer")
    mark = len(log("ime"))
    tell(ime, "popup 200 100")
    desktop.wait_for(lambda: state()[2] == [(True, x + 40, y + 70, 200, 100)], "the popup placed")
    desktop.wait_for(lambda: "rectangle 0 -20 2 20" in since("ime", mark), "the cursor told")
    # Past the output's right edge it moves in; with no room below it goes above the cursor.
    tell(writer, "cursor 1250 50 2 20")
    desktop.wait_for(lambda: state()[2] == [(True, 1080, y + 70, 200, 100)], "the popup moved in")
    desktop.wait_for(lambda: f"rectangle {x + 1250 - 1080} -20 2 20" in since("ime", mark),
                     "the cursor told")
    tell(writer, f"cursor 40 {700 - y} 2 20")
    desktop.wait_for(lambda: state()[2] == [(True, x + 40, 600, 200, 100)], "the popup above")
    desktop.wait_for(lambda: "rectangle 0 100 2 20" in since("ime", mark), "the cursor told")
    # It is drawn over the window, and under a panel on the overlay layer (as the shell's
    # overlays are) along the bottom of the output.
    if grim:
        def shows(*points):
            shot = harness.grab(grim, desktop.env, "HEADLESS-1")
            return all(shot.at(px, py) == colour for px, py, colour in points)
        desktop.wait_for(lambda: shows((x + 60, 620, POPUP), (x + 60, 690, POPUP),
                                       (x + 300, y + 20, PAPER)), "the popup over the window")
        bar = desktop.spawn([input_probe, "--overlay", "--no-gestures", "--no-touch",
                             "--no-tablet", "Bar"], log="bar.log")
        desktop.wait_for(lambda: "ready" in log("bar") and
                         shows((x + 60, 620, POPUP), (x + 60, 690, PANEL)),
                         "the popup under the overlay")
        bar.terminate()
        desktop.reap(bar)
    # A search field on the overlay layer, as the shell's start menu is, takes the keyboard: the
    # input method serves it, and its popup follows it and is drawn over the overlay.
    search = desktop.spawn([text_probe, "--overlay", "Search"], log="Search.log",
                           stdin=subprocess.PIPE, text=True, **UTF8)
    desktop.wait_for(lambda: served() == "Search", "the input method serving the search field")
    tell(search, "cursor 40 0 2 20")  # the field is 400 by 60 at the middle of the top edge
    desktop.wait_for(lambda: state()[2] == [(True, 480, 20, 200, 100)], "the popup at the field")
    if grim:
        desktop.wait_for(lambda: shows((500, 40, POPUP)), "the popup over the overlay")
    search.terminate()
    desktop.reap(search)
    desktop.wait_for(lambda: served() == "Writer", "the input method back at Writer")
    mark = len(log("ime"))
    tell(writer, "cursor 40 50 2 20")
    desktop.wait_for(lambda: since("ime", mark)[-1:] == ["rectangle 0 -20 2 20"],
                     "the cursor back")

    # Another window takes the keyboard: the first text input leaves and the input method serves
    # the other, its popup following.
    mark = len(log("ime"))
    other = window("Other")
    desktop.wait_for(lambda: since("ime", mark)[:2] == ["deactivate", "done"] and
                     activated("ime", mark + 2), "the input method moved to Other")
    assert log("Writer")[-1] == "leave", log("Writer")
    assert state()[1] == [(True, False, "-"), (True, True, "Other")], state()
    x, y = origin("Other")
    desktop.wait_for(lambda: state()[2] == [(True, x + 40, y + 70, 200, 100)],
                     "the popup at Other")
    # Back to the first, which entered enabled still: it is served as it commits.
    mark = len(log("ime"))
    msg("focus_last")
    desktop.wait_for(lambda: focused() == "Writer" and since("ime", mark)[:2] ==
                     ["deactivate", "done"] and activated("ime", mark + 2, "hello world"),
                     "the input method back at Writer")
    assert state()[1] == [(True, True, "Writer"), (True, False, "-")], state()

    # The window with the text input goes: the input method is deactivated, and serves the
    # window that has the keyboard next.
    mark = len(log("ime"))
    writer.terminate()
    desktop.reap(writer)
    desktop.wait_for(lambda: since("ime", mark)[:2] == ["deactivate", "done"] and
                     activated("ime", mark + 2), "the input method moved to Other again")
    assert focused() == "Other" and state() == ((True, True, False), [(True, True, "Other")],
                                                [(True, x + 40, y + 70, 200, 100)]), state()

    # The session locked, the input method is deactivated and its grab hears no key; the lock
    # screen gets them. Unlocked, it serves the window again.
    tell(ime, "grab")
    desktop.wait_for(lambda: state()[0] == (True, True, True), "the keyboard grabbed again")
    mark = len(log("ime"))
    locker = desktop.spawn([lock_probe, "hold", str(desktop.root / "locker.log")])
    desktop.wait_for(lambda: "Session locked" in desktop.log.read_text(), "the session locked")
    desktop.wait_for(lambda: since("ime", mark)[:2] == ["deactivate", "done"],
                     "the input method deactivated by the lock")
    press(A)
    desktop.stays(lambda: not [line for line in since("ime", mark) if line.startswith("key ")],
                  "no key in the grab while locked")
    assert state()[0] == (True, False, True) and not state()[2][0][0], state()
    locker.terminate()
    assert desktop.reap(locker) == 0
    desktop.wait_for(lambda: "Session unlocked" in desktop.log.read_text(),
                     "the session unlocked")
    desktop.wait_for(lambda: focused() == "Other" and state()[0] == (True, True, True),
                     "the input method serving Other again")

    # The input method goes: the text input leaves, to enter again as another connects. Only one
    # is connected at a time.
    mark = len(log("Other"))
    ime.terminate()
    desktop.reap(ime)
    desktop.wait_for(lambda: since("Other", mark) == ["leave"], "the text input left")
    assert state() == ((False, False, False), [(True, False, "-")], []), state()
    second = method("second")
    desktop.wait_for(lambda: activated("second", 1), "the second input method activated")
    assert since("Other", mark)[1:] == ["enter"], log("Other")
    third = method("third")
    desktop.wait_for(lambda: "unavailable" in log("third"), "a third told it is unavailable")
    assert state()[0] == (True, True, False), state()
    other.stdin.close()
print("Input methods relay text, keys and popups between applications and fcitx5-like clients, "
      "passed")
