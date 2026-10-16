#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Whether a soak leaked: the trough of each cycle and a line fitted through them.

usage: tools/prof/heaptrend.py DIR [--exe NAME] [--marks FILE] [--skip N] [--leak-kb KB]

DIR is soak.py's --out: pawprobe's .log of each probed process and the marks of the cycles
("cycle 1", "cycle 2", ...). For each process, the least value of the live heap (malloc's
in-use bytes), mmapped allocations, RSS, open fds and threads logged inside each cycle: every
cycle ends with its windows closed and nothing moving, so the troughs compare like with like.
A least-squares line through the troughs, leaving out the first --skip cycles (default 2,
while caches and pools fill), gives the growth per cycle and how well a line explains it (r²).
A leak is a steady rise: the heap growing by more than --leak-kb (default 1) per cycle, or
fds or threads by two or more over the cycles fitted, with r² over 0.5; anything else is
noise. The more cycles, the smaller a leak that shows.
"""
import argparse
from pathlib import Path
import re
import sys

COLUMNS = {"heap": "live heap", "mmap": "mmapped", "rss_kb": "RSS", "fds": "fds",
           "threads": "threads"}
KB = {"heap": 1024, "mmap": 1024, "rss_kb": 1, "fds": None, "threads": None}


def read_log(path):
    """[(t, {column: value})] from a pawprobe log."""
    names, rows = None, []
    for line in path.read_text().splitlines():
        if line.startswith("#"):
            names = line[1:].split()
            continue
        values = line.split("\t")
        if names and len(values) == len(names):
            row = dict(zip(names, (float(v) for v in values)))
            rows.append((row["t"], row))
    return rows


def read_cycles(path):
    """[(cycle number, start, end)] from a marks file."""
    cycles = []
    for line in path.read_text().splitlines():
        if found := re.fullmatch(r"cycle (\d+)\t([\d.]+)\t([\d.]+)", line):
            cycles.append((int(found[1]), float(found[2]), float(found[3])))
    return sorted(cycles)


def fit(xs, ys):
    """Slope and r² of the least-squares line through the points."""
    n = len(xs)
    mean_x, mean_y = sum(xs) / n, sum(ys) / n
    sxx = sum((x - mean_x) ** 2 for x in xs)
    sxy = sum((x - mean_x) * (y - mean_y) for x, y in zip(xs, ys))
    syy = sum((y - mean_y) ** 2 for y in ys)
    slope = sxy / sxx if sxx else 0.0
    r2 = sxy * sxy / (sxx * syy) if sxx and syy else 0.0
    return slope, r2


def show(column, value):
    scale = KB[column]
    return f"{value / scale:.1f} kB" if scale else f"{value:.0f}"


def analyse(log, cycles, skip, leak_kb):
    rows = read_log(log)
    troughs = []
    for number, start, end in cycles:
        inside = [row for t, row in rows if start <= t <= end]
        if inside:
            troughs.append((number, {c: min(r[c] for r in inside) for c in COLUMNS}))
    name = log.name.removesuffix(".log")
    if not troughs:
        print(f"{name}: no logged line falls inside a cycle")
        return False
    print(f"{name}: {len(troughs)} cycle{'s' * (len(troughs) != 1)}, troughs:")
    print("cycle  " + "  ".join(f"{COLUMNS[c]:>12}" for c in COLUMNS))
    for number, trough in troughs:
        print(f"{number:5d}  " + "  ".join(f"{show(c, trough[c]):>12}" for c in COLUMNS))
    fitted = troughs[skip:]
    if len(fitted) < 3:
        print(f"too few cycles after the first {skip} to fit a line\n")
        return False
    print(f"\ngrowth per cycle over cycles {fitted[0][0]} to {fitted[-1][0]}:")
    leaking = []
    for column in COLUMNS:
        slope, r2 = fit([n for n, _ in fitted], [t[column] for _, t in fitted])
        total = fitted[-1][1][column] - fitted[0][1][column]
        scale = KB[column] or 1
        print(f"  {COLUMNS[column]:>9}  {slope / scale:+9.2f}{' kB' if KB[column] else '   '}"
              f"  r² {r2:4.2f}   first to last {total / scale:+.1f}{' kB' if KB[column] else ''}")
        if column == "heap" and slope / 1024 > leak_kb and r2 > 0.5:
            leaking.append(COLUMNS[column])
        # One fd more at one trough is a socket still closing; a leak keeps adding them.
        if KB[column] is None and slope * (len(fitted) - 1) >= 2 and r2 > 0.5:
            leaking.append(COLUMNS[column])
    if leaking:
        print(f"LEAK? {', '.join(leaking)} rise steadily from cycle to cycle\n")
    else:
        print(f"no leak: the heap grows by less than {leak_kb} kB a cycle or not steadily, "
              "and fds and threads do not keep rising\n")
    return bool(leaking)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dir", type=Path, help="soak.py's --out")
    parser.add_argument("--exe", help="only this program's log (paw, paw-shell)")
    parser.add_argument("--marks", type=Path, help="the marks file (default DIR/marks)")
    parser.add_argument("--skip", type=int, default=2, help="warm-up cycles left out of the fit")
    parser.add_argument("--leak-kb", type=float, default=1.0,
                        help="heap growth per cycle that counts as a leak (default 1 kB)")
    args = parser.parse_args()

    cycles = read_cycles(args.marks or args.dir / "marks")
    if not cycles:
        sys.exit("no cycles marked")
    logs = sorted(p for p in args.dir.glob("*.log")
                  if not args.exe or re.sub(r"(\.\d+)+\.log$", "", p.name) == args.exe)
    if not logs:
        sys.exit(f"no pawprobe log in {args.dir}")
    leaks = [analyse(log, cycles, args.skip, args.leak_kb) for log in logs]
    sys.exit(1 if any(leaks) else 0)


if __name__ == "__main__":
    main()
