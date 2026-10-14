# SPDX-License-Identifier: GPL-3.0-or-later
"""paw fetch: the session's profile, uptime, frames, windows and monitors beside a paw print in
the profile's colours, the top application and the saved sessions from the state files, plain
when the output is no terminal or colour is turned off, and what it can tell outside a session."""
import os
from pathlib import Path
import pty
import re
import subprocess
import sys
import tempfile

import harness

compositor, probe, example = (str(Path(p).resolve()) for p in sys.argv[1:4])

source = Path(example).read_text().replace("xwayland = true", "xwayland = false")
escape = re.compile(r"\x1b\[[0-9;]*m")


def fetch(env, *arguments, terminal=False):
    """paw fetch's output, through a pipe or a pseudo-terminal 120 columns wide."""
    if not terminal:
        done = subprocess.run([compositor, "fetch", *arguments], env=env, capture_output=True,
                              text=True, timeout=30)
        assert done.returncode == 0, done.stderr
        return done.stdout
    leader, follower = pty.openpty()
    subprocess.run(["stty", "cols", "120", "rows", "40"], stdin=follower, check=True)
    process = subprocess.Popen([compositor, "fetch", *arguments], env=env, stdout=follower,
                               stderr=subprocess.PIPE)
    os.close(follower)
    output = b""
    while True:
        try:
            chunk = os.read(leader, 65536)
        except OSError:  # the terminal closes as the program exits
            break
        if not chunk:
            break
        output += chunk
    os.close(leader)
    assert process.wait(timeout=30) == 0, process.stderr.read()
    return output.decode().replace("\r\n", "\n")


def fields(output):
    """The fields beside the logo, label to value; a continued field's lines joined by "|"."""
    found, label = {}, None
    for line in escape.sub("", output).splitlines():
        text = line[25:] if line.startswith((" ", "▄", "█", "▀")) else line
        if match := re.match(r"(\S+(?: \S+)?)  +(.+)$", text):
            label = match[1]
            found[label] = match[2]
        elif label and text.startswith(" ") and text.strip():
            found[label] += "|" + text.strip()
    return found


