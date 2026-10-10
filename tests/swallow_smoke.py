# SPDX-License-Identifier: GPL-3.0-or-later
"""Window swallowing: a window started from a terminal (found through the process ancestry) takes
the terminal's tile or floating place and hides it, closing the window brings the terminal back,
swallow_toggle does it by hand, and the exceptions, the terminal list and the setting are obeyed."""
from pathlib import Path
import sys

import harness

compositor, probe, example = (str(Path(p).resolve()) for p in sys.argv[1:4])

# A lone tile keeps its gaps, which tell it from a fullscreen window.
base = (Path(example).read_text().replace("xwayland = true", "xwayland = false")
        .replace("tiling = false,", "tiling = true,", 1)
        .replace("    layout = {\n", "    layout = {\n        smart_gaps = false,\n", 1))
assert "    windows = {\n" in base


def with_swallow(enabled):
    return base.replace(
        "    windows = {\n",
        "    windows = {\n        swallow = { enabled = %s, terminals = { \"swallow-term\" },"
        " exceptions = { \"swallow-exempt\" } },\n" % str(enabled).lower(), 1)


with harness.Compositor(compositor, with_swallow(True)) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def windows():
        """app_id -> (focused, tiled, x, y, width, height, visible, workspace) of every window."""
        rows = {}
        for f in desktop.rows("windows"):
            rows[f[8]] = (f[1] == "1", f[3] == "1", int(f[4]), int(f[5]), int(f[6]), int(f[7]),
                          f[11] == "1", int(f[0]))
        return rows

    def swallow():
        """app_id -> (swallowed, visible, peer)."""
        return {f[0]: (f[1] == "1", f[2] == "1", f[3]) for f in desktop.rows("swallow")}

    def rect(row):
        return row[1:6]

    def count():
        return msg("get", "windows").count("\n")

    def open_window(app_id, **extra):
        before = count()
        process = desktop.spawn([probe, "--window-only"],
                                env={"PAW_PROBE_APP_ID": app_id, **extra})
        wait_for(lambda: count() == before + 1 and app_id in windows(), f"{app_id} opens")
        return process

    def start_child(terminal, app_id, expect):
        """Signals the terminal to start a window; returns once `expect` windows exist."""
        terminal.send_signal(10)  # SIGUSR1
        wait_for(lambda: count() == expect and app_id in windows(), f"{app_id} opens")

    def finish(process):
        assert desktop.reap(process) == 0

    def close_focused(app_id):
        msg("close")
        wait_for(lambda: app_id not in windows(), f"{app_id} closes")

    # An unrelated window, then a terminal that starts its window through a shell.
    open_window("plain")
    term = open_window("swallow-term", PAW_PROBE_SPAWN_APP_ID="swallow-child",
                       PAW_PROBE_SPAWN_SHELL="1")
    rows = windows()
    slot = rect(rows["swallow-term"])
    assert rows["swallow-term"][0] and rows["swallow-term"][1], rows
    assert swallow()["swallow-term"] == (False, True, "-"), swallow()

    # The child (a grandchild process) takes the tile and hides the terminal.
    start_child(term, "swallow-child", 3)
    rows = windows()
    assert rect(rows["swallow-child"]) == slot, (rows, slot)
    assert rows["swallow-child"][0] and rows["swallow-child"][6], rows
    assert not rows["swallow-term"][6] and not rows["swallow-term"][0], rows
    assert swallow()["swallow-term"] == (True, False, "swallow-child"), swallow()
    assert swallow()["swallow-child"] == (False, True, "swallow-term"), swallow()
    assert rect(rows["plain"])[0] and rows["plain"][6], rows  # the neighbour is untouched
    plain_rect = rect(rows["plain"])

    # Closing the window brings the terminal back into the tile, focused.
    close_focused("swallow-child")
    rows = windows()
    assert rect(rows["swallow-term"]) == slot and rows["swallow-term"][6], rows
    assert rows["swallow-term"][0], rows
    assert rect(rows["plain"]) == plain_rect, rows
    assert swallow()["swallow-term"] == (False, True, "-"), swallow()

    # A floating terminal: the child takes its rectangle, and gives it back.
    msg("toggle_floating")
    wait_for(lambda: not windows()["swallow-term"][1], "terminal floats")
    floating = rect(windows()["swallow-term"])
    start_child(term, "swallow-child", 3)
    rows = windows()
    assert not rows["swallow-child"][1] and rect(rows["swallow-child"]) == floating, rows
    assert not rows["swallow-term"][6], rows
    close_focused("swallow-child")
    rows = windows()
    assert rect(rows["swallow-term"]) == floating and rows["swallow-term"][6], rows
    msg("toggle_floating")
    wait_for(lambda: windows()["swallow-term"][1], "terminal tiles again")
    assert rect(windows()["swallow-term"]) == slot, windows()

    # An exception does not swallow; neither does a window that is a terminal itself.
    start = count()
    term.terminate()
    desktop.reap(term)
    wait_for(lambda: count() == start - 1, "terminal closes")
    term = open_window("Swallow-Term", PAW_PROBE_SPAWN_APP_ID="swallow-exempt")
    start_child(term, "swallow-exempt", 3)
    assert all(row[6] for row in windows().values()), windows()
    close_focused("swallow-exempt")
    msg("close")
    finish(term)
    term = open_window("Swallow-Term", PAW_PROBE_SPAWN_APP_ID="swallow-term")
    start_child(term, "swallow-term", 3)
    assert all(row[6] for row in windows().values()), windows()  # the app_id is case-blind
    close_focused("swallow-term")
    msg("close")
    finish(term)

    # A window that closed on another workspace, or fullscreen, gives the terminal back
    # there, not fullscreen.
    term = open_window("swallow-term", PAW_PROBE_SPAWN_APP_ID="swallow-child")
    slot = rect(windows()["swallow-term"])
    start_child(term, "swallow-child", 3)
    msg("move_to_workspace", "3")
    wait_for(lambda: windows()["swallow-child"][7] == 3, "child on workspace 3")
    assert windows()["swallow-term"][7] == 1 and not windows()["swallow-term"][6]
    msg("workspace", "3")
    msg("fullscreen")
    wait_for(lambda: windows()["swallow-child"][4] == 1280, "child fullscreen")
    close_focused("swallow-child")
    rows = windows()
    assert rows["swallow-term"][7] == 3 and rows["swallow-term"][6], rows
    assert rows["swallow-term"][0] and rows["swallow-term"][4] < 1280, rows
    msg("workspace", "1")
    assert not windows()["swallow-term"][6], windows()
    msg("workspace", "3")
    msg("move_to_workspace", "1")
    msg("workspace", "1")
    wait_for(lambda: windows()["swallow-term"][7] == 1 and windows()["swallow-term"][6],
             "terminal back on workspace 1")

    # A terminal on another workspace than the one shown: its window opens there.
    msg("workspace", "2")
    start_child(term, "swallow-child", 3)
    rows = windows()
    assert rows["swallow-child"][7] == 1 and not rows["swallow-child"][6], rows
    assert not rows["swallow-term"][6], rows
    msg("workspace", "1")
    rows = windows()
    assert rows["swallow-child"][6] and not rows["swallow-term"][6], rows
    close_focused("swallow-child")
    msg("close")
    finish(term)

    # Killing a swallowed terminal leaves the window that replaced it in its slot.
    term = open_window("swallow-term", PAW_PROBE_SPAWN_APP_ID="swallow-child")
    slot = rect(windows()["swallow-term"])
    start_child(term, "swallow-child", 3)
    term.terminate()
    desktop.reap(term)
    wait_for(lambda: count() == 2, "swallowed terminal gone")
    rows = windows()
    assert rect(rows["swallow-child"]) == slot and rows["swallow-child"][6], rows
    assert swallow()["swallow-child"] == (False, True, "-"), swallow()
    close_focused("swallow-child")

    # With the setting off nothing swallows by itself, but swallow_toggle still works.
    desktop.reload(with_swallow(False))
    term = open_window("swallow-term", PAW_PROBE_SPAWN_APP_ID="swallow-child")
    slot = rect(windows()["swallow-term"])
    start_child(term, "swallow-child", 3)
    rows = windows()
    assert rows["swallow-term"][6] and rows["swallow-child"][6], rows
    assert rect(rows["swallow-child"]) != slot, rows
    msg("swallow_toggle")
    rows = windows()
    assert rect(rows["swallow-child"]) == slot and not rows["swallow-term"][6], rows
    assert swallow()["swallow-term"] == (True, False, "swallow-child"), swallow()
    # Again: the terminal comes back beside it, and both show.
    msg("swallow_toggle")
    rows = windows()
    assert rows["swallow-term"][6] and rows["swallow-child"][6], rows
    assert rows["swallow-term"][1] and rows["swallow-child"][1], rows
    assert rect(rows["swallow-term"]) != rect(rows["swallow-child"]), rows
    assert swallow()["swallow-child"] == (False, True, "-"), swallow()
    close_focused("swallow-child")
    msg("close")
    finish(term)
print("Window swallowing takes tiles and floating places, restores them, and obeys its settings")
