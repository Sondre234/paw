#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Headless benchmark of the compositor's hot paths.

Starts `paw --headless` (pixman renderer, no GPU, no real session), opens N windows with
paw-bench-client, then drives it through the control socket and reports timings:

  open     time to map N windows, and compositor CPU per window
  idle     compositor CPU and thread wakeups per second with nothing happening
  ops      per action (workspace switch, layout change, focus, tiling toggle, ...): the round
           trip of the control request, and the compositor CPU per action including the
           reflow the windows answer with
  animate  M windows redrawing on every frame: frames per second, time per frame, per commit
  pointer  M pointer motions swept over the N windows (needs --pointer-probe): compositor time
           per motion event
  smooth   window animations on, M windows redrawing, requests that move every window
           (layout changes, workspace switches) one after another: p50/p95/p99 of the time
           the compositor spends in a frame and of the interval between frames

CPU is the compositor's scheduler run time from /proc, so it counts only the compositor
process, not the clients or this script. Nothing here touches a real display.

  tools/bench/bench.py --compositor build/paw --client build/paw-bench-client
  tools/bench/bench.py ... --windows 60 --json before.json
  tools/bench/bench.py ... --quick        (a few seconds; what ctest runs)
  tools/bench/bench.py ... --profile layout_next,focus_next   (where the compositor spends
                                          its time on those requests; needs gdb, no perf)
