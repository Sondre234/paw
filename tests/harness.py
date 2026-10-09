# SPDX-License-Identifier: GPL-3.0-or-later
"""Helpers shared by the integration tests."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time


class Timeout(AssertionError):
    """wait_for ran out of time (rather than a process exiting): worth retrying."""


def wait_for(predicate, processes, message, timeout=15, detail=None):
    """Polls `predicate` until it holds, failing if a process exits or time runs out."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for process in processes:
            assert process.poll() is None, f"process exited ({process.returncode}): {message}"
        if predicate():
            return
        time.sleep(.02)
    raise Timeout(f"timed out: {message}" + (f"; {detail()}" if detail else ""))


def stays(predicate, processes, message, duration=.3, detail=None):
    """Checks that `predicate` keeps holding for `duration`: for something that must not happen,
    which an animation or a client's commit could otherwise do after a single look."""
    deadline = time.monotonic() + duration
    while time.monotonic() < deadline:
        for process in processes:
            assert process.poll() is None, f"process exited ({process.returncode}): {message}"
        assert predicate(), message + (f"; {detail()}" if detail else "")
        time.sleep(.02)


def disjoint(rects):
    """Whether no two of the (x, y, width, height) rectangles overlap."""
    return all(a[0] + a[2] <= b[0] or b[0] + b[2] <= a[0] or
               a[1] + a[3] <= b[1] or b[1] + b[3] <= a[1]
               for i, a in enumerate(rects) for b in rects[i + 1:])


def end(process):
    """Ends a process that may still be running: politely, then by force."""
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


