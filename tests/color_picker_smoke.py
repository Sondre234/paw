# SPDX-License-Identifier: GPL-3.0-or-later
"""The colour picker (pick_color): while it picks, the cursor is its loupe ("crosshair") and the
pointer is its alone, the window under it hearing nothing; `get color_picker` says the colour
under the crosshair as it moves, read from what the screen shows, also where the loupe itself
was a moment before. A click picks it: subscribers hear `color-picked #rrggbb`, the clipboard
holds "#rrggbb", and `paw msg pick_color` prints it. Escape, another button, its own binding
again and the session locking cancel, with no event and `paw msg pick_color` failing. Return picks too,
and the arrows move the pointer a pixel. On a monitor at a scale of 1.25 the loupe is drawn
in whole pixels at that scale and still reads the pixel under the crosshair, and so on one
turned a quarter. The pointer is a virtual one (pointer_probe), the colours those the probes
draw: wayland_probe's blue body under its dark band, input_probe's green window and its
wallpaper, blue on the left and orange on the right."""
from pathlib import Path
import subprocess
import sys

import harness

(compositor, probe, input_probe, pointer_probe, clipboard_probe,
 lock_probe) = (str(Path(p).resolve()) for p in sys.argv[1:7])

BODY, BAND = "#417bc4", "#23314a"  # wayland_probe's body, and the band along its top
GREEN = "#6a8f3c"  # an input_probe window
BLUE, ORANGE = "#2050a0", "#e0a040"  # input_probe's wallpaper, its left and right halves
# Escape, Return, Down, Up, Super, Shift, C.
ESCAPE, RETURN, DOWN, UP, SUPER, SHIFT, C = 1, 28, 108, 103, 125, 42, 46


def config(scale=1, transform=0, hide_after=0, cursor_size=0):
    return f"""return {{
    xwayland = false,
    animations = {{ enabled = false }},
    idle = {{ display_off = 0 }},
    mouse = {{ shake_to_find = true, hide_after = {hide_after} }},
    appearance = {{ cursor_size = {cursor_size} }},
    layout = {{ tiling = false }},
    outputs = {{ monitors = {{ ["HEADLESS-1"] = {{
        mode = "1280x720", scale = {scale}, transform = {transform} }} }} }},
    windows = {{ border_width = 0, rules = {{
        {{ title = "^paint$", position = {{ x = 100, y = 100 }} }},
        {{ title = "^ears$", position = {{ x = 500, y = 100 }} }},
    }} }},
    bindings = {{
        {{ mods = {{ "Super", "Shift" }}, key = "c", action = "pick_color" }},
    }},
}}"""


