# SPDX-License-Identifier: GPL-3.0-or-later
"""The display settings window end to end, on a headless compositor with two monitors and the
shell: the display_settings action opens it on the monitor under the pointer, holding the
keyboard; a trial shows its question, and Escape there takes the settings back; a trial that turns
off the window's own monitor moves the window to the other one, and Escape brings the monitor back,
the window with it; Escape closes the window."""
from pathlib import Path
import sys

import harness

compositor, shell, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

CONFIG = """return {
    xwayland = false,
    idle = { display_off = 0 },
    outputs = { primary = "HEADLESS-1", order = { "HEADLESS-1", "HEADLESS-2" } },
}"""

with harness.Compositor(compositor, CONFIG, start=False,
                        env={"WLR_HEADLESS_OUTPUTS": "2", "PAW_TEST_TRIAL_MS": "60000"}) as desktop:
    root, env, msg = desktop.root, desktop.env, desktop.msg
    env.update(QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software", QT_FORCE_STDERR_LOGGING="1",
               XDG_DATA_HOME=str(root), XDG_DATA_DIRS=str(root),
               DBUS_SESSION_BUS_ADDRESS="disabled:")
    shell_log = root / "shell.log"
    desktop.start()

    def log():
        return shell_log.read_text()

    def outputs():
        """name: (in the layout, scale) per monitor."""
        return {r[0]: (r[1] == "1", float(r[6])) for r in desktop.rows("outputs")}

    def window():
        """The window's layer surface shown, else one hidden: (output, shown, holds the keyboard),
        or None."""
        rows = [(row[1], row[3] == "1", row[4] == "1") for row in desktop.rows("layers")
                if row[0] == "paw-display-settings"]
        return max(rows, key=lambda row: row[1], default=None)

    def trial():
        return msg("get", "monitors_trial").strip()

    desktop.detail = lambda: f"outputs: {outputs()}, window: {window()}\n{log()[-1500:]}"
    desktop.spawn([shell, "--config", str(desktop.config)], log="shell.log")
    desktop.wait_for(lambda: log().count("paw surface rendered: paw taskbar") >= 2,
                     "the panels rendered", timeout=30)
    pointer = desktop.virtual_pointer(pointer_probe, 2560, 720)
    pointer("move 640 200")  # on the first monitor
    msg("headless_keyboard", "add", "keys")

    def escape():
        for state in ("press", "release"):
            msg("headless_keyboard", "key", "keys", "1", state)

    # The action opens it on the monitor under the pointer, holding the keyboard. The shell
    # subscribes asynchronously: ask again until it hears.
    for _ in range(20):
        msg("display_settings")
        try:
            desktop.wait_for(lambda: "paw display settings shown on HEADLESS-1" in log(),
                             "the window shown", timeout=2)
            break
        except harness.Timeout:
            continue
    desktop.wait_for(lambda: window() == ("HEADLESS-1", True, True),
                     "the window on HEADLESS-1 with the keyboard")

    # A trial shows its question; Escape there takes the settings back.
    msg("monitors", "apply", "HEADLESS-2", "scale=2")
    assert outputs()["HEADLESS-2"] == (True, 2.0), outputs()
    desktop.stays(lambda: trial() != "-", "the trial runs while the question waits", duration=0.5)
    escape()
    desktop.wait_for(lambda: trial() == "-" and outputs()["HEADLESS-2"] == (True, 1.0),
                     "Escape on the question took the settings back")
    assert window() == ("HEADLESS-1", True, True), window()

    # A trial turning the window's own monitor off: the window goes to the other one, where
    # Escape brings the monitor back.
    shown = log().count("paw display settings shown on HEADLESS-2")
    msg("monitors", "apply", "HEADLESS-1", "enabled=off")
    desktop.wait_for(lambda: not outputs()["HEADLESS-1"][0], "HEADLESS-1 off")
    desktop.wait_for(lambda: log().count("paw display settings shown on HEADLESS-2") > shown and
                     window() == ("HEADLESS-2", True, True),
                     "the window moved to HEADLESS-2 with the keyboard")
    escape()
    desktop.wait_for(lambda: trial() == "-" and outputs()["HEADLESS-1"][0],
                     "Escape brought HEADLESS-1 back")
    # Back on the monitor it was opened on.
    desktop.wait_for(lambda: window() == ("HEADLESS-1", True, True),
                     "the window back on HEADLESS-1 with the keyboard")

    # Without a trial, Escape closes the window.
    escape()
    desktop.wait_for(lambda: (window() or ("", False, False))[1] is False, "the window closed")
print("The display settings window opened, followed trials and their end, and closed")
