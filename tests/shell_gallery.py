# SPDX-License-Identifier: GPL-3.0-or-later
"""Every popup of tools/shell_gallery.py renders in both of its themes without a QML warning,
each picture is the whole output a preview stands for, and each one with a popup differs from the
bar alone, the popup drawn over it."""
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

tool, build, renderer = sys.argv[1], sys.argv[2], sys.argv[3]
spec = importlib.util.spec_from_file_location("shell_gallery", tool)
gallery = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gallery)
popups = sys.argv[4:] or gallery.POPUPS

with tempfile.TemporaryDirectory(prefix="paw-gallery-") as directory:
    # With animations off each popup is at rest as it opens.
    command = [sys.executable, tool, build, directory, "--renderer", renderer,
               "--no-animations", "--wait", "150"]
    for popup in popups:
        command += ["--popup", popup]
    result = subprocess.run(command, capture_output=True, text=True, timeout=240)
    assert result.returncode == 0, result.stdout + result.stderr
    suffix = "-gpu" if renderer == "gpu" else ""
    pictures = {}
    for theme in gallery.THEMES:
        for popup in (popup for popup in popups if gallery.shows(theme, popup)):
            picture = Path(directory) / f"{theme}-{popup}{suffix}.png"
            data = picture.read_bytes()
            assert data[:8] == b"\x89PNG\r\n\x1a\n", f"{picture.name} is no PNG"
            pictures[theme, popup] = data
    for (theme, popup), data in pictures.items():
        width, height = struct.unpack(">II", data[16:24])
        assert width >= 640 and height >= 600, f"{theme}-{popup} is {width}x{height} pixels"
        if popup != "bar" and (theme, "bar") in pictures:
            assert data != pictures[theme, "bar"], f"{theme}-{popup} shows no popup"
print(f"{len(pictures)} popups rendered ({renderer})")
