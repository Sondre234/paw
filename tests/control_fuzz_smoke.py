# SPDX-License-Identifier: GPL-3.0-or-later
"""Hammer the control socket with every action and query, odd arguments, and raw garbage while a
window is mapped: the compositor must stay alive, answer afterwards, and shut down cleanly.
Connections that never send a request are let go, and running out of file descriptors does not
make the compositor spin."""
import os
from pathlib import Path
import random
import re
import resource
import socket
import sys
import time

import harness

compositor, probe, example, query_source = (str(Path(p).resolve()) for p in sys.argv[1:5])
# Every query in query.c's table, so that a new one is hammered too.
QUERIES = re.findall(r'^\s*\{"([a-z_]+)", \w+, (?:true|false)\},$', Path(query_source).read_text(),
                     re.MULTILINE)
assert len(QUERIES) > 50, QUERIES
ACTIONS = """close cycle snap_left snap_right maximize restore tile reload fullscreen workspace
move_to_workspace workspace_next workspace_prev workspace_back toggle_tiling layout_next
layout_prev layout_dwindle layout_master layout_spiral layout_monocle layout_scroll
scroll_left scroll_right column_widen column_narrow column_cycle_width consume_left consume_right
expel center_column promote focus_next
focus_prev swap_next swap_prev master_grow master_shrink master_more master_less split_toggle
split_swap pseudo_toggle toggle_floating
launcher focus_left focus_right focus_up focus_down move_left move_right move_up move_down
move_to_scratchpad scratchpad_show toggle_sticky resize_left resize_right resize_up resize_down
switcher switcher_prev switcher_app switcher_app_prev switcher_confirm switcher_cancel
snap_top_left snap_top_right
snap_bottom_left snap_bottom_right snap_cycle_left snap_cycle_right snap_cycle_up snap_cycle_down
toggle_shortcuts_inhibit
get output subscribe""".split()
ARGUMENTS = ["", "0", "1", "-1", "4", "5", "99999999999999999999", "-99999999999999999999", "abc",
             "1 2", "1\t2", "0x10", "1e3", "%s%n", "'", "\"", "HEADLESS-1", "nonexistent",
             "workspaces", "windows", "outputs", "tiling", "animations", "x" * 400]

source = Path(example).read_text().replace("xwayland = true", "xwayland = false")