class Compositor:
    """A headless compositor of the test's own: a temporary runtime directory, the pixman
    renderer, `config` (Lua text) in its init.lua, and in `env` what a client needs to reach it
    (WAYLAND_DISPLAY, PAW_SOCKET, and DISPLAY when XWayland listens).

    As a context manager it starts the compositor (unless start=False, for a test that sets
    something up first). On the way out, unless the test failed, it checks that the compositor
    shuts down cleanly with its clients still connected, as at the end of a session; then it ends
    every process it started, prints the logs if anything failed, and removes the directory.

        with harness.Compositor(compositor, CONFIG) as desktop:
            desktop.spawn([probe, "--window-only"])
            desktop.wait_for(lambda: desktop.rows("windows"), "the window mapped")
    """

    def __init__(self, binary, config=None, *, env=None, bus=False, start=True,
                 prefix="paw-test-"):
        self.binary = binary
        # Keep the prefix short: the control socket lives in this directory, and a Unix
        # socket's path is limited to about 107 bytes.
        self._directory = tempfile.TemporaryDirectory(prefix=prefix)
        self.root = Path(self._directory.name)
        self.config = self.root / "init.lua"
        if config is not None:
            self.config.write_text(config)
        self.log = self.root / "compositor.log"
        # State of its own too: the developer's saved appearance profile must not change the
        # configuration a test runs.
        self.env = dict(os.environ, XDG_RUNTIME_DIR=str(self.root),
                        XDG_STATE_HOME=str(self.root / "state"), WLR_RENDERER="pixman")
        for name in ("WAYLAND_DISPLAY", "DISPLAY", "PAW_SOCKET"):
            self.env.pop(name, None)
        self.env.update(env or {})
        self.server = None
        self.bus = None
        self.clients = []
        self._started = []  # everything, to end even after a failure
        # Called for wait_for's and stays' failure messages when they are given none.
        self.detail = None
        # Printed if the test fails; a test may add files of its own.
        self.logs = [self.log]
        self._start, self._private_bus = start, bus

    def __enter__(self):
        try:
            if self._private_bus:
                self.private_bus()
            if self._start:
                self.start()
        except BaseException:
            self.__exit__(*sys.exc_info())
            raise
        return self

    def __exit__(self, kind, error, trace):
        failed = kind is not None
        try:
            if self.server is not None and not failed:
                self.stop()
        except BaseException:
            failed = True
            raise
        finally:
            for process in reversed(self._started):
                end(process)
            self.clients.clear()
            if failed:
                for path in self.logs:
                    if path.exists():
                        print(f"== {path.name}\n{path.read_text()}", file=sys.stderr)
            self._directory.cleanup()
        return False

    @property
    def processes(self):
        """Every process the test started and expects to keep running, the compositor first."""
        return [p for p in (self.bus, self.server) if p is not None] + self.clients

    def start(self, config=None):
        """Starts the compositor, or starts it again after stop(), on `config` if given (written
        to init.lua), and waits until clients can connect. Returns the compositor's process."""
        assert self.server is None, "the compositor is already running"
        if config is not None:
            self.config.write_text(config)
        for name in ("WAYLAND_DISPLAY", "DISPLAY", "PAW_SOCKET"):
            self.env.pop(name, None)
        with self.log.open("w") as output:
            self.server = subprocess.Popen(
                [self.binary, "--headless", "--config", str(self.config)], env=self.env,
                stdout=output, stderr=output)
        self._started.append(self.server)
        self.wait_for(lambda: "Running Wayland compositor" in self.log.read_text(), "startup",
                      timeout=30)
        text = self.log.read_text()
        self.env["WAYLAND_DISPLAY"] = re.search(r"WAYLAND_DISPLAY=(\S+)", text)[1]
        for name, pattern in (("PAW_SOCKET", r"Control socket: (\S+)"),
                              ("DISPLAY", r"XWayland listening on DISPLAY=(\S+)")):
            if found := re.search(pattern, text):
                self.env[name] = found[1]
        return self.server

    def stop(self):
        """Ends the compositor as a session does (SIGTERM) and checks that it exits cleanly and
        removes its sockets."""
        server, self.server = self.server, None
        server.terminate()
        code = server.wait(timeout=30)
        assert code == 0, f"the compositor exited with {code}"
        display = self.root / self.env["WAYLAND_DISPLAY"]
        assert not display.exists(), f"{display} left behind"
        if "PAW_SOCKET" in self.env:
            control = Path(self.env["PAW_SOCKET"])
            assert not control.exists(), f"{control} left behind"

    def run(self, *words, timeout=30):
        """`paw msg WORDS`, as a finished process."""
        return subprocess.run([self.binary, "msg", *words], env=self.env, capture_output=True,
                              text=True, timeout=timeout)

    def msg(self, *words, ok=True):
        """`paw msg WORDS`: what it prints, or with ok=False the error of a request that must
        be refused."""
        result = self.run(*words)
        assert (result.returncode == 0) == ok, (words, result.stdout, result.stderr)
        return result.stdout if ok else result.stderr

    def rows(self, request, *words):
        """The tab-separated fields of each line `get REQUEST` prints."""
        return [line.split("\t") for line in self.msg("get", request, *words).splitlines()]

    def reload(self, config=None):
        """Writes `config` (if given) to init.lua and reloads it; done when this returns."""
        if config is not None:
            self.config.write_text(config)
        self.msg("reload")

    def spawn(self, command, env=None, log=None, **options):
        """Starts a client of the compositor, ended on the way out. `env` adds to the
        compositor's environment. `log` names a file in the runtime directory that takes the
        client's output and is printed if the test fails; without one its standard output is
        discarded unless `options` (for subprocess.Popen) say otherwise."""
        if log is not None:
            path = self.root / log
            self.logs.append(path)
            with path.open("w") as output:
                options.setdefault("stdout", output)
                options.setdefault("stderr", subprocess.STDOUT)
                process = subprocess.Popen(command, env={**self.env, **(env or {})}, **options)
        else:
            options.setdefault("stdout", subprocess.DEVNULL)
            process = subprocess.Popen(command, env={**self.env, **(env or {})}, **options)
        self.clients.append(process)
        self._started.append(process)
        return process

    def reap(self, process, timeout=30):
        """Waits for a client that is to exit, stops watching it, and returns its exit code."""
        code = process.wait(timeout=timeout)
        self.clients.remove(process)
        return code

    def wait_for(self, predicate, message, timeout=15, detail=None):
        """harness.wait_for, failing too if any process of this test exits."""
        wait_for(predicate, self.processes, message, timeout, detail or self.detail)

    def stays(self, predicate, message, duration=.3, detail=None):
        """harness.stays, failing too if any process of this test exits."""
        stays(predicate, self.processes, message, duration, detail or self.detail)

    def virtual_pointer(self, probe, width, height):
        """Starts tests/pointer_probe for a layout of width x height. Returns a function that
        sends it a line of commands ("move X Y", "click left", "key alt down", ...) and waits
        until they are done; its `process` is the probe."""
        process = self.spawn([probe, str(width), str(height)], stdin=subprocess.PIPE,
                             stdout=subprocess.PIPE, text=True)
        assert process.stdout.readline().strip() == "ready"

        def pointer(*words):
            process.stdin.write(" ".join(words) + "\n")
            process.stdin.flush()
            assert process.stdout.readline().strip() == "done", words

        pointer.process = process
        return pointer

    def private_bus(self):
        """Starts a dbus-daemon of the test's own as the session bus, for the compositor and
        every client started after this."""
        conf = self.root / "bus.conf"
        conf.write_text(
            f'<busconfig><type>session</type><listen>unix:dir={self.root}</listen>'
            '<auth>EXTERNAL</auth><policy context="default"><allow send_destination="*" '
            'eavesdrop="true"/><allow eavesdrop="true"/><allow own="*"/></policy></busconfig>')
        self.bus = subprocess.Popen(["dbus-daemon", f"--config-file={conf}", "--nofork",
                                     "--print-address=1"], stdout=subprocess.PIPE, text=True)
        self._started.append(self.bus)
        address = self.bus.stdout.readline().strip()
        assert address.startswith("unix:"), address
        self.env["DBUS_SESSION_BUS_ADDRESS"] = address
        return address


class Shot:
    """A screenshot decoded from grim's PPM output."""

    def __init__(self, data):
        header, rest = data.split(b"\n255\n", 1)
        magic, size = header.split(b"\n", 1)
        assert magic == b"P6", magic
        self.width, self.height = (int(n) for n in size.split())
        self.pixels = rest
        assert len(rest) == 3 * self.width * self.height

    def at(self, x, y):
        i = 3 * (int(y) * self.width + int(x))
        return tuple(self.pixels[i:i + 3])


def grab(grim, env, output=None):
    """A Shot of the compositor's output, or None when grim is not installed (`grim` empty)."""
    if not grim:
        return None
    command = [grim, "-t", "ppm"] + (["-o", output] if output else []) + ["-"]
    return Shot(subprocess.run(command, env=env, capture_output=True, check=True,
                               timeout=20).stdout)
