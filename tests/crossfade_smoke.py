# SPDX-License-Identifier: GPL-3.0-or-later
"""A new appearance profile crossfades in: each monitor keeps showing a picture of the old look
while the shell draws the new one, until it says so (`profile_drawn NAME`) or a second has passed
(three seconds here, time enough on a slow machine to look at it held), then the picture fades
out; without a shell it fades at once. The picture is what the monitor showed, to the pixel,
magnified or turned and scaled as it was; it takes no input, and the pointer moves on over it. A
switch during the fade starts again from what shows, the session locking takes the pictures away
at once, and nothing crossfades without animations, without a change of profile, on a monitor
that is off or over a fullscreen window. The pictures are read on a mirror of the first monitor."""
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
            ["HEADLESS-1"] = {{ mode = "1280x720" {first} }},
            ["HEADLESS-3"] = {{ mirror = "HEADLESS-1" }},
        }},
    }},
    windows = {{ rules = {{
        {{ title = "^big$", output = "HEADLESS-1", fullscreen = true }},
        {{ title = "^aside$", output = "HEADLESS-2" }},
        {{ title = "^corner$", output = "HEADLESS-1", floating = true, position = {{ 30, 50 }},
          size = {{ 200, 300 }} }},
    }} }},
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


def same(a, b):
    """Whether two pictures are alike to within a step of rounding, every channel of every fifth
    pixel."""
    if (a.width, a.height) != (b.width, b.height):
        return False
    return all(abs(a.pixels[i + c] - b.pixels[i + c]) <= 2
               for i in range(0, len(a.pixels), 15) for c in range(3))


def between(colour, a, b):
    """Whether `colour` is a mix of `a` and `b`, well clear of both."""
    return all(min(x, y) - 3 <= c <= max(x, y) + 3 for c, x, y in zip(colour, a, b)) and \
        not near(colour, a, 12) and not near(colour, b, 12)


def pictures(desktop):
    """output: (state, profile, opacity in thousandths) for each picture laid over one."""
    return {row[0]: (row[1], row[2], int(row[3])) for row in desktop.rows("crossfade")}


def shot(desktop):
    """The first monitor as its mirror last drew it."""
    path = desktop.root / "mirror.ppm"
    desktop.wait_for(lambda: desktop.run("headless_output", "capture", "HEADLESS-3",
                                         str(path)).returncode == 0, "the mirror drew a frame")
    return harness.Shot(path.read_bytes())


