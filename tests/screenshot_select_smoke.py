# SPDX-License-Identifier: GPL-3.0-or-later
"""Region screenshots through the real slurp, as on Hyprland with grimblast: a click on a window
takes the window, a click on the bare desktop the monitor under it, and a drag the region dragged;
what a click would take shows clear of slurp's grey, outlined in the focused border's colour; and
the cursor has no image while grim takes the picture, so that an output that draws it into its
frames (a software cursor) leaves it out. A stand-in grim records what it is asked to take and
whether the cursor shows, and hands the picture to the real grim, with the cursor asked for, when
that is installed. Left out where slurp is not installed."""
from pathlib import Path
import sys

import harness

compositor, probe, pointer_probe, slurp, grim = sys.argv[1:6]
if not slurp:
    print("slurp is not installed: region selection not tested")
    sys.exit(0)

with harness.Compositor(compositor, start=False, env={"WLR_HEADLESS_OUTPUTS": "2"}) as desktop:
    root, msg = desktop.root, desktop.msg
    tools = root / "bin"
    tools.mkdir()
    (tools / "slurp").symlink_to(slurp)
    tool_log = root / "tools.log"
    tool_log.touch()
    desktop.logs.append(tool_log)
    cursor_log = root / "cursor.log"
    (tools / "grim").write_text('#!/bin/sh\necho "grim $*" >> "$TOOL_LOG"\n'
                                '"$PAW" msg get cursor >> "$CURSOR_LOG"\n'
                                'if [ -n "$REAL_GRIM" ]; then exec "$REAL_GRIM" -c -t ppm "$@"; fi\n'
                                'for last; do :; done; echo "fake png" > "$last"\n')
    (tools / "grim").chmod(0o755)
    desktop.env.update(PATH=str(tools), TOOL_LOG=str(tool_log), CURSOR_LOG=str(cursor_log),
                       PAW=compositor, REAL_GRIM=grim)
    desktop.start(f"""return {{
        xwayland = false,
        windows = {{ border_color = "#ff8000" }},
        screenshots = {{ directory = "{root / 'shots'}", clipboard = false, notify = false }},
    }}""")

    outputs = [row for row in desktop.rows("outputs")]
    assert len(outputs) == 2, outputs
    second = [int(field) for field in outputs[1][2:6]]
    width = max(int(row[2]) + int(row[4]) for row in outputs)
    height = max(int(row[3]) + int(row[5]) for row in outputs)
    # slurp reads the keymap before its keyboard focus; a seat without a keyboard sends none.
    desktop.keyboard()
    pointer = desktop.virtual_pointer(pointer_probe, width, height)
    pointer("move 10 10")
    desktop.open_window(probe, "shot", focused=True)
    (window,) = [w for w in desktop.windows() if w.title == "shot"]
    x, y, w, h = window.box

    def start():
        """Starts a region screenshot and waits for slurp's layer."""
        tool_log.write_text("")
        cursor_log.write_text("")
        # A screenshot is refused until the one before has been reaped.
        desktop.wait_for(lambda: desktop.run("screenshot", "region").returncode == 0,
                         "region screenshot started")
        # slurp has a layer on each output; the pointer moves between them.
        desktop.wait_for(lambda: sorted(row[1] for row in desktop.rows("layers")
                                        if row[0] == "selection" and row[3] == "1")
                         == sorted(row[0] for row in outputs),
                         "slurp's layers shown", detail=lambda: desktop.rows("layers"))

    def select(*moves, started=False):
        """Starts a region screenshot unless `started`, does `moves` once slurp is up, and
        returns the geometry grim was given and the file it saved, once the screenshot is over.
        The cursor was hidden while grim ran, and shows again."""
        if not started:
            start()
        for move in moves:
            pointer(move)
        desktop.wait_for(lambda: cursor_log.read_text(), "grim run",
                         detail=lambda: tool_log.read_text())
        assert cursor_log.read_text() == "hidden\n", cursor_log.read_text()
        desktop.wait_for(lambda: msg("get", "cursor") == "shown\n", "the cursor shown again")
        words = tool_log.read_text().splitlines()[0].split()
        assert words[1] == "-g", words
        return " ".join(words[2:4]), Path(words[-1])

    def drawn(shot, at, colour):
        """Whether anything but `colour` is drawn where the cursor would be, from `at`."""
        return any(shot.at(at[0] + dx, at[1] + dy) != colour
                   for dx in range(16) for dy in range(24))

    # Over the window, slurp greys everything but the window, which a click would take, and
    # outlines it in the focused border's colour; over the bare desktop, everything but the
    # monitor.
    middle, beside = (x + w // 2, y + h // 2), (x + w + 100, y + h // 2)
    window_colour = (65, 123, 196)  # wayland_probe's, below its title bar
    clear = harness.grab(grim, desktop.env).at(*beside) if grim else None
    if grim:
        # The default cursor, from the desktop, stays over the probe, which sets none.
        pointer(f"move {middle[0]} {middle[1]}")
        desktop.wait_for(lambda: drawn(harness.grab(grim, desktop.env, cursor=True), middle,
                                       window_colour), "the cursor in a picture of the screen")
    start()
    if grim:
        pointer(f"move {beside[0]} {beside[1] + 100}")
        desktop.wait_for(lambda: harness.grab(grim, desktop.env).at(*beside) == clear,
                         "the monitor clear under the pointer")
        window = harness.grab(grim, desktop.env).at(*middle)
        pointer(f"move {middle[0]} {middle[1]}")
        desktop.wait_for(lambda: harness.grab(grim, desktop.env).at(*beside) != clear,
                         "the desktop greyed once the pointer is over the window")
        shot = harness.grab(grim, desktop.env)
        assert shot.at(*middle) == window, (shot.at(*middle), window)
        grey = shot.at(*beside)
        assert all(a > b for a, b in zip(grey, clear)), (grey, clear)
        assert shot.at(x - 1, middle[1]) == shot.at(x, middle[1]) == (255, 128, 0), \
            [shot.at(i, middle[1]) for i in range(x - 2, x + 2)]
    # A click on the window takes it, without the cursor over it even when it is asked for.
    geometry, saved = select(f"move {middle[0]} {middle[1]}", "click left", started=True)
    assert geometry == f"{x},{y} {w}x{h}", geometry
    if grim:
        assert not drawn(harness.Shot(saved.read_bytes()), (w // 2, h // 2), window_colour)
        desktop.wait_for(lambda: drawn(harness.grab(grim, desktop.env, cursor=True), middle,
                                       window_colour), "the cursor drawn again")
    # A click on the bare desktop takes the monitor under the pointer.
    geometry, _ = select(f"move {second[0] + second[2] - 20} {second[1] + second[3] - 20}",
                         "click left")
    assert geometry == "{},{} {}x{}".format(*second), geometry
    # A drag takes the region from the press to the release.
    region, _ = select("move 100 400", "press left", "move 200 450", "move 300 500",
                       "release left")
    position, size = region.split()
    assert position == "100,400", region
    assert size in ("200x100", "201x101"), region
print("A click took the window or the monitor, and a drag a region")
