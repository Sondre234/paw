# SPDX-License-Identifier: GPL-3.0-or-later
"""The pointer heatmap: each output counts, in a grid of cells 8 logical pixels square, how long
the pointer rested in each cell and how often it was clicked there. A resting pointer heats its
cell up to a limit (PAW_TEST_HEATMAP_REST_MS here), a pointer passing along warms its path a
little, clicks count apart, nothing counts while the session is locked, and a grid follows its
output's size, keeping its heat where it was. The heatmap action shows the grids over every
output, fading in and out, drawn again twice a second but not under the lock, input going
through to the window under it (and, with grim, hot colours where the pointer rested, the
desktop as it was elsewhere, and the legend); it switches between rest and clicks, and reset
forgets everything. With heatmap.enabled off nothing counts. `paw heatmap` runs the action, draws the heatmap in a terminal of the size
$COLUMNS and $LINES say, in colour or, with NO_COLOR, in shades, and writes it as a PNG, a PAM and
a PPM of the layout's size, the PNG's pixels those of the PAM. The pointer is a virtual one
(pointer_probe)."""
from pathlib import Path
import struct
import subprocess
import sys
import zlib

import harness

compositor, pointer_probe, lock_probe, probe = (str(Path(p).resolve()) for p in sys.argv[1:5])
grim = sys.argv[5] if len(sys.argv) > 5 else ""
SCREEN = (1280, 720)
REST_MS = 1500


# A speed of a fifth makes the overlay's fade a second, long enough to see it on its way.
def config(mode="1280x720", enabled=True):
    return f"""return {{
    xwayland = false,
    idle = {{ display_off = 0 }},
    animations = {{ speed = 0.2 }},
    outputs = {{ monitors = {{ ["HEADLESS-1"] = {{ mode = "{mode}" }} }} }},
    heatmap = {{ enabled = {str(enabled).lower()} }},
}}"""


def heatmap(desktop, *words):
    """{output: (columns, rows, cell, box, rows of counts)} from `get heatmap WORDS`."""
    lines, grids = desktop.rows("heatmap", *words), {}
    while lines:
        name, kind, columns, rows, cell, *box = lines.pop(0)
        columns, rows = int(columns), int(rows)
        counts = [[int(n) for n in line] for line in lines[:rows]]
        assert all(len(line) == columns for line in counts), name
        grids[name] = (columns, rows, int(cell), tuple(int(n) for n in box), counts)
        del lines[:rows]
    return grids


def cell(desktop, x, y, kind="motion", output="HEADLESS-1"):
    """The count of the cell at logical x, y of `output`."""
    counts = heatmap(desktop, output, kind)[output][4]
    return counts[y // 8][x // 8]


def total(desktop, kind="motion"):
    return sum(sum(map(sum, grid[4])) for grid in heatmap(desktop, kind).values())


def png_pixels(data):
    """The width, height and RGBA rows of a PNG `paw heatmap --export` wrote (8-bit RGBA, each
    row filtered with Sub), checking every chunk's CRC on the way."""
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    chunks, at = {}, 8
    while at < len(data):
        length, kind = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + length]
        assert struct.unpack(">I", data[at + 8 + length:at + 12 + length])[0] == \
            zlib.crc32(kind + body), kind
        chunks[kind] = chunks.get(kind, b"") + body
        at += 12 + length
    width, height, depth, colour = struct.unpack(">IIBB", chunks[b"IHDR"][:10])
    assert (depth, colour) == (8, 6) and b"IEND" in chunks
    raw, stride, pixels = zlib.decompress(chunks[b"IDAT"]), width * 4, bytearray()
    for y in range(height):
        row = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        assert raw[y * (stride + 1)] == 1, "a row not filtered with Sub"
        for x in range(4, stride):
            row[x] = (row[x] + row[x - 4]) & 0xff
        pixels += row
    return width, height, bytes(pixels)


