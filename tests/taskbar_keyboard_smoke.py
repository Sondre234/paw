# SPDX-License-Identifier: GPL-3.0-or-later
"""The keyboard on the bar in a session: taskbar_focus gives it to the panel's popover on the
focused monitor, taking it from the focused window, and Escape gives it back to that window, as
the action again does. Left and Right move between the windows' buttons, and Enter brings the
selected window up with the keyboard, or minimizes the one that had it, the keyboard going on to
the next. The pointer moving over the bar hands the bar back to it, and the keyboard back to the
window."""
from pathlib import Path
import re
import sys

import harness

compositor, shell, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:5])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    notifications = { enabled = false },
    shell = { panel_height = 52 },
}"""
WIDTH, HEIGHT, BAR = 1280, 720, 52
KEYS = {"Escape": 1, "Enter": 28, "Left": 105, "Right": 106}  # evdev's codes
POPOVER = ("layer", "paw-popover")

with harness.Compositor(compositor, CONFIG, start=False) as desktop:
    root, env, msg = desktop.root, desktop.env, desktop.msg
    env.update(QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software",
               QT_FORCE_STDERR_LOGGING="1", XDG_DATA_HOME=str(root), XDG_DATA_DIRS=str(root),
               XDG_STATE_HOME=str(root), DBUS_SESSION_BUS_ADDRESS="disabled:")
    shell_log = root / "shell.log"

    def log():
        return shell_log.read_text(errors="replace")

    def keyboard():
        """What has the keyboard: ("window", TITLE), ("layer", NAMESPACE) or ("-", "-")."""
        return next(tuple(row[1:3]) for row in desktop.rows("seat") if row[0] == "keyboard")

    def windows():
        """Each window's title, with whether it is focused and whether it is minimized."""
        return {w.title: (w.focused, w.minimized) for w in desktop.windows()}

    def key(name):
        for state in ("press", "release"):
            msg("headless_keyboard", "key", "keys", str(KEYS[name]), state)

    def toggles():
        """What the panel said of each taskbar_focus it heard: "on" or "off". The shell may be
        writing the last line still."""
        return re.findall(r"^paw taskbar keyboard (on|off) on \S+\n", log(), re.MULTILINE)

    def focus_bar():
        """taskbar_focus, until the panel has heard it, and the keyboard on the bar."""
        heard = len(toggles())
        # The shell subscribes asynchronously: ask until it has heard.
        for _ in range(10):
            msg("taskbar_focus")
            try:
                desktop.wait_for(lambda: len(toggles()) > heard, "the panel hearing taskbar_focus",
                                 timeout=3)
                break
            except harness.Timeout:
                pass
        # Heard twice, the second time asked for when the first was slow to come, it gave the
        # keyboard back: once more.
        if toggles()[-1] == "off":
            heard = len(toggles())
            msg("taskbar_focus")
            desktop.wait_for(lambda: len(toggles()) > heard, "the panel hearing taskbar_focus again")
        assert toggles()[-1] == "on", toggles()
        desktop.wait_for(lambda: keyboard() == POPOVER, "the popover with the keyboard")

    desktop.start()
    msg("headless_keyboard", "add", "keys")
    desktop.spawn([shell, "--config", str(desktop.config)], log="shell.log")
    desktop.detail = lambda: f"seat: {desktop.rows('seat')}, windows: {windows()}\n{log()[-1500:]}"
    desktop.wait_for(lambda: "paw surface rendered: paw taskbar" in log(),
                     "the panel rendered", timeout=30)
    for title in ("First", "Second"):
        desktop.spawn([probe, "--window-only"],
                      env=dict(PAW_PROBE_TITLE=title, PAW_PROBE_APP_ID=title.lower()))
        desktop.wait_for(lambda: keyboard() == ("window", title), f"{title} with the keyboard")

    # The bar takes the keyboard from the focused window, and Escape gives it back unchanged.
    focus_bar()
    assert not any(focused for focused, _ in windows().values()), windows()
    key("Escape")
    desktop.wait_for(lambda: keyboard() == ("window", "Second") and windows()["Second"][0],
                     "the keyboard back with the window that had it")
    desktop.wait_for(lambda: POPOVER[1] not in [row[0] for row in desktop.rows("layers") if row[3] == "1"],
                     "the popover gone")
    assert windows() == {"First": (False, False), "Second": (True, False)}, windows()

    # So does the action again.
    focus_bar()
    msg("taskbar_focus")
    desktop.wait_for(lambda: keyboard() == ("window", "Second") and toggles()[-1] == "off",
                     "the keyboard back with the window after taskbar_focus again")

    # Left, from the focused window's button to the other's, and Enter brings it up.
    focus_bar()
    key("Left")
    key("Enter")
    desktop.wait_for(lambda: keyboard() == ("window", "First") and windows()["First"][0],
                     "the window selected brought up with the keyboard")

    # Enter on the button of the window that had the keyboard minimizes it, as a click does, and
    # the next window has the keyboard.
    focus_bar()
    key("Enter")
    desktop.wait_for(lambda: windows()["First"][1] and keyboard() == ("window", "Second"),
                     "the window that had the keyboard minimized, the next with the keyboard")

    # The pointer moving over the bar hands the bar back to it, and the keyboard to the window.
    focus_bar()
    pointer = desktop.virtual_pointer(pointer_probe, WIDTH, HEIGHT)
    pointer("move", "700", str(HEIGHT - BAR // 2))
    pointer("move", "760", str(HEIGHT - BAR // 2))
    desktop.wait_for(lambda: keyboard() == ("window", "Second"),
                     "the keyboard back with the window once the pointer moved over the bar")
    assert windows() == {"First": (False, True), "Second": (True, False)}, windows()
    for message in ("ReferenceError", "TypeError", "is not defined", "Cannot read"):
        assert message not in log(), log()
print("The keyboard on the bar passed")