with harness.Compositor(compositor, start=False, env={"WLR_HEADLESS_OUTPUTS": "2", "PAW_TEST_CROSSFADE_HOLD_MS": "3000"}) as desktop:
    desktop.env["XDG_STATE_HOME"] = str(desktop.root / "state")
    desktop.start(CONFIG.format(animations="true", first=""))
    msg, wait_for, stays = desktop.msg, desktop.wait_for, desktop.stays

    def crossfade():
        return pictures(desktop)

    def colour():
        """The middle of the first monitor."""
        return shot(desktop).at(640, 360)

    def frames():
        return int(desktop.rows("stats")[0][0])

    desktop.detail = lambda: f"crossfade: {crossfade()}"
    msg("headless_output", "add", "HEADLESS-3", "1280x720")
    desktop.open_window(probe, "aside")
    wait_for(lambda: [w.output for w in desktop.windows()] == ["HEADLESS-2"], "a window aside")
    aside = desktop.windows()[0]
    middle = (str(aside.x + aside.width // 2), str(aside.y + aside.height // 2))
    assert desktop.rows("pid_at", *middle), "no window found under the pointer"
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
    # The pointer reaches what is under the picture.
    assert desktop.rows("pid_at", *middle), "the picture took the pointer"
    msg("profile_drawn", "blue")  # another profile's: it waits on
    assert crossfade()["HEADLESS-1"][0] == "held", crossfade()
    msg("profile_drawn", "green")
    assert crossfade()["HEADLESS-1"][:2] == ("fading", "green"), crossfade()
    wait_for(lambda: between(colour(), BLUE, GREEN), "the blue picture fading over the green")

    # A switch during the fade starts again from what shows: the mix there was, held for the
    # shell, which it waits for (here) three seconds at most.
    wait_for(lambda: crossfade().get("HEADLESS-1", ("", "", 1000))[2] < 700, "the fade half way")
    msg("profile", "red")
    held = colour()
    assert between(held, BLUE, GREEN), held
    assert crossfade()["HEADLESS-1"][:2] == ("held", "red"), crossfade()
    stays(lambda: near(colour(), held, 4), "the mix held")  # the mirror may be a frame behind
    wait_for(lambda: crossfade().get("HEADLESS-1", ("",))[0] == "fading",
             "the hold given up on the shell", timeout=6)
    assert "did not draw profile red in time" in desktop.log.read_text()
    wait_for(lambda: crossfade() == {} and near(colour(), RED), "the red background")

    # A magnified monitor's picture is magnified as the monitor was.
    msg("zoom_in")
    wait_for(lambda: desktop.rows("zoom") == [["1250", "1250", "1"]], "the monitor magnified")
    before = shot(desktop)
    msg("profile", "green")
    assert crossfade()["HEADLESS-1"][0] == "held", crossfade()
    stays(lambda: same(shot(desktop), before), "the magnified picture held")
    msg("profile_drawn", "green")
    wait_for(lambda: crossfade() == {} and near(colour(), GREEN), "the green background")
    msg("zoom_reset")
    wait_for(lambda: desktop.rows("zoom") == [["1000", "1000", "0"]], "the magnifier off")

    # The pointer is no part of the picture: it moves on over it, and leaves nothing behind.
    def cursor_at(picture, x, y):
        return any(not near(picture.at(x + dx, y + dy), GREEN) for dx in range(10) for dy in range(14))
    msg("headless_pointer", "add", "mouse")
    msg("headless_pointer", "move", "mouse", "300", "300")
    wait_for(lambda: cursor_at(shot(desktop), 300, 300), "the pointer shown")
    msg("profile", "blue")
    msg("headless_pointer", "move", "mouse", "900", "400")
    def moved():
        picture = shot(desktop)
        return cursor_at(picture, 900, 400) and not cursor_at(picture, 300, 300)
    wait_for(moved, "the pointer moved over the picture")  # the mirror draws it a frame later
    stays(moved, "the pointer moving over the picture alone")
    msg("profile_drawn", "blue")
    wait_for(lambda: crossfade() == {}, "the crossfade over")
    msg("headless_pointer", "remove", "mouse")

    # A monitor that is off gets no picture.
    msg("display_off", "HEADLESS-2")
    msg("profile", "red")
    assert set(crossfade()) == {"HEADLESS-1"}, crossfade()
    msg("profile_drawn", "red")
    wait_for(lambda: crossfade() == {}, "the crossfade over")
    msg("display_on", "HEADLESS-2")

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
    desktop.reload(CONFIG.format(animations="false", first=""))
    msg("profile", "red")
    assert crossfade() == {}, crossfade()

# A monitor turned and scaled holds a picture of itself that lies exactly where it showed.
with harness.Compositor(compositor, start=False, env={"WLR_HEADLESS_OUTPUTS": "2", "PAW_TEST_CROSSFADE_HOLD_MS": "3000"}) as desktop:
    desktop.env["XDG_STATE_HOME"] = str(desktop.root / "state")
    desktop.start(CONFIG.format(animations="true", first=", transform = 1, scale = 2"))
    desktop.msg("headless_output", "add", "HEADLESS-3", "1280x720")
    desktop.open_window(probe, "corner")
    desktop.wait_for(lambda: [w.box for w in desktop.windows()] == [(30, 50, 200, 300)],
                     "a window in the corner")
    desktop.wait_for(lambda: near(shot(desktop).at(640, 700), RED), "the red background")
    desktop.wait_for(lambda: desktop.rows("animations")[0][0] == "0", "the window opened")
    before = shot(desktop)
    shell = desktop.subscribe(shell=True)
    desktop.msg("profile", "blue")
    assert pictures(desktop)["HEADLESS-1"][0] == "held", pictures(desktop)
    desktop.stays(lambda: same(shot(desktop), before), "the turned picture held")
    desktop.msg("profile_drawn", "blue")
    desktop.wait_for(lambda: near(shot(desktop).at(640, 700), BLUE), "the blue background")
print("crossfade passed")
