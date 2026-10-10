#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Render every popup of the taskbar and every overlay surface as a PNG, in a light and a dark
theme of each style (the taskbar and macOS).

usage: tools/shell_gallery.py BUILD_DIR OUT_DIR [--renderer software|gpu|both]
                              [--theme light|dark|macos-light|macos-dark] [--popup NAME] [--scale FACTOR]
                              [--icon-theme NAME] [--jobs N] [--wait MS] [--no-animations]

Each picture is `paw-shell --preview-popup NAME --screenshot`: the taskbar with that popup
open, or with that overlay (the on-screen display, the cards, the switcher, ...) over it, on
stand-in windows, sound, tray items and notifications, over a wallpaper made for the purpose;
"desktop" is the desktop alone (`--preview-desktop`), without that wallpaper. They are written as OUT_DIR/THEME-NAME.png, and OUT_DIR/THEME-NAME-gpu.png for the GPU
renderer. The software renderer runs offscreen; the GPU one (Qt's OpenGL, on Mesa's software
implementation here) needs a display, so it runs against a private headless compositor from
BUILD_DIR. Nothing touches a real session: no display, session bus, configuration or state of
the user's is used, only the icon theme (named in GTK's settings, or by --icon-theme).

Exits with 1, after writing what it could, when a popup failed to render or the shell printed a
QML warning while rendering it. Each picture is taken 400 ms after its popup opens (600 on the
GPU), or --wait's; with --no-animations, which the test takes, nothing is still moving by then
however short it is.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import zlib

# The names previewPopup in shell/Panel.qml knows.
POPUPS = ["bar", "launcher", "power", "bar-menu", "bar-submenu", "task-menu", "stack-menu",
          "pin-menu", "group", "thumbnails", "keyboard", "tray-menu", "tray-submenu", "calendar",
          "mixer", "outputs", "profiles", "wallpapers", "notifications", "clock-empty",
          "calendar-years"]
# The overlay surfaces, each shown over the bar alone (PreviewData::surfaces in shell/preview.cpp).
POPUPS += ["osd-volume", "osd-text", "cards", "power-dialog", "palette", "switcher",
           "overview", "palette-empty", "auth-dialog", "snap-assist", "osd-microphone",
           "palette-calculator", "palette-files", "clipboard", "emoji"]
# The start menu's other views and its menus.
POPUPS += ["display-mode", "display-settings", "display-settings-trial"]
POPUPS += ["launcher-all", "launcher-search", "launcher-empty", "launcher-menu"]
POPUPS += ["quick-settings", "quick-settings-mixer", "bar-all"]
POPUPS += ["quick-settings-power", "quick-settings-wifi", "wifi"]
POPUPS += ["quick-settings-bluetooth", "quick-settings-pairing"]
# The menus of the macOS style's menu bar, which only its themes picture.
MACOS_POPUPS = ["system-menu", "app-menu", "window-menu", "window-submenu"]
POPUPS += MACOS_POPUPS
# The desktop alone, without the wallpaper made for the gallery: the style's own background.
POPUPS += ["desktop"]
# The switcher's icon grid, which the taskbar style shows without pictures of the windows; the
# macOS style always does, as its "switcher".
TASKBAR_POPUPS = ["switcher-icons"]
POPUPS += TASKBAR_POPUPS

# Pictures taken with settings of their own, put in the shell table: name -> (popup, settings).
# The volume's and the profiles' popups belong to buttons Quick Settings holds by default, and a
# stack's list, like the switcher's grid of icons, shows on the taskbar only without pictures of
# its windows.
VARIANTS = {
    "group": ("group", "thumbnails = { enabled = false },"),
    "switcher-icons": ("switcher", "thumbnails = { enabled = false },"),
    "mixer": ("mixer", 'widgets = { volume = "bar" },'),
    "outputs": ("outputs", 'widgets = { volume = "bar" },'),
    "profiles": ("profiles", 'widgets = { profiles = "bar" },'),
    "wifi": ("wifi", 'widgets = { network = "bar" },'),
    "bar-all": ("bar", 'widgets = { network = "bar", battery = "bar", volume = "bar", '
                       'tiling = "bar", profiles = "bar", notifications = "bar" },'),
}

