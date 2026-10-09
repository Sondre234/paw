# SPDX-License-Identifier: GPL-3.0-or-later
"""The display settings window's side in the compositor, on three headless monitors, one a laptop's
panel: `get monitors` says what the window shows; `monitors apply` puts every monitor's settings on
trial at once, after testing them, and they go back by themselves unless kept, at once where a
monitor refuses them; `monitors keep` writes them to $XDG_STATE_HOME/paw/outputs, which is
laid over outputs.monitors as the compositor starts, for the monitor each line was kept for;
`monitors reset` puts the configuration back and removes the file once kept. Applying takes over
from a wlr-output-management client's changes, and going back brings them back."""
from pathlib import Path
import socket
import subprocess
import sys

import harness

compositor, randr_probe = (str(Path(p).resolve()) for p in sys.argv[1:3])

CONFIG = """return {
    xwayland = false,
    animations = { enabled = false },
    idle = { display_off = 0 },
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720", scale = 1.5, tiling = true } } },
}"""
# A trial lasts a minute while the test keeps or reverts it, and a moment where it runs out.
LONG, SHORT = "60000", "3000"
env = {"WLR_HEADLESS_OUTPUTS": "2", "PAW_TEST_TRIAL_MS": LONG,
       "PAW_TEST_REFUSE_MODE": "1600x900", "PAW_TEST_REFUSE_COMMIT": "1024x768"}

