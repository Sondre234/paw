# SPDX-License-Identifier: GPL-3.0-or-later
"""paw showcase: the windows' arrangement saved, a terminal opened with paw fetch in it, the monitor
taken with the cursor hidden, then the terminal closed and the arrangement restored; --keep leaves
the terminal, and a failure or Ctrl+C puts everything back. A stand-in terminal opens a
wayland_probe window and runs what it is given; a stand-in grim notes whether the cursor shows
and runs the real one where it is installed."""
import os
from pathlib import Path
import re
import signal
import struct
import subprocess
import sys

import harness

compositor, probe, example = (str(Path(p).resolve()) for p in sys.argv[1:4])
grim = sys.argv[4] if len(sys.argv) > 4 else ""

source = (Path(example).read_text().replace("xwayland = true", "xwayland = false")
          .replace("tiling = false,", "tiling = true,", 1))

TOOLS = {
    # Records its process (the group paw showcase starts it in), opens a window titled as paw
    # fetch's terminal is, and once it shows runs what follows -e with the output in
    # $SHOWCASE_LOG, closing the window as that ends.
    "term": 'echo "$$" >> "$SHOWCASE_LOG.pids"; [ "$1" = -e ] && shift; '
            'PAW_PROBE_TITLE="paw fetch" "$PROBE" --window-only & window=$!; '
            'until "$PAW" msg get windows | grep -q "paw fetch"; do sleep 0.05; done; '
            '"$@" > "$SHOWCASE_LOG" 2>&1; kill "$window"',
    "grim": '"$PAW" msg get cursor >> "$SHOWCASE_LOG.cursor"; echo "grim $*" >> "$SHOWCASE_LOG.grim"; '
            '[ -n "$REAL_GRIM" ] && exec "$REAL_GRIM" "$@"; '
            'for last; do :; done; printf "fake png" > "$last"',
    # The user's shell, which --keep hands the terminal to: it holds the window open.
    "hold": "exec sleep 600",
}


def png_size(path):
    """The width and height in a PNG's header."""
    data = Path(path).read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", data[:16]
    return struct.unpack(">II", data[16:24])


