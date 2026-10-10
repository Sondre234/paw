# SPDX-License-Identifier: GPL-3.0-or-later
"""The notification daemon and the on-screen display in the shell, end to end: a private session
bus (a dbus-daemon this test starts and kills) and a headless compositor with the shell. Cards
appear top right, a click runs the default action, hovering holds the timer, do-not-disturb keeps
them away, `paw msg osd` draws the pill, and the clock flyout with the history opens. The
shell never sees the real session bus."""
import os
from pathlib import Path
import select
import shutil
import signal
import subprocess
import sys
import time

import harness

compositor, shell, notify, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:5])
grim = shutil.which("grim")
if not shutil.which("dbus-daemon"):
    print("dbus-daemon not found: the notification daemon was not driven")
    sys.exit(0)

CONFIG = """return {
    xwayland = false,
    layout = { tiling = false, workspaces = 2 },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" }, ["HEADLESS-2"] = { mode = "1280x720" } } },
    notifications = { timeout = 60000 },
    animations = { speed = 2 },
    osd = { timeout = 400 },
}"""
SCREEN = (2560, 720)  # two outputs side by side; HEADLESS-2 is the one at the origin
CARD = (1150, 60)  # a point on a card, top right on HEADLESS-2
PANEL = (21, 30, 44)  # shell.panel_color

