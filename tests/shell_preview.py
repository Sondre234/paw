# SPDX-License-Identifier: GPL-3.0-or-later
"""Load and render both QML roots without a desktop connection."""
import os
import select
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time
import zlib

executable, config = (str(Path(path).resolve()) for path in sys.argv[1:])
with tempfile.TemporaryDirectory(prefix="paw-ui-") as directory:
    root = Path(directory)
    applications = root / "applications"
    applications.mkdir()
    (applications / "test.desktop").write_text(
        "[Desktop Entry]\nType=Application\nName=Test application\nExec=true\n"
    )
    empty = root / "empty"
    empty.mkdir()
    # Qt logs to the systemd journal instead of stderr when JOURNAL_STREAM is set, which
    # would hide the QML warnings asserted on below.
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software",
               QT_FORCE_STDERR_LOGGING="1",
               XDG_DATA_HOME=directory, XDG_DATA_DIRS=str(empty))
    for desktop in (False, True):
        screenshot = root / ("desktop.png" if desktop else "panel.png")
        command = [executable, "--config", config, "--preview", "--quit-after", "300",
                   "--screenshot", str(screenshot)]
        if desktop:
            command.append("--preview-desktop")
        result = subprocess.run(command, env=env, capture_output=True, text=True, timeout=30)
        assert result.returncode == 0, result.stderr
        assert "ReferenceError" not in result.stderr and "TypeError" not in result.stderr, result.stderr
        image = screenshot.read_bytes()
        assert image[:8] == b"\x89PNG\r\n\x1a\n", "no PNG produced"
        width, height = struct.unpack(">II", image[16:24])
        assert width >= 640 and height >= 300 and len(image) > 1000, "empty or undersized rendering"
    # A wallpaper that is missing when the shell starts is retried until it loads. Random
    # pixels keep the screenshot large, while the fallback gradient compresses to little. The
    # retry comes a second after the failure, which comes at once.
    late = root / "late.png"
    late_config = root / "late.lua"
    late_config.write_text(f'return {{ version = 1, shell = {{ wallpaper = "{late}" }} }}\n')
    screenshot = root / "late-desktop.png"
    shell = subprocess.Popen(
        [executable, "--config", str(late_config), "--preview", "--preview-desktop",
         "--quit-after", "2500", "--screenshot", str(screenshot)],
        env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    # Write the file only once the shell has failed to load it, however slowly it starts.
    seen = ""
    deadline = time.monotonic() + 8
    while "wallpaper failed to load, retrying" not in seen:
        assert time.monotonic() < deadline and shell.poll() is None, seen
        if select.select([shell.stderr], [], [], 0.1)[0]:
            seen += os.read(shell.stderr.fileno(), 4096).decode()
    width, height = 320, 200
    rows = b"".join(b"\0" + os.urandom(width * 3) for _ in range(height))
    chunk = lambda kind, data: (struct.pack(">I", len(data)) + kind + data
                                + struct.pack(">I", zlib.crc32(kind + data)))
    late.write_bytes(b"\x89PNG\r\n\x1a\n"
                     + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))
    _, stderr = shell.communicate(timeout=20)
    assert shell.returncode == 0, seen + stderr
    assert screenshot.stat().st_size > 200_000, "late wallpaper was not retried"
    print("Taskbar/launcher and desktop QML rendered successfully")
