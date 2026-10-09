# SPDX-License-Identifier: GPL-3.0-or-later
"""Mirroring on headless outputs: a monitor with outputs.monitors' mirror leaves the layout (no
workspaces, windows or panels of its own) and shows its source's picture scaled to fit, with black
bars where the shapes differ, rotated with either of them, the cursor included; it follows its
source's power, joins the layout while its source is gone and leaves it again as it comes back,
holds a lock up as the others do, goes on mirroring when a wlr-output-management client applies
it where it is listed (on its source) and joins the layout moved elsewhere, and a reload without
mirror brings it into the layout. The
pictures are read with `headless_output capture`, and the source's with grim when it is
installed."""
from pathlib import Path
import shutil
import subprocess
import sys

import harness

compositor, probe, lock_probe, randr_probe = (str(Path(p).resolve()) for p in sys.argv[1:5])
grim = shutil.which("grim")

CONFIG = """return {{
    xwayland = false,
    animations = {{ enabled = false }},
    idle = {{ display_off = 0 }},
    outputs = {{
        order = {{ "HEADLESS-1", "HEADLESS-2" }},
        monitors = {{
            ["HEADLESS-1"] = {{ mode = "1280x720", transform = {source_transform} }},
            ["HEADLESS-3"] = {{ {mirror} transform = {mirror_transform} }},
        }},
    }},
    windows = {{ rules = {{ {{ title = "^big$", output = "HEADLESS-1", fullscreen = true }} }} }},
}}"""
BODY, BAND, BLACK = (0x41, 0x7b, 0xc4), (0x23, 0x31, 0x4a), (0, 0, 0)
BACKGROUND = (0x19, 0x21, 0x2e)  # appearance.background's default


def config(mirror='mirror = "HEADLESS-1",', source_transform=0, mirror_transform=0):
    return CONFIG.format(mirror=mirror, source_transform=source_transform,
                         mirror_transform=mirror_transform)


def near(colour, expected, slack=6):
    return all(abs(a - b) <= slack for a, b in zip(colour, expected))