with tempfile.TemporaryDirectory(prefix="paw-fetch-") as home:
    home = Path(home)
    data, config = home / "data", home / "config"
    entry = data / "applications/org.example.Kitten.desktop"
    entry.parent.mkdir(parents=True)
    entry.write_text("[Desktop Entry]\nType=Application\nName=Kitten Terminal\nExec=kitten\n")
    env = {"XDG_DATA_HOME": str(data), "XDG_DATA_DIRS": str(data), "XDG_CONFIG_HOME": str(config),
           "NO_COLOR": ""}
    with harness.Compositor(compositor, source, env=env) as desktop:
        msg = desktop.msg
        version = subprocess.run([compositor, "--version"], capture_output=True, text=True,
                                 check=True).stdout.splitlines()
        state = Path(desktop.env["XDG_STATE_HOME"]) / "paw"

        # The queries it reads the session through.
        about = dict(row[:2] for row in desktop.rows("about"))
        assert about["version"] == version[0].removeprefix("paw "), about
        assert re.fullmatch(r"0\.20\.\d+", about["wlroots"]), about
        assert int(about["uptime"]) >= 0 and about["profile"] == "macos-light", about
        msg("profile", "washi")
        desktop.wait_for(lambda: dict(row[:2] for row in desktop.rows("about"))["profile"] ==
                         "washi", "the washi profile in use")
        palette = {row[0]: row[1:] for row in desktop.rows("palette")}
        assert palette["scheme"] == ["light"] and palette["background"] == ["#f4f0f3ff"], palette
        assert palette["border"] == ["135", "#8fc1ecff", "#f2a0beff"], palette
        assert palette["border_inactive"] == ["0", "#4a5170ff"], palette
        assert palette["urgent"] == ["#d9608cff"], palette

        desktop.open_window(probe, "fetched")
        desktop.wait_for(lambda: int(desktop.rows("stats")[0][0]) > 0, "a frame drawn")
        (state / "launches").write_text("org.example.Kitten.desktop\t3\t1791983390\n"
                                        "firefox.desktop\t2\t1791983391\n")
        msg("session", "save", "work")
        msg("session", "save", "showcase-42")  # paw showcase's, while it runs

        # Through a pipe: no colours, the logo and the fields.
        output = fetch(desktop.env)
        assert "\x1b" not in output, repr(output)
        lines = output.splitlines()
        title = "paw " + version[0].removeprefix("paw ").replace("(git ", "(") + " · wlroots "
        assert lines[0].startswith("     ▄██▄    ▄██▄        " + title), lines[0]
        assert lines[7].startswith("    ▀██▀ ▀██▀ ▀██▀       "), lines[7]
        found = fields(output)
        assert found["profile"] == "washi · light", found
        assert re.fullmatch(r"\S+ · \S+ frames?", found["session"]), found
        assert re.fullmatch(r"avg \S+ms( · p99 \S+ms)? · worst \S+ms", found["frames"]), found
        assert found["windows"] == "1 on 1 workspace · floating", found
        assert found["monitor"] == "HEADLESS-1 1280x720", found
        assert "dim " in found["idle"], found
        assert found["top app"] == "Kitten Terminal · 3 launches", found
        assert found["sessions"] == "work", found
        assert "running" not in found, found

        # A second monitor, the tiling and its layout.
        msg("headless_output", "add", "DP-2", "1920x1080")
        desktop.wait_for(lambda: len(desktop.rows("outputs")) == 2, "a second monitor")
        msg("toggle_tiling")
        desktop.wait_for(lambda: msg("get", "tiling").strip() == "on", "tiling on")
        found = fields(fetch(desktop.env))
        assert sorted(found["monitors"].split("|")) == ["DP-2 1920x1080", "HEADLESS-1 1280x720"], \
            found
        layout = desktop.rows("layout")[0][0]
        assert found["windows"] == "1 on 1 workspace · " + layout, found

        # In a terminal: 24-bit colours, the labels and the palette's strip in washi's.
        colored = fetch(desktop.env, terminal=True)
        assert "\x1b[38;2;" in colored and "\x1b[1m" in colored, repr(colored)
        assert "\x1b[38;2;143;193;236m███" in colored, repr(colored)
        assert fields(colored)["profile"] == "washi · light", repr(colored)
        for words, variables in ((["--no-color"], {}), ([], {"NO_COLOR": "1"})):
            plain = fetch(desktop.env | variables, *words, terminal=True)
            assert "\x1b" not in plain and "▄██▄" in plain, repr(plain)
        bare = fetch(desktop.env, "--no-logo", terminal=True)
        assert escape.sub("", bare).startswith(title), repr(bare)
        assert "█" not in escape.sub("", bare).replace("███", ""), repr(bare)

        # Outside the session: the configuration's profile and colours, and the state files.
        (config / "paw").mkdir(parents=True)
        (config / "paw/init.lua").write_text(source)
        outside = {name: value for name, value in desktop.env.items() if name != "PAW_SOCKET"}
        found = fields(fetch(outside))
        assert found["profile"] == "washi · light", found
        assert found["session"] == "not in a paw session", found
        assert found["top app"] == "Kitten Terminal · 3 launches", found
        assert "windows" not in found and "frames" not in found, found
        stale = outside | {"PAW_SOCKET": str(home / "gone.sock")}
        assert fields(fetch(stale))["session"] == "not in a paw session"
        colored = fetch(outside, terminal=True)
        assert "\x1b[38;2;143;193;236m███" in colored, "washi's colours from the configuration"

        bad = subprocess.run([compositor, "fetch", "--bogus"], env=desktop.env,
                             capture_output=True, text=True, timeout=30)
        assert bad.returncode == 1 and "usage: paw fetch" in bad.stderr, bad
