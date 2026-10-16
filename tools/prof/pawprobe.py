#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds pawprobe.so and starts compositors under it, for soak.py and featcost.py.

usage: tools/prof/pawprobe.py BUILD_DIR

builds BUILD_DIR/pawprobe.so from tools/prof/pawprobe.c if it is missing or older than its
source (outside CMake: nothing in the build or the tests needs it) and prints its path, for
LD_PRELOAD by hand.
"""
from contextlib import contextmanager
import os
from pathlib import Path
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(ROOT / "tests"))
import harness  # noqa: E402  (tests/harness.py: a headless compositor of its own)

# A compositor for a soak or a measurement: one 1280x720 output, no XWayland, and profiles to
# switch between, among them one with the blur and translucent windows.
CONFIG = """return {
    xwayland = false,
    idle = { display_off = 0 },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    layout = { tiling = false, workspaces = 4 },
    windows = { border_width = 2, shadow = { enabled = true } },
    heatmap = { enabled = true },
    profile = "plain",
    profiles = {
        plain = {},
        glass = { windows = { opacity = 0.9, inactive_opacity = 0.8, blur = { enabled = true } } },
        ink = { appearance = { background = "#202030" },
                windows = { border_color = "#704214", shadow = { enabled = false } } },
    },
}"""


def build(build_dir):
    """BUILD_DIR/pawprobe.so, compiled first if it is missing or older than its source."""
    source, target = HERE / "pawprobe.c", Path(build_dir).resolve() / "pawprobe.so"
    if not target.exists() or target.stat().st_mtime < source.stat().st_mtime:
        subprocess.run([os.environ.get("CC", "cc"), "-O2", "-g", "-shared", "-fPIC", "-pthread",
                        "-Wall", "-Wextra", "-o", str(target), str(source)], check=True)
    return target


def sanitized(binary):
    """Whether `binary` is an ASan build, under which the probe cannot load."""
    return b"libasan.so" in Path(binary).read_bytes()


def environment(library, directory, programs=("paw",), hz=None, log_ms=None):
    """What preloads the probe into `programs`, writing to `directory`; `paw msg` is left
    alone."""
    env = {"LD_PRELOAD": str(library), "PAWPROBE_DIR": str(directory),
           "PAWPROBE_ONLY": ",".join(programs), "PAWPROBE_SKIP_ARG1": "msg"}
    if hz is not None:
        env["PAWPROBE_HZ"] = str(hz)
    if log_ms is not None:
        env["PAWPROBE_LOG_MS"] = str(log_ms)
    return env


class Marks:
    """Named spans of CLOCK_MONOTONIC time, "NAME\\tSTART\\tEND" a line, which report.py's
    --mark and heaptrend.py read."""

    def __init__(self, path):
        self.path = Path(path)
        self.path.write_text("# name\tstart\tend (CLOCK_MONOTONIC seconds)\n")

    @contextmanager
    def span(self, name):
        start = time.monotonic()
        try:
            yield
        finally:
            with self.path.open("a") as out:
                out.write(f"{name}\t{start:.6f}\t{time.monotonic():.6f}\n")


def settled(desktop):
    """Whether nothing animates."""
    return desktop.msg("get", "animations").split("\t")[0].strip() == "0"


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__.strip())
    print(build(sys.argv[1]))
