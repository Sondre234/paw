# SPDX-License-Identifier: GPL-3.0-or-later
"""The gdb half of a sampling profiler for hosts without perf: run as `gdb -batch -x
gdb_sampler.py --args paw ...`. Each SIGURG that bench.py sends the compositor stops it
here; the call stack is recorded and the compositor continues. On exit the counts go to
$PAW_SAMPLES. (The compositor takes SIGINT itself, and gdb's Python threads do not run
while it waits for the inferior, hence the signal comes from outside.) Sampling slows the
compositor down: read the shares, not the times.
"""
import collections
import os

import gdb

output = os.environ.get("PAW_SAMPLES", "samples.txt")
depth = int(os.environ.get("PAW_SAMPLE_DEPTH", "30"))
self_time = collections.Counter()
inclusive = collections.Counter()
total = [0]

gdb.execute("handle SIGURG stop print nopass")
gdb.execute("handle SIGPIPE nostop noprint pass")
gdb.execute("handle SIGTERM nostop noprint pass")
gdb.execute("handle SIGHUP nostop noprint pass")
gdb.execute("set pagination off")
gdb.execute("set confirm off")


def frames():
    names = []
    frame = gdb.newest_frame()
    while frame is not None and len(names) < depth:
        names.append(frame.name() or "??")
        frame = frame.older()
    return names


def exited(event):
    with open(output, "w") as out:
        out.write(f"samples {total[0]}\n")
        for name, count in self_time.most_common(40):
            out.write(f"self {count} {name}\n")
        for name, count in inclusive.most_common(80):
            out.write(f"incl {count} {name}\n")


def sample():
    names = frames()
    if names:
        total[0] += 1
        self_time[names[0]] += 1
        for name in set(names):
            inclusive[name] += 1


gdb.events.exited.connect(exited)
gdb.execute("run")
while gdb.selected_inferior().pid:  # each stop is one SIGURG; the run ends when it exits
    sample()
    gdb.execute("continue")
