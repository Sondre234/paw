# SPDX-License-Identifier: GPL-3.0-or-later
"""With shell.group_windows = false, a window's taskbar button follows its monitor among its
application's buttons, on every monitor's bar: of two windows of one application, the one on the
monitor further left has its button further left. A window moved to the monitor on the right
moves its button past the other's, and past a button of another application between them; moved
to the same monitor as the other, it goes after it; moved back to the left, back ahead of it.
Grouped, the card of the application's windows shows them in the order of their monitors. With
shell.taskbar_buttons = "own", each bar shows its own monitor's windows alone; with
"main_and_own", the main monitor's bar shows every window, the main monitor being the leftmost
until outputs.primary names another, which the compositor tells the shell (`primary NAME`), and
resting on a button warms up the pictures of its own monitor's windows alone. The buttons' and
pictures' places are where the shell tells the compositor they are, for the windows to minimize
into (`get minimize`)."""
from pathlib import Path
import signal
import subprocess
import sys

import harness

compositor, shell, probe, window_probe, pointer_probe = (str(Path(p).resolve())
                                                          for p in sys.argv[1:6])

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false },
    outputs = {
        order = { "HEADLESS-1", "HEADLESS-2" },%s
        monitors = {
            ["HEADLESS-1"] = { mode = "1280x720" },
            ["HEADLESS-2"] = { mode = "1280x720" },
        },
    },
    notifications = { enabled = false },
    shell = { panel_height = 52, group_windows = %s, thumbnails = { delay = %d }%s },
}"""


def config(group="false", buttons="", primary="", delay=300):
    """The configuration with group_windows = `group`, taskbar_buttons = `buttons` if given,
    outputs.primary = `primary` if given and the cards of pictures after `delay` ms."""
    return CONFIG % (f'\n        primary = "{primary}",' if primary else "", group, delay,
                     f', taskbar_buttons = "{buttons}"' if buttons else "")

LEFT, RIGHT = "HEADLESS-1", "HEADLESS-2"
WIDTH, HEIGHT, BAR = 1280, 720, 52

with harness.Compositor(compositor, config(), start=False,
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

    with shell_log.open("w") as output:
        panels = desktop.spawn([shell, "--config", str(desktop.config)], stdout=output,
                               stderr=subprocess.STDOUT)
    desktop.wait_for(lambda: log().count("paw surface rendered: paw taskbar") >= 2, "the panels")

    def probe_window(*words):
        return subprocess.run([window_probe, *words], env=env, check=True, timeout=30,
                              capture_output=True, text=True).stdout

    titles = ("One", "Other", "Two")
    for title, app_id in zip(titles, ("paw-a", "paw-b", "paw-a")):
        desktop.open_window(probe, title, app_id)
    ids = {int(probe_window(title, "id")): title for title in titles}

    def outputs():
        return {window.title: window.output for window in desktop.windows()}

    def rects(output):
        """{title: (x, y, width, height)} of where `output`'s bar shows each window."""
        return {ids[int(row[1])]: tuple(map(int, row[3:7])) for row in desktop.rows("minimize")
                if row[0] == "rect" and row[2] == output and int(row[1]) in ids}

    def order(output):
        """The windows' titles in the order of their buttons on `output`'s bar."""
        at = rects(output)
        return sorted(at, key=at.get)

    def shows(expected):
        return all(order(output) == expected for output in (LEFT, RIGHT))

    desktop.detail = lambda: (f"outputs: {outputs()}, left bar: {order(LEFT)}, "
                              f"right bar: {order(RIGHT)}\n{log()[-1500:]}")
    desktop.wait_for(lambda: outputs() == dict.fromkeys(titles, LEFT), "the windows on the left")
    desktop.wait_for(lambda: shows(["One", "Other", "Two"]),
                     "the buttons in the order their windows opened")

    def move(title, output, expected, message):
        probe_window(title, "output", output)
        desktop.wait_for(lambda: outputs()[title] == output, f"{title} on {output}")
        desktop.wait_for(lambda: shows(expected), message)

    move("One", RIGHT, ["Other", "Two", "One"],
         "One's button past Other's and Two's, its window right of Two")
    move("Two", RIGHT, ["Other", "One", "Two"], "Two's button after One's, on the same monitor")
    move("Two", LEFT, ["Other", "Two", "One"], "Two's button back ahead of One's")

    # Grouped, One and Two share a button, and its card shows Two's picture, on the left monitor,
    # ahead of One's, which opened first.
    desktop.reload(config(group="true"))
    panels.send_signal(signal.SIGHUP)
    desktop.wait_for(lambda: "One" in rects(LEFT) and rects(LEFT)["One"] == rects(LEFT).get("Two"),
                     "One and Two sharing a button")
    x, y, width, height = rects(LEFT)["One"]
    pointer = desktop.virtual_pointer(pointer_probe, 2 * WIDTH, HEIGHT)
    pointer("move", str(x + width // 2), str(y + height // 2))

    def pictures():
        """Whether One's and Two's places are their pictures on the card above the bar."""
        at = rects(LEFT)
        return all(at[title][1] + at[title][3] <= HEIGHT - BAR for title in ("One", "Two")) and \
            at["One"] != at["Two"]

    desktop.wait_for(pictures, "the card of One's and Two's pictures")
    desktop.wait_for(lambda: rects(LEFT)["Two"][0] < rects(LEFT)["One"][0],
                     "Two's picture ahead of One's")
    pointer("move", "900", "300")

    # Each bar its own monitor's windows, grouped: resting on the left bar's button of Two and
    # Three warms up their pictures halfway to the card (`get pictures`), and the card shows
    # them, but neither asks for One's, which is of the same application on the right monitor.
    desktop.reload(config(group="true", buttons="own", delay=2000))
    panels.send_signal(signal.SIGHUP)
    three = desktop.open_window(probe, "Three", "paw-a")
    ids[int(probe_window("Three", "id"))] = "Three"
    for title, output in (("Three", LEFT), ("One", RIGHT)):
        probe_window(title, "output", output)
        desktop.wait_for(lambda: outputs()[title] == output, f"{title} on {output}")
    desktop.wait_for(lambda: "One" not in rects(LEFT) and "Three" in rects(LEFT) and
                     rects(LEFT)["Three"] == rects(LEFT).get("Two"),
                     "Two and Three sharing a button on the left bar, without One")

    def pictured():
        return {row[3] for row in desktop.rows("pictures")}

    x, y, width, height = rects(LEFT)["Two"]
    pointer("move", str(x + width // 2), str(y + height // 2))
    # Halfway to the card, a second in, the pictures are asked for; the card opens a second later.
    desktop.stays(lambda: "One" not in pictured(), "One's picture asked for on the left bar", 2.5)
    assert {"Two", "Three"} <= pictured(), desktop.rows("pictures")
    pointer("move", "900", "300")
    three.terminate()
    desktop.reap(three)
    desktop.wait_for(lambda: "Three" not in outputs(), "Three closed")

    # Each bar its own monitor's windows ("own"), then the main monitor's every window
    # ("main_and_own"): the leftmost's, until outputs.primary names the other.
    subscriber = desktop.subscribe()
    desktop.wait_for(lambda: subscriber.last("primary ") == f"primary {LEFT}",
                     "the leftmost monitor the primary one")

    def shown(buttons, left, right, message, primary=""):
        desktop.reload(config(buttons=buttons, primary=primary))
        panels.send_signal(signal.SIGHUP)
        desktop.wait_for(lambda: order(LEFT) == left and order(RIGHT) == right, message)

    shown("own", ["Other", "Two"], ["One"], "each bar showing its own monitor's windows")
    shown("main_and_own", ["Other", "Two", "One"], ["One"],
          "the left bar showing every window, the right one its own")
    shown("main_and_own", ["Other", "Two"], ["Other", "Two", "One"],
          "the right bar showing every window once its monitor is the primary one",
          primary=RIGHT)
    assert subscriber.last("primary ") == f"primary {RIGHT}", subscriber.lines("primary ")
    move("One", LEFT, ["Other", "Two", "One"], "One's button on the left bar once it is there")
    shown("all", ["Other", "Two", "One"], ["Other", "Two", "One"], "every bar every window")

    for message in ("ReferenceError", "TypeError", "is not defined", "Cannot read"):
        assert message not in log(), log()
print("a window's taskbar button follows its monitor among its application's")
