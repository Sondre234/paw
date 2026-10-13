# SPDX-License-Identifier: GPL-3.0-or-later
"""The shell's peeks at a selected window in a session. The switcher, once it shows, tells the
compositor so, and its selected window then peeks; the command palette peeks at the window result
selected once the selection rests, hands the peek over at once as the selection moves to another
window, and ends it as it closes, Enter focusing the window where it shows. The macOS style
peeks at neither, and peek.switcher = false turns the palette's off too."""
from pathlib import Path
import signal
import sys

import harness

compositor, shell, probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    notifications = { enabled = false },
    shell = { panel_height = 52 },
    peek = { duration = 100 },
    profiles = { mac = { shell = { style = "macos" } }, plain = { shell = { panel_height = 52 } } },
}"""
REST = 0.5  # seconds a selection rests before the first peek
ESCAPE, ENTER, DOWN = 1, 28, 108  # evdev's key codes

with harness.Compositor(compositor, CONFIG, start=False) as desktop:
    root, env, msg = desktop.root, desktop.env, desktop.msg
    env.update(QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software",
               QT_FORCE_STDERR_LOGGING="1", XDG_DATA_HOME=str(root), XDG_DATA_DIRS=str(root),
               XDG_STATE_HOME=str(root), DBUS_SESSION_BUS_ADDRESS="disabled:")
    desktop.start()
    shell_log = root / "shell.log"

    def log():
        return shell_log.read_text()

    def layer(namespace):
        """Whether the shell's surface is shown and holds the keyboard, or None without one."""
        return next((row[3:5] for row in desktop.rows("layers") if row[0] == namespace), None)

    def peeks():
        """title -> (peeked, drawn, stacked at, shown through a peek, opacity)."""
        return {row[5]: tuple(int(n) for n in row[:5]) for row in desktop.rows("window_peek")}

    def peeked():
        return next((title for title, row in peeks().items() if row[0]), None)

    def fade():
        return int(msg("get", "peek").split("\t")[0])

    def focused():
        return [w.title for w in desktop.windows() if w.focused]

    def back():
        """No peek, and every window in full again."""
        return peeked() is None and fade() == 0 and \
            all(row[4] == 1000 for row in peeks().values())

    desktop.detail = lambda: (f"peeks: {peeks()}, fade: {fade()}\n"
                              f"layers: {desktop.rows('layers')}\n{log()[-1500:]}")
    key = desktop.keyboard()
    shell_process = desktop.spawn([shell, "--config", str(desktop.config)], log="shell.log")
    desktop.wait_for(lambda: "paw surface rendered: paw taskbar" in log(), "the panel",
                     timeout=30)
    for title in ("Alpha", "Beta"):
        desktop.open_window(probe, title, f"app-{title.lower()}")
    desktop.wait_for(lambda: focused() == ["Beta"], "Beta focused")

    def palette(text):
        """Opens the palette, as often as it takes the shell to hear, and types `text`."""
        for _ in range(10):
            msg("palette")
            try:
                desktop.wait_for(lambda: layer("paw-palette") == ["1", "1"],
                                 "the palette open with the keyboard", timeout=1)
                break
            except harness.Timeout:
                pass
        assert layer("paw-palette") == ["1", "1"], desktop.rows("layers")
        msg("type", text)

    def switcher():
        """Opens the switcher until the shell shows it."""
        shown = log().count("paw switcher shown on HEADLESS-1")
        for _ in range(10):
            msg("switcher")
            try:
                desktop.wait_for(lambda: log().count("paw switcher shown on HEADLESS-1") > shown,
                                 "the switcher shown", timeout=1)
                return
            except harness.Timeout:
                msg("switcher_cancel")
        raise AssertionError("the shell never showed the switcher")

    # The switcher, shown, has the compositor peek at its selection, Alpha, once it rests.
    switcher()
    desktop.wait_for(lambda: peeked() == "Alpha", "the switcher's selection peeked at")
    msg("switcher_cancel")
    desktop.wait_for(back, "the switcher's peek over")

    # The palette: the window found peeks once the selection rests on it; Escape ends the peek,
    # changing nothing.
    palette("@Alpha")
    desktop.wait_for(lambda: peeked() == "Alpha", "the palette's window peeked at")
    assert fade() > 0, fade()
    key(ESCAPE)
    desktop.wait_for(back, "the palette's peek over once it closed")
    assert focused() == ["Beta"], focused()

    # Moving to another window hands the peek over at once, the others staying faded; Enter
    # focuses the window peeked at where it shows, the others coming back around it.
    palette("@")
    desktop.wait_for(lambda: peeked() is not None, "the first window peeked at")
    first = peeked()
    other = "Alpha" if first == "Beta" else "Beta"
    desktop.wait_for(lambda: fade() == 1000, "the others faded")
    key(DOWN)
    desktop.wait_for(lambda: peeked() == other, "the peek handed over to the next window")
    assert fade() == 1000, "the others came back between the peeks"
    desktop.wait_for(lambda: peeks()[other][3:] == (1000, 1000), f"{other} shown in full")
    key(ENTER)
    desktop.wait_for(lambda: focused() == [other], f"{other} focused")
    assert peeks()[other][4] == 1000, peeks()
    desktop.wait_for(back, "the palette's peek over once the window was focused")

    # The macOS style peeks at neither.
    msg("profile", "mac")
    shell_process.send_signal(signal.SIGHUP)
    desktop.wait_for(lambda: (layer("paw-menubar") or ["0"])[0] == "1", "the macOS style")
    switcher()
    desktop.stays(lambda: peeked() is None, "the macOS style's switcher peeked",
                  duration=REST + .3)
    msg("switcher_cancel")
    palette("@Alpha")
    desktop.stays(lambda: peeked() is None, "Spotlight peeked", duration=REST + .3)
    key(ESCAPE)
    desktop.wait_for(lambda: layer("paw-palette") != ["1", "1"], "Spotlight closed")

    # peek.switcher = false turns the palette's peek off too.
    desktop.reload(CONFIG.replace("duration = 100 }", "duration = 100, switcher = false }"))
    msg("profile", "plain")
    shell_process.send_signal(signal.SIGHUP)
    desktop.wait_for(lambda: (layer("paw-menubar") or ["0"])[0] == "0", "the taskbar style again")
    palette("@Alpha")
    desktop.stays(lambda: peeked() is None, "the palette peeked with peek.switcher off",
                  duration=REST + .3)
    key(ESCAPE)

    for message in ("ReferenceError", "TypeError", "is not defined", "Cannot read"):
        assert message not in log(), log()
print("The switcher and the palette peek at their selected window in a session, the macOS style "
      "at neither")
