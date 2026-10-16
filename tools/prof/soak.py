#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Runs a headless paw, pawprobe preloaded, through cycles of most of what it does, to find
leaks (heaptrend.py) and hot paths (report.py).

usage: tools/prof/soak.py BUILD_DIR [--cycles N] [--out DIR] [--windows N] [--busy N]
                          [--shell] [--hz HZ] [--skip STEP...]

Each cycle opens --windows windows (paw-bench-client, --busy of them redrawing on every frame),
goes through the tiling layouts, workspaces, the overview, the window switcher, the scratchpad,
a group of tabs, the toys (shake, gravity, party), the exploded view, the heatmap's overlay,
zoom, night light, the appearance profiles and a reload, then closes every window and waits
for nothing to move, so that each cycle ends where it began. --skip leaves steps out.

The compositor is tests/harness.py's: its own runtime and state directories, the pixman
renderer, nothing of the real session. With --shell paw-shell runs on it too (software
rendering, no session bus) and is probed as well. Afterwards OUT (default prof-soak) holds
pawprobe's files for each probed process and `marks`, a span per cycle ("cycle 3") and per step
of every cycle ("overview"):

    python3 tools/prof/heaptrend.py OUT
    python3 tools/prof/report.py OUT --exe paw --thread main
    python3 tools/prof/report.py OUT --exe paw --marks OUT/marks --mark party