with harness.Compositor(compositor, source) as desktop:
    path = desktop.env["PAW_SOCKET"]

    def talk(data, read=True):
        """Sends raw bytes on a fresh connection and returns the reply."""
        with socket.socket(socket.AF_UNIX) as sock:
            sock.settimeout(5)
            sock.connect(path)
            reply = b""
            try:
                sock.sendall(data)
                if not read:
                    return b""
                sock.shutdown(socket.SHUT_WR)
                while chunk := sock.recv(4096):
                    reply += chunk
            except (ConnectionResetError, BrokenPipeError):
                pass  # the compositor hung up on a request it will not answer
            return reply

    server = desktop.server
    desktop.spawn([probe, "--external-control"])
    desktop.wait_for(lambda: talk(b"get windows\n").count(b"\n") == 2, "window mapped")

    rng = random.Random(int(os.environ.get("PAW_FUZZ_SEED", 1234)))
    for action in ACTIONS:
        for argument in ARGUMENTS:
            if action == "subscribe":
                continue
            reply = talk(f"{action} {argument}\n".encode())
            assert reply.startswith((b"ok", b"error")), (action, argument, reply)
            assert server.poll() is None, (action, argument)
            if action in ("output", "get") and argument in ("", "0"):
                continue
            talk(f"output {argument} {action}\n".encode())
    assert server.poll() is None
    # Every query bare, as `paw msg get NAME` sends it, and with each odd argument.
    for query in QUERIES:
        for argument in ARGUMENTS:
            request = f"get {query} {argument}".rstrip()
            reply = talk(f"{request}\n".encode())
            assert reply.startswith((b"ok", b"error")), (request, reply)
            assert server.poll() is None, request

    # Raw garbage: binary, NULs, unterminated and oversized lines, early disconnects.
    for _ in range(200):
        blob = bytes(rng.randrange(256)
                     for _ in range(rng.choice([1, 8, 100, 511, 512, 513, 4000])))
        talk(blob, read=rng.random() < .5)
        talk(blob.replace(b"\n", b"") + b"\n", read=rng.random() < .5)
    talk(b"\0\n")
    talk(b"workspace 2\0garbage\n")
    talk(b"x" * 100000, read=False)
    talk(b"", read=False)
    talk(b"get windows\n", read=False)  # hang up without reading the reply
    # Subscribers that vanish or never read must not stall or crash the compositor.
    crowd = []
    for _ in range(100):  # a burst well past a small listen backlog
        sock = socket.socket(socket.AF_UNIX)
        sock.connect(path)
        sock.sendall(b"subscribe\n")
        crowd.append(sock)
    for index, sock in enumerate(crowd):
        if index % 2:
            sock.close()
    for _ in range(20):
        talk(b"workspace_next\n")
    for sock in crowd[::2]:
        sock.close()

    # A client that asks for more than the socket holds and never reads it holds up the
    # compositor for its time limit at most: here the heatmap of a 4K monitor, over 200 KB.
    desktop.msg("headless_output", "add", "HEADLESS-2", "3840x2160")
    stuck = socket.socket(socket.AF_UNIX)
    stuck.connect(path)
    stuck.sendall(b"get heatmap\n")
    started = time.monotonic()
    assert talk(b"get workspace\n").startswith(b"ok\n")
    assert time.monotonic() - started < 3, "a client that never read held up the compositor"
    stuck.close()
    desktop.msg("headless_output", "remove", "HEADLESS-2")

    heard = {}

    def closed(sock):
        """Whether the compositor has closed `sock`, keeping what it said first in `heard`."""
        try:
            while data := sock.recv(4096, socket.MSG_DONTWAIT):
                heard[sock] = heard.get(sock, b"") + data
            return True
        except BlockingIOError:
            return False
        except ConnectionResetError:
            return True

    # Connections that never send a request: past 64 waiting at once, the one waiting longest
    # goes for each new one, and each goes 5 seconds after it came. (80 stay well inside the
    # test's own limit of open files.)
    started = time.monotonic()
    idle = [socket.socket(socket.AF_UNIX) for _ in range(80)]
    for sock in idle:
        sock.connect(path)
    assert talk(b"get workspace\n").startswith(b"ok\n")
    # Taken in turn, the request's connection after them: 17 too many.
    assert all(closed(sock) for sock in idle[:17]), [closed(sock) for sock in idle]
    assert heard[idle[0]] == b"error: too many connections waiting\n", heard.get(idle[0])
    if time.monotonic() - started < 4:  # else they may have run out of time as well
        assert not any(closed(sock) for sock in idle[17:]), [closed(sock) for sock in idle]
    # The compositor alone: "close" may have ended the window's probe.
    harness.wait_for(lambda: all(closed(sock) for sock in idle), [server],
                     "idle connections let go")
    assert heard[idle[-1]] == b"error: no request within 5 seconds\n", heard.get(idle[-1])
    for sock in idle:
        sock.close()

    # Out of file descriptors, the compositor stops taking connections for a moment rather than
    # fail to take one on every pass of its event loop, and takes them once it has some again.
    # Its limit goes down to the lowest descriptor it has free, so that it can open none.
    def lowest_free():
        used = {int(name) for name in os.listdir(f"/proc/{server.pid}/fd")}
        return next(number for number in range(len(used) + 1) if number not in used)

    def cpu_seconds():
        fields = Path(f"/proc/{server.pid}/stat").read_text().rsplit(")", 1)[1].split()
        return (int(fields[11]) + int(fields[12])) / os.sysconf("SC_CLK_TCK")

    limits = resource.prlimit(server.pid, resource.RLIMIT_NOFILE)
    resource.prlimit(server.pid, resource.RLIMIT_NOFILE, (lowest_free(), limits[1]))
    try:
        starved = [socket.socket(socket.AF_UNIX) for _ in range(8)]
        for sock in starved:
            sock.connect(path)
        before = cpu_seconds()
        harness.stays(lambda: cpu_seconds() - before < .5, [server], "the compositor spinning",
                      duration=1.5, detail=lambda: f"{cpu_seconds() - before:.2f} s of CPU")
    finally:
        resource.prlimit(server.pid, resource.RLIMIT_NOFILE, limits)
    # Said once, however many times it waited.
    assert desktop.log.read_text().count("Control socket: cannot take a connection") == 1
    assert talk(b"get workspace\n").startswith(b"ok\n")
    for sock in starved:
        sock.close()

    talk(b"workspace 1\n")
    assert server.poll() is None
    assert talk(b"get windows\n").startswith(b"ok\n")  # "close" may have ended the window
    assert talk(b"get workspace\n") == b"ok\n1\n"
    # A client still sending its request as the session ends is let go with the rest (under the
    # sanitizers, a leak of it fails the clean exit). The answer to a later connection shows the
    # compositor has taken it in.
    half = socket.socket(socket.AF_UNIX)
    half.connect(path)
    half.sendall(b"get work")
    crowd.append(half)
    assert talk(b"get workspace\n") == b"ok\n1\n"
print("Control socket survived every action, odd argument, and garbage input")
