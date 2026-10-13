# SPDX-License-Identifier: GPL-3.0-or-later
"""The card of a workspace in a session: resting the pointer on a workspace of the taskbar's
indicator shows a card above it with a picture of the workspace, its windows drawn from the
pictures the shell copied from the compositor through the window control, where the compositor's
paw_workspace_v1 says they lie. The shell asks for the workspace and its window's picture halfway
into the delay, before the card's surface is made: WAYLAND_DEBUG prints the order. The window is on
a workspace not shown, so its colours on the screen can only be the card's. Moving onto another
workspace shows that one at once, and leaving takes it all away, the compositor's objects and
sources with it. Without grim the pixels are left out."""
from pathlib import Path
import re
import subprocess
import sys

import harness

compositor, shell, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:5])
grim = sys.argv[5] if len(sys.argv) > 5 else ""

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false, workspaces = 4 },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    notifications = { enabled = false },
    shell = { panel_height = 52, thumbnails = { delay = 600 } },
}"""
WIDTH, HEIGHT, BAR = 1280, 720, 52
BODY = (0x41, 0x7b, 0xc4)  # the probe's window below its top band

with harness.Compositor(compositor, CONFIG, start=False) as desktop:
    root, env = desktop.root, desktop.env
    env.update(QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software",
               QT_FORCE_STDERR_LOGGING="1", XDG_DATA_HOME=str(root), XDG_DATA_DIRS=str(root),
               XDG_STATE_HOME=str(root), DBUS_SESSION_BUS_ADDRESS="disabled:")
    desktop.start()
    shell_log = root / "shell-protocol.log"

    def log():
        return shell_log.read_text(errors="replace")

    def messages():
        """What the shell printed but the protocol."""
        return "\n".join(line for line in log().splitlines() if not line.startswith("["))

    def layers():
        return {(row[0], row[1]): row[2:4] for row in desktop.rows("layers")}

    def views():
        return desktop.rows("workspace_views")

    def body_pixels():
        """How many pixels above the bar have the window's colour, or None without grim."""
        shot = harness.grab(grim, env)
        if shot is None:
            return None
        return sum(1 for y in range(0, HEIGHT - BAR, 2) for x in range(0, WIDTH, 2)
                   if all(abs(a - b) <= 2 for a, b in zip(shot.at(x, y), BODY)))

    desktop.detail = lambda: (f"layers: {layers()}, views: {views()}, windows: {desktop.windows()}"
                              f"\n{messages()[-1500:]}")
    with shell_log.open("w") as output:
        desktop.spawn([shell, "--config", str(desktop.config)], env={"WAYLAND_DEBUG": "client"},
                      stdout=output, stderr=subprocess.STDOUT)
    desktop.wait_for(lambda: "paw surface rendered: paw taskbar" in log(), "the panel")
    # A window on the second workspace, which the output does not show.
    desktop.msg("workspace", "2")
    desktop.spawn([probe, "--window-only"])
    desktop.wait_for(lambda: len(desktop.windows()) == 1, "the window")
    desktop.msg("workspace", "1")
    desktop.wait_for(lambda: not desktop.windows()[0].visible, "the window out of sight")
    popover = ("paw-popover", "HEADLESS-1")
    assert popover not in layers(), layers()
    assert not body_pixels(), "the window's colour on the screen before any card"

    # Where the workspaces are on the bar, which fonts and widgets decide: the pointer moves
    # along the bar from its end until resting there has the shell ask for a workspace, a quarter
    # of the delay in.
    pointer = desktop.virtual_pointer(pointer_probe, WIDTH, HEIGHT)
    middle = HEIGHT - BAR // 2
    found = None
    for x in range(WIDTH - 10, WIDTH // 3, -12):
        pointer("move", str(x), str(middle))
        try:
            harness.wait_for(lambda: views(), desktop.processes, "a workspace asked for",
                             timeout=0.4)
        except harness.Timeout:
            continue
        found = x, int(views()[0][1])
        break
    assert found, "no workspace found on the bar"
    # From the one found to the second, a button (and the spacing) at a time.
    x, number = found
    while number != 2:
        x += 30 if number < 2 else -30
        pointer("move", str(x), str(middle))
        desktop.wait_for(lambda: views() and int(views()[0][1]) != number, "the next workspace")
        number = int(views()[0][1])
    # Away and back, resting there afresh.
    pointer("move", "640", "300")
    desktop.wait_for(lambda: not views() and popover not in layers(), "nothing asked for")
    start = len(log())
    pointer("move", str(x), str(middle))
    desktop.wait_for(lambda: layers().get(popover) == ["3", "1"], "the card in the popover")
    # The shell asked for the workspace and the picture of its window, and had a frame of it,
    # before it showed the card.
    protocol = log()[start:]
    asked = re.search(r'\.get_workspace\(new id paw_workspace_v1#\d+, "HEADLESS-1", 2\)', protocol)
    listed = re.search(r"paw_workspace_v1#\d+\.window\(", protocol)
    picture = re.search(r"\.get_scaled_capture_source\(", protocol)
    copied = re.search(r"ext_image_copy_capture_frame_v1#\d+\.ready\(", protocol)
    card = re.search(r'get_layer_surface\(.*"paw-popover"', protocol)
    assert asked and listed and picture and copied, \
        "the workspace or its window's picture not asked for: " + protocol[-3000:]
    # The window's picture is asked for once the workspace is (here before its list comes, as the
    # shell keeps the one it had), and copied after.
    assert asked.start() < picture.start() < copied.start(), \
        "the workspace and its window's picture not asked for in turn"
    # The popover's surface, made as the first card opens.
    assert not card or copied.start() < card.start(), "the window's picture not in ahead of the card"
    assert views() == [["HEADLESS-1", "2", "1"]], views()
    # The window's picture on the card: the probe's 320 x 240 window at the picture's scale, a
    # third of it; every other pixel of its body, at least.
    desktop.wait_for(lambda: (pixels := body_pixels()) is None or pixels > 1000,
                     "the window's picture on the card")
    assert not desktop.windows()[0].visible, desktop.windows()
    # Its source fits the box the card asks pictures of the workspace's windows in.
    sources = desktop.rows("pictures")
    assert len(sources) == 1 and sources[0][0] == "360x225" and sources[0][2] == "1", sources

    # Onto the first workspace: at once, its card empty, the second workspace and its window
    # let go once the card has faded over.
    pointer("move", str(x - 30), str(middle))
    desktop.wait_for(lambda: views() == [["HEADLESS-1", "1", "0"]] and not desktop.rows("pictures"),
                     "the first workspace's card, the second let go")
    desktop.wait_for(lambda: (pixels := body_pixels()) is None or pixels == 0,
                     "the window gone from the card")

    pointer("move", "640", "300")
    desktop.wait_for(lambda: popover not in layers(), "the card gone once the pointer left")
    desktop.wait_for(lambda: not views() and not desktop.rows("pictures"),
                     "the workspace and the pictures let go with the card")
    assert not body_pixels(), "the window's picture left on the screen"
    for message in ("ReferenceError", "TypeError", "is not defined", "Cannot read"):
        assert message not in messages(), messages()
print("workspace card: a workspace's window drawn on the card above its workspace, from its "
      "picture where the compositor says it lies" + ("" if grim else " (no grim: no pixels)"))
