# SPDX-License-Identifier: GPL-3.0-or-later
"""Actions the shell carries out reach it as lines on the control socket's stream. The media
keys: the default configuration binds XF86AudioPlay, XF86AudioPause, XF86AudioNext, XF86AudioPrev
and XF86AudioStop to the media actions, which hand them to the shell as "media VERB" lines; `paw
msg media_next` does the same. desktop_clock and desktop_icons send "desktop-clock toggle" and
"desktop-icons toggle", which show or hide the desktop's clock and icons (shell.desktop_clock,
shell.desktop_icons), and wallpaper_next and wallpaper_prev "wallpaper-cycle next" and
"wallpaper-cycle prev", which step its slideshow (shell.wallpaper_cycle). None takes an argument.
The shell's part is media_smoke's for the media keys and shell_ui's for the rest."""
from pathlib import Path
import sys

import harness

compositor, default_config = (str(Path(p).resolve()) for p in sys.argv[1:3])

# evdev key codes, which the evdev keymap gives these keysyms.
KEYS = {"XF86AudioPlay": (200, "play-pause"), "XF86AudioPause": (201, "play-pause"),
        "XF86AudioNext": (163, "next"), "XF86AudioPrev": (165, "previous"),
        "XF86AudioStop": (166, "stop")}
PLAY_PAUSE = 164  # the one key most keyboards have, XF86AudioPlay


CONFIG = 'return { version = 1, extends = "default", xwayland = false }\n'

with harness.Compositor(compositor, CONFIG,
                        env={"PAW_DEFAULT_CONFIG": default_config}) as desktop:
    msg = desktop.msg
    subscriber = desktop.subscribe()
    desktop.wait_for(lambda: "tiling " in subscriber.text(), "the first state",
                     detail=subscriber.text)

    def heard():
        """The media lines heard, as the shell reads them."""
        return subscriber.values("media ")

    msg("headless_keyboard", "add", "media")
    expected = []
    for name, (code, verb) in list(KEYS.items()) + [("XF86AudioPlay", (PLAY_PAUSE, "play-pause"))]:
        msg("headless_keyboard", "key", "media", str(code), "press")
        msg("headless_keyboard", "key", "media", str(code), "release")
        expected.append(verb)
        desktop.wait_for(lambda: heard() == expected, f"{name} sent on",
                         detail=heard)
    for action, verb in [("media_play_pause", "play-pause"), ("media_next", "next"),
                         ("media_previous", "previous"), ("media_stop", "stop")]:
        msg(action)
        expected.append(verb)
        desktop.wait_for(lambda: heard() == expected, f"{action} sent on",
                         detail=heard)
    # They take no argument.
    assert "takes no argument" in msg("media_next", "now", ok=False)

    # The desktop's clock, icons and slideshow, each a line of its own.
    for action, line in (("desktop_clock", "desktop-clock toggle"),
                         ("desktop_icons", "desktop-icons toggle"),
                         ("wallpaper_next", "wallpaper-cycle next"),
                         ("wallpaper_prev", "wallpaper-cycle prev")):
        prefix, verb = line.split(" ")
        before = subscriber.values(prefix + " ")
        msg(action)
        desktop.wait_for(lambda: subscriber.values(prefix + " ") == before + [verb],
                         f"{action} sent on", detail=lambda: subscriber.values(prefix + " "))
        assert "takes no argument" in msg(action, "on", ok=False)
print("The media keys and the desktop's actions reached the shell")
