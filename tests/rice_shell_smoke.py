# SPDX-License-Identifier: GPL-3.0-or-later
"""The question while a rice is on trial, end to end, on a headless compositor with the shell:
`paw rice try` shows "Keep this look?" in the middle of the monitor, holding the keyboard; Escape
there takes the rice back and the question goes; kept from a terminal, the question goes and the
rice stays."""
from pathlib import Path
import re
import subprocess
import sys

import harness

compositor, shell = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    idle = { display_off = 0 },
    animations = { enabled = false },
    profile = "dark",
    profiles = { dark = { shell = { accent = "#112233" } } },
}"""

with harness.Compositor(compositor, CONFIG, start=False,
                        env={"PAW_TEST_RICE_TRIAL_MS": "60000"}) as desktop:
    root, env, msg = desktop.root, desktop.env, desktop.msg
    env.update(QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software", QT_FORCE_STDERR_LOGGING="1",
               XDG_DATA_HOME=str(root), XDG_DATA_DIRS=str(root), XDG_CONFIG_HOME=str(root / "conf"),
               DBUS_SESSION_BUS_ADDRESS="disabled:")
    washi = root / "washi"
    washi.mkdir()
    (washi / "rice.lua").write_text("""return {
        format = 1, name = "washi", author = "someone", description = "Sakura paper.",
        profile = { shell = { accent = "#e0607e" } },
    }""")
    shell_log = root / "shell.log"
    desktop.start()

    def log():
        return shell_log.read_text()

    def question():
        """The question's layer surface: (output, shown, holds the keyboard), or None."""
        rows = [(row[1], row[3] == "1", row[4] == "1") for row in desktop.rows("layers")
                if row[0] == "paw-rice-trial"]
        return max(rows, key=lambda row: row[1], default=None)

    def trial():
        return msg("get", "rice_trial").strip()

    def profile():
        found = re.findall(r"Configuration reloaded: .*?(?: \(profile (\S+)\))?$",
                           desktop.log.read_text(), re.M)
        return found[-1] if found else None

    def rice(*words):
        result = subprocess.run([compositor, "rice", *words, "--config", str(desktop.config)],
                                env=env, capture_output=True, text=True, timeout=60)
        assert result.returncode == 0, (words, result.stdout, result.stderr)
        return result.stdout

    def hidden():
        return (question() or ("", False, False))[1] is False

    desktop.detail = lambda: f"question {question()}, trial {trial()}\n{log()[-1500:]}"
    desktop.spawn([shell, "--config", str(desktop.config)], log="shell.log")
    desktop.wait_for(lambda: "paw surface rendered: paw taskbar" in log(), "the panel rendered",
                     timeout=30)
    msg("headless_keyboard", "add", "keys")

    def escape():
        for state in ("press", "release"):
            msg("headless_keyboard", "key", "keys", "1", state)

    # Tried, the question shows, holding the keyboard; Escape takes the rice back. The shell
    # subscribes asynchronously: try again until it hears.
    for attempt in range(20):
        rice("try", str(washi))
        try:
            desktop.wait_for(lambda: "paw rice trial shown on HEADLESS-1" in log(),
                             "the question shown", timeout=3)
            break
        except harness.Timeout:
            rice("revert")  # the shell was not listening yet
    desktop.wait_for(lambda: question() == ("HEADLESS-1", True, True),
                     "the question in the middle of HEADLESS-1 with the keyboard")
    assert profile() == "washi", profile()
    escape()
    desktop.wait_for(lambda: trial() == "-" and profile() == "dark", "Escape took washi back")
    desktop.wait_for(hidden, "the question went")

    # Kept from a terminal, the question goes and the rice stays.
    shown = log().count("paw rice trial shown on HEADLESS-1")
    rice("try", str(washi))
    desktop.wait_for(lambda: log().count("paw rice trial shown on HEADLESS-1") > shown,
                     "the question shown again")
    rice("keep")
    desktop.wait_for(hidden, "the question went once the rice was kept")
    assert trial() == "-" and profile() == "washi"

print("the rice's question showed, and went as the rice was taken back and kept")
