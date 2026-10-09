# SPDX-License-Identifier: GPL-3.0-or-later
"""Capturing one window, as the taskbar's pictures do through paw-window-control-v1's
get_capture_source and get_scaled_capture_source and screen sharing through
ext-foreign-toplevel-list: a session on the window's capture source copies the window's own
surfaces at its size, without the border or shadow paw draws around it, also while it is
minimized or on a workspace not shown; it follows the window as it redraws and stops as it
closes. A scaled source's frames fit the size asked for, keep the window's aspect ratio and are
never larger than the window, follow it as it changes size, come only as it draws, and come at
once to a second session on the same source. While the session is locked, or once the window is
gone, a source is inert, its session stopped at once, and the client asking stays connected."""
import math
from pathlib import Path
import queue
import subprocess
import sys
import threading
import time

import harness

compositor, probe, window_probe, lock_probe = (str(Path(p).resolve()) for p in sys.argv[1:5])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false, workspaces = 4 },
    windows = { border_width = 4, shadow = { enabled = true } },
}"""

# What wayland_probe draws: a dark band along its top, the rest blue.
BAND, BODY = "23314a", "417bc4"
# The ways to a window's capture source: the taskbar's, by its wlr-foreign-toplevel handle, at
# full size or scaled within a box larger than the window, and screen sharing's, by its
# ext-foreign-toplevel-list handle.
PATHS = (("capture",), ("capture-scaled", "4096", "4096"), ("capture-listed",))

with harness.Compositor(compositor, CONFIG) as desktop:
    def windows():
        """title -> (width, height, minimized, visible, focused)."""
        return {r[9]: (int(r[6]), int(r[7]), r[2] == "1", r[11] == "1", r[1] == "1")
                for r in desktop.rows("windows")}

    desktop.detail = lambda: f"windows: {windows()}"

    def run(title, *words):
        """What the window probe prints for the window titled `title`; it must exit cleanly, its
        connection sound to its last roundtrip."""
        result = subprocess.run([window_probe, title, *words], env=desktop.env,
                                capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, (words, result.stdout, result.stderr)
        return result.stdout.strip()

    def picture(width, height):
        """The probe's line for a frame of the probe's window at that size."""
        return f"{width}x{height} {BAND} {BODY}"

    def captures(title, expected):
        """Capturing the window every way prints `expected`."""
        for path in PATHS:
            printed = run(title, *path)
            assert printed == expected, (path, printed)

    def fitted(width, height, box_width, box_height):
        """The size of a scaled frame of a window of width x height within the box: scaled down
        to fit, never up, keeping the aspect ratio."""
        fit = min(1, box_width / width, box_height / height)
        return max(1, math.floor(width * fit + .5)), max(1, math.floor(height * fit + .5))

    a = desktop.spawn([probe, "--window-only"],
                      env={"PAW_PROBE_TITLE": "A", "PAW_PROBE_APP_ID": "app-A"})
    desktop.wait_for(lambda: "A" in windows(), "A mapped")
    width, height = windows()["A"][:2]

    # The window as it draws itself: its size, its band at the top-left corner where the border
    # would be, and its body in the middle.
    captures("A", picture(width, height))

    # Scaled, its frame fits the box the width or the height of it, keeps the window's aspect
    # ratio, and is never larger than the window: smaller than half its size (taking halving
    # steps), smaller than that, as large, and larger. Its colours stay where they were.
    for box in ((160, 100), (40, 40), (width, 10), (width, height), (1000, 1000)):
        size = fitted(width, height, *box)
        assert size[0] <= box[0] and size[1] <= box[1], (box, size)
        assert abs(size[0] / size[1] - width / height) < 2 / min(size), (box, size)
        printed = run("A", "capture-scaled", *map(str, box))
        assert printed == picture(*size), (box, printed)

    # A second session on the same source gets its first frame at once, though the window does
    # not draw again.
    result = subprocess.run([window_probe, "A", "capture-scaled", "160", "100", "twice"],
                            env=desktop.env, capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result
    assert result.stdout.split("\n")[:2] == [picture(*fitted(width, height, 160, 100))] * 2, \
        result.stdout

    # A box with no width or height is a protocol error, for that client only.
    result = subprocess.run([window_probe, "A", "capture-scaled", "0", "100"], env=desktop.env,
                            capture_output=True, text=True, timeout=30)
    assert result.returncode != 0, result

    # Each of those sources went once its client let go of it.
    desktop.wait_for(lambda: not desktop.rows("pictures"), "no scaled source left")

    # Minimized, and on a workspace not shown, it is still there to capture.
    run("A", "minimize")
    desktop.wait_for(lambda: windows()["A"][2], "A minimized")
    captures("A", picture(width, height))
    subprocess.run([probe, "--activate", "app-A"], env=desktop.env, check=True, timeout=30,
                   stdout=subprocess.DEVNULL)
    desktop.wait_for(lambda: not windows()["A"][2], "A restored from the taskbar")
    run("A", "workspace", "2")
    desktop.wait_for(lambda: not windows()["A"][3], "A on workspace 2")
    captures("A", picture(width, height))

    # Locked, the window is not to be seen: the source is inert, its session stops at once, and
    # the probe goes on to its last roundtrip without a protocol error.
    calls = desktop.root / "locker.log"
    locker = desktop.spawn([lock_probe, "hold", str(calls)])
    desktop.wait_for(lambda: "Session locked" in desktop.log.read_text(), "the session locked")
    captures("A", "stopped")
    locker.terminate()
    assert desktop.reap(locker) == 0
    desktop.wait_for(lambda: "Session unlocked" in desktop.log.read_text(),
                     "the session unlocked")
    captures("A", picture(width, height))

    # A session follows the window, a frame each time it redraws, at its new size when that
    # changes. The watching probe prints a line for each frame.
    subprocess.run([probe, "--activate", "app-A"], env=desktop.env, check=True, timeout=30,
                   stdout=subprocess.DEVNULL)
    desktop.wait_for(lambda: windows()["A"][3:] == (True, True), "A focused, and shown")
    watch = desktop.spawn([window_probe, "A", "capture", "watch"], stdout=subprocess.PIPE,
                          text=True)
    lines = queue.Queue()
    threading.Thread(target=lambda: [lines.put(line.strip()) for line in watch.stdout],
                     daemon=True).start()

    def heard(expected, lines=lines):
        """Waits for a watching probe to print `expected`, past the frames before it."""
        deadline, seen = time.monotonic() + 15, []
        while time.monotonic() < deadline:
            try:
                seen.append(lines.get(timeout=.1))
            except queue.Empty:
                continue
            if seen[-1] == expected:
                return
        raise AssertionError(f"the watching probe printed {seen}, not {expected!r}")

    heard(picture(width, height))
    desktop.msg("maximize")
    desktop.wait_for(lambda: windows()["A"][:2] != (width, height), "A maximized")
    heard(picture(*windows()["A"][:2]))
    desktop.msg("restore")
    desktop.wait_for(lambda: windows()["A"][:2] == (width, height), "A restored")
    heard(picture(width, height))

    # So does a scaled session, a frame fitting the box at each size, and none while the window
    # does not draw.
    scaled = desktop.spawn([window_probe, "A", "capture-scaled", "160", "100", "watch"],
                           stdout=subprocess.PIPE, text=True)
    scaled_lines = queue.Queue()
    threading.Thread(target=lambda: [scaled_lines.put(line.strip()) for line in scaled.stdout],
                     daemon=True).start()
    heard(picture(*fitted(width, height, 160, 100)), scaled_lines)
    desktop.stays(scaled_lines.empty, "no frame while the window does not draw", duration=.5)
    assert desktop.rows("pictures") == [["160x100", "x".join(map(str, fitted(width, height, 160,
                                                                             100))), "1", "A"]]
    desktop.msg("maximize")
    desktop.wait_for(lambda: windows()["A"][:2] != (width, height), "A maximized")
    heard(picture(*fitted(*windows()["A"][:2], 160, 100)), scaled_lines)
    desktop.msg("restore")
    desktop.wait_for(lambda: windows()["A"][:2] == (width, height), "A restored")
    heard(picture(*fitted(width, height, 160, 100)), scaled_lines)

    # A source asked for once the window is gone is inert.
    late = desktop.spawn([window_probe, "A", "capture-scaled", "160", "100", "closed"],
                         stdout=subprocess.PIPE, text=True)
    assert late.stdout.readline().strip(), "the probe found no window"

    # Closing the window stops the sessions, and the watching probes exit cleanly.
    subprocess.run([probe, "--close", "app-A"], env=desktop.env, check=True, timeout=30,
                   stdout=subprocess.DEVNULL)
    assert desktop.reap(a) == 0
    heard("stopped")
    assert desktop.reap(watch) == 0
    heard("stopped", scaled_lines)
    assert desktop.reap(scaled) == 0
    assert late.stdout.read().strip() == "stopped"
    assert desktop.reap(late) == 0
print("A window's capture shows its own surfaces as they change, minimized or out of sight too, "
      "scaled to fit a size when asked, stops as it closes, and is inert while the session is "
      "locked or once the window is gone")
