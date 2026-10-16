# SPDX-License-Identifier: GPL-3.0-or-later
"""The cursor trail (appearance.cursor_trail, the cursor_trail action): off, a moving pointer drops
nothing; on, a moving pointer drops particles along its way, more the further it goes, and once it
rests they fade away and nothing more is drawn for them; a jump drops none. A fullscreen window, the
lock and a screenshot (`cursor hide`) take them away and let none fall meanwhile. The action goes
through the kinds, turns the trail off and back on, from a binding too, and a profile's kind comes
in as it is picked. With grim, the screen shows the sparkles behind the pointer."""
from pathlib import Path
import sys

import harness

compositor, probe, pointer_probe, lock_probe = (str(Path(p).resolve()) for p in sys.argv[1:5])
grim = sys.argv[5] if len(sys.argv) > 5 else ""

CONFIG = """return {{
    xwayland = false,
    animations = {{ enabled = false }},
    appearance = {{ background = "#000000", cursor_trail = {{ kind = "{kind}" }} }},
    bindings = {{ {{ key = "F3", action = "cursor_trail", mode = "next" }} }},
    profiles = {{
        cute = {{ appearance = {{ cursor_trail = {{ kind = "hearts", amount = 2 }} }} }},
        plain = {{}},
    }},
}}"""
F3 = 61  # evdev key code
WIDTH, HEIGHT = 1280, 720


def moves(x0, x1, y=360, step=12, pause=0):
    """Lines for pointer_probe moving from x0 to x1 along y in steps of `step` pixels, `pause` ms
    apart: a dozen moves a line, as the probe reads at most 64 words and 511 bytes of one."""
    sign = 1 if x1 >= x0 else -1
    words = [f"move {x} {y}" + (f" sleep {pause}" if pause else "")
             for x in range(x0, x1 + sign, sign * step)]
    return [" ".join(words[i:i + 12]) for i in range(0, len(words), 12)]