with harness.Compositor(compositor, config()) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def picker():
        """`get color_picker`: state, colour, output, x, y, last, loupe width and height, scale,
        sides, pictures, loupes given to the cursor."""
        return desktop.rows("color_picker")[0]

    def state():
        return picker()[0]

    def under():
        return picker()[1]

    def cursor_image():
        return msg("get", "cursor_image").strip()

    desktop.detail = lambda: f"picker: {picker()}"

    wallpaper = desktop.spawn([input_probe, "--background"], log="wallpaper.log")
    wait_for(lambda: "ready" in (desktop.root / "wallpaper.log").read_text(),
             "the wallpaper drawn")
    desktop.open_window(probe, "paint")
    ears = desktop.spawn([input_probe, "ears"], log="ears.log")
    wait_for(lambda: any(w.title == "ears" for w in desktop.windows()), "ears mapped")
    paint = next(w for w in desktop.windows() if w.title == "paint")
    assert paint.box[:2] == (100, 100), paint
    events = desktop.subscribe()
    keys = desktop.keyboard()

    def pointer_for(width, height, old=None):
        """A virtual pointer for a layout of width x height, in place of `old`."""
        if old:
            old.process.terminate()
            desktop.reap(old.process)
        return desktop.virtual_pointer(pointer_probe, width, height)

    pointer = pointer_for(1280, 720)
    pointer("move", "50", "650")
    cursor_size = msg("get", "cursor_size").strip()  # $XCURSOR_SIZE, 24 unless set otherwise

    assert picker()[0] == "idle" and picker()[5] == "-", picker()

    def waiting():
        """`paw msg pick_color`, left waiting for the colour."""
        return subprocess.Popen([compositor, "msg", "pick_color"], env=desktop.env,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    def finished(process):
        out, err = process.communicate(timeout=15)
        return process.returncode, out, err

    # Picking: the cursor is the loupe, and the colour under it follows the pointer: the
    # wallpaper's left half, the probe's body, its band, the green window, the wallpaper's right.
    asked = waiting()
    wait_for(lambda: state() == "picking", "picking")
    assert cursor_image() == "crosshair", cursor_image()
    wait_for(lambda: under() == BLUE, "the wallpaper's left half under the pointer")
    for (x, y), colour in (((200, 250), BODY), ((200, 110), BAND), ((620, 200), GREEN),
                           ((1200, 650), ORANGE), ((200, 250), BODY)):
        pointer("move", str(x), str(y))
        wait_for(lambda: under() == colour, f"{colour} under the pointer at {x},{y}")
    # Where the loupe was drawn a moment ago, the screen under it shows.
    pointer("move", "260", "310")
    wait_for(lambda: under() == BODY, "the body, not the loupe")
    row = picker()
    assert row[2:5] == ["HEADLESS-1", "260", "310"], row
    assert row[8:11] == ["1", "right", "below"], row
    # Near the right and bottom edges the loupe's card turns to the other side.
    pointer("move", "1270", "710")
    wait_for(lambda: picker()[9:11] == ["left", "above"], "the card turned toward the screen")
    # Shaking the pointer to find it grows nothing: the loupe stays the cursor.
    pointer("sleep 150 " + " ".join(f"move {400 + 300 * (i % 2)} 360" for i in range(7)))
    desktop.stays(lambda: msg("get", "cursor_size").strip() == cursor_size and
                  cursor_image() == "crosshair", "the loupe kept through a shake")
    # Resting with mouse.hide_after on hides no loupe; the cursor's size (or theme) changing
    # meanwhile gives the cursor the loupe again, not the theme's crosshair.
    loupes = int(picker()[12])
    desktop.reload(config(hide_after=1, cursor_size=48))
    wait_for(lambda: msg("get", "cursor_size").strip() == "48", "the cursor's size changed")
    assert state() == "picking" and cursor_image() == "crosshair", (picker(), cursor_image())
    assert int(picker()[12]) > loupes, "the loupe was not given to the cursor again"
    desktop.stays(lambda: msg("get", "cursor").strip() == "shown", "the loupe hid at rest", 1.5)
    desktop.reload(config())
    pointer("move", "620", "200")
    wait_for(lambda: under() == GREEN, "green")
    assert "pointer button" not in (desktop.root / "ears.log").read_text()

    # A click picks: the event, the clipboard, the answer; the click reaches no window, and the
    # cursor comes back.
    pointer("click", "left")
    wait_for(lambda: events.last("color-picked ") == f"color-picked {GREEN}", "the event")
    assert finished(asked) == (0, GREEN + "\n", ""), "paw msg printed the colour"
    assert state() == "idle" and picker()[5] == GREEN, picker()
    clipboard = subprocess.run([clipboard_probe, "get", "text/plain;charset=utf-8"],
                               env=desktop.env, capture_output=True, text=True, timeout=15)
    assert clipboard.stdout == GREEN, clipboard
    for kind in ("text/plain", "UTF8_STRING"):
        assert subprocess.run([clipboard_probe, "get", kind], env=desktop.env,
                              capture_output=True, text=True, timeout=15).stdout == GREEN
    desktop.stays(lambda: "pointer button" not in (desktop.root / "ears.log").read_text(),
                  "the click went to the picker alone")
    assert cursor_image() != "crosshair", cursor_image()
    # The pointer is the windows' again.
    pointer("click", "left")
    wait_for(lambda: "pointer button" in (desktop.root / "ears.log").read_text(),
             "the window hears the next click")

    def cancelled(how):
        """Starts picking with `paw msg pick_color`, cancels it by `how`, and checks that
        nothing was picked."""
        events.forget()
        asked = waiting()
        wait_for(lambda: state() == "picking", "picking")
        how()
        wait_for(lambda: state() == "idle", "cancelled")
        assert finished(asked) == (1, "", "paw: error: cancelled\n"), how
        desktop.stays(lambda: not events.lines("color-picked"), "no colour picked")
        assert picker()[5] == GREEN, picker()
        assert cursor_image() != "crosshair"

    cancelled(lambda: keys(ESCAPE))
    cancelled(lambda: pointer("click", "right"))
    cancelled(lambda: pointer("click", "middle"))
    cancelled(lambda: keys(SUPER, SHIFT, C))  # its binding again
    locker = None

    def lock():
        global locker
        locker = desktop.spawn([lock_probe, "hold", str(desktop.root / "locker.log")])

    cancelled(lock)
    wait_for(lambda: "Session locked" in desktop.log.read_text(), "the session locked")
    assert "error: the session is locked" in desktop.run("msg", "pick_color").stderr
    locker.terminate()
    assert desktop.reap(locker) == 0
    wait_for(lambda: "Session unlocked" in desktop.log.read_text(), "the session unlocked")

    # By its binding, then from the keyboard: the pointer a pixel above the band's lower edge,
    # Down a pixel onto the body, Up back onto the band, and Return picks there.
    events.forget()
    pointer("move", "200", "135")
    keys(SUPER, SHIFT, C)
    wait_for(lambda: state() == "picking" and under() == BAND, "the band under the pointer")
    keys(DOWN)
    wait_for(lambda: under() == BODY and picker()[4] == "136", "a pixel down, on the body")
    keys(UP)
    wait_for(lambda: under() == BAND and picker()[4] == "135", "a pixel up, on the band")
    keys(RETURN)
    wait_for(lambda: events.last("color-picked ") == f"color-picked {BAND}", "Return picked")
    assert state() == "idle" and picker()[5] == BAND

    # At a scale of 1.25 the layout is 1024x576, the windows where they were; the loupe is drawn
    # at that scale, in whole pixels, and reads the pixel under the crosshair.
    desktop.reload(config(scale=1.25))
    pointer = pointer_for(1024, 576, pointer)
    pointer("move", "200", "250")
    asked = waiting()
    wait_for(lambda: state() == "picking" and under() == BODY, "the body at 1.25")
    row = picker()
    assert row[3:5] == ["250", "312"], row  # 200 * 1.25 and 250 * 1.25
    width, height = int(row[6]), int(row[7])
    assert row[8] == "1.25" and width % 5 == 0 and height % 5 == 0, row  # 4 logical, 5 pixels
    pointer("move", "130", "95")
    wait_for(lambda: under() == BLUE, "the wallpaper above the window")
    pointer("move", "130", "106")
    wait_for(lambda: under() == BAND, "the band, which starts at 100")
    pointer("click", "left")
    assert finished(asked) == (0, BAND + "\n", "")

    # Turned a quarter, the monitor is 720 wide and 1280 high as it is seen: the wallpaper's
    # halves are left and right as seen, and the window's band along its top.
    desktop.reload(config(transform=1))
    pointer = pointer_for(720, 1280, pointer)
    pointer("move", "20", "1200")
    asked = waiting()
    wait_for(lambda: state() == "picking" and under() == BLUE, "blue on the left")
    for (x, y), colour in (((700, 1200), ORANGE), ((200, 110), BAND), ((200, 300), BODY)):
        pointer("move", str(x), str(y))
        wait_for(lambda: under() == colour, f"{colour} at {x},{y} turned")
    keys(ESCAPE)
    assert finished(asked)[0] == 1
print("The colour picker picks what the screen shows under its crosshair")