# Translucent bars, as appearance profiles often have them; `shell` is more of the profile's
# shell table, as Lua.
THEMES = {
    "light": {"panel_color": "#f3f2fbf2", "text_color": "#141a48", "accent": "#4a64dc",
              "background": "#c3cde4"},
    "dark": {"panel_color": "#0c1131c7", "text_color": "#f2f4ff", "accent": "#7f9bff",
             "background": "#1b2238"},
    # The macOS style, as config/init.lua's macos-light and macos-dark profiles have it but for
    # their widgets' places, which would override the variants'.
    "macos-light": {"panel_color": "#f6f6f8bf", "text_color": "#1d1d1f", "accent": "#007aff",
                    "background": "#9dbbe6",
                    "shell": 'style = "macos", font_size = 13, panel_height = 64, '
                             'panel_margin = { bottom = 6 }, panel_radius = 20'},
    "macos-dark": {"panel_color": "#232326bf", "text_color": "#f5f5f7", "accent": "#0a84ff",
                   "background": "#1b2440",
                   "shell": 'style = "macos", font_size = 13, panel_height = 64, '
                            'panel_margin = { bottom = 6 }, panel_radius = 20'},
}

# Installed applications for the launcher: desktop id, name, icon.
APPS = [("firefox", "Firefox", "firefox"), ("org.kde.dolphin", "Dolphin", "system-file-manager"),
        ("foot", "Foot", "foot"), ("kitty", "kitty", "kitty"),
        ("org.gnome.Calculator", "Calculator", "accessories-calculator"),
        ("gimp", "GNU Image Manipulation Program", "gimp"),
        ("thunderbird", "Thunderbird", "thunderbird"), ("mpv", "mpv Media Player", "mpv"),
        ("org.kde.kate", "Kate", "kate"), ("steam", "Steam", "steam")]

# Their desktop actions ([Desktop Action …]), for menus that offer them: desktop id, then each
# action's id, name and icon.
ACTIONS = {
    "firefox": [("new-window", "New Window", "window-new"),
                ("new-private-window", "New Private Window", "view-private")],
    "foot": [("server", "Foot Server", "")],
    "thunderbird": [("compose", "Write New Message", "mail-message-new"),
                    ("contacts", "Open Address Book", "x-office-address-book")],
}

# More of them, so that the start menu's pages of pins and its list from A to Z look as on a
# desktop in use (the preview pins some of them).
APPS += [("code", "Visual Studio Code", "code"), ("libreoffice-writer", "LibreOffice Writer",
                                                  "libreoffice-writer"),
         ("libreoffice-calc", "LibreOffice Calc", "libreoffice-calc"),
         ("spotify", "Spotify", "spotify"), ("obsidian", "Obsidian", "obsidian"),
         ("systemsettings", "System Settings", "preferences-system"),
         ("discord", "Discord", "discord"), ("inkscape", "Inkscape", "inkscape"),
         ("keepassxc", "KeePassXC", "keepassxc"), ("obs", "OBS Studio", "obs"),
         ("blender", "Blender", "blender"), ("krita", "Krita", "krita"),
         ("org.kde.okular", "Okular", "okular"), ("org.kde.gwenview", "Gwenview", "gwenview"),
         ("chromium", "Chromium", "chromium"), ("vlc", "VLC media player", "vlc"),
         ("signal-desktop", "Signal", "signal-desktop"), ("htop", "htop", "htop"),
         ("org.gnome.SystemMonitor", "System Monitor", "org.gnome.SystemMonitor")]

