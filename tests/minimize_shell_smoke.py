# SPDX-License-Identifier: GPL-3.0-or-later
"""The shell tells the compositor where each window's button is, on every monitor's bar, for the
window to minimize into and restore out of: the window minimizes into its button on its own
monitor's bar. While the card of its pictures is open, the window's picture counts over its
button, which counts again once the card has closed. The bar moved to the top edge takes the
buttons with it; while the monitors tile, the tiling bar's title of the focused window is where
it minimizes into; and a window that closes takes its buttons away."""
from pathlib import Path
import signal
import subprocess
import sys

import harness

compositor, shell, probe, window_probe, pointer_probe = (str(Path(p).resolve())
                                                          for p in sys.argv[1:5 + 1])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false },
    outputs = {
        order = { "HEADLESS-1", "HEADLESS-2" },
        monitors = {
            ["HEADLESS-1"] = { mode = "1280x720" },
            ["HEADLESS-2"] = { mode = "1280x720" },
        },
    },
    notifications = { enabled = false },
    shell = { panel_height = 52, thumbnails = { delay = 300 }%s },
}"""
WIDTH, HEIGHT, BAR = 1280, 720, 52

with harness.Compositor(compositor, CONFIG % "", start=False,
                        env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    root, env = desktop.root, desktop.env
    env.update(QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software",
               QT_FORCE_STDERR_LOGGING="1", XDG_DATA_HOME=str(root), XDG_DATA_DIRS=str(root),
               XDG_STATE_HOME=str(root), DBUS_SESSION_BUS_ADDRESS="disabled:")
    desktop.start()
    shell_log = root / "shell.log"
    desktop.logs.append(shell_log)

    def log():
        return shell_log.read_text(errors="replace")

    def rects():
        """{output: (x, y, width, height)} of where the shell shows the only window."""
        return {row[2]: tuple(map(int, row[3:7])) for row in desktop.rows("minimize")
                if row[0] == "rect"}

    def folds():
        return [row for row in desktop.rows("minimize") if row[0] == "fold"]

    def windows():
        return desktop.windows()

    def on_bar(rect, top=False):
        """Whether `rect` is a button on a bar 52 high along the bottom (or the top) edge."""
        x, y, width, height = rect
        bar_top = 0 if top else HEIGHT - BAR
        return width >= 40 and 30 <= height <= BAR and bar_top <= y and y + height <= bar_top + BAR

    desktop.detail = lambda: f"rects: {rects()}, windows: {windows()}\n{log()[-1500:]}"
    with shell_log.open("w") as output:
        panels = desktop.spawn([shell, "--config", str(desktop.config)], stdout=output,
                               stderr=subprocess.STDOUT)
    desktop.wait_for(lambda: log().count("paw surface rendered: paw taskbar") >= 2, "the panels")
    window = desktop.spawn([probe, "--window-only"])
    desktop.wait_for(lambda: len(windows()) == 1 and windows()[0].output == "HEADLESS-1",
                     "the window")

    # Its button on each monitor's bar, in that monitor's coordinates.
    desktop.wait_for(lambda: set(rects()) == {"HEADLESS-1", "HEADLESS-2"} and
                     all(on_bar(rect) for rect in rects().values()), "the window's buttons")
    button = rects()["HEADLESS-1"]

    # Minimized, the window shrinks into its button on its own monitor's bar.
    subprocess.run([window_probe, "paw protocol probe", "minimize"], env=env, check=True,
                   timeout=30)
    desktop.wait_for(lambda: windows()[0].minimized, "the window minimized")
    fold = folds()
    assert not fold or (fold[0][2] == "minimize" and tuple(map(int, fold[0][4:8])) == button and
                        fold[0][8] == "button"), fold
    desktop.wait_for(lambda: not folds(), "the window in its button")

    # While its card is open, its picture counts; once the card has closed, its button again.
    pointer = desktop.virtual_pointer(pointer_probe, 2 * WIDTH, HEIGHT)
    pointer("move", str(button[0] + button[2] // 2), str(button[1] + button[3] // 2))
    desktop.wait_for(lambda: rects()["HEADLESS-1"][1] + rects()["HEADLESS-1"][3] <= HEIGHT - BAR,
                     "the window's picture on the card above the bar")
    picture = rects()["HEADLESS-1"]
    assert picture[2] > button[2] and rects()["HEADLESS-2"] != picture, rects()
    pointer("move", "900", "300")
    desktop.wait_for(lambda: rects()["HEADLESS-1"] == button, "the button once the card closed")

    # Restored from its button, it grows out of it.
    subprocess.run([window_probe, "paw protocol probe", "restore"], env=env, check=True,
                   timeout=30)
    desktop.wait_for(lambda: not windows()[0].minimized, "the window restored")
    fold = folds()
    assert not fold or (fold[0][2] == "restore" and tuple(map(int, fold[0][4:8])) == button), fold
    desktop.wait_for(lambda: not folds(), "the window back in its place")

    # The bar along the top edge takes its buttons along.
    desktop.reload(CONFIG % ', panel_position = "top"')
    panels.send_signal(signal.SIGHUP)
    desktop.wait_for(lambda: set(rects()) == {"HEADLESS-1", "HEADLESS-2"} and
                     all(on_bar(rect, top=True) for rect in rects().values()),
                     "the buttons along the top")
    top_button = rects()["HEADLESS-1"]

    # While the monitors tile, the tiling bar along the top shows the focused window's title,
    # which it minimizes into.
    desktop.reload(CONFIG.replace("tiling = false", "tiling = true") % "")
    panels.send_signal(signal.SIGHUP)
    desktop.wait_for(lambda: windows()[0].tiled and windows()[0].focused and
                     "HEADLESS-1" in rects() and rects()["HEADLESS-1"] != top_button and
                     rects()["HEADLESS-1"][1] + rects()["HEADLESS-1"][3] <= 52,
                     "the window's title on the tiling bar")
    title = rects()["HEADLESS-1"]
    subprocess.run([window_probe, "paw protocol probe", "minimize"], env=env, check=True,
                   timeout=30)
    desktop.wait_for(lambda: windows()[0].minimized, "the tile minimized")
    fold = folds()
    assert not fold or (fold[0][2] == "minimize" and tuple(map(int, fold[0][4:8])) == title and
                        fold[0][8] == "button"), (fold, title)
    subprocess.run([window_probe, "paw protocol probe", "restore"], env=env, check=True,
                   timeout=30)
    desktop.wait_for(lambda: not windows()[0].minimized, "the tile restored")

    # Closing the window takes its buttons away with its handles.
    subprocess.run([probe, "--close", "paw-probe"], env=env, check=True, timeout=30,
                   stdout=subprocess.DEVNULL)
    desktop.reap(window)
    desktop.wait_for(lambda: not windows(), "the window closed")
    assert not rects(), rects()
    for message in ("ReferenceError", "TypeError", "is not defined", "Cannot read"):
        assert message not in log(), log()
print("the shell's buttons and pictures as the windows' places to minimize into")
