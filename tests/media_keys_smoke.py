# SPDX-License-Identifier: GPL-3.0-or-later
"""The media keys: the default configuration binds XF86AudioPlay, XF86AudioPause, XF86AudioNext,
XF86AudioPrev and XF86AudioStop to the media actions, which hand them to the shell as "media
VERB" lines on the control socket's stream; `paw msg media_next` does the same. The shell's
part, sending them on to a player, is media_smoke's."""
from pathlib import Path
import socket
import sys

import harness

compositor, default_config = (str(Path(p).resolve()) for p in sys.argv[1:3])

# evdev key codes, which the evdev keymap gives these keysyms.
KEYS = {"XF86AudioPlay": (200, "play-pause"), "XF86AudioPause": (201, "play-pause"),
        "XF86AudioNext": (163, "next"), "XF86AudioPrev": (165, "previous"),
        "XF86AudioStop": (166, "stop")}
PLAY_PAUSE = 164  # the one key most keyboards have, XF86AudioPlay


class Subscriber:
    """The media lines the control socket's stream sent, as the shell reads them."""

    def __init__(self, desktop):
        self.socket = socket.socket(socket.AF_UNIX)
        self.socket.connect(desktop.env["PAW_SOCKET"])
        self.socket.sendall(b"subscribe\n")
        self.socket.settimeout(0.05)
        self.buffer = ""

    def read(self):
        """Everything heard so far."""
        try:
            while data := self.socket.recv(8192):
                self.buffer += data.decode()
        except socket.timeout:
            pass
        return self.buffer

    def heard(self):
        return [line[len("media "):] for line in self.read().splitlines()
                if line.startswith("media ")]


CONFIG = 'return { version = 1, extends = "default", xwayland = false }\n'

with harness.Compositor(compositor, CONFIG,
                        env={"PAW_DEFAULT_CONFIG": default_config}) as desktop:
    msg = desktop.msg
    subscriber = Subscriber(desktop)
    desktop.wait_for(lambda: "tiling " in subscriber.read(), "the first state",
                     detail=subscriber.read)
    msg("headless_keyboard", "add", "media")
    expected = []
    for name, (code, verb) in list(KEYS.items()) + [("XF86AudioPlay", (PLAY_PAUSE, "play-pause"))]:
        msg("headless_keyboard", "key", "media", str(code), "press")
        msg("headless_keyboard", "key", "media", str(code), "release")
        expected.append(verb)
        desktop.wait_for(lambda: subscriber.heard() == expected, f"{name} sent on",
                         detail=subscriber.heard)
    for action, verb in [("media_play_pause", "play-pause"), ("media_next", "next"),
                         ("media_previous", "previous"), ("media_stop", "stop")]:
        msg(action)
        expected.append(verb)
        desktop.wait_for(lambda: subscriber.heard() == expected, f"{action} sent on",
                         detail=subscriber.heard)
    # They take no argument.
    assert "takes no argument" in msg("media_next", "now", ok=False)
