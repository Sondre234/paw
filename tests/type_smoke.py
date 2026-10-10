# SPDX-License-Identifier: GPL-3.0-or-later
"""Typing text into what has the keyboard: `paw msg type TEXT` types it into the focused
window on a keymap made for it, accents, emoji and their sequences too, a keymap at a time when it
has more characters than one holds; the window has its own keymap back after, so that a key
pressed then types as before; and there is nothing to type into without a window."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])
KEY_A = 30  # evdev's

with harness.Compositor(compositor, "return { xwayland = false }") as desktop:
    msg = desktop.msg
    typed = desktop.root / "typed.txt"
    typed.write_text("")
    assert "nothing has the keyboard" in msg("type", "x", ok=False)

    msg("headless_keyboard", "add", "keys")
    desktop.spawn([probe, "--window-only"],
                  env={"PAW_PROBE_TYPED": str(typed), "PAW_PROBE_TITLE": "Typing"})
    desktop.wait_for(lambda: [w.title for w in desktop.windows() if w.focused] ==
                     ["Typing"], "the window focused")

    def written():
        return typed.read_text(encoding="utf-8")

    text = "Héllo wörld, 2 × 3 = 6 😀👍🏽 👩‍👩‍👧 🇳🇴"
    msg("type", text)
    desktop.wait_for(lambda: written() == text, "the text typed", detail=written)
    # The window's own keymap is back: the keyboard's a types an a.
    for state in ("press", "release"):
        msg("headless_keyboard", "key", "keys", str(KEY_A), state)
    desktop.wait_for(lambda: written() == text + "a", "a key typing as before", detail=written)
    # More different characters than one keymap holds (200), within a request's 512 bytes.
    many = "".join(chr(code) for code in range(0x100, 0x100 + 210))
    msg("type", many)
    desktop.wait_for(lambda: written() == text + "a" + many, "the long text typed",
                     detail=lambda: len(written()))
    assert "usage: type TEXT" in msg("type", ok=False)
print("Typing passed")
