# SPDX-License-Identifier: GPL-3.0-or-later
"""paw wrapped's counting: what `get wrapped` counts as a virtual pointer moves, clicks and
scrolls, keys are pressed, touchpad gestures begin, windows open, move, resize, snap and close,
workspaces switch and the overview and the switcher open; the time an application has the focus,
its streak, and idle time; the day's file written as the session ends, added to by the next
session and every few minutes, and split at midnight; wrapped.enabled turning it off; and
`paw wrapped` reporting on the files and the running session's count."""
from datetime import date, timedelta
import json
from pathlib import Path
import stat
import subprocess
import sys

import harness

compositor, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])

SCREEN = (1280, 720)
CONFIG = """return {
    xwayland = false,
    layout = { tiling = false },
    windows = { magnet = { enabled = false } },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },%s
}"""
EVENTS = ("clicks_left", "clicks_right", "clicks_middle", "clicks_other", "keys", "gestures",
          "windows_opened", "windows_closed", "moves", "resizes", "snaps", "workspace_switches",
          "overview_opens", "switcher_opens", "launches", "scroll_px")


def parse(lines):
    """A day's lines as {key: value}, with `apps` {id: seconds} and `streak` (app, seconds,
    start); get wrapped's own lines too."""
    result = {"apps": {}}
    for fields in lines:
        if not fields or fields[0].startswith("#"):
            continue
        if fields[0] == "app":
            result["apps"][fields[1]] = int(fields[2])
        elif fields[0] == "streak":
            result["streak"] = (fields[1], int(fields[2]), int(fields[3]))
        elif fields[0] == "focus":
            result["focus"] = (fields[1], int(fields[2]))
        else:
            result[fields[0]] = int(fields[1]) if fields[1].lstrip("-").isdigit() else fields[1]
    return result


def counted(desktop):
    return parse(desktop.rows("wrapped"))


def day_file(directory, day):
    return parse(line.split("\t") for line in (directory / day).read_text().splitlines())


