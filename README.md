<div align="center">

# paw

**P**AW **A**rranges **W**indows

A mouse-first Wayland desktop.<br>
Floating windows, edge snapping and optional Hyprland-style tiling, a Qt Quick shell laid out
as macOS's (menu bar, dock, Launchpad) or as a taskbar, and a Lua configuration that reloads when
you save it.

[![license](https://img.shields.io/badge/license-GPL--3.0--or--later-blue)](LICENSE)
[![wlroots](https://img.shields.io/badge/wlroots-0.20-teal)](https://gitlab.freedesktop.org/wlroots/wlroots)
[![Lua](https://img.shields.io/badge/config-Lua%205.4-navy)](docs/config-reference.md)
[![release](https://img.shields.io/badge/release-0.1.1%20beta-yellowgreen)](CHANGELOG.md)

[Highlights](#highlights) · [Building](#building) · [Installing](#installing) ·
[Running](#running) · [Configuration](#configuration) · [Features](docs/features.md) ·
[Contributing](CONTRIBUTING.md)

</div>

> **Status:** beta; the latest release is 0.1.1. It is my everyday desktop on Gentoo,
> as a session started from a display manager or a TTY, and it also runs nested in another
> Wayland session. It has been used on few machines so far, so expect rough edges on hardware
> it has not met: [docs/verification.md](docs/verification.md) records what has been checked,
> and [Reporting bugs](#reporting-bugs) says what to send.

## Highlights

- **Floating first.** Focus follows the mouse, Super + drag moves and resizes, windows snap
  and stick to edges, and new windows are placed where they cover the least.
- **Tiling when you want it**, per monitor: dwindle (as in Hyprland), master, spiral, monocle,
  and a niri/PaperWM-style scrolling layout.
- **Per-monitor workspaces**, a scratchpad, sticky windows, tab groups, window rules,
  terminal swallowing, and saved sessions you can restore, the last one at the next login.
- **A Qt Quick shell** on every monitor, in [the macOS style](docs/features.md#the-macos-style)
  (a menu bar and a dock, Launchpad, Spotlight, Control Center, Notification Center, in light and
  dark) or as a taskbar with a start menu and [pictures of a button's
  windows](docs/features.md#window-pictures) on hover and in the Alt + Tab switcher; either way:
  workspace indicator,
  Quick Settings (network, volume, brightness, battery, night light, tiling, appearance; each
  can sit on the bar instead), keyboard layout, a clock with the notifications and a calendar,
  a system tray, a notification daemon with history and do-not-disturb, an on-screen display,
  an Alt + Tab switcher, an Exposé-style overview, a command palette, and a power menu.
- **Monitors** set up in a [display settings window](docs/features.md#display-settings), as on
  Windows and KDE: drag them into place, choose resolution, refresh rate, scale, rotation,
  adaptive sync, mirroring, 10-bit colour and HDR, and keep the result only once it is seen to
  work (it goes back by itself after 15 seconds); or in Lua, with Win + P's display modes for a
  projector.
- **Power controls** through logind (systemd-logind or elogind): lock, suspend, hibernate,
  restart, power off and log out, locking before any sleep and closing windows first so that
  applications can save; and [power saving when idle](docs/features.md#power-saving-when-idle)
  without an idle daemon: the screens dim, the monitors turn off, the screen locks and the
  machine suspends after their timeouts, shorter on battery if you like, held off while a video
  plays. A laptop docked with its [lid closed](docs/features.md#the-laptop-lid) turns its own
  panel off.
- **Administrator passwords:** the shell is the session's polkit agent, so `pkexec`, GParted and
  updaters ask for a password in a dialog of their own.
- **Effects:** interruptible animations with spring curves, dimming of inactive windows, peek,
  night light, a magnifier, and hot corners.
- **Touch:** three-finger swipes move between workspaces with the fingers and open the
  overview, other touchpad gestures reach applications (pinch-zoom), touchscreens take several
  fingers at once, and drawing tablets give applications pressure and tilt.
- **Lua configuration** that is validated with file and line, reloads on save, can extend
  the defaults instead of copying them, and switches between appearance profiles.
  `paw import` carries over an existing Hyprland/Waybar setup.
- **Scriptable:** every action, plus queries and an event stream, over a control socket
  (`paw msg`).
- **Works with everyday applications:** XWayland, screen sharing through portals,
  `ext-session-lock` lockers, idle daemons, clipboard managers, and the protocols browsers,
  Electron applications and games look for.

See [docs/features.md](docs/features.md) for how each of these works.

## Building

Requirements:

- CMake 3.25+, C11 and C++20 compilers, pkg-config, Ninja (used below)
- Lua 5.4, xkbcommon
- wlroots **0.20.x** (its API changes between release series), wayland-server,
  wayland-protocols, wayland-scanner
- For the shell: Qt 6.9+ (Core, Gui, Network, Qml, Quick, Quick Controls Basic, Quick Layouts,
  Quick Effects, the Wayland platform plugin, and DBus for notifications, the tray, the media
  controls, the power mode, Wi-Fi and Bluetooth),
  LayerShellQt 6.6+, GLib/GIO, wayland-client
- libinput and xcb (with xcb-xfixes and xcb-ewmh) when wlroots is built with its libinput
  backend or with XWayland; optionally sd-bus (libsystemd, libelogind or basu) for the
  [sleep inhibitor](docs/features.md#screen-locking-and-idle) and libpulse for the volume control
- Python 3 for the tests

```sh
cmake -S . -B build -G Ninja -DPAW_BUILD_COMPOSITOR=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Without `CMAKE_BUILD_TYPE` the build is `RelWithDebInfo`; pass `-DCMAKE_BUILD_TYPE=Debug` for a
debug build.

| Option | Default | Effect |
| --- | --- | --- |
| `PAW_BUILD_COMPOSITOR` | `ON` | Build the wlroots compositor; `OFF` builds only the configuration, placement and tiling code and tests |
| `PAW_BUILD_SHELL` | `ON` | Build the Qt Quick shell |
| `PAW_SHELL_PREVIEW_ONLY` | `OFF` | Build only a shell UI preview, without LayerShellQt (see [Development](#development)) |
| `PAW_NOTIFICATIONS` | `ON` | Build the shell's notification daemon (needs Qt DBus) |
| `PAW_PULSEAUDIO` | `ON` | Build the panel's volume control (needs libpulse, which PipeWire also serves) |
| `PAW_TRAY` | `ON` | Build the panel's system tray (needs Qt DBus) |
| `PAW_POLKIT` | `ON` | Build the shell's polkit authentication agent (needs libpolkit-agent-1; left out without it) |
| `PAW_MEDIA` | `ON` | Build the media controls: Quick Settings' card and the media keys, through MPRIS (needs Qt DBus) |
| `PAW_POWER_PROFILES` | `ON` | Build Quick Settings' power mode tile, through power-profiles-daemon (needs Qt DBus) |
| `PAW_NETWORKMANAGER` | `ON` | Build Wi-Fi in Quick Settings and the network widget, through NetworkManager (needs Qt DBus) |
| `PAW_BLUETOOTH` | `ON` | Build Quick Settings' Bluetooth tile and pairing, through BlueZ (needs Qt DBus) |
| `PAW_INSTALL_SESSION` | `OFF` | Install the display-manager session entry |
| `PAW_XWM_WAKER` | `ON` | Work around lost X11 windows; turn off with wlroots patched by `packaging/patches/wlroots-xwm-drain.patch` |

## Installing

```sh
cmake -S . -B build -G Ninja -DCMAKE_INSTALL_PREFIX=/usr -DPAW_INSTALL_SESSION=ON
cmake --build build
sudo cmake --install build
```

This installs `paw`, `paw-session` (see [First run](#first-run)) and `paw-shell`,
the default configuration (`share/paw/init.lua`), `paw-portals.conf` for
xdg-desktop-portal, the documentation, and with `PAW_INSTALL_SESSION` the session entry
display managers list. Choose the prefix
when configuring, as above: paw looks for its default configuration under it, so
`cmake --install --prefix` with another one leaves that unfound. `DESTDIR` stages an install
for packaging. On Gentoo, `packaging/gentoo` has ebuilds; see [docs/gentoo.md](docs/gentoo.md).

paw runs without any of these, and uses them when they are installed:

- a terminal: Super + Q opens the first of kitty, foot, alacritty, wezterm, ghostty, konsole,
  gnome-terminal and xterm that is installed ([how to choose one](#first-run))
- `grim` and `slurp` for screenshots, and `wl-clipboard` to copy them
- `xdg-desktop-portal-wlr`, `xdg-desktop-portal-gtk` and PipeWire for screen sharing and file
  choosers ([Portals](docs/features.md#portals))
- `Xwayland` for X11 applications
- fcitx5 for [input methods](docs/features.md#input-methods): Chinese, Japanese and Korean in
  Wayland applications
- a locker such as swaylock or gtklock for [locking](docs/features.md#screen-locking-and-idle),
  by hand or [when idle](docs/features.md#power-saving-when-idle)
- an icon theme (Adwaita, Breeze, Papirus, ...) for application icons, and `dconf` for the
  window buttons of GTK applications

The shell is the notification daemon; `notifications = { enabled = false }` leaves that to mako,
dunst or another one.

## Running

```sh
./build/paw --config config/init.lua --check-config   # validate a configuration
./build/paw --config config/init.lua --exec kitty     # run nested, starting kitty
```

By default paw opens as a window inside the current Wayland session. Other modes:

- `--session` runs it standalone on DRM/libinput from a TTY. It is in daily use on an NVIDIA
  desktop with three monitors at mixed scales and has run on an AMD laptop; hotplug is tested
  only with virtual outputs, and suspend is unchecked. See [docs/gentoo.md](docs/gentoo.md) and
  [docs/verification.md](docs/verification.md).
- `--headless` runs without any display, for tests.

The shell starts with the compositor unless you pass `--no-shell` or set
`shell.enabled = false` (headless mode never starts it). Everything after `--exec` is one
command, run without a shell, that inherits the compositor's Wayland socket.

When nested, the host may keep shortcuts for itself: a host that grabs Super (Hyprland, GNOME)
needs `mod = "Alt"` in the configuration. Applications that reuse an existing process or D-Bus
service may also open in the host session instead.

### First run

- **From a display manager**, pick paw from its list of sessions. The entry (installed with
  `PAW_INSTALL_SESSION`, and by the Gentoo ebuild) runs `paw-session`.
- **From a text console**, log in and run `paw-session`. A seat manager (elogind,
  systemd-logind or seatd) has to give you the GPU and input devices, as it does for any
  Wayland desktop.
- **To try it first**, run `paw` inside your current Wayland session; it opens in a window.

`paw-session` runs `paw --session`, passing its arguments on. Where nothing has
started a D-Bus session bus, as is usual without systemd, it starts one with `dbus-run-session`:
notifications, the tray and portals need one.

A standalone session starts the `startup` commands of the configuration and then the
applications set to start at login, the XDG autostart entries in `~/.config/autostart` and
`/etc/xdg/autostart` (see [Starting the session](docs/features.md#starting-the-session)).

Without `~/.config/paw/init.lua`, the installed [config/init.lua](config/init.lua) is
used. It starts in the macOS style; the profiles in the system menu (the spiral at the top left)
switch to dark or to the taskbar. Super + R opens Launchpad (the start menu in the taskbar),
Super + Q opens a terminal, Super + C closes the
focused window, and Super + M quits; [Default bindings](#default-bindings) lists the rest. The
terminal is `$TERMINAL` when that is set, else the first one installed (kitty, foot, alacritty,
...). To choose it, write a configuration that extends the default:

```lua
return {
    version = 1,
    extends = "default",
    terminal = { "foot" },
}
```

When a binding cannot start its program, because none of those terminals is installed for
example, the panel says why for a few seconds.

paw, the shell and the programs they start log to standard error, which `paw-session`
writes to `~/.local/state/paw/session.log` (in `$XDG_STATE_HOME` when that is set); the
previous session's log is kept as `session.log.old`. Running `paw` yourself, keep it in a
file with `paw --session 2> ~/paw.log`.

## Configuration

paw reads `$XDG_CONFIG_HOME/paw/init.lua` (`~/.config/paw/init.lua`), else the
installed example, [config/init.lua](config/init.lua); it never writes a personal
configuration for you. A configuration only needs what it changes when it extends the
defaults:

```lua
return {
    version = 1,
    extends = "default",
    theme = "theme.lua",
    bindings = {
        { mods = { "Super" }, key = "e", action = "spawn", command = { "dolphin" } },
        { mods = { "Super" }, key = "v", action = "none" }, -- drop a default binding
    },
}
```

Its bindings take their keys first and the defaults fill in the rest; `action = "none"` leaves
a key unbound. Other lists, such as `startup` or `shell.launchers`, replace the default list
whole.

Settings are checked before they apply, and a mistake is reported with its file and line.
Saving the file reloads it (`auto_reload = false` turns that off); a file with an error loads
the default configuration instead and shows the error across the top of every monitor until
it is fixed. `paw --check-config` checks a file without starting anything. SIGHUP or
Super + Shift + R also reload; a reload does not rerun startup commands.

Lua can compute settings with the base, table, string, math and UTF-8 libraries; process and
file I/O are not exposed. The configuration is your own code: the evaluator is not a sandbox
for untrusted scripts.

- [docs/config-reference.md](docs/config-reference.md): every setting and binding action,
  generated from the schema
- [docs/features.md](docs/features.md): what the settings do, with examples
- [docs/dotfile-import.md](docs/dotfile-import.md): `paw import ~/.config` for Hyprland/Waybar setups

## Default bindings

Edit them in [config/init.lua](config/init.lua).

| Input | Action |
| --- | --- |
| Super + left / right drag | Move / resize a window (on a tile: move it, or move its splits) |
| Super + Q | [Open a terminal](docs/features.md#terminal): `terminal`, `$TERMINAL`, or the first one installed |
| Super + R | Application menu on the monitor under the pointer |
| Super + P | [Command palette](docs/features.md#command-palette) |
| Super + B | Walk the [taskbar](docs/features.md#taskbar) or the dock from the keyboard: arrows, Enter, Escape |
| Super + C | Close the focused window |
| Super + M | Exit paw |
| Super + Shift + L | [Lock the screen](docs/features.md#power) with `power.lock_command` (swaylock) |
| Super + Escape | [Power menu](docs/features.md#power): lock, suspend, hibernate, restart, power off, log out |
| Super + Shift + Escape | Take the keys back from a virtual machine, remote desktop or game [holding them](docs/features.md#applications-that-take-the-shortcuts), or give them back to it (`toggle_shortcuts_inhibit`) |
| Super + V | Float or tile the focused window |
| Super + Shift + V | [Clipboard history](docs/features.md#clipboard-history): what was copied lately, to copy again |
| Super + semicolon | [Emoji picker](docs/features.md#emoji-picker): an emoji typed into the focused window |
| Super + F | Fullscreen |
| Super + T | Arrange the monitor's windows in a grid (floating mode) |
| Super + S | [Tiling](docs/features.md#tiling) on or off for the focused monitor |
| Super + Shift + P | Make the focused window [sticky](docs/features.md#sticky-windows), or not |
| Super + Ctrl + T | [Keep the focused window above](docs/features.md#keeping-windows-above) the others, or not |
| Alt + Tab / Alt + Shift + Tab | [Window switcher](docs/features.md#window-switcher) over every window |
| Super + O | [Overview](docs/features.md#overview) of the monitor's workspace |
| Super + ` | Back to the previously focused window, on any workspace |
| Super + arrows | Focus the nearest window that way |
| Super + Shift + arrows | Move the window that way: a tile trades places, a floating window goes to the edge, then on to the next monitor |
| Super + Alt + arrows | [Snap](docs/features.md#snapping) as Windows' Win + arrows: left / right to that half, then on to the next monitor; up maximizes; down restores, then minimizes |
| Super + Ctrl + Shift + arrows | Resize by 40 pixels |
| Super + Space / Super + Shift + Space | Next / previous [tiling layout](docs/features.md#tiling-layouts) |
| Super + Return | Promote the focused tile to master |
| Super + J / K | Focus the next / previous tile; with Shift, swap with it |
| Super + H / L | Shrink / grow the master column |
| Super + comma / period | One more / fewer master |
| Super + 1–4 | Switch to workspace 1–4 |
| Super + Shift + 1–4 | Move the focused window to workspace 1–4 |
| Super + Ctrl + Left / Right | Previous / next workspace |
| Super + Tab | Back to the workspace shown before |
| Three fingers left / right on a touchpad | Next / previous workspace, the windows following the fingers ([gestures](docs/features.md#touchpad-gestures)) |
| Three fingers up / down on a touchpad | Open / close the overview |
| Super + Ctrl + comma / period | Send the workspace to the monitor on the left / right ([details](docs/features.md#workspaces-between-monitors)) |
| Super + G | Make the focused window a [tab group](docs/features.md#window-groups), or dissolve it |
| Super + Shift + G | Take the focused window out of its group |
| Super + ] / [ | Next / previous tab of the group |
| Super + Shift + minus | Hide the focused window in the [scratchpad](docs/features.md#scratchpad) |
| Super + minus | Show, hide or cycle scratchpad windows |
| Super + U | Focus the window [asking for attention](docs/features.md#urgent-windows) the longest |
| Super + N / Super + Shift + N | [Notification](docs/features.md#notifications-and-on-screen-display) history / do-not-disturb |
| Super + Shift + R | Reload the configuration |
| Print / Shift + Print / Super + Print | [Screenshot](docs/features.md#screenshots-and-screen-sharing) of a region / the monitor / the focused window |
| Volume up / down / mute keys | [Volume](docs/features.md#volume-and-brightness-keys) of the default output up or down by 5 %, or mute it; on the lock screen too, and held they repeat |
| Microphone mute key | Mute the default input, or unmute it; on the lock screen too |
| Brightness up / down keys | The backlight up or down by 5 %; on the lock screen too, and held they repeat |
| Play/Pause, Next, Previous, Stop keys | [Media controls](docs/features.md#media-controls) of the player playing most lately; on the lock screen too |
| Display key (Fn + the monitor key) | Step through [PC screen only, duplicate, extend and second screen only](docs/features.md#display-modes-win--p), as Windows' Win + P |

## Scripting

`paw msg` runs any binding action, and queries state, over the compositor's control socket
(`PAW_SOCKET`, set for everything the session starts):

```sh
paw msg workspace 2
paw msg output HDMI-A-1 toggle_tiling
paw msg spawn foot
paw msg get windows
```

A client that sends `subscribe` receives a line for every change of workspaces, tiling, the
switcher, the overview, urgent windows, the keyboard layout, the binding mode and the power
actions that may run. See [Control socket](docs/features.md#control-socket) for the queries and events.

## Development

The test suite needs no display: it covers configuration validation, the layouts, and a
headless compositor with real xdg-shell and X11 clients, the shell rendered offscreen, and the
notification daemon and the system tray on a private D-Bus. `tools/check-all.sh` runs it again
under ASan and UBSan. [docs/verification.md](docs/verification.md) records what has been tested, on which
hardware, and what has not.

To work on the shell UI without LayerShellQt, build the preview:

```sh
cmake -S . -B build-preview -G Ninja -DPAW_SHELL_PREVIEW_ONLY=ON
cmake --build build-preview
./build-preview/paw-shell --config config/init.lua --preview
./build-preview/paw-shell --config config/init.lua --preview --preview-desktop
```

Preview windows show the UI and can launch applications, but do not manage windows or reserve
space on the host desktop. `--preview-popup NAME` opens one of the taskbar's popups on stand-in
windows, sound and notifications, and `tools/shell_gallery.py build OUT_DIR` saves a picture of
every popup in a light and a dark theme (see
[docs/architecture.md](docs/architecture.md#seeing-a-change)).

Work happens on short-lived branches off `main` that are merged back with `--no-ff`. See
[CONTRIBUTING.md](CONTRIBUTING.md), [CHANGELOG.md](CHANGELOG.md) for notable changes, and
[docs/performance.md](docs/performance.md) for measurements.

Not yet done: blur and shadows, and Lua extension APIs for custom
layouts and shell widgets.

## Reporting bugs

Open an issue at [github.com/Sondre234/paw/issues](https://github.com/Sondre234/paw/issues).
The bug report template asks for `paw --version`, the GPU and its driver, the wlroots version,
whether paw ran nested or with `--session`, your configuration, and the log
([First run](#first-run) says where it goes).

## References

- [wlroots API](https://wlroots.pages.freedesktop.org/wlroots/)
- [TinyWL 0.20.2](https://gitlab.freedesktop.org/wlroots/wlroots/-/tree/0.20.2/tinywl), which
  the compositor's C adapter derives from
- [Lua 5.4 API](https://www.lua.org/manual/5.4/manual.html)
- [Hyprland dwindle layout](https://wiki.hypr.land/configuring/layouts/dwindle-layout/), the
  model for the tiling (reimplemented; no Hyprland code is included)

## License

paw is free software, licensed under the GNU General Public License, version 3 or (at your
option) any later version. See [LICENSE](LICENSE).

The compositor adapter derives from TinyWL, whose MIT license is kept in
[vendor/tinywl/LICENSE](vendor/tinywl/LICENSE); `src/compositor/scaled_capture.c` adapts
wlroots' capture source for a scene node under the same license, wlroots' own. The protocol
files in `protocols/` keep the licenses stated in each file.
