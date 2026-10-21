# Changelog

Notable changes, newest first, one section per release. 0.1.0 is the first release and holds
all the work that led up to it.

## Unreleased

The shell, redesigned after Windows 11: a start menu, a clock flyout, Quick Settings, restyled
menus and overlays, and motion throughout. And a second style after macOS, where paw now
starts. Three rices ship with paw, and the compositor learned Windows' snapping, touch and
tablet input, power saving when idle and HDR.

- The macOS style (`shell.style = "macos"`), where the shipped configuration now starts: a menu
  bar, a centred dock, Launchpad, Spotlight, Control Center and Notification Center, in a light
  and a dark profile, with traffic-light window controls and window shadows.
- The Windows 11 style: a start menu with pins and search, a clock flyout with the notifications
  over the calendar, Quick Settings (volume, brightness, media, Wi-Fi through NetworkManager,
  Bluetooth through BlueZ, power mode), and window pictures on the taskbar and in Alt + Tab.
- The rices `washi` and `sumi`, `mochi` and `hibana`, and what they are made of: blur, shadows,
  gradient borders, wallpapers per monitor, a profile's own cursor and icons, and a tiling bar
  in Hyprland's style while a monitor tiles. `paw rice pack` shares a look as one file.
- Snapping as on Windows: dragging to an edge or a corner, Super + Alt + arrows through halves
  and quarters, and Snap Assist offering the other windows for the space left.
- A clipboard history (Super + Shift + V), an emoji picker (Super + semicolon), a run launcher
  after rofi, a colour picker and a sheet of every binding (Super + /); search calculates and
  finds files.
- Binding modes, bindings that run while locked or on release, input methods such as fcitx5,
  and the keys left to virtual machines and games that ask for them.
- Touchpad gestures (three fingers swipe between workspaces), touchscreens and drawing tablets.
- Monitors turned off in place, power saving when idle without an idle daemon, a laptop lid's
  clamshell mode, mirroring, Win + P's display modes, 10-bit colour, HDR, tearing for games and
  a display settings window.
- A standalone session starts XDG autostart entries, is the polkit agent, and comes back as it
  was left.
- New commands: `paw doctor` finds what is wrong with a session, `paw config`, `paw top`,
  `paw fetch`, and toys such as `paw cat`, `paw wrapped` and `paw tour`.
- A right click on the Quick Settings button lists the sound outputs to switch to, as one on the
  bar's volume does.

## 0.1.1 (2026-10-05)

Fixes to the tests, CI and the ebuild; the compositor and the shell behave as in 0.1.0.

- Where grim is not installed, as in an ebuild's test phase, the pixel tests leave out their
  pixel checks as they were meant to; CI installs grim, so they run there.
- The release ebuild suggests every terminal Super + Q looks for, not only kitty.

## 0.1.0 (2026-10-05)

The first release, and the first version I'm handing to other people to try: a wlroots
compositor configured in Lua, with a Qt Quick shell, that runs nested or as a session of its
own.

- Floating windows with snapping, magnetic edges and smart placement; dwindle, master and
  scrolling tiling, per monitor or per workspace; window groups, a scratchpad, sticky windows
  and swallowing.
- A Lua configuration that reloads as it is saved, can extend the shipped one with only its
  changes, and holds appearance profiles; `paw import` carries a Hyprland, Waybar, wallbash or
  pywal look over.
- A shell with a taskbar of pinned and grouped applications, a launcher, the desktop,
  workspaces, a volume control, notifications, a system tray, an Alt + Tab switcher, an
  overview (Super + O) and a command palette (Super + P).
- Monitors with their own modes, scales, rotation and workspaces, windows kept through hotplug,
  and changes from `wlr-randr` and `kanshi`.
- Animations with a curve for each kind, dimmed inactive windows, night light, a magnifier and
  hot corners.
- Power controls through logind, a power menu and a lock before sleep; keyboard layouts and XKB
  keymap files.
- Screenshots through grim and slurp, sessions saved and restored by name, and urgent windows.
- XWayland on demand, and what browsers and Electron applications need: dmabuf, primary
  selection, xdg-activation and screen capture.
- `--session` on DRM and libinput from a TTY or, through `paw-session`, a display manager; a
  `terminal` action that finds an installed terminal; programs that fail to start say so on the
  panel.
- Gentoo ebuilds in `packaging/gentoo`.
