# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise ext-session-lock-v1 on a private headless compositor."""
from pathlib import Path
import subprocess
import sys

import harness

compositor, lock_probe, example = (str(Path(p).resolve()) for p in sys.argv[1:4])

source = Path(example).read_text().replace("xwayland = true", "xwayland = false")

with harness.Compositor(compositor, source) as desktop:
    # The control socket's subscribers hear whether the session is locked (the shell records no
    # clipboard then), as it locks and as it unlocks.
    subscriber = desktop.subscribe()

    def locked():
        """Whether the session is locked, as subscribers heard it, each once until it
        changes."""
        return subscriber.values("locked ", changes=True)

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
