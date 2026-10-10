# SPDX-License-Identifier: GPL-3.0-or-later
"""Night light: the compositor picks a colour temperature from the schedule (with the clock
fixed by PAW_NIGHT_LIGHT_TIME) and follows configuration and the toggle actions. The headless
backend has no gamma hardware and its screen capture is taken before the colour transform, so
what the screen shows needs a real output and is not covered here."""
from pathlib import Path
import socket
import sys

import harness

compositor = str(Path(sys.argv[1]).resolve())


def settings(extra):
    return f"""return {{
    xwayland = false,
    layout = {{ tiling = true, gap = 4 }},
    animations = {{ enabled = false }},
    night_light = {{ {extra} }},
}}"""


def state(msg):
    """(kelvin, override, enabled)"""
    return tuple(int(n) for n in msg("get", "night_light").splitlines()[0].split("\t"))


manual = ("enabled = true, night_temperature = 3400, sunrise = '07:00', sunset = '20:00', "
          "transition = 60")


class Subscriber:
    """The night light states the control socket's stream announced, as the shell reads them:
    "ACTIVE MODE"."""

    def __init__(self, desktop):
        self.socket = socket.socket(socket.AF_UNIX)
        self.socket.connect(desktop.env["PAW_SOCKET"])
        self.socket.sendall(b"subscribe\n")
        self.socket.settimeout(0.05)
        self.buffer = ""

    def heard(self):
        try:
            while data := self.socket.recv(8192):
                self.buffer += data.decode()
        except socket.timeout:
            pass
        return [line[len("night-light "):] for line in self.buffer.splitlines()
                if line.startswith("night-light ")]


def run(clock, body, extra=manual):
    """Runs `body` on a compositor with the clock fixed at `clock`."""
    with harness.Compositor(compositor, settings(extra),
                            env={"PAW_NIGHT_LIGHT_TIME": clock, "TZ": "UTC"}) as desktop:
        body(desktop)


def noon(desktop):
    msg = desktop.msg
    assert state(msg) == (6500, 0, 1), state(msg)
    subscriber = Subscriber(desktop)
    desktop.wait_for(lambda: subscriber.heard() == ["off auto"], "the first state",
                     detail=subscriber.heard)
    # Forcing night, neutral, and back to the schedule; subscribers (the shell's Quick Settings)
    # hear each.
    msg("night_light_on")
    assert state(msg) == (3400, 2, 1), state(msg)
    msg("night_light_toggle")
    assert state(msg) == (6500, 1, 1), state(msg)
    msg("night_light_toggle")
    assert state(msg)[:2] == (3400, 2), state(msg)
    msg("night_light_auto")
    assert state(msg) == (6500, 0, 1), state(msg)
    desktop.wait_for(lambda: subscriber.heard() == ["off auto", "on on", "off off", "on on",
                                                     "off auto"],
                     "subscribers told of each change", detail=subscriber.heard)


def midnight(desktop):
    msg = desktop.msg
    assert state(msg) == (3400, 0, 1), state(msg)
    # Reloading with the schedule off ends it; the toggle still works.
    desktop.reload(settings("enabled = false"))
    desktop.wait_for(lambda: state(msg) == (6500, 0, 0), "schedule off")
    msg("night_light_toggle")
    assert state(msg) == (3400, 2, 0), state(msg)


def dusk(desktop):
    # Sunset at 20:00 lasting an hour: at 20:00 the temperature is half way.
    k = state(desktop.msg)[0]
    assert abs(k - (6500 + 3400) / 2) <= 60, k


def expect(kelvin_check, message):
    def body(desktop):
        assert kelvin_check(state(desktop.msg)[0]), (message, state(desktop.msg))
    return body


run("12:00", noon)
run("23:00", midnight)
run("20:00", dusk)
run("22:00", expect(lambda k: k == 3400, "night after the transition"))
# A location gives the times: on the equator at the prime meridian (the clock runs in UTC)
# the sun is up at noon and down at midnight, whatever the date.
located = "enabled = true, latitude = 0, longitude = 0, transition = 10"
run("12:00", expect(lambda k: k == 6500, "day at noon"), located)
run("00:00", expect(lambda k: k == 3400, "night at midnight"), located)
print("Night light follows the schedule and overrides, and tells subscribers")
