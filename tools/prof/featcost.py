#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""What each feature costs the compositor per frame, from `get frame_times`.

usage: tools/prof/featcost.py BUILD_DIR [--renderer pixman|gles2 ...] [--frames N]
                              [--windows N] [--warmup N] [--only FEATURE ...]

For each renderer, starts a headless paw (tests/harness.py's: nothing of the real session;
gles2 renders on the GPU through its render node, which needs no display), opens --windows
windows of which one redraws on every frame, so that the output draws every frame, and for
each feature turns it on, takes the time paw spent drawing each of the next --frames frames
(inside output_frame, `get frame_times HEADLESS-1`) and turns it off again. The baseline is the
same windows with nothing on, taken after --warmup frames and again at the end ("baseline end",
which shows what drifted meanwhile). Prints the median, the 95th percentile and the worst frame,
and the median's difference from the baseline's.

This is the compositor's CPU time per frame; with gles2 the GPU's own time is not in it.
Compare runs made one after the other on the same machine: times below a millisecond move by
tens of percent with the machine's load.
"""
import argparse
from pathlib import Path
import statistics
import sys

import pawprobe
from pawprobe import harness

OUTPUT = "HEADLESS-1"
CONFIG = """return {
    xwayland = false,
    idle = { display_off = 0 },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    layout = { tiling = false, workspaces = 4 },
    heatmap = { enabled = true },
    profile = "plain",
    profiles = {
        plain = {},
        shadows = { windows = { shadow = { enabled = true } } },
        blur = { windows = { opacity = 0.9, inactive_opacity = 0.8, blur = { enabled = true } } },
    },
}"""


def profile(name):
    def switch(desktop):
        before = desktop.reloads()
        desktop.msg("profile", name)
        desktop.wait_for(lambda: desktop.reloads() > before, f"profile {name}")
    return switch


def actions(*names):
    def run(desktop):
        for name in names:
            desktop.msg(*name.split())
    return run


def nothing(desktop):
    pass


# name: (turn on, turn off). The frames are measured after `on` once nothing moves, except
# for the toys, which are measured while they play.
FEATURES = {
    "baseline": (nothing, nothing),
    "shadows": (profile("shadows"), profile("plain")),
    "blur": (profile("blur"), profile("plain")),
    "tiling": (actions("toggle_tiling"), actions("toggle_tiling")),
    "overview": (actions("toggle_overview"), actions("overview_cancel")),
    "switcher": (actions("switcher"), actions("switcher_cancel")),
    "xray": (actions("xray"), actions("xray")),
    "heatmap": (actions("heatmap"), actions("heatmap")),
    "zoom": (actions("zoom_in", "zoom_in"), actions("zoom_reset")),
    "night_light": (actions("night_light_on"), actions("night_light_auto")),
    "party": (actions("party"), nothing),
    "gravity": (actions("gravity"), nothing),
}
PLAYING = {"party", "gravity"}


def frame_times(desktop):
    """(frames drawn so far, [time spent in each of the last frames, oldest first, in us])."""
    lines = desktop.msg("get", "frame_times", OUTPUT).splitlines()
    return int(lines[0]), [int(line.split("\t")[0]) for line in lines[1:]]


def measure(desktop, frames, warmup=3):
    """The time spent in each of the next `frames` frames, after `warmup` frames that may
    still be drawing everything once over."""
    start, _ = frame_times(desktop)
    desktop.wait_for(lambda: frame_times(desktop)[0] >= start + warmup, "warm-up frames")
    start += warmup
    desktop.wait_for(lambda: frame_times(desktop)[0] >= start + frames, f"{frames} frames",
                     timeout=60)
    total, spent = frame_times(desktop)
    return spent[-min(total - start, len(spent)):]


def run(build, renderer, features, windows, frames, warmup):
    """{feature: [frame times]} with this renderer, or None if paw would not start."""
    env = {"WLR_RENDERER": renderer}
    results = {}
    with harness.Compositor(str(build / "paw"), CONFIG, env=env, start=False,
                            prefix="paw-cost-") as desktop:
        try:
            desktop.start()
        except AssertionError as error:
            print(f"{renderer}: paw did not start ({error}):\n{desktop.log.read_text()[-800:]}",
                  file=sys.stderr)
            desktop.server = None
            return None
        for i in range(windows):
            title = f"cost {i + 1}"
            desktop.spawn([str(build / "paw-bench-client"), "--title", title,
                           *(["--animate"] if i == 0 else [])])
            desktop.wait_for(lambda: any(w.title == title for w in desktop.windows()),
                             f"{title} mapped")
        # A GPU's clocks settle some seconds into a steady load: with gles2, a baseline taken
        # before they did read 270 us where every feature after it read 380 us or more.
        start, _ = frame_times(desktop)
        desktop.wait_for(lambda: frame_times(desktop)[0] >= start + warmup,
                         f"{warmup} warm-up frames", timeout=60 + warmup / 30)
        for name in features:
            on, off = FEATURES[name]
            desktop.wait_for(lambda: pawprobe.settled(desktop), f"{name}: settled before")
            on(desktop)
            if name not in PLAYING:
                desktop.wait_for(lambda: pawprobe.settled(desktop), f"{name}: settled")
            results[name] = measure(desktop, frames)
            off(desktop)
            if name in PLAYING:
                desktop.wait_for(lambda: desktop.rows("toys")[0][1] == "none", f"{name} over",
                                 timeout=30)
            print(f"  {renderer} {name}: {len(results[name])} frames", file=sys.stderr)
        desktop.wait_for(lambda: pawprobe.settled(desktop), "settled at the end")
        results["baseline end"] = measure(desktop, frames)
    return results


def percentile(values, share):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(share * len(ordered)))]


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("build", type=Path, help="the build directory (paw, paw-bench-client)")
    parser.add_argument("--renderer", nargs="+", default=["pixman"], choices=["pixman", "gles2"])
    parser.add_argument("--frames", type=int, default=120, help="frames per feature (120)")
    parser.add_argument("--windows", type=int, default=4, help="windows open (default 4)")
    parser.add_argument("--warmup", type=int, default=600,
                        help="frames drawn before the baseline (600, about 10 s)")
    parser.add_argument("--only", nargs="+", choices=list(FEATURES)[1:], metavar="FEATURE",
                        help=f"measure only these: {', '.join(list(FEATURES)[1:])}")
    args = parser.parse_args()
    build = args.build.resolve()
    features = ["baseline", *(args.only or list(FEATURES)[1:])]

    for renderer in args.renderer:
        results = run(build, renderer, features, args.windows, args.frames, args.warmup)
        if results is None:
            continue
        base = statistics.median(results["baseline"])
        print(f"\n{renderer}: time in output_frame per frame, {args.windows} windows "
              f"(one redrawing), 1280x720")
        print(f"{'feature':<12} {'frames':>6} {'p50 us':>8} {'p95 us':>8} {'max us':>8} "
              f"{'p50 - baseline':>15}")
        for name, spent in results.items():
            p50 = statistics.median(spent)
            extra = "" if name == "baseline" else f"{p50 - base:+15.0f}"
            print(f"{name:<12} {len(spent):6d} {p50:8.0f} {percentile(spent, .95):8d} "
                  f"{max(spent):8d} {extra}".rstrip())


if __name__ == "__main__":
    main()
