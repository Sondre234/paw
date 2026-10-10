# SPDX-License-Identifier: GPL-3.0-or-later
"""windows.placement: new floating windows cascade (the default), open centered, or open in free
space (smart), with a panel reserving the bottom; a rule's position wins; windows on another
workspace do not count; tiles are not moved by it."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

USABLE = (0, 0, 1280, 720 - 48)


def config(placement, extra="", tiling="false"):
    return """return {
    xwayland = false,
    layout = { tiling = %s },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    windows = { %s %s },
}""" % (tiling, ('placement = "%s",' % placement) if placement else "", extra)


with harness.Compositor(compositor, config(None)) as desktop:
    msg = desktop.msg

    def windows():
        """By title: x, y, width, height, tiled, workspace."""
        return {w.title: (w.x, w.y, w.width, w.height, w.tiled, w.workspace)
                for w in desktop.windows()}

    desktop.detail = lambda: f"windows: {windows()}"
    wait_for = desktop.wait_for

    def open_window(title, *, panel=False, app_id="paw-probe"):
        before = len(windows())
        client = desktop.spawn(
            [probe, "--external-control" if panel else "--window-only"],
            env={"PAW_PROBE_TITLE": title, "PAW_PROBE_APP_ID": app_id})
        wait_for(lambda: len(windows()) == before + 1 and title in windows(), f"{title} mapped")
        return client

    def rect(title):
        return windows()[title][:4]

    def apart(a, b):
        return (a[0] + a[2] <= b[0] or b[0] + b[2] <= a[0] or
                a[1] + a[3] <= b[1] or b[1] + b[3] <= a[1])

    def close_all(clients):
        for client in clients:
            client.terminate()
            desktop.reap(client, timeout=5)
        wait_for(lambda: not windows(), "windows closed")

    def start(placement, extra="", tiling="false"):
        desktop.reload(config(placement, extra, tiling))

    # Cascade, as before: 40 px in and 32 px for each window.
    clients = [open_window("A", panel=True)]
    assert rect("A") == (40, 40, 320, 240), windows()
    clients.append(open_window("B"))
    assert rect("B") == (72, 72, 320, 240), windows()
    clients.append(open_window("C"))
    assert rect("C") == (104, 104, 320, 240), windows()
    close_all(clients)

    # Center: the middle of the area above the panel, whatever is there.
    start("center")
    clients = [open_window("A", panel=True)]
    assert rect("A") == (480, 216, 320, 240), windows()
    clients.append(open_window("B"))
    assert rect("B") == (480, 216, 320, 240), windows()
    close_all(clients)

    # Smart: the first window is centered, then the others keep out of the way.
    start("smart")
    clients = [open_window("A", panel=True)]
    assert rect("A") == (480, 216, 320, 240), windows()
    placed = ["A"]
    for title in "BC":
        clients.append(open_window(title))
        r = rect(title)
        assert all(apart(r, rect(other)) for other in placed), (title, windows())
        assert (r[0] >= 0 and r[1] >= 0 and r[0] + r[2] <= USABLE[2]
                and r[1] + r[3] <= USABLE[3]), (title, windows())
        placed.append(title)
    # The second sits in a free strip beside the first, centered vertically.
    assert rect("B")[1] == 216 and rect("B")[0] in (80, 880), windows()
    # No room is left for a fourth (the strips above and below are too low), so it
    # cascades, one step for each window there.
    clients.append(open_window("D"))
    assert rect("D") == (136, 136, 320, 240), windows()

    # A window on another workspace leaves its space to the next one.
    msg("workspace", "2")
    clients.append(open_window("E"))
    assert rect("E") == (480, 216, 320, 240) and windows()["E"][5] == 2, windows()
    msg("workspace", "1")
    close_all(clients)

    # A rule's position beats the mode.
    start("smart", 'rules = { { app_id = "^placed$", position = { 30, 40 } },'
                   ' { app_id = "^middle$", position = "center" } },')
    clients = [open_window("A", panel=True)]
    clients.append(open_window("P", app_id="placed"))
    assert rect("P") == (30, 40, 320, 240), windows()
    clients.append(open_window("M", app_id="middle"))
    assert rect("M") == (480, 216, 320, 240), windows()
    close_all(clients)

    # Tiles are laid out by the tiling, not by the placement mode.
    start("smart", tiling="true")
    clients = [open_window("A", panel=True)]
    clients.append(open_window("B"))
    assert windows()["A"][4] and windows()["B"][4], windows()
    assert apart(rect("A"), rect("B")), windows()
    close_all(clients)
print("Window placement cascades, centers, finds free space, and yields to rules")
