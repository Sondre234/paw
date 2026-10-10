# SPDX-License-Identifier: GPL-3.0-or-later
"""The display mode popup end to end, from the XF86Display key on a headless compositor with two
monitors to the shell: the popup shows on the focused monitor, the pointer on it holds it open, a
click on a choice takes it at once (the other monitor then mirrors the main one, leaving the layout
and the shell's screens), and it closes."""
from pathlib import Path
import sys

import harness

compositor, shell, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

CONFIG = """return {
    xwayland = false,
    outputs = { primary = "HEADLESS-1", order = { "HEADLESS-1", "HEADLESS-2" } },
    bindings = { { key = "XF86Display", action = "display_mode" } },
}"""

with harness.Compositor(compositor, CONFIG, start=False,
                        env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    root, env = desktop.root, desktop.env
    env.update(QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software", QT_FORCE_STDERR_LOGGING="1",
               XDG_DATA_HOME=str(root), XDG_DATA_DIRS=str(root),
               DBUS_SESSION_BUS_ADDRESS="disabled:")
    shell_log = root / "shell.log"
    desktop.start()

    def log():
        return shell_log.read_text()

    def outputs():
        """name: (in the layout, mirrored) per monitor."""
        return {r[0]: (r[1] == "1", r[11]) for r in desktop.rows("outputs")}

    desktop.detail = lambda: f"outputs: {outputs()}\n{log()[-1500:]}"
    desktop.spawn([shell, "--config", str(desktop.config)], log="shell.log")
    desktop.wait_for(lambda: log().count("paw surface rendered: paw taskbar") >= 2,
                     "the panels rendered", timeout=30)
    pointer = desktop.virtual_pointer(pointer_probe, 2560, 720)
    pointer("move 640 200")  # the first monitor has the focus
    key = desktop.keyboard()

    def pointer_on():
        return tuple(next(row[1:3] for row in desktop.rows("seat") if row[0] == "pointer"))

    duplicate = {"HEADLESS-1": (True, "-"), "HEADLESS-2": (False, "HEADLESS-1")}
    # The popup's Duplicate tile is the second of four of 112 by 92 pixels, 4 apart, in the middle
    # of the output. The pointer waits there; on the popup it holds it open past the key's time,
    # and a click takes duplicate at once. The shell subscribes asynchronously, and a busy machine
    # may take the key's time to show it: open it again until a click lands.
    pointer("move 582 372")
    for _ in range(20):
        shown = log().count("paw display mode shown on HEADLESS-1")
        key(227)
        try:
            desktop.wait_for(lambda: log().count("paw display mode shown on HEADLESS-1") > shown,
                             "the popup shown", timeout=2)
        except harness.Timeout:
            key(1)  # Escape: closed, to be opened anew
            continue
        pointer("move 583 372")  # onto the popup, which came up under the pointer
        try:
            desktop.wait_for(lambda: pointer_on() == ("layer", "paw-display-mode"),
                             "the pointer on the popup", timeout=2)
        except harness.Timeout:
            key(1)
            continue
        # Held open by the pointer past the key's time.
        desktop.stays(lambda: desktop.rows("display_mode")[0][1] == "extend",
                      "the popup stayed open under the pointer", duration=2)
        pointer("click left")
        try:
            desktop.wait_for(lambda: outputs() == duplicate, "the click took duplicate", timeout=3)
            break
        except harness.Timeout:
            key(1)
    assert outputs() == duplicate, outputs()
    desktop.wait_for(lambda: "paw display mode hidden on HEADLESS-1" in log(),
                     "the popup closed")
    assert desktop.rows("display_mode")[0] == ["duplicate", "-", "-"]
print("The display mode popup shows in the shell, and a click takes a choice")
