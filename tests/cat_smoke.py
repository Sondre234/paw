# SPDX-License-Identifier: GPL-3.0-or-later
"""The desktop cat (`cat`, `paw cat`): let out, it turns up over the focused window and lands on its
top, taking no input there; it falls onto the window below as that window is minimized, and on to
the floor above the taskbar as that one is; nobody using the computer (PAW_TEST_CAT_SLEEP_MS), it
sleeps, drawing a frame only now and then, purrs in its sleep and sleeps on as the pointer strokes
it, and other input wakes it; a pointer flicked past it sets it running, and stroked awake it
purrs, sending up pink hearts; it hides behind a fullscreen window, under the overview and under
the lock; it pats the top of a window that asks for attention; and it goes as it is called in, by
`paw cat`, a binding or cat.enabled changing in a reload. With grim, the screen shows it."""
from pathlib import Path
import subprocess
import sys
import time

import harness

compositor, probe, lock_probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:5])
grim = sys.argv[5] if len(sys.argv) > 5 else ""

CONFIG = """return {{
    xwayland = false,
    animations = {{ enabled = false }},
    appearance = {{ background = "#4a6d8c" }},
    windows = {{ border_width = 2, rules = {{
        {{ title = "^back$", floating = true, position = {{ 80, 60 }}, size = {{ 1100, 560 }} }},
        {{ title = "^low$", floating = true, position = {{ 300, 420 }}, size = {{ 700, 160 }} }},
        {{ title = "^high$", floating = true, position = {{ 420, 200 }}, size = {{ 360, 140 }} }},
        {{ title = "^chat$", floating = true, position = {{ 860, 150 }}, size = {{ 300, 200 }} }},
    }} }},
    bindings = {{ {{ key = "F3", action = "cat", mode = "off" }} }},
    cat = {{ enabled = {enabled}, coat = "ginger" }},
}}"""
F3 = 61  # evdev key code

