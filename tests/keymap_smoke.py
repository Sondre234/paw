# SPDX-License-Identifier: GPL-3.0-or-later
"""Keymaps reach keyboards and, through the seat, applications: a keyboard gets the keymap of
keyboard.layout or keyboard.file, a reload swaps it while keys stay held and locks stay on, a
virtual keyboard keeps its own, and a keymap file that does not compile is a configuration
error, at startup or on a reload, which leaves the default configuration's keymap in place of
none. `get keyboard` says where the keymap came from and what each keyboard is doing."""
from pathlib import Path
import re
import subprocess
import sys

import harness

compositor, probe, pointer_probe, default_config = (str(Path(p).resolve()) for p in sys.argv[1:5])

# Two layouts, the first renamed so it is plain where the keymap came from.
KEYMAP = """xkb_keymap {
  xkb_keycodes { include "evdev" };
  xkb_types { include "complete" };
  xkb_compat { include "complete" };
  xkb_symbols { include "pc+us+no:2" name[Group1] = "Testish"; };
};
"""
# The same with a syntax error on line 5.
BROKEN = KEYMAP.replace('name[Group1] = "Testish";', "oops")
SHIFT, CAPS_LOCK = 42, 58  # evdev key codes
SH_SHIFT, SH_CAPS, SH_ALT = 1, 2, 8  # modifier bits in `get keyboard`


def config(keyboard):
    """A configuration whose keyboard table, on line 3, holds `keyboard`."""
    return "return {\n    xwayland = false,\n    keyboard = { %s },\n}\n" % keyboard


