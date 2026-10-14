# SPDX-License-Identifier: GPL-3.0-or-later
"""A SIGHUP sent while the shell starts reloads it rather than ending it.

The compositor reloads its shell with SIGHUP as soon as the configuration changes, which can be
while the shell is still starting, and starts it with SIGHUP blocked so that one sent before the
shell catches it waits. Starting the shell with SIGHUP blocked and sending it at once stands in
for that: the shell must unblock it, and reload once it runs."""
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile

executable, config = (str(Path(path).resolve()) for path in sys.argv[1:])
with tempfile.TemporaryDirectory(prefix="paw-ui-") as directory:
    empty = Path(directory) / "empty"
    empty.mkdir()
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software",
               QT_FORCE_STDERR_LOGGING="1", XDG_DATA_HOME=directory, XDG_DATA_DIRS=str(empty),
               DBUS_SESSION_BUS_ADDRESS="disabled:")
    shell = subprocess.Popen(
        [executable, "--config", config, "--preview", "--quit-after", "1000"], env=env,
        stderr=subprocess.PIPE, text=True,
        preexec_fn=lambda: signal.pthread_sigmask(signal.SIG_BLOCK, {signal.SIGHUP}))
    shell.send_signal(signal.SIGHUP)
    _, log = shell.communicate(timeout=60)
    assert shell.returncode == 0, f"the shell ended with {shell.returncode}:\n{log}"
    assert "paw shell reloading on SIGHUP" in log, log
print("a SIGHUP sent while the shell starts reloads it")