# More still in the macOS themes, so that Launchpad has a second page to show.
LAUNCHPAD_APPS = [("audacity", "Audacity", "audacity"), ("brave-browser", "Brave", "brave-browser"),
                  ("calibre", "calibre", "calibre"), ("darktable", "darktable", "darktable"),
                  ("evince", "Document Viewer", "evince"), ("gnome-calendar", "Calendar", "gnome-calendar"),
                  ("org.gnome.Maps", "Maps", "org.gnome.Maps"),
                  ("org.gnome.Weather", "Weather", "org.gnome.Weather"),
                  ("org.gnome.clocks", "Clocks", "org.gnome.clocks"), ("kdenlive", "Kdenlive", "kdenlive"),
                  ("rhythmbox", "Rhythmbox", "rhythmbox"), ("shotwell", "Shotwell", "shotwell"),
                  ("telegram", "Telegram", "telegram"), ("transmission", "Transmission", "transmission"),
                  ("virt-manager", "Virtual Machine Manager", "virt-manager"),
                  ("wireshark", "Wireshark", "wireshark")]

# What a search finds some of them by besides their names: desktop id, then the generic name,
# comment and keywords.
DETAILS = {
    "firefox": ("Web Browser", "Browse the World Wide Web", "Internet;WWW;Browser;Web;Explorer;"),
    "chromium": ("Web Browser", "Access the Internet", "browser;web;"),
    "foot": ("Terminal", "A Wayland native terminal emulator", "shell;prompt;command;commandline;"),
    "kitty": ("Terminal emulator", "Fast, feature-rich, GPU based terminal", "shell;prompt;command;"),
    "org.kde.dolphin": ("File Manager", "Browse and manage your files", "files;folders;explorer;"),
    "thunderbird": ("Mail Client", "Send and receive mail with Thunderbird", "Email;E-mail;Calendar;"),
    "code": ("Text Editor", "Code Editing. Redefined.", "vscode;ide;"),
    "org.kde.kate": ("Advanced Text Editor", "Edit text files", "text;editor;"),
    "gimp": ("Image Editor", "Create images and edit photographs", "photo;paint;"),
    "libreoffice-writer": ("Word Processor", "Create and edit text and graphics in letters, "
                           "reports, documents and Web pages", "Text;Letter;Fax;Document;"),
    "libreoffice-calc": ("Spreadsheet", "Perform calculations, analyze information and manage "
                         "lists in spreadsheets", "Accounting;Stats;Spreadsheet;"),
    "systemsettings": ("System Settings", "Configure the system", "settings;preferences;"),
    "org.gnome.Calculator": ("Calculator", "Perform arithmetic, scientific or financial "
                             "calculations", "calculation;arithmetic;scientific;"),
    "mpv": ("Multimedia player", "Play movies and songs", "mpv;media;player;video;audio;"),
    "vlc": ("Media player", "Read, capture, broadcast your multimedia streams", "Player;Video;"),
    "steam": ("Game Store", "Application for managing and playing games on Steam", "Games;"),
}

# A line of the shell's output that is a QML warning or error.
QML_WARNING = re.compile(r"\.qml:\d+|ReferenceError|TypeError|Binding loop|QQmlComponent")


def shows(theme_name, popup):
    """Whether a theme's style has the popup."""
    macos = 'style = "macos"' in THEMES[theme_name].get("shell", "")
    return (popup not in MACOS_POPUPS or macos) and (popup not in TASKBAR_POPUPS or not macos)


def png(path, width, height, pixel):
    """Writes an RGB PNG whose pixel at (x, y) is pixel(x, y), a tuple of three bytes."""
    rows = b"".join(b"\0" + bytes(c for x in range(width) for c in pixel(x, y))
                    for y in range(height))

    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data
                + struct.pack(">I", zlib.crc32(kind + data)))

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"\x89PNG\r\n\x1a\n"
                     + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


def rgb(color):
    return tuple(int(color[i:i + 2], 16) for i in (1, 3, 5))


