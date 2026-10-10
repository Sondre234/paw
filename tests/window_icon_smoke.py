# SPDX-License-Identifier: GPL-3.0-or-later
"""Windows' own icons, as paw-window-control-v1 sends them from version 5: a Wayland window's
from xdg-toplevel-icon-v1 (a name, pixels, or both) and an X11 window's from _NET_WM_ICON, of
their sizes the largest within 256 by 256 or else the smallest larger one, premultiplied ARGB in a
sealed file. A window's objects hear its icon with its first state when it has one, and again as
it changes or is dropped, but not when it is set to the same again; a client of version 4 hears
none of it. `get window_icons` lists them. Without XWayland the X11 part is skipped."""
from pathlib import Path
import queue
import subprocess
import sys
import threading

import harness

compositor, probe, window_probe = (str(Path(p).resolve()) for p in sys.argv[1:4])
x11_probe = str(Path(sys.argv[4]).resolve()) if len(sys.argv) > 4 else ""

CONFIG = f"""return {{
    xwayland = {"true" if x11_probe else "false"},
    layout = {{ tiling = false }},
}}"""


def pixels(spec, count):
    """What window_probe prints for an icon's pixels, `count` of them in one colour: the first
    and the sum of them all."""
    return f"{spec:08x} {count * spec % 2**32:08x}"


