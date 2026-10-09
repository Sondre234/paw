# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise a real compositor in an isolated headless runtime directory. Given the shell's
task_model_test (as shell_tasks), it runs that against the compositor instead."""
import os
from pathlib import Path
import signal
import subprocess
import sys

import harness

compositor, probe, example = map(lambda p: str(Path(p).resolve()), sys.argv[1:4])
task_model_test = str(Path(sys.argv[4]).resolve()) if len(sys.argv) > 4 else None

if not task_model_test:
    # Reject standalone mode inside a GUI before attempting device/session access.
    for arguments, expected in [
        (["--session"], "outside an existing graphical session"),
        (["--session", "--headless"], "choose only one backend"),
    ]:
        result = subprocess.run([compositor, "--config", example, *arguments],
                                env=dict(os.environ, WAYLAND_DISPLAY="test-parent"),
                                capture_output=True, text=True, timeout=30)
        assert result.returncode != 0 and expected in result.stderr, result.stderr

with harness.Compositor(compositor, Path(example).read_text()) as desktop:
    env = desktop.env
    if task_model_test:
        subprocess.run([task_model_test, probe], env=env, check=True, timeout=15)
    else:
        # Browsers and Electron apps expect these beyond the core desktop protocols.
        advertised = set(subprocess.run([probe, "--globals"], env=env, check=True,
                                        capture_output=True, text=True,
                                        timeout=30).stdout.split())
        expected = {"wp_viewporter", "wp_fractional_scale_manager_v1", "zxdg_output_manager_v1",
                    "wp_presentation", "zwp_primary_selection_device_manager_v1",
                    "zwlr_data_control_manager_v1", "ext_data_control_manager_v1",
                    "xdg_activation_v1", "zwp_relative_pointer_manager_v1",
                    "zwp_pointer_constraints_v1", "wp_single_pixel_buffer_manager_v1",
                    "zxdg_exporter_v2", "zxdg_importer_v2", "xdg_wm_dialog_v1",
                    "zwlr_gamma_control_manager_v1", "zwlr_screencopy_manager_v1",
                    "zwlr_export_dmabuf_manager_v1", "ext_image_copy_capture_manager_v1",
                    "ext_output_image_capture_source_manager_v1",
                    "ext_foreign_toplevel_list_v1", "zxdg_decoration_manager_v1",
                    "ext_foreign_toplevel_image_capture_source_manager_v1",
                    "zwp_virtual_keyboard_manager_v1", "zwlr_virtual_pointer_manager_v1",
                    "xdg_toplevel_icon_manager_v1"}
        assert expected <= advertised, f"missing globals: {sorted(expected - advertised)}"
        for _ in range(3):
            subprocess.run([probe], env=env, check=True, timeout=30)
        desktop.config.write_text("return {appearance={background='#315071'}, layout={gap=12}}")
        desktop.server.send_signal(signal.SIGHUP)
        desktop.wait_for(lambda: "Configuration reloaded" in desktop.log.read_text(),
                         "valid reload")
        # --check-config must reject invalid data without starting a display.
        broken = desktop.root / "broken.lua"
        broken.write_text("return { layout = {gap = -1} }")
        result = subprocess.run([compositor, "--config", str(broken), "--check-config"],
                                env=env, capture_output=True, text=True, timeout=30)
        assert result.returncode != 0 and "gap" in result.stderr
    # SIGTERM: the compositor must exit 0 and remove its Wayland socket.
    desktop.stop()
print("Headless clients, reload, --check-config and clean shutdown passed")