def wallpaper(path, theme):
    """A gradient in the theme's background with light and dark bands across it, so that a
    translucent colour over it shows."""
    top, bottom = rgb(theme["background"]), (40, 60, 110)

    def pixel(x, y):
        t = y / 359
        base = [round(a + (b - a) * t) for a, b in zip(top, bottom)]
        band = (x + 2 * y) // 90 % 3
        shade = 1.25 if band == 0 else 0.7 if band == 1 else 1
        return tuple(min(255, round(c * shade)) for c in base)

    png(path, 640, 360, pixel)


def config(root, theme_name, animations):
    """The shell's configuration: both themes as profiles, this one in use."""
    profiles = ",\n".join(
        f'        ["{name}"] = {{ appearance = {{ background = "{t["background"]}" }}, '
        f'shell = {{ panel_color = "{t["panel_color"]}", text_color = "{t["text_color"]}", '
        f'accent = "{t["accent"]}", {t.get("shell", "")} }} }}' for name, t in THEMES.items())
    return f"""return {{
    profile = "{theme_name}",
    animations = {{ enabled = {str(animations).lower()} }},
    profiles = {{
{profiles},
    }},
    layout = {{ workspace_names = {{ "web", "code", "", "mail" }} }},
    shell = {{
        wallpaper = [[{root / "wallpaper.png"}]],
        wallpapers = [[{root / "wallpapers"}]],
        launchers = {{
            {{ name = "Files", command = {{ "dolphin" }}, icon = "system-file-manager" }},
        }},
    }},
}}
"""


def icon_theme():
    """The icon theme the desktop uses, as GTK's settings name it, else hicolor."""
    config = Path(os.environ.get("XDG_CONFIG_HOME") or Path.home() / ".config")
    for version in ("gtk-4.0", "gtk-3.0"):
        try:
            for line in (config / version / "settings.ini").read_text().splitlines():
                name, _, value = line.partition("=")
                if name.strip() == "gtk-icon-theme-name" and value.strip():
                    return value.strip()
        except OSError:
            pass
    return "hicolor"


def prepare(root, theme_name, animations=True):
    """Writes the configuration, pictures and applications of a theme's runs under `root`, and
    returns the environment the shell runs in."""
    root.mkdir(parents=True)
    theme = THEMES[theme_name]
    (root / "init.lua").write_text(config(root, theme_name, animations))
    wallpaper(root / "wallpaper.png", theme)
    for folder, name, color in (("nature", "forest", "#2f6b3a"), ("nature", "lake", "#2d5f8a"),
                                ("nature", "dunes", "#c49a5a"), ("abstract", "dusk", "#6a3d8f"),
                                ("abstract", "ember", "#b5482f"), ("abstract", "slate", "#4a5260")):
        png(root / "wallpapers" / folder / f"{name}.png", 64, 36,
            lambda x, y, c=rgb(color): tuple(min(255, v + x + y) for v in c))
    data = root / "data"
    (data / "applications").mkdir(parents=True)
    macos = 'style = "macos"' in theme.get("shell", "")
    for desktop_id, name, icon in APPS + (LAUNCHPAD_APPS if macos else []):
        actions = ACTIONS.get(desktop_id, [])
        entry = f"[Desktop Entry]\nType=Application\nName={name}\nIcon={icon}\nExec=true\n"
        if desktop_id in DETAILS:
            entry += "GenericName={}\nComment={}\nKeywords={}\n".format(*DETAILS[desktop_id])
        if actions:
            entry += "Actions=" + "".join(f"{action};" for action, _, _ in actions) + "\n"
        for action, title, action_icon in actions:
            entry += f"\n[Desktop Action {action}]\nName={title}\nExec=true\n"
            entry += f"Icon={action_icon}\n" if action_icon else ""
        (data / "applications" / f"{desktop_id}.desktop").write_text(entry)
    # One of them pinned from the shell, without a window, so that pin-menu has actions to show.
    (root / "state" / "paw").mkdir(parents=True)
    (root / "state" / "paw" / "pinned").write_text("thunderbird.desktop\n")
    # The applications are the ones above, but the icons are the desktop's: the user's and the
    # system's icon folders are linked into the private data folders.
    home = Path(os.environ.get("XDG_DATA_HOME") or Path.home() / ".local/share")
    if (home / "icons").is_dir():
        (data / "icons").symlink_to(home / "icons")
    systems = []
    for i, folder in enumerate(os.environ.get("XDG_DATA_DIRS", "/usr/local/share:/usr/share")
                               .split(":")):
        if folder and (Path(folder) / "icons").is_dir():
            system = root / f"system{i}"
            system.mkdir()
            (system / "icons").symlink_to(Path(folder) / "icons")
            systems.append(str(system))
    runtime = root / "runtime"
    runtime.mkdir(mode=0o700)
    (root / "config").mkdir()
    env = {name: value for name, value in os.environ.items()
           if name not in ("WAYLAND_DISPLAY", "DISPLAY", "PAW_SOCKET", "QT_QUICK_BACKEND",
                           "QSG_RHI_BACKEND", "QT_SCALE_FACTOR", "WLR_RENDERER")}
    env.update(XDG_RUNTIME_DIR=str(runtime), XDG_CONFIG_HOME=str(root / "config"),
               XDG_DATA_HOME=str(data), XDG_DATA_DIRS=":".join(systems) or str(root / "none"),
               XDG_STATE_HOME=str(root / "state"), XDG_CACHE_HOME=str(root / "cache"),
               DBUS_SESSION_BUS_ADDRESS="disabled:", QT_FORCE_STDERR_LOGGING="1")
    return env


