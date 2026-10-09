# SPDX-License-Identifier: GPL-3.0-or-later
"""Unplugging an output moves its windows to the nearest one, keeping their workspace numbers and
tiling; plugging it back in returns them (outputs.return_windows), unless the setting is off or
they were placed elsewhere by hand. Uses the headless backend's virtual outputs."""
from pathlib import Path
import signal
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])


def config(primary="HEADLESS-1", tiling="true", extra="", first='mode = "1280x720"'):
    return f"""return {{
    xwayland = false,
    layout = {{ tiling = {tiling}, gap = 0 }},
    outputs = {{
        primary = "{primary}",
        {extra}
        monitors = {{
            ["HEADLESS-1"] = {{ {first} }},
            ["HEADLESS-2"] = {{ mode = "1280x720" }},
        }},
    }},
}}"""


with harness.Compositor(compositor, env={"WLR_HEADLESS_OUTPUTS": "2"}, start=False) as desktop:
    log = desktop.log

    def msg(*words):
        result = desktop.run(*words, timeout=5)
        assert result.returncode == 0 and not result.stdout.startswith("error"), \
            (words, result.stdout, result.stderr)
        return result.stdout

    def outputs():
        rows = [line.split("\t") for line in msg("get", "outputs").splitlines()]
        return {row[0]: tuple(int(v) for v in row[2:6]) for row in rows}  # x, y, width, height

    def windows():
        """One (workspace, tiled, x, y, width, height, output, visible) per window, oldest first."""
        rows = [line.split("\t") for line in msg("get", "windows").splitlines()]
        found = [(int(r[0]), r[3] == "1", *map(int, r[4:8]), r[10], r[11] == "1") for r in rows]
        return sorted(found, key=lambda w: (w[6], w[0], w[2], w[3]))

    def inside(window, output):
        x, y, width, height = outputs()[output]
        return (window[2] >= x and window[3] >= y and window[2] + window[4] <= x + width
                and window[3] + window[5] <= y + height)

    def disjoint(a, b):
        return (a[2] + a[4] <= b[2] or b[2] + b[4] <= a[2] or a[3] + a[5] <= b[3]
                or b[3] + b[5] <= a[3])

    desktop.detail = lambda: f"windows: {windows()}, outputs: {outputs()}"

    reloads = 0

    def reload(text):
        global reloads
        reloads += 1
        desktop.config.write_text(text)
        desktop.server.send_signal(signal.SIGHUP)
        desktop.wait_for(lambda: log.read_text().count("Configuration reloaded") == reloads,
                         "reload")

    def launch():
        count = len(windows())
        desktop.spawn([probe, "--external-control"])
        desktop.wait_for(lambda: len(windows()) == count + 1, f"window {count + 1} mapped")

    def unplug(name):
        msg("headless_output", "remove", name)
        desktop.wait_for(lambda: name not in outputs(), f"{name} removed")

    def plug(name):
        assert msg("headless_output", "add", name).split() == [name]
        desktop.wait_for(lambda: name in outputs(), f"{name} added")

    def finish():
        for window in desktop.clients:
            window.kill()
            window.wait(timeout=5)
        desktop.clients.clear()
        desktop.stop()

    # Tiled windows on two workspaces of HEADLESS-1, one window on HEADLESS-2.
    desktop.start(config())
    reloads = 0
    launch()
    launch()
    msg("output", "HEADLESS-1", "workspace", "2")
    launch()
    msg("output", "HEADLESS-1", "workspace", "1")
    reload(config(primary="HEADLESS-2"))
    launch()
    assert [(w[0], w[1], w[6]) for w in windows()] == [
        (1, True, "HEADLESS-1"), (1, True, "HEADLESS-1"), (2, True, "HEADLESS-1"),
        (1, True, "HEADLESS-2")], windows()

    unplug("HEADLESS-1")
    desktop.wait_for(lambda: all(w[6] == "HEADLESS-2" for w in windows()), "windows moved")
    state = windows()
    assert sorted((w[0], w[1]) for w in state) == [(1, True)] * 3 + [(2, True)], state
    assert all(inside(w, "HEADLESS-2") for w in state), state
    shown = [w for w in state if w[0] == 1]
    assert len(shown) == 3 and all(w[7] for w in shown), state
    assert not [w for w in state if w[0] == 2][0][7], "workspace 2 is not the one shown"
    assert all(disjoint(a, b) for i, a in enumerate(shown) for b in shown[i + 1:]), shown
    assert "Moved windows of HEADLESS-1" in log.read_text()

    plug("HEADLESS-1")
    desktop.wait_for(lambda: [w[6] for w in windows()] ==
                     ["HEADLESS-1", "HEADLESS-1", "HEADLESS-1", "HEADLESS-2"], "windows returned")
    state = windows()
    assert [(w[0], w[1]) for w in state] == [(1, True), (1, True), (2, True), (1, True)], state
    assert all(inside(w, w[6]) for w in state), state
    assert state[0][4] + state[1][4] == 1280 and disjoint(state[0], state[1]), state
    assert state[3][4] == 1280, state

    # Unplugging again works the same way.
    unplug("HEADLESS-1")
    desktop.wait_for(lambda: all(w[6] == "HEADLESS-2" for w in windows()), "windows moved again")
    plug("HEADLESS-1")
    desktop.wait_for(lambda: [w[6] for w in windows()].count("HEADLESS-1") == 3, "returned again")

    # With outputs.return_windows off they stay.
    reload(config(primary="HEADLESS-2", extra="return_windows = false,"))
    unplug("HEADLESS-1")
    desktop.wait_for(lambda: all(w[6] == "HEADLESS-2" for w in windows()),
                     "windows moved (no return)")
    plug("HEADLESS-1")
    assert all(w[6] == "HEADLESS-2" for w in windows()), windows()
    # Turning it on afterwards does not bring back what was not remembered.
    reload(config(primary="HEADLESS-2"))
    assert all(w[6] == "HEADLESS-2" for w in windows()), windows()
    finish()

    # Floating windows keep their relative place and size.
    desktop.start(config(tiling="false"))
    reloads = 0
    launch()
    before = windows()[0]
    assert not before[1] and before[6] == "HEADLESS-1"
    origin = outputs()["HEADLESS-1"]
    relative = (before[2] - origin[0], before[3] - origin[1])
    unplug("HEADLESS-1")
    desktop.wait_for(lambda: windows()[0][6] == "HEADLESS-2", "floating window moved")
    moved = windows()[0]
    assert inside(moved, "HEADLESS-2") and moved[4:6] == before[4:6], (before, moved)
    plug("HEADLESS-1")
    desktop.wait_for(lambda: windows()[0][6] == "HEADLESS-1", "floating window returned")
    # Turning an output off in the configuration moves its windows the same way, and turning
    # it on again returns them.
    reload(config(tiling="false", first="enabled = false"))
    desktop.wait_for(lambda: windows()[0][6] == "HEADLESS-2", "window left the disabled output")
    assert inside(windows()[0], "HEADLESS-2"), windows()
    reload(config(tiling="false"))
    desktop.wait_for(lambda: windows()[0][6] == "HEADLESS-1",
                     "window returned to the enabled output")
    back = windows()[0]
    offset = (back[2] - outputs()["HEADLESS-1"][0], back[3] - outputs()["HEADLESS-1"][1])
    assert abs(offset[0] - relative[0]) <= 1 and abs(offset[1] - relative[1]) <= 1, \
        (before, back)
    finish()
    # A fullscreen window covers whichever output it is on, above the probe's 48-pixel panel,
    # which is on HEADLESS-2.
    def above_panel(name):
        x, y, width, height = outputs()[name]
        return (x, y, width, height - 48 * (name == "HEADLESS-2"))

    desktop.start(config(tiling="false"))
    reloads = 0
    launch()
    msg("fullscreen")
    desktop.wait_for(lambda: windows()[0][4:6] == (1280, 720), "fullscreen")
    unplug("HEADLESS-1")
    desktop.wait_for(lambda: windows()[0][6] == "HEADLESS-2" and
                     windows()[0][2:6] == above_panel("HEADLESS-2"), "fullscreen window moved")
    plug("HEADLESS-1")
    desktop.wait_for(lambda: windows()[0][6] == "HEADLESS-1" and
                     windows()[0][2:6] == above_panel("HEADLESS-1"), "fullscreen window returned")
print("Output unplug and replug moved windows away and back")