with harness.Compositor(compositor, config(), env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def outputs():
        """name: (enabled, width, height, power, mirrored) per monitor."""
        return {r[0]: (r[1] == "1", int(r[4]), int(r[5]), r[10], r[11])
                for r in desktop.rows("outputs")}

    def capture(name="HEADLESS-3"):
        """The mirror's last frame, once it has drawn one since it started mirroring."""
        path = desktop.root / "mirror.ppm"
        wait_for(lambda: desktop.run("headless_output", "capture", name, str(path)).returncode == 0,
                 "the mirror drew a frame")
        return harness.Shot(path.read_bytes())

    def windows():
        return sorted((r[9], r[10]) for r in desktop.rows("windows"))

    desktop.detail = lambda: f"outputs: {outputs()}, windows: {windows()}"
    msg("headless_output", "add", "HEADLESS-3", "1024x768")
    wait_for(lambda: outputs().get("HEADLESS-3") == (False, 0, 0, "on", "HEADLESS-1"),
             "HEADLESS-3 mirrors HEADLESS-1")
    assert outputs()["HEADLESS-1"][:4] == (True, 1280, 720, "on"), outputs()
    assert sorted(r[0] for r in desktop.rows("workspaces")) == ["HEADLESS-1", "HEADLESS-2"]
    assert "HEADLESS-3 mirrors HEADLESS-1" in desktop.log.read_text()

    # The cursor on the source shows on the mirror too, scaled with the picture.
    msg("headless_pointer", "add", "mouse")
    msg("headless_pointer", "move", "mouse", "640", "400")

    def cursor_at(shot, x, y):
        return any(not near(shot.at(x + dx, y + dy), BACKGROUND)
                   for dx in range(0, 10) for dy in range(0, 14))
    wait_for(lambda: cursor_at(capture(), 512, 96 + 320), "the cursor shows on the mirror")
    msg("headless_pointer", "move", "mouse", "100", "600")
    wait_for(lambda: cursor_at(capture(), 80, 96 + 480) and not cursor_at(capture(), 512, 416),
             "the cursor moved on the mirror")
    shot = capture()
    assert near(shot.at(512, 384), BACKGROUND) and near(shot.at(512, 40), BLACK), \
        (shot.at(512, 384), shot.at(512, 40))

    # A window fills the source; the mirror shows it at 0.8 of its size between black bars.
    window = desktop.spawn([probe, "--window-only"], env={"PAW_PROBE_TITLE": "big"})
    wait_for(lambda: windows() == [("big", "HEADLESS-1")], "the window opened")

    def mirrored():
        shot = capture()
        return ((shot.width, shot.height) == (1024, 768) and near(shot.at(512, 40), BLACK) and
                near(shot.at(512, 110), BAND) and near(shot.at(512, 384), BODY) and
                near(shot.at(512, 700), BLACK))
    wait_for(mirrored, "the mirror shows the window, letterboxed")


    # Turning the source off turns its mirror off, and on with it.
    msg("display_off", "HEADLESS-1")
    wait_for(lambda: outputs()["HEADLESS-3"][3] == "off", "the mirror went off with its source")
    msg("display_on", "HEADLESS-1")
    wait_for(lambda: outputs()["HEADLESS-3"][3:] == ("on", "HEADLESS-1"), "the mirror came on")
    wait_for(mirrored, "the mirror shows the window again")

    # A lock holds once the mirror shows it too (a frame the source drew since it locked); the
    # control socket takes no capture while locked.
    log = desktop.root / "locker.log"
    log.touch()
    locker = desktop.spawn([lock_probe, "hold", str(log)])
    wait_for(lambda: "locked" in log.read_text(), "the lock held with a mirror")
    locker.terminate()
    assert desktop.reap(locker) == 0
    wait_for(mirrored, "the mirror shows the window once unlocked")

    # A wlr-output-management client lists the mirror on at its source's place; applied there, as
    # wdisplays applies every head, it goes on mirroring, and moved elsewhere it joins the layout.
    def randr(*args):
        result = subprocess.run([randr_probe, *args], env=desktop.env, capture_output=True,
                                text=True, timeout=30)
        assert result.returncode == 0, result.stderr
        return result.stdout.split("\n")[:-1]
    heads = {line.split()[0]: line.split()[1:4] for line in randr("list")}
    assert heads["HEADLESS-3"] == ["1", "0", "0"], heads
    assert randr("apply", "HEADLESS-3", "enabled=1", "x=0", "y=0") == ["succeeded"]
    assert outputs()["HEADLESS-3"] == (False, 0, 0, "on", "HEADLESS-1"), outputs()
    assert randr("apply", "HEADLESS-3", "enabled=1", "x=2560", "y=0") == ["succeeded"]
    wait_for(lambda: outputs()["HEADLESS-3"] == (True, 1024, 768, "on", "-"),
             "moved away, the mirror joined the layout")
    desktop.reload()
    wait_for(lambda: outputs()["HEADLESS-3"] == (False, 0, 0, "on", "HEADLESS-1"),
             "a reload made it a mirror again")
    wait_for(mirrored, "the mirror shows the window after the reload")

    # The source gone, the mirror joins the layout, with workspaces of its own; back, the mirror
    # leaves the layout again.
    window.terminate()
    desktop.reap(window)
    msg("headless_output", "remove", "HEADLESS-1")
    wait_for(lambda: outputs().get("HEADLESS-3") == (True, 1024, 768, "on", "-"),
             "the mirror joined the layout")
    assert sorted(r[0] for r in desktop.rows("workspaces")) == ["HEADLESS-2", "HEADLESS-3"]
    msg("headless_output", "add", "HEADLESS-1")
    wait_for(lambda: outputs().get("HEADLESS-3") == (False, 0, 0, "on", "HEADLESS-1"),
             "the mirror left the layout again")
    desktop.spawn([probe, "--window-only"], env={"PAW_PROBE_TITLE": "big"})
    wait_for(lambda: windows() == [("big", "HEADLESS-1")], "the window opened again")
    wait_for(mirrored, "the mirror shows the window after the source came back")

    # A rotated mirror shows the picture upright: across the middle of its portrait screen.
    desktop.reload(config(mirror_transform=1))
    # Its buffer stays 1024x768; seen rotated it is 768 by 1024, the picture 768x432 at y 296.

    def rotated():
        shot = capture()
        return ((shot.width, shot.height) == (1024, 768) and near(shot.at(100, 384), BLACK) and
                near(shot.at(305, 384), BAND) and near(shot.at(700, 384), BODY) and
                near(shot.at(900, 384), BLACK))
    wait_for(rotated, "the rotated mirror shows the picture upright")

    # A rotated source: the mirror shows it upright too, as grim sees it, in the middle.
    desktop.reload(config(source_transform=1))
    wait_for(lambda: outputs()["HEADLESS-1"][1:3] == (720, 1280), "the source turned")

    def upright():
        shot = capture()
        # Seen upright the source is 720 by 1280: on the mirror 432x768 from x 296.
        return (near(shot.at(200, 384), BLACK) and near(shot.at(512, 10), BAND) and
                near(shot.at(512, 384), BODY) and near(shot.at(800, 384), BLACK))
    wait_for(upright, "the mirror shows the rotated source upright")
    if grim:
        source = harness.grab(grim, desktop.env, "HEADLESS-1")
        mirror = capture()
        for x, y in ((360, 10), (360, 640), (40, 1200)):
            assert near(mirror.at(296 + x * 0.6, y * 0.6), source.at(x, y), 12), (x, y)

    # Without mirror in the configuration, it joins the layout.
    desktop.reload(config(mirror=""))
    wait_for(lambda: outputs()["HEADLESS-3"] == (True, 1024, 768, "on", "-"),
             "the monitor joined the layout")
    assert "HEADLESS-3 mirrors nothing" in desktop.log.read_text()
    assert "shows no mirror" in msg("headless_output", "capture", "HEADLESS-3",
                                    str(desktop.root / "none.ppm"), ok=False)
print("A mirror shows its source letterboxed, follows it, and leaves and joins the layout")
