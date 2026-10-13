# SPDX-License-Identifier: GPL-3.0-or-later
"""A new appearance profile crossfades in: each monitor keeps showing a picture of the old look
while the shell draws the new one, until it says so (`profile_drawn NAME`) or a second has passed,
then the picture fades out; without a shell it fades at once. A switch during the fade starts
again from what shows, the session locking takes the pictures away at once, and nothing
crossfades without animations, without a change of profile, or over a fullscreen window. The
pictures are read on a mirror of the first monitor."""
from pathlib import Path
import sys

import harness

compositor, probe, lock_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

# A speed of a quarter makes the fade 1.6 seconds, long enough to look at it on the way.
CONFIG = """return {{
    xwayland = false,
    animations = {{ enabled = {animations}, speed = 0.25 }},
    idle = {{ display_off = 0 }},
    outputs = {{
        order = {{ "HEADLESS-1", "HEADLESS-2" }},
        monitors = {{
            ["HEADLESS-1"] = {{ mode = "1280x720" }},
            ["HEADLESS-3"] = {{ mirror = "HEADLESS-1" }},
        }},
    }},
    windows = {{ rules = {{ {{ title = "^big$", output = "HEADLESS-1", fullscreen = true }} }} }},
    profile = "red",
    profiles = {{
        red = {{ appearance = {{ background = "#c03030" }} }},
        blue = {{ appearance = {{ background = "#3050c0" }} }},
        green = {{ appearance = {{ background = "#30a050" }} }},
    }},
}}"""
RED, BLUE, GREEN = (0xc0, 0x30, 0x30), (0x30, 0x50, 0xc0), (0x30, 0xa0, 0x50)


def near(colour, expected, slack=6):
    return all(abs(a - b) <= slack for a, b in zip(colour, expected))


def between(colour, a, b):
    """Whether `colour` is a mix of `a` and `b`, well clear of both."""
    return all(min(x, y) - 3 <= c <= max(x, y) + 3 for c, x, y in zip(colour, a, b)) and \
        not near(colour, a, 12) and not near(colour, b, 12)


with harness.Compositor(compositor, start=False, env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    desktop.env["XDG_STATE_HOME"] = str(desktop.root / "state")
    desktop.start(CONFIG.format(animations="true"))
    msg, wait_for, stays = desktop.msg, desktop.wait_for, desktop.stays

    def crossfade():
        """output: (state, profile, opacity in thousandths) for each picture laid over one."""
        return {row[0]: (row[1], row[2], int(row[3])) for row in desktop.rows("crossfade")}

    def colour():
        """The middle of the first monitor, as its mirror last drew it."""
        path = desktop.root / "mirror.ppm"
        wait_for(lambda: desktop.run("headless_output", "capture", "HEADLESS-3",
                                     str(path)).returncode == 0, "the mirror drew a frame")
        return harness.Shot(path.read_bytes()).at(640, 360)

    def frames():
        return int(desktop.rows("stats")[0][0])

    desktop.detail = lambda: f"crossfade: {crossfade()}"
    msg("headless_output", "add", "HEADLESS-3", "1280x720")
    wait_for(lambda: near(colour(), RED), "the mirror showing the red background")
    assert crossfade() == {}, crossfade()
    assert "usage: profile_drawn NAME" in msg("profile_drawn", ok=False)

    # Without a shell, the picture of the old look fades out at once over both monitors.
    msg("profile", "blue")
    assert set(crossfade()) == {"HEADLESS-1", "HEADLESS-2"}, crossfade()
    assert all(state == "fading" and profile == "blue" for state, profile, _ in
               crossfade().values()), crossfade()
    wait_for(lambda: between(colour(), RED, BLUE), "the red picture fading over the blue")
    wait_for(lambda: crossfade() == {}, "the crossfade over")
    wait_for(lambda: near(colour(), BLUE), "the blue background")
    drawn = frames()
    stays(lambda: frames() - drawn <= 2, "frames drawn once the crossfade is over",
          detail=lambda: f"{frames() - drawn} frames")

    # With a shell, the picture holds until the shell has drawn the profile it is for.
    shell = desktop.subscribe(shell=True)
    msg("profile", "green")
    assert all(state == "held" and profile == "green" and opacity == 1000 for state, profile,
               opacity in crossfade().values()), crossfade()
    drawn = frames()
    stays(lambda: near(colour(), BLUE) and crossfade()["HEADLESS-1"][0] == "held" and
          frames() - drawn <= 4, "the blue picture held, and nothing drawn, while the shell draws",
          detail=lambda: f"{frames() - drawn} frames, {colour()}, {crossfade()}")
    msg("profile_drawn", "blue")  # another profile's: it waits on
    assert crossfade()["HEADLESS-1"][0] == "held", crossfade()
    msg("profile_drawn", "green")
    assert crossfade()["HEADLESS-1"][:2] == ("fading", "green"), crossfade()
    wait_for(lambda: between(colour(), BLUE, GREEN), "the blue picture fading over the green")

    # A switch during the fade starts again from what shows: the mix there was, held for the
    # shell, which it waits for a second at most.
    wait_for(lambda: crossfade().get("HEADLESS-1", ("", "", 1000))[2] < 700, "the fade half way")
    msg("profile", "red")
    held = colour()
    assert between(held, BLUE, GREEN), held
    assert crossfade()["HEADLESS-1"][:2] == ("held", "red"), crossfade()
    stays(lambda: near(colour(), held, 2), "the mix held")
    wait_for(lambda: crossfade().get("HEADLESS-1", ("",))[0] == "fading",
             "the hold given up on the shell", timeout=3)
    assert "did not draw profile red in time" in desktop.log.read_text()
    wait_for(lambda: crossfade() == {} and near(colour(), RED), "the red background")

    # Locking the session takes the pictures away at once.
    msg("profile", "blue")
    assert crossfade(), crossfade()
    log = desktop.root / "locker.log"
    log.touch()
    locker = desktop.spawn([lock_probe, "hold", str(log)])
    wait_for(lambda: "locked" in log.read_text(), "the lock held")
    assert crossfade() == {}, crossfade()
    locker.terminate()
    assert desktop.reap(locker) == 0
    shell.close()

    # A fullscreen window shows nothing of the look: its monitor gets no picture.
    desktop.open_window(probe, "big")
    wait_for(lambda: any(w.title == "big" and w.box == (0, 0, 1280, 720)
                         for w in desktop.windows()), "the window fullscreen")
    msg("profile", "green")
    assert set(crossfade()) == {"HEADLESS-2"}, crossfade()
    wait_for(lambda: crossfade() == {}, "the crossfade over")

    # A reload that keeps the profile changes nothing at once, and nor does one without
    # animations.
    desktop.reload()
    assert crossfade() == {}, crossfade()
    desktop.reload(CONFIG.format(animations="false"))
    msg("profile", "red")
    assert crossfade() == {}, crossfade()
print("crossfade passed")