with harness.Compositor(compositor, CONFIG) as desktop:

    def icons():
        """app_id -> (icon name, size) from `get window_icons`."""
        return {row[1]: (row[2], row[3]) for row in desktop.rows("window_icons")}

    def titles():
        return [w.title for w in desktop.windows()]

    desktop.detail = lambda: f"icons: {icons()}"

    def icon(title, version=None):
        """The icon window_probe prints for the window titled `title`."""
        env = dict(desktop.env)
        if version:
            env["PAW_WINDOW_PROBE_VERSION"] = str(version)
        return subprocess.run([window_probe, title, "icon"], env=env, check=True,
                              capture_output=True, text=True, timeout=30).stdout.strip()

    def launch(title, app_id, spec=None, **options):
        env = {"PAW_PROBE_TITLE": title, "PAW_PROBE_APP_ID": app_id}
        if spec:
            env["PAW_PROBE_ICON"] = spec
        client = desktop.spawn([probe, "--commands" if options else "--window-only"], env=env,
                               **options)
        desktop.wait_for(lambda: title in titles(), f"{title} open")
        return client

    # A name alone, which the shell looks up in its icon theme.
    launch("Named", "named", "name:utilities-terminal")
    assert icons()["named"] == ("utilities-terminal", "-"), icons()
    assert icon("Named") == "utilities-terminal - - -", icon("Named")

    # Pixels: of 16, 64 and 300 pixels square, the 64, the largest within 256, and its
    # premultiplied colour as the client gave it; the padding at the end of the client's rows is
    # left out. A name goes with them.
    launch("Pixels", "pixels", "16:ff0000ff 300:ffffffff name:app 64:80402010")
    assert icons()["pixels"] == ("app", "64x64"), icons()
    assert icon("Pixels") == f"app 64x64 {pixels(0x80402010, 64 * 64)}", icon("Pixels")
    # A client of version 4 hears no icon.
    assert icon("Pixels", version=4) == "none"

    # A window with no icon sends none; then one that changes its icon, watched by a client of
    # version 5 and one of version 4.
    changing = launch("Changing", "changing", stdin=subprocess.PIPE, text=True)
    assert icons()["changing"] == ("-", "-")
    assert icon("Changing") == "none"
    heard = {}
    watchers = []
    for version in (5, 4):
        watcher = desktop.spawn([window_probe, "Changing", "icon", "watch"],
                                env={"PAW_WINDOW_PROBE_VERSION": str(version)},
                                stdout=subprocess.PIPE, text=True)
        heard[version] = queue.Queue()
        threading.Thread(target=lambda w=watcher, q=heard[version]:
                         [q.put(line.strip()) for line in w.stdout], daemon=True).start()
        assert heard[version].get(timeout=15) == "none"
        watchers.append(watcher)

    def tell(command):
        changing.stdin.write(command + "\n")
        changing.stdin.flush()

    def hear():
        """The next icon the client of version 5 printed."""
        return heard[5].get(timeout=15)

    tell("icon name:first 48:ff00ff00")
    assert hear() == f"first 48x48 {pixels(0xff00ff00, 48 * 48)}"
    assert icons()["changing"] == ("first", "48x48")
    # Above 256 only, the smallest of them.
    tell("icon 512:ff00ff00 300:ff102030")
    assert hear() == f"- 300x300 {pixels(0xff102030, 300 * 300)}"
    # The same icon again is no change. The title changing after it shows that the compositor
    # has had it, and would have sent it, apart from the next, so the next icon heard is that.
    tell("icon 512:ff00ff00 300:ff102030")
    tell("title Changing again")
    desktop.wait_for(lambda: "Changing again" in titles(), "the new title")
    # xrgb8888 is opaque, whatever its alpha byte says.
    tell("icon 32:00336699:x")
    assert hear() == f"- 32x32 {pixels(0xff336699, 32 * 32)}"
    # An icon with neither a name nor pixels drops it, as no icon does.
    tell("icon empty")
    assert hear() == "- - - -"
    assert icons()["changing"] == ("-", "-")
    tell("icon none")  # no icon already: nothing to hear
    tell("icon name:last")
    assert hear() == "last - - -"
    tell("icon none")
    assert hear() == "- - - -"
    assert icons()["changing"] == ("-", "-")

    # Closing the window ends both watches; the client of version 4 heard nothing past its first
    # state, and the other nothing more.
    subprocess.run([probe, "--close", "changing"], env=desktop.env, check=True, timeout=30,
                   stdout=subprocess.DEVNULL)
    for watcher in watchers:
        assert desktop.reap(watcher) == 0
    assert desktop.reap(changing) == 0
    assert heard[4].empty(), heard[4].get()
    assert heard[5].empty(), heard[5].get()

    if not x11_probe or "DISPLAY" not in desktop.env:
        print("XWayland is not available: the X11 part is skipped")
    else:
        # _NET_WM_ICON: of 16 and 48 pixels square, the 48, its colour premultiplied, as X11
        # gives it straight.
        x11 = desktop.spawn([x11_probe, "commands"],
                            env={"PAW_PROBE_TITLE": "X11 icon",
                                 "PAW_PROBE_ICON": "16:ff0000ff 48x48:80ff0000"},
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        assert x11.stdout.readline().strip() == "X11 window mapped and focused"
        assert x11.stdout.readline().strip() == "waiting for commands"
        X11 = "paw-x11-probe"
        desktop.wait_for(lambda: icons().get(X11) == ("-", "48x48"), "the X11 window's icon")
        assert icon("X11 icon") == f"- 48x48 {pixels(0x80800000, 48 * 48)}", icon("X11 icon")

        def tell_x11(command):
            x11.stdin.write(command + "\n")
            x11.stdin.flush()

        # Changed, to an image that is not square; a quarter opaque white is a quarter grey.
        tell_x11("icon 24x20:40ffffff")
        desktop.wait_for(lambda: icons()[X11] == ("-", "24x20"), "the X11 icon changed")
        assert icon("X11 icon") == f"- 24x20 {pixels(0x40404040, 24 * 20)}", icon("X11 icon")
        # And deleted.
        tell_x11("unicon")
        desktop.wait_for(lambda: icons()[X11] == ("-", "-"), "the X11 icon gone")
        assert icon("X11 icon") == "none"
        subprocess.run([probe, "--close", X11], env=desktop.env, check=True, timeout=30,
                       stdout=subprocess.DEVNULL)
        x11.stdin.close()
        assert desktop.reap(x11, timeout=10) == 0
print("Windows' own icons reach the window control and get window_icons")
