# SPDX-License-Identifier: GPL-3.0-or-later
"""A compositor that cannot make its Wayland socket says why and exits with status 1, rather than
aborting: with XDG_RUNTIME_DIR unset, too long for a socket's path, or not writable."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

compositor, example = (str(Path(p).resolve()) for p in sys.argv[1:3])


def start(runtime):
    env = dict(os.environ, WLR_RENDERER="pixman")
    for name in ("WAYLAND_DISPLAY", "DISPLAY", "PAW_SOCKET", "XDG_RUNTIME_DIR"):
        env.pop(name, None)
    if runtime:
        env["XDG_RUNTIME_DIR"] = str(runtime)
    return subprocess.run([compositor, "--headless", "--config", example], env=env,
                          capture_output=True, text=True, timeout=30)


with tempfile.TemporaryDirectory(prefix="paw-startup-") as directory:
    root = Path(directory)
    # A socket's path holds 107 bytes; this directory alone is longer.
    long = root / ("x" * 100)
    long.mkdir(mode=0o700)
    locked = root / "locked"
    locked.mkdir(mode=0o500)
    cases = [(None, "XDG_RUNTIME_DIR is not set"),
             (long, f"XDG_RUNTIME_DIR ({long}) is too long for a socket path")]
    if os.geteuid() != 0:  # root writes anywhere
        cases.append((locked, f"in XDG_RUNTIME_DIR ({locked}): Permission denied"))
    for runtime, reason in cases:
        result = start(runtime)
        assert result.returncode == 1, (runtime, result.returncode, result.stderr[-3000:])
        assert "[ERROR]" in result.stderr and "Cannot create a Wayland socket" in result.stderr \
            and reason in result.stderr, (runtime, result.stderr[-3000:])
    print(f"{len(cases)} unusable runtime directories end the compositor with an error")