with harness.Compositor(compositor, config(),
                        env={"PAW_TEST_HEATMAP_REST_MS": str(REST_MS)}) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def paw_heatmap(*words, env=None, ok=True):
        """`paw heatmap WORDS`: what it prints, or with ok=False what it says as it fails."""
        result = subprocess.run([compositor, "heatmap", *words], capture_output=True, text=True,
                                env={**desktop.env, **(env or {})}, timeout=30)
        assert (result.returncode == 0) == ok, (words, result.stdout, result.stderr)
        return result.stdout if ok else result.stderr

    grids = heatmap(desktop)
    assert list(grids) == ["HEADLESS-1"], grids
    columns, rows, size, box, counts = grids["HEADLESS-1"]
    assert (columns, rows, size, box) == (160, 90, 8, (0, 0, 1280, 720)), grids
    assert not any(map(any, counts)), "nothing counted before the pointer moved"
    assert heatmap(desktop, "clicks")["HEADLESS-1"][:4] == (160, 90, 8, (0, 0, 1280, 720))
    msg("get", "heatmap", "HEADLESS-9", ok=False)
    msg("get", "heatmap", "motion", "clicks", "more", ok=False)

    pointer = desktop.virtual_pointer(pointer_probe, *SCREEN)

    # A resting pointer heats its cell, as far as its rest has come; moving on keeps it.
    pointer("move", "100", "100")
    wait_for(lambda: cell(desktop, 100, 100) >= 400, "the resting pointer's cell warming")
    pointer("move", "1000", "100")
    rested = cell(desktop, 100, 100)
    assert 400 <= rested <= REST_MS, rested
    desktop.stays(lambda: cell(desktop, 100, 100) == rested, "a cell left behind warming")
    # Resting past the limit counts no more than the limit.
    wait_for(lambda: cell(desktop, 1000, 100) >= REST_MS, "a rest reaching the limit")
    desktop.stays(lambda: cell(desktop, 1000, 100) == REST_MS, "a rest counted past the limit",
                  duration=.5)

    # A pointer passing along warms its path a little, much less than where it rested.
    for x in range(20, 1260, 40):
        pointer("move", str(x), "300", "sleep", "5")
    pointer("move", "640", "360")
    path = heatmap(desktop)["HEADLESS-1"][4][300 // 8]
    warm = [count for count in path if count]
    assert len(warm) >= 25 and max(warm) < 200, path
    hottest = max(max(line) for line in heatmap(desktop)["HEADLESS-1"][4])
    assert hottest == REST_MS, hottest

    # Clicks count apart from the rest, where the pointer is.
    pointer("click", "left", "click", "left", "click", "right")
    assert cell(desktop, 640, 360, "clicks") == 3
    assert total(desktop, "clicks") == 3
    pointer("move", "8", "712", "press", "left", "release", "left")
    assert cell(desktop, 8, 712, "clicks") == 1 and total(desktop, "clicks") == 4

    # Nothing counts while the session is locked; what was counted stays.
    locker = desktop.spawn([lock_probe, "hold", str(desktop.root / "locker.log")])
    wait_for(lambda: "Session locked" in desktop.log.read_text(), "the session locked")
    before = total(desktop), total(desktop, "clicks")
    pointer("move", "300", "300", "sleep", "200", "click", "left", "move", "320", "300")
    desktop.stays(lambda: (total(desktop), total(desktop, "clicks")) == before,
                  "counted while locked")
    locker.terminate()
    assert desktop.reap(locker) == 0
    wait_for(lambda: "Session unlocked" in desktop.log.read_text(), "the session unlocked")

    # A smaller mode: the grid shrinks with it, its heat where it was on the screen.
    clicks = total(desktop, "clicks")
    desktop.reload(config("640x360"))
    wait_for(lambda: heatmap(desktop)["HEADLESS-1"][0] == 80, "the grid following the mode")
    columns, rows, size, box, counts = heatmap(desktop)["HEADLESS-1"]
    assert (columns, rows, box) == (80, 45, (0, 0, 640, 360)), (columns, rows, box)
    assert counts[50 // 8][50 // 8] >= rested, "the rested cell moved with the screen"
    assert total(desktop, "clicks") == clicks, "the clicks kept"
    assert cell(desktop, 320, 180, "clicks") == 3

    # A second output has a grid of its own at its place in the layout.
    added = msg("headless_output", "add", "HEADLESS-2", "800x600").strip()
    assert added == "HEADLESS-2", added
    wait_for(lambda: "HEADLESS-2" in heatmap(desktop), "the second output's grid")
    columns, rows, size, box, counts = heatmap(desktop)["HEADLESS-2"]
    assert (columns, rows, box) == (100, 75, (640, 0, 800, 600)), (columns, rows, box)
    assert list(heatmap(desktop, "HEADLESS-2")) == ["HEADLESS-2"]

    # The overlay, over every output, a window under it on the first.
    def overlay():
        """{output: (state, kind, opacity, legend, drawn)} from `get heatmap_overlay`."""
        return {row[0]: tuple(row[1:]) for row in desktop.rows("heatmap_overlay")}

    assert overlay() == {}
    pointer = desktop.virtual_pointer(pointer_probe, 1440, 600)
    pointer("move", "100", "100")
    under = desktop.open_window(probe, "under")
    window = next(w for w in desktop.windows() if w.title == "under")
    assert window.output == "HEADLESS-1", window
    middle = [str(n) for n in (window.x + window.width // 2, window.y + window.height // 2)]
    assert msg("get", "pid_at", *middle).strip() == str(under.pid)
    desktop.wait_for(lambda: msg("get", "animations").split("\t")[0].strip() == "0",
                     "the window opened")
    before = harness.grab(grim, desktop.env, "HEADLESS-1")
    msg("heatmap")
    state = overlay()
    assert set(state) == {"HEADLESS-1", "HEADLESS-2"}, state
    assert state["HEADLESS-1"][:2] == ("shown", "motion"), state
    assert int(state["HEADLESS-1"][2]) < 1000, "the overlay did not fade in"
    wait_for(lambda: all(o[2] == "1000" for o in overlay().values()), "the overlay faded in")
    assert overlay()["HEADLESS-1"][3] == "1.5 s", overlay()
    drawn = int(overlay()["HEADLESS-1"][4])
    wait_for(lambda: int(overlay()["HEADLESS-1"][4]) >= drawn + 2, "the overlay drawn again")
    # Input goes through it.
    assert msg("get", "pid_at", *middle).strip() == str(under.pid), "the overlay took input"
    pointer("move", *middle)
    wait_for(lambda: desktop.rows("seat")[1][:2] == ["pointer", "window"],
             "the pointer on the window under the overlay")
    if grim:
        after = harness.grab(grim, desktop.env, "HEADLESS-1")
        # (500, 50) is where the pointer rested longest, (620, 240) where it never was, and the
        # legend stands in the bottom-right corner, nothing being hot there: its "0" is white.
        hot, cold = after.at(500, 50), after.at(620, 240)
        assert hot[0] > before.at(500, 50)[0] + 60, (before.at(500, 50), hot)
        assert cold == before.at(620, 240), (before.at(620, 240), cold)
        left, top = 640 - 16 - 232 + 12, 360 - 16 - 76 + 50
        assert any(min(after.at(x, y)) > 200 for x in range(left, left + 10)
                   for y in range(top, top + 14)), "no legend in the bottom-right corner"

    # Clicks in place of the rest, then hidden: it fades out and goes.
    msg("heatmap", "clicks")
    wait_for(lambda: overlay()["HEADLESS-1"][1:4:2] == ("clicks", "3 clicks"), "the clicks shown")
    msg("heatmap", "clicks")
    state = overlay()
    assert state["HEADLESS-1"][0] == "hiding" and int(state["HEADLESS-1"][2]) < 1000, state
    wait_for(lambda: overlay() == {}, "the overlay gone")
    # Toggled, by `paw heatmap` too, it shows what it showed last.
    assert paw_heatmap() == ""
    wait_for(lambda: overlay() and overlay()["HEADLESS-1"][:2] == ("shown", "clicks"),
             "the clicks shown again")

    # Under the lock it is not drawn again. Nothing counting then, the images written meanwhile
    # show the same counts: the PNG's pixels are the PAM's, and the PPM is as large.
    locker = desktop.spawn([lock_probe, "hold", str(desktop.root / "locker.log")])
    wait_for(lambda: desktop.log.read_text().count("Session locked") == 2, "locked again")
    drawn = overlay()["HEADLESS-1"][4]
    for name in ("heat.png", "heat.PAM", "heat.ppm"):
        said = paw_heatmap("--export", str(desktop.root / name))
        assert said.startswith("Wrote the heatmap of HEADLESS-1, HEADLESS-2 to "), said
    width, height, pixels = png_pixels((desktop.root / "heat.png").read_bytes())
    assert (width, height) == (1440, 600), (width, height)
    pam = (desktop.root / "heat.PAM").read_bytes()
    header = b"P7\nWIDTH 1440\nHEIGHT 600\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n"
    assert pam == header + pixels, "the PNG's pixels are not the PAM's"
    # Clear below the first output, where there is none, and where the pointer never was.
    assert pixels[(500 * 1440 + 100) * 4 + 3] == 0 and pixels[(240 * 1440 + 620) * 4 + 3] == 0
    assert pixels[(50 * 1440 + 500) * 4 + 3] == 255, "nothing where the pointer rested longest"
    ppm = (desktop.root / "heat.ppm").read_bytes()
    header = b"P6\n1440 600\n255\n"
    assert ppm.startswith(header) and len(ppm) == len(header) + 1440 * 600 * 3
    desktop.stays(lambda: overlay()["HEADLESS-1"][4] == drawn, "drawn under the lock",
                  duration=1.2)
    locker.terminate()
    assert desktop.reap(locker) == 0
    wait_for(lambda: overlay()["HEADLESS-1"][4] != drawn, "drawn again after the lock")

    # In the terminal: the layout's 180 by 75 cells in 72 columns and 18 lines, half blocks
    # of two pixels each, then the legend.
    lines = paw_heatmap("--term", env={"COLUMNS": "72", "LINES": "20"}).splitlines()
    assert len(lines) == 16 and all(line.endswith("\x1b[0m") for line in lines[:15]), lines
    assert "▀" in "".join(lines) and "\x1b[38;2;" in lines[2], lines
    assert lines[15].endswith(" 1.5 s  pointer rest on HEADLESS-1, HEADLESS-2"), lines[15]
    plain = paw_heatmap("--term", env={"COLUMNS": "72", "LINES": "20", "NO_COLOR": "1"})
    lines = plain.splitlines()
    assert "\x1b" not in plain and len(lines) == 16 and "█" in plain, plain
    assert all(len(line) <= 72 for line in lines[:15]), plain
    lines = paw_heatmap("--term", "--clicks", "--output", "HEADLESS-1",
                        env={"COLUMNS": "40", "LINES": "30"}).splitlines()
    # 80 by 45 cells in 40 columns: 40 by 23 pixels (one more row to fill the last line).
    assert len(lines) == 13 and lines[12].endswith(" 3 clicks  clicks on HEADLESS-1"), lines
    assert "usage" in paw_heatmap("--clicks", ok=False)
    assert "usage" in paw_heatmap("hot", ok=False)
    assert ".png" in paw_heatmap("--export", str(desktop.root / "heat.gif"), ok=False)
    assert "no such output" in paw_heatmap("--term", "--output", "HEADLESS-9", ok=False)
    assert "PAW_SOCKET" in paw_heatmap("--term", env={"PAW_SOCKET": ""}, ok=False)

    # Reset forgets everything, on every output, and the overlay shows it; the pointer's rest
    # starts again.
    msg("heatmap", "reset")
    assert total(desktop) < 200 and total(desktop, "clicks") == 0
    assert overlay()["HEADLESS-1"][3] == "0 clicks", overlay()
    paw_heatmap("motion")
    assert overlay()["HEADLESS-1"][1] == "motion"
    paw_heatmap("motion")
    wait_for(lambda: overlay() == {}, "the overlay gone again")

    # With heatmap.enabled off nothing counts, and on again it counts as before.
    desktop.reload(config("640x360", enabled=False))
    before = total(desktop), total(desktop, "clicks")
    pointer("move", "200", "200", "sleep", "100", "click", "left", "move", "210", "200")
    desktop.stays(lambda: (total(desktop), total(desktop, "clicks")) == before,
                  "counted with heatmap.enabled off")
    desktop.reload(config("640x360"))
    pointer("move", "200", "200", "click", "left")
    assert total(desktop, "clicks") == before[1] + 1
    assert "heatmap" in msg("heatmap", "hot", ok=False)
