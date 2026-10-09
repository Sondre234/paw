# SPDX-License-Identifier: GPL-3.0-or-later
"""The system tray end to end: a private session bus (a dbus-daemon this test starts and kills), a
headless compositor with two outputs and the shell, and tray_probe playing the applications. An
item's icon shows on every monitor's panel, clicks and the wheel reach the application, its menu
opens and an entry picked reaches it, an item registered by object path (as Ayatana's are) comes
after, items that go passive or quit leave, and shell.widgets.tray = false lets go of the bus
names. The shell never sees the real session bus."""
import os
from pathlib import Path
import re
import select
import shutil
import signal
import subprocess
import sys
import time

import harness

compositor, shell, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:5])
grim = shutil.which("grim")
if not shutil.which("dbus-daemon"):
    print("dbus-daemon not found: the tray was not driven")
    sys.exit(0)

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false, workspaces = 2 },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" }, ["HEADLESS-2"] = { mode = "1280x720" } } },
    notifications = { enabled = false },
}"""
SCREEN = (2560, 720)
PANEL_HEIGHT = 52
PANEL = (21, 30, 44)  # shell.panel_color
MAGENTA, GREEN, CYAN = (255, 0, 255), (0, 255, 0), (0, 255, 255)


class Lines:
    """A process's output, a line at a time, read from the pipe itself: a buffered reader would
    hold lines select() no longer reports."""

    def __init__(self, process):
        self.process, self.partial, self.lines = process, b"", []

    def pump(self, timeout):
        ready, _, _ = select.select([self.process.stdout], [], [], timeout)
        if ready:
            self.partial += os.read(self.process.stdout.fileno(), 4096)
            *lines, self.partial = self.partial.split(b"\n")
            self.lines.extend(line.decode().strip() for line in lines)

    def expect(self, pattern, timeout=6):
        """The first line matching `pattern` (a regular expression), taken from the list."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for line in self.lines:
                match = re.fullmatch(pattern, line)
                if match:
                    self.lines.remove(line)
                    return match
            self.pump(0.05)
        raise AssertionError(f"no line like '{pattern}'; saw {self.lines}")