with harness.Compositor(compositor, CONFIG.format(kind="none")) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def trail():
        """`get cursor_trail`'s line, as a dict."""
        rows = desktop.rows("cursor_trail")
        assert len(rows) == 1 and len(rows[0]) == 3, rows
        return dict(zip(("kind", "count", "state"), rows[0]))

    def particles():
        """The particles drawn: x, y, width, height, opacity in thousandths, picture, transform."""
        return [[int(field) for field in row] for row in desktop.rows("cursor_trail", "particles")]

    def count():
        return int(trail()["count"])

    def frames():
        return int(desktop.rows("output_frames")[0][1])

    desktop.detail = lambda: f"{trail()} {particles()[:4]}"
    pointer = desktop.virtual_pointer(pointer_probe, WIDTH, HEIGHT)

    def stroke(*arguments, **options):
        for line in moves(*arguments, **options):
            pointer(line)

    # Off: nothing falls however the pointer moves.
    assert trail() == {"kind": "none", "count": "0", "state": "off"}, trail()
    stroke(100, 700)
    desktop.stays(lambda: count() == 0 and particles() == [], "no trail while it is off")
    assert "sparkles" in msg("cursor_trail", "glitter", ok=False)
    assert "takes one word" in msg("cursor_trail", "hearts", "now", ok=False)
    assert "particles" in msg("get", "cursor_trail", "everything", ok=False)

    # On, the pointer resting: nothing yet.
    msg("cursor_trail", "sparkles")
    wait_for(lambda: trail() == {"kind": "sparkles", "count": "0", "state": "still"}, "on")

    # Moving: sparkles along the way, inside the area it went over, fading in and out.
    stroke(200, 800, pause=4)
    wait_for(lambda: trail()["state"] == "shown" and len(particles()) > 10, "sparkles fall")
    for x, y, width, height, opacity, picture, transform in particles():
        assert 150 < x < 850 and 300 < y < 420, (x, y)
        assert 1 <= width <= 26 and 1 <= height <= 26 and 0 < opacity <= 1000, (width, height)
        assert transform == 0, transform
    if grim:
        # Something bright behind the pointer on the black desktop.
        stroke(800, 300, step=10, pause=4)
        shot = harness.grab(grim, desktop.env)
        lit = sum(1 for px in range(250, 850) for py in range(330, 390)
                  if sum(shot.at(px, py)) > 200)
        assert lit > 30, lit

    # Resting, they fade away within about a second, and then nothing more is drawn.
    wait_for(lambda: trail() == {"kind": "sparkles", "count": "0", "state": "still"},
             "gone once the pointer rests", timeout=5)
    assert particles() == []
    before = frames()
    desktop.stays(lambda: frames() - before <= 2, "no frames for a trail at rest", duration=1)

    # More way, more particles: a short stroke drops fewer than a long one.
    stroke(400, 480, step=8)
    short = count()
    wait_for(lambda: count() == 0, "the short stroke's gone", timeout=5)
    stroke(100, 1100, step=8)
    long = count()
    assert long > 3 * short and short > 0, (short, long)
    wait_for(lambda: count() == 0, "the long stroke's gone", timeout=5)

    # A jump (a warp, or a single motion from far off) drops nothing along its way.
    pointer("move 50 50 move 1200 650")
    desktop.stays(lambda: count() == 0, "no line across the screen")

    # A screenshot (as `paw showcase` and the screenshot action hide the cursor) takes them away
    # at once and lets none fall until it is over.
    stroke(200, 800)
    assert count() > 0
    msg("cursor", "hide")
    wait_for(lambda: trail() == {"kind": "sparkles", "count": "0", "state": "screenshot"},
             "out of the screenshot")
    stroke(800, 200)
    assert count() == 0 and particles() == [], trail()
    msg("cursor", "show")
    stroke(200, 600)
    assert count() > 0

    # Over a fullscreen window none fall, and those there go.
    desktop.open_window(probe, "game", focused=True)
    msg("fullscreen")
    stroke(600, 300)
    wait_for(lambda: trail() == {"kind": "sparkles", "count": "0", "state": "fullscreen"},
             "none over a fullscreen window")
    stroke(300, 900)
    desktop.stays(lambda: count() == 0, "still none over it")
    msg("fullscreen")
    stroke(900, 300)
    wait_for(lambda: count() > 0, "back once it leaves fullscreen")

    # Nor while the session is locked.
    locker = desktop.spawn([lock_probe, "hold", str(desktop.root / "locker.log")])
    wait_for(lambda: trail()["state"] == "locked" and count() == 0, "none under the lock")
    stroke(300, 900)
    assert count() == 0, trail()
    locker.terminate()
    assert desktop.reap(locker) == 0
    wait_for(lambda: trail()["state"] == "still", "free again after the unlock")

    # The action: the next kind, off, back on as it was, and from a binding.
    msg("cursor_trail", "next")
    wait_for(lambda: trail()["kind"] == "petals", "petals")
    stroke(100, 900, pause=2)
    wait_for(lambda: count() > 5, "petals fall")
    assert any(transform for *_, transform in particles()) or len(particles()) < 8, particles()
    msg("cursor_trail")
    wait_for(lambda: trail() == {"kind": "none", "count": "0", "state": "off"},
             "toggled off, the petals gone at once")
    msg("cursor_trail", "toggle")
    wait_for(lambda: trail()["kind"] == "petals", "petals again")
    keys = desktop.keyboard()
    keys(F3)
    wait_for(lambda: trail()["kind"] == "hearts", "the binding brings hearts")
    stroke(100, 900)
    wait_for(lambda: count() > 5, "hearts float")
    msg("cursor_trail", "off")
    wait_for(lambda: trail()["state"] == "off", "off")

    # A profile with hearts brings them as appearance.cursor_trail.kind changes; one without goes
    # back to none. A reload keeps the action's choice while the kind configured stays.
    msg("profile", "cute")
    wait_for(lambda: trail()["kind"] == "hearts", "the profile's hearts")
    msg("profile", "plain")
    wait_for(lambda: trail()["state"] == "off", "none in the plain profile")
    msg("cursor_trail", "sparkles")
    desktop.reload()
    assert trail()["kind"] == "sparkles", trail()
    desktop.reload(CONFIG.format(kind="petals"))
    wait_for(lambda: trail()["kind"] == "petals", "the configured kind changed")
