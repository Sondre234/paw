#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Measure paw-shell startup and idle cost against a private headless compositor.

usage: tools/shell_perf.py BUILD_DIR [--runs N] [--idle SECONDS] [--renderer gpu|software]
                           [--private-bus]

Reports, as medians over the runs: time from spawning the shell to its first rendered frame
per surface, resident and proportional memory, mapped libraries, threads, and while idle the
CPU time and context switches per second (a context switch is what a wakeup costs). Uses
only /proc, so it needs no perf or strace. --renderer picks how the shell draws, as
shell.renderer does (the GPU by default; here Mesa's software OpenGL, since the private
compositor has no GPU buffers), and the environment is the one the compositor gives a shell
drawing that way; --env QSG_RENDER_LOOP=... and the like compare other rendering choices. The
shell never sees the real session bus: without --private-bus it has none, so it serves no
notifications; with it, a dbus-daemon of its own (killed afterwards) carries the shell's
notification service, as in a session.
"""
import argparse
import os
from pathlib import Path
import re
import signal
import statistics
import subprocess
import sys
import tempfile
import time

TICKS = os.sysconf("SC_CLK_TCK")


def proc_stat(pid):
    """(cpu seconds, {thread: context switches}, threads) of the process."""
    fields = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
    cpu = (int(fields[11]) + int(fields[12])) / TICKS
    switches = {}
    for task in Path(f"/proc/{pid}/task").iterdir():
        try:
            for line in (task / "status").read_text().splitlines():
                if "ctxt_switches" in line:
                    switches[task.name] = switches.get(task.name, 0) + int(line.split()[1])
        except (FileNotFoundError, ProcessLookupError):
            pass  # the thread ended meanwhile
    return cpu, switches, int(fields[17])


def memory(pid):
    rss = pss = 0
    for line in Path(f"/proc/{pid}/smaps_rollup").read_text().splitlines():
        if line.startswith("Rss:"):
            rss = int(line.split()[1])
        elif line.startswith("Pss:"):
            pss = int(line.split()[1])
    return rss / 1024, pss / 1024


def libraries(pid):
    return len({m.split()[-1] for m in Path(f"/proc/{pid}/maps").read_text().splitlines()
                if m.split()[-1].startswith("/") and ".so" in m.split()[-1]})


def wait_for_text(path, text, process, timeout=20):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise SystemExit(f"process exited early: {process.returncode}")
        if text in path.read_text():
            return time.monotonic()
        time.sleep(.002)
    raise SystemExit(f"timed out waiting for {text!r}")


def run(build, idle, extra_env, renderer, private_bus=False):
    compositor, shell = build / "paw", build / "paw-shell"
    example = Path(__file__).resolve().parent.parent / "config" / "init.lua"
    with tempfile.TemporaryDirectory(prefix="paw-perf-") as directory:
        root = Path(directory)
        env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman",
                   QT_QPA_PLATFORM="wayland", QT_FORCE_STDERR_LOGGING="1",
                   XDG_DATA_HOME=directory, XDG_STATE_HOME=directory, XDG_CACHE_HOME=directory,
                   DBUS_SESSION_BUS_ADDRESS="disabled:")  # never the real session bus
        env.pop("QT_QUICK_BACKEND", None)
        if renderer == "software":
            # As the compositor starts a shell that draws in software: no GPU vendor's GLX.
            if os.environ.get("__GLX_VENDOR_LIBRARY_NAME"):
                env["PAW_GLX_VENDOR"] = os.environ["__GLX_VENDOR_LIBRARY_NAME"]
            env["__GLX_VENDOR_LIBRARY_NAME"] = "paw-none"
            env["QT_QUICK_BACKEND"] = "software"
        env.update(extra_env)
        bus = None
        if private_bus:
            (root / "bus.conf").write_text(
                f"<busconfig><type>session</type><listen>unix:dir={directory}</listen>"
                '<auth>EXTERNAL</auth><policy context="default"><allow send_destination="*" eavesdrop="true"/>'
                '<allow eavesdrop="true"/><allow own="*"/></policy></busconfig>')
            bus = subprocess.Popen(["dbus-daemon", f"--config-file={root / 'bus.conf'}",
                                    "--nofork", "--print-address=1"], stdout=subprocess.PIPE,
                                   text=True)
            env["DBUS_SESSION_BUS_ADDRESS"] = bus.stdout.readline().strip()
        env.pop("DISPLAY", None)
        env.pop("WAYLAND_DISPLAY", None)
        clog, slog = root / "compositor.log", root / "shell.log"
        with clog.open("w") as cout, slog.open("w") as sout:
            server = subprocess.Popen([compositor, "--headless", "--config", str(example)],
                                      env=env, stdout=cout, stderr=cout)
            try:
                wait_for_text(clog, "Running Wayland compositor", server)
                text = clog.read_text()
                env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
                env["PAW_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
                start = time.monotonic()
                child = subprocess.Popen([shell, "--config", str(example)], env=env,
                                         stdout=sout, stderr=sout)
                try:
                    result = {}
                    for key, marker in (("ready_ms", "paw shell ready"),
                                        ("panel_ms", "paw surface rendered: paw taskbar"),
                                        ("desktop_ms", "paw surface rendered: paw desktop")):
                        result[key] = (wait_for_text(slog, marker, child) - start) * 1000
                    time.sleep(4)  # let startup work (the popups made ahead) settle before looking at idle
                    result["rss_mb"], result["pss_mb"] = memory(child.pid)
                    result["libs"] = libraries(child.pid)
                    cpu0, sw0, threads = proc_stat(child.pid)
                    t0 = time.monotonic()
                    time.sleep(idle)
                    cpu1, sw1, _ = proc_stat(child.pid)
                    span = time.monotonic() - t0
                    result["threads"] = threads
                    result["idle_cpu_pct"] = (cpu1 - cpu0) / span * 100
                    result["idle_ctxsw_per_s"] = sum(sw1[t] - sw0[t] for t in sw0.keys() & sw1.keys()) / span
                finally:
                    child.send_signal(signal.SIGTERM)
                    child.wait(timeout=10)
            finally:
                server.send_signal(signal.SIGTERM)
                server.wait(timeout=10)
                if bus:
                    bus.terminate()
                    bus.wait(timeout=10)
        return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("build", type=Path)
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--idle", type=float, default=10)
    parser.add_argument("--private-bus", action="store_true",
                        help="give the shell a session bus of its own (it serves notifications)")
    parser.add_argument("--renderer", choices=["gpu", "software"], default="gpu",
                        help="how the shell draws, as shell.renderer (default: gpu)")
    parser.add_argument("--env", action="append", default=[], metavar="NAME=VALUE")
    args = parser.parse_args()
    extra = dict(item.split("=", 1) for item in args.env)
    runs = [run(args.build.resolve(), args.idle, extra, args.renderer, args.private_bus)
            for _ in range(args.runs)]
    for key in runs[0]:
        values = [r[key] for r in runs]
        print(f"{key:18} median {statistics.median(values):9.2f}   min {min(values):9.2f}"
              f"   max {max(values):9.2f}")


if __name__ == "__main__":
    sys.exit(main())
