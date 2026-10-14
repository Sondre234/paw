# SPDX-License-Identifier: GPL-3.0-or-later
"""paw top and what it reads: `get commits`, each window's and layer surface's commits and the
time handling them took, and `get frame_times OUTPUT`, a monitor's own last frames; with a
window that draws on every frame beside one that is still. `paw top --once` prints one look, and
on a terminal paw top draws on a screen of its own until a key or a signal ends it."""
import fcntl
import os
from pathlib import Path
import pty
import re
import select
import signal
import struct
import subprocess
import sys
import termios

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

    # One look, plain where it does not print to a terminal, as wide as COLUMNS says.
    result = subprocess.run([compositor, "top", "--once", "--interval", "300"],
                            env={**desktop.env, "COLUMNS": "100"}, capture_output=True,
                            text=True, timeout=30)
    assert result.returncode == 0, result.stderr
    lines = result.stdout.splitlines()
    assert re.fullmatch(r"paw \S.* · up 0:00:\d\d · 2 monitors", lines[0]), lines[0]
    assert not any("\x1b" in line or len(line) > 100 for line in lines), lines
    monitor = next(line for line in lines if line.startswith("HEADLESS-1"))
    assert re.search(r"1280x720 240Hz  \d+ fps  avg \S+  p99 \S+  worst \S+  budget 4.17ms",
                     monitor), monitor
    assert int(re.search(r"(\d+) fps", monitor)[1]) >= 20, monitor
    assert next(line for line in lines if line.startswith("DP-9")).startswith(
        "DP-9 800x600  "), lines
    assert any(line.startswith("Activity  frames ") for line in lines), lines
    head = next(i for i, line in enumerate(lines) if line.startswith("COMMITS/s"))
    assert lines[head].split() == ["COMMITS/s", "TIME/s", "EACH", "WS", "APP", "ID", "TITLE"]
    table = [line.split() for line in lines[head + 1:]]
    assert table[0][3:] == ["1", "busy", "Busy"] and float(table[0][0]) >= 20, table
    assert table[1] == ["0", "0", "-", "-", "paw-test-panel", "layer", "surface", "on",
                        "HEADLESS-1"], table
    assert table[2] == ["0", "0", "-", "1", "still", "Still"], table
    assert len(table) == 3, table
    for arguments, error in ((["--interval", "x"], "milliseconds"), (["--bogus"], "usage")):
        result = subprocess.run([compositor, "top", *arguments], env=desktop.env,
                                capture_output=True, text=True, timeout=30)
        assert result.returncode == 1 and error in result.stderr, result

    # Live on a terminal: a screen of its own, s to sort, and the terminal back as it was after
    # q, Escape or a signal.
    def live(end):
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
        # The terminal is its controlling one, as in a session, for Ctrl+C to be a signal.
        process = desktop.spawn([compositor, "top", "--interval", "100"], stdin=slave,
                                stdout=slave, stderr=slave, start_new_session=True,
                                preexec_fn=lambda: fcntl.ioctl(0, termios.TIOCSCTTY, 0))
        os.close(slave)
        shown = bytearray()

        def drawn(text):
            while select.select([master], [], [], 0)[0]:
                try:
                    shown.extend(os.read(master, 65536))
                except OSError:  # the other end is closed
                    break
            return text in shown

        wait_for(lambda: drawn(b"\x1b[?1049h") and drawn(b"COMMITS/s") and drawn(b"busy"),
                 "paw top draws on a screen of its own")
        end(process, master, drawn)
        assert desktop.reap(process) == 0
        drawn(b"")
        os.close(master)
        assert shown.endswith(b"\x1b[?7h\x1b[?25h\x1b[?1049l"), shown[-80:]

    def sort_then_quit(process, master, drawn):
        os.write(master, b"s")
        wait_for(lambda: drawn(b"s sort: commits"), "s sorts the windows by their commits")
        os.write(master, b"p")
        wait_for(lambda: drawn(b"p resume"), "p pauses")
        os.write(master, b"q")

    live(sort_then_quit)
    live(lambda process, master, drawn: os.write(master, b"\x1b"))
    live(lambda process, master, drawn: process.terminate())

    # A compositor that does not answer: paw top says so, and Ctrl+C still ends it at once.
    def hung_then_interrupt(process, master, drawn):
        desktop.server.send_signal(signal.SIGSTOP)
        try:
            wait_for(lambda: drawn(b"no reply"), "paw top says the compositor does not answer")
            os.write(master, b"\x03")
            process.wait(timeout=1)
        finally:
            desktop.server.send_signal(signal.SIGCONT)

    live(hung_then_interrupt)
print("top_smoke passed")
