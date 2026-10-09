# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise ext-session-lock-v1 on a private headless compositor."""
from pathlib import Path
import re
import socket
import subprocess
import sys

import harness

compositor, lock_probe, example = (str(Path(p).resolve()) for p in sys.argv[1:4])

source = Path(example).read_text().replace("xwayland = true", "xwayland = false")

with harness.Compositor(compositor, source) as desktop:
    # The control socket's subscribers hear whether the session is locked (the shell records no
    # clipboard then), as it locks and as it unlocks.
    subscriber = socket.socket(socket.AF_UNIX)
    subscriber.connect(desktop.env["PAW_SOCKET"])
    subscriber.sendall(b"subscribe\n")
    subscriber.settimeout(0.05)
    heard = []

    def locked():
        """The locked lines heard so far, without repeats."""
        buffer = ""
        try:
            while data := subscriber.recv(8192):
                buffer += data.decode()
        except socket.timeout:
            pass
        for line in re.findall(r"^locked (\S+)$", buffer, re.M):
            if not heard or heard[-1] != line:
                heard.append(line)
        return heard

    desktop.wait_for(lambda: locked() == ["off"], "the first state", detail=locked)
    locker = desktop.spawn([lock_probe, "hold", str(desktop.root / "locker.log")])
    desktop.wait_for(lambda: locked() == ["off", "on"], "locked on", detail=locked)
    locker.terminate()
    assert desktop.reap(locker) == 0
    desktop.wait_for(lambda: locked() == ["off", "on", "off"], "locked off", detail=locked)
    subscriber.close()
    # A crashed locker leaves the session locked; a new locker may take over.
    for mode in ("abandon", "check-locked", "cycle"):
        subprocess.run([lock_probe, mode], env=desktop.env, check=True, timeout=20)
    text = desktop.log.read_text()
    assert "Lock client vanished" in text and "Session unlocked" in text, text
print("Session lock, rejection, focus isolation, abandonment, unlock and the locked state passed")
