# SPDX-License-Identifier: GPL-3.0-or-later
"""Take screenshots through the control socket with stand-ins for grim, slurp, and wl-copy."""
from pathlib import Path
import re
import sys

import harness

compositor, probe, example = (str(Path(p).resolve()) for p in sys.argv[1:4])

# Each stand-in records its arguments; grim writes the file it is given, and whether the cursor
# shows as it runs. Only shell builtins and paw are used, because the compositor's PATH holds
# nothing but these.
TOOLS = {
    "grim": 'echo "grim $*" >> "$TOOL_LOG"; "$PAW" msg get cursor >> "$TOOL_LOG.cursor"; '
            'for last; do :; done; echo "fake png" > "$last"',
    # Records the boxes it is given on standard input, each in <>, and waits, like a user
    # choosing a region, until the test creates $TOOL_LOG.go.
    "slurp": 'boxes=; while IFS= read -r box || [ -n "$box" ]; do boxes="$boxes<$box>"; done; '
             'echo "slurp $* $boxes" >> "$TOOL_LOG"; while [ ! -e "$TOOL_LOG.go" ]; do :; done; '
             'echo "10,20 30x40"',
    "wl-copy": 'IFS= read -r data; echo "wl-copy $* <$data>" >> "$TOOL_LOG"',
    "notify-send": 'echo "notify-send $*" >> "$TOOL_LOG"',
}

