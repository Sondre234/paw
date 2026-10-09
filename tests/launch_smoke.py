# SPDX-License-Identifier: GPL-3.0-or-later
"""Starting programs with stand-ins on a PATH the test controls: a program that cannot start is
reported to the caller and, as `spawn-error`, to the shell, whether a key binding or the control
socket asked for it; and the terminal action finds the configured terminal, else $TERMINAL, else
the first installed of a list."""
from pathlib import Path
import re
import socket
import sys

import harness

compositor, example = (str(Path(p).resolve()) for p in sys.argv[1:3])
LEFTMETA, Q = 125, 16  # evdev key codes

with harness.Compositor(compositor, start=False) as desktop:
    root, msg = desktop.root, desktop.msg
    tools = root / "bin"
    tools.mkdir()
    tool_log = root / "tools.log"
    tool_log.touch()

    def stand_in(name):
        """A program on PATH that records its name and arguments."""
        (tools / name).write_text(f'#!/bin/sh\necho "{name} $*" >> "$TOOL_LOG"\n')
        (tools / name).chmod(0o755)

    def calls():
        return tool_log.read_text().splitlines()

    desktop.env.update(PATH=str(tools), TOOL_LOG=str(tool_log), TERMINAL="my-terminal")

    def key(code, state):
        msg("headless_keyboard", "key", "one", str(code), state)

    def press(*codes):
        for code in codes:
            key(code, "press")
        for code in reversed(codes):
            key(code, "release")

    class Subscriber:
        """The control socket's stream, as the shell reads it."""

        def __init__(self):
            self.socket = socket.socket(socket.AF_UNIX)
            self.socket.connect(desktop.env["PAW_SOCKET"])
            self.socket.sendall(b"subscribe\n")
            self.socket.settimeout(0.05)
            self.buffer = ""

        def errors(self):
            try:
                while data := self.socket.recv(8192):
                    self.buffer += data.decode()
            except socket.timeout:
                pass
            return re.findall(r"^spawn-error (.*)$", self.buffer, re.M)

    desktop.detail = lambda: (calls(), desktop.log.read_text())
    wait_for = desktop.wait_for
    desktop.start(Path(example).read_text().replace("xwayland = true", "xwayland = false"))
    subscriber = Subscriber()
    msg("headless_keyboard", "add", "one")

    # The control socket: a program that is there starts; one that is not is an error for
    # the caller and for the panel.
    stand_in("editor")
    msg("spawn", "editor", "notes.txt")
    wait_for(lambda: calls() == ["editor notes.txt"], "the editor")
    missing = "no-such-program: No such file or directory"
    assert f"error: cannot launch {missing}" in msg("spawn", "no-such-program", "-v", ok=False)
    wait_for(lambda: subscriber.errors() == [f"Cannot launch {missing}"], "the panel's error")
    assert f"Cannot launch {missing}" in desktop.log.read_text()

    # The terminal action, with no terminal installed, from Super + Q and the control
    # socket: the error says what to do.
    advice = "o terminal installed: set terminal in the configuration, or install one of " \
        "kitty, foot, alacritty, wezterm, ghostty, konsole, gnome-terminal, xterm"
    press(LEFTMETA, Q)
    wait_for(lambda: len(subscriber.errors()) == 2, "Super + Q's error on the panel")
    assert subscriber.errors()[1] == "N" + advice, subscriber.errors()
    assert "$TERMINAL, my-terminal, is not installed" in desktop.log.read_text()
    assert "error: n" + advice in msg("terminal", ok=False)
    wait_for(lambda: len(subscriber.errors()) == 3, "the second error on the panel")

    # The first of the list that is installed, then $TERMINAL once it is.
    def opens(expected):
        tool_log.write_text("")
        msg("terminal")
        wait_for(lambda: calls() == [expected], f"{expected} opened")
    stand_in("xterm")
    opens("xterm ")
    tool_log.write_text("")
    press(LEFTMETA, Q)
    wait_for(lambda: calls() == ["xterm "], "xterm opened from Super + Q")
    stand_in("foot")
    opens("foot ")
    stand_in("my-terminal")
    opens("my-terminal ")

    # The configured terminal wins, with its arguments; one not installed is an error rather
    # than a reason to open another.
    desktop.reload(desktop.config.read_text().replace(
        "    version = 1,\n", '    version = 1,\n    terminal = { "kitty", "-1" },\n', 1))
    assert "error: cannot launch kitty: No such file or directory" in msg("terminal", ok=False)
    stand_in("kitty")
    opens("kitty -1")
print("Programs and terminals start, and those that cannot are reported")
