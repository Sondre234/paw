# SPDX-License-Identifier: GPL-3.0-or-later
"""`paw rice`: pack packs a profile's look, try installs a rice and switches the session to it,
the time running out takes it back, keep keeps it, revert takes it back at once (and puts back an
installed rice of its name), a compositor ending or dying during a trial starts again in the
previous look, and a rice that would run code, reach outside itself or set more than the look is
refused with nothing changed."""
import io
from pathlib import Path
import re
import subprocess
import sys
import tarfile

import harness

compositor = str(Path(sys.argv[1]).resolve())

CONFIG = """return {
    xwayland = false,
    outputs = { monitors = { ["HEADLESS-1"] = { mode = "1280x720" } } },
    profile = "dark",
    profiles = {
        dark = { shell = { accent = "#112233" } },
        light = { appearance = { background = "#f2f4f8" },
                  shell = { accent = "#3366cc", wallpaper = "day.png",
                            tiling_bar = { workspace_icons = { "ws1.png", "two.svg" } },
                            launchers = { { name = "x", command = { "true" } } } } },
    },
}"""


def rice_lua(name="washi", accent="#e0607e", description="Sakura paper.", extra=""):
    return f"""return {{
    format = 1, name = "{name}", author = "someone", description = "{description}",
    profile = {{
        appearance = {{ background = "#f6eef0" }},
        shell = {{ accent = "{accent}", wallpaper = "wallpaper.png" }},
        {extra}
    }},
}}"""


def tar_of(entries):
    """A tar archive of (TarInfo, bytes or None) pairs, made as an archive meant to harm would."""
    data = io.BytesIO()
    with tarfile.open(fileobj=data, mode="w", format=tarfile.USTAR_FORMAT) as archive:
        for info, contents in entries:
            if contents is not None:
                info.size = len(contents)
                archive.addfile(info, io.BytesIO(contents))
            else:
                archive.addfile(info)
    return data.getvalue()