"""
import argparse
from pathlib import Path
import re
import sys
import time

import pawprobe
from pawprobe import harness


class Soak:
    def __init__(self, desktop, client, windows, busy):
        self.desktop, self.client, self.windows, self.busy = desktop, client, windows, busy
        self.msg = desktop.msg
        self.clients = []
        self.opened = 0

    def wait(self, predicate, message, timeout=20):
        self.desktop.wait_for(predicate, message, timeout=timeout)

    def settle(self, message):
        self.wait(lambda: pawprobe.settled(self.desktop), f"{message}: nothing moving")

    def titles(self):
        return [w.title for w in self.desktop.windows()]

    def open(self, animate=False):
        self.opened += 1
        title = f"soak {self.opened}"
        self.clients.append(self.desktop.spawn(
            [self.client, "--title", title, *(["--animate"] if animate else [])]))
        self.wait(lambda: title in self.titles(), f"{title} mapped")

    # The steps, in order; each leaves things as it found them but for the windows open.

    def open_windows(self):
        for i in range(self.windows):
            self.open(animate=i < self.busy)
        self.settle("opened")

    def layouts(self):
        msg = self.msg
        msg("toggle_tiling")
        for action in ("layout_next", "layout_next", "layout_next", "layout_prev", "master_grow",
                       "master_shrink", "promote", "focus_next", "swap_next", "focus_prev"):
            msg(action)
            self.settle(action)
        msg("toggle_tiling")
        self.settle("tiling off")

    def workspaces(self):
        msg = self.msg
        msg("move_to_workspace", "2")
        msg("workspace", "2")
        self.settle("workspace 2")
        msg("workspace", "3")
        msg("workspace", "2")
        msg("move_to_workspace", "1")
        msg("workspace", "1")
        self.wait(lambda: all(w.workspace == 1 for w in self.desktop.windows()),
                  "every window back on workspace 1")
        self.settle("workspace 1")

    def overview(self):
        self.msg("toggle_overview")
        self.wait(lambda: not self.msg("get", "overview").startswith("closed"), "overview open")
        self.settle("overview opened")
        self.msg("overview_cancel")
        self.wait(lambda: self.msg("get", "overview").startswith("closed"), "overview closed")
        self.settle("overview closed")

    def switcher(self):
        for action in ("switcher", "switcher", "switcher_prev", "switcher_cancel"):
            self.msg(action)
        self.settle("switcher")

    def scratchpad(self):
        msg = self.msg
        msg("move_to_scratchpad")
        self.wait(lambda: any(w.scratchpad for w in self.desktop.windows()), "in the scratchpad")
        for _ in range(3):  # shown, hidden, shown
            msg("scratchpad_show")
            self.settle("scratchpad")
        # Moved to another workspace, it leaves the scratchpad; then it comes back.
        msg("move_to_workspace", "2")
        self.wait(lambda: not any(w.scratchpad for w in self.desktop.windows()),
                  "out of the scratchpad")
        msg("workspace", "2")
        msg("move_to_workspace", "1")
        msg("workspace", "1")
        self.wait(lambda: all(w.workspace == 1 for w in self.desktop.windows()),
                  "every window back on workspace 1")
        self.settle("scratchpad")

    def group(self):
        msg = self.msg
        msg("group_toggle")
        self.wait(lambda: any(w.group for w in self.desktop.windows()), "grouped")
        self.open()  # joins the group as a tab
        for action in ("group_next", "group_prev", "group_next"):
            msg(action)
        msg("group_toggle")
        self.wait(lambda: not any(w.group for w in self.desktop.windows()), "group dissolved")
        self.settle("group")

    def toys(self):
        for toy in ("shake", "gravity", "party"):
            self.msg(toy)
            self.wait(lambda: self.desktop.rows("toys")[0][1] == "none", f"{toy} over",
                      timeout=30)
            self.settle(toy)

    def xray(self):
        self.msg("xray")
        self.wait(lambda: not self.msg("get", "xray").startswith("closed"), "xray open")
        self.settle("xray opened")
        self.msg("xray")
        self.wait(lambda: self.msg("get", "xray").startswith("closed"), "xray closed")
        self.settle("xray closed")

    def heatmap(self):
        self.msg("heatmap")
        self.settle("heatmap shown")
        self.msg("heatmap")
        self.settle("heatmap hidden")

    def zoom(self):
        for action in ("zoom_in", "zoom_in", "zoom_out", "zoom_reset"):
            self.msg(action)
            self.settle(action)

    def night_light(self):
        for action in ("night_light_on", "night_light_off", "night_light_auto"):
            self.msg(action)
            self.settle(action)

    def profiles(self):
        for name in ("glass", "ink", "plain"):
            before = self.desktop.reloads()
            self.msg("profile", name)
            self.wait(lambda: self.desktop.reloads() > before, f"profile {name}")
            self.settle(f"profile {name}")

    def reload(self):
        self.desktop.reload()
        self.settle("reload")

    def close_windows(self):
        while titles := self.titles():
            if not any(w.focused for w in self.desktop.windows()):
                self.msg("focus_next")
            self.msg("close")
            # The client exits as its window closes, which the harness's own waits take for a
            # failure: watch the compositor alone.
            harness.wait_for(lambda: len(self.titles()) < len(titles), [self.desktop.server],
                             "a window closed")
        for client in self.clients:
            self.desktop.reap(client)
        self.clients.clear()
        self.settle("closed")


STEPS = ["open_windows", "layouts", "workspaces", "overview", "switcher", "scratchpad", "group",
         "toys", "xray", "heatmap", "zoom", "night_light", "profiles", "reload", "close_windows"]


def logged_since(directory, moment):
    """Whether every probed process still running has logged a line after `moment`
    (CLOCK_MONOTONIC)."""
    logs = [log for log in Path(directory).glob("*.log")
            if Path("/proc", re.search(r"\.(\d+)(\.\d+)?\.log$", log.name)[1]).exists()]
    for log in logs:
        lines = log.read_text().splitlines()
        if not lines or lines[-1].startswith("#") or float(lines[-1].split("\t")[0]) <= moment:
            return False
    return bool(logs)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("build", help="the build directory (paw, paw-bench-client)")
    parser.add_argument("--cycles", type=int, default=20, help="cycles to run (default 20)")
    parser.add_argument("--out", default="prof-soak", help="where the probe's files go")
    parser.add_argument("--windows", type=int, default=4, help="windows each cycle opens")
    parser.add_argument("--busy", type=int, default=1,
                        help="how many of them redraw on every frame (default 1)")
    parser.add_argument("--shell", action="store_true", help="run and probe paw-shell too")
    parser.add_argument("--hz", type=int, help="CPU samples per second (pawprobe's 250)")
    parser.add_argument("--skip", nargs="*", default=[], choices=STEPS[1:-1], metavar="STEP",
                        help=f"steps to leave out: {', '.join(STEPS[1:-1])}")
    args = parser.parse_args()

    build = Path(args.build).resolve()
    compositor, client = build / "paw", build / "paw-bench-client"
    for binary in (compositor, client):
        if not binary.exists():
            sys.exit(f"{binary} is missing: build paw with -DPAW_BUILD_COMPOSITOR=ON")
    if pawprobe.sanitized(compositor):
        sys.exit("this is an ASan build: its runtime has to load first, so the probe cannot")
    shell = next((p for p in (build / "paw-shell", build / "shell" / "paw-shell") if p.exists()),
                 None)
    if args.shell and not shell:
        sys.exit(f"no paw-shell in {build}")
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    for old in (*out.glob("*.samples"), *out.glob("*.maps"), *out.glob("*.log"),
                *out.glob("*.threads"), out / "marks"):
        old.unlink(missing_ok=True)  # an earlier soak's
    library = pawprobe.build(build)
    programs = ("paw", "paw-shell") if args.shell else ("paw",)
    env = pawprobe.environment(library, out, programs, hz=args.hz)
    marks = pawprobe.Marks(out / "marks")
    steps = [step for step in STEPS if step not in args.skip]

    with harness.Compositor(str(compositor), pawprobe.CONFIG, env=env, start=False,
                            prefix="paw-soak-") as desktop:
        if args.shell:
            root = desktop.root
            desktop.env.update(QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software",
                               XDG_DATA_HOME=str(root), XDG_DATA_DIRS=str(root),
                               DBUS_SESSION_BUS_ADDRESS="disabled:")
        desktop.start()
        if args.shell:
            log = desktop.root / "shell.log"
            desktop.spawn([str(shell), "--config", str(desktop.config)], log="shell.log")
            desktop.wait_for(lambda: "paw surface rendered" in log.read_text(),
                             "the shell drew", timeout=60)
        soak = Soak(desktop, str(client), args.windows, args.busy)
        began = time.monotonic()
        for cycle in range(1, args.cycles + 1):
            with marks.span(f"cycle {cycle}"):
                for step in steps:
                    with marks.span(step):
                        getattr(soak, step)()
                # Rest until each process has logged the state the cycle ends in twice.
                rested = time.monotonic() + .3
                desktop.wait_for(lambda: logged_since(out, rested), "the probe logged")
            print(f"cycle {cycle}/{args.cycles} done, {time.monotonic() - began:.0f} s",
                  flush=True)
        desktop.stop()

    print(f"\n{out}:")
    for path in sorted(out.iterdir()):
        print(f"  {path.name}")
    print(f"\npython3 {Path(__file__).parent / 'heaptrend.py'} {out}"
          f"\npython3 {Path(__file__).parent / 'report.py'} {out} --exe paw --thread main")


if __name__ == "__main__":
    main()
