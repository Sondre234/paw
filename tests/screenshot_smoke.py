# SPDX-License-Identifier: GPL-3.0-or-later
"""Take screenshots through the control socket with stand-ins for grim, slurp, and wl-copy."""
from pathlib import Path
import re
import sys

import harness

compositor, probe, example = (str(Path(p).resolve()) for p in sys.argv[1:4])

# Each stand-in records its arguments; grim writes the file it is given. Only shell builtins
# are used, because the compositor's PATH holds nothing but these.
TOOLS = {
    "grim": 'echo "grim $*" >> "$TOOL_LOG"; for last; do :; done; echo "fake png" > "$last"',
    # Waits, like a user choosing a region, until the test creates $TOOL_LOG.go.
    "slurp": 'echo "slurp" >> "$TOOL_LOG"; while [ ! -e "$TOOL_LOG.go" ]; do :; done; '
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
    desktop.env.update(PATH=str(tools), TOOL_LOG=str(tool_log))

    def calls():
        return tool_log.read_text().splitlines()

    def saved():
        return set(shots.glob("Screenshot_*.png"))

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

    # A region comes from slurp; without wl-copy the file is still saved.
    (tools / "wl-copy").unlink()
    tool_log.write_text("")
    before = saved()
    desktop.wait_for(lambda: screenshot_accepted(), "region screenshot start")
    desktop.wait_for(lambda: calls() == ["slurp"], "slurp", detail=calls)
    # Pressing Print again while choosing a region takes no second screenshot.
    assert "already being taken" in msg("screenshot", ok=False)
    assert "already being taken" in msg("screenshot", "output", ok=False)
    Path(str(tool_log) + ".go").touch()
    desktop.wait_for(lambda: len(calls()) == 3, "region screenshot", detail=calls)
    (region,) = saved() - before
    assert calls()[:2] == ["slurp", f"grim -g 10,20 30x40 {region}"], calls()
    assert calls()[2].startswith("notify-send"), calls()
    assert "wl-copy is not installed" in desktop.log.read_text()

    # Missing tools are reported to the caller and nothing runs.
    (tools / "slurp").unlink()
    assert "grim and slurp" in msg("screenshot", "region", ok=False)
    (tools / "grim").unlink()
    assert "need grim" in msg("screenshot", "output", ok=False)
    assert len(saved()) == 5
print("Output, window, and region screenshots and missing-tool errors passed")