today = date.today()
with harness.Compositor(compositor, CONFIG % "",
                        env={"PAW_TEST_WRAPPED_IDLE_MS": "1500"}) as desktop:
    msg = desktop.msg
    files = desktop.root / "state/paw/wrapped"
    desktop.detail = lambda: msg("get", "wrapped")
    start = counted(desktop)
    assert start["enabled"] == "yes" and start["day"] == today.isoformat(), start
    assert start["sessions"] == 1 and start["started"] > 0 and start["saves"] == 0, start
    assert not files.exists(), "nothing is saved before the first save"

    def stop():
        """Ends the session, as the user logging out does, and its clients with it."""
        desktop.stop()
        for client in desktop.clients:
            harness.end(client)
        desktop.clients.clear()

    def expect(action, **counts):
        """Checks that `action` adds `counts` to those counters and nothing to the others."""
        before = counted(desktop)
        action()
        after = counted(desktop)
        added = {key: after[key] - before[key] for key in EVENTS if after[key] != before[key]}
        assert added == counts, added

    desktop.open_window(probe, "A", "app.a", focused=True)
    pointer = desktop.virtual_pointer(pointer_probe, *SCREEN)
    pointer("move", "100", "100")

    def travel(action):
        """How far the pointer went during `action`, in pixels and millimetres."""
        before = counted(desktop)
        action()
        after = counted(desktop)
        return (after["pointer_px"] - before["pointer_px"],
                after["pointer_mm"] - before["pointer_mm"])

    # The pointer's travel in layout pixels, and on a monitor that gives no size at 96 dots an
    # inch: 900 pixels are 238 mm.
    pixels, millimetres = travel(lambda: pointer("move", "400", "500", "move", "400", "100"))
    assert pixels == 900 and abs(millimetres - 900 * 25.4 / 96) <= 1, (pixels, millimetres)

    expect(lambda: pointer("click", "left", "click", "right", "click", "middle"),
           clicks_left=1, clicks_right=1, clicks_middle=1)
    expect(lambda: pointer("scroll", "15", "scroll", "-30"), scroll_px=45)

    # Key presses are counted, not which; a virtual keyboard's (wtype's, the probe's modifiers)
    # are not the user's.
    keys = desktop.keyboard()
    expect(lambda: keys(30, 31, 32), keys=3)
    expect(lambda: pointer("key", "shift", "down", "key", "shift", "up"))

    msg("headless_pointer", "add", "touchpad")

    def gestures():
        for kind, fingers in (("swipe", "3"), ("pinch", "2")):
            msg("headless_pointer", kind, "touchpad", "begin", fingers)
            msg("headless_pointer", kind, "touchpad", "end")
    expect(gestures, gestures=2)

    # Moving and resizing with the pointer (Alt and a button), but not a press let go in place.
    a = next(w for w in desktop.windows() if w.title == "A")
    x, y = a.x + a.width // 2, a.y + a.height // 2
    pointer("move", str(x), str(y))

    def drag(button, dx, dy):
        pointer("key", "alt", "down", "press", button, "move", str(x + dx), str(y + dy),
                "release", button, "key", "alt", "up")
    expect(lambda: drag("left", 120, 60), clicks_left=1, moves=1)
    x, y = x + 120, y + 60
    expect(lambda: drag("left", 2, 0), clicks_left=1)
    x += 2
    expect(lambda: drag("right", 60, 80), clicks_right=1, resizes=1)

    def snaps():
        msg("snap_left")
        msg("maximize")  # not a snap: windows opening maximized go through the same place
        msg("restore")
    expect(snaps, snaps=1)

    def workspaces():
        msg("workspace", "2")
        msg("workspace", "2")
        msg("workspace", "1")
    expect(workspaces, workspace_switches=2)

    def overview():
        msg("toggle_overview")
        msg("overview_cancel")
    expect(overview, overview_opens=1)

    def switcher():
        msg("switcher")
        msg("switcher")  # the next window, in the switcher already open
        msg("switcher_cancel")
    expect(switcher, switcher_opens=1)
    expect(lambda: msg("spawn", "true"), launches=1)

    # The focused application's time and its streak, while the pointer keeps the session active.
    def nudge():
        pointer("move", "640", "360", "move", "641", "360")
        return True
    desktop.wait_for(lambda: nudge() and counted(desktop)["focus"] >= ("app.a", 2),
                     "a streak of two seconds in app.a")
    now = counted(desktop)
    assert now["focus"][0] == "app.a" and now["apps"]["app.a"] >= 2, now
    assert now["streak"][0] == "app.a" and now["streak"][1] >= 2, now["streak"]
    assert now["state"] == "active" and now["active"] >= 2, now

    # Without input the session goes idle, which ends the streak.
    desktop.wait_for(lambda: counted(desktop)["state"] == "idle", "idle")
    desktop.wait_for(lambda: counted(desktop)["idle"] >= 1, "a second of idle time")
    assert counted(desktop)["focus"] == ("app.a", 0)
    nudge()
    assert counted(desktop)["state"] == "active"

    # Another application takes the focus; a window closing is counted.
    opened = []
    expect(lambda: opened.append(desktop.open_window(probe, "B", "app.b", focused=True)),
           windows_opened=1)
    assert counted(desktop)["focus"][0] == "app.b"

    def close():
        msg("close")
        desktop.reap(opened[0])
        desktop.wait_for(lambda: len(desktop.windows()) == 1, "B closed")
    expect(close, windows_closed=1)
    last = counted(desktop)
    assert last["saves"] == 0 and last["windows_opened"] == 2, last

    # Ending the session saves the day, for the user alone.
    stop()
    saved = day_file(files, today.isoformat())
    assert all(saved[key] == last[key] for key in EVENTS), (saved, last)
    assert saved["sessions"] == 1 and saved["started"] == start["started"], saved
    assert saved["active"] >= last["active"] and saved["apps"]["app.a"] >= 2, saved
    assert stat.S_IMODE(files.stat().st_mode) == 0o700
    assert stat.S_IMODE((files / today.isoformat()).stat().st_mode) == 0o600

    # The next session adds to the day's file, also every few minutes while it runs (here
    # 300 ms).
    desktop.env["PAW_TEST_WRAPPED_SAVE_MS"] = "300"
    desktop.start()
    keys = desktop.keyboard()
    keys(30, 31)
    desktop.wait_for(lambda: day_file(files, today.isoformat())["keys"] == saved["keys"] + 2,
                     "the keys saved while the session runs")
    assert counted(desktop)["saves"] >= 1 and counted(desktop)["keys"] == 0
    assert day_file(files, today.isoformat())["sessions"] == 2

    # wrapped.enabled = false saves what was counted and counts nothing more; turned on again,
    # it counts on in the same session.
    desktop.reload(CONFIG % "\n    wrapped = { enabled = false },")
    assert desktop.rows("wrapped") == [["enabled", "no"]]
    keys(30)
    desktop.reload(CONFIG % "")
    assert counted(desktop)["enabled"] == "yes" and counted(desktop)["sessions"] == 0
    stop()
    second = day_file(files, today.isoformat())
    assert second["keys"] == saved["keys"] + 2 and second["sessions"] == 2, second
    assert second["windows_opened"] == 2, "no window opened in the second session"

    # At midnight the day's file is saved and the next day starts (here a pretend midnight two
    # seconds after the session starts).
    desktop.env["PAW_TEST_WRAPPED_MIDNIGHT"] = "2"
    desktop.start()
    tomorrow = (today + timedelta(days=1)).isoformat()
    desktop.wait_for(lambda: counted(desktop)["day"] == tomorrow, "the next day")
    assert day_file(files, today.isoformat())["sessions"] == 3
    assert counted(desktop)["sessions"] == 0, "the session started the day before"
    stop()
    assert day_file(files, tomorrow)["sessions"] == 0
    assert sorted(path.name for path in files.iterdir()) == [today.isoformat(), tomorrow]

    def wrapped(*words, session=True):
        """`paw wrapped WORDS`' output, in the session or outside it."""
        env = dict(desktop.env)
        if not session:
            env.pop("PAW_SOCKET", None)
        result = subprocess.run([compositor, "wrapped", *words], env=env, capture_output=True,
                                text=True, timeout=30)
        assert result.returncode == 0, result.stderr
        return result.stdout

    # The report adds up the files, outside a session too.
    report = json.loads(wrapped("--all", "--json", session=False))
    assert report["days"] == 2 and report["from"] is None, report
    assert report["counters"]["keys"] == second["keys"], report
    assert report["counters"]["sessions"] == 3 and report["apps"][0]["id"] == "app.a", report
    assert report["streak"]["app"] == "app.a" and report["busiest_day"] == today.isoformat()
    text = wrapped("--plain", session=False)
    assert text.startswith("paw wrapped %d: " % today.year), text
    assert "\nTop apps: Your top app was app.a: " in text, text
    assert "\nKeyboard: You pressed %d keys." % second["keys"] in text, text
    assert "\nWindows: You opened 2 windows and closed 1.\n" in text, text
    assert "\nThat's a wrap: See you next time.\n" in text, text
    assert wrapped(session=False) == text, "plain without a terminal"
    assert wrapped("--since", tomorrow, session=False).startswith("Nothing counted in that time")

    # In a session, what it counted and has not saved joins the files, once, though it saves
    # every 300 ms meanwhile.
    del desktop.env["PAW_TEST_WRAPPED_MIDNIGHT"]
    desktop.start()
    keys = desktop.keyboard()
    keys(30, 31, 32)
    desktop.wait_for(lambda: json.loads(wrapped("--today", "--json"))["counters"]["keys"] ==
                     second["keys"] + 3, "the session's keys in the report")
    for _ in range(5):
        assert json.loads(wrapped("--today", "--json"))["counters"]["keys"] == second["keys"] + 3
    desktop.reload(CONFIG % "\n    wrapped = { enabled = false },")
    assert "\n  Counting is off now: wrapped.enabled is false.\n" in wrapped("--plain")
    stop()
