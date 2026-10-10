# SPDX-License-Identifier: GPL-3.0-or-later
"""The border of an urgent window pulses for a few seconds, then holds windows.urgent_color, with
a border_width or (inside the window's edge) without one, and gives way to the focus color; without
animations it holds the color at once."""
from pathlib import Path
import subprocess
import sys
import time

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])
grim = sys.argv[3] if len(sys.argv) > 3 else ""
if not grim:
    print("grim is missing: urgent borders not checked")
    sys.exit(0)


def settings(border, animations="true"):
    return f"""return {{
    xwayland = false,
    layout = {{ tiling = false }},
    animations = {{ enabled = {animations} }},
    windows = {{ border_width = {border}, urgent_color = "#ff9e64", border_color = "#7da8ff",
                 border_inactive_color = "#404a5c" }},
}}"""


URGENT, FOCUSED, INACTIVE = (255, 158, 100), (125, 168, 255), (64, 74, 92)


def near(pixel, want, tolerance=3):
    return all(abs(a - b) <= tolerance for a, b in zip(pixel, want))


with harness.Compositor(compositor, settings(3)) as desktop:
    msg = desktop.msg

    def windows():
        return {w.app_id: w for w in desktop.windows()}

    def edge(app_id, inside):
        """The pixel on the middle of the window's left edge: outside it, or just inside."""
        x, y, _, h = windows()[app_id].box
        return harness.grab(grim, desktop.env).at(x + (1 if inside else -2), y + h // 2)

    def start(app_id):
        client = desktop.spawn([probe, "--commands"], env={"PAW_PROBE_APP_ID": app_id},
                               stdin=subprocess.PIPE, text=True)
        desktop.wait_for(lambda: app_id in windows() and windows()[app_id].focused, f"{app_id} focused")
        return client

    def ask(client):
        client.stdin.write("activate\n")
        client.stdin.flush()

    def urgent_count():
        return len(msg("get", "urgent").splitlines())

    a = start("urgent-a")
    b = start("urgent-b")
    # Move the windows apart so neither's edge is under the other: b right, a left.
    msg("move_right")
    subprocess.run([probe, "--activate", "urgent-a"], env=desktop.env, check=True, timeout=5,
                   stdout=subprocess.DEVNULL)
    desktop.wait_for(lambda: windows()["urgent-a"].focused, "a focused")
    msg("move_left")
    subprocess.run([probe, "--activate", "urgent-b"], env=desktop.env, check=True, timeout=5,
                   stdout=subprocess.DEVNULL)
    desktop.wait_for(lambda: windows()["urgent-b"].focused, "b focused")
    desktop.wait_for(lambda: windows()["urgent-a"].x + 400 < windows()["urgent-b"].x and
                     near(edge("urgent-b", False), FOCUSED) and
                     near(edge("urgent-a", False), INACTIVE),
                     "a left and inactive, b right and focused",
                     detail=lambda: f"{windows()}, {edge('urgent-a', False)}, "
                     f"{edge('urgent-b', False)}")

    ask(a)
    desktop.wait_for(lambda: urgent_count() == 1, "a urgent")
    seen = []
    start_time = time.monotonic()
    while time.monotonic() - start_time < 1.5:
        seen.append(edge("urgent-a", False))
        time.sleep(.03)
    brightness = [sum(p) for p in seen]
    assert max(brightness) - min(brightness) > 60, f"the border does not pulse: {seen}"
    assert not any(near(p, INACTIVE) for p in seen[3:]), seen

    def held():
        """Whether the edge shows the urgent color for a while (3 looks over 0.3 s or more):
        longer than the pulse passes through it."""
        start, looks = time.monotonic(), 0
        while looks < 3 or time.monotonic() - start < .3:
            if not near(edge("urgent-a", False), URGENT):
                return False
            looks += 1
        return True

    desktop.wait_for(held, "the urgent color held once the pulse is over",
                     detail=lambda: edge("urgent-a", False))
    # Focus ends it: the focus color, and the inactive color once focus moves on.
    msg("focus_urgent")
    desktop.wait_for(lambda: near(edge("urgent-a", False), FOCUSED) and
                     near(edge("urgent-b", False), INACTIVE),
                     "a focused, b inactive",
                     detail=lambda: f"{edge('urgent-a', False)}, {edge('urgent-b', False)}")

    # Without a border_width the urgent frame sits inside the window's edge, and nothing is
    # drawn once it is over. Without animations it holds the urgent color at once, no pulse.
    desktop.reload(settings(0, "false"))
    ask(b)
    desktop.wait_for(lambda: urgent_count() == 1, "b urgent")
    desktop.wait_for(lambda: near(edge("urgent-b", True), URGENT), "the inset frame",
                     detail=lambda: edge("urgent-b", True))
    desktop.stays(lambda: near(edge("urgent-b", True), URGENT), "the inset frame pulses",
                  duration=.5, detail=lambda: edge("urgent-b", True))
    before = windows()["urgent-b"].box
    msg("focus_urgent")
    desktop.wait_for(lambda: not near(edge("urgent-b", True), URGENT),
                     "the inset frame gone", detail=lambda: edge("urgent-b", True))
    assert windows()["urgent-b"].box == before, "the frame moved the window"
print("Urgent borders pulse, hold the urgent color, sit inside without a border, "
      "and give way to focus")