with harness.Compositor(compositor, start=False) as desktop:
    root, msg = desktop.root, desktop.msg
    tools = root / "bin"
    tools.mkdir()
    for name, body in TOOLS.items():
        (tools / name).write_text("#!/bin/sh\n" + body + "\n")
        (tools / name).chmod(0o755)
    tool_log = root / "tools.log"
    tool_log.touch()
    desktop.logs.append(tool_log)
    shots = root / "shots"
    desktop.env.update(PATH=str(tools), TOOL_LOG=str(tool_log), PAW=compositor)
    cursor_log = Path(str(tool_log) + ".cursor")

    def cursor_hidden_while_grim_ran():
        """Whether the cursor was hidden while grim ran, and shows again since."""
        hidden = cursor_log.read_text() == "hidden\n"
        cursor_log.write_text("")
        desktop.wait_for(lambda: msg("get", "cursor") == "shown\n", "the cursor shown again")
        return hidden

    def calls():
        return tool_log.read_text().splitlines()

    def saved():
        return set(shots.glob("Screenshot_*.png"))

    def slurp_boxes(call):
        """The boxes a slurp call was given, after checking its arguments: monitors to click
        too, everything greyed but the selection, outlined in an opaque colour."""
        match = re.fullmatch(
            r"slurp -o -b ffffff40 -B ffffff40 -s 00000000 -c [0-9a-f]{6}ff (.*)", call)
        assert match, call
        return match[1]

    desktop.start(Path(example).read_text()
                  .replace("xwayland = true", "xwayland = false")
                  .replace('-- directory = "~/Pictures/Screenshots"', f'directory = "{shots}"'))
    assert "no focused window" in msg("screenshot", "window", ok=False)
    assert "mode must be" in msg("screenshot", "screen", ok=False)
    assert "one mode" in msg("screenshot", "window", "output", ok=False)

    # The output under the pointer, copied to the clipboard and announced.
    output_name = msg("get", "outputs").split("\t")[0]
    msg("screenshot", "output")
    desktop.wait_for(lambda: any(c.startswith("notify-send") for c in calls()),
                     "output screenshot", detail=calls)
    (first,) = saved()
    assert first.parent == shots and re.fullmatch(
        r"Screenshot_\d{4}-\d\d-\d\d_\d\d-\d\d-\d\d\.png", first.name), first
    assert first.read_text() == "fake png\n"
    assert calls() == [f"grim -o {output_name} {first}",
                       "wl-copy --type image/png <fake png>",
                       f"notify-send -a paw -i {first} Screenshot saved {first}"], \
        calls()
    assert f"Screenshot saved: {first}" in desktop.log.read_text()
    # The cursor is hidden at once, before grim starts.
    assert cursor_hidden_while_grim_ran()

    # The focused window's box, in layout coordinates.
    tool_log.write_text("")
    desktop.spawn([probe, "--external-control"])

    def focused():
        return [w.box for w in desktop.windows() if w.focused]
    desktop.wait_for(lambda: focused(), "focused window")
    (x, y, width, height), = focused()
    msg("screenshot", "window")
    desktop.wait_for(lambda: len(calls()) == 3, "window screenshot", detail=calls)
    (second,) = saved() - {first}
    assert calls()[0] == f"grim -g {x},{y} {width}x{height} {second}", calls()
    assert cursor_hidden_while_grim_ran()

    # Screenshots within the same second get distinct names. Each waits for the one
    # before to be reaped, since a request while one is running is refused.
    def screenshot_accepted(*words):
        result = desktop.run("screenshot", *words)
        assert result.returncode == 0 or "already" in result.stderr, result.stderr
        return result.returncode == 0
    tool_log.write_text("")
    before = saved()
    desktop.wait_for(lambda: screenshot_accepted("output"), "first quick screenshot")
    desktop.wait_for(lambda: screenshot_accepted("output"), "second quick screenshot")
    desktop.wait_for(lambda: len(calls()) == 6, "two quick screenshots", detail=calls)
    assert len(saved() - before) == 2, saved()

    # A region comes from slurp, which offers the window and the monitors to click; without
    # wl-copy the file is still saved.
    (tools / "wl-copy").unlink()
    tool_log.write_text("")
    before = saved()
    desktop.wait_for(lambda: screenshot_accepted(), "region screenshot start")
    desktop.wait_for(lambda: calls(), "slurp", detail=calls)
    assert slurp_boxes(calls()[0]) == f"<{x},{y} {width}x{height}>", calls()
    # Pressing Print again while choosing a region takes no second screenshot.
    assert "already being taken" in msg("screenshot", ok=False)
    assert "already being taken" in msg("screenshot", "output", ok=False)
    Path(str(tool_log) + ".go").touch()
    desktop.wait_for(lambda: len(calls()) == 3, "region screenshot", detail=calls)
    (region,) = saved() - before
    assert calls()[1] == f"grim -g 10,20 30x40 {region}", calls()
    assert calls()[2].startswith("notify-send"), calls()
    assert "wl-copy is not installed" in desktop.log.read_text()

    def offered():
        """The boxes slurp is given for the next region screenshot."""
        tool_log.write_text("")
        desktop.wait_for(lambda: screenshot_accepted("region"), "region screenshot start")
        desktop.wait_for(lambda: len(calls()) == 3, "region screenshot", detail=calls)
        return slurp_boxes(calls()[0])

    # Every window shown, the front one first, cut to its monitor.
    desktop.open_window(probe, "second", focused=True)
    (front,) = [w.box for w in desktop.windows() if w.title == "second"]
    assert offered() == "<{},{} {}x{}><{},{} {}x{}>".format(*front, x, y, width, height), calls()
    # One a window in front covers whole, as a fullscreen one does, is left out: slurp takes the
    # smallest box under the pointer, which would be the one hidden.
    msg("fullscreen")

    def covering():
        (box,) = [w.box for w in desktop.windows() if w.title == "second"]
        return box if (box[0] <= x and box[1] <= y and box[0] + box[2] >= x + width
                       and box[1] + box[3] >= y + height) else None
    desktop.wait_for(covering, "the fullscreen window over the first", detail=desktop.windows)
    assert offered() == "<{},{} {}x{}>".format(*covering()), calls()

    # Missing tools are reported to the caller and nothing runs.
    (tools / "slurp").unlink()
    assert "grim and slurp" in msg("screenshot", "region", ok=False)
    (tools / "grim").unlink()
    assert "need grim" in msg("screenshot", "output", ok=False)
    assert len(saved()) == 7

    # A screenshot taken without the action, as paw showcase takes one, hides the cursor too,
    # until it says so or for five seconds at most.
    msg("cursor", "hide")
    assert msg("get", "cursor") == "hidden\n"
    msg("cursor", "show")
    assert msg("get", "cursor") == "shown\n"
    assert "usage: cursor hide|show" in msg("cursor", "away", ok=False)
    msg("cursor", "hide")
    desktop.wait_for(lambda: msg("get", "cursor") == "shown\n", "the cursor back by itself",
                     timeout=15)
print("Output, window, and region screenshots and missing-tool errors passed")