def render(shell, env, root, popup, out, wait, icons):
    """Renders one popup to `out`; returns a list of what went wrong."""
    config = root / "init.lua"
    if popup in VARIANTS:
        # A configuration of the picture's own, beside the theme's, for this run alone.
        popup, settings = VARIANTS[popup]
        config = root / f"init-{out.stem}.lua"
        config.write_text((root / "init.lua").read_text().replace(
            "    shell = {\n", "    shell = {\n        " + settings + "\n", 1))
    shown = ["--preview-popup", popup]
    if popup == "desktop":
        config = root / f"init-{out.stem}.lua"
        config.write_text("".join(line for line in (root / "init.lua").read_text().splitlines(True)
                                  if not line.strip().startswith("wallpaper = ")))
        shown = ["--preview", "--preview-desktop"]
    result = subprocess.run([shell, "--config", str(config), *shown,
                             "--icon-theme", icons, "--quit-after", str(wait),
                             "--screenshot", str(out)],
                            env=env, capture_output=True, text=True, timeout=60)
    lines = [line.strip() for line in result.stderr.splitlines() if line.strip()]
    # Qt prints a QML error with the line it is on and again on its own; each is listed once.
    problems = list(dict.fromkeys(line for line in lines if QML_WARNING.search(line)))
    drawn = "drawn on the GPU" if env.get("QT_QPA_PLATFORM") == "wayland" else "drawn in software"
    if result.returncode != 0:
        problems += [f"exit code {result.returncode}"] + [l for l in lines[-2:] if l not in problems]
    elif drawn not in result.stderr:
        problems.append(f"not {drawn}")
    elif not out.exists() or out.read_bytes()[:8] != b"\x89PNG\r\n\x1a\n":
        problems.append("no PNG written")
    return problems


def run_all(shell, env, root, jobs, wait, args, render_one=None):
    """Renders the (popup, out) jobs in parallel; returns {out: problems} for those that had any."""
    for _, path in jobs:
        path.unlink(missing_ok=True)
    render_one = render_one or (lambda popup, out: render(shell, env, root, popup, out, wait,
                                                         args.icon_theme))
    with ThreadPoolExecutor(args.jobs) as pool:
        results = pool.map(lambda job: (job[1], render_one(*job)), jobs)
        return {path: problems for path, problems in results if problems}


