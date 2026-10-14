# SPDX-License-Identifier: GPL-3.0-or-later
"""`paw tour` on a headless compositor without the shell, its windows wayland_probe's: the queries
it reads (actions and their keys, profiles, the terminal), and the caption it would show, refused
without the shell. It runs on an empty workspace, opens its windows there and closes them, leaves
the window that was there before as it was, and puts the workspace, the tiling and its layout,
night light, the appearance profile and its saved choice back; interrupted with SIGINT midway, or
with one of its windows closed, it cleans up the same way. --list works without a session."""
import os
from pathlib import Path
import signal
import subprocess
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    layout = { workspaces = 4 },
    terminal = { "foot", "--app-id", "a terminal" },
    profile = "day",
    profiles = { day = {}, night = { appearance = { background = "#101020" } } },
    bindings = {
        { mods = { "Super" }, key = "Tab", action = "workspace_back" },
        { mods = { "Super", "Shift" }, key = "minus", action = "move_to_scratchpad" },
    },
}"""
CLIENT = f"env PAW_PROBE_TITLE={{title}} {probe} --window-only"

with harness.Compositor(compositor, CONFIG) as desktop:
    msg, root = desktop.msg, desktop.root

    # What the tour reads.
    actions = {row[0]: row[1] for row in desktop.rows("actions")}
    assert actions["workspace_back"] == "Super + Tab", actions.get("workspace_back")
    assert actions["move_to_scratchpad"] == "Super + Shift + minus"
    assert actions["toggle_tiling"] == "-", "unbound here"
    assert len(actions) > 100 and all(len(row) == 2 for row in desktop.rows("actions"))
    assert desktop.rows("profiles") == [["day", "1"], ["night", "0"]], desktop.rows("profiles")
    assert desktop.rows("terminal") == [["foot", "--app-id", "a terminal"]], \
        desktop.rows("terminal")

    # A caption needs the shell to show it.
    assert "no shell" in msg("caption", "5", "Title\tBody\tSuper + S", ok=False)
    for bad in (["caption"], ["caption", "0", "Title"], ["caption", "601", "Title"],
                ["caption", "5"], ["caption", "soon", "Title"], ["caption", "5", "\tBody"]):
        assert "usage" in msg(*bad, ok=False), bad
    msg("caption", "hide")

    # A window of the user's, floating on workspace 1 with the focus.
    desktop.open_window(probe, "mine", focused=True)

    def mine():
        return next(w for w in desktop.windows() if w.title == "mine")

    before = mine()
    night = desktop.rows("night_light")[0][1]
    profile = root / "state" / "paw" / "profile"
    assert not profile.exists()

    def tour_windows():
        return [w for w in desktop.windows() if w.title.startswith("paw tour")]

    def unchanged():
        """Everything the tour may change is as it was."""
        assert not tour_windows(), tour_windows()
        assert mine() == before, (mine(), before)
        assert desktop.rows("workspaces")[0][1:5] == ["1", "1", "1", "off"], \
            desktop.rows("workspaces")
        assert desktop.rows("layout", "HEADLESS-1", "2")[0][0] == "dwindle"
        assert desktop.rows("profiles") == [["day", "1"], ["night", "0"]]
        assert not profile.exists(), profile.read_text()
        assert desktop.rows("night_light")[0][1] == night
        assert "tour" not in msg("session", "list")

    def probe_of(title):
        """The process of the probe showing the window titled `title`, by the title it was given."""
        found = []
        for entry in Path("/proc").iterdir():
            try:
                if entry.name.isdigit() and f"\0PAW_PROBE_TITLE={title}\0".encode() in \
                        b"\0" + (entry / "environ").read_bytes():
                    found.append(int(entry.name))
            except OSError:
                pass  # another user's, or gone meanwhile
        assert len(found) == 1, found
        return found[0]

    def tour(*arguments, log):
        """`paw tour` with the probe for its windows, its output in `log`."""
        path = root / log
        desktop.logs.append(path)
        with path.open("w") as output:
            return subprocess.Popen(
                [compositor, "tour", "--client", CLIENT, *arguments],
                env={**desktop.env, "GSETTINGS_BACKEND": "memory"}, stdout=output,
                stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)

    # The whole tour, quickly: its windows open on workspace 2, the first empty one, every step
    # runs, and everything is put back.
    seen = set()
    process = tour("--speed", "20", log="tour.log")
    desktop.detail = lambda: (root / "tour.log").read_text()

    def watch():
        """Notes the tour's windows, on its workspaces only, while it runs; true once it ended."""
        for window in tour_windows():
            seen.add(window.title)
            assert (window.workspace, window.output) in ((2, "HEADLESS-1"), (3, "HEADLESS-1")), \
                window
        assert any(w.title == "mine" for w in desktop.windows()), "the user's window stays"
        return process.poll() is not None

    desktop.wait_for(watch, "the tour ended", timeout=120)
    text = (root / "tour.log").read_text()
    assert process.returncode == 0, text
    assert seen == {"paw tour 1", "paw tour 2", "paw tour 3", "paw tour 4"}, seen
    assert "[15/15] That's paw" in text and "skipped" not in text, text
    assert "Your desktop is back as it was." in text, text
    unchanged()

    # Interrupted once the monitor tiles, in the middle of the tour, it puts everything back.
    process = tour("--speed", "2", log="interrupted.log")
    desktop.detail = lambda: (root / "interrupted.log").read_text()
    desktop.wait_for(lambda: "[6/15]" in (root / "interrupted.log").read_text(),
                     "the tour reached the scrolling layout", timeout=60)
    assert desktop.rows("workspaces")[0][4] == "on" and len(tour_windows()) == 3
    process.send_signal(signal.SIGINT)
    assert process.wait(timeout=60) == 130, (root / "interrupted.log").read_text()
    assert "Stopping: interrupted." in (root / "interrupted.log").read_text()
    unchanged()

    # Interrupted while the other profile shows, it comes back, and the saved choice goes again.
    process = tour("--speed", "2", "--from", "13", log="profile.log")
    desktop.detail = lambda: (root / "profile.log").read_text()
    desktop.wait_for(lambda: ["night", "1"] in desktop.rows("profiles"),
                     "the tour switched profiles", timeout=60)
    assert profile.read_text() == "night\n"
    process.send_signal(signal.SIGINT)
    assert process.wait(timeout=60) == 130, (root / "profile.log").read_text()
    unchanged()

    # One of its windows closed by the user stops it the same way.
    process = tour("--speed", "2", "--from", "4", log="closed.log")
    desktop.detail = lambda: (root / "closed.log").read_text()
    desktop.wait_for(lambda: len(tour_windows()) == 3 and all(w.tiled for w in tour_windows()),
                     "the windows tiled", timeout=60)
    os.kill(probe_of("paw tour 2"), signal.SIGTERM)
    assert process.wait(timeout=60) == 1, (root / "closed.log").read_text()
    assert "the window paw tour 2 was closed" in (root / "closed.log").read_text()
    unchanged()

    # The list of steps, with the keys of this configuration, and without a session.
    listing = subprocess.run([compositor, "tour", "--list"], env=desktop.env, text=True,
                             capture_output=True, timeout=30)
    assert listing.returncode == 0, listing.stderr
    assert "11  Workspaces  (Super + Tab)" in listing.stdout, listing.stdout
    assert "(paw msg toggle_tiling)" in listing.stdout, listing.stdout
    offline = subprocess.run([compositor, "tour", "--list"], text=True, capture_output=True,
                             env={k: v for k, v in desktop.env.items() if k != "PAW_SOCKET"},
                             timeout=30)
    assert offline.returncode == 0 and " 1  Welcome to paw" in offline.stdout, offline
    assert "(workspace_next / workspace_back)" in offline.stdout, offline.stdout

    # Refused with a window of an earlier tour still open, before changing anything.
    leftover = desktop.open_window(probe, "paw tour 1")
    refused = subprocess.run([compositor, "tour", "--client", CLIENT], env=desktop.env,
                             text=True, capture_output=True, timeout=30)
    assert refused.returncode == 1 and "earlier tour" in refused.stderr, refused
    leftover.terminate()
    desktop.reap(leftover)
    desktop.wait_for(lambda: not tour_windows(), "the leftover window closed")
    unchanged()
