# SPDX-License-Identifier: GPL-3.0-or-later
"""The window switcher lists every window on every output and workspace, most recently focused
first, on the focused output; it moves its selection, focuses the chosen window (switching its
output's workspace), cancels, follows windows closing, and tells subscribers each step, naming
each window by the number the window control gives it too. Alt+Tab from a keyboard confirms on
releasing Alt."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe, window_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false, workspaces = 4 },
    outputs = { order = { "HEADLESS-1", "HEADLESS-2" },
                monitors = { ["HEADLESS-1"] = { mode = "1280x720" },
                             ["HEADLESS-2"] = { mode = "1280x720" } } },
    windows = { rules = { { title = "^C$", output = "HEADLESS-2" } } },
    bindings = {
        { mods = { "Alt" }, key = "Tab", action = "switcher" },
        { mods = { "Alt", "Shift" }, key = "Tab", action = "switcher_prev" },
    },
}"""

with harness.Compositor(compositor, CONFIG, env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def windows():
        """By title: workspace, focused, minimized, output, visible."""
        return {w.title: dict(workspace=w.workspace, focused=w.focused, minimized=w.minimized,
                              output=w.output, visible=w.visible) for w in desktop.windows()}

    def focused():
        return next((t for t, w in windows().items() if w["focused"]), None)

    desktop.detail = lambda: f"windows: {windows()}"

    def expect(predicate, message):
        """Waits for the switcher lines heard to satisfy `predicate`, then forgets them."""
        lines = []

        def seen():
            lines[:] = events.lines("switcher")
            return predicate(lines)
        wait_for(seen, message, detail=lambda: f"events: {lines}")
        events.forget()
        return lines

    def number(title):
        """The window's number, as the window control gives it a taskbar."""
        return subprocess.run([window_probe, title, "id"], env=desktop.env, check=True,
                              capture_output=True, text=True, timeout=30).stdout.strip()

    def opened(lines):
        """The output, selection, and window titles of the last full switcher announcement."""
        for i in range(len(lines) - 1, -1, -1):
            words = lines[i].split(" ")
            if words[0] == "switcher" and len(lines) - i - 1 >= int(words[3]):
                entries = [line[len("switcher-window "):].split("\t")
                           for line in lines[i + 1:i + 1 + int(words[3])]]
                return words[1], int(words[2]), [e[1] for e in entries], entries
        return None

    clients = {}
    events = desktop.subscribe()

    def open_window(title):
        clients[title] = desktop.open_window(probe, title)

    # A and B on workspace 1 of HEADLESS-1, D on its workspace 2, C on HEADLESS-2.
    open_window("A")
    open_window("B")
    msg("output", "HEADLESS-1", "workspace", "2")
    open_window("D")
    msg("output", "HEADLESS-1", "workspace", "1")
    msg("switcher")  # Switching back focused B.
    _, selected, titles, _ = opened(expect(opened, "switcher opened"))
    assert titles == ["B", "D", "A"] and selected == 1, (titles, selected)
    msg("switcher_cancel")
    expect(lambda l: "switcher-close" in l, "switcher cancelled")
    open_window("C")
    wait_for(lambda: windows()["C"]["output"] == "HEADLESS-2" and focused() == "C",
             "C on HEADLESS-2 and focused")

    # Every window, most recently focused first, on the focused output.
    msg("switcher")
    where, selected, titles, entries = opened(expect(opened, "switcher opened"))
    assert where == "HEADLESS-2", where
    assert titles == ["C", "B", "D", "A"], titles
    assert selected == 1, selected
    d = entries[2]
    assert d[2] == "HEADLESS-1" and d[3] == "2" and d[4] == "0", d
    # Each line ends with the window's number, the one the window control gives it: one of its
    # own, C's the last given as C came last.
    assert all(len(e) == 7 for e in entries), entries
    ids = {e[1]: e[6] for e in entries}
    assert ids == {title: number(title) for title in titles}, ids
    assert len(set(ids.values())) == 4 and max(ids.values(), key=int) == ids["C"], ids
    msg("switcher")
    expect(lambda l: l == ["switcher-select 2"], "next")
    msg("switcher_prev")
    msg("switcher_prev")
    msg("switcher_prev")
    expect(lambda l: l == ["switcher-select 1", "switcher-select 0",
                                  "switcher-select 3"], "previous, wrapping around")
    assert focused() == "C", "the switcher focused a window before confirming"

    # Confirming focuses A, on its output's current workspace.
    msg("switcher_confirm")
    expect(lambda l: l == ["switcher-close"], "switcher closed")
    wait_for(lambda: focused() == "A", "A focused")

    # D is on a workspace HEADLESS-1 does not show: picking it switches there.
    msg("switcher")
    _, _, titles, _ = opened(expect(opened, "switcher reopened"))
    assert titles == ["A", "C", "B", "D"], titles
    msg("switcher_confirm", "4")
    wait_for(lambda: focused() == "D" and windows()["D"]["visible"] and
             not windows()["A"]["visible"], "D focused on workspace 2")
    expect(lambda l: "switcher-close" in l, "closed after picking")

    # Cancelling leaves focus alone.
    msg("switcher")
    expect(opened, "switcher opened to cancel")
    msg("switcher_cancel")
    expect(lambda l: l == ["switcher-close"], "cancelled")
    assert focused() == "D"

    # A listed window closing leaves the list; the selection stays on its window.
    msg("switcher")
    _, selected, titles, _ = opened(expect(opened, "opened before a close"))
    assert titles == ["D", "A", "C", "B"] and selected == 1, (titles, selected)
    msg("switcher")  # C selected
    expect(lambda l: l == ["switcher-select 2"], "C selected")
    clients["B"].terminate()
    desktop.reap(clients["B"])
    _, selected, titles, entries = opened(expect(opened, "list without B"))
    assert titles == ["D", "A", "C"] and selected == 2, (titles, selected)
    assert [e[6] for e in entries] == [ids[t] for t in titles], (entries, ids)
    clients["C"].terminate()
    desktop.reap(clients["C"])
    _, selected, titles, _ = opened(expect(opened, "list without C"))
    assert titles == ["D", "A"] and selected == 1, (titles, selected)
    msg("switcher_confirm")
    wait_for(lambda: focused() == "A", "A focused after the list shrank")

    error = desktop.run("switcher_confirm", "x")
    assert error.returncode != 0 and "switcher_confirm takes" in error.stdout + \
        error.stderr, (error.stdout, error.stderr)

    # The keyboard: Alt+Tab then releasing Alt; Alt+Shift+Tab; Escape (evdev's key codes).
    ALT, SHIFT, TAB, ESCAPE = 56, 42, 15, 1
    press = desktop.keyboard()
    events.forget()
    press(ALT, TAB)
    wait_for(lambda: focused() == "D", "Alt+Tab picks the previous window, D")
    expect(lambda l: "switcher-close" in l, "closed on releasing Alt")
    press(ALT, SHIFT, TAB)
    wait_for(lambda: focused() == "A", "Alt+Shift+Tab picks the last, A")
    press.down(ALT)
    press(TAB)
    press(ESCAPE)
    press.up(ALT)
    expect(lambda l: "switcher-close" in l, "Escape closes")
    desktop.stays(lambda: focused() == "A", "Escape still switched")
print("Window switcher passed")
