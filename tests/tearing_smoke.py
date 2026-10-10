# SPDX-License-Identifier: GPL-3.0-or-later
"""Tearing on a headless output, as get tearing reports it: with windows.allow_tearing a fullscreen
window asking through tearing-control-v1, or one a rule's allow_tearing names, has its output's
frames flipped at once, but not one that does neither, nor while a panel is drawn over it or the
overview is open, nor with the setting off; where the backend refuses an asynchronous flip
(PAW_TEST_REFUSE_TEARING, as a GPU without them would) the frames go out at the refresh as
before. A real flip cannot be seen headless: the decisions and the commits' flags are what is
tested."""
from pathlib import Path
import sys

import harness

compositor, probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {{
    xwayland = false,
    animations = {{ enabled = false }},
    windows = {{
        allow_tearing = {allow},
        rules = {{
            {{ app_id = "^(hinted|game|plain)$", fullscreen = true }},
            {{ app_id = "^game$", allow_tearing = true }},
        }},
    }},
}}"""


def config(allow="true"):
    return CONFIG.format(allow=allow)


# Refused: the frames go out at the refresh, and nothing tears.
with harness.Compositor(compositor, config(),
                        env={"PAW_TEST_REFUSE_TEARING": "HEADLESS-1"}) as desktop:
    def tearing():
        row = desktop.rows("tearing")[0]
        return row[1], int(row[2]), int(row[3]), row[4]
    desktop.detail = lambda: f"tearing: {tearing()}"
    desktop.spawn([probe, "--window-only"], env={"PAW_PROBE_APP_ID": "hinted",
                                                 "PAW_PROBE_TITLE": "hinted",
                                                 "PAW_PROBE_TEARING": "async"})
    desktop.wait_for(lambda: tearing()[0] == "refused" and tearing()[2] > 0,
                     "the asynchronous flip refused")
    assert tearing()[1] == 0 and tearing()[3] == "hinted", tearing()
    assert "HEADLESS-1 refuses asynchronous page flips" in desktop.log.read_text()

with harness.Compositor(compositor, config()) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for

    def tearing():
        """(what the frames do, flipped at once, refused, title) of HEADLESS-1."""
        row = desktop.rows("tearing")[0]
        return row[1], int(row[2]), int(row[3]), row[4]

    def window(app_id, *arguments, **env):
        process = desktop.spawn([probe, *arguments],
                                env={"PAW_PROBE_APP_ID": app_id,
                                     "PAW_PROBE_TITLE": app_id, **env})
        wait_for(lambda: any(w.app_id == app_id for w in desktop.windows()), f"{app_id} opened")
        return process

    def close(process):
        process.terminate()
        desktop.reap(process)
        wait_for(lambda: not desktop.windows(), "the window closed")

    desktop.detail = lambda: f"tearing: {tearing()}"
    assert tearing()[0] in ("no fullscreen window", "off"), tearing()

    # A fullscreen window that does not ask, and no rule names: no tearing.
    plain = window("plain", "--window-only")
    wait_for(lambda: tearing()[0] == "not asked", "a window that does not ask")
    close(plain)

    # One that asks through tearing-control-v1 tears; its frames flip at once.
    hinted = window("hinted", "--window-only", PAW_PROBE_TEARING="async")
    wait_for(lambda: tearing()[0] == "tearing" and tearing()[1] > 0, "the hinted window tears")
    assert tearing()[2:] == (0, "hinted"), tearing()
    # The overview over it: no tearing until it closes.
    msg("toggle_overview")
    wait_for(lambda: tearing()[0] == "overlay", "no tearing under the overview")
    msg("toggle_overview")
    wait_for(lambda: tearing()[0] == "tearing", "tearing again")
    close(hinted)

    # One that asks for vsync does not.
    vsync = window("hinted", "--window-only", PAW_PROBE_TEARING="vsync")
    wait_for(lambda: tearing()[0] == "not asked", "a window asking for vsync")
    close(vsync)

    # A rule's allow_tearing makes a window tear that does not ask, as an X11 game cannot.
    game = window("game", "--window-only")
    wait_for(lambda: tearing()[0] == "tearing", "the window the rule names tears")
    close(game)

    # A panel over a fullscreen window the compositor made so (not one that asked itself, which
    # covers the panels): something else is drawn on the output, so no tearing.
    hinted = window("hinted", PAW_PROBE_TEARING="async")
    wait_for(lambda: tearing()[0] == "covered", "the panel over the window")
    close(hinted)

    # windows.allow_tearing off: nothing tears whatever windows ask.
    desktop.reload(config(allow="false"))
    hinted = window("hinted", "--window-only", PAW_PROBE_TEARING="async")
    wait_for(lambda: tearing()[0] == "off", "nothing tears with the setting off")
    flips = tearing()[1]
    desktop.stays(lambda: tearing()[1] == flips, "no frame flipped at once")
print("Fullscreen windows that ask or are ruled to tear do, alone on their output and allowed")
