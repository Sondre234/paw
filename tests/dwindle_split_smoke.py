# SPDX-License-Identifier: GPL-3.0-or-later
"""Hyprland's dwindle controls: split_toggle turns the focused tile's split from side by side to
stacked and keeps it so through a reload to a monitor of another shape, split_swap trades the
split's halves at their sizes, and pseudo_toggle keeps a tile at its floating size in the middle
of its tile, resized about its middle, for a group's other tabs too; master takes none of
them."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])


def config(mode="1280x720"):
    return f"""return {{
    xwayland = false,
    layout = {{ tiling = true, gap = 0 }},
    windows = {{ rules = {{ {{ title = "^float$", floating = true, size = {{ 300, 200 }} }} }} }},
    outputs = {{ monitors = {{ ["HEADLESS-1"] = {{ mode = "{mode}" }} }} }},
}}"""


with harness.Compositor(compositor, config()) as desktop:
    msg = desktop.msg

    def boxes():
        return {w.title: w.box for w in desktop.windows()}

    desktop.detail = lambda: f"windows: {boxes()}"

    def expect(message, wanted):
        desktop.wait_for(lambda: all(boxes().get(t) == b for t, b in wanted.items()), message)

    def centred(box, tile):
        """Whether `box` has the middle of `tile`, to the pixel an odd difference leaves."""
        return (abs(2 * box[0] + box[2] - 2 * tile[0] - tile[2]) <= 1 and
                abs(2 * box[1] + box[3] - 2 * tile[1] - tile[3]) <= 1)

    def tiled(title):
        return next(w.tiled for w in desktop.windows() if w.title == title)

    desktop.open_window(probe, "one", focused=True)
    desktop.open_window(probe, "two", focused=True)
    # The second opens on the side of the first nearer the pointer.
    desktop.wait_for(lambda: sorted(b[:2] for b in boxes().values()) == [(0, 0), (640, 0)],
                     "side by side")
    left, right = sorted(boxes(), key=lambda title: boxes()[title][0])
    expect("halves", {left: (0, 0, 640, 720), right: (640, 0, 640, 720)})

    # togglesplit: stacked, and kept so where a wider monitor would split it side by side,
    # through reloads.
    msg("split_toggle")
    expect("stacked", {left: (0, 0, 1280, 360), right: (0, 360, 1280, 360)})
    desktop.reload(config("1600x900"))
    expect("stacked on a wider monitor", {left: (0, 0, 1600, 450), right: (0, 450, 1600, 450)})
    desktop.reload(config())
    expect("stacked again", {left: (0, 0, 1280, 360), right: (0, 360, 1280, 360)})
    msg("split_toggle")
    expect("side by side again", {left: (0, 0, 640, 720), right: (640, 0, 640, 720)})

    # swapsplit: the halves trade places, each keeping its width. The focused window, two, grows
    # to 768 pixels first.
    msg("resize_right" if left == "two" else "resize_left", "128")
    width = 768 if left == "two" else 512
    expect("resized", {left: (0, 0, width, 720), right: (width, 0, 1280 - width, 720)})
    msg("split_swap")
    expect("swapped", {right: (0, 0, 1280 - width, 720), left: (1280 - width, 0, width, 720)})
    msg("split_swap")
    expect("swapped back", {left: (0, 0, width, 720), right: (width, 0, 1280 - width, 720)})

    # pseudo: a window that opened tiled has no floating size; it takes half the screen each
    # way, in the middle of its tile.
    tile = boxes()["two"]
    msg("pseudo_toggle")
    expect("pseudotiled", {"two": (tile[0] + 64, 180, 640, 360)})
    msg("pseudo_toggle")
    expect("filling its tile", {"two": tile})

    # A window that floated keeps its floating size, and resizing it changes that size about
    # its middle, which it then floats at too.
    desktop.open_window(probe, "float", focused=True)
    desktop.wait_for(lambda: boxes()["float"][2:] == (300, 200), "float at its size")
    msg("toggle_floating")
    desktop.wait_for(lambda: tiled("float") and boxes()["float"][2:] != (300, 200) and
                     harness.disjoint(list(boxes().values())), "float tiled")
    tile = boxes()["float"]
    msg("pseudo_toggle")
    desktop.wait_for(lambda: boxes()["float"][2:] == (300, 200) and
                     centred(boxes()["float"], tile), "float pseudotiled")
    msg("resize_right", "60")
    msg("resize_up", "50")
    desktop.wait_for(lambda: boxes()["float"][2:] == (360, 150) and
                     centred(boxes()["float"], tile), "resized about its middle")
    msg("toggle_floating")
    desktop.wait_for(lambda: boxes()["float"][2:] == (360, 150) and not tiled("float") and
                     boxes()["one"][2] + boxes()["two"][2] == 1280, "floating at that size")
    msg("toggle_floating")
    desktop.wait_for(lambda: tiled("float") and boxes()["float"][2:] == (360, 150) and
                     harness.disjoint(list(boxes().values())), "pseudotiled again")
    dwindle = boxes()

    # Dwindle only: master fills each tile and takes none of the actions; back in dwindle the
    # tiles are as they were.
    msg("layout_master")
    desktop.wait_for(lambda: boxes()["float"][2:] != (360, 150), "master fills the tile")
    master = boxes()
    for action in ("split_toggle", "split_swap", "pseudo_toggle"):
        msg(action)
    desktop.stays(lambda: boxes() == master, "master took a dwindle action")
    msg("layout_dwindle")
    desktop.wait_for(lambda: boxes() == dwindle, "dwindle as it was")

    # A group keeps its slot pseudotiled: the tab opened into it shows at the same size there.
    msg("group_toggle")
    desktop.open_window(probe, "tab", focused=True)
    desktop.wait_for(lambda: boxes()["tab"] == dwindle["float"], "the tab pseudotiled")
print("Dwindle split controls passed")