with harness.Compositor(compositor, CONFIG, bus=True, start=False) as desktop:
    root, env, msg = desktop.root, desktop.env, desktop.msg
    # A backlight of its own, which the shell polls in place of /sys.
    backlight = root / "sys" / "class" / "backlight" / "fake"
    backlight.mkdir(parents=True)
    (backlight / "max_brightness").write_text("100\n")
    (backlight / "brightness").write_text("50\n")
    env.update(QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software",
               QT_FORCE_STDERR_LOGGING="1", XDG_DATA_HOME=str(root), XDG_DATA_DIRS=str(root),
               XDG_STATE_HOME=str(root), WLR_HEADLESS_OUTPUTS="2",
               PAW_SYSFS=str(root / "sys"))
    shell_log = root / "shell.log"

    def wait_for(predicate, message, timeout=6):
        desktop.wait_for(predicate, message, timeout=timeout)

    def log():
        return shell_log.read_text()

    def count(text):
        return log().count(text)

    def send(*words):
        """Notifies; returns the id."""
        result = subprocess.run([notify, "notify", *words], env=env, capture_output=True,
                                text=True, timeout=10)
        assert result.returncode == 0, result.stderr
        return int(result.stdout)

    def shot():
        if not grim:
            return None
        return harness.grab(grim, env, "HEADLESS-2")

    def looks(color, at, tolerance=8):
        """Whether HEADLESS-2 shows `color` at `at`; always, without grim."""
        picture = shot()
        return picture is None or all(abs(a - b) <= tolerance
                                      for a, b in zip(picture.at(*at), color))

    def shows(color, at, message, tolerance=8):
        """Waits until HEADLESS-2 shows `color` at `at`, as something slides or fades in."""
        wait_for(lambda: looks(color, at, tolerance), message)

    def close_to(color, at, message, tolerance=8):
        assert looks(color, at, tolerance), (message, shot().at(*at), at)

    desktop.start()
    panels = desktop.spawn([shell, "--config", str(desktop.config)], log="shell.log")
    desktop.detail = lambda: log()[-800:]
    wait_for(lambda: "serving org.freedesktop.Notifications" in log(), "the daemon serving")
    info = subprocess.run([notify, "info"], env=env, capture_output=True, text=True,
                          timeout=10)
    assert info.stdout.strip() == "paw 1.2", info

    watcher = desktop.spawn([notify, "watch"], stdout=subprocess.PIPE)
    events = []
    partial = b""

    def pump(timeout):
        """Reads what the watcher printed into `events`, a line each. Reads the pipe
        itself: a buffered reader would hold lines select() no longer reports."""
        global partial
        ready, _, _ = select.select([watcher.stdout], [], [], timeout)
        if ready:
            partial += os.read(watcher.stdout.fileno(), 4096)
            *lines, partial = partial.split(b"\n")
            events.extend(line.decode().strip() for line in lines)

    while "watching" not in events:
        pump(2)
    events.remove("watching")

    def event(kind, number, extra=None, timeout=6):
        """Waits for a signal line such as "closed 3 2" from the watcher."""
        want = f"{kind} {number}" + (f" {extra}" if extra is not None else "")
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if want in events:
                events.remove(want)
                return
            pump(0.1)
        raise AssertionError(f"no '{want}'; saw {events}\n{log()[-600:]}")

    pointer = desktop.virtual_pointer(pointer_probe, *SCREEN)

    def layers():
        return msg("get", "layers")

    def pointer_on(namespace):
        """Whether the pointer is on a layer surface of that namespace."""
        return ["pointer", "layer", namespace] in desktop.rows("seat")

    def onto_card(what):
        """Puts the pointer on the card once it has slid in under that point."""
        shows(PANEL, CARD, what)
        pointer("move", *(str(n) for n in CARD))
        wait_for(lambda: pointer_on("paw-notifications"), "the pointer on the card")

    # A card appears in the top right corner of the output, on its own layer surface.
    pointer("move", "300", "300")
    first = send("Hi", "--timeout", "0", "--action", "default=Open")
    wait_for(lambda: count("notifications shown on HEADLESS-2") == 1, "the card shown")
    wait_for(lambda: "paw-notifications" in layers(), "the card surface mapped")
    shows(PANEL, (1200, 60), "a card top right")
    close_to((35, 46, 64), (300, 200), "background elsewhere", tolerance=30)

    # Clicking it runs its default action and dismisses it.
    onto_card("the card under the pointer")
    pointer("click", "left")
    event("action", first, "default")
    event("closed", first, 2)
    wait_for(lambda: count("notifications hidden on HEADLESS-2") == 1, "the surface gone")

    # The application can close its own notification.
    second = send("Second", "--timeout", "0")
    wait_for(lambda: count("notifications shown") == 2, "second card shown")
    assert subprocess.run([notify, "close", str(second)], env=env, timeout=10).returncode == 0
    event("closed", second, 3)
    wait_for(lambda: count("notifications hidden") == 2, "surface gone after CloseNotification")

    # A timeout expires it; the surface goes once the card has slid away.
    third = send("Brief", "--timeout", "300")
    event("closed", third, 1)
    wait_for(lambda: count("notifications hidden") == 3, "surface gone after expiry")

    # Hovering a card holds its timer: it outlives its timeout, then goes once the pointer
    # leaves.
    fourth = send("Held", "--timeout", "800")
    wait_for(lambda: count("notifications shown") == 4, "fourth card shown")
    shown_at = time.monotonic()
    pointer("move", "300", "400")
    onto_card("the fourth card under the pointer")

    def open_still(number):
        pump(0)
        return not any(e.startswith(f"closed {number}") for e in events)
    # Until well past its timeout.
    desktop.stays(lambda: open_still(fourth), "the card expired under the pointer",
                  duration=max(.3, shown_at + 1.3 - time.monotonic()))
    pointer("move", "300", "400")
    event("closed", fourth, 1)
    wait_for(lambda: count("notifications hidden") == 4, "surface gone after hover")

    # Replacing keeps the id (that the card is updated, not doubled, is
    # notifications_test's).
    fifth = send("Version one", "--timeout", "0")
    assert send("Version two", "--timeout", "0", "--replaces", str(fifth)) == fifth
    unrelated = send("Unrelated", "--timeout", "0")
    assert unrelated != fifth
    for number in (fifth, unrelated):
        subprocess.run([notify, "close", str(number)], env=env, check=True, timeout=10)
        event("closed", number, 3)
    wait_for(lambda: count("notifications hidden") == 5, "surface gone")

    # A real libnotify client (notify-send, where it is installed) gets its action back.
    libnotify = shutil.which("notify-send")
    if libnotify:
        shown = count("notifications shown")
        client = desktop.spawn(
            [libnotify, "-a", "libnotify", "-w", "-t", "0", "-A", "default=Open",
             "-h", "int:value:70", "Real client", "with <b>markup</b>"],
            stdout=subprocess.PIPE, text=True)
        wait_for(lambda: count("notifications shown") == shown + 1, "notify-send's card")
        onto_card("notify-send's card under the pointer")
        pointer("click", "left")
        assert client.stdout.readline().strip() == "default"
        assert desktop.reap(client, timeout=10) == 0
        wait_for(lambda: count("notifications hidden") == shown + 1, "its card gone")

    # Do not disturb: the bell keeps them, no card opens; critical ones still do.
    shown_before = count("notifications shown")
    osd_before = count("osd shown")
    # The shell subscribes asynchronously: resend until it has heard.
    for _ in range(20):
        msg("dnd", "on")
        try:
            wait_for(lambda: count("osd shown") > osd_before, "the do-not-disturb display",
                     timeout=0.5)
            break
        except harness.Timeout:
            pass
    assert count("osd shown") == osd_before + 1
    send("Quiet", "--timeout", "0")
    desktop.stays(lambda: count("notifications shown") == shown_before,
                  "a card shown in do-not-disturb", duration=.5)
    loud = send("Fire", "--urgency", "2")
    wait_for(lambda: count("notifications shown") == shown_before + 1,
             "a critical card shown in do-not-disturb")
    subprocess.run([notify, "close", str(loud)], env=env, check=True, timeout=10)
    event("closed", loud, 3)
    wait_for(lambda: count("osd hidden") >= 1 and count("osd hidden") == count("osd shown"),
             "the first display gone")
    msg("dnd", "off")
    wait_for(lambda: count("osd shown") == osd_before + 2, "the display for turning it off")
    wait_for(lambda: count("osd hidden") == count("osd shown"), "the display gone")
    msg("dnd", "toggle")
    wait_for(lambda: count("osd shown") == osd_before + 3, "the display for toggling on")
    wait_for(lambda: count("osd hidden") == count("osd shown"), "the display gone")
    msg("dnd", "toggle")
    wait_for(lambda: count("osd shown") == osd_before + 4, "the display for toggling off")
    result = desktop.run("dnd", "sideways")
    assert "usage" in result.stdout + result.stderr, result

    # The on-screen display: a pill at the bottom centre that fades away.
    wait_for(lambda: count("osd hidden") == count("osd shown"), "the display gone")
    hidden = count("osd hidden")
    shown = count("osd shown")
    msg("osd", "Volume", "40")
    wait_for(lambda: count("osd shown") == shown + 1, "the display shown")
    # Above the label and the level: how far the label reaches depends on the fonts.
    shows(PANEL, (600, 612), "the display's pill")
    wait_for(lambda: count("osd hidden") == hidden + 1, "the display faded out", timeout=4)
    bare = desktop.run("osd")
    assert "usage" in bare.stdout + bare.stderr, bare

    # A backlight change shows the display too (the level is read from a fake sysfs).
    hidden = count("osd hidden")
    shown = count("osd shown")
    (backlight / "brightness").write_text("20\n")
    wait_for(lambda: count("osd shown") == shown + 1, "the display for a brightness change")
    shows(PANEL, (600, 612), "the brightness display's pill")
    wait_for(lambda: count("osd hidden") == hidden + 1, "the brightness display faded out",
             timeout=4)

    # The clock flyout, with the history: opened by the notification_history action.
    msg("notification_history")
    shows(PANEL, (1100, 560), "the clock flyout", tolerance=10)

    # Cards and the display go to the monitor with the focus (a click on its desktop gives
    # it), and the cards stay there while they last. The display showing there says the shell
    # has heard where the focus is.
    def focus_on(x, output):
        before = count(f"osd shown on {output}")
        pointer("move", str(x), "300", "click", "left")
        msg("osd", "Focus")
        wait_for(lambda: count(f"osd shown on {output}") == before + 1,
                 f"the display on {output}")

    focus_on(1900, "HEADLESS-1")
    other_shown = count("notifications shown on HEADLESS-1")
    moved = send("Elsewhere", "--timeout", "0")
    wait_for(lambda: count("notifications shown on HEADLESS-1") == other_shown + 1,
             "a card on the other monitor")
    focus_on(300, "HEADLESS-2")
    stays = send("Still there", "--timeout", "0")
    desktop.stays(lambda: count("notifications shown on HEADLESS-2")
                  == count("notifications hidden on HEADLESS-2"),
                  "a card followed the focus while another showed", duration=.5)
    assert count("notifications shown on HEADLESS-1") == other_shown + 1
    for number in (moved, stays):
        subprocess.run([notify, "close", str(number)], env=env, check=True, timeout=10)
        event("closed", number, 3)
    wait_for(lambda: count("notifications hidden on HEADLESS-1") == other_shown + 1,
             "the other monitor's cards gone")

    # The setting is followed on reload: off releases the name, on takes it again.
    desktop.config.write_text(CONFIG.replace("notifications = { timeout = 60000 }",
                                             "notifications = { enabled = false }"))
    panels.send_signal(signal.SIGHUP)

    def serving():
        return subprocess.run([notify, "info"], env=env, capture_output=True, text=True,
                              timeout=10).returncode == 0

    wait_for(lambda: not serving(), "the name released by notifications.enabled = false")
    desktop.config.write_text(CONFIG)
    panels.send_signal(signal.SIGHUP)
    wait_for(serving, "the name taken again")
    assert log().count("serving org.freedesktop.Notifications") == 2, log()

    # Quitting frees the name for another daemon.
    panels.terminate()
    desktop.reap(panels, timeout=10)
    gone = subprocess.run([notify, "info"], env=env, capture_output=True, text=True,
                          timeout=10)
    assert gone.returncode != 0, gone.stdout
    for message in ("ReferenceError", "TypeError", "is not defined", "Cannot read"):
        assert message not in log(), log()
print("notification cards, actions, expiry, hover, do-not-disturb, osd and history passed")
