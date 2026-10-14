# SPDX-License-Identifier: GPL-3.0-or-later
"""What paw top reads: `get commits`, each window's and layer surface's commits and the time
handling them took, and `get frame_times OUTPUT`, a monitor's own last frames; with a window
that draws on every frame beside one that is still."""
from pathlib import Path
import sys

import harness

compositor, probe, client = (str(Path(p).resolve()) for p in sys.argv[1:4])

CONFIG = """return {
    xwayland = false,
    animations = { enabled = false },
    -- Frames come four times as often as at 60 Hz, so the test fills an output's 512 sooner.
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720@240" } } },
}"""

with harness.Compositor(compositor, CONFIG) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def commits():
        """By app ID or namespace: kind, number, process, commits, time (ns), output,
        workspace, title."""
        return {row[7]: row[:7] + row[8:] for row in desktop.rows("commits")}

    def count(name):
        return int(commits()[name][3])

    desktop.detail = lambda: f"commits: {commits()}"

    # A window with a panel, which commit as they open and then only when told something.
    still = desktop.open_window(probe, "Still", "still", args=("--external-control",))
    busy = desktop.spawn([client, "--animate", "--app-id", "busy", "--title", "Busy"])
    wait_for(lambda: "busy" in commits(), "the busy window is listed")
    first = count("busy")
    wait_for(lambda: count("busy") >= first + 30, "the busy window commits on every frame")
    quiet = count("still")
    desktop.stays(lambda: count("still") == quiet, "the still window makes no commits",
                  duration=.5)
    rows = commits()
    assert rows["busy"][:3] == ["window", rows["busy"][1], str(busy.pid)], rows
    assert rows["still"][:3] == ["window", rows["still"][1], str(still.pid)], rows
    assert rows["busy"][1] != rows["still"][1], "each window has its own number"
    assert int(rows["busy"][4]) > 0, "handling the busy window's commits takes time"
    assert rows["busy"][5:] == ["HEADLESS-1", "1", "Busy"], rows
    assert rows["still"][5:] == ["HEADLESS-1", "1", "Still"], rows
    panel = rows["paw-test-panel"]
    assert panel[0] == "layer" and panel[2] == str(still.pid), panel
    assert int(panel[3]) >= 1 and panel[5:] == ["HEADLESS-1", "0", ""], panel
    # Windows first, oldest first, then the layer surfaces.
    assert [row[7] for row in desktop.rows("commits")] == ["still", "busy", "paw-test-panel"]

    # A monitor's own frames: how many it has drawn, then the last of them, spent and interval
    # in microseconds.
    def frame_times(output):
        lines = msg("get", "frame_times", output).splitlines()
        return int(lines[0]), [tuple(map(int, line.split("\t"))) for line in lines[1:]]

    total, frames = frame_times("HEADLESS-1")
    assert len(frames) == min(total, 512), (total, len(frames))
    wait_for(lambda: frame_times("HEADLESS-1")[0] >= 600, "the busy window draws 600 frames",
             timeout=40)
    total, frames = frame_times("HEADLESS-1")
    assert len(frames) == 512, "an output keeps its last 512 frames"
    assert all(interval > 0 for _, interval in frames), "each frame follows another"
    msg("headless_output", "add", "DP-9", "800x600")
    wait_for(lambda: frame_times("DP-9")[0] >= 1, "the new monitor draws a frame")
    total, frames = frame_times("DP-9")
    assert len(frames) == total and frames[0][1] == 0, "its first frame follows none of its own"
    assert "no output" in msg("get", "frame_times", "NOPE", ok=False)
    assert len(msg("get", "frame_times").splitlines()) > 512, "every output's frames together"
print("top_smoke passed")
