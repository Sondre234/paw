#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Names pawprobe's CPU samples and says where the time went.

usage: tools/prof/report.py PATH... [--exe NAME] [--pid PID] [--thread main|all|TID|NAME]
                            [--marks FILE --mark PATTERN] [--top N] [--no-inlines]
                            [--collapsed FILE] [--symbolizer PATH]

Each PATH is a directory pawprobe wrote to (PAWPROBE_DIR) or one of its .samples files. For
each process, the share of the samples spent in each function itself (self) and in it or what
it called (inclusive), and in each object (the program or a library). Addresses are mapped
through the object's PT_LOAD segments and named by llvm-symbolizer, inlined functions as frames
of their own unless --no-inlines; paw's own frames carry their source file, a library's its
name. System libraries are stripped: their exported functions get names, the rest "??".

--thread main keeps the samples of the main thread (the compositor's event loop), a number or a
name (from the .threads file, e.g. QSGRenderThread) one thread's. --marks and --mark keep the
samples taken inside the marked spans whose name matches the shell pattern (soak.py's marks:
"cycle 3", "overview", "cycle *"). --collapsed writes one line per distinct stack, the thread
first and the frames root to leaf, and its count: what flamegraph.pl and speedscope read.
"""
import argparse
import bisect
import collections
import fnmatch
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys

MAGIC = b"PAWPROB1"
HEADER = struct.Struct("<8sIIQII")
RECORD = struct.Struct("<IIQ")
UNKNOWN = ("[unknown]", "[unknown]")


class Process:
    """One process's samples: (tid, ns, addresses) with the interrupted instruction first."""

    def __init__(self, samples_path):
        self.base = str(samples_path)[:-len(".samples")]
        name = Path(self.base).name
        data = Path(samples_path).read_bytes()
        if len(data) < HEADER.size or data[:8] != MAGIC:
            raise ValueError(f"{samples_path}: not a pawprobe samples file")
        _, version, self.interval_us, self.start_ns, self.pid, _ = HEADER.unpack_from(data)
        if version != 1:
            raise ValueError(f"{samples_path}: version {version}, not 1")
        self.exe = name[:name.index(f".{self.pid}")] if f".{self.pid}" in name else name
        self.samples = []
        at = HEADER.size
        while at + RECORD.size <= len(data):
            depth, tid, ns = RECORD.unpack_from(data, at)
            end = at + RECORD.size + 8 * depth
            if end > len(data):  # the process ended mid-write
                break
            self.samples.append((tid, ns, struct.unpack_from(f"<{depth}Q", data, at + RECORD.size)))
            at = end
        self.threads = {}
        threads = Path(self.base + ".threads")
        if threads.exists():
            for line in threads.read_text().splitlines():
                tid, _, thread = line.partition("\t")
                self.threads[int(tid)] = thread
        self.maps = Maps(Path(self.base + ".maps"))

    def thread_name(self, tid):
        return self.threads.get(tid, str(tid))


class Maps:
    """The executable mappings of /proc/PID/maps: which object an address is in, and where."""

    def __init__(self, path):
        self.ranges = []
        if not path.exists():
            print(f"warning: {path} is missing; no address can be named", file=sys.stderr)
            return
        for line in path.read_text().splitlines():
            fields = line.split(maxsplit=5)
            if len(fields) < 6 or "x" not in fields[1]:
                continue
            start, end = (int(value, 16) for value in fields[0].split("-"))
            self.ranges.append((start, end, int(fields[2], 16), fields[5]))
        self.ranges.sort()
        self.starts = [r[0] for r in self.ranges]

    def find(self, address):
        """(object path, offset in its file) or None."""
        at = bisect.bisect_right(self.starts, address) - 1
        if at < 0:
            return None
        start, end, offset, path = self.ranges[at]
        if address >= end:
            return None
        return path, address - start + offset


_loads = {}


def pt_loads(path):
    """The PT_LOAD segments (file offset, size in the file, address) of a 64-bit ELF file."""
    if path not in _loads:
        segments = []
        try:
            with open(path, "rb") as elf:
                ident = elf.read(64)
                if ident[:4] == b"\x7fELF" and ident[4] == 2:
                    order = "<" if ident[5] == 1 else ">"
                    phoff, = struct.unpack_from(order + "Q", ident, 32)
                    phentsize, phnum = struct.unpack_from(order + "HH", ident, 54)
                    elf.seek(phoff)
                    table = elf.read(phentsize * phnum)
                    for i in range(phnum):
                        kind, _, offset, vaddr, _, filesz = struct.unpack_from(
                            order + "IIQQQQ", table, i * phentsize)
                        if kind == 1:
                            segments.append((offset, filesz, vaddr))
        except OSError:
            pass
        _loads[path] = segments
    return _loads[path]


def object_address(path, offset):
    for start, size, vaddr in pt_loads(path):
        if start <= offset < start + size:
            return offset - start + vaddr
    return None


class Symbolizer:
    def __init__(self, tool, inlines):
        self.tool, self.inlines = tool, inlines

    def names(self, path, addresses):
        """{address: [(function, source file), ...] innermost first}; "" for an unknown one."""
        addresses = sorted(addresses)
        self.tool = find_symbolizer(self.tool)
        command = [self.tool, "--output-style=JSON", f"--obj={path}", "--functions=linkage",
                   "--demangle", "--inlines" if self.inlines else "--no-inlines"]
        result = subprocess.run(command, input="".join(f"0x{a:x}\n" for a in addresses),
                                capture_output=True, text=True)
        names = {}
        for address, line in zip(addresses, result.stdout.splitlines()):
            frames = []
            for symbol in json.loads(line).get("Symbol", []):
                function = symbol.get("FunctionName", "")
                source = os.path.basename(symbol.get("FileName", "") or "")
                frames.append((function if function not in ("", "??") else "", source))
            names[address] = frames or [("", "")]
        return names


def find_symbolizer(given):
    if given:
        return given
    if found := shutil.which("llvm-symbolizer"):
        return found
    candidates = sorted(Path("/usr/lib/llvm").glob("*/bin/llvm-symbolizer"),
                        key=lambda p: int(p.parts[-3]) if p.parts[-3].isdigit() else 0)
    if candidates:
        return str(candidates[-1])
    sys.exit("report.py needs llvm-symbolizer (LLVM); --symbolizer names it")


def name_frames(process, samples, symbolizer):
    """{(index, address): [(function, where), ...] innermost first} for every frame of the
    samples. A return address (every frame but the first) is looked up one byte back, inside
    the call."""
    wanted = collections.defaultdict(set)  # object path -> object addresses
    located = {}
    warned = set()
    for _, _, addresses in samples:
        for index, address in enumerate(addresses):
            key = (index > 0, address)
            if key in located:
                continue
            look = address - 1 if index > 0 else address
            found = process.maps.find(look)
            if found is None:
                located[key] = None
                continue
            path, offset = found
            if not path.startswith("/"):
                located[key] = (path, None)  # [vdso] and the like
                continue
            if path.startswith("/memfd:"):  # code made at run time: a JIT's, Qt Quick's
                located[key] = (f"[{path[1:].removesuffix(' (deleted)')}]", None)
                continue
            real = path.removesuffix(" (deleted)")
            if real != path and real not in warned:
                warned.add(real)
                print(f"warning: {real} was replaced while it ran; its names may be wrong",
                      file=sys.stderr)
            vaddr = object_address(real, offset)
            located[key] = (real, vaddr)
            if vaddr is not None:
                wanted[real].add(vaddr)
    names = {path: symbolizer.names(path, addresses) for path, addresses in wanted.items()}
    frames = {}
    for key, place in located.items():
        if place is None:
            frames[key] = [UNKNOWN]
            continue
        path, vaddr = place
        library = os.path.basename(path) if path.startswith("/") else path
        if vaddr is None:
            frames[key] = [(library, library) if library == path else ("??", library)]
            continue
        chain = []
        for function, source in names[path].get(vaddr, [("", "")]):
            if function:
                chain.append((function, source or library))
            else:
                chain.append(("??", library))
        frames[key] = chain
    return frames


def expand(addresses, frames):
    """The stack's frames, innermost first, inlined functions included."""
    stack = []
    for index, address in enumerate(addresses):
        stack.extend(frames[(index > 0, address)])
    return stack


def load_marks(path, pattern):
    spans = []
    for line in Path(path).read_text().splitlines():
        if not line or line.startswith("#"):
            continue
        name, start, end = line.rsplit("\t", 2)
        if fnmatch.fnmatchcase(name, pattern):
            spans.append((float(start), float(end)))
    if not spans:
        sys.exit(f"no mark in {path} matches {pattern!r}")
    return spans


def label(key, width=None):
    function, where = key
    text = function if function == where else f"{function} ({where})"
    if width and len(text) > width:
        text = text[:width - 3] + "..."
    return text


def report(process, samples, frames, top):
    total = len(samples)
    seconds = total * process.interval_us / 1e6
    per_thread = collections.Counter(tid for tid, _, _ in samples)
    print(f"{process.exe} {process.pid}: {total} samples {process.interval_us} us of CPU apart, "
          f"about {seconds:.1f} s, in {len(per_thread)} thread{'s' * (len(per_thread) != 1)}")
    if not total:
        return
    self_time, inclusive, objects = (collections.Counter() for _ in range(3))
    for _, _, addresses in samples:
        stack = expand(addresses, frames)
        self_time[stack[0]] += 1
        inclusive.update(set(stack))
        found = process.maps.find(addresses[0])
        objects[os.path.basename(found[0].removesuffix(" (deleted)")) if found else "[unknown]"] += 1

    def table(title, counter):
        print(f"\n{title}")
        for key, count in counter.most_common(top):
            name = key if isinstance(key, str) else label(key, 110)
            print(f"{100 * count / total:6.1f}%  {count:7d}  {name}")

    if len(per_thread) > 1:
        table("threads", collections.Counter(
            {f"{process.thread_name(tid)} ({tid})": n for tid, n in per_thread.items()}))
    table("self time, by function", self_time)
    table("inclusive time, by function", inclusive)
    table("self time, by object", objects)


def write_collapsed(output, process, samples, frames):
    stacks = collections.Counter()
    for tid, _, addresses in samples:
        stack = expand(addresses, frames)
        names = [process.thread_name(tid)] + [label(key).replace(";", ":")
                                              for key in reversed(stack)]
        stacks[";".join(names)] += 1
    for stack, count in sorted(stacks.items()):
        output.write(f"{stack} {count}\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("paths", nargs="+", help="pawprobe's directory or .samples files")
    parser.add_argument("--exe", help="only this program's processes (e.g. paw, paw-shell)")
    parser.add_argument("--pid", type=int, help="only this process")
    parser.add_argument("--thread", default="all", help="main, all, a thread id or a name")
    parser.add_argument("--marks", help="a marks file (soak.py's) for --mark")
    parser.add_argument("--mark", help="keep samples inside the marks matching this pattern")
    parser.add_argument("--top", type=int, default=25, help="rows per table (default 25)")
    parser.add_argument("--no-inlines", dest="inlines", action="store_false",
                        help="name the function an inlined one was inlined into")
    parser.add_argument("--collapsed", help="write collapsed stacks for a flame graph here")
    parser.add_argument("--symbolizer", help="the llvm-symbolizer to use")
    args = parser.parse_args()
    if bool(args.marks) != bool(args.mark):
        parser.error("--marks and --mark go together")

    files = []
    for path in map(Path, args.paths):
        files.extend(sorted(path.glob("*.samples")) if path.is_dir() else [path])
    processes = [Process(f) for f in files]
    processes = [p for p in processes if (not args.exe or p.exe == args.exe)
                 and (not args.pid or p.pid == args.pid)]
    if not processes:
        sys.exit("no samples of such a process")
    spans = load_marks(args.marks, args.mark) if args.marks else None
    symbolizer = Symbolizer(args.symbolizer, args.inlines)  # found when first needed
    collapsed = open(args.collapsed, "w") if args.collapsed else None

    for number, process in enumerate(processes):
        samples = process.samples
        if args.thread == "main":
            samples = [s for s in samples if s[0] == process.pid]
        elif args.thread.isdigit():
            samples = [s for s in samples if s[0] == int(args.thread)]
        elif args.thread != "all":
            samples = [s for s in samples if process.threads.get(s[0]) == args.thread]
        if spans:
            samples = [s for s in samples if any(a <= s[1] / 1e9 <= b for a, b in spans)]
        frames = name_frames(process, samples, symbolizer)
        if number:
            print()
        report(process, samples, frames, args.top)
        if collapsed:
            write_collapsed(collapsed, process, samples, frames)
    if collapsed:
        collapsed.close()


if __name__ == "__main__":
    main()