with harness.Compositor(compositor, CONFIG, start=False,
                        env={"PAW_TEST_RICE_TRIAL_MS": "60000"}) as desktop:
    root = desktop.root
    state = root / "state" / "paw"
    rices = state / "rices"
    saved = state / "profile"
    desktop.env["XDG_CONFIG_HOME"] = str(root / "config-home")
    (root / "day.png").write_bytes(b"\x89PNG day")
    (root / "ws1.png").write_bytes(b"\x89PNG one")
    washi = root / "washi"
    washi.mkdir()
    (washi / "rice.lua").write_text(rice_lua())
    (washi / "wallpaper.png").write_bytes(b"\x89PNG washi")
    desktop.start()
    msg = desktop.msg
    events = desktop.subscribe(shell=True)

    def rice(*words, ok=True, env=None):
        result = subprocess.run([compositor, "rice", *words, "--config", str(desktop.config)],
                                env={**desktop.env, **(env or {})}, capture_output=True,
                                text=True, timeout=60)
        assert (result.returncode == 0) == ok, (words, result.stdout, result.stderr)
        return result.stdout if ok else result.stderr

    def profile():
        found = re.findall(r"Configuration reloaded: .*?(?: \(profile (\S+)\))?$",
                           desktop.log.read_text(), re.M)
        return found[-1] if found else None

    def trial():
        return msg("get", "rice_trial").strip()

    def installed():
        return sorted(p.name for p in rices.iterdir()) if rices.exists() else []

    desktop.detail = lambda: f"trial {trial()}, profile {profile()}, rices {installed()}"

    # Packing takes the profile's look, its wallpaper with it, and leaves the rest out: the
    # tiling bar's workspace pictures too while one of them is missing, all or none.
    packed = root / "light.rice.tar"
    out = rice("pack", "light", "-o", str(packed))
    assert "shell.tiling_bar.workspace_icons" in out and "two.svg" in out, out
    with tarfile.open(packed) as archive:
        assert not [name for name in archive.getnames() if "workspaces" in name]
        assert "workspace_icons" not in archive.extractfile("light/rice.lua").read().decode()
    (root / "two.svg").write_bytes(b"<svg/>")
    out = rice("pack", "light", "-o", str(packed), "--description", "Daylight.")
    assert "wallpaper.png" in out and "shell.launchers" in out, out
    with tarfile.open(packed) as archive:
        names = archive.getnames()
        manifest = archive.extractfile("light/rice.lua").read().decode()
    assert "light/wallpaper.png" in names, names
    assert "light/workspaces/1.png" in names and "light/workspaces/2.svg" in names, names
    assert '"workspaces/1.png"' in manifest and '"workspaces/2.svg"' in manifest, manifest
    assert 'accent = "#3366cc"' in manifest and "launchers" not in manifest, manifest
    assert 'description = "Daylight."' in manifest, manifest

    # Tried, it is installed as a profile and switched to, and it goes back by itself in time.
    out = rice("try", str(washi))
    assert "shell.accent" in out and '"#112233" -> "#e0607e"' in out, out
    desktop.wait_for(lambda: trial().startswith("washi\t"), "washi on trial")
    assert profile() == "washi", profile()
    assert saved.read_text() == "washi\n"
    assert installed() == ["washi"], installed()
    assert events.last("rice-trial ") == "rice-trial 60000 washi", events.lines("rice-")
    assert "is on trial" in rice("try", str(washi), ok=False)
    desktop.stop()  # a compositor ending during a trial takes it back as it goes
    assert not saved.exists() and installed() == [], installed()
    assert not (state / "rice-trial").exists()

    desktop.env["PAW_TEST_RICE_TRIAL_MS"] = "1500"
    desktop.start()
    events = desktop.subscribe(shell=True)
    rice("try", str(washi))
    desktop.wait_for(lambda: trial() == "-", "the trial ran out", timeout=20)
    desktop.wait_for(lambda: profile() == "dark", "back to dark")
    assert not saved.exists() and installed() == [], installed()
    desktop.wait_for(lambda: events.last("rice-reverted ") == "rice-reverted timeout",
                     "the shell told why")

    # Kept, it stays: a profile like the others.
    desktop.env["PAW_TEST_RICE_TRIAL_MS"] = "60000"
    desktop.stop()
    desktop.start()
    events = desktop.subscribe(shell=True)
    rice("try", str(washi))
    assert "Kept washi" in rice("keep")
    desktop.wait_for(lambda: events.last("rice-kept ") == "rice-kept washi", "rice-kept")
    assert trial() == "-" and profile() == "washi" and installed() == ["washi"]
    assert "washi, by someone [in use]" in rice("list"), rice("list")
    assert "no rice is on trial" in rice("keep", ok=False)
    assert "in use" in rice("remove", "washi", ok=False)

    # Tried over the installed one, which waits aside and comes back on revert.
    msg("profile", "dark")
    desktop.wait_for(lambda: profile() == "dark", "dark again")
    (washi / "rice.lua").write_text(rice_lua(accent="#00aa00", description="Greener."))
    out = rice("try", str(washi))
    assert '"#112233" -> "#00aa00"' in out, out
    assert installed() == [".washi.previous", "washi"], installed()
    assert "Took washi back" in rice("revert")
    desktop.wait_for(lambda: profile() == "dark", "back to dark")
    assert saved.read_text() == "dark\n"
    assert installed() == ["washi"] and "Sakura" in (rices / "washi/rice.lua").read_text()
    assert events.last("rice-reverted ") == "rice-reverted asked", events.lines("rice-")

    # A rice named as a profile of the configuration's is installed beside it.
    out = rice("try", str(packed))
    assert "Trying light-2" in out, out
    assert (rices / "light-2/wallpaper.png").read_bytes() == b"\x89PNG day"
    assert (rices / "light-2/workspaces/1.png").read_bytes() == b"\x89PNG one"
    rice("revert")
    desktop.wait_for(lambda: trial() == "-" and profile() == "dark", "light-2 reverted")

    # A compositor that dies during a trial: the next one starts in the previous look.
    rice("try", str(washi))
    assert profile() == "washi"
    desktop.server.kill()
    desktop.server.wait()
    desktop.server = None
    assert (state / "rice-trial").exists()
    desktop.start()
    assert "was on trial as the last session ended" in desktop.log.read_text()
    msg("reload")
    desktop.wait_for(lambda: profile() == "dark", "dark after the crash")
    assert saved.read_text() == "dark\n" and installed() == ["washi"], installed()
    assert "Sakura" in (rices / "washi/rice.lua").read_text(), "the kept one is back"
    assert not (state / "rice-trial").exists()

    # Refused, with nothing changed: code, files outside, more than the look, a bad value.
    def refused(source, why):
        before = installed()
        error = rice("try", str(source), ok=False)
        assert why in error, (why, error)
        assert installed() == before and trial() == "-", (installed(), trial())
        assert saved.read_text() == "dark\n" and not (state / "rice-trial").exists()

    evil = root / "evil"
    evil.mkdir()
    (evil / "wallpaper.png").write_bytes(b"x")
    marker = root / "ran"
    (evil / "rice.lua").write_text(f'os.execute("touch {marker}") return {{ format = 1 }}')
    refused(evil, "rice.lua is one table")
    (evil / "rice.lua").write_text(
        f'return {{ format = 1, name = "evil", profile = {{}}, x = io.open("{marker}", "w") }}')
    refused(evil, "is a name")
    assert not marker.exists(), "nothing in rice.lua ran"
    (evil / "rice.lua").write_text(rice_lua(
        name="evil", extra='windows = { rules = { { app_id = ".", opacity = 0 } } },'))
    refused(evil, "cannot set windows.rules")
    (evil / "rice.lua").write_text(
        'return { format = 1, name = "evil", profile = {}, extras = { "../../.bashrc" } }')
    refused(evil, "an extra must name a file inside the rice")
    (evil / "rice.lua").write_text(rice_lua(name="evil").replace(
        '"wallpaper.png"', '"/etc/passwd.png"'))
    refused(evil, "inside the rice")
    (evil / "rice.lua").write_text(rice_lua(name="evil"))
    (evil / "wallpaper.png").unlink()
    (evil / "wallpaper.png").symlink_to("/etc/passwd")
    refused(evil, "is a link")
    (evil / "wallpaper.png").unlink()
    (evil / "wallpaper.png").write_bytes(b"x")
    (evil / "rice.lua").write_text(rice_lua(name="evil", accent="blue"))
    refused(evil, "accent must be")

    manifest = rice_lua(name="evil").encode()

    def member(name, kind=tarfile.REGTYPE, target=""):
        info = tarfile.TarInfo(name)
        info.type = kind
        info.linkname = target
        return info

    for entries, why in [
            ([(member("evil/rice.lua"), manifest), (member("evil/../../escape"), b"x")],
             "a path out of it"),
            ([(member("evil/rice.lua"), manifest), (member("/tmp/paw-rice-escape"), b"x")],
             "an absolute path"),
            ([(member("evil/rice.lua"), manifest),
              (member("evil/wallpaper.png", tarfile.SYMTYPE, "/etc/passwd"), None)], "is a link"),
            ([(member("evil/rice.lua"), manifest),
              (member("evil/wallpaper.png", tarfile.LNKTYPE, "evil/rice.lua"), None)],
             "is a link"),
            ([(member("evil/rice.lua"), manifest),
              (member("evil/wallpaper.png", tarfile.CHRTYPE), None)], "not a plain file"),
            ([(member("evil/rice.lua"), manifest)], "has no wallpaper.png")]:
        archive = root / "evil.tar"
        archive.write_bytes(tar_of(entries))
        refused(archive, why)
    assert not (root.parent / "escape").exists() and not Path("/tmp/paw-rice-escape").exists()
    refused("http://example.org/washi.tar", "https alone")
    error = rice("try", str(washi), ok=False, env={"PAW_SOCKET": ""})
    assert "PAW_SOCKET" in error, error
    assert installed() == ["washi"] and saved.read_text() == "dark\n"

    # Removed once it is not in use.
    assert "Removed washi" in rice("remove", "washi")
    assert installed() == [] and "No rices are installed" in rice("list")
print("rices were packed, tried, kept, taken back and refused")
