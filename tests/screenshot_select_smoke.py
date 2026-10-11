# SPDX-License-Identifier: GPL-3.0-or-later
"""Region screenshots through the real slurp, as on Hyprland with grimblast: a click on a window
takes the window, a click on the bare desktop the monitor under it, and a drag the region dragged;
what a click would take shows clear of slurp's grey, outlined in the focused border's colour. A
stand-in grim records what it is asked to take, and the real one, when installed, what is on the
screen. Left out where slurp is not installed."""
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
    (tools / "grim").write_text('#!/bin/sh\necho "grim $*" >> "$TOOL_LOG"\n'
                                'for last; do :; done; echo "fake png" > "$last"\n')
    (tools / "grim").chmod(0o755)
    desktop.env.update(PATH=str(tools), TOOL_LOG=str(tool_log))
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
        # A screenshot is refused until the one before has been reaped.
        desktop.wait_for(lambda: desktop.run("screenshot", "region").returncode == 0,
                         "region screenshot started")
        desktop.wait_for(lambda: ["selection", "1"] in [[row[0], row[3]]
                                                         for row in desktop.rows("layers")],
                         "slurp's layer shown", detail=lambda: desktop.rows("layers"))

    def select(*moves, started=False):
        """Starts a region screenshot unless `started`, does `moves` once slurp is up, and
        returns the geometry grim was given."""
        if not started:
            start()
        for move in moves:
            pointer(move)
        desktop.wait_for(lambda: tool_log.read_text().startswith("grim"), "grim run",
                         detail=lambda: tool_log.read_text())
        words = tool_log.read_text().splitlines()[0].split()
        assert words[1] == "-g", words
        return " ".join(words[2:4])

    # Over the window, slurp greys everything but the window, which a click would take, and
    # outlines it in the focused border's colour; over the bare desktop, everything but the
    # monitor.
    middle, beside = (x + w // 2, y + h // 2), (x + w + 100, y + h // 2)
    clear = harness.grab(grim, desktop.env).at(*beside) if grim else None
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
    # A click on the window takes it.
    assert select(f"move {middle[0]} {middle[1]}", "click left",
                  started=True) == f"{x},{y} {w}x{h}"
    # A click on the bare desktop takes the monitor under the pointer.
    assert select(f"move {second[0] + second[2] - 20} {second[1] + second[3] - 20}",
                  "click left") == "{},{} {}x{}".format(*second)
    # A drag takes the region from the press to the release.
    region = select("move 100 400", "press left", "move 200 450", "move 300 500",
                    "release left")
    position, size = region.split()
    assert position == "100,400", region
    assert size in ("200x100", "201x101"), region
print("A click took the window or the monitor, and a drag a region")