with harness.Compositor(compositor, source) as desktop:
    msg, root = desktop.msg, desktop.root
    tools = root / "bin"
    tools.mkdir()
    for name, body in TOOLS.items():
        (tools / name).write_text("#!/bin/sh\n" + body + "\n")
        (tools / name).chmod(0o755)
    log, pictures = root / "showcase.log", root / "pictures"
    pids, cursor, grims = (Path(f"{log}.{kind}") for kind in ("pids", "cursor", "grim"))
    desktop.logs += [log, grims]
    env = desktop.env | {"PATH": f"{tools}:{os.environ['PATH']}", "SHOWCASE_LOG": str(log),
                         "PROBE": probe, "PAW": compositor, "REAL_GRIM": grim,
                         "XDG_PICTURES_DIR": str(pictures), "SHELL": str(tools / "hold")}
    showcase = [compositor, "showcase", "--terminal", str(tools / "term"), "--delay", "0"]

    def run(*words, environment=env):
        return subprocess.run(showcase + list(words), env=environment, capture_output=True,
                              text=True, timeout=60)

    def arrangement():
        return sorted((w.title, w.box, w.focused, w.workspace) for w in desktop.windows())

    def sessions():
        return [line.split("\t")[0] for line in msg("session", "list").splitlines()]

    def tiles():
        return sorted((w.title, w.box) for w in desktop.windows())

    def end_terminals():
        """Ends every stand-in terminal with what it runs."""
        for pid in pids.read_text().split() if pids.exists() else []:
            try:
                os.killpg(int(pid), signal.SIGTERM)
            except ProcessLookupError:
                pass
        pids.unlink(missing_ok=True)

    try:
        desktop.open_window(probe, "left")
        desktop.open_window(probe, "right", focused=True)
        desktop.wait_for(lambda: all(w.tiled for w in desktop.windows()), "two tiles")
        before = arrangement()

        # The picture in the pictures directory, paw fetch in the terminal with the windows it
        # saw, the cursor hidden meanwhile, and everything as it was after.
        done = run()
        assert done.returncode == 0, done.stderr
        (picture,) = pictures.iterdir()
        assert done.stdout == f"{picture}\n", done.stdout
        assert re.fullmatch(r"paw-showcase-\d{4}-\d\d-\d\d-\d{6}\.png", picture.name), picture
        if grim:
            assert png_size(picture) == (1280, 720), png_size(picture)
        assert grims.read_text() == f"grim -t png -o HEADLESS-1 {picture}.part\n", \
            grims.read_text()
        assert cursor.read_text() == "hidden\n", cursor.read_text()
        assert msg("get", "cursor") == "shown\n"
        fetched = log.read_text()
        assert "paw " in fetched and "3 on 1 workspace" in fetched, fetched
        assert arrangement() == before, (arrangement(), before)
        assert sessions() == [], sessions()
        assert not list(Path(desktop.env["XDG_RUNTIME_DIR"]).glob("paw-showcase-*"))

        # A file and a monitor of one's own.
        msg("headless_output", "add", "DP-2", "1920x1080")
        desktop.wait_for(lambda: len(desktop.rows("outputs")) == 2, "a second monitor")
        before = arrangement()
        grims.unlink()
        target = root / "shots/mine.png"
        done = run("--output", "DP-2", "-o", str(target))
        assert done.returncode == 0 and done.stdout == f"{target}\n", done
        assert grims.read_text() == f"grim -t png -o DP-2 {target}.part\n", grims.read_text()
        if grim:
            assert png_size(target) == (1920, 1080), png_size(target)
        assert arrangement() == before, (arrangement(), before)
        bad = run("--output", "DP-9")
        assert bad.returncode == 1 and "no monitor DP-9" in bad.stderr, bad

        # --keep leaves the terminal and its tiles as they are.
        done = run("--keep")
        assert done.returncode == 0, done.stderr
        assert "paw fetch" in [w.title for w in desktop.windows()]
        desktop.stays(lambda: "paw fetch" in [w.title for w in desktop.windows()],
                      "the terminal kept")
        assert sessions() == [], sessions()
        kept = [w.box for w in desktop.windows() if w.title == "right"]
        end_terminals()
        desktop.wait_for(lambda: tiles() == [(title, box) for title, box, *_ in before],
                         "the kept terminal closed", detail=tiles)
        assert kept != [w.box for w in desktop.windows() if w.title == "right"]
        before = arrangement()

        # Ctrl+C while it waits puts everything back, and leaves no picture.
        shots = set(pictures.iterdir())
        log.unlink()
        process = subprocess.Popen(showcase[:-1] + ["30"], env=env, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True)
        desktop.wait_for(lambda: "paw fetch" in [w.title for w in desktop.windows()],
                         "the terminal open")
        desktop.wait_for(lambda: log.exists() and "windows" in log.read_text(), "paw fetch shown")
        assert sessions() == [f"showcase-{process.pid}"], sessions()
        process.send_signal(signal.SIGINT)
        out, err = process.communicate(timeout=30)
        assert process.returncode == 130 and "interrupted" in err and not out, (out, err)
        assert arrangement() == before, (arrangement(), before, err)
        assert sessions() == [] and set(pictures.iterdir()) == shots

        # What changed meanwhile beyond the terminal comes back from the saved session: the
        # tiling turned off, here.
        log.unlink()
        process = subprocess.Popen(showcase[:-1] + ["30"], env=env, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE, text=True)
        desktop.wait_for(lambda: log.exists() and "windows" in log.read_text(), "paw fetch shown")
        msg("toggle_tiling")
        desktop.wait_for(lambda: not any(w.tiled for w in desktop.windows()), "the tiles floated")
        process.send_signal(signal.SIGINT)
        out, err = process.communicate(timeout=30)
        assert process.returncode == 130, (out, err)
        assert msg("get", "tiling") == "on\n" and all(w.tiled for w in desktop.windows())
        assert sorted(box for _, box, *_ in arrangement()) == sorted(box for _, box, *_ in before)
        assert sessions() == []
        before = arrangement()

        # A terminal that fails, and a missing grim, change nothing either.
        done = subprocess.run([compositor, "showcase", "--terminal", "false"], env=env,
                              capture_output=True, text=True, timeout=60)
        assert done.returncode == 1 and "false failed to start" in done.stderr, done
        assert arrangement() == before and sessions() == []
        empty = root / "empty"
        empty.mkdir()
        done = run(environment=env | {"PATH": str(empty)})
        assert done.returncode == 1 and "grim, which is not installed" in done.stderr, done
        outside = {name: value for name, value in env.items() if name != "PAW_SOCKET"}
        done = run(environment=outside)
        assert done.returncode == 1 and "PAW_SOCKET is not set" in done.stderr, done
        assert set(pictures.iterdir()) == shots and arrangement() == before
    finally:
        end_terminals()
