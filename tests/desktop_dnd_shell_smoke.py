# SPDX-License-Identifier: GPL-3.0-or-later
"""Drag and drop between the desktop's icons and other windows in a session (shell.desktop_icons),
with real button presses from pointer_probe: a file a window drags as a file manager does
(text/uri-list) and drops on the bare desktop is copied into the desktop folder in the cell under
the pointer, the window hearing the drop taken as a copy; one whose drag offers only to move it is
moved there. An icon dragged on the desktop is a drag of the compositor's from the desktop's layer
surface, which over the desktop moves the icon to the cell it is let go on; carried onto a window,
it hands the window the file's address. The shell's part of it, the pads, the bubble and folders,
is shell_ui's; the edge cases of names and copies are desktop_icons_test's."""
from pathlib import Path
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
WIDTH, HEIGHT = 1280, 720
# The middle of the first row's icons, as the grid lays them with icons of 48 pixels: a margin of
# 12, cells 91 wide, the icon 8 below the cell's top, from the desktop's top left (the bar is at
# the bottom).
ROW = 44


def column(n):
    return 12 + 91 * n + 45


with harness.Compositor(compositor, CONFIG, start=False) as desktop:
    root, env, msg = desktop.root, desktop.env, desktop.msg
    folder, outside = root / "Desktop", root / "outside"
    folder.mkdir()
    outside.mkdir()
    (folder / "note.txt").write_text("note\n")
    (outside / "copied.txt").write_text("copied\n")
    (outside / "moved.txt").write_text("moved\n")
    env.update(QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software",
               QT_FORCE_STDERR_LOGGING="1", XDG_DATA_HOME=str(root), XDG_DATA_DIRS=str(root),
               DBUS_SESSION_BUS_ADDRESS="disabled:")
    desktop.start()
    shell_log = root / "shell.log"
    desktop.spawn([shell, "--config", str(desktop.config)], log="shell.log")
    state = root / "state" / "paw" / "desktop-icons"

    def cells():
        """{key: (column, row)} the shell remembers."""
        found = {}
        for line in state.read_text().splitlines() if state.exists() else []:
            fields = line.split("\t")
            if len(fields) == 3:
                found[fields[2]] = (int(fields[0]), int(fields[1]))
        return found

    def seat():
        return {row[0]: (row[1], row[2]) for row in desktop.rows("seat")}

    def log(name):
        return (root / f"{name}.log").read_text()

    desktop.logs += [root / f"{name}.log" for name in ("copier", "mover", "taker")]
    desktop.detail = lambda: (f"cells: {cells()}, seat: {seat()}, windows: {desktop.windows()}\n"
                              f"{shell_log.read_text()[-1500:]}")
    desktop.wait_for(lambda: cells().get("file:note.txt") == (0, 0), "the desktop's icon")

    # Windows clear of the desktop's first row: two dragging files, one of them offering only to
    # move them, as GTK's file managers do with Shift held, and one taking them.
    def window(title, role, snap, **env):
        desktop.open_window(probe, title, title.lower(), focused=True, log=f"{title.lower()}.log",
                            env={"PAW_PROBE_DRAG": role, "PAW_PROBE_DRAG_MIME": "text/uri-list",
                                 **env})
        msg(snap)

        def box():
            return [w for w in desktop.windows() if w.title == title][0].box

        def snapped():
            x, y, w, h = box()
            return (w < WIDTH // 2 and h < HEIGHT // 2 and (x >= WIDTH // 2) == ("right" in snap)
                    and (y >= HEIGHT // 3) == ("bottom" in snap))
        desktop.wait_for(snapped, f"{title} snapped to its quarter")
        x, y, w, h = box()
        return x + w // 2, y + h // 2

    copier = window("Copier", "source", "snap_bottom_right",
                    PAW_PROBE_DRAG_TEXT=(outside / "copied.txt").as_uri() + "\r\n")
    mover = window("Mover", "source", "snap_bottom_left",
                   PAW_PROBE_DRAG_TEXT=(outside / "moved.txt").as_uri() + "\r\n",
                   PAW_PROBE_DRAG_ACTIONS="move")
    taker = window("Taker", "target", "snap_top_right")
    pointer = desktop.virtual_pointer(pointer_probe, WIDTH, HEIGHT)

    def drag(start, path, source, done, message):
        """Drags from `start` along `path` and lets go once the window `source` hears that the
        desktop takes it, as a hand lets go a moment later; `done` once dropped."""
        pointer("move", *map(str, start), "press", "left")
        desktop.wait_for(lambda: "drag" in seat(), f"{message}: the drag under way")
        for x, y in path:
            pointer("move", str(x), str(y))
        desktop.wait_for(lambda: seat().get("drag") == ("layer", "paw-desktop") and
                         "drag target text/uri-list" in log(source),
                         f"{message}: the desktop taking the drag")
        pointer("release", "left")
        desktop.wait_for(done, message)

    # A file a window drags, dropped on the desktop's fourth column, is copied there.
    drag(copier, [(640, 200), (column(3), ROW + 10), (column(3), ROW)], "copier",
         lambda: cells().get("file:copied.txt") == (3, 0) and (folder / "copied.txt").exists(),
         "the file copied into the cell it was dropped on")
    assert (outside / "copied.txt").exists(), "the file dropped was moved, not copied"
    desktop.wait_for(lambda: "drag finished" in log("copier"), "the source told the drop was taken")
    assert "drag action 1" in log("copier"), log("copier")  # a copy
    # Offered only to move, it is moved.
    drag(mover, [(column(5), ROW + 10), (column(5), ROW)], "mover",
         lambda: cells().get("file:moved.txt") == (5, 0) and not (outside / "moved.txt").exists(),
         "the file moved into the cell it was dropped on")
    desktop.wait_for(lambda: "drag action 2" in log("mover") and "drag finished" in log("mover"),
                     "the source told the drop was taken as a move")

    # An icon dragged on the desktop is the compositor's drag from the desktop, and lands where it
    # is let go.
    pointer("move", str(column(0)), str(ROW), "press", "left")
    for step in range(1, 6):
        pointer("move", str(column(0) + 18 * step), str(ROW))
    desktop.wait_for(lambda: seat().get("drag") == ("layer", "paw-desktop"),
                     "the icon's drag over the desktop")
    pointer("move", str(column(2) - 10), str(ROW), "move", str(column(2)), str(ROW),
            "release", "left")
    desktop.wait_for(lambda: cells().get("file:note.txt") == (2, 0),
                     "the icon dropped two cells on")
    desktop.wait_for(lambda: seat().get("pointer") == ("layer", "paw-desktop"),
                     "the pointer back on the desktop")

    # Carried onto a window that takes files, the icon hands it the file's address.
    pointer("move", str(column(2)), str(ROW), "press", "left")
    for step in range(1, 6):
        pointer("move", str(column(2) + 18 * step), str(ROW + 10 * step))
    desktop.wait_for(lambda: "drag" in seat(), "the icon's drag")
    pointer("move", "700", "150", "move", *map(str, taker))
    desktop.wait_for(lambda: seat().get("drag") == ("window", "Taker") and
                     "drop offered" in log("taker"), "Taker taking the icon's drag")
    pointer("release", "left")
    desktop.wait_for(lambda: "drop received " + (folder / "note.txt").as_uri() in log("taker"),
                     "Taker given the file's address")
    assert cells().get("file:note.txt") == (2, 0) and (folder / "note.txt").exists(), cells()

    messages = "\n".join(line for line in shell_log.read_text().splitlines()
                         if not line.startswith("["))
    for message in ("ReferenceError", "TypeError", "is not defined", "Cannot read"):
        assert message not in messages, messages
print("Files dragged from windows land on the desktop where they are dropped, copied or moved, and "
      "its icons are dragged as the compositor's drags, on the desktop and out to windows")
