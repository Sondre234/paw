# SPDX-License-Identifier: GPL-3.0-or-later
"""The blur behind windows (windows.blur): each output's background, here input_probe's wallpaper
(blue on the left half, orange on the right), blurred into a picture of half the output's pixels
once as it changes, and under each window the part of that picture the window covers, which
follows the window as it moves and stays whole under a translucent window. None under a
fullscreen window or one a rule leaves out, none at all by default, on and off with the profile,
and a rule can give one window a blur with the setting off. With grim, the wallpaper is sharp on
the screen and soft behind the window."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe, input_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])
grim = sys.argv[4] if len(sys.argv) > 4 else ""


def config(rule=""):
    return f"""return {{
    xwayland = false,
    animations = {{ enabled = false }},
    outputs = {{ monitors = {{ ["HEADLESS-1"] = {{ mode = "1280x720" }} }} }},
    windows = {{
        opacity = 0.5,
        blur = {{ radius = 40, noise = 0 }},
        rules = {{
            {{ title = "^one$", position = {{ x = 480, y = 200 }} }},
            {{ title = "^plain$", position = {{ x = 40, y = 40 }}, blur = false }},
            {{ title = "^edge$", position = {{ x = 1100, y = 400 }} }},
            {rule}
        }},
    }},
    profiles = {{
        glass = {{ windows = {{ blur = {{ enabled = true }} }} }},
        flat = {{ appearance = {{ background = "#101010" }} }},
    }},
}}"""


BODY = (0x41, 0x7b, 0xc4)  # what wayland_probe paints below its title bar
BLUE, ORANGE = (0x20, 0x50, 0xa0), (0xe0, 0xa0, 0x40)

with harness.Compositor(compositor, config()) as desktop:
    msg = desktop.msg

    def pictures():
        """By output: width, height, pictures taken, whether one is due."""
        return {r[1]: (int(r[2]), int(r[3]), int(r[4]), r[6] == "1")
                for r in desktop.rows("blur") if r[0] == "output"}

    def blurs():
        """By title: shown, the part of the layout covered, where it lies in the picture, and
        its opacity in thousandths."""
        return {r[1]: (r[2] == "1", tuple(int(n) for n in r[3:7]),
                       tuple(int(n) for n in r[7:11]), int(r[11]))
                for r in desktop.rows("blur") if r[0] == "window"}

    def window(title):
        return next(w for w in desktop.windows() if w.title == title)

    def under(title):
        """The blur the window should have where it rests: its box, and that halved."""
        box = window(title).box
        return True, box, tuple(n // 2 for n in box), 1000

    desktop.detail = lambda: msg("get", "blur") + msg("get", "windows")

    wallpaper = desktop.spawn([input_probe, "--background", "--commands", "wallpaper"],
                              stdin=subprocess.PIPE, log="wallpaper.log")
    desktop.wait_for(lambda: "ready" in (desktop.root / "wallpaper.log").read_text(),
                     "the wallpaper drawn")

    # Off by default: no picture, and no blur under a window.
    desktop.open_window(probe, "one")
    assert pictures() == {"HEADLESS-1": (0, 0, 0, False)}, pictures()
    desktop.stays(lambda: not blurs(), "no blur by default")

    # On in a profile: the background drawn at half its pixels, under the window where it is,
    # whole though the window is at half its opacity.
    msg("profile", "glass")
    desktop.wait_for(lambda: pictures()["HEADLESS-1"][:2] == (640, 360) and
                     not pictures()["HEADLESS-1"][3], "the background blurred")
    desktop.wait_for(lambda: blurs().get("one") == under("one"), "the blur under the window")
    assert window("one").box[:2] == (480, 200), window("one")

    if grim:
        # Behind the window over the wallpaper's edge, the blue and the orange run into each
        # other; around it the wallpaper stays sharp.
        one = window("one")
        shot = harness.grab(grim, desktop.env)
        assert shot.at(639, 100) == BLUE and shot.at(640, 100) == ORANGE, shot.at(639, 100)
        y = one.y + one.height // 2
        # The blur's colour, taking the window's own at half its opacity off each pixel.
        seen = [tuple(round((p - b / 2) * 2) for p, b in zip(shot.at(x, y), BODY))
                for x in range(one.x + 8, one.x + one.width - 8, 8)]
        assert all(abs(a - b) <= 8 for a, b in zip(seen[0], BLUE)), seen
        assert all(abs(a - b) <= 8 for a, b in zip(seen[-1], ORANGE)), seen
        steps = [max(abs(a - b) for a, b in zip(p, q)) for p, q in zip(seen, seen[1:])]
        assert max(steps) < 60, f"a sharp edge behind the window: {steps}"

    # It follows the window as an action moves it.
    msg("move_right")
    desktop.wait_for(lambda: window("one").x != 480, "the window moved")
    desktop.wait_for(lambda: blurs().get("one") == under("one"), "the blur follows it")

    # A wallpaper that changes is blurred again once it settles: three changes at once, one
    # picture.
    taken = pictures()["HEADLESS-1"][2]
    wallpaper.stdin.write(b"swap\nswap\nswap\n")
    wallpaper.stdin.flush()
    desktop.wait_for(lambda: pictures()["HEADLESS-1"][2] > taken and
                     not pictures()["HEADLESS-1"][3], "the new wallpaper blurred")
    desktop.stays(lambda: pictures()["HEADLESS-1"][2] == taken + 1, "blurred once")
    assert blurs()["one"] == under("one"), blurs()

    # A rule leaves a window out; a window hanging off the output shows the part on it.
    desktop.open_window(probe, "plain")
    desktop.open_window(probe, "edge")
    desktop.wait_for(lambda: "edge" in blurs(), "the blur under the edge window")
    edge = window("edge")
    inside = 1280 - edge.x
    assert inside < edge.width, edge
    assert blurs()["edge"] == (True, (edge.x, edge.y, inside, edge.height),
                               (edge.x // 2, edge.y // 2, inside // 2, edge.height // 2),
                               1000), blurs()
    assert "plain" not in blurs(), blurs()

    # None under a fullscreen window; it comes back as the window leaves fullscreen.
    msg("fullscreen")
    desktop.wait_for(lambda: "edge" not in blurs(), "no blur under a fullscreen window")
    msg("fullscreen")
    desktop.wait_for(lambda: blurs().get("edge", (False,))[0], "the blur back")

    # A profile without it takes the pictures and the blurs away.
    msg("profile", "flat")
    desktop.wait_for(lambda: not blurs() and pictures()["HEADLESS-1"][:2] == (0, 0),
                     "the blur gone with the profile")

    # A rule gives one window a blur with the setting off.
    desktop.reload(config('{ title = "^one$", blur = true },'))
    desktop.wait_for(lambda: list(blurs()) == ["one"] and blurs()["one"] == under("one"),
                     "the blur the rule gives")
print("blur_smoke passed")
