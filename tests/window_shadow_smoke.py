# SPDX-License-Identifier: GPL-3.0-or-later
"""windows.shadow: a soft shadow under each window that draws none of its own, darker under the
focused one, around the window and further below it than above; none under a fullscreen or
maximized window, nor under one with a shadow of its own until tiling clips that away. The
shadow takes no input (a click on it reaches the window below) and a reload turns it off. With
grim, the pixels under a window are darker. Read through `get frames`."""
from pathlib import Path
import sys

import harness

compositor, probe, pointer_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])
grim = sys.argv[4] if len(sys.argv) > 4 else ""

SCREEN = (1280, 720)


def config(shadow):
    return """return {
    xwayland = false,
    appearance = { background = "#dfe4ec" },
    layout = { tiling = false },
    mouse = { focus_follows = false },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    windows = {
        round = "always",
        magnet = { enabled = false },
        shadow = { enabled = %s, blur = 30, offset = 10, color = "#00000059",
                   inactive_color = "#00000033" },
        rules = {
            { title = "^below$", position = { 100, 100 } },
            { title = "^above$", position = { 100, 350 } },
            { title = "^own$", position = { 800, 100 } },
        },
    },
}""" % ("true" if shadow else "false")


with harness.Compositor(compositor, config(True)) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def frames():
        rows = desktop.rows("frames")
        return {r[1]: {"focused": r[2] == "1", "shadow": r[6] == "1", "alpha": int(r[7]),
                       "box": tuple(map(int, r[8:12]))} for r in rows}

    def windows():
        return {w.title: w for w in desktop.windows()}

    desktop.detail = lambda: msg("get", "frames")

    def screen():
        """A screenshot once the windows have stopped moving."""
        wait_for(lambda: msg("get", "animations").split("\t")[0].strip() == "0",
                 "animations finished")
        return harness.grab(grim, desktop.env)
    # Two windows the compositor decorates, the second just below the first so that its shadow
    # falls on it, and one that draws a shadow of its own around its geometry; "above" opens
    # last and has focus.
    for title, env in (("own", {"PAW_PROBE_SHADOW": "12"}),
                       ("below", {"PAW_PROBE_SSD": "1"}),
                       ("above", {"PAW_PROBE_SSD": "1"})):
        desktop.spawn([probe, "--window-only"], env={"PAW_PROBE_TITLE": title, **env})
        wait_for(lambda: title in frames(), f"{title} mapped")
    wait_for(lambda: frames()["above"]["focused"], "the last window focused")
    f = frames()
    assert f["above"]["shadow"] and f["below"]["shadow"] and not f["own"]["shadow"], f
    # Darker under the focused window; around it, further below than above.
    assert f["above"]["alpha"] == 349 and f["below"]["alpha"] == 200, f
    x, y, w, h = f["above"]["box"]
    assert x < -30 and y < -20 and x + w == 320 - x and y + h - 240 == -y + 20, f["above"]

    # The shadow takes no input: a click where "above"'s shadow lies over "below" focuses it.
    pointer = desktop.virtual_pointer(pointer_probe, *SCREEN)
    pointer("move", "300", "325", "click", "left")
    wait_for(lambda: frames()["below"]["focused"], "a click on the shadow reached the window")
    wait_for(lambda: frames()["below"]["alpha"] == 349 and frames()["above"]["alpha"] == 200,
             "the darker shadow followed focus")
    pointer("move", "300", "500", "click", "left")
    wait_for(lambda: frames()["above"]["focused"], "focus back on the upper window")

    if grim:
        # Under the window's bottom edge the desktop is darker than far from any window.
        shot = screen()
        under, clear = shot.at(260, 350 + 240 + 6), shot.at(640, 700)
        assert sum(under) < sum(clear) - 60, (under, clear)

    # None under a fullscreen or maximized window.
    msg("fullscreen")
    wait_for(lambda: not frames()["above"]["shadow"], "no shadow under a fullscreen window")
    msg("fullscreen")
    wait_for(lambda: frames()["above"]["shadow"], "shadow back out of fullscreen")
    msg("maximize")
    wait_for(lambda: not frames()["above"]["shadow"], "no shadow under a maximized window")
    msg("restore")
    wait_for(lambda: frames()["above"]["shadow"], "shadow back restored")

    # The overview draws its own cards; the windows keep their shadows through it.
    msg("toggle_overview")
    wait_for(lambda: msg("get", "overview").startswith("open 1000"), "overview open")
    msg("toggle_overview")
    wait_for(lambda: msg("get", "overview").startswith("closed"), "overview closed")
    assert frames()["above"]["shadow"], frames()

    # Tiling rounds the window with its own shadow, which cuts that away: it gets one then,
    # where wlroots can round windows.
    msg("toggle_tiling")
    wait_for(lambda: all(w.tiled for w in windows().values()), "all tiled")
    rounded = desktop.rows("frames")[0][5] != "0"
    wait_for(lambda: frames()["own"]["shadow"] == rounded, "a clipped window gets a shadow")
    msg("toggle_tiling")
    wait_for(lambda: not frames()["own"]["shadow"], "its own shadow back off tiling")

    desktop.reload(config(False))
    wait_for(lambda: not any(v["shadow"] for v in frames().values()), "reload turned shadows off")
    if grim:
        shot = screen()
        assert shot.at(260, 350 + 240 + 6) == shot.at(640, 700), "a shadow left behind"
print("Shadows sit under windows without their own, follow focus and take no input"
      + ("" if grim else " (pixels not checked: grim missing)"))