with harness.Compositor(compositor, CONFIG, env=env) as desktop:
    msg, wait_for = desktop.msg, desktop.wait_for
    state_file = desktop.root / "state" / "paw" / "outputs"

    def outputs():
        """name: (in the layout, x, y, logical width, scale, mirrored) per monitor."""
        return {r[0]: (r[1] == "1", int(r[2]), int(r[3]), int(r[4]), float(r[6]), r[11])
                for r in desktop.rows("outputs")}

    def monitors():
        """name: the columns of `get monitors` by name."""
        names = ["name", "description", "built_in", "source", "enabled", "state", "mirror", "x",
                 "y", "mode", "scale", "transform", "vrr", "bit_depth", "drawn", "hdr",
                 "hdr_state", "hdr_why", "primary", "modes"]
        return {r[0]: dict(zip(names, r)) for r in desktop.rows("monitors")}

    def trial():
        return msg("get", "monitors_trial").strip()

    class Events:
        """A subscriber's monitors- lines."""

        def __init__(self):
            self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.sock.connect(desktop.env["PAW_SOCKET"])
            self.sock.sendall(b"subscribe\n")
            self.sock.settimeout(0.05)
            self.buffer, self.lines = b"", []

        def seen(self, line):
            try:
                while data := self.sock.recv(65536):
                    self.buffer += data
            except socket.timeout:
                pass
            *complete, self.buffer = self.buffer.split(b"\n")
            self.lines += [text.decode() for text in complete if text.startswith(b"monitors-")]
            return line in self.lines

    def randr(*args):
        result = subprocess.run([randr_probe, *args], env=desktop.env, capture_output=True,
                                text=True, timeout=30)
        assert result.returncode == 0, result.stderr
        return result.stdout.split("\n")[:-1]

    desktop.detail = lambda: f"outputs: {outputs()}, trial: {trial()}"
    msg("headless_output", "add", "eDP-1", "1920x1080")
    START = {"HEADLESS-2": (True, 0, 0, 1280, 1.0, "-"),
             "HEADLESS-1": (True, 1280, 0, 853, 1.5, "-"),
             "eDP-1": (True, 2133, 0, 1920, 1.0, "-")}
    wait_for(lambda: outputs() == START, "three monitors side by side")

    # What the window shows: the settings, where they come from, and what each can have.
    shown = monitors()
    assert list(shown) == ["HEADLESS-2", "HEADLESS-1", "eDP-1"], shown
    one = shown["HEADLESS-1"]
    assert (one["source"], one["enabled"], one["state"], one["mirror"], one["x"], one["mode"],
            one["scale"], one["vrr"], one["bit_depth"], one["hdr"], one["primary"],
            one["modes"]) == ("config", "1", "on", "-", "1280", "1280x720", "1.5", "-", "8",
                              "off", "0", "1280x720"), one
    assert one["hdr_why"] == "the monitor does not offer BT.2020 with PQ", one
    assert shown["HEADLESS-2"]["source"] == "default" and shown["eDP-1"]["built_in"] == "1", shown
    assert trial() == "-"
    events = Events()

    # A trial: every monitor at once, those not named as they are, back by themselves unless kept.
    reply = msg("monitors", "apply", "HEADLESS-2", "scale=2", "position=0,0",
                "HEADLESS-1", "position=640,0", "primary=on")
    assert reply.split() == [LONG], reply
    TRIAL = {"HEADLESS-2": (True, 0, 0, 640, 2.0, "-"),
             "HEADLESS-1": (True, 640, 0, 853, 1.5, "-"),
             "eDP-1": (True, 2133, 0, 1920, 1.0, "-")}
    assert outputs() == TRIAL, outputs()
    wait_for(lambda: events.seen(f"monitors-trial {LONG}"), "the shell heard of the trial")
    assert 0 < int(trial()) <= int(LONG), trial()
    shown = monitors()
    assert {m["source"] for m in shown.values()} == {"window"}, shown
    assert [n for n, m in shown.items() if m["primary"] == "1"] == ["HEADLESS-1"], shown
    msg("monitors", "revert")
    wait_for(lambda: events.seen("monitors-reverted asked"), "the shell heard it go back")
    assert outputs() == START and trial() == "-", outputs()
    assert monitors()["HEADLESS-1"]["source"] == "config"
    assert "no monitor settings are on trial" in msg("monitors", "revert", ok=False)
    assert "no monitor settings are on trial" in msg("monitors", "keep", ok=False)

    # Settings that cannot be had change nothing.
    for words, error in [(("DP-9", "scale=2"), "no monitor DP-9"),
                         (("scale=2",), "name a monitor"),
                         (("HEADLESS-2", "scale=99"), "cannot take scale=99"),
                         (("HEADLESS-2", "sparkle=on"), "no setting sparkle"),
                         (("HEADLESS-2", "description=x"), "no setting description"),
                         (("HEADLESS-2", "mirror=DP-9"), "not connected"),
                         (("HEADLESS-2", "mirror=eDP-1", "eDP-1", "mirror=HEADLESS-1"),
                          "which mirrors HEADLESS-1"),
                         (("HEADLESS-2", "enabled=off", "HEADLESS-1", "enabled=off", "eDP-1",
                           "mirror=HEADLESS-1"), "must show the desktop"),
                         # The test refuses this size: nothing changes.
                         (("HEADLESS-2", "mode=1600x900"), "HEADLESS-2 refused these settings")]:
        assert error in msg("monitors", "apply", *words, ok=False), (words, error)
        assert outputs() == START and trial() == "-", (words, outputs())
    assert "usage" in msg("monitors", "sideways", ok=False)
    # A monitor that passes the test and then does not take its settings: back at once.
    error = msg("monitors", "apply", "HEADLESS-2", "mode=1024x768", "HEADLESS-1", "scale=1",
                ok=False)
    assert "HEADLESS-2 did not take its settings" in error, error
    wait_for(lambda: events.seen("monitors-reverted refused HEADLESS-2"), "the shell heard")
    assert outputs() == START and trial() == "-", outputs()
    assert not state_file.exists()

    # A wlr-output-management client's change shows in the window, which takes it over as it
    # applies its settings; it comes back with the trial's end.
    assert randr("apply", "eDP-1", "scale=2") == ["succeeded"]
    wait_for(lambda: outputs()["eDP-1"][4] == 2.0, "wlr-randr's scale")
    assert monitors()["eDP-1"]["source"] == "override"
    msg("monitors", "apply", "eDP-1", "transform=1")
    assert outputs()["eDP-1"][3:5] == (540, 2.0), outputs()
    assert monitors()["eDP-1"]["source"] == "window"
    msg("monitors", "revert")
    assert outputs()["eDP-1"][3:5] == (960, 2.0), outputs()
    assert monitors()["eDP-1"]["source"] == "override"
    desktop.reload()
    wait_for(lambda: outputs() == START, "a reload brought the configuration back")

    # Kept: written to the state file, a line per monitor with the description it was kept for.
    msg("monitors", "apply", "HEADLESS-2", "enabled=off", "HEADLESS-1", "mirror=eDP-1", "eDP-1",
        "mode=1920x1080", "primary=on")
    KEPT = {"eDP-1": (True, 0, 0, 1920, 1.0, "-"),
            "HEADLESS-1": (False, 0, 0, 0, 1.5, "eDP-1"),
            "HEADLESS-2": (False, 0, 0, 0, 1.0, "-")}
    assert outputs() == KEPT, outputs()
    shown = monitors()
    assert (shown["HEADLESS-1"]["mirror"], shown["HEADLESS-1"]["x"]) == ("eDP-1", "2133"), shown
    assert shown["HEADLESS-2"]["enabled"] == "0" and shown["HEADLESS-2"]["state"] == "off", shown
    msg("monitors", "keep")
    wait_for(lambda: events.seen("monitors-kept"), "the shell heard it kept")
    assert trial() == "-"
    lines = state_file.read_text().splitlines()
    assert lines[0] == "paw-outputs 1", lines
    kept = {line.split("\t")[0]: dict(f.split("=", 1) for f in line.split("\t")[1:])
            for line in lines if line and not line.startswith(("#", "paw"))}
    assert kept["HEADLESS-2"]["enabled"] == "off", kept
    assert kept["HEADLESS-1"]["mirror"] == "eDP-1" and kept["HEADLESS-1"]["scale"] == "1.5", kept
    assert kept["eDP-1"]["primary"] == "on" and kept["eDP-1"]["mode"] == "1920x1080", kept
    assert kept["eDP-1"]["position"] == "2133,0", kept
    # A reload reads them again.
    desktop.reload()
    assert outputs() == KEPT, outputs()

    # A reload during a trial ends it, the kept settings in force again.
    msg("monitors", "apply", "HEADLESS-2", "enabled=on", "position=0,0")
    assert outputs()["HEADLESS-2"][0], outputs()
    desktop.reload()
    wait_for(lambda: events.seen("monitors-reverted reload"), "the shell heard of the reload")
    assert outputs() == KEPT and trial() == "-", outputs()

    # They are laid over outputs.monitors as the compositor starts, for the monitor each line was
    # kept for: another monitor on HEADLESS-1's connector keeps the configuration's settings.
    desktop.stop()
    state_file.write_text(state_file.read_text().replace("HEADLESS-1\tdescription=  \t",
                                                         "HEADLESS-1\tdescription=Other\t"))
    desktop.env["PAW_TEST_TRIAL_MS"] = SHORT
    desktop.start()
    msg("headless_output", "add", "eDP-1", "1920x1080")
    wait_for(lambda: outputs() == {"eDP-1": (True, 0, 0, 1920, 1.0, "-"),
                                   "HEADLESS-1": (True, 1920, 0, 853, 1.5, "-"),
                                   "HEADLESS-2": (False, 0, 0, 0, 1.0, "-")},
             "the kept settings, but HEADLESS-1's, which another monitor's are")
    shown = monitors()
    assert [shown[n]["source"] for n in ("eDP-1", "HEADLESS-1", "HEADLESS-2")] == [
        "window", "config", "window"], shown
    events = Events()

    # A trial not kept goes back by itself.
    msg("monitors", "apply", "HEADLESS-2", "enabled=on", "position=-1280,0")
    wait_for(lambda: outputs()["HEADLESS-2"][:3] == (True, 0, 0), "HEADLESS-2 on, on the left")
    wait_for(lambda: events.seen(f"monitors-reverted timeout"), "the trial ran out")
    assert not outputs()["HEADLESS-2"][0] and trial() == "-", outputs()

    # Reset: the configuration alone, on trial; kept, the file goes.
    desktop.env["PAW_TEST_TRIAL_MS"] = LONG
    desktop.stop()
    desktop.start()
    msg("headless_output", "add", "eDP-1", "1920x1080")
    wait_for(lambda: len(outputs()) == 3, "three monitors")
    assert monitors()["eDP-1"]["source"] == "window"
    msg("monitors", "reset")
    wait_for(lambda: all(o[0] and o[5] == "-" for o in outputs().values()), "every monitor on")
    assert {m["source"] for m in monitors().values()} == {"config", "default"}, monitors()
    msg("monitors", "keep")
    assert not state_file.exists()
print("The display settings' monitors went on trial, back, and were kept")
