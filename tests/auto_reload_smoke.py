# SPDX-License-Identifier: GPL-3.0-or-later
"""Saving the configuration reloads it: written in place or renamed over, once per save, and
not at all with auto_reload = false or for files that are not Lua (or an XKB keymap). A file
with an error, at startup or on a save, gives the default configuration until a save fixes
it."""
from pathlib import Path
import sys
import time

import harness

compositor, default_config = (str(Path(p).resolve()) for p in sys.argv[1:3])
KEYMAP = """xkb_keymap {
  xkb_keycodes { include "evdev" };
  xkb_types { include "complete" };
  xkb_compat { include "complete" };
  xkb_symbols { include "pc+us" };
};
"""

with harness.Compositor(compositor, "return { xwayland = false, layout = { gap = -1 } }",
                        env={"PAW_AUTO_RELOAD": "1",
                             "PAW_DEFAULT_CONFIG": default_config}) as desktop:
    root, config, log = desktop.root, desktop.config, desktop.log

    def reloads():
        return log.read_text().count("Configuration reloaded")

    def errors():
        return log.read_text().count("using the default configuration")

    def settle():
        time.sleep(0.6)

    assert errors() == 1 and "layout.gap must be between" in log.read_text()
    # Written in place, and fixed.
    config.write_text("return { xwayland = false, layout = { gap = 3 } }")
    desktop.wait_for(lambda: reloads() == 1, "reload after writing")
    # Saved through a temporary file renamed over it, as many editors do: one reload.
    temporary = root / "init.lua.tmp"
    temporary.write_text("return { xwayland = false, layout = { gap = 4 } }")
    temporary.rename(config)
    desktop.wait_for(lambda: reloads() == 2, "reload after renaming")
    settle()
    assert reloads() == 2, "one save reloaded more than once"
    # Another Lua file beside it, such as theme.lua, counts; other files do not.
    (root / "notes.txt").write_text("not configuration")
    settle()
    assert reloads() == 2, "a file that is not Lua reloaded"
    (root / "theme.lua").write_text("return {}")
    desktop.wait_for(lambda: reloads() == 3, "reload after writing theme.lua")
    # An error on a save gives the default configuration again.
    config.write_text("return { xwayland = false, layout = { bogus = 1 } }")
    desktop.wait_for(lambda: errors() == 2, "error on a save")
    assert reloads() == 4
    # An XKB keymap beside it counts too (saved here while the error stands, it reloads
    # into the same error): saving keyboard.file reloads, a broken one gives the default
    # configuration, and saving it fixed brings the configuration back.
    keymap = root / "keymap.xkb"
    keymap.write_text(KEYMAP)
    desktop.wait_for(lambda: reloads() == 5 and errors() == 3, "reload after an .xkb")
    config.write_text('return { xwayland = false, keyboard = { file = "keymap.xkb" } }')
    desktop.wait_for(lambda: reloads() == 6, "reload naming the keymap file")
    keymap.write_text(KEYMAP.replace("pc+us", "pc+no"))
    desktop.wait_for(lambda: reloads() == 7, "reload after saving the keymap")
    keymap.write_text("xkb_keymap { oops };\n")
    desktop.wait_for(lambda: errors() == 4 and reloads() == 8, "error from a broken keymap")
    assert "init.lua:1: keyboard.file: " in log.read_text()
    keymap.write_text(KEYMAP)
    desktop.wait_for(lambda: reloads() == 9, "reload after fixing the keymap")
    settle()
    assert errors() == 4 and reloads() == 9, (errors(), reloads())
    # Turned off, saving changes nothing until the next manual reload.
    config.write_text("return { xwayland = false, auto_reload = false }")
    desktop.wait_for(lambda: reloads() == 10, "reload that turns it off")
    config.write_text("return { xwayland = false, auto_reload = false, layout = { gap = 5 } }")
    keymap.write_text(KEYMAP)
    settle()
    assert reloads() == 10, "reloaded with auto_reload = false"
    assert errors() == 4
print("Automatic reload on save passed")
