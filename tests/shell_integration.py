# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify real Qt layer surfaces, reserved space, and reload on a private compositor."""
from pathlib import Path
import shutil
import signal
import subprocess
import sys

import harness

compositor, shell, probe, example = (str(Path(p).resolve()) for p in sys.argv[1:])

# The example leaves the bar's look to theme.lua; spell it out so it can be edited here. It
# starts in the macOS style, whose profile sets the panel itself; this tests the taskbar's.
source = Path(example).read_text().replace('profile = "macos-light"', 'profile = "default"')
assert 'profile = "default"' in source
for setting in ("panel_height", "panel_position", "panel_margin", "panel_radius"):
    source = source.replace(f"-- {setting} =", f"{setting} =")

with harness.Compositor(compositor, source, start=False) as desktop:
    root, env, config, msg = desktop.root, desktop.env, desktop.config, desktop.msg
    env.update(QT_QPA_PLATFORM="wayland", QT_QUICK_BACKEND="software",
               QT_FORCE_STDERR_LOGGING="1", XDG_DATA_HOME=str(root), XDG_DATA_DIRS=str(root),
               PAW_DEFAULT_CONFIG=example,
               DBUS_SESSION_BUS_ADDRESS="disabled:")  # the shell must not use the real session bus
    shell_log = root / "shell.log"
    desktop.start()
    panels = desktop.spawn([shell, "--config", str(config)], log="shell.log")
    marker = "paw surface rendered: paw taskbar"
    desktop.wait_for(lambda: marker in shell_log.read_text(), "panel rendering")
    assert "paw surface rendered: paw desktop" in shell_log.read_text()

    def check_panel(height):
        # A render marker can precede the compositor applying the new layer state.
        last = []

        def reserved():
            last[:] = [subprocess.run([probe, "--external-panel", str(height)], env=env,
                                      capture_output=True, text=True, timeout=30)]
            return last[0].returncode == 0

        desktop.wait_for(reserved, f"panel reservation of {height}",
                         detail=lambda: last[0].stderr)

    check_panel(52)
    # The launcher action reaches the panel on the output under the pointer.
    def opened():
        return "paw launcher opened on" in shell_log.read_text()

    # The shell subscribes asynchronously: resend until the first request arrives.
    for _ in range(10):
        msg("launcher")
        try:
            desktop.wait_for(opened, "launcher opened", timeout=.5)
            break
        except harness.Timeout:
            pass
    assert opened() and "launcher closed" not in shell_log.read_text()
    msg("launcher")
    desktop.wait_for(lambda: "paw launcher closed on" in shell_log.read_text(),
                     "launcher closed")
    # The palette action opens the command palette as an overlay holding the keyboard;
    # Escape (typed with wtype where it is installed) closes it again.
    msg("palette")
    desktop.wait_for(lambda: "paw palette shown on" in shell_log.read_text(),
                     "palette shown")
    def layers():
        return msg("get", "layers")
    desktop.wait_for(lambda: "paw-palette" in layers(), "palette surface mapped",
                     detail=layers)
    wtype = shutil.which("wtype")
    if wtype:
        # Typed again until it lands: on a loaded machine the palette can map a moment before
        # it holds the keyboard, and an Escape typed in between goes elsewhere.
        def hidden():
            return "paw palette hidden on" in shell_log.read_text()
        for _ in range(10):
            subprocess.run([wtype, "-k", "Escape"], env=env, check=True, timeout=30)
            try:
                desktop.wait_for(hidden, "palette closed with Escape", timeout=1)
                break
            except harness.Timeout:
                pass
        assert hidden(), "palette closed with Escape"
    else:
        msg("palette")
    # A live panel-height change must alter maximized client geometry.
    config.write_text(source.replace("panel_height = 52", "panel_height = 72"))
    panels.send_signal(signal.SIGHUP)
    desktop.wait_for(lambda: shell_log.read_text().count(marker) >= 2,
                     "panel resize after reload")
    check_panel(72)
    # A floating bar on top reserves its height plus the margins above and below it.
    floating = (source.replace('panel_position = "bottom"', 'panel_position = "top"')
                .replace("panel_margin = 0,",
                         "panel_margin = { top = 6, bottom = 4, left = 12, right = 12 },")
                .replace("panel_radius = 0,", "panel_radius = 10,"))
    assert 'panel_position = "top"' in floating and "top = 6" in floating
    config.write_text(floating)
    panels.send_signal(signal.SIGHUP)
    desktop.wait_for(lambda: shell_log.read_text().count(marker) >= 3,
                     "panel moved to the top after reload")
    check_panel(62)
    config.write_text(source.replace("panel_height = 52", "panel_height = 72"))
    panels.send_signal(signal.SIGHUP)
    desktop.wait_for(lambda: shell_log.read_text().count(marker) >= 4,
                     "panel back at the bottom after reload")
    check_panel(72)
    # Invalid data must not kill the shell: the default configuration stands in, with
    # the error shown across the top until the file is fixed.
    config.write_text("return { shell = { panel_height = -1 } }")
    panels.send_signal(signal.SIGHUP)
    desktop.wait_for(lambda: "paw configuration error shown on" in shell_log.read_text(),
                     "configuration error banner")
    assert "panel_height" in shell_log.read_text()
    # The shipped configuration starts in the macOS style: a dock 64 pixels tall 6 above the
    # bottom edge, and the menu bar's 28 along the top.
    check_panel(64 + 6 + 28)
    config.write_text(source.replace("enabled = true", "enabled = false"))
    panels.send_signal(signal.SIGHUP)
    assert desktop.reap(panels) == 0, shell_log.read_text()
    check_panel(0)
    for message in ("ReferenceError", "TypeError", "failed", "error in client"):
        assert message not in shell_log.read_text(), shell_log.read_text()
print("Qt desktop/panel rendering, launcher action, reservation, resize, error banner, and "
      "disable passed")