with harness.Compositor(compositor, CONFIG.format(enabled="false"),
                        env={"PAW_TEST_CAT_SLEEP_MS": "4000"}) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def cat():
        """`get cat` as a dict."""
        fields = desktop.rows("cat")[0]
        assert len(fields) == 9, fields
        keys = ("enabled", "drawn", "output", "x", "y", "state", "frame", "coat", "on")
        found = dict(zip(keys, fields))
        found["x"], found["y"] = int(found["x"]), int(found["y"])
        return found

    desktop.detail = lambda: str(cat())

    def paw_cat(*words):
        result = subprocess.run([compositor, "cat", *words], env=desktop.env, capture_output=True,
                                text=True, timeout=30)
        assert result.returncode == 0, result.stderr
        return result.stdout.strip()

    assert cat()["enabled"] == "off" and cat()["drawn"] == "off", cat()
    assert "takes one word" in msg("cat", "on", "now", ok=False)
    assert "toggle" in msg("cat", "maybe", ok=False)

    # The back window brings a 48-pixel taskbar along the bottom of the 1280 x 720 output.
    back = desktop.open_window(probe, "back", args=("--external-control",))
    desktop.open_window(probe, "low")
    desktop.open_window(probe, "high", focused=True)

    # Let out, it lands on the focused window's top border, at its middle.
    msg("cat", "on")
    wait_for(lambda: cat()["on"] == "high", "the cat lands on the focused window")
    found = cat()
    assert found["enabled"] == "on" and found["drawn"] == "shown", found
    assert found["output"] == "HEADLESS-1" and found["coat"] == "ginger", found
    assert found["y"] == 198 and 418 + 20 <= found["x"] <= 782 - 20, found

    # It takes no input: the window under it is what the pointer would reach there.
    assert int(msg("get", "pid_at", str(found["x"]), str(found["y"] - 12))) == back.pid

    if grim:
        # Ginger fur (and nothing else ginger here) fills much of its 40 x 32 box.
        shot = harness.grab(grim, desktop.env)
        x, y = found["x"], found["y"]
        ginger = sum(1 for px in range(x - 20, x + 20) for py in range(y - 32, y)
                     if (lambda r, g, b: r > 200 and 100 < g < 190 and b < 120)(*shot.at(px, py)))
        assert ginger > 150, ginger

    # The window it sits on minimized, it falls onto the top of the window below; that one
    # minimized too, on down to the top of the taskbar.
    msg("minimize")
    wait_for(lambda: cat()["on"] == "low", "the cat falls onto the window below")
    assert cat()["y"] == 418 and 300 <= cat()["x"] <= 1002, cat()
    msg("minimize")
    wait_for(lambda: cat()["on"] == "floor", "the cat falls to the floor")
    assert cat()["y"] == 672, cat()

    # Nobody has used the computer since it started: it sleeps, and the output draws only for its
    # breaths, one every 2.2 seconds, each change costing a headless output two frames.
    wait_for(lambda: cat()["state"] == "sleep", "the cat sleeps", timeout=30)
    assert cat()["frame"] in ("sleep_1", "sleep_2"), cat()

    def frames():
        return int(desktop.rows("output_frames")[0][1])

    before = frames()
    time.sleep(4.5)
    assert cat()["state"] == "sleep" and frames() - before <= 6, (frames() - before, cat())

    pointer = desktop.virtual_pointer(pointer_probe, 1280, 720)

    def stroke():
        """Strokes the cat where it is: the pointer in from its left, a moment's rest, then back
        and forth over its back, 30 pixels each way, a motion every 25 ms or so (ten to a line,
        which the probe takes 64 words at most of)."""
        found = cat()
        x, y = found["x"], found["y"] - 16
        pointer(f"move {max(x - 80, 0)} {y} sleep 400")
        sweep = [abs((5 * i + 30) % 60 - 30) - 15 for i in range(50)]
        for start in range(0, len(sweep), 10):
            pointer(*(f"move {x + offset} {y} sleep 25" for offset in sweep[start:start + 10]))

    # Stroked in its sleep, it purrs in its sleep: the input does not wake it, and it sleeps on
    # once the strokes stop.
    seen = set()

    def purring_asleep():
        stroke()
        seen.add(cat()["state"])
        return cat()["state"] == "purr"

    wait_for(purring_asleep, "the sleeping cat purrs as it is stroked")
    assert cat()["frame"] in ("sleep_1", "sleep_2"), cat()
    wait_for(lambda: seen.add(cat()["state"]) or cat()["state"] == "sleep", "it sleeps on")
    assert seen <= {"sleep", "purr"}, seen

    # Other input wakes it, and a flick of the pointer past it sets it running after the pointer.
    pointer("move 1200 100")
    wait_for(lambda: cat()["state"] in ("wake", "sit", "look", "walk", "groom", "scratch",
                                        "jump", "land", "yawn"), "the cat wakes")
    wait_for(lambda: cat()["state"] != "wake", "the cat is awake")
    x, y = cat()["x"], cat()["y"]
    pointer(" ".join(f"move {x - 150 + 30 * i} {y - 60}" for i in range(8)))
    wait_for(lambda: cat()["state"] in ("chase", "pounce", "fall"), "the cat chases the pointer")
    wait_for(lambda: cat()["on"] != "-", "the cat comes down again")

    # Stroked awake, it purrs with its eyes shut, sending up hearts over its ears, and stops two
    # seconds after the strokes do.
    calm = ("sit", "look", "walk", "groom", "scratch")
    wait_for(lambda: cat()["state"] in calm and (stroke() or cat()["state"] == "purr"),
             "the cat purrs as it is stroked")
    assert cat()["frame"] in ("purr_1", "purr_2"), cat()
    if grim:
        def hearts():
            """The pink pixels over the cat's ears, where its hearts float."""
            shot = harness.grab(grim, desktop.env)
            x, y = cat()["x"], cat()["y"]
            return sum(1 for px in range(x - 30, x + 30) for py in range(y - 64, y - 34)
                       if (lambda r, g, b: r > 180 and r - g > 60 and b > g)(*shot.at(px, py)))

        wait_for(lambda: hearts() >= 12, "hearts float up from the purring cat")
    wait_for(lambda: cat()["state"] != "purr", "the cat stops purring", timeout=5)
    assert cat()["state"] == "sit", cat()

    # A fullscreen window hides it, and so do the overview and the lock.
    msg("fullscreen")
    wait_for(lambda: cat()["drawn"] == "fullscreen", "hidden behind a fullscreen window")
    msg("fullscreen")
    wait_for(lambda: cat()["drawn"] == "shown", "shown again")
    msg("toggle_overview")
    wait_for(lambda: cat()["drawn"] == "overview", "hidden under the overview")
    msg("overview_cancel")
    wait_for(lambda: cat()["drawn"] == "shown", "shown as the overview closes")
    locker = desktop.spawn([lock_probe, "hold", str(desktop.root / "locker.log")])
    wait_for(lambda: cat()["drawn"] == "locked", "hidden under the lock")
    locker.terminate()
    assert desktop.reap(locker) == 0
    wait_for(lambda: cat()["drawn"] == "shown", "shown after the unlock")

    # A window asking for attention has it run over and pat its top until the window has the focus.
    chat = desktop.spawn([probe, "--commands"], env={"PAW_PROBE_TITLE": "chat"},
                         stdin=subprocess.PIPE, text=True)
    wait_for(lambda: any(w.title == "chat" and w.focused for w in desktop.windows()),
             "the chat window opens")
    msg("focus_last")
    chat.stdin.write("activate\n")
    chat.stdin.flush()
    wait_for(lambda: [w.title for w in desktop.windows("urgent")] == ["chat"], "chat is urgent")
    wait_for(lambda: cat()["state"] == "knock", "the cat paws at the urgent window")
    assert cat()["on"] == "chat" and cat()["y"] == 148, cat()
    assert cat()["frame"] in ("paw_1", "paw_2"), cat()
    msg("focus_urgent")
    wait_for(lambda: cat()["state"] != "knock", "the cat stops as the window has the focus")
    assert cat()["on"] == "chat", cat()

    # Called in and let out again by paw cat.
    assert paw_cat("off") == "The cat is away."
    assert cat()["enabled"] == "off" and cat()["drawn"] == "off", cat()
    assert paw_cat().startswith("The cat is out on HEADLESS-1")
    wait_for(lambda: cat()["on"] != "-", "the cat lands again")

    # A reload leaves it as the action put it, unless cat.enabled changes; its binding calls it in.
    desktop.reload()
    assert cat()["enabled"] == "on", cat()
    desktop.reload(CONFIG.format(enabled="true"))
    assert cat()["enabled"] == "on", cat()
    keys = desktop.keyboard()
    keys(F3)
    wait_for(lambda: cat()["enabled"] == "off", "the binding calls it in")
    desktop.reload(CONFIG.format(enabled="false"))
    assert cat()["enabled"] == "off", cat()
    desktop.reload(CONFIG.format(enabled="true"))
    assert cat()["enabled"] == "on", cat()
    wait_for(lambda: cat()["on"] != "-", "the cat lands once more")
print("The cat lands, falls, sleeps, wakes, chases, hides and goes as told")