"""
import argparse
import json
import os
from pathlib import Path
import re
import socket
import statistics
import subprocess
import sys
import tempfile
import time

RULES = """
    windows = { border_width = 2, inactive_opacity = 0.9, rules = {
        { app_id = "^firefox$", opacity = 1 },
        { app_id = "^(kitty|alacritty|foot)$", opacity = 0.95, inactive_opacity = 0.85 },
        { app_id = "^(code|codium|emacs)$", opacity = 0.97 },
        { title = "^(Picture-in-Picture|Volume Control)$", floating = true },
        { app_id = "^org\\\\.gnome\\\\..*$", opacity = 0.99 },
    } },"""


def config_text(animations, rules, output):
    return f"""return {{
    xwayland = false,
    layout = {{ tiling = true, workspaces = 4 }},
    animations = {{ enabled = {str(animations).lower()} }},
    outputs = {{ monitors = {{ ["HEADLESS-1"] = {{ mode = "{output}" }} }} }},
{RULES if rules else "    windows = { border_width = 2 },"}
}}
"""


class Compositor:
    def __init__(self, binary, directory, config, wrapper=(), quiet=0.15):
        """`quiet`: how long settle() waits without a commit."""
        self.binary = str(binary)
        self.quiet = quiet
        self.directory = directory
        self.env = dict(os.environ, XDG_RUNTIME_DIR=directory, WLR_RENDERER="pixman")
        for name in ("WAYLAND_DISPLAY", "DISPLAY", "PAW_SOCKET"):
            self.env.pop(name, None)
        self.log_path = Path(directory) / "compositor.log"
        self.log = self.log_path.open("w")
        self.process = subprocess.Popen([*wrapper, self.binary, "--headless", "--config", str(config)],
                                        env=self.env, stdout=self.log, stderr=self.log)
        deadline = time.monotonic() + 15
        while "Running Wayland compositor" not in self.log_path.read_text():
            if self.process.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError("compositor did not start:\n" + self.log_path.read_text())
            time.sleep(0.02)
        text = self.log_path.read_text()
        self.env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
        self.env["PAW_SOCKET"] = re.search(r"Control socket: (\S+)", text)[1]
        self.clients = []

    def request(self, line):
        """(reply body, seconds from sending the request to its reply)"""
        with socket.socket(socket.AF_UNIX) as sock:
            sock.settimeout(10)
            sock.connect(self.env["PAW_SOCKET"])
            started = time.perf_counter()
            sock.sendall(line.encode() + b"\n")
            reply = b""
            while chunk := sock.recv(65536):
                reply += chunk
            elapsed = time.perf_counter() - started
        text = reply.decode()
        if not text.startswith("ok"):
            raise RuntimeError(f"{line}: {text.strip()}")
        return text.split("\n", 1)[1], elapsed

    def windows(self):
        return [row.split("\t") for row in self.request("get windows")[0].splitlines()]

    def stats(self):
        keys = ("frames", "frame_ns", "frame_max_ns", "commits", "commit_ns", "configures",
                "opacity_rules", "motions", "motion_ns", "reflows", "reflow_ns")
        return dict(zip(keys, map(int, self.request("get stats")[0].split())))

    def cpu_ns(self):
        """Scheduler run time of the compositor process (nanoseconds)."""
        return int(Path(f"/proc/{self.process.pid}/schedstat").read_text().split()[0])

    def wakeups(self):
        status = Path(f"/proc/{self.process.pid}/status").read_text()
        return sum(int(m) for m in re.findall(r"^(?:non)?voluntary_ctxt_switches:\s+(\d+)", status,
                                              re.M))

    def rss_kib(self):
        return int(re.search(r"^VmRSS:\s+(\d+)", Path(f"/proc/{self.process.pid}/status")
                             .read_text(), re.M)[1])

    def launch(self, client, *args):
        process = subprocess.Popen([str(client), *args], env=self.env, stdout=subprocess.DEVNULL)
        self.clients.append(process)
        return process

    def settle(self, timeout=10):
        """Waits until commits stop arriving (clients answered every configure)."""
        quiet = self.quiet
        deadline = time.monotonic() + timeout
        last, since = self.stats()["commits"], time.monotonic()
        while time.monotonic() < deadline:
            time.sleep(0.02)
            now = self.stats()["commits"]
            if now != last:
                last, since = now, time.monotonic()
            elif time.monotonic() - since >= quiet:
                return
        raise RuntimeError("compositor never settled")

    def stop_clients(self):
        for client in self.clients:
            client.terminate()
        for client in self.clients:
            client.wait(timeout=10)
        self.clients = []
        deadline = time.monotonic() + 10
        while self.windows():
            if time.monotonic() >= deadline:
                raise RuntimeError("windows outlived their clients")
            time.sleep(0.02)

    def stop(self):
        for client in self.clients:
            if client.poll() is None:
                client.terminate()
        for client in self.clients:
            try:
                client.wait(timeout=5)
            except subprocess.TimeoutExpired:
                client.kill()
                client.wait()
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        self.log.close()


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(len(ordered) * fraction))]


def open_windows(comp, client, count):
    cpu, started = comp.cpu_ns(), time.perf_counter()
    for i in range(count):
        comp.launch(client, "--app-id", ("kitty", "firefox", "code", "other")[i % 4],
                    "--title", f"bench window {i}")
    deadline = time.monotonic() + 60
    while len(comp.windows()) < count:
        if time.monotonic() > deadline:
            raise RuntimeError(f"only {len(comp.windows())} of {count} windows mapped")
        time.sleep(0.01)
    comp.settle()
    return {"windows": count, "total_ms": (time.perf_counter() - started) * 1000,
            "cpu_ms_per_window": (comp.cpu_ns() - cpu) / 1e6 / max(1, count)}


def idle(comp, seconds):
    comp.settle()
    cpu, wake, frames, started = comp.cpu_ns(), comp.wakeups(), comp.stats()["frames"], \
        time.perf_counter()
    time.sleep(seconds)
    elapsed = time.perf_counter() - started
    return {"seconds": elapsed, "cpu_ms_per_s": (comp.cpu_ns() - cpu) / 1e6 / elapsed,
            "wakeups_per_s": (comp.wakeups() - wake) / elapsed,
            "frames_per_s": (comp.stats()["frames"] - frames) / elapsed}


OPERATIONS = [
    # name, layout to start from, requests per iteration (a pair goes back and forth, so the
    # state does not drift)
    ("workspace 1<->2", "layout_dwindle", ["workspace 2", "workspace 1"]),
    ("layout_next", "layout_dwindle", ["layout_next"]),
    ("toggle_tiling x2", "layout_dwindle", ["toggle_tiling", "toggle_tiling"]),
    ("focus_next", "layout_dwindle", ["focus_next"]),
    ("swap_next", "layout_dwindle", ["swap_next"]),
    ("master_grow/shrink", "layout_master", ["master_grow", "master_shrink"]),
    ("promote", "layout_master", ["promote"]),
    ("toggle_floating x2", "layout_dwindle", ["toggle_floating", "toggle_floating"]),
    ("move_to_workspace 2/1", "layout_dwindle",
     ["move_to_workspace 2", "workspace 2", "move_to_workspace 1", "workspace 1"]),
]


def operations(comp, iterations):
    results = {}
    for name, layout, requests in OPERATIONS:
        comp.request(layout)
        comp.settle()
        latencies = []
        cpu, before = comp.cpu_ns(), comp.stats()
        for _ in range(iterations):
            for request in requests:
                latencies.append(comp.request(request)[1] * 1000)
        comp.settle()
        after = comp.stats()
        count = iterations * len(requests)
        results[name] = {
            "requests": count,
            "p50_ms": statistics.median(latencies),
            "p95_ms": percentile(latencies, 0.95),
            "max_ms": max(latencies),
            "cpu_ms_per_request": (comp.cpu_ns() - cpu) / 1e6 / count,
            "commits_per_request": (after["commits"] - before["commits"]) / count,
            "commit_us": (after["commit_ns"] - before["commit_ns"]) / 1e3 /
                         max(1, after["commits"] - before["commits"]),
            "frame_ms_per_request": (after["frame_ns"] - before["frame_ns"]) / 1e6 / count,
            "placements_per_request": (after["configures"] - before["configures"]) / count,
            "reflow_us": (after["reflow_ns"] - before["reflow_ns"]) / 1e3 /
                         max(1, after["reflows"] - before["reflows"]),
        }
    return results


def animate(comp, client, count, seconds):
    comp.settle()
    comp.stop_clients()
    for i in range(count):
        comp.launch(client, "--animate", "--app-id", ("kitty", "firefox", "code")[i % 3],
                    "--title", f"animated {i}")
    deadline = time.monotonic() + 30
    while len(comp.windows()) < count:
        if time.monotonic() > deadline:
            raise RuntimeError("animated windows did not map")
        time.sleep(0.02)
    time.sleep(0.5)
    cpu, stats, started = comp.cpu_ns(), comp.stats(), time.perf_counter()
    time.sleep(seconds)
    elapsed = time.perf_counter() - started
    after = comp.stats()
    frames = max(1, after["frames"] - stats["frames"])
    commits = max(1, after["commits"] - stats["commits"])
    return {
        "windows": count,
        "cpu_percent": (comp.cpu_ns() - cpu) / 1e9 / elapsed * 100,
        "frames_per_s": frames / elapsed,
        "frame_ms_avg": (after["frame_ns"] - stats["frame_ns"]) / frames / 1e6,
        "frame_ms_max": after["frame_max_ns"] / 1e6,
        "commits_per_s": commits / elapsed,
        "commit_us_avg": (after["commit_ns"] - stats["commit_ns"]) / commits / 1e3,
    }


# Requests that move or resize every window while all of them stay on screen and uncovered: a
# redrawing window that is hidden (another workspace, floating windows piled over it) is sent no
# frame callbacks, the frames stop, and the idle gap that follows would read as a stall.
SMOOTH_REQUESTS = ["layout_dwindle", "layout_master", "master_grow", "master_shrink", "focus_next",
                   "swap_next", "promote", "layout_dwindle", "swap_next", "focus_next"]


def frame_times(comp):
    """(total frames, [(spent us, interval us)]) of the frames the compositor kept."""
    lines = comp.request("get frame_times")[0].splitlines()
    return int(lines[0]), [tuple(map(int, line.split("\t"))) for line in lines[1:]]


def smooth(comp, client, count, animated, seconds, gap):
    """Frame times while requests keep every window moving and some windows keep redrawing."""
    comp.settle()
    comp.stop_clients()
    for i in range(count):
        args = ["--animate"] if i < animated else []
        comp.launch(client, *args, "--app-id", ("kitty", "firefox", "code")[i % 3],
                    "--title", f"smooth {i}")
    deadline = time.monotonic() + 60
    while len(comp.windows()) < count:
        if time.monotonic() > deadline:
            raise RuntimeError("windows did not map")
        time.sleep(0.02)
    time.sleep(0.5)
    comp.request("layout_dwindle")
    time.sleep(0.5)
    total, _ = frame_times(comp)
    cpu, started = comp.cpu_ns(), time.perf_counter()
    latencies, sent = [], 0
    while time.perf_counter() - started < seconds:
        latencies.append(comp.request(SMOOTH_REQUESTS[sent % len(SMOOTH_REQUESTS)])[1] * 1000)
        sent += 1
        time.sleep(gap)
    elapsed = time.perf_counter() - started
    after, kept = frame_times(comp)
    mine = kept[-min(len(kept), after - total):] if after > total else []
    spent = [row[0] / 1000 for row in mine]
    # The redrawing windows keep frames coming, so a long gap is a stall of the event loop.
    intervals = [row[1] / 1000 for row in mine if row[1] > 0]
    result = {"windows": count, "animated": animated, "requests": sent,
              "frames": len(mine), "late_frames": sum(1 for value in intervals if value > 25),
              "frames_lost": max(0, after - total - len(mine)),
              "cpu_percent": (comp.cpu_ns() - cpu) / 1e9 / elapsed * 100,
              "request_p50_ms": statistics.median(latencies),
              "request_p95_ms": percentile(latencies, 0.95)}
    for name, values in (("frame", spent), ("interval", intervals)):
        if values:
            result[f"{name}_p50_ms"] = statistics.median(values)
            result[f"{name}_p95_ms"] = percentile(values, 0.95)
            result[f"{name}_p99_ms"] = percentile(values, 0.99)
            result[f"{name}_max_ms"] = max(values)
    return result


def pointer(comp, probe, count, moves, width, height, animating=False):
    """Compositor time per pointer motion event with `count` windows tiled under the pointer;
    with `animating`, while requests keep the windows gliding (a hit test then has to put every
    moving window at its resting place and back)."""
    import threading
    if not animating:
        comp.settle()
    busy = threading.Event()

    def keep_moving():
        i = 0
        while not busy.is_set():
            comp.request(("layout_dwindle", "layout_master")[i % 2])
            i += 1
            busy.wait(0.03)  # a glide lasts longer than that: something is always moving

    mover = threading.Thread(target=keep_moving) if animating else None
    if mover:
        mover.start()
    process = subprocess.Popen([probe, str(width), str(height)], env=comp.env,
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    try:
        assert process.stdout.readline().strip() == "ready"
        # The first sweep warms caches; the compositor counts events and their time itself.
        rounds = []
        for _ in range(5):
            before = comp.stats()
            process.stdin.write(f"sweep {moves}\n")
            process.stdin.flush()
            assert process.stdout.readline().strip() == "done"
            after = comp.stats()
            events = after["motions"] - before["motions"]
            rounds.append((after["motion_ns"] - before["motion_ns"]) / max(1, events) / 1e3)
    finally:
        busy.set()
        if mover:
            mover.join()
        process.stdin.close()
        process.wait(timeout=10)
    return {"windows": count, "moves": moves, "animating": animating, "motion_us_median": statistics.median(rounds[1:]),
            "motion_us_min": min(rounds[1:]), "motion_us_max": max(rounds[1:])}


def profile(args):
    """Samples the compositor's call stacks while it handles a stream of requests, using gdb
    (tools/bench/gdb_sampler.py) for hosts without perf. Returns (samples, self, inclusive)."""
    import collections
    import signal
    import threading
    requests = args.profile.split(",")
    with tempfile.TemporaryDirectory(prefix="paw-prof-") as directory:
        os.environ["PAW_SAMPLES"] = str(Path(directory) / "samples.txt")
        config = Path(directory) / "init.lua"
        config.write_text(config_text(args.animations, not args.no_rules, args.output))
        sampler = Path(__file__).with_name("gdb_sampler.py")
        comp = Compositor(args.compositor, directory, config,
                          wrapper=["gdb", "-batch", "-x", str(sampler), "--args"])
        try:
            open_windows(comp, args.client, args.windows)
            comp.request("layout_dwindle")
            comp.settle()
            children = subprocess.run(["pgrep", "-P", str(comp.process.pid)],
                                      capture_output=True, text=True).stdout.split()
            target = int(children[0])
            running = True

            def poke():
                while running:
                    time.sleep(0.005)
                    os.kill(target, signal.SIGURG)

            thread = threading.Thread(target=poke)
            thread.start()
            done = time.monotonic() + args.seconds
            while time.monotonic() < done:
                for request in requests:
                    comp.request(request)
            running = False
            thread.join()
        finally:
            comp.stop_clients()
            for child in subprocess.run(["pgrep", "-P", str(comp.process.pid)],
                                        capture_output=True, text=True).stdout.split():
                os.kill(int(child), signal.SIGTERM)
            comp.process.wait(timeout=30)
        counts = {"self": collections.Counter(), "incl": collections.Counter()}
        samples = 0
        for line in (Path(directory) / "samples.txt").read_text().splitlines():
            kind, *rest = line.split(None, 2)
            if kind == "samples":
                samples = int(rest[0])
            else:
                counts[kind][rest[1]] = int(rest[0])
    return samples, counts["self"], counts["incl"]


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--compositor", required=True, help="path to the paw binary")
    parser.add_argument("--client", required=True, help="path to paw-bench-client")
    parser.add_argument("--windows", type=int, default=40, help="windows to open (default 40)")
    parser.add_argument("--iterations", type=int, default=30, help="repeats of each operation")
    parser.add_argument("--animated", type=int, default=8, help="windows redrawing every frame")
    parser.add_argument("--seconds", type=float, default=4, help="idle/animate sample length")
    parser.add_argument("--output", default="1920x1080", help="headless output mode")
    parser.add_argument("--animations", action="store_true", help="leave window animations on")
    parser.add_argument("--no-rules", action="store_true", help="no window rules in the config")
    parser.add_argument("--only", nargs="*", choices=["open", "idle", "ops", "animate", "smooth", "pointer"])
    parser.add_argument("--pointer-probe", help="path to pointer_probe: enables the pointer scenario")
    parser.add_argument("--gap", type=float, default=0.25,
                        help="seconds between requests in the smooth scenario (default 0.25)")
    parser.add_argument("--json", help="also write the results to this file")
    parser.add_argument("--profile", metavar="REQUESTS",
                        help="comma separated control requests to sample under gdb, for "
                             "--seconds, instead of running the benchmark")
    parser.add_argument("--quick", action="store_true", help="tiny run for a smoke test")
    args = parser.parse_args()
    if args.quick:
        args.windows, args.iterations, args.animated, args.seconds = 12, 3, 2, 0.5
        args.gap = 0.1
    if args.profile:
        samples, own, inclusive = profile(args)
        print(f"{samples} samples of {args.profile} with {args.windows} windows")
        print(f"{'share':>6}  self time in")
        for name, count in own.most_common(15):
            print(f"{100 * count / max(1, samples):5.1f}%  {name}")
        print(f"{'share':>6}  including callees")
        for name, count in inclusive.most_common(60):
            print(f"{100 * count / max(1, samples):5.1f}%  {name}")
        return
    wanted = set(args.only or ["open", "idle", "ops", "animate", "smooth", "pointer"])

    results = {"windows": args.windows, "output": args.output, "rules": not args.no_rules}
    with tempfile.TemporaryDirectory(prefix="paw-bench-") as directory:
        config = Path(directory) / "init.lua"
        config.write_text(config_text(args.animations, not args.no_rules, args.output))
        # A smoke test trusts no timing: a shorter quiet spell does to settle.
        quiet = 0.05 if args.quick else 0.15
        comp = Compositor(args.compositor, directory, config, quiet=quiet)
        try:
            if wanted & {"open", "idle", "ops", "pointer"}:
                results["open"] = open_windows(comp, args.client, args.windows)
            if "idle" in wanted:
                results["idle"] = idle(comp, args.seconds)
            if "ops" in wanted:
                results["ops"] = operations(comp, args.iterations)
            if "pointer" in wanted and args.pointer_probe:
                width, height = map(int, args.output.split("x"))
                results["pointer"] = pointer(comp, args.pointer_probe, args.windows,
                                             2000 if not args.quick else 200, width, height)
            if "animate" in wanted:
                results["animate"] = animate(comp, args.client, args.animated, args.seconds)
            results["rss_mib"] = comp.rss_kib() / 1024
        finally:
            comp.stop()
        if "smooth" in wanted:  # its own compositor: the scenario needs animations on
            config.write_text(config_text(True, not args.no_rules, args.output))
            comp = Compositor(args.compositor, directory, config, quiet=quiet)
            try:
                results["smooth"] = smooth(comp, args.client, args.windows, args.animated,
                                           args.seconds if args.quick else max(args.seconds, 8),
                                           args.gap)
                if args.pointer_probe:
                    width, height = map(int, args.output.split("x"))
                    results["pointer_animating"] = pointer(
                        comp, args.pointer_probe, args.windows, 2000 if not args.quick else 100,
                        width, height, animating=True)
            finally:
                comp.stop()
    report(results)
    if args.quick:  # the checks a test can make without trusting timings
        assert results["smooth"]["frames"] > 0, "no frame times were reported"
        placed = results["ops"]["toggle_tiling x2"]["placements_per_request"]
        assert placed <= 2 * args.windows, f"toggling tiling placed {placed} windows for {args.windows}"
    if args.json:
        Path(args.json).write_text(json.dumps(results, indent=2) + "\n")


def report(results):
    print(f"paw benchmark: {results['windows']} windows, {results['output']} headless, "
          f"window rules {'on' if results['rules'] else 'off'}")
    if "open" in results:
        o = results["open"]
        print(f"open     {o['total_ms']:8.0f} ms for {o['windows']} windows, "
              f"{o['cpu_ms_per_window']:.2f} ms CPU per window")
    if "idle" in results:
        i = results["idle"]
        print(f"idle     {i['cpu_ms_per_s']:8.2f} ms CPU/s, {i['wakeups_per_s']:.1f} wakeups/s, "
              f"{i['frames_per_s']:.1f} frames/s")
    if "ops" in results:
        print(f"{'operation':24} {'p50 ms':>8} {'p95 ms':>8} {'max ms':>8} {'cpu ms':>8} "
              f"{'commits':>8} {'commit us':>9} {'frame ms':>8} {'placed':>7} {'reflow us':>9}")
        for name, r in results["ops"].items():
            print(f"{name:24} {r['p50_ms']:8.2f} {r['p95_ms']:8.2f} {r['max_ms']:8.2f} "
                  f"{r['cpu_ms_per_request']:8.2f} {r['commits_per_request']:8.1f} "
                  f"{r['commit_us']:9.1f} {r['frame_ms_per_request']:8.2f} "
                  f"{r['placements_per_request']:7.1f} {r['reflow_us']:9.1f}")
    if "animate" in results:
        a = results["animate"]
        print(f"animate  {a['windows']} windows: {a['frames_per_s']:.1f} frames/s, "
              f"{a['frame_ms_avg']:.2f} ms/frame (worst {a['frame_ms_max']:.2f}), "
              f"{a['commits_per_s']:.0f} commits/s at {a['commit_us_avg']:.1f} us, "
              f"{a['cpu_percent']:.1f}% CPU")
    if "smooth" in results:
        m = results["smooth"]
        print(f"smooth   {m['windows']} windows ({m['animated']} redrawing), {m['requests']} requests, "
              f"{m['frames']} frames ({m['late_frames']} later than 25 ms), {m['cpu_percent']:.1f}% CPU, request p50 "
              f"{m['request_p50_ms']:.2f} p95 {m['request_p95_ms']:.2f} ms")
        for name, label in (("frame", "time in a frame"), ("interval", "between frames")):
            if f"{name}_p50_ms" in m:
                print(f"           {label:16} p50 {m[name + '_p50_ms']:6.2f}  p95 {m[name + '_p95_ms']:6.2f}"
                      f"  p99 {m[name + '_p99_ms']:6.2f}  max {m[name + '_max_ms']:6.2f} ms")
    if "pointer" in results:
        m = results["pointer"]
        print(f"pointer  {m['windows']} windows, {m['moves']} moves per sweep: "
              f"{m['motion_us_median']:.1f} us per motion (sweeps {m['motion_us_min']:.1f} to "
              f"{m['motion_us_max']:.1f})")
    if "pointer_animating" in results:
        m = results["pointer_animating"]
        print(f"pointer  while windows glide: {m['motion_us_median']:.1f} us per motion (sweeps "
              f"{m['motion_us_min']:.1f} to {m['motion_us_max']:.1f})")
    if "rss_mib" in results:
        print(f"memory   {results['rss_mib']:.1f} MiB resident")


if __name__ == "__main__":
    sys.exit(main())
