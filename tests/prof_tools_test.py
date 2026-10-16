# SPDX-License-Identifier: GPL-3.0-or-later
"""tools/prof reads what pawprobe writes: report.py counts a samples file's stacks by function,
thread and marked span and writes them collapsed, and stops at a sample cut off by the process
ending; heaptrend.py takes each cycle's trough from a log and calls a steady rise of the heap or
of the fds a leak, and noise none. The files are made up here, of addresses no object holds, so
no symbolizer is needed."""
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

tools = Path(sys.argv[1])
NS = 1_000_000_000


def run(script, *words, code=0):
    result = subprocess.run([sys.executable, str(tools / script), *map(str, words)],
                            capture_output=True, text=True)
    assert result.returncode == code, (words, result.returncode, result.stdout, result.stderr)
    return result.stdout


with tempfile.TemporaryDirectory(prefix="paw-prof-") as directory:
    root = Path(directory)

    # Three samples of the main thread in the vdso called from an unknown address, at 10.0 s,
    # 10.1 s and 10.2 s, one of a worker at 20 s, and half a sample.
    def sample(tid, seconds, *addresses):
        return struct.pack(f"<IIQ{len(addresses)}Q", len(addresses), tid, int(seconds * NS),
                           *addresses)

    data = struct.pack("<8sIIQII", b"PAWPROB1", 1, 4000, 9 * NS, 100, 0)
    data += b"".join(sample(100, 10 + i / 10, 0x7fff0010, 0x500) for i in range(3))
    data += sample(101, 20, 0x600) + sample(100, 21, 0x700, 0x800)[:20]
    (root / "paw.100.samples").write_bytes(data)
    (root / "paw.100.maps").write_text(
        "55550000-55551000 r--p 00000000 00:00 0 /nonexistent/paw\n"
        "7fff0000-7fff2000 r-xp 00000000 00:00 0                          [vdso]\n")
    (root / "paw.100.threads").write_text("100\tpaw\n101\tworker\n")
    (root / "marks").write_text("# name\tstart\tend\ncycle 1\t9.95\t10.15\ncycle 2\t19\t21\n")

    folded = root / "folded"
    out = run("report.py", root, "--collapsed", folded)
    assert "paw 100: 4 samples 4000 us of CPU apart, about 0.0 s, in 2 threads" in out, out
    assert " 75.0%        3  [vdso]\n" in out and " 25.0%        1  [unknown]\n" in out, out
    assert "100.0%        4  [unknown]" in out.split("inclusive")[1], out
    assert " 75.0%        3  paw (100)\n" in out and " 25.0%        1  worker (101)\n" in out, out
    assert folded.read_text() == "paw;[unknown];[vdso] 3\nworker;[unknown] 1\n", folded.read_text()
    assert "paw 100: 3 samples" in run("report.py", root, "--thread", "main")
    assert "1 samples" in run("report.py", root, "--thread", "worker")
    assert "2 samples" in run("report.py", root, "--marks", root / "marks", "--mark", "cycle 1")
    assert "3 samples" in run("report.py", root, "--marks", root / "marks", "--mark", "cycle *")
    assert "no samples" in subprocess.run(
        [sys.executable, str(tools / "report.py"), root, "--exe", "paw-shell"],
        capture_output=True, text=True).stderr

    # Ten cycles of 5 s, logged every second: the heap dips to its trough in each.
    def soak(name, heap, fds=lambda cycle: 40):
        marks, lines = [], ["# t\theap\tmmap\trss_kb\tfds\tthreads\tcpu_ms\tvcsw\tivcsw"]
        for cycle in range(1, 11):
            start = 100 + 5 * cycle
            marks.append(f"cycle {cycle}\t{start}\t{start + 4.5}")
            for second in range(5):
                busy = 0 if second == 4 else 500_000
                lines.append(f"{start + second}\t{heap(cycle) + busy}\t4096\t50000\t{fds(cycle)}"
                             f"\t3\t{cycle * 10}\t{cycle}\t0")
        (root / name).mkdir()
        (root / name / "marks").write_text("\n".join(marks) + "\n")
        (root / name / "paw.200.log").write_text("\n".join(lines) + "\n")
        return root / name

    noise = [0, 3000, -2000, 1000, -500, 2500, -1500, 0, 1000, -3000]
    out = run("heaptrend.py", soak("flat", lambda c: 2_000_000 + noise[c - 1]))
    assert "paw.200: 10 cycles" in out and "no leak" in out, out
    assert f"{2_000_000 / 1024:.1f} kB" in out, "a cycle's trough is its least heap"
    out = run("heaptrend.py", soak("leak", lambda c: 2_000_000 + 10_240 * c + noise[c - 1]),
              code=1)
    assert re.search(r"live heap +\+(9|10)\.\d\d kB", out) and "LEAK? live heap rise" in out, out
    out = run("heaptrend.py", soak("fds", lambda c: 2_000_000, lambda c: 40 + c), code=1)
    assert "LEAK? fds rise" in out, out
    out = run("heaptrend.py", soak("fdnoise", lambda c: 2_000_000, lambda c: 40 + (c == 10)))
    assert "no leak" in out, "one fd more at the last trough is noise: " + out
