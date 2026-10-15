# SPDX-License-Identifier: GPL-3.0-or-later
"""Frosted glass in a session (shell.blur), drawn through the GPU: the shell takes the wallpaper
as its desktop draws it and blurs it, once, and the bar shows it blurred under its colour where
it showed it sharp without the glass; the launcher's card, opaque without the glass, lets the
wallpaper's colour through, blurred, and the command palette, an overlay of its own, what lies
behind each end of it. Turned off by a reload, everything is as before, and drawn in
software (Qt's renderer, as shell.renderer = "software" has it) the glass is never drawn. On a
monitor at a scale other than 1, where the desktop reads its wallpaper again once it knows the
scale, the glass takes that picture too."""
from pathlib import Path
import re
import signal
import statistics
import struct
import sys
import zlib

import harness

compositor, shell = (str(Path(p).resolve()) for p in sys.argv[1:3])
grim = sys.argv[3] if len(sys.argv) > 3 else ""
if not grim:
    print("grim is missing: the glass was not looked at")
    sys.exit(0)

WIDTH, HEIGHT, BAR = 1280, 720, 52


def wallpaper(path):
    """Red on the left half, blue on the right, and black and white stripes four pixels wide
    across the bottom 120 pixels, where the bar is: sharp, they alternate; blurred, they are grey."""
    def pixel(x, y):
        if y >= HEIGHT - 120:
            return (255, 255, 255) if x // 4 % 2 else (0, 0, 0)
        return (220, 40, 40) if x < WIDTH // 2 else (40, 60, 220)
    rows = b"".join(b"\0" + bytes(c for x in range(WIDTH) for c in pixel(x, y))
                    for y in range(HEIGHT))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    path.write_bytes(b"\x89PNG\r\n\x1a\n"
                     + chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def config(root, blur, scale=1):
    return f"""return {{
    xwayland = false,
    animations = {{ enabled = false }},
    outputs = {{ monitors = {{ ["HEADLESS-1"] = {{ mode = "{WIDTH}x{HEIGHT}", scale = {scale} }} }} }},
    notifications = {{ enabled = false }},
    shell = {{
        wallpaper = [[{root / "wallpaper.png"}]],
        panel_color = "#20202080",
        panel_height = {BAR},
        blur = {{ enabled = {"true" if blur else "false"}, radius = 40 }},
    }},
}}"""


def luma(pixel):
    r, g, b = pixel
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


with harness.Compositor(compositor, "return {}", start=False) as desktop:
    root, env = desktop.root, desktop.env
    wallpaper(root / "wallpaper.png")
    desktop.config.write_text(config(root, True))
    # Qt's own renderer for Wayland: OpenGL, on Mesa's software implementation here.
    env.update(QT_QPA_PLATFORM="wayland", QT_FORCE_STDERR_LOGGING="1", XDG_DATA_HOME=str(root),
               XDG_DATA_DIRS=str(root), XDG_STATE_HOME=str(root),
               DBUS_SESSION_BUS_ADDRESS="disabled:")
    for name in ("QT_QUICK_BACKEND", "QSG_RHI_BACKEND"):
        env.pop(name, None)
    desktop.start()
    log = root / "shell.log"
    panel = desktop.spawn([shell, "--config", str(desktop.config)], log="shell.log")
    desktop.detail = lambda: log.read_text()[-3000:]
    desktop.wait_for(lambda: "paw surface rendered: paw taskbar" in log.read_text(), "the panel")
    assert "drawn on the GPU" in log.read_text(), log.read_text()

    def bar_spread():
        """How much the bar's colour varies along a row below its buttons, where nothing but the
        bar is drawn over the stripes."""
        shot = harness.grab(grim, env)
        return statistics.pstdev(luma(shot.at(x, HEIGHT - 2)) for x in range(300, 900))

    def card_red():
        """The launcher's card's average red, over the wallpaper's red half."""
        shot = harness.grab(grim, env)
        return statistics.mean(shot.at(x, y)[0] for x in range(100, 500, 7) for y in range(250, 550, 7))

    def launcher(opened):
        desktop.msg("launcher")
        word = "opened" if opened else "closed"
        desktop.wait_for(lambda: log.read_text().count(f"paw launcher {word} on HEADLESS-1") >= launcher.times[word] + 1,
                         f"the launcher {word}")
        launcher.times[word] += 1
    launcher.times = {"opened": 0, "closed": 0}

    # The wallpaper is blurred once it is drawn, and the bar shows it so: no stripes.
    desktop.wait_for(lambda: "paw glass blurred HEADLESS-1" in log.read_text(), "the wallpaper blurred")
    desktop.wait_for(lambda: bar_spread() < 8, "the stripes blurred under the bar")
    launcher(True)
    desktop.wait_for(lambda: "paw popover shown on HEADLESS-1" in log.read_text(), "the popover")
    glass_red = None

    def reddened():
        """The card drawn over the wallpaper's red (220 bare), its own colour (32) three quarters
        opaque over the red blurred."""
        global glass_red
        glass_red = card_red()
        return 60 < glass_red < 140
    desktop.wait_for(reddened, "the wallpaper's red through the launcher's card")
    launcher(False)

    # The command palette, an overlay of its own in the middle of the output, shows what lies
    # behind each part of it: red on its left, blue on its right.
    def palette_ends():
        """How much redder than blue the palette's card is left of the output's middle, and how
        much bluer than red right of it."""
        shot = harness.grab(grim, env)

        def tint(left):
            # Beside its rows' text, short of the middle by three of the blur's deviations.
            xs = range(500, 580, 3) if left else range(700, 860, 3)
            pixels = [shot.at(x, y) for x in xs for y in range(300, 500, 3)]
            red, blue = (statistics.mean(p[i] for p in pixels) for i in (0, 2))
            return red - blue if left else blue - red
        return tint(True), tint(False)
    # Bare, the wallpaper's halves differ by 180; under a card a quarter of that shows.
    key = desktop.keyboard()
    desktop.msg("palette")
    desktop.wait_for(lambda: "paw palette shown on HEADLESS-1" in log.read_text(), "the palette")
    desktop.wait_for(lambda: all(20 < end < 100 for end in palette_ends()),
                     "the red half behind the palette's left and the blue half behind its right")
    key(1)  # Escape
    desktop.wait_for(lambda: "paw palette hidden on HEADLESS-1" in log.read_text(), "the palette gone")

    # Turned off, the stripes show through the bar sharp, and the card is opaque.
    desktop.config.write_text(config(root, False))
    desktop.reload(desktop.config.read_text())
    panel.send_signal(signal.SIGHUP)
    desktop.wait_for(lambda: bar_spread() > 30, "the stripes sharp under the bar without the glass")
    launcher(True)
    desktop.wait_for(lambda: card_red() < 45, "the launcher's card opaque without the glass")
    launcher(False)
    # Nothing the glass drew on the way had QML complain.
    warnings = [line for line in log.read_text().splitlines()
                if re.search(r"\.qml:\d+|ReferenceError|TypeError|Binding loop", line)]
    assert not warnings, "\n".join(warnings)
    panel.terminate()
    desktop.reap(panel)

    # Drawn in software, the glass is never drawn, on or not.
    desktop.config.write_text(config(root, True))
    desktop.reload(desktop.config.read_text())
    env["QT_QUICK_BACKEND"] = "software"
    panel = desktop.spawn([shell, "--config", str(desktop.config)], log="shell.log")
    desktop.wait_for(lambda: "paw surface rendered: paw taskbar" in log.read_text(), "the panel")
    assert "drawn in software" in log.read_text(), log.read_text()
    desktop.wait_for(lambda: bar_spread() > 30, "the stripes sharp under the bar drawn in software")
    assert "paw glass blurred" not in log.read_text(), log.read_text()
    panel.terminate()
    desktop.reap(panel)

    # At scale 1.25 the desktop reads its wallpaper again as its window learns the scale, after it
    # has shown it; the glass is taken from that picture, and the bar blurs the stripes.
    del env["QT_QUICK_BACKEND"]
    desktop.config.write_text(config(root, True, scale=1.25))
    desktop.reload(desktop.config.read_text())
    log.write_text("")
    desktop.spawn([shell, "--config", str(desktop.config)], log="shell.log")
    desktop.wait_for(lambda: "paw glass blurred HEADLESS-1" in log.read_text(),
                     "the wallpaper blurred at scale 1.25")
    desktop.wait_for(lambda: bar_spread() < 8, "the stripes blurred under the bar at scale 1.25")
print("the glass blurs the wallpaper under the bar and a card, at scale 1 and 1.25, and stays out "
      "in software")
