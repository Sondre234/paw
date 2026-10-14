# SPDX-License-Identifier: GPL-3.0-or-later
"""paw doctor against a headless compositor in a world of the test's own: a private session bus
(whose only service, xdg-desktop-portal, would start on demand), a fake logind on it, a wpctl
that says what the test wrote, XDG directories and state in the test's directory, and paw run
from a prefix there, with its paw-portals.conf. First the desktop is healthy; then it breaks the
ways paw desktops have (the shell gone, another notification daemon, WirePlumber on a dead bus,
no sound output, an older compositor than the installed paw, a failed update, paw-portals.conf
out of xdg-desktop-portal's sight, ...), and the fix the doctor gives for the portal is run."""
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys

import harness

compositor, fake_login1 = (str(Path(p).resolve()) for p in sys.argv[1:3])
test_tool = shutil.which("dbus-test-tool")
if not shutil.which("dbus-daemon") or not test_tool:
    print("dbus-daemon or dbus-test-tool not found: paw doctor was not run")
    sys.exit(0)

# The default configuration it extends is the compositor's, beside its program, as the doctor
# finds it; this paw's own would be elsewhere.
CONFIG = 'return { extends = "default" }'
DEFAULT = "return { xwayland = false }"
PORTALS = """[preferred]
default=gtk
org.freedesktop.impl.portal.ScreenCast={}
"""
WPCTL = """PipeWire 'pipewire-0' [1.6.8, someone@host, cookie:1]
 └─ Clients:
        32. pipewire                            [1.6.8, someone@host, pid:1]
        34. WirePlumber                         [1.6.8, someone@host, pid:{pid}]

Audio
 ├─ Devices:
 │
 ├─ Sinks:
{sinks} │
 └─ Streams:
"""
SPEAKERS = " │  *   33. Speakers                            [vol: 1.00]\n"