with harness.Compositor(compositor, CONFIG, bus=True, start=False) as desktop:
    root, env = desktop.root, desktop.env
    env.update(QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software",
               QT_FORCE_STDERR_LOGGING="1", XDG_DATA_HOME=str(root), XDG_DATA_DIRS=str(root),
               XDG_STATE_HOME=str(root), WLR_HEADLESS_OUTPUTS="2")
    shell_log = root / "shell.log"

    def log():
        return shell_log.read_text()

    def wait_for(predicate, message, timeout=8):
        desktop.wait_for(predicate, message, timeout=timeout)

    def start_probe(*options):
        process = desktop.spawn([probe, *options], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        lines = Lines(process)
        lines.expect(r"registered \S+ /\S+")
        return process, lines

    def command(process, lines, line):
        process.stdin.write((line + "\n").encode())
        process.stdin.flush()
        lines.expect("ok")

    def icons(output, color, tolerance=12):
        """The centres of the runs of `color` along the middle of `output`'s panel, in the
        layout's coordinates, left to right; [] without grim."""
        shot = harness.grab(grim, env, output)
        x0, y0 = origins[output]
        y = shot.height - PANEL_HEIGHT // 2
        found, start = [], None
        for x in range(shot.width + 1):
            pixel = shot.at(x, y) if x < shot.width else (-100, -100, -100)
            close = all(abs(a - b) <= tolerance for a, b in zip(pixel, color))
            if close and start is None:
                start = x
            elif not close and start is not None:
                if x - start >= 6:
                    found.append((x0 + (start + x - 1) // 2, y0 + y))
                start = None
        return found

    desktop.start()
    origins = {}

    def outputs():
        for name, _, x, y, *_ in desktop.rows("outputs"):
            origins[name] = (int(x), int(y))
        return sorted(origins) == ["HEADLESS-1", "HEADLESS-2"]

    wait_for(outputs, "both outputs")
    panels = desktop.spawn([shell, "--config", str(desktop.config)], log="shell.log")
    desktop.detail = lambda: log()[-800:]
    wait_for(lambda: "paw tray: serving org.kde.StatusNotifierWatcher" in log(),
             "the shell serving the watcher")
    wait_for(lambda: log().count("paw surface rendered: paw taskbar") >= 2,
             "both panels drawn")

    # An item registered by name: its icon (the probe's pixels) on both panels.
    first, first_out = start_probe("--color", "#ff00ff", "--title", "First")
    if grim:
        wait_for(lambda: icons("HEADLESS-1", MAGENTA) and icons("HEADLESS-2", MAGENTA),
                 "the icon on both panels")
        (x, y), = icons("HEADLESS-2", MAGENTA)
    else:
        x, y = None, None
    # The probe answers each layout request; the menu was read when the item came.
    first_out.expect("layout 0")

    pointer = desktop.virtual_pointer(pointer_probe, *SCREEN)

    def approach(x, y):
        """Moves the pointer onto x, y in a few steps from just below it. The compositor
        tells a surface where the pointer is only when it moves, not when the surface grows
        under it, as the panel's does for a menu."""
        for step in (12, 8, 4, 0):
            pointer("move", str(x), str(y + step))
            time.sleep(0.05)
        time.sleep(0.2)

    def panel_at(x, y):
        """Whether the panel's colour is drawn at x, y of HEADLESS-2, as a menu is."""
        return all(abs(a - b) <= 8 for a, b in zip(harness.grab(grim, env, "HEADLESS-2").at(x, y), PANEL))

    if grim:
        # Clicks reach the application, with the icon's place on the screen.
        approach(x, y)
        pointer("click", "left")
        match = first_out.expect(r"activate (-?\d+) (-?\d+)")
        assert abs(int(match[1]) - x) <= 3 and abs(int(match[2]) - y) <= 3, (match[0], x, y)
        pointer("click", "middle")
        first_out.expect(r"secondary -?\d+ -?\d+")
        pointer("scroll", "15")
        first_out.expect(r"scroll -?\d+ vertical")

        # A right click opens the menu above the icon; the application hears of it. Its
        # last entry ("Quit", id 6) is the 36 pixel row just above the menu's bottom edge, inside
        # its 4 pixels of padding, 8 pixels over the bar.
        pointer("click", "right")
        first_out.expect("abouttoshow 0")
        first_out.expect("event 0 opened")
        bar_top = y - PANEL_HEIGHT // 2
        quit_row = bar_top - 8 - 4 - 36 // 2
        wait_for(lambda: panel_at(x, quit_row), "the menu drawn")
        approach(x, quit_row)
        pointer("click", "left")
        first_out.expect("event 6 clicked")
        first_out.expect("event 0 closed")

        # A new icon is drawn; passive, the item leaves the panels, and comes back.
        command(first, first_out, "color #00ff00")
        wait_for(lambda: icons("HEADLESS-2", GREEN) and not icons("HEADLESS-2", MAGENTA),
                 "the new icon")
        command(first, first_out, "status Passive")
        wait_for(lambda: not icons("HEADLESS-2", GREEN), "the passive item hidden")
        command(first, first_out, "status Active")
        wait_for(lambda: icons("HEADLESS-2", GREEN), "the item back")

    # An item registered by path, as Ayatana's are, without Activate: after the first,
    # and a left click opens its menu.
    second, second_out = start_probe("--path", "--no-activate", "--color", "#00ffff")
    second_out.expect("layout 0")
    if grim:
        wait_for(lambda: icons("HEADLESS-2", CYAN), "the second icon")
        (cx, cy), = icons("HEADLESS-2", CYAN)
        (gx, _), = icons("HEADLESS-2", GREEN)
        assert gx < cx, (gx, cx)
        approach(cx, cy)
        pointer("click", "left")
        second_out.expect("abouttoshow 0")
        wait_for(lambda: panel_at(cx, cy - PANEL_HEIGHT // 2 - 30), "the second item's menu drawn")
        approach(cx, cy)
        pointer("click", "right")  # a second press closes it
        second_out.expect("event 0 closed")

    # An application quitting takes its icon away.
    first.terminate()
    desktop.reap(first, timeout=10)
    if grim:
        wait_for(lambda: not icons("HEADLESS-2", GREEN), "the icon of a quit application gone")
        assert icons("HEADLESS-1", CYAN)

    # shell.widgets.tray = false lets go of the names (the item hears the watcher go and
    # come back, and registers again); on again, the shell serves the watcher once more.
    desktop.config.write_text(CONFIG.replace("notifications = { enabled = false },",
                                             "notifications = { enabled = false },\n"
                                             "    shell = { widgets = { tray = false } },"))
    panels.send_signal(signal.SIGHUP)
    # Without this wait both reloads could land before the shell turns the tray off.
    wait_for(lambda: "paw tray: off, its names released" in log(),
             "the tray turned off")
    if grim:
        wait_for(lambda: not icons("HEADLESS-2", CYAN), "the tray hidden")
    desktop.config.write_text(CONFIG)
    panels.send_signal(signal.SIGHUP)
    wait_for(lambda: log().count("paw tray: serving org.kde.StatusNotifierWatcher") == 2,
             "the watcher served again")
    second_out.expect(r"registered \S+ /\S+")
    if grim:
        wait_for(lambda: icons("HEADLESS-2", CYAN), "the item back after the reload")

    for message in ("ReferenceError", "TypeError", "is not defined", "Cannot read",
                    "Unable to assign"):
        assert message not in log(), log()
    print("tray icons on both panels, clicks, wheel, menu, passive, Ayatana items, "
          "quitting and shell.widgets.tray passed" + ("" if grim else " (no grim: no pixels)"))