def run_gpu(build, shell, env, root, jobs, args):
    """The jobs with Qt's default (GPU) renderer, each in a private headless compositor of its
    own: a window opening beside another takes the focus from it, and a panel that loses the
    focus closes its popups."""
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tests"))
    import harness
    (root / "init.lua").write_text((root / "init.lua").read_text().replace(
        "    shell = {\n", '    shell = {\n        renderer = "gpu",\n', 1))
    # The compositor makes a runtime directory of its own, short enough for its sockets.
    env = {name: value for name, value in env.items() if name != "XDG_RUNTIME_DIR"}

    # An output large enough for the preview's popover window, which would be shrunk to fit.
    config = ('return { xwayland = false, outputs = { monitors = { ["HEADLESS-1"] = '
              '{ mode = "1920x1080" } } } }\n')

    def render_one(popup, out):
        with harness.Compositor(str(build / "paw"), config, env=env,
                                prefix="sd-gal-") as desktop:
            gpu_env = dict(desktop.env, QT_QPA_PLATFORM="wayland")
            gpu_env.pop("PAW_SOCKET", None)  # a preview needs no compositor state
            return render(shell, gpu_env, root, popup, out, args.wait or 600, args.icon_theme)

    return run_all(shell, env, root, jobs, args.wait or 600, args, render_one)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("build", type=Path)
    parser.add_argument("out", type=Path)
    parser.add_argument("--renderer", choices=["software", "gpu", "both"], default="both")
    parser.add_argument("--theme", action="append", choices=list(THEMES),
                        help="only this theme (repeatable)")
    parser.add_argument("--popup", action="append", choices=POPUPS,
                        help="only this popup (repeatable)")
    parser.add_argument("--scale", help="device pixel ratio, e.g. 1.25")
    parser.add_argument("--icon-theme", default=icon_theme(),
                        help="icon theme (default: the desktop's, from GTK's settings)")
    parser.add_argument("--jobs", type=int, default=min(8, os.cpu_count() or 1))
    parser.add_argument("--wait", type=int, metavar="MS",
                        help="how long a popup is open before its picture (default: 400, "
                             "600 on the GPU)")
    parser.add_argument("--no-animations", action="store_true",
                        help="with animations off, so that a short --wait catches nothing "
                             "moving")
    args = parser.parse_args()
    build = args.build.resolve()
    shell = build / "paw-shell"
    args.out.mkdir(parents=True, exist_ok=True)
    out = args.out.resolve()
    renderers = ["software", "gpu"] if args.renderer == "both" else [args.renderer]
    if "gpu" in renderers and not (build / "paw").exists():
        print("no compositor in the build directory: the GPU renderer is left out",
              file=sys.stderr)
        renderers.remove("gpu")
    failures, written = {}, 0
    with tempfile.TemporaryDirectory(prefix="paw-gal-") as directory:
        for theme in args.theme or list(THEMES):
            root = Path(directory) / theme
            env = prepare(root, theme, not args.no_animations)
            if args.scale:
                env["QT_SCALE_FACTOR"] = args.scale
            for renderer in renderers:
                jobs = [(popup, out / f"{theme}-{popup}{'-gpu' if renderer == 'gpu' else ''}.png")
                        for popup in args.popup or POPUPS if shows(theme, popup)]
                if renderer == "software":
                    failures.update(run_all(shell, dict(env, QT_QPA_PLATFORM="offscreen",
                                                        QT_QUICK_BACKEND="software"),
                                            root, jobs, args.wait or 400, args))
                else:
                    failures.update(run_gpu(build, shell, env, root, jobs, args))
                written += sum(path.exists() for _, path in jobs)
    for path, problems in sorted(failures.items()):
        print(f"{path.name}:", *problems, sep="\n    ", file=sys.stderr)
    print(f"{written} pictures in {out}"
          + (f"; {len(failures)} with problems" if failures else ""))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