with harness.Compositor(compositor, env={"PAW_DEFAULT_CONFIG": default_config},
                        start=False) as desktop:
    root, init, msg = desktop.root, desktop.config, desktop.msg
    (root / "keymap.xkb").write_text(KEYMAP)
    (root / "broken.xkb").write_text(BROKEN)

    def layouts(expected=True):
        """The names of the layouts in the keymap a new application gets. (The probe's output
        says so rather than its status, which leak checkers change.)"""
        result = subprocess.run([probe, "--keymap"], env=desktop.env, capture_output=True,
                                text=True, timeout=30)
        arrived = "no keymap arrived" not in result.stderr and "xkb_symbols" in result.stdout
        assert arrived == expected, (result.stdout[-2000:], result.stderr[-2000:])
        symbols = result.stdout[result.stdout.find("xkb_symbols"):]
        return re.findall(r'^\s*name\[\w*?(\d+)\]\s*=\s*"([^"]*)";', symbols, re.M)

    def keyboard():
        """`get keyboard`: the source, the layouts as (short, name, active), and the keyboards
        by name as (layout, layouts, virtual, held, locked)."""
        source, layouts, keyboards = None, [], {}
        for fields in desktop.rows("keyboard"):
            if fields[0] == "source":
                source = fields[1:]
            elif fields[0] == "layout":
                assert int(fields[1]) == len(layouts) + 1, fields
                layouts.append((fields[3], fields[4], fields[2] == "1"))
            elif fields[0] == "keyboard":
                keyboards[fields[6]] = tuple(int(field) for field in fields[1:6])
        return source, layouts, keyboards

    def key(name, code, state):
        msg("headless_keyboard", "key", name, str(code), state)

    # --check-config refuses a broken keymap file, saying where in both files.
    init.write_text(config('file = "broken.xkb"'))
    check = subprocess.run([compositor, "--check-config", "--config", str(init)], env=desktop.env,
                           capture_output=True, text=True, timeout=30)
    assert check.returncode != 0, check.stdout
    assert f"init.lua:3: keyboard.file: {root / 'broken.xkb'}:5:" in check.stderr, check.stderr

    desktop.start(config('layout = "us,no"'))
    # Until a keyboard is plugged in, applications get no keymap from the seat.
    layouts(expected=False)
    assert keyboard() == (["rules"], [("us", "English (US)", True), ("no", "Norwegian", False)],
                          {}), keyboard()
    msg("headless_keyboard", "add", "one")
    assert layouts() == [("1", "English (US)"), ("2", "Norwegian")], layouts()
    assert keyboard()[2] == {"one": (1, 2, 0, 0, 0)}, keyboard()
    # The control socket refuses what it cannot do.
    for words in (("add", "one"), ("remove", "nobody"), ("key", "one", "30", "hold"),
                  ("key", "one", "99999", "press"), ("jump",)):
        msg("headless_keyboard", *words, ok=False)

    # A virtual keyboard (from the pointer probe) brings its own keymap, with one layout.
    tell = desktop.virtual_pointer(pointer_probe, 1280, 720)
    tell("key", "alt", "down")
    desktop.wait_for(lambda: len(keyboard()[2]) == 2, "a virtual keyboard")
    virtual = next(name for name, state in keyboard()[2].items() if state[2])
    assert keyboard()[2][virtual] == (1, 1, 1, SH_ALT, 0), keyboard()

    # keyboard.file, relative to the configuration, replaces the names on a reload, while a
    # key is held and Caps Lock is on; the virtual keyboard keeps its keymap and its Alt.
    key("one", CAPS_LOCK, "press")
    key("one", CAPS_LOCK, "release")
    key("one", SHIFT, "press")
    assert keyboard()[2]["one"] == (1, 2, 0, SH_SHIFT, SH_CAPS), keyboard()
    desktop.reload(config('layout = "de", file = "keymap.xkb"'))
    assert layouts() == [("1", "Testish"), ("2", "Norwegian")], layouts()
    source, names, keyboards = keyboard()
    assert source == ["file", str(root / "keymap.xkb")], source
    assert names == [("te", "Testish", True), ("no", "Norwegian", False)], names
    assert keyboards == {"one": (1, 2, 0, SH_SHIFT, SH_CAPS), virtual: (1, 1, 1, SH_ALT, 0)}, \
        keyboards
    key("one", SHIFT, "release")
    key("one", CAPS_LOCK, "press")
    key("one", CAPS_LOCK, "release")
    assert keyboard()[2]["one"] == (1, 2, 0, 0, 0), keyboard()
    tell("key", "alt", "up")
    tell.process.stdin.close()
    desktop.reap(tell.process)
    desktop.wait_for(lambda: len(keyboard()[2]) == 1, "the virtual keyboard gone")

    # Back to names, and a keyboard plugged in later gets the same.
    desktop.reload(config('layout = "no"'))
    assert layouts() == [("1", "Norwegian")], layouts()
    msg("headless_keyboard", "add", "two")
    msg("headless_keyboard", "remove", "one")
    assert layouts() == [("1", "Norwegian")], layouts()
    assert keyboard() == (["rules"], [("no", "Norwegian", True)], {"two": (1, 1, 0, 0, 0)})

    # A broken file on a reload: the default configuration, and a keyboard that still works.
    desktop.reload(config('layout = "no", file = "broken.xkb"'))
    desktop.wait_for(lambda: "using the default configuration" in desktop.log.read_text(),
                     "the error reported")
    assert f"init.lua:3: keyboard.file: {root / 'broken.xkb'}:5:" in desktop.log.read_text()
    assert layouts() == [("1", "English (US)")], layouts()
    assert keyboard()[:2] == (["rules"], [("us", "English (US)", True)]), keyboard()
    key("two", SHIFT, "press")
    assert keyboard()[2]["two"][3] == SH_SHIFT, keyboard()
    key("two", SHIFT, "release")
    assert keyboard()[2]["two"][3] == 0, keyboard()
    desktop.stop()

    # The same at startup.
    desktop.start(config('file = "broken.xkb"'))
    assert f"init.lua:3: keyboard.file: {root / 'broken.xkb'}:5:" in desktop.log.read_text()
    msg("headless_keyboard", "add", "one")
    assert layouts() == [("1", "English (US)")], layouts()
print("Keymaps from names and files, reloads, virtual keyboards, and broken keymap files passed")
