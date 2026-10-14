# SPDX-License-Identifier: GPL-3.0-or-later
"""The toys only draw: shake, gravity and party each play (get toys) and draw windows off their
places (get drawn) while get windows says the same before, during and after, and each ends with
every window drawn where it is. A party colours the borders, a gradient one in one colour, and
throws confetti, all gone after, the gradient back to the pixel. A window closed, resized, made
fullscreen or taken by the pointer mid-toy drops out of it while the others play on; the workspace
changing and the session locking end it; a click brings gravity's windows back up early; with
animations off the toys still play; and `paw shake`, `paw gravity` and `paw party` send them."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, probe, lock_probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:5])
grim = sys.argv[5] if len(sys.argv) > 5 else ""

SCREEN = (1280, 720)
CONFIG = """return {
    xwayland = false,
    animations = { enabled = %s },
    layout = { tiling = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    windows = { border_width = 3, border_color = { "#ff0000", "#0000ff" },
                border_inactive_color = { "#00ff00", "#ff00ff" } },
}"""

with harness.Compositor(compositor, CONFIG % "true") as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def toys():
        """The toy line's fields (name, played, lasts, output, workspace), its windows by title
        (number, title, posed, x and y offset, stretch across and down, border colour), and the
        confetti showing."""
        toy, windows, confetti = None, {}, None
        for row in desktop.rows("toys"):
            if row[0] == "toy":
                toy = row[1:]
            elif row[0] == "window":
                windows[row[2]] = row[1:]
            elif row[0] == "confetti":
                confetti = int(row[1])
        return toy, windows, confetti

    def playing():
        return toys()[0][0]

    def boxes():
        return {w.title: w.box for w in desktop.windows()}

    def drawn():
        """By title: where its geometry is drawn and at what size."""
        return {r[1]: tuple(int(n) for n in r[2:6]) for r in desktop.rows("drawn")}

    def at_rest():
        return drawn() == boxes()

    def settled():
        """Nothing runs: no animation and no focus fade."""
        running, _, fades = msg("get", "animations").split()
        return running == "0" and fades == "0"

    def borders():
        """By title: how the border is drawn (none, color or gradient)."""
        return {r[1]: r[-1] for r in desktop.rows("frames")}

    def numbers():
        """By title: the window's number."""
        rows = desktop.rows("window_icons")
        return {w.title: int(r[0]) for w, r in zip(desktop.windows(), rows)}

    def offset(title):
        """The offset the toy draws the window at, or None while it does not draw it."""
        window = toys()[1].get(title)
        return (int(window[3]), int(window[4])) if window and window[2] == "1" else None

    def over():
        return playing() == "none" and at_rest()

    desktop.detail = lambda: f"toys: {desktop.rows('toys')}\ndrawn: {drawn()}\nwindows: {boxes()}"
    assert toys() == (["none", "0", "0", "-", "0"], {}, 0), toys()

    clients = {title: desktop.open_window(probe, title, focused=True)
               for title in ("one", "two", "three")}
    msg("snap_top_right")  # three, from the middle to the top right
    msg("overview_cancel")  # Snap Assist offers the other half
    wait_for(lambda: settled() and msg("get", "overview").startswith("closed"),
             "the windows settled")
    before = boxes()
    assert at_rest() and set(borders().values()) == {"gradient"}, borders()
    shot = harness.grab(grim, desktop.env)

    # A shake: the focused window, drawn to either side and back.
    msg("shake")
    toy, windows, _ = toys()
    assert toy[0] == "shake" and toy[2] == "400" and list(windows) == ["three"], toys()
    wait_for(lambda: playing() == "none" or offset("three") not in (None, (0, 0)),
             "three drawn off its place")
    assert boxes() == before, boxes()
    wait_for(over, "the shake over", timeout=5)
    assert boxes() == before, boxes()

    # By number; one that is not there shakes nothing; a number is all it takes.
    number = numbers()["one"]
    msg("shake", str(number))
    toy, windows, _ = toys()
    assert toy[0] == "shake" and list(windows) == ["one"] and windows["one"][0] == str(number)
    wait_for(over, "the shake over", timeout=5)
    msg("shake", "999")
    assert playing() == "none"
    assert "takes a window's number" in msg("shake", "one", ok=False)

    # Gravity: every window falls, the highest one far, and comes back up.
    msg("gravity")
    toy, windows, _ = toys()
    assert toy[0] == "gravity" and sorted(windows) == ["one", "three", "two"], toys()
    lasts = int(toy[2])
    assert 1500 < lasts < 4000, lasts
    wait_for(lambda: drawn()["three"][1] > before["three"][1] + 150, "three falling")
    assert boxes() == before, boxes()
    # Squashed or not, every window is drawn within the screen's width and above its bottom.
    for title, (x, y, width, height) in drawn().items():
        assert x >= -40 and x + width <= SCREEN[0] + 40 and y + height <= SCREEN[1], drawn()
    wait_for(over, "gravity over", timeout=10)
    assert boxes() == before, boxes()

    # A click while they are down sends them back up from there, sooner than they would go. It
    # lands where the window really is.
    pointer = desktop.virtual_pointer(pointer_probe, *SCREEN)
    x, y, width, height = before["three"]
    pointer("move", str(x + width // 2), str(y + height // 2))
    msg("gravity")
    lasts = int(toys()[0][2])
    wait_for(lambda: (offset("three") or (0, 0))[1] > 150, "three down")
    pointer("click", "left")
    toy = toys()[0]
    assert toy[0] == "gravity" and int(toy[2]) < lasts, (toy, lasts)
    wait_for(over, "gravity cut short", timeout=5)
    assert boxes() == before, boxes()
    assert next(w for w in desktop.windows() if w.focused).title == "three"

    # A party: the windows bob, their borders in rainbow colours, under confetti; then all back.
    msg("party")
    toy, windows, _ = toys()
    assert toy[0] == "party" and toy[2] == "4000" and len(windows) == 3, toys()
    wait_for(lambda: toys()[2] > 20, "confetti")
    colours = {w[7] for w in toys()[1].values()}
    assert len(colours) == 3 and all(c.startswith("#") for c in colours), colours
    assert set(borders().values()) == {"color"}, borders()
    wait_for(lambda: any(offset(t) not in (None, (0, 0)) for t in before), "the windows bob")
    assert boxes() == before, boxes()
    wait_for(over, "the party over", timeout=10)
    assert toys()[2] == 0 and set(borders().values()) == {"gradient"}, (toys(), borders())
    assert boxes() == before, boxes()
    if shot:
        # The borders as they were, to the pixel: the left side of each window's.
        after = harness.grab(grim, desktop.env)
        for title, (x, y, width, height) in before.items():
            assert after.at(x - 2, y + height // 2) == shot.at(x - 2, y + height // 2), title

    # A window closing mid-party goes, the others play on.
    msg("party")
    wait_for(lambda: offset("three") is not None, "three at the party")
    msg("close")  # three has the focus
    assert desktop.reap(clients.pop("three")) == 0
    wait_for(lambda: "three" not in boxes(), "three closed")
    toy, windows, _ = toys()
    assert toy[0] == "party" and sorted(windows) == ["one", "two"], toys()
    before.pop("three")
    # One resized drops out, back in its place; the other plays on.
    focused = next(w.title for w in desktop.windows() if w.focused)
    msg("resize_right", "40")
    wait_for(lambda: focused not in toys()[1], f"{focused} out of the party")
    assert playing() == "party" and len(toys()[1]) == 1, toys()
    wait_for(lambda: settled() and drawn()[focused] == boxes()[focused], f"{focused} at rest")
    before = boxes()
    # Another workspace ends it.
    msg("workspace", "2")
    wait_for(lambda: playing() == "none", "the party over with the workspace")
    msg("workspace", "1")
    wait_for(lambda: settled() and at_rest(), "back on the first workspace")
    assert boxes() == before, boxes()

    # Going fullscreen, or being taken by the pointer, lets a window go too.
    clients["mover"] = desktop.open_window(probe, "mover", focused=True,
                                           env={"PAW_PROBE_MOVE": "1"})
    wait_for(settled, "mover settled")
    msg("party")
    wait_for(lambda: offset("mover") is not None, "mover at the party")
    msg("fullscreen")  # mover has the focus
    wait_for(lambda: "mover" not in toys()[1], "mover out of the party")
    assert playing() == "party" and len(toys()[1]) == 2, toys()
    msg("fullscreen")
    wait_for(lambda: settled() and drawn()["mover"] == boxes()["mover"], "mover back")
    msg("party")
    wait_for(lambda: offset("mover") is not None, "mover at the party again")
    x, y, width, height = boxes()["mover"]
    pointer("move", str(x + 100), str(y + 20), "press", "left")  # it asks to be moved
    wait_for(lambda: "mover" not in toys()[1], "mover out of the party as it is taken")
    assert playing() == "party" and len(toys()[1]) == 2, toys()
    pointer("release", "left")
    msg("close")  # mover, focused by the press
    assert desktop.reap(clients.pop("mover")) == 0
    wait_for(lambda: "mover" not in boxes() and over(), "mover gone and the party over",
             timeout=10)
    assert boxes() == before, boxes()

    # The lock ends it at once.
    msg("gravity")
    wait_for(lambda: any((offset(t) or (0, 0))[1] > 50 for t in before), "the windows falling")
    log = desktop.root / "locker.log"
    log.touch()
    locker = desktop.spawn([lock_probe, "hold", str(log)])
    wait_for(lambda: "locked" in log.read_text(), "the lock held")
    assert playing() == "none" and at_rest(), toys()
    locker.terminate()
    assert desktop.reap(locker) == 0
    wait_for(lambda: "Session unlocked" in desktop.log.read_text(), "unlocked")
    assert boxes() == before, boxes()

    # Without animations a toy still plays.
    desktop.reload(CONFIG % "false")
    msg("party")
    wait_for(lambda: any(offset(t) not in (None, (0, 0)) for t in before), "the windows bob")
    msg("party")  # again: the first ends, the second begins
    assert int(toys()[0][1]) < 1000, toys()
    msg("shake")  # a shake ends the party
    assert playing() == "shake" and toys()[2] == 0, toys()
    wait_for(over, "the shake over", timeout=5)

    # paw shake, paw gravity and paw party send them.
    def paw(*words):
        return subprocess.run([compositor, *words], env=desktop.env, capture_output=True,
                              text=True, timeout=30)

    assert paw("gravity").returncode == 0 and playing() == "gravity", toys()
    assert paw("party").returncode == 0 and playing() == "party", toys()
    number = numbers()["one"]
    assert paw("shake", str(number)).returncode == 0, toys()
    assert playing() == "shake" and list(toys()[1]) == ["one"], toys()
    refused = paw("shake", "nine")
    assert refused.returncode == 1 and "window's number" in refused.stderr, refused
    refused = paw("party", "hard")
    assert refused.returncode == 1 and "usage: paw party" in refused.stderr, refused
    wait_for(over, "the last shake over", timeout=5)
    assert boxes() == before, boxes()

print("Shake, gravity and party drew, let go and ended cleanly")
