# SPDX-License-Identifier: GPL-3.0-or-later
"""paw-window-control-v1's workspaces, as the taskbar's card of a workspace draws it: a
paw_workspace_v1 lists the windows a workspace of an output shows, from the bottom up, each by its
number with the rectangle its picture covers on the output, in logical pixels from the output's
corner. It lists them again, whole and only then, as they are raised, move, change size, go to
another workspace, are minimized, made sticky or fullscreen, open and close. A window drawing a
shadow of its own around itself is listed with it, as its picture shows it. Another output's
windows are placed from that output's corner, at its scale in logical pixels; an output not
connected, or a workspace past the last, lists none."""
from pathlib import Path
import queue
import subprocess
import sys
import threading

import harness

compositor, probe, window_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = true, gap = 0, workspaces = 4 },
    features = { sticky = true },
    animations = { enabled = false },
    outputs = {
        primary = "HEADLESS-1",
        monitors = {
            ["HEADLESS-1"] = { mode = "1280x720", tiling = false },
            ["HEADLESS-2"] = { mode = "1280x720", scale = 2 },
        },
    },
}"""
SHADOW = 12  # the margin a window draws around itself, as a client-side frame's shadow

with harness.Compositor(compositor, CONFIG, env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    def windows():
        return {w.title: w for w in desktop.windows()}

    def outputs():
        """name -> (x, y, width, height) in the layout."""
        return {row[0]: tuple(map(int, row[2:6])) for row in desktop.rows("outputs")}

    desktop.detail = lambda: f"windows: {windows()}"

    def probed(*words):
        """What the window probe prints; it must exit cleanly, else what it said is the failure."""
        result = subprocess.run([window_probe, *words], env=desktop.env, capture_output=True,
                                text=True, timeout=30)
        assert result.returncode == 0, (words, result.returncode, result.stdout, result.stderr)
        return result.stdout.strip()

    def control(title, *words):
        return probed(title, *words)

    def listed(output, number):
        """What a new paw_workspace_v1 lists first, as the probe prints it."""
        return probed("--workspace", output, str(number))

    ids = {}

    def launch(title, **env):
        process = desktop.open_window(probe, title, f"app-{title}", focused=True, env=env)
        ids[title] = int(control(title, "id"))
        return process

    def entry(title, grow=0):
        """The probe's word for the window: its number and its box from `get windows`, from its
        output's corner, grown by `grow` on every side."""
        w = windows()[title]
        ox, oy = outputs()[w.output][:2]
        return (f"{ids[title]}:{w.x - ox - grow},{w.y - oy - grow},"
                f"{w.width + 2 * grow}x{w.height + 2 * grow}")

    def expected(output, *entries):
        width, height = outputs()[output][2:]
        return " ".join([f"{width}x{height}", *entries])

    launch("A")
    launch("B")
    assert listed("HEADLESS-1", 1) == expected("HEADLESS-1", entry("A"), entry("B")), \
        listed("HEADLESS-1", 1)
    # Nothing on the other workspaces; none past the last, nor on an output not connected.
    assert listed("HEADLESS-1", 2) == "1280x720", listed("HEADLESS-1", 2)
    assert listed("HEADLESS-1", 5) == "1280x720", listed("HEADLESS-1", 5)
    assert listed("HDMI-A-9", 1) == "0x0", listed("HDMI-A-9", 1)

    # A probe watching the first workspace hears each new list, and only a new one.
    watch = desktop.spawn([window_probe, "--workspace", "HEADLESS-1", "1", "watch"],
                          stdout=subprocess.PIPE, text=True)
    lines = queue.Queue()
    threading.Thread(target=lambda: [lines.put(line.strip()) for line in watch.stdout],
                     daemon=True).start()
    heard = []

    def latest():
        while not lines.empty():
            heard.append(lines.get())
        return heard[-1] if heard else None

    def hears(text, message):
        desktop.wait_for(lambda: latest() == text, message,
                         detail=lambda: f"heard {heard[-3:]}, windows: {windows()}")

    hears(expected("HEADLESS-1", entry("A"), entry("B")), "the first list")
    desktop.wait_for(lambda: desktop.rows("workspace_views") == [["HEADLESS-1", "1", "2"]],
                     "the object in get workspace_views")
    count = len(heard)
    desktop.stays(lambda: latest() is not None and len(heard) == count,
                  "nothing heard while nothing changes")

    # Raised, A comes over B.
    control("A", "restore")
    desktop.wait_for(lambda: windows()["A"].focused, "A focused")
    hears(expected("HEADLESS-1", entry("B"), entry("A")), "A raised over B")

    # Moved by the keyboard, A is listed where it rests.
    before = windows()["A"].box
    desktop.msg("move_right")
    desktop.wait_for(lambda: windows()["A"].x > before[0], "A moved")
    hears(expected("HEADLESS-1", entry("B"), entry("A")), "A listed where it moved")

    # To another workspace: it leaves this one's list for that one's.
    control("B", "workspace", "2")
    hears(expected("HEADLESS-1", entry("A")), "B gone to workspace 2")
    assert listed("HEADLESS-1", 2) == expected("HEADLESS-1", entry("B")), listed("HEADLESS-1", 2)

    # Sticky, a window shows on every workspace of its output, over the others it was raised
    # above.
    launch("C")
    hears(expected("HEADLESS-1", entry("A"), entry("C")), "C opened")
    control("C", "sticky", "1")
    desktop.wait_for(lambda: windows()["C"].sticky, "C sticky")
    assert listed("HEADLESS-1", 2) == expected("HEADLESS-1", entry("B"), entry("C")), \
        listed("HEADLESS-1", 2)
    hears(expected("HEADLESS-1", entry("A"), entry("C")), "C sticky on workspace 1 too")

    # Minimized, a window shows on none.
    control("A", "minimize")
    desktop.wait_for(lambda: windows()["A"].minimized, "A minimized")
    hears(expected("HEADLESS-1", entry("C")), "A minimized")
    control("A", "restore")
    desktop.wait_for(lambda: not windows()["A"].minimized, "A restored")
    hears(expected("HEADLESS-1", entry("C"), entry("A")), "A restored, on top")

    # Fullscreen, a window covers the output, over the others.
    first = outputs()["HEADLESS-1"]
    desktop.msg("fullscreen")
    desktop.wait_for(lambda: windows()["A"].box == first, "A fullscreen")
    hears(expected("HEADLESS-1", entry("C"), "%d:0,0,1280x720" % ids["A"]), "A fullscreen on top")
    desktop.msg("fullscreen")
    desktop.wait_for(lambda: windows()["A"].box != first, "A out of fullscreen")

    # A window that draws a shadow around itself is listed with it: its picture covers it.
    d = launch("D", PAW_PROBE_SHADOW=str(SHADOW))
    hears(expected("HEADLESS-1", entry("C"), entry("A"), entry("D", SHADOW)),
          "D listed with its shadow")

    # Closing, a window leaves the list.
    desktop.msg("close")
    assert desktop.reap(d) == 0
    desktop.wait_for(lambda: "D" not in windows(), "D closed")
    hears(expected("HEADLESS-1", entry("C"), entry("A")), "D gone")

    # A window on a workspace not shown that draws itself at another size, unasked, is listed at
    # it, though no output shows it and nothing takes its picture.
    third = desktop.spawn([window_probe, "--workspace", "HEADLESS-1", "3", "watch"],
                          stdout=subprocess.PIPE, text=True)
    third_lines = queue.Queue()
    threading.Thread(target=lambda: [third_lines.put(line.strip()) for line in third.stdout],
                     daemon=True).start()
    third_heard = []

    def third_listed(title):
        """The rectangle the third workspace's watcher last heard for the window, or None."""
        while not third_lines.empty():
            third_heard.append(third_lines.get())
        words = third_heard[-1].split()[1:] if third_heard else []
        return next((word for word in words if word.startswith(f"{ids[title]}:")), None)

    desktop.msg("workspace", "3")
    e = desktop.open_window(probe, "E", "app-E", args=("--commands",), stdin=subprocess.PIPE,
                            text=True)
    ids["E"] = int(control("E", "id"))
    desktop.msg("workspace", "1")
    desktop.wait_for(lambda: not windows()["E"].visible, "E out of sight")
    desktop.wait_for(lambda: third_listed("E") == entry("E"), "E listed on workspace 3",
                     detail=lambda: f"heard {third_heard[-2:]}, windows: {windows()}")
    e.stdin.write("draw 400 300\n")
    e.stdin.flush()
    desktop.wait_for(lambda: windows()["E"].box[2:] == (400, 300), "E drawn at 400 by 300")
    desktop.wait_for(lambda: third_listed("E") == entry("E"), "E listed at its new size",
                     detail=lambda: f"heard {third_heard[-2:]}, windows: {windows()}")
    third.terminate()
    assert desktop.reap(third) in (0, -15)

    # On the second output, at a scale of 2, windows are placed from its corner, in logical
    # pixels; it tiles, and they are listed where the tiling put them.
    control("A", "output", "HEADLESS-2")
    desktop.wait_for(lambda: windows()["A"].output == "HEADLESS-2", "A on HEADLESS-2")
    hears(expected("HEADLESS-1", entry("C")), "A gone to the other output")
    assert outputs()["HEADLESS-2"][2:] == (640, 360), outputs()
    desktop.wait_for(lambda: windows()["A"].tiled, "A tiled on HEADLESS-2")
    shown = windows()["A"].workspace
    desktop.wait_for(lambda: listed("HEADLESS-2", shown) == expected("HEADLESS-2", entry("A")),
                     "A listed on HEADLESS-2 from its corner",
                     detail=lambda: f"{listed('HEADLESS-2', shown)}, windows: {windows()}")

    # The object goes with its client, and with it what it costs.
    watch.terminate()
    assert desktop.reap(watch) in (0, -15)
    desktop.wait_for(lambda: not desktop.rows("workspace_views"), "no object left")
print("workspace views: the windows each workspace shows, bottom to top, where their pictures "
      "lie, listed again as they change")