with harness.Compositor(compositor, CONFIG, start=False) as desktop:
    root = desktop.root
    # A session bus whose xdg-desktop-portal would start on demand, which the doctor never asks.
    services = root / "services"
    services.mkdir()
    (services / "portal.service").write_text(
        "[D-BUS Service]\nName=org.freedesktop.portal.Desktop\nExec=/bin/false\n")
    conf = root / "bus.conf"
    conf.write_text(
        f'<busconfig><type>session</type><listen>unix:dir={root}</listen>'
        f'<servicedir>{services}</servicedir><auth>EXTERNAL</auth><policy context="default">'
        '<allow send_destination="*" eavesdrop="true"/><allow eavesdrop="true"/><allow own="*"/>'
        '</policy></busconfig>')
    desktop.bus = subprocess.Popen(["dbus-daemon", f"--config-file={conf}", "--nofork",
                                    "--print-address=1"], stdout=subprocess.PIPE, text=True)
    desktop._started.append(desktop.bus)
    address = desktop.bus.stdout.readline().strip()
    assert address.startswith("unix:"), address
    desktop.env["DBUS_SESSION_BUS_ADDRESS"] = address
    gone = f"unix:path={root}/gone"

    # paw installed in a prefix, with its paw-portals.conf.
    prefix = root / "prefix"
    paw = prefix / "bin/paw"
    paw.parent.mkdir(parents=True)
    shutil.copy2(compositor, paw)
    installed_portals = prefix / "share/xdg-desktop-portal/paw-portals.conf"
    installed_portals.parent.mkdir(parents=True)
    installed_portals.write_text(PORTALS.format("wlr"))
    default = prefix / "share/paw/init.lua"
    default.parent.mkdir(parents=True)
    default.write_text(DEFAULT)
    desktop.binary = str(paw)
    desktop.env["PAW_DEFAULT_CONFIG"] = str(default)

    # The doctor's own world.
    config_home, state = root / "config", root / "state"
    portal_directory = config_home / "xdg-desktop-portal"
    portal_directory.mkdir(parents=True)
    (portal_directory / "paw-portals.conf").symlink_to(installed_portals)
    backends = root / "portals"
    backends.mkdir()
    (backends / "wlr.portal").write_text(
        "[portal]\nDBusName=org.freedesktop.impl.portal.desktop.wlr\n"
        "Interfaces=org.freedesktop.impl.portal.Screenshot;org.freedesktop.impl.portal.ScreenCast;\n")
    stubs = root / "stubs"
    stubs.mkdir()
    wpctl = stubs / "wpctl"
    wpctl.write_text('#!/bin/sh\ncat "$(dirname "$0")/status"\nexit "$(cat "$(dirname "$0")/exit")"\n')
    wpctl.chmod(0o755)
    (stubs / "exit").write_text("0")
    (state / "paw").mkdir(parents=True)
    update_log = state / "paw/update.log"
    update_log.write_text("update-paw: 2026-10-14 15:03:52 building 0123abcdef\n"
                          f"-- Installing: {paw}\n"
                          "update-paw: 2026-10-14 15:04:14 installed 0123abcdef\n")
    fakes = root / "fakes"
    fakes.mkdir()
    for name in ("paw-shell", "mako", "xdg-desktop-portal"):
        shutil.copy2(test_tool, fakes / name)

    doctor_env = {"PATH": f"{stubs}:{os.environ.get('PATH', '/usr/bin:/bin')}",
                  "HOME": str(root / "home"), "XDG_CONFIG_HOME": str(config_home),
                  "XDG_CONFIG_DIRS": str(root / "etc"), "XDG_DATA_HOME": str(root / "data"),
                  "XDG_DATA_DIRS": str(root / "share"), "XDG_DESKTOP_PORTAL_DIR": str(backends),
                  "XDG_CURRENT_DESKTOP": "paw", "PAW_LOGIN1_BUS": address,
                  "PAW_DEFAULT_CONFIG": None}
    # A program started in that world, as xdg-desktop-portal is.
    world = {name: value for name, value in doctor_env.items() if value is not None}

    def doctor(**changes):
        """`paw doctor --plain` in the doctor's world with `changes` to its environment (None
        unsets): its exit status and, by check, (status, finding, fix)."""
        env = {**desktop.env, **doctor_env, **changes}
        env = {name: value for name, value in env.items() if value is not None}
        result = subprocess.run([compositor, "doctor", "--plain"], env=env, capture_output=True,
                                text=True, timeout=30)
        assert not result.stderr, result.stderr
        checks = {}
        for line in result.stdout.splitlines():
            status, name, finding, fix = line.split("\t")
            checks[name] = (status, finding, fix)
        return result.returncode, checks

    def waits(check, status, finding="", **changes):
        """Waits until `check` has `status` and a finding starting with `finding`, and returns
        the doctor's findings then."""
        found = {}

        def done():
            found.clear()
            found.update(doctor(**changes)[1])
            return found.get(check, ("", ""))[0] == status and \
                found[check][1].startswith(finding)

        desktop.wait_for(done, f"{check} {status}", detail=lambda: str(found.get(check)))
        return found

    def wpctl_says(pid, sinks=SPEAKERS):
        (stubs / "status").write_text(WPCTL.format(pid=pid, sinks=sinks))

    login1_log, answers = root / "login1.log", root / "answers"
    login1_log.touch()
    answers.touch()
    logind = desktop.spawn([fake_login1, address, str(login1_log), str(answers)])
    desktop.wait_for(lambda: "ready" in login1_log.read_text(), "the fake logind is ready")
    wireplumber = desktop.spawn(["sleep", "300"])
    wpctl_says(wireplumber.pid)

    desktop.start()
    pid = desktop.server.pid
    shell_pid = None
    try:
        # A healthy desktop: a "paw-shell" the compositor started serves notifications.
        desktop.msg("spawn", str(fakes / "paw-shell"), "echo",
                    "--name=org.freedesktop.Notifications")
        checks = waits("notifications", "ok")
        code, checks = doctor()
        assert code == 0, checks
        assert all(status == "ok" for status, _, _ in checks.values()), checks
        assert set(checks) == {"session", "shell", "build", "update", "config", "bus",
                               "notifications", "polkit", "logind", "pipewire", "output",
                               "desktop", "portal-config", "portal-backend", "portal",
                               "xwayland"}, checks
        assert checks["session"][1].endswith(f"answers (PID {pid})"), checks["session"]
        shell_pid = int(checks["shell"][1].split("PID ")[1].rstrip(")"))
        assert checks["shell"][1] == f"paw-shell runs (PID {shell_pid})"
        assert checks["build"][1].startswith(f"paw (PID {pid}) runs {paw}"), checks["build"]
        assert checks["update"][1] == "update-paw installed 0123abc at 2026-10-14 15:04:14"
        assert checks["config"][1].startswith(f"{desktop.config} is valid")
        assert checks["notifications"][1] == f"paw's shell shows notifications (PID {shell_pid})"
        assert "only in a session started with --session" in checks["polkit"][1], checks["polkit"]
        assert checks["logind"][1] == f"fake_login1 (PID {logind.pid}) answers (suspend: yes)"
        assert checks["pipewire"][1] == f"PipeWire and WirePlumber (PID {wireplumber.pid}) run"
        assert checks["output"][1] == "Sound plays on Speakers"
        assert checks["portal-config"][1] == (
            f"xdg-desktop-portal reads {portal_directory}/paw-portals.conf "
            f"(a link to {installed_portals})"), checks["portal-config"]
        assert checks["portal-backend"][1] == f"Screen sharing goes to wlr ({backends}/wlr.portal)"
        assert "not running yet" in checks["portal"][1]
        assert checks["xwayland"][1] == "X11 applications are off (xwayland = false)"

        # The shell gone, mako serving notifications, logind gone, WirePlumber on a dead bus and
        # PipeWire without outputs, paw installed anew under the running compositor after an
        # update that failed, a saved profile the configuration lacks, paw-portals.conf a link to
        # nothing beside a portals.conf, and xdg-desktop-portal running without paw in
        # XDG_CURRENT_DESKTOP.
        os.kill(shell_pid, signal.SIGTERM)
        waits("notifications", "warning", "Nothing shows notifications")
        mako = desktop.spawn([str(fakes / "mako"), "echo", "--name=org.freedesktop.Notifications"])
        logind.terminate()
        desktop.reap(logind)
        dead = desktop.spawn(["sleep", "300"], env={"DBUS_SESSION_BUS_ADDRESS": gone})
        wpctl_says(dead.pid, sinks="")
        shutil.copy2(compositor, paw.with_name("paw.new"))
        paw.with_name("paw.new").replace(paw)
        update_log.write_text("update-paw: 2026-10-14 16:00:00 building 4567defabc\n"
                              "FAILED: src/main.cpp.o\n")
        (state / "paw/profile").write_text("gone\n")
        (portal_directory / "paw-portals.conf").unlink()
        (portal_directory / "paw-portals.conf").symlink_to(root / "nowhere/paw-portals.conf")
        (portal_directory / "portals.conf").write_text(PORTALS.format("gnome"))
        portal = desktop.spawn([str(fakes / "xdg-desktop-portal"), "echo",
                                "--name=org.freedesktop.portal.Desktop"],
                               env={**world, "XDG_CURRENT_DESKTOP": ""})
        waits("portal", "error", XDG_CURRENT_DESKTOP=None)
        waits("notifications", "warning", "mako", XDG_CURRENT_DESKTOP=None)
        code, checks = doctor(XDG_CURRENT_DESKTOP=None)
        assert code == 1, checks
        status = {name: check[0] for name, check in checks.items()}
        assert status == {"session": "ok", "shell": "error", "build": "warning",
                          "update": "warning", "config": "ok", "profile": "warning", "bus": "ok",
                          "notifications": "warning", "polkit": "ok", "logind": "error",
                          "pipewire": "error", "output": "error", "desktop": "error",
                          "portal-config": "error", "portal": "error",
                          "xwayland": "ok"}, checks
        assert checks["shell"][1].startswith("The desktop shell is not running")
        assert checks["shell"][2] == f"kill -HUP {pid}", checks["shell"]
        assert checks["build"][1].startswith(f"paw (PID {pid}) runs an older build than {paw}")
        assert checks["build"][2] == "Log out and back in to start the new build"
        assert checks["update"][1].startswith("update-paw could not build or install 4567def "
                                              "(2026-10-14 16:00:00), so paw stays at the build")
        assert checks["update"][2].startswith(f"tail -n 30 {update_log}"), checks["update"]
        assert checks["profile"][2] == f"rm {state}/paw/profile", checks["profile"]
        assert checks["notifications"][1].startswith(
            f"mako (PID {mako.pid}) shows notifications instead of paw's shell")
        assert checks["notifications"][2] == (f"kill {mako.pid} && kill -HUP {pid}; Take mako out "
                                              "of your autostart and startup commands"), \
            checks["notifications"]
        assert checks["logind"][1].startswith("logind is not running"), checks["logind"]
        assert checks["pipewire"][1].startswith(
            f"WirePlumber (PID {dead.pid}) is on a session bus that is gone ({gone})")
        assert checks["pipewire"][2], "a way to restart PipeWire"
        assert checks["output"][1].startswith("PipeWire has no sound output")
        assert checks["portal-config"][1].startswith(
            f"xdg-desktop-portal reads {portal_directory}/portals.conf, not paw-portals.conf")
        assert (f"{portal_directory}/paw-portals.conf links to {root}/nowhere/paw-portals.conf, "
                "which is gone") in checks["portal-config"][1], checks["portal-config"]
        fix = checks["portal-config"][2]
        assert fix == (f"mkdir -p {portal_directory} && ln -sf {installed_portals} "
                       f"{portal_directory}/paw-portals.conf && kill {portal.pid}"), fix
        assert checks["portal"][1].startswith(
            f"xdg-desktop-portal (PID {portal.pid}) started with XDG_CURRENT_DESKTOP unset")

        # The doctor's fix for the portal's configuration, which ends the portal too; then one
        # started for paw, but with no ScreenCast to offer, and paw-portals.conf naming a backend
        # that is not installed.
        subprocess.run(["sh", "-c", fix], check=True)
        desktop.reap(portal)
        installed_portals.write_text(PORTALS.format("missing"))
        checks = waits("portal", "ok", "xdg-desktop-portal is not running yet")
        assert checks["portal-config"][0] == "ok", checks["portal-config"]
        assert checks["portal-backend"] == (
            "error", f"{portal_directory}/paw-portals.conf sends screen sharing to missing, which "
            "is not installed", "Install xdg-desktop-portal-missing"), checks["portal-backend"]
        portal = desktop.spawn([str(fakes / "xdg-desktop-portal"), "echo",
                                "--name=org.freedesktop.portal.Desktop"], env=world)
        checks = waits("portal", "error")
        assert checks["portal"][1].startswith(
            f"xdg-desktop-portal (PID {portal.pid}) offers no screen sharing"), checks["portal"]
        assert checks["portal"][2] == f"kill {portal.pid}"

        # A configuration with an error, a session bus that is gone, PipeWire not running.
        desktop.config.write_text("return { xwayland = 1 }")
        (stubs / "status").write_text("Could not connect to PipeWire\n")
        (stubs / "exit").write_text("1")
        code, checks = doctor(DBUS_SESSION_BUS_ADDRESS=gone)
        assert code == 1
        assert checks["config"][0] == "error" and "xwayland" in checks["config"][1], checks
        assert checks["config"][2] == (
            f"Fix that, then check it with: paw --check-config --config {desktop.config}")
        assert checks["bus"][0] == "error" and gone in checks["bus"][1], checks["bus"]
        assert "notifications" not in checks and "portal" not in checks, checks
        assert checks["pipewire"][1] == (
            "PipeWire does not answer (wpctl: Could not connect to PipeWire): no sound")
        assert "output" not in checks
        desktop.config.write_text(CONFIG)

        # Outside the session: the test's compositor is not one with --session, though a
        # developer's own session may be.
        code, checks = doctor(PAW_SOCKET=None)
        assert checks["session"][0] != "ok", checks["session"]
        assert "PAW_SOCKET is unset" in checks["session"][1], checks["session"]
    finally:
        if shell_pid is not None:
            try:
                os.kill(shell_pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
