# SPDX-License-Identifier: GPL-3.0-or-later
"""Ambient particles (appearance.ambient, the ambient action): turned on, sakura petals drift over
the output, inside it and about its edges; a fullscreen window pauses them, the output drawing no
frames for them meanwhile, and so does the lock; the action goes through the kinds, turns them off
(which takes them away) and back on, from a binding too; a profile setting another kind brings it
in, a reload leaves the action's choice unless the kind configured changes, and a new count
adds or takes particles away; without input for a while (PAW_TEST_AMBIENT_IDLE_MS) they fade out,
and input brings them back. With grim, the screen shows petals."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe, lock_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])
grim = sys.argv[4] if len(sys.argv) > 4 else ""

CONFIG = """return {{
    xwayland = false,
    animations = {{ enabled = false }},
    appearance = {{ background = "#203040", ambient = {{ kind = "{kind}", count = 12 }} }},
    bindings = {{ {{ key = "F3", action = "ambient", mode = "next" }} }},
    profiles = {{
        night = {{ appearance = {{ ambient = {{ kind = "fireflies" }} }} }},
        plain = {{}},
    }},
}}"""
F3 = 61  # evdev key code
WIDTH, HEIGHT = 1280, 720

with harness.Compositor(compositor, CONFIG.format(kind="none")) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def sky():
        """`get ambient`'s line for the output, as a dict."""
        rows = desktop.rows("ambient")
        assert len(rows) == 1 and len(rows[0]) == 4, rows
        return dict(zip(("output", "kind", "count", "state"), rows[0]))

    def particles():
        """The particles drawn: x, y, width, height, opacity in thousandths, picture, transform."""
        return [[int(field) for field in row] for row in desktop.rows("ambient", "HEADLESS-1")]

    def frames():
        return int(desktop.rows("output_frames")[0][1])

    desktop.detail = lambda: f"{sky()} {particles()[:4]}"

    assert sky() == {"output": "HEADLESS-1", "kind": "none", "count": "0", "state": "off"}, sky()
    assert particles() == []
    assert "sakura" in msg("ambient", "rain", ok=False)
    assert "takes one word" in msg("ambient", "snow", "now", ok=False)
    assert "no output" in msg("get", "ambient", "HDMI-9", ok=False)

    # On: a dozen petals over the output, fading in, inside it or just past its edges.
    msg("ambient", "sakura")
    wait_for(lambda: sky() == {"output": "HEADLESS-1", "kind": "sakura", "count": "12",
                               "state": "shown"}, "petals drift")
    wait_for(lambda: len(particles()) == 12 and min(p[4] for p in particles()) > 500,
             "every petal faded in")
    for x, y, width, height, opacity, picture, transform in particles():
        assert -80 < x < WIDTH + 80 and -150 < y < HEIGHT + 40, (x, y)
        assert 3 <= width <= 40 and 3 <= height <= 40 and 0 <= transform <= 3, (width, height)
    first = particles()
    wait_for(lambda: particles() != first, "the petals move")

    if grim:
        # Pink over the blue-grey background: (32, 48, 64) has none.
        shot = harness.grab(grim, desktop.env)
        pink = sum(1 for px in range(0, WIDTH, 2) for py in range(0, HEIGHT, 2)
                   if (lambda r, g, b: r > 90 and r > g + 30 and r > b)(*shot.at(px, py)))
        assert pink > 20, pink

    # A fullscreen window pauses them, and nothing of them draws a frame meanwhile.
    desktop.open_window(probe, "game", focused=True)
    msg("fullscreen")
    wait_for(lambda: sky()["state"] == "fullscreen", "paused behind a fullscreen window")
    assert particles() == [] and sky()["count"] == "12", sky()
    before = frames()
    desktop.stays(lambda: frames() - before <= 2, "no frames for the particles", duration=1)
    msg("fullscreen")
    wait_for(lambda: sky()["state"] == "shown", "shown again")

    # So does the lock.
    locker = desktop.spawn([lock_probe, "hold", str(desktop.root / "locker.log")])
    wait_for(lambda: sky()["state"] == "locked", "paused under the lock")
    locker.terminate()
    assert desktop.reap(locker) == 0
    wait_for(lambda: sky()["state"] == "shown", "shown after the unlock")

    # The next kind fades the petals out and the snow in; toggling takes the snow away and
    # brings it back.
    msg("ambient", "next")
    wait_for(lambda: sky()["kind"] == "snow" and sky()["state"] == "shown", "snow falls")
    msg("ambient")
    wait_for(lambda: sky() == {"output": "HEADLESS-1", "kind": "none", "count": "0",
                               "state": "off"}, "toggled off, the particles gone")
    assert particles() == []
    msg("ambient", "toggle")
    wait_for(lambda: sky()["kind"] == "snow" and sky()["state"] == "shown", "snow again")
    keys = desktop.keyboard()
    keys(F3)
    wait_for(lambda: sky()["kind"] == "fireflies", "the binding brings fireflies")
    msg("ambient", "off")
    wait_for(lambda: sky()["state"] == "off", "off")

    # A profile with fireflies brings them in, as appearance.ambient.kind changes; one without
    # goes back to none.
    msg("profile", "night")
    wait_for(lambda: sky()["kind"] == "fireflies" and sky()["state"] == "shown",
             "the profile's fireflies")
    msg("profile", "plain")
    wait_for(lambda: sky()["state"] == "off", "none in the plain profile")

    # A reload keeps the action's choice unless the kind configured changes.
    msg("ambient", "snow")
    wait_for(lambda: sky()["kind"] == "snow", "snow from the action")
    desktop.reload()
    assert sky()["kind"] == "snow", sky()
    desktop.reload(CONFIG.format(kind="sakura"))
    wait_for(lambda: sky()["kind"] == "sakura" and sky()["state"] == "shown",
             "the configured kind changed")
    # A new count adds petals, fading in, or takes some away.
    desktop.reload(CONFIG.format(kind="sakura").replace("count = 12", "count = 20"))
    wait_for(lambda: sky()["count"] == "20" and len(particles()) == 20, "twenty petals")
    desktop.reload(CONFIG.format(kind="sakura").replace("count = 12", "count = 5"))
    wait_for(lambda: sky()["count"] == "5" and len(particles()) == 5, "five petals")
    desktop.reload(CONFIG.format(kind="none"))
    wait_for(lambda: sky()["state"] == "off", "configured off")

# Without input for a while they fade out, drawing nothing more, and input brings them back.
with harness.Compositor(compositor, CONFIG.format(kind="snow"),
                        env={"PAW_TEST_AMBIENT_IDLE_MS": "3000"}) as desktop:
    def state():
        return desktop.rows("ambient")[0][3]

    keys = desktop.keyboard()
    keys(F3)  # input: snow on to fireflies, and the idle time starts again
    desktop.wait_for(lambda: desktop.rows("ambient")[0][1:] == ["fireflies", "12", "shown"],
                     "fireflies")
    desktop.wait_for(lambda: state() == "idle", "faded out for want of input", timeout=10)
    assert desktop.rows("ambient", "HEADLESS-1") == []
    keys(30)  # A, bound to nothing
    desktop.wait_for(lambda: desktop.rows("ambient")[0][1:] == ["fireflies", "12", "shown"],
                     "input brings them back")
print("Ambient particles drift, pause, switch, follow the profile and rest")
