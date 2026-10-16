# SPDX-License-Identifier: GPL-3.0-or-later
"""power.lock_style: the `lock` action starts the stock `swaylock -f` in the appearance profile's
look, its wallpaper (the one picked from the panel too), colours and font, and leaves it as it is
under a swaylock configuration of the user's, another lock_command or lock_style = false. A
stand-in swaylock on PATH writes down its arguments; no real locker locks anything, though a real
swaylock's option parser checks them."""
from pathlib import Path
import os
import shutil
import subprocess
import sys

import harness

compositor = str(Path(sys.argv[1]).resolve())

PROFILE = """return {{
    xwayland = false,
    profile = "paper",
    profiles = {{ paper = {{
        appearance = {{ background = "#f4f0f3" }},
        windows = {{ border_color = {{ "#8fc1ec", "#f2a0be" }}, urgent_color = "#d9608c" }},
        shell = {{ accent = "#5e9edb", panel_color = "#f0ecf0", text_color = "#363c58",
                  font = "Space Grotesk", wallpaper = "paper.png" }},
    }} }},
    power = {{ {power} }},
}}"""

if Path("/etc/swaylock/config").exists():
    print("/etc/swaylock/config exists, whose look paw leaves alone: not tested")
    sys.exit(0)

with harness.Compositor(compositor, PROFILE.format(power=""), start=False) as desktop:
    root, msg = desktop.root, desktop.msg
    log = root / "swaylock.log"
    log.touch()
    tools, home, config_home = root / "bin", root / "home", root / "config"
    for directory in (tools, home, config_home):
        directory.mkdir()
    # Each run: its arguments, each ended by a NUL, on a line.
    script = tools / "swaylock"
    script.write_text(f"#!/bin/sh\nprintf '%s\\0' \"$@\" >> '{log}'\necho >> '{log}'\n")
    script.chmod(0o755)
    # Neither the developer's swaylock configuration nor their PATH's swaylock.
    desktop.env.update(PATH=f"{tools}:{os.environ.get('PATH', '/usr/bin:/bin')}",
                       HOME=str(home), XDG_CONFIG_HOME=str(config_home))
    wallpaper = root / "paper.png"
    wallpaper.write_bytes(b"not looked at")
    desktop.start()

    def runs():
        return [line.split("\0")[:-1] for line in log.read_text().splitlines()]

    def lock():
        """The arguments of the locker `lock` starts."""
        before = len(runs())
        msg("lock")
        desktop.wait_for(lambda: len(runs()) > before, "the locker started")
        return runs()[-1]

    def option(arguments, name):
        return arguments[arguments.index(name) + 1] if name in arguments else None

    desktop.detail = lambda: f"swaylock ran with {runs()}"

    # The profile's look, after the stock -f.
    arguments = lock()
    assert arguments[0] == "-f", arguments
    assert option(arguments, "--image") == f":{wallpaper}", arguments
    assert option(arguments, "--scaling") == "fill", arguments
    assert option(arguments, "--color") == "f4f0f3ff", arguments
    assert option(arguments, "--font") == "Space Grotesk", arguments
    assert option(arguments, "--ring-color") == "5e9edbff", arguments
    assert option(arguments, "--inside-color") == "f0ecf0ff", arguments
    assert option(arguments, "--text-color") == "363c58ff", arguments
    assert option(arguments, "--key-hl-color") == "f2a0beff", arguments
    assert option(arguments, "--ring-wrong-color") == "d9608cff", arguments
    assert "--indicator-idle-visible" in arguments and "--show-failed-attempts" in arguments
    # The real swaylock, where installed, takes every option: only its option parser runs, as
    # --version last makes it exit before it connects anywhere (and there is nothing to connect
    # to in an empty runtime directory), while an option it does not know fails first. Only a
    # program itself, not a script that might do more.
    real = shutil.which("swaylock", path=os.environ.get("PATH"))
    if real and Path(real).read_bytes()[:4] == b"\x7fELF":
        empty = root / "empty"
        empty.mkdir(mode=0o700)
        checked = subprocess.run([real, *arguments, "--version"], capture_output=True, text=True,
                                 timeout=10, env={"PATH": os.environ.get("PATH", ""),
                                                  "HOME": str(home), "XDG_RUNTIME_DIR": str(empty)})
        assert checked.returncode == 0 and "version" in checked.stdout, \
            (checked.stderr.splitlines()[:1], arguments)

    # The wallpaper picked from the panel for the profile, as the shell saves it.
    picked = root / "picked one.jpg"
    picked.write_bytes(b"not looked at either")
    state = root / "state" / "paw"
    state.mkdir(parents=True, exist_ok=True)
    (state / "wallpapers").write_text(f"paper\tpaper.png\t{picked}\n")
    assert option(lock(), "--image") == f":{str(picked).replace(' ', chr(92) + ' ')}"
    # Gone: the background colour alone.
    picked.unlink()
    arguments = lock()
    assert "--image" not in arguments and option(arguments, "--color") == "f4f0f3ff", arguments

    # The user's swaylock configuration, in either place swaylock reads, keeps its look.
    for place in (config_home / "swaylock", home / ".swaylock"):
        place.mkdir()
        (place / "config").write_text("color=000000\n")
        assert lock() == ["-f"], place
        (place / "config").unlink()
    assert option(lock(), "--color") == "f4f0f3ff"

    # Off, or another command: as it is.
    desktop.reload(PROFILE.format(power="lock_style = false"))
    assert lock() == ["-f"]
    desktop.reload(PROFILE.format(power='lock_command = { "swaylock", "--daemonize" }'))
    assert lock() == ["--daemonize"]
