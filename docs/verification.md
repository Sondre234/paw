# Verification: checkpoint log

Dated records of what was checked, automatically and by hand, and what was not. New
checkpoints are appended; the automated part is the `ctest` suite, which CI also runs
headless in an Arch Linux container (see `.github/workflows/ci.yml`).

## First nested compositor checkpoint

Verified on 2026-09-23 with wlroots 0.20.2, GCC 16.2.1, Lua 5.4.8, and Wayland
1.26.0 on Arch Linux. Nested graphics ran inside Hyprland on an NVIDIA RTX 4090
using the wlroots GLES2 renderer. Nothing was installed into the login/session
configuration, and the existing desktop remained running.

## Automated checks

- CMake/Ninja build: passed without compiler warnings.
- Configuration tests: valid/computed settings, key/modifier matching, malformed
  values, unknown settings/actions, duplicate bindings, bounded Lua evaluation,
  and preserving configuration after a failed load.
- Placement tests: output bounds, non-overlap, gaps, negative output origins,
  small outputs, and invalid inputs.
- Headless compositor integration: four actual xdg-shell client connections,
  shared-memory rendering with frame callbacks, maximize/restore, unmap/destroy,
  valid SIGHUP reload, invalid reload rejection with continued rendering,
  invalid `--check-config` exit status, and SIGTERM exit status zero with socket
  removal. Uses Pixman and a private temporary `XDG_RUNTIME_DIR`.
- C/C++ formatting and `git diff --check`: passed.

Run all automated checks with `ctest --test-dir build --output-on-failure`.

## Nested runtime checks

This section records the checkpoint as it was run. Its keys were the then-default Alt
bindings (Alt + drag, Alt + F4, Alt + Shift + Escape); the default modifier is now Super
(see [config/init.lua](../config/init.lua)), and there are no default bindings for the
last two: close is Super + C and quit is Super + M.

Real Kitty clients were opened inside temporary nested compositor instances.
Input was sent to those windows through Hyprland's documented dispatch API;
window-only screenshots were inspected. Verified:

- Background and application rendering, keyboard text input, window cycling.
- Keyboard half-screen snapping, grid arrangement with two applications,
  maximize and restore.
- Alt + left-button movement and Alt + right-button resizing.
- Alt + F4 closes a client (verified by its process exiting with status zero).
- The host window can be resized; the background follows its dimensions.
- Closing the host window shuts down the compositor with status zero.
- Alt + Shift + Escape terminates the nested compositor.

An oversized initial Kitty window exposed a placement issue. Newly mapped
windows now receive a bounded size suggestion and a reachable initial position.
The updated executable was rebuilt and exercised in the nested runtime checks.
Client minimum-size requirements can still exceed very small outputs.

All temporary test compositor instances and terminals were closed after testing.
Screenshots and host-control helpers were kept outside the repository. The
repository's automated tests do not control the user's desktop.

## Remaining coverage and limitations

Physical-device testing, hotplug, multi-monitor behavior, fractional scaling,
clipboard interoperability, client-side title-bar grabs, and extended soak tests
remain. The compositor checkpoint did not include shell UI, workspaces, persistent tiling,
fullscreen handling, XWayland, portals, or session locking (all were added later;
see the checkpoints below and [features.md](features.md)). Interactive resize
positions are applied before clients submit their replacement buffers. This is
a development checkpoint for nested use, not a complete desktop session.

Host input testing followed the
[Hyprland dispatcher documentation](https://wiki.hypr.land/configuring/core/dispatchers/).

## Shell protocol checkpoint

The headless protocol probe now maps an actual layer-shell panel with a
48-pixel exclusive zone and verifies that maximization leaves that area free.
It also receives the window title through foreign-toplevel-management and
exercises minimize, restore/activate, close, and handle removal after unmapping.
The integration test repeats those operations across configuration reloads.

## Qt shell preview checkpoint

On 2026-09-24, the preview build compiles with Qt 6.11.2 and GIO 2.88.3. CTest
renders the taskbar/launcher and desktop QML into PNGs with the offscreen software
backend, using an isolated application directory. Both previews were also
visually inspected. Configuration tests cover shell colors, height, wallpaper,
launchers, and malformed values. The compositor-only build remains supported.

The shell's actual Qt task model also connects to the headless compositor and
controls a real xdg-shell client. Its test verifies title/app ID, active state,
click-to-minimize, restore/activate, maximize/restore, show desktop, close, and
row removal. Stale task IDs after closing a window are harmless. All five CTest
checks pass in the preview build.

At this checkpoint LayerShellQt was not yet installed, so the live shell had not
run. It has since run on physical hardware; see the next section.
The lower-level compositor protocols are tested separately as described above.

## First physical session

On 2026-09-24 `--session` ran from tty3 on an Arch laptop (AMD Vega 8 through
amdgpu, 1920×1080 eDP panel, touchpad; an NVIDIA GTX 1650 was present but
unused). libseat opened the seat through logind. I checked rendering,
touchpad and keyboard input, launching and moving/resizing windows, the shell's
panel and launcher, and an XWayland application (Discord).

Switching to another VT made paw exit. wlroots 0.20 destroys every DRM output
when the session pauses and recreates them on resume; the compositor took the last
output's removal to mean its nested window had closed, and the shell quit when its
last view closed. After the fix, repeated `chvt` round trips kept the compositor,
shell, and clients alive, and the panel returned on the recreated output. Output
removal logs `Failed to disable CRTC` while the DRM FD is paused; it is harmless.

The same day it ran on an Arch desktop: NVIDIA RTX 4090 with the proprietary
610.57 driver (`nvidia_drm.modeset=1`), wlroots 0.20.2 on the GLES2 renderer,
and three monitors (HDMI-A-1 and DP-3 at 2560×1440, DP-1 at 1920×1080). No
NVIDIA workaround variables were needed. All three screens lit at native
resolution; I checked the cursor crossing between them, keyboard input,
launching and moving windows, and the panel and taskbar on each screen. The
session was started from tty3, since SDDM's Xorg holds tty2 there, and Xwayland
fell back from the `:0` socket SDDM owns.

The test found three problems, now fixed:

- Every monitor marked a 60 Hz mode as preferred, so 144 and 200 Hz panels ran
  at 60 Hz. Outputs now use the fastest refresh at the preferred resolution
  (200/144/144 Hz on this machine), falling back if the driver rejects it.
- `wlr_output_layout_add_auto` placed monitors in connector order, not their
  physical order. The Lua `outputs.order` and `outputs.primary` settings now set
  it; I confirmed the layout.
- The taskbar's window menu always opened over the first task button.

These runs were launched over SSH into the tty3 login (`XDG_SESSION_ID`), which
left the process outside that logind session: Ctrl+Alt+F1 was refused
(`Could not switch session: Access denied`), and after the compositor stopped,
the console's keyboard stayed unusable until a reboot. Start `--session` from the
console itself. VT switching on NVIDIA therefore remains unverified.

Clients noted that paw lacks xdg-activation, primary selection, fractional
scaling, and server-side decorations. The first three were added later (see the browser
checkpoint below); X11 windows that ask for decorations now get window controls.

Still untested on hardware: VT switching on NVIDIA, hotplug, suspend/resume, lid
close, and brightness/volume keys.

## Tiling checkpoint

Added 2026-09-24. `ctest` covers the dwindle layout on its own (splits, gaps,
bounds, non-overlap, removal, pointer-side placement, split resizing, separate
trees per output and workspace) and in a headless compositor with real clients:
the toggle, splitting on map, refill after moving a tile to another workspace,
`toggle_floating`, restoring floating sizes, and `subscribe` events. The shell UI
test clicks the panel button against a stand-in control socket. Mouse resizing
and drag-to-retile of tiles have not yet been exercised in a nested or physical
session.

## Browser, Electron, and screen capture checkpoint

Added 2026-09-24. In a headless session on the NVIDIA desktop's GLES2 renderer,
Firefox 154 and Discord 1.0.158 (Electron, both `--ozone-platform=wayland` and via
XWayland) mapped, rendered, and appeared in `paw msg get windows`, using
linux-dmabuf and explicit sync. `wl-copy --primary` / `wl-paste --primary` round-tripped
the primary selection. `grim` captured the output (wlr-screencopy), and `grim -T`
captured Firefox's window alone through ext-foreign-toplevel-list and the per-window
capture source. `compositor_smoke` checks that every one of these globals is advertised.

Not yet exercised: screen sharing end to end through xdg-desktop-portal-wlr and
PipeWire, the D-Bus environment export in a physical `--session`, drag-and-drop, pointer
lock, and popup placement at output edges.

## Multi-monitor tiling checkpoint

Added 2026-09-25. `output_tiling_smoke` runs two headless outputs of different sizes
and scales (2048x1152 logical at 1.25, and 1600x900): disabling one in the config moves
its tiles into the other's tiling without overlap, fullscreen then covers exactly the
remaining output, and turning tiling off leaves every window floating inside it. With a
temporary virtual-pointer build (not committed), dragging a maximized window onto the
smaller output tiled it there beside later windows, a tile dropped there stayed inside
it after tiling was turned off, a window floated with `toggle_floating` stayed floating,
and a window straddling both outputs went fullscreen and maximized on the one it mostly
covered. Not yet exercised on physical mixed-scale monitors.

## Per-monitor workspaces checkpoint

Added 2026-09-25. `output_workspace_smoke` runs two headless outputs with real clients:
both start on workspace 1, switching one (by `output NAME` or through the focused output)
leaves the other alone, each window's visibility follows its own output's workspace,
`move_to_workspace` keeps a window on its output, a window placed onto the other output
joins the workspace showing there, and the `subscribe` stream reports each output. The
shell UI test checks the panel's workspace indicator (current and occupied marks, click,
and wheel paging) against a stand-in control socket, and a headless run with the real
shell on two outputs showed each panel marking its own output's workspace (`grim`).
Not yet exercised on hardware: pointer drags between monitors, wheel paging with a real
mouse or touchpad, and the remembered workspaces across a VT switch.

## Screenshot checkpoint

Added 2026-09-25. `config` covers the default Print bindings, the `screenshots` settings, and
invalid modes and directories. `screenshot_smoke` drives `paw msg screenshot` in a headless
session whose `PATH` holds only stand-ins for grim, slurp, wl-copy, and notify-send: it checks
`grim -o` with the output under the pointer, `grim -g` with the focused window's box, the region
from slurp, distinct names for screenshots within one second, saving without wl-copy, and the
errors when grim or slurp is missing. With the real grim in a headless session, the output and
window modes produced a 1280×720 PNG and a 320×240 crop of the probe window. Not yet exercised:
the Print keys and slurp's interactive selection in a nested or physical session, the clipboard
copy, and the notification.

## Window animation checkpoint

Added 2026-09-25. `animation_smoke` runs a headless compositor with tiling and a
1-second duration, and checks through `paw msg get animations` that opening,
the neighbour's glide, and the closing copy each run and then end, that closing copies
leave no scene trees behind, that a reload turning animations off ends those running,
and that quitting mid-animation exits cleanly. Under AddressSanitizer and
LeakSanitizer the compositor exited without findings. `grim` screenshots of a headless
session with foot showed the fade and scale on opening, the glide, and the closing
copy fading out. Not yet seen on a real display: smoothness at 120 ms on high refresh
rates, and GPU clients (dmabuf) closing.

## Panel context menus

Added 2026-09-25. The task menu used a `TapHandler`, which drops a press held past
the long-press time (0.8 s) and one that moves past the drag threshold, so a
right-click could open nothing. Menus now open on press. In a headless session
(pixman and GLES2, output scale 1.25 and three outputs, a floating bar) driven by a
scratch `zwlr_virtual_pointer_v1` client, right-clicking a task showed its menu on
every output, and Maximize / restore, Minimize and Close window acted on foot;
right-clicking empty bar space showed the bar menu, whose tiling and Applications
items worked. `shell_ui` right-clicks a stand-in task (holding the button for a
second) and empty bar space, and checks each menu lies inside the grown surface.
Not yet checked with a physical mouse or touchpad.

## Scrolling layout

Added 2026-09-28. `tiling_tests` checks the column geometry (widths, gaps, stacks, the
three view-follow modes, presets, consuming and expelling, moving columns) and runs 4000
random operations checking that every window is placed once and none overlap, also under
AddressSanitizer and UBSan. `scroll_smoke` drives a headless compositor with three
clients through scroll, width, center, consume and expel actions. Not checked: how the
gliding view looks at real refresh rates, focus following the mouse while columns
move under a still pointer, dragging a column's edge with a physical mouse, Xwayland
windows in columns, or several monitors with different layouts on a real display.

## Output layouts and hotplug

Added 2026-09-28. `tiling_tests` checks that an output's layout defaults sit between the
global defaults and a workspace's own choice. `output_layout_smoke` reloads the configuration
under two headless outputs and checks that `layout.outputs` reaches windows already open
without overriding a layout chosen by an action. `output_hotplug_smoke` unplugs and re-plugs
a headless output (`paw msg headless_output`) holding tiled windows on two workspaces and
floating windows: they move to the other output, keep their workspaces and tiling, and come
back, or stay with `outputs.return_windows = false`. Not checked on real hardware: a monitor
that goes away and comes back through DRM (sleep, cable pull, DisplayPort link retraining),
hotplug events arriving while a client is mid-commit, or fractional scales that differ
between the monitors.

`scroll_follow_smoke` checks the three `layout.scroll.follow` modes (`center`, `edge`, `never`)
against a headless compositor. `output_workspace_move_smoke` moves and swaps workspaces between two headless outputs
(windows, layout state, what each output shows, and an output that does not tile), and checks
that the windows glide. `session_scroll_smoke` saves and restores scroll columns. Not checked:
the look of the glide across two real monitors with different scales or refresh rates.

## Window swallowing, magnetic edges and placement

Added 2026-09-28. `swallow_smoke` runs a headless compositor whose probe client starts
another probe window on a signal, directly or through a shell that stays in between, as an
application started from a terminal is: the window takes the terminal's tile or floating
rectangle, the terminal is hidden and off the taskbar, closing the window brings it back
(also from another workspace, and from fullscreen), `exceptions`, the case-blind terminal
list, a terminal that dies first, `enabled = false` and `swallow_toggle` (both ways) behave as
documented. `swallow_x11_smoke` does the same with an XWayland window, whose process comes from
`_NET_WM_PID`. `magnet_smoke` drags and resizes a floating window through a virtual pointer
and keyboard (`pointer_probe`, with the client asking for the move as a client-decorated window
does): edges land on the output, the area a panel leaves free and another window's edges, hold
until the pointer is the distance away, honour the bypass modifier and the settings, keep
drag-to-top maximizing, and the guide lines' geometry and pixels (grim) are checked.
`window_placement_tests` covers cascade, center and smart placement including 500 random
layouts, and `placement_smoke` the three modes with a panel, a rule's `position`, other
workspaces and tiling. The compositor tests also ran under AddressSanitizer and UBSan.

Not checked: swallowing with real terminals and real applications (foot, kitty, wezterm
or a terminal that keeps one process for several windows), an application that hands its
window to a running instance, how the magnetism feels with a physical mouse or touchpad at
real refresh rates and whether 12 px suits high-density outputs, magnetism across monitors
with different scales, and smart placement when a client changes its size right after
opening.

## Urgent windows

Added 2026-09-28. `urgent_smoke` drives two to five headless probe windows that ask for
attention through xdg-activation (a token without an input serial, the way a browser handed a
link does): the default policy marks them and leaves focus alone; repeated requests and requests
from the focused window change nothing; `get urgent` and the subscription report them in order;
`focus_urgent` takes them oldest first, switches workspace, brings a scratchpad window back, and
does nothing when none is urgent; focusing by the taskbar protocol, closing, and the policies
`focus` (also from another workspace) and `ignore`, changed by reload, behave as documented.
`urgent_border_smoke` checks with grim that the border pulses, holds `urgent_color`, sits inside
the window without a `border_width` and gives way to the focus color, and that animations off
hold the color at once. `xwayland_urgent_smoke` does the same for
`_NET_WM_STATE_DEMANDS_ATTENTION` and the `WM_HINTS` urgency flag set and cleared by an X11
client, and for a window that asked before mapping (`focus = false` rule). `urgent_shell_smoke` runs the shell on the headless compositor: the taskbar's marker
and the overview's frame appear as pixels and go with focus. `shell_ui`, `shell_tasks`,
`shell_task_filter` and `palette` cover the panel's workspace and task markers, the task
model's urgent role and the palette's ordering. The tests ran repeatedly without a failure, and
the suite passed under AddressSanitizer and UBSan (`shell_preview` timed out once in that
parallel run and passed alone, as it did before this work). The pulse redraws borders every 40 ms
for four seconds: ten windows pulsing at once cost the compositor about 40 ms of CPU in those
four seconds (headless, pixman, 20 windows open), and nothing runs once it ends or while idle.

Not checked: real applications asking for attention (a chat client's notification, Firefox or
Chromium opening a link, a terminal's bell through Xwayland, Steam), whether the pulse looks
right on a real display at its refresh rate, how the taskbar's dot reads next to a real icon
theme. An X11 window that asks for attention before it maps is marked only when it opens
without focus (a `focus = false` rule or another workspace); one that opens with focus has it. A token created by a different client than the window it
activates is treated the same as one from the window itself.

## Notifications and on-screen display

Added 2026-09-29. Everything runs headless on a dbus-daemon the tests start (and kill by process)
on a private address; the shell is started with that address, or with none in the older shell
tests, and never sees the real session bus. `notifications_test` (QtTest) covers the model, the
timers (expiry, hover pause keeping the remaining time, replacement while held), replacement by id
and by stack tag, the limits, do-not-disturb with critical notifications passing, dismissal and
default and named actions, `resident` and `transient`, closing by the application, the history
cap and unread count, and 23 cases of body markup (unknown tags dropped, links limited to web and
mail addresses, entities, unbalanced tags, `>` inside a quoted attribute).
`notifications_dbus_test` calls the interface over the private bus: server information,
capabilities, `Notify` with every hint the parser reads (urgency, value, image data, image path,
desktop entry, resident, transient, stack tag), `CloseNotification`, the `NotificationClosed` and
`ActionInvoked` signals, refusal of a second daemon, taking the name when the owner leaves, and
1500 random calls with mistyped hints, odd action lists and short image data, which must neither
crash nor break the card and history limits. `notifications_smoke` runs the compositor and the
shell headless on two monitors: the card appears top right (pixels checked with grim), a click
runs the default action, `CloseNotification` and timeouts remove the card, hovering with a
virtual pointer holds the timer past its timeout, replacement keeps the id, do-not-disturb
silences cards but not critical ones, the display and the cards go to the monitor with the focus
and cards stay on one monitor while they last, the display's pill and the history popover are
found as pixels, a change of a fake backlight (`PAW_SYSFS`, polled) shows the display, turning `notifications.enabled` off and on by reload releases and retakes the
name, and a real `notify-send -w -A` gets its action back. `shell_ui` drives the bell, the
badge, the cards' buttons, close button and default click, and the history's switch and Clear
button through Qt Quick offscreen; `shell_osd` the display's timing, the volume hook (baseline
quiet, changes shown, mute, output switches quiet) and a fake sysfs backlight. The new tests ran
repeatedly without a failure, under AddressSanitizer and UBSan without a report, and with
`-DPAW_NOTIFICATIONS=OFF` the shell builds and its tests pass. A search for a bug found one:
the do-not-disturb switch called a property setter QML could not reach, which `shell_ui` caught.

The display's surface has an empty input region (seen in the Wayland trace), so clicks go through it.

Not checked: a physical backlight and its keys (the reading and the display are tested against a
fake sysfs; the uevent socket that prompts a read on real hardware only has its message filter
tested, and the kernel is expected to send one per change), the volume
keys on a real PipeWire or PulseAudio session (the hook is driven by a stand-in sound server), real
applications' notifications (Firefox, Chromium, Discord, Telegram, Thunderbird; only `notify-send`
and a probe client were used), icons from a real icon theme (the tests have none, so cards show
the stand-in), cards over a fullscreen window and over the lock screen on a real display, the look
at high scales and with other fonts, a card pointer-hovered on real hardware, and whether the
timings feel right. Another notification daemon started before the shell keeps the bus name; the
shell then logs it and hides its bell, which is only checked with a second in-process daemon.


## Daily use on Gentoo

Added 2026-10-05. paw is my everyday desktop on Gentoo: `--session` started from
a TTY, installed under a user prefix (`~/.local/paw`) with the shell, running
`~/.config/paw/init.lua`. The machine is an Intel i9-12900K with an NVIDIA RTX 4090 on the
proprietary 595.99.02 driver, kernel 6.18, wlroots 0.20.2 and Qt 6.11.2, driving three monitors:
HDMI-A-1 at 2560×1440 and 144 Hz and DP-3 at 2560×1440 and 200 Hz, both at scale 1.25, and DP-1
at 1920×1080 and 144 Hz at scale 1 (mixed scales side by side). Problems found in daily use have
been fixed on `main` as they came up; none are known to be open.

This replaces the earlier note in [gentoo.md](gentoo.md) that the Gentoo build had only run in a
VM. Daily use is not a targeted test, so the hardware items listed as not checked in the
sections above stay unconfirmed until someone checks them on purpose: suspend and resume, lid
close (this machine has none), monitors unplugged or woken through DRM, VT switching on NVIDIA,
and brightness keys.

## Keyboard layouts and keymap files

Added 2026-10-05. Headless keyboards (`headless_keyboard`, keyboards without a device that the
compositor treats as real ones) stand in for hardware. `keymap_smoke` checks with `wayland_probe
--keymap` that a new application gets the keymap of `keyboard.layout` or `keyboard.file`, that a
reload swaps it while a held Shift stays held and Caps Lock stays on, that a virtual keyboard
(from `pointer_probe`) keeps its own keymap and held Alt through it, and that a keymap file
that does not compile, at startup or on a reload, gives the default configuration's keymap,
with the setting's and the keymap's lines in the log and from `--check-config`.
`keyboard_layout_smoke` drives `switch_layout` next, prev and by number from the control socket
and a binding, Alt + Shift with `grp:alt_shift_toggle` on either of two keyboards, a virtual
keyboard left in its own layout, and reloads that keep the active layout by name or by place,
reading `get keyboard` and the subscription's `keyboard-layout` lines. `keyboard_config`
covers the settings and their diagnostics (paths relative to the configuration, `~/`, missing
and unreadable files, syntax and include errors with their lines, the fallback through
`load_config_or_default`), `import` the Hyprland keys, `auto_reload_smoke` reloading on a saved
`.xkb` file, and `shell_ui` the panel indicator: hidden with one layout, the short name with two,
`switch_layout next` on a click, and `shell.widgets.keyboard_layout = false`. The suite passed;
under AddressSanitizer and UBSan the failures were the known leaks of the test clients and the
shell.

Not checked: real keyboards through libinput (two plugged in at once, one unplugged while a key
is held), whether applications (GTK, Qt, Firefox, foot, Xwayland clients) type in the switched
layout and follow a reload's new keymap, the indicator on a real display, an input method
alongside the layouts, and xkbcommon older than 1.13.

## Power controls

Added 2026-10-05. Everything runs headless against `fake_login1`, a stand-in for logind's
Manager on a dbus-daemon the test starts (and kills by process) on a private address; the
compositor reaches it through `PAW_LOGIN1_BUS` and, headless, never uses the system bus, so
no test can power off, reboot or suspend the machine. `power_smoke` checks that a headless
compositor without the variable reports logind unavailable and refuses the logind actions;
`get power` against the fake's `CanPowerOff`, `CanReboot`, `CanSuspend` and `CanHibernate`
answers, refusal of `na` and `no`, reload asking again, and a call the fake turns down reported
as `power-error`; `PowerOff`, `Reboot`, `Suspend` and `Hibernate` arriving with
`interactive = true`; `lock` starting the configured locker (a probe holding an
`ext-session-lock-v1` lock), refused without one or with one not installed; suspend and
hibernate starting the locker and calling logind only once the lock holds (a locker that waits
to be told to lock shows logind is not asked before), and cancelling after five seconds with a
locker that never locks; the sleep delay inhibitor taken at startup, released on
`PrepareForSleep` once the lock holds, for a suspend the fake starts on its own (as the lid or
an idle daemon would) too, and taken again after waking; `lock_before_sleep = false` sleeping
without a locker or an inhibitor; power off closing two probe windows before calling logind; a
probe refusing to close cancelling a reboot after `close_timeout`, `force` going ahead and
`close_windows = false` not asking; log out closing the windows, waiting for their clients and
exiting with status 0; and the `power` state line and `power-menu` event subscribers get.
`shell_ui` drives the power button in the launcher's corner and its menu (only what may run,
hidden with nothing, closed by a press beside it),
suspend and lock from it, refusals and `power-error` shown on the panel, the confirmation
dialog's countdown running out into a power off, its button, Enter, Escape, Cancel and a click
beside it, an action that may no longer run dropping its dialog, the command palette's entries,
and the keyboard in the menu `power_menu` opens. `power_config` covers the settings.

Not checked: a real logind (only its read-only `CanPowerOff` and the like were asked, on
elogind 255, which answers as the fake does), and so a real suspend, hibernate, reboot or power
off, a polkit password prompt for a `challenge` answer, the lid or an idle daemon sending the
machine to sleep with swaylock locking first, and systemd-logind; logging out of a session a
display manager started; real applications asked to close (only probe clients were) and how
long they take; and the dialog and the menu on a real display, at other scales, and with a real
keyboard.

## System tray

Added 2026-10-05. `tray_dbus_test` drives the watcher and the host on a private bus with fake items:
registration by bus name and by object path, hosts, owners leaving, another watcher already
serving, the change signals, items without GetAll or Activate, pixel byte order and size choice,
icons from an item's own folder, menus (the layout, AboutToShow, the events, LayoutUpdated and
ItemsPropertiesUpdated), and a seeded fuzz of malformed properties, pixmaps, layouts and signals
that fails on any Qt warning. `shell_ui` clicks, scrolls and opens menus on the panel offscreen;
`tray_smoke` runs a headless compositor with two outputs, the shell and two `tray_probe` items,
checking icons with screenshots and clicks with a virtual pointer. Not checked: real applications
(Discord and other Electron applications, Steam, nm-applet, blueman, KDE Connect, Telegram,
Nextcloud, OBS), a physical mouse or touchpad, scaled outputs, and another tray such as Waybar
running alongside in a real session. While a panel menu is open, a click made without moving the
pointer first lands where the pointer was in the smaller surface: the compositor tells a surface
where the pointer is only when it moves.

## First run: terminal, session wrapper and startup errors

Added 2026-10-05. `launch_smoke` runs a headless compositor with nothing on `PATH` but stand-in
programs: `spawn` from the control socket starting one and refusing a missing one with the
reason, both to the caller and as `spawn-error` to a subscriber (the shell), and Super + Q, with
no terminal installed, reporting what to install; the `terminal` action then opening xterm, foot
ahead of it, `$TERMINAL` ahead of both once it is installed, and a configured `terminal` with
its arguments, an uninstalled one being an error. `shell_ui` shows `spawn-error` on the panel
and opens a terminal from the command palette. `session_wrapper` runs `paw-session` with a
stand-in `paw` and `dbus-run-session`: the arguments after `--session`, the log and the
previous one kept, a bus started only without `DBUS_SESSION_BUS_ADDRESS` and
`$XDG_RUNTIME_DIR/bus`, the `paw` installed beside it found, and an unwritable log
directory. A staged install with the session entry ran its `paw-session` (with a stand-in
`paw`) under a real `dbus-run-session`, as CI's Install step does. `startup_failure_smoke`
starts the compositor with `XDG_RUNTIME_DIR` unset, too long for a socket's path, and not
writable, and gets the reason and status 1 each time, not an abort.

Not checked: a display manager (SDDM, GDM, LightDM, greetd) starting the session entry, and the
log and session bus it then has; `paw-session` from a text console on OpenRC; the message a
standalone session logs without a session bus; real terminals opened by `terminal` and swallowed
(GNOME Terminal's `org.gnome.Terminal` app ID is taken from its documentation); and the panel's
error on a real display.

## Shell groundwork: popup gallery, design tokens, GPU renderer

Added 2026-10-06. `tools/shell_gallery.py` renders every popup of the taskbar in a light and a
dark translucent profile, with the software renderer offscreen and with Qt's OpenGL renderer
against a private headless compositor (Mesa's llvmpipe), and the pictures were looked at after
each step. Splitting `Panel.qml` into files was checked picture for picture against the gallery
before it (identical but for the clock), besides the existing tests; `shell_gallery` and
`shell_gallery_gpu` fail on any QML warning, and `shell_ui` checks that the tokens follow the
animation settings and a light, translucent profile. `tools/shell_perf.py` measured both
renderers headlessly (see [performance.md](performance.md)).

Not checked: the shell on the GPU in a real session (a real driver, its memory and startup time,
fractional scaling at 1.25, a 144 or 200 Hz output), hover and pressed states under a real
pointer (offscreen Qt sends no hover, so the gallery shows none), the tooltips (popup windows of
their own, which a grab of the panel leaves out), and the switcher, palette, overview, cards,
display and power dialog after their move to the tokens, which only the tests ran.

## Taskbar popover and shared menus

Added 2026-10-06. The taskbar's popups moved to a layer surface of their own (`PopoverWindow`)
and onto `PopupCard` and `PopupMenu`. Every popup, and the bar menu's and a tray menu's open
submenus, were looked at in the gallery in both themes and both renderers, and a top panel at
scale 1.25 in software. `shell_ui` checks the popover's input region and keyboard, a bar button
switching popups, a press beside closing them, keyboard navigation, hover-opened submenus, tray
submenus and the levels their application hears of, and desktop actions from a private desktop
entry; `popover_smoke` checks the overlay layer, the keyboard, switching on the bar and closing
beside the popups without reaching the window under them through a headless compositor and a
virtual pointer, with screenshots. Both ran eight at a time, besides the whole suite, without
failing. The latency from `paw msg launcher` to the launcher's first frame was measured on
a headless 2560x1440 output (software, and Qt's OpenGL on llvmpipe): later openings take 1.7 ms
and 8 ms, against 7 ms and 9.5 ms when the bar's surface grew; the first after startup 6 and 26
ms, with the popover's graphics set up a moment after startup.

Not checked: any of it on a real display and GPU (an NVIDIA driver's first frame of the popover,
144 or 200 Hz outputs, fractional scaling there), direct scanout of a fullscreen game with the
popover hidden, two monitors with popups open on both, a real pointer's hover over menus, and
tooltips over the popover beyond one look at the power button's under a headless compositor.

## Start menu

Added 2026-10-06. The launcher became a start menu after Windows 11's. Its four pictures in the
gallery (`launcher`, `launcher-all`, `launcher-search`, `launcher-menu`) were looked at after each
step in both themes and both renderers, and in software at scale 1.25. `start_menu` covers the
model without the shell: the launch history (saving, reading back what it can, the cap, failing to
save), seeding the pins from the taskbar's and the default browser and a terminal of a private set
of applications, pinning and moving, the applications by letter, how long ago, the user's picture
from a private home, and the search's groups, words and ties. `shell_ui` drives the menu itself:
the tiles and their pages, recent launches and All apps with its letters, the search's groups and
best match, the keyboard and the pointer moving through them, Escape, the application menu pinning
to the start menu and the taskbar, desktop actions, dragging a tile, an application installed and
removed while the shell runs, and launches recorded from the start menu, desktop actions and the
palette, all under a private `XDG_STATE_HOME`.

Not checked: any of it on a real display and GPU, a real pointer's hover and drags,
AccountsService's picture over a real system bus, a package manager installing an application while
the shell runs, and a monitor short enough that the menu drops a row of pins.

## Clock flyout and Quick Settings

Added 2026-10-06. The clock's flyout (the notifications by application over the calendar, with
its month and year pickers) and Quick Settings were looked at in the gallery in both themes and
both renderers (`calendar`, `notifications`, `clock-empty`, `calendar-years`, `quick-settings`,
`quick-settings-mixer`, `bar-all`), and the flyout under a top panel in software. `shell_ui`
checks paging by the arrows and the wheel, the pickers, Today, the grouped history with its
expander, cross and action buttons, do-not-disturb from the clock, Super + N through the
compositor's request, the widgets' places on the bar and in Quick Settings, every tile against
stand-ins (night light through a fake control socket), the volume, outputs and applications, and
the brightness slider over a fake sysfs backlight. `backlight_dbus_test` checks the call to
logind's `SetBrightness` against a stand-in on a private bus, and `night_light_smoke` what a
real compositor tells subscribers.

Not checked: any of it on a real display (a real pointer's hover over days and notifications,
the slide and zoom animations at 144 or 200 Hz), `SetBrightness` against a real logind and
backlight, and real applications' notifications with pictures and actions in the history.

## Shell motion and consistency

Added 2026-10-06. The bar's motion, the overlays' exits and the shared components were looked at
in the gallery in both themes, both renderers and at a scale of 1.25 (every popup and overlay,
`launcher-empty` added for the start menu's search finding nothing). Motion does not show in
stills, so `shell_ui` slows the animations to a quarter and checks them on their way: a window's
button fading and growing in and shrinking out without taking clicks, a newly pinned application
growing in and a pinned application's window opening on its launcher drawing only its line out,
the tiling button's icon crossfading, and the workspace pill settling under the workspace shown. It also
checks the safe triangle (heading for an open submenu across another entry keeps it, resting on
that entry hands over), the keyboard reaching the best match's buttons, the calendar giving its
time and date to the notifications on a short output with the list's edge fading, and the
desktop's menu. `overlay_exit_smoke` checks, in a headless compositor slowed to a tenth, that the
palette fades out without holding the keyboard (the window under it has it back at once), comes
back when opened as it fades, and that the switcher fades before its surface goes.
`tools/shell_perf.py` shows no wakeups while idle with either renderer.

Not checked: any of the motion on a real display at 144 or 200 Hz (how it looks and what it costs
on the GPU), the safe triangle with a real pointer's speed and jitter, and the overlays' exits
over real applications.

## XDG autostart

Added 2026-10-08. `autostart_tests` checks the splitting of `Exec` (double and single quotes,
escapes, every field code), which directory's file counts, each reason an entry is skipped, the
entries recognised as a notification daemon, a tray watcher or a polkit agent, and the settings.
`autostart_smoke` runs a headless compositor as a login session (`PAW_LOGIN_SESSION=1`) on
temporary XDG directories: the entries start after `startup` with their arguments and in their
`Path`, the user's file hides the system's, the skipped and failed ones are listed with their
reasons by `get autostart`, a reload starts nothing again, and neither a compositor that is no
login session nor `autostart.xdg = false` starts any.

Not checked: a real `--session` starting real applications' entries (Steam, Discord, Nextcloud,
KeePassXC and the like) and the shell skipping a real dunst, mako or snixembed; applications that
start before the shell's tray or notification daemon is on the bus and so miss it (they start
right after the shell, which takes the names a moment later).

## Polkit authentication agent

Added 2026-10-08. `shell_authentication` checks the dialog's model with stand-ins for polkit's
helper: the user chosen, answers typed before and after the helper asks, wrong answers, another
user, a helper that asks twice (a second factor) or says something, Cancel, and requests waiting
their turn or withdrawn. `shell_polkit_agent` runs the agent against `fake_polkitd` on a private
bus started as the system bus: Qt's event loop is GLib's (a GLib idle source runs on it), the
agent registers for a `unix-session` subject, polkit's `BeginAuthentication` reaches the model
with its users (a group left out) and pkexec's details and is answered `ok`, dismissed
(`org.freedesktop.PolicyKit1.Error.Cancelled`), withdrawn by `CancelAuthentication`, two at once
in turn, and the agent going with one open unregisters and withdraws it; registering where
another agent serves the session fails quietly, and so does a bus without polkitd.
`shell_auth_dialog` drives the dialog in its view on the offscreen platform: typing and Enter, a
wrong password, Up, Down and a click choosing the user, a click beside it, Escape, Cancel, one
user named, the next request opening with an empty field. The gallery pictures it
(`auth-dialog`) in all four themes.

Not checked: the live agent, which needs a real `--session` and polkitd: `pkexec true` asking and
running after the right password and again after a wrong one, Cancel making pkexec say the
request was dismissed, GParted and an updater, logind's `challenge` for a power off while another
user is logged in, polkit-agent-helper-1's PAM conversation (with pam_u2f or fprintd, whose
messages the dialog shows), several administrators in `wheel` to choose from, the shell staying
out of the way of a running polkit-gnome, and `shell.polkit_agent` turned off and on by a reload.

## The last session

Added 2026-10-08. `session_restore_smoke` runs headless compositors one after another as login
sessions (`PAW_LOGIN_SESSION=1`) on temporary XDG directories, with a `startup` program and
an autostart entry that open windows of their own and windows opened by hand: `quit` saves
`last`, and `logout` saves it before it asks the windows to close; the next start with
`session.restore = "windows"` puts the windows startup and autostart open again on their
workspaces and places, shows the workspace that was shown and starts nothing else; with
`"launch"` it starts the program of the window opened by hand again and places its window,
waiting for startup's and autostart's rather than starting them twice; `"off"` and a session
that is not a login session neither save nor restore. `autostart_tests` checks the names of what
startup and autostart started and how a saved window is matched to them. The existing
`session_smoke` and `session_scroll_smoke` still check saving and restoring by hand.

Not checked: a real `--session` log out and log in with real applications (browsers that restore
their own windows, Electron applications, terminals, Steam starting slowly at login, X11
windows), power off and restart saving the session as they go (the same code as log out, but
only reached with logind allowing them), monitors that differ between the sessions, and which
real programs `"launch"` cannot start again by their command line.

## Touchpad gestures, touchscreens and tablets

Added 2026-10-08. This machine has no touchpad, touchscreen or tablet, so headless devices stand in
for them: `headless_pointer` (`headless_input.c`) is a pointer that moves and sends swipes,
pinches and holds as libinput's gesture events. `pointer_gestures_smoke` checks with
`input_probe`, a window that prints the input it gets, that each kind of gesture reaches the
window under the pointer with its fingers, deltas, scale, rotation and cancellation, that a
window which never bound the gestures gets none, and that they go to the window under the
pointer rather than the focused one.

Once the compositor offered the gestures, `tray_smoke` found the tray deaf to the wheel: Qt (6.11)
then takes the seat's pointer for a touchpad, and a `WheelHandler` hears only mice by default.
Every one in the shell now takes both.

The swipes the compositor takes have unit tests of their arithmetic (`swipe`: direction, progress,
speed across libinput's 32-bit times, finishing and flicks), of the held slide (`animation`:
holds that time and a late frame leave alone, copies of hidden windows, sliding on from either)
and of the settings (`input_config`). `touchpad_gestures_smoke` drives them with event times of
its own: the slide held at the fingers' progress (`get gesture`), finishing past half way and
with a flick, going back when short, flicked back or cancelled and at the last workspace, the
overview opening and closing with the fingers, a short swipe and an unbound direction reaching the
window from their beginning, a request, inversion, and gestures turned off.

Not checked: a real touchpad through libinput (its units and speeds, against the default distance
of 300 and the flick at 0.5 a millisecond), how the slide held under the fingers looks on a real
display at 144 or 200 Hz, and real applications' use of the gestures (pinch-zoom in Firefox and
Chromium, a hold stopping kinetic scrolling, GTK's gestures).

Touchscreens are `headless_touch` devices (`headless_input.c`), which put fingers down at a place
on the screen from 0 to 1 as libinput gives it. `touchscreen_smoke` checks with `input_probe`
windows and a panel that fingers reach the surface under them with surface coordinates, several
at once, past the window's edge, with frames and cancel, and focus the window but not a panel
without the keyboard; that a window without wl_touch gets the pointer and its left button from
one finger at a time, beside a finger on another window; that a tap on a traffic light closes a
window, a finger on the drag strip drags it, and a tap on the desktop leaves the pointer on
nothing; and the mapping: every output, then a built-in panel plugged in (`eDP-1`), the output a
device names, and `touch.output` by connector and by a description that matches nothing.
`touch_shell_smoke` checks that the Qt shell binds wl_touch and opens and closes the launcher on
taps, the fingers going to its layer surfaces by wl_touch.

Not checked: a real touchscreen through libinput (its device's `WL_OUTPUT`, a rotated or scaled
built-in panel, which libinput's calibration matrix from udev must match, palm rejection's
cancels), GTK, Firefox and Chromium windows and X11 applications under real fingers, and how
the cursor jumping to a finger that stands in for the pointer looks.

Drawing tablets are `headless_tablet` devices, a tablet with a pen and an eraser and a pad.
`tablet_smoke` checks with `input_probe` that a pen near a window reaches it with its position,
pressure, distance, tilt, rotation, slider and wheel (in tablet-v2's units), that its tip
focuses the window and keeps the pen past its edge, that over a window without tablet-v2 it is the
pointer, the tip and the stylus buttons its left, right and middle buttons, that the eraser
comes as a tool of its own, that the pad's buttons reach the window with the keyboard and none
other, and `tablet.output`. `touch_shell_smoke` checks that the Qt shell takes the pen through
tablet-v2 and opens and closes the launcher with its tip.

Not checked: a real tablet through libinput (Wacom's tools and their serials, a tablet's mouse
or lens, which act as absolute tools here, a pad that libinput attaches to its tablet, pad rings
and strips, which go through groups the headless pad has none of), pressure in Krita, GIMP and
Inkscape, the cursors applications set for a tool, and a tablet mapped to a rotated or scaled
monitor.

## Snapping by dragging

Added 2026-10-08. `snap_smoke` drags a probe window through a virtual pointer (the client asks
for the move as a client-decorated window does) in headless compositors: the zones and slots of
both side edges, the top and the four corners with a panel along the bottom counting as the
edge, the bottom edge alone snapping nothing, the drop placing the window in the slot and
`restore` putting it back where it floated before the drag, a snapped window getting its size
back as it is dragged away, `distance`, `corners = false` and `enabled = false`; on two monitors
of different heights, the shared edge snapping only below the shorter one and the snap going to
the monitor under the pointer; on a tiling monitor, a tile dropped at a side edge splitting the
tile there and one dropped at the top maximizing. With animations slowed to a tenth it checks
the preview growing out of the window, gliding into a corner, fading as the pointer leaves the
edge and going on the drop, its radius against the window's own once snapped, its pixels
(grim) and `preview = false`. `layout_tests` covers the zones, shared edges and the quarters'
geometry. The preview was looked at in screenshots of a headless compositor drawing through
GLES2 (rounded, with its outline) and pixman (square, the fill alone).

Not checked: the feel of the distance and the corners with a real mouse or touchpad at real
refresh rates, the preview's easing on a real display at 144 or 200 Hz, snapping across real
monitors with different scales or offsets, X11 windows dragged by their own title bars, and the
preview on NVIDIA and with the Vulkan renderer.

## Snapping from the keyboard

Added 2026-10-08. `snap_keys_smoke` runs the quarter actions and the Win+arrow cycle through
the control socket, as a binding or the macOS Window menu runs them, on two headless monitors of
different sizes: every quarter and `restore`, left and right through the halves, back to the
window's own size and on to the next monitor and back, none past the last one, up through a
quarter to maximized and down to the window's own size, quarters keeping their row, down from a
bottom quarter and from the window's own size minimizing, and a fullscreen window leaving
fullscreen first. `layout_tests` checks every step of the cycle, and `control_fuzz_smoke` sends
the new actions with odd arguments. The Window menu sending `snap_left` is in `shell_ui`.

Not checked: Super + Alt + arrows on a real keyboard (the keys reach the compositor as any other
binding, and the keyboard layout may give Alt another meaning), and the cycle across real
monitors arranged above one another, where left and right find no monitor beside.

## Snap Assist

Added 2026-10-08. `snap_assist_smoke` runs headless compositors with three probe windows, a
headless keyboard and a virtual pointer: nothing offered beside a lone window; snapping to the
left offers the other two in the right half, most recently used first, within the slot, and
Return puts the selected one there and gives it the focus, after which, with both halves taken,
it stays away; Escape, Super + Alt + Right, an action through the socket (which, snapping, brings
it back for the new slot) and `overview_cancel` dismiss it; a click beside the slot dismisses it
and reaches the window there, and a click on a thumbnail puts that window into the slot; quarters
fill in turn until no window is left to offer; a window dropped at an edge with the pointer
brings it up, and the snapped window closing takes it away; `assist = false` and a tiling monitor
keep it away. `snap_assist_shell_smoke` runs the shell with it: the overlay shows, the titles are
drawn in the slot and the search box is not (grim), and the pointer reaches the snapped window
through the overlay, which it did not before the overlay was made to take no input (checked by
running the test against the old view). It was looked at in screenshots of a headless compositor
drawing through GLES2 with the shell, and in the gallery's `snap-assist` in all four themes.

Not checked: Snap Assist on a real display (how the backdrop and the thumbnails look over real
windows and at 144 or 200 Hz), with real applications' windows and their live contents, with a
real keyboard and mouse, and across monitors of different scales.

## Volume, microphone and brightness keys

Added 2026-10-08. `volume_keys_smoke` types the six keys on a headless keyboard and sends the
actions through `paw msg` with stand-ins for `wpctl` and `brightnessctl` on `PATH`: without
a shell the compositor runs them with the step asked for, every subscriber hears the line, a
subscriber that says it is the shell gets the lines instead of the programs running, and they run
again once it has gone. `volume_keys_shell_smoke` starts the real shell, whose sound server is out
of reach, with a backlight in a made-up sysfs: the keys reach the shell, which sets the backlight
and shows the on-screen display, and runs the stand-in `wpctl`. `shell_volume_keys` checks the
shell's side on a stand-in sound server: the steps, their ends, unmuting as the volume changes,
the microphone's mute, the backlight stopping at 1 %, the display, and `osd.volume` and
`osd.brightness` turned off. The display's microphone was looked at in the preview
(`osd-microphone`).

Not checked: the keys on a real keyboard, a real backlight (this desktop has none), a real sound
server's default input (PulseAudio's source and its mute through PipeWire's pulse server), a real
`wpctl` and `brightnessctl` (their options `-l 1.0` and `--min-value=1` are from their manuals),
and logind setting a real backlight from the keys.

## Locked and repeating bindings

Added 2026-10-08. `locked_bindings_smoke` holds keys on a headless keyboard, whose test requests
now reach the lock screen as any keyboard's keys do, with stand-ins for `wpctl` and
`brightnessctl` writing down each step: a binding with `repeats` runs until its key comes up and
one without runs once however long it is held; locked by a probe holding an `ext-session-lock-v1`
lock, a repeating binding that is not `locked` stops, the bindings marked `locked` run and repeat,
the others do not and `paw msg` is still refused; once unlocked, every binding runs again.
The `config` test checks that keyboard resizing's bindings repeat unless `repeats = false`.

Not checked: a real keyboard's held keys on a real lock screen (swaylock), and how a locker that
reads the keys itself takes the ones a `locked` binding keeps from it.

## Binding modes

Added 2026-10-08. `binding_modes_smoke` types on a headless keyboard with a stand-in for `wpctl`
writing down what the mode's bindings run: outside the mode its bare keys do nothing, a binding
enters it, its keys then run and those outside it do not, one mode leads to another and Escape or
Return back out; `paw msg mode NAME` and `get mode`, the refusal of a mode that does not
exist, what a subscriber hears at each change, and the mode left by a reload and as the session
locks (a probe holding an `ext-session-lock-v1` lock). `config` and `config_diagnostics` check the
shape of `modes`, the mode action and the refusals with their lines, and `example_snippets` the
shipped configuration's resize mode uncommented. `shell_ui` checks the pill on the taskbar and the
macOS menu bar, and a click on it leaving the mode; both were looked at on a headless compositor
through grim.

Not checked: a mode in daily use with a real keyboard, over real applications that take the keys
a mode leaves them.

## Monitors turned off in the layout

Added 2026-10-08. `output_power_smoke` turns monitors off and on on a headless compositor through
wlr-output-power-management, with a probe that speaks it as wlopm does, and through the
`display_off`, `display_on` and `display_toggle` actions from the control socket and a binding:
a monitor that is off keeps its place, windows, workspace and panel, draws no frames however the
scene changes, is reported `off` by `get outputs` and to a client watching it, stays off over a
reload, and a lock taken while every monitor is off holds at once; once every one is off, a key
press (which does nothing else) or the pointer turns them on, a key coming up does not, and for a
second after the action input leaves them off; a monitor unplugged while off sends its windows to
another, and they return with it, on. `display_power_config` checks the bindings.

Not checked: any of it on real monitors, that is, what a DRM output does when it is disabled and
enabled again (the monitor's standby and how long it takes to come back, on NVIDIA and AMD, at
mixed refresh rates and scales), the real `wlopm` and swayidle with it (neither is installed
here), and night light and the magnifier on a monitor turned on again.

## Power saving when idle

Added 2026-10-08. `idle_smoke` runs the `idle` steps on a headless compositor with two monitors
and timeouts of a few seconds, against a fake `/sys/class/power_supply` (`PAW_SYSFS`): the
screens dim (half dark in a screenshot, with grim) and input brightens them; the monitors go off,
dimmed, and the screen locks with them off at once; the pointer turns them on, showing the lock;
an idle inhibitor from a probe holds every step off and the time counts from when it goes; the
key that turns the monitors on runs no binding, and a monitor turned off by hand stays off; the
battery's steps apply on battery and the others back on mains; and an `ext-idle-notify-v1` client,
as swayidle is, hears idled and resumed beside the steps. `idle_suspend_smoke` takes the suspend
step against the fake logind (`tests/fake_login1.c`): the monitors off, the lock, the suspend, and
the monitors on again as the machine wakes up. `power_supply` reads fake supplies: a mouse's
battery, a laptop on mains and on battery, a USB-C charger, two batteries.
`display_power_config` checks the settings, defaults and derived dimming.

Not checked: any of it on a real machine over the real ten minutes; real batteries and chargers
(this desktop has none, only a mouse's battery, which is rightly passed over); real video players'
and browsers' inhibitors; swayidle itself beside the steps; a real suspend through logind from
the step, and what the monitors and the locker do as the machine wakes; the dimming's look and
fade at a high refresh rate and with the GPU renderer.

## The laptop lid

Added 2026-10-08. `lid_smoke` drives a headless lid switch (`headless_switch`) beside a headless
panel named `eDP-1` and another monitor: closing the lid takes the panel out of the layout and
its window to the other monitor on the same workspace, opening it brings both back and runs the
lid's binding; with no other monitor the panel stays on, a monitor plugged in turns it off and
unplugging that one brings it back without the session ending; a lid switch that appears closed
holds the panel off, and so does a panel appearing behind it; `outputs.lid = "ignore"` leaves it
on; opening the lid turns on the monitors the idle steps turned off; tablet mode runs its binding.
`lid_logind_smoke` gives the fake logind a `LidClosed` property: closed as the compositor starts,
it holds a panel plugged in afterwards off, and its changes turn the panel on and off. `lid`
checks which connectors count as built in and when the panel goes off, and
`display_power_config` the setting and the switch bindings.

Not checked, for want of a laptop here: a real lid switch through libinput; a real logind's
`LidClosed` and its PropertiesChanged as the lid moves (this desktop's elogind offers the property,
always false; systemd-logind and elogind are both meant to emit its changes), on which a laptop
started closed on a dock relies; a real eDP panel going off and on through DRM, and the windows
and the cursor moving with it; logind's own handling beside it (`HandleLidSwitchDocked`, by
default ignoring the lid while another monitor is connected, and whether logind still counts a
monitor as docked while the idle steps or `display_off` have turned it off, or suspends the
closed laptop then); a convertible's tablet-mode switch; and a switch's binding while the session
is locked.

## Media controls

Added 2026-10-08. The now-playing card was looked at in the gallery in both themes of both styles
(`quick-settings`, with a stand-in music player and a paused browser). `media_test` checks the
model's order of players (playing, then by when they last played, then those found paused), the
player picked staying current until another starts playing, the controls reaching only what a
player offers, and the position between reads; `media_dbus_test` the MPRIS backend against
stand-in players on a private bus: players there before it and coming after, their changes and
invalidated properties, Seeked, the calls the controls make, players going, and playerctld left
out. `shell_ui` clicks the card's controls, its position and its arrows to another player against
a stand-in model, and checks `shell.widgets.media` and the Quick Settings button staying for a
player. `media_keys_smoke` presses the bound keys on a headless keyboard and hears the `media`
lines the shell would, and `media_smoke` follows them through the shell to `mpris_probe` players.

Not checked: real players (Spotify, mpv, VLC, Elisa, Firefox and Chromium tabs), their covers
from `https:` and `file:` URLs, players that give no track id or length, a real keyboard's media
keys, and players under playerctld or KDE Connect.

## Power mode

Added 2026-10-08. The tile and its list were looked at in the gallery in both themes of both
styles and both renderers (`quick-settings`, `quick-settings-power`). `power_mode_dbus_test` drives
the backend against a stand-in power-profiles-daemon on a private bus: nothing while it is absent,
the daemon coming and going, its newer name and the older one (and the newer winning while both
are owned), its announced changes, a switch reaching it as a property set, and a switch it refuses
said and undone. `shell_ui` clicks the tile and a profile against a stand-in model, and checks the
held-back performance, `shell.widgets.power_mode` and the Quick Settings button staying for it.

Not checked: a real power-profiles-daemon (this desktop runs none) with its polkit rule, its
drivers' sets of profiles (the placeholder driver's two), and a laptop's lap detection or heat
holding performance back.

## Wi-Fi

Added 2026-10-08. The Wi-Fi tile, its list and the network widget's popup were looked at in the
gallery in both themes of both styles and both renderers (`quick-settings-wifi`, `wifi`).
`wifi_test` checks the security read from NetworkManager's flags (open, Enhanced Open, WEP, WPA and
WPA2 Personal, WPA3 Personal and in transition, 802.1X and Suite B), one entry per network name at
its strongest, the order, when a password is asked for, the rows changed and moved in place, and
what the controls ask of NetworkManager. `network_manager_dbus_test` drives the backend against a
stand-in NetworkManager on a private bus, with a wired and a Wi-Fi device, six access points (one
hidden) and three known connections: NetworkManager coming and going, signals changing, access
points coming and going, the radio switched and a refusal undone, a known network activated on its
strongest access point, a new one added with a WPA3 key, a refused password asked again with the
connection added for it deleted, an error from `AddAndActivateConnection` said as given, and
disconnecting. `shell_ui` clicks through the widget's popup and Quick Settings' tile against a
stand-in model: typing a password (Connect waiting for eight characters, Enter connecting and the
card taking the keyboard back), a refused password, an open network, one that needs a sign-in,
Disconnect, the switch and the tile's chevron.

Not checked: a real NetworkManager (this desktop has none) with a real Wi-Fi card, its polkit
rules, WPA3 and Enhanced Open access points, scans as NetworkManager rate-limits them, a laptop's
rfkill switch, and the bars on the bar following a real signal.

## Bluetooth

Added 2026-10-08. The tile, the device list and a pairing question were looked at in the gallery
in both themes of both styles (`quick-settings-bluetooth`, `quick-settings-pairing`).
`bluetooth_test` checks the model's lists (the paired devices, connected first; those found while
looking, with a name, the strongest first), rows kept in place, what the controls ask of BlueZ and
when they ask nothing, and the answers to its questions. `bluez_dbus_test` drives the backend
against a stand-in BlueZ on a private bus, which calls the shell's agent as BlueZ does: BlueZ coming
and going, no agent until asked, the adapter switched and a refusal undone, connecting and a
connection refused, forgetting, looking for devices (registering the agent once, as the default),
devices and a battery coming and going, a signal dropped, pairing with a passkey confirmed (then
trusted and connected), a PIN and a passkey typed, the user's no, a code shown and a pairing
cancelled from it, BlueZ cancelling a question, and services authorized for paired devices only.
`shell_ui` clicks through the tile and the list against a stand-in model, typing a PIN.

Not checked: a real BlueZ (this desktop has no Bluetooth adapter) and real devices (headphones,
keyboards that show a code, phones that confirm a passkey, controllers), batteries BlueZ reads
from headsets, rfkill blocking the adapter, another agent (blueman, KDE's) running alongside, and a
device asking to pair by itself.

## Applications that take the shortcuts

Added 2026-10-08. `shortcuts_inhibit_smoke` types on a headless keyboard into windows of
`input_probe --inhibit`, which asks for the shortcuts through keyboard-shortcuts-inhibit and prints
the keys it gets and what the compositor says of its request: while it has the keyboard Super + T
reaches it and its binding does not run, and the bindings are back as another window has the
keyboard, as the window lets go and as it closes; Super + Shift + Escape and `paw msg
toggle_shortcuts_inhibit` turn its request off (the window hears `inactive`, the on-screen display
line says so, Escape stays the compositor's) and on again, and it stays off as focus leaves and
comes back; a window rule and `keyboard.shortcuts_inhibit = false` refuse requests, the window
never hearing `active` and the binding not turning them on, and a reload applies a change either
way; the request taking effect leaves a binding mode; the shell's notice line comes once for each
window. `xwayland_grab_smoke` has an X11 probe grab the keyboard (XGrabKeyboard) under Xwayland:
the grab reaches the compositor through xwayland-keyboard-grab, holds the keys as a request does
(the X11 window prints them), takes the binding and a window rule, and ends as the probe lets go;
a Wayland client is not offered the global. `shell_osd` checks the notice as a notification card,
and on the on-screen display under do not disturb; `config`, `keyboard_config` and
`example_snippets` the setting, the rule, the default binding and the keys it is named by.

Not checked: real applications asking (virt-manager and GNOME Boxes through GTK's keyboard grab,
QEMU's GTK and SDL windows, Remmina, FreeRDP, Moonlight, games), none of which is installed here;
X11 programs that grab the keyboard (VirtualBox, Xephyr, an X11 VNC viewer) and the X11 menus that
grab it briefly while open; the notification's look on the desktop; and a lock screen that asks for
the shortcuts itself (its `locked` bindings still run, by the code, not by a test).

## Input methods

Added 2026-10-08. `input_method_smoke` connects `input_method_probe`, an input method speaking
input-method-unstable-v2 as fcitx5 does, to windows of `input_probe --text-input`, which take
text through text-input-unstable-v3 as GTK and Qt do: a text input waits until an input method connects, then
enters the window with the keyboard and activates it with its surrounding text, change cause and
content type; committed text (Chinese among it), a preedit, the text replacing it and a deletion
reach the window, and the window's new text reaches the input method; its keyboard grab gets the
keys no binding takes, with the keymap and modifiers, while a binding still runs and the window
hears none, and a key it passes on through its virtual keyboard reaches the window and not the
grab again; its popup sits under the text cursor, moves in from the output's right edge and goes
above the cursor at its bottom edge, the input method hearing where the cursor is from it, and
grim shows it over the window and under a panel on the overlay layer, but over a search field on
the overlay layer, as the shell's start menu is, whose text input it serves; focus moving to
another window and back moves the input method with it, and a window that entered enabled is
served again as it commits; the window closing deactivates it; the session locked, it is off, its
grab hears no key and its popup is hidden, and unlocked it serves the window again; the input
method going away makes the text input leave, a second one connecting serves it, and a third is
told it is unavailable.

Not checked, none of them being installed here: fcitx5 itself (its Wayland frontend, its
candidate window, the keys it passes on, Pinyin, Mozc, Hangul), ibus (whose own Wayland frontend
is believed to speak input-method-unstable-v1 only), and real GTK 3 and 4, Qt 6, Chromium and
Electron applications typing through them, the shell's own search fields among them; and how the
candidate window looks at fractional scales and on a second monitor.

## Keeping windows above

Added 2026-10-08. `stacking_smoke` follows the stacking order through `get stacking` on a headless
compositor: a window kept above stays in front as the others are focused and raised, among those
kept above as focus raises them, through workspace switches, sticky, snapping and a group (whose
windows share it), under a fullscreen window of its monitor while another monitor has the focus
and over it once a window of its monitor is raised; a tile kept above stays a tile; the window rule
keeps one above as it opens, focused or not; and `layout.floating_above_tiles` keeps floating
windows over tiles, keeping their order as a reload turns it on and off. `window_control_smoke`
keeps a window above through the window control's version 6, and a client of version 5 hears no
such state; `shell_tasks` does it through the task model; `shell_ui` checks the entries of the
taskbar's window menu and the macOS Window menu; `session_smoke` saves and restores it.

Not checked: real applications kept above in daily use (a video's picture-in-picture, a calculator)
over fullscreen games and videos, X11 windows' own idea of their stacking beside it, and the menu
entries in the running shell on a real panel.

## Dynamic window rules

Added 2026-10-08. `dynamic_rules_smoke` changes a Wayland window's title and app ID on a headless
compositor: a dynamic rule floats it and keeps it above while its title matches, and lets go as it
stops, giving back the tile; a change by hand while held stays; a rule matching as the window
opens holds it from the start; a sticky rule matches by app ID and title together; a static rule
does not act on a new title; opacity rules follow the title; and reloads let go of what a rule no
longer holds. `dynamic_rule` tests the holding and giving back, and `config` the matching and that
a dynamic rule's other actions act once.

Not checked: X11 windows' titles changing (`WM_NAME` through XWayland, which reaches the same code),
and real applications whose titles change often (a browser, a terminal) with dynamic rules in use.

## Gradient borders

Added 2026-10-08. `gradient_border_smoke` draws borders in gradients on a headless compositor with
the pixman renderer and checks them through grim: the focused window's border red at its left,
blue at its right and between the two at the middle of its top, the other's in its colour, both
following focus; a rounded window's border clear past its outer arc and coloured on it, over the
window's square corner; rects of one colour again after a reload; and the pointer reaching the
window through its border's corners inside its geometry. `border` tests the gradient's
direction, its colours between the stops, the pieces' places and sizes, and their pixels, rounded
corners included; `config` the settings and their refusals. A screenshot of rounded gradient
borders at 45 and 90 degrees was looked at.

Not checked: any of it through the GPU renderer, where the rounded-corners patch clips the
window's content to the arc the border's corner squares are painted to (pixman leaves the content
square, so a translucent border shows the content's corner through it); fractional scales, at
which the corners are painted at the next whole scale and the scene scales them down; NVIDIA; and
the import of a real Hyprland configuration's gradients.

## Mirroring and display modes

Added 2026-10-08. `mirror_smoke` mirrors a headless output of 1024 by 768 onto another of 1280 by
720 and reads the mirror's picture with `headless_output capture`: it leaves the layout and has no
workspaces, shows the source's desktop and a fullscreen window at 0.8 of their size between black
bars, the source's (software) cursor where it is, scaled, rotated upright on a mirror turned by 90
degrees and of a source turned by 90 degrees (against grim's picture of the source), goes off and on
with its source, holds a lock up, joins the layout while its source is unplugged and leaves it as it
returns, and joins it on a reload without `mirror`. `display_mode_smoke` takes each of the four
choices with a headless `eDP-1` beside two monitors, a reload, the popup by XF86Display with the
arrows, Return and Escape, the primary monitor without a panel, and one monitor;
`display_mode_shell_smoke` shows the popup in the shell and takes duplicate by a click on it.
`shell_display_modes` reads the compositor's lines, `output_config` the settings, `import`
Hyprland's `mirror`, and the gallery pictures the popup in both styles.

Not checked, for want of the hardware here: a mirror on real monitors through DRM, with the GPU
renderers (sampling the source's scan-out buffers, implicit sync, a source and mirror on two GPUs,
where importing the buffer may fail and the mirror stays black), at different refresh rates, and
across a VT switch; a hardware cursor drawn onto the mirror (the headless backend has none, so its
cursor is in the source's frames), including a client's cursor surface and a rotated or scaled
source; a direct scan-out on the source (a fullscreen game or video) seen on the mirror; a mirror of
a source with a gamma table set by `wlsunset` (applied by the source's CRTC, so the mirror shows the
picture without it); a real laptop's lid with a projector mirroring its panel; the XF86Display key a
real laptop's Fn key sends (some send Super+P instead, which the palette takes); and kanshi or
wdisplays beside a mirror.

## Colour depth

Added 2026-10-08. `bit_depth_smoke` asks for 10 bits on three headless outputs with the pixman
renderer: two are drawn in `XRGB2101010` (one of them a mirror, in its own), and the third, whose
10 bits a test variable refuses as a monitor without them would, stays at 8 and says so; grim's
picture of a 10-bit output and the mirror's show the background's colour exactly, a
wlr-output-management change keeps the depth and a reload without `bit_depth` brings 8 back.
`output_config` reads the setting and `import` Hyprland's `bitdepth`.

Not checked: 10 bits on a real monitor through DRM, with the GLES renderer (which may take only
`XBGR2101010`) and Vulkan, on NVIDIA and AMD; that a monitor really receives 10 bits (the link's
bandwidth at a high refresh rate may hold it to 8, which wlroots does not tell); direct scan-out and
the hardware cursor beside a 10-bit primary plane; screen recording and screenshots of a 10-bit
output through the GPU (grim and the portals reading `XRGB2101010` buffers).

## Tearing

Added 2026-10-08. `tearing_smoke` runs a headless output with `windows.allow_tearing`: a
fullscreen `wayland_probe` asking for the async hint through tearing-control-v1 has its output's
frames committed with `tearing_page_flip` (counted by `get tearing`), one asking for vsync, one
asking nothing and one under a panel do not, the overview stops it while it is open, a rule's
`allow_tearing` makes a window tear that does not ask, and with the setting off nothing tears;
where the backend's test refuses the flip (`PAW_TEST_REFUSE_TEARING`) the frames are committed
as before and the log says so once. `output_config` reads the settings, and `import` Hyprland's
`allow_tearing` and `immediate`.

Not checked, for want of a real flip headless: an asynchronous page flip on a real monitor through
DRM (that the picture tears and latency falls, on AMD and on NVIDIA, whose drivers took
asynchronous atomic flips late), the kernel refusing one at commit rather than at the test, a
game asking through tearing-control-v1 (Proton, SDL, mpv's `--vo=gpu-next` with
`wayland-present-mode`), an X11 game through a rule, the interplay with adaptive sync (`vrr`),
and the hardware cursor over a tearing output.

## HDR

Added 2026-10-08. `hdr_smoke` on headless outputs with pixman: without `hdr` nothing is offered and
nothing logged; a monitor asking for HDR without it in its EDID stays SDR and says so, and one whose
EDID would offer it (`PAW_TEST_HDR`) stays SDR for the renderer, which cannot convert colours.
With `PAW_TEST_VULKAN=1` it ran here on the Vulkan renderer of an NVIDIA RTX 4090 (driver
595.99.02): color-management-v1 was offered, the headless backend's test refused the colour space,
and the monitor stayed SDR and drawn as before, its mirror's picture the background's colour, and
SDR after a reload without `hdr`. The headless backend takes no colour space, so nothing here drove
a monitor in HDR. `output_config` reads the setting and `import` Hyprland's `cm`.

Not checked, for want of an HDR monitor here: a real one driven in BT.2020 with PQ through DRM (the
connector's colour space and HDR metadata, how long the switch blanks it, on AMD, Intel and
NVIDIA); SDR windows' brightness on it; mpv (`--vo=gpu-next --target-colorspace-hint`), a game
through Proton and Gamescope describing their colours through color-management-v1, and how the
scene converts them; direct scan-out of an HDR video; the hardware cursor's colours; night light
left off it; a mirror of it converting the picture to SDR; screenshots and screen sharing of it;
and the Vulkan renderer as a daily one (wlroots calls it experimental).

## Display settings

Added 2026-10-08. `display_settings_smoke` runs three headless monitors, one a laptop's panel:
`get monitors` says what the window shows; `monitors apply` puts every monitor's settings on
trial, its milliseconds shortened by `PAW_TEST_TRIAL_MS`, taken back by `monitors revert`, by
running out and by a reload, and kept by `monitors keep` into the state file, read again on a
reload and at the next start, where a line kept for another monitor on the same connector counts
for nothing; settings that cannot be had change nothing (an unknown monitor or setting, a mirror of
a mirror, nothing left showing the desktop, a size the headless test refuses with
`PAW_TEST_REFUSE_MODE`); one refused after the test (`PAW_TEST_REFUSE_COMMIT`, as
`configure_output`'s own test failing) goes back at once; a wlr-output-management change gives way
to the window's and comes back as the trial ends; and `monitors reset`, kept, removes the file.
`display_settings_shell_smoke` opens the window in the shell with the action, takes a trial back
with Escape on its question, follows the window to the other monitor as a trial turns its own off
and back as it returns, and closes it. `shell_ui` drives the window on stand-in monitors (choosing,
dragging with the snapping outline, the keyboard on a scale, Apply, Keep, Escape, a trial running
out, a refusal, the last monitor on, HDR where it can be had, Reset to configuration, closing during
a trial), and opens it from Quick Settings, the system menu and the compositor's line;
`shell_display_settings` tests the model, `shell_display_layout` the snapping and settling, and
`output_state` the state file. The gallery pictures the window and its question in both styles.

Not checked, for want of the hardware here: a trial on real monitors through DRM, on the NVIDIA
RTX 4090 in daily use and on AMD and Intel: that a mode, refresh rate, scale or rotation the test
passes is taken as the commit is made, that one the kernel refuses at the commit after a passing
test comes back at once, and how long a modeset blanks the monitors against the 15 seconds;
several monitors' settings together beyond what each one's test checks (the window tests each
monitor as wlr-output-management's test does, not all of them in one atomic test, so a bandwidth
limit across monitors shows only as a monitor not taking its settings); adaptive sync, 10-bit
colour and HDR from the window on monitors that have them; real EDIDs' descriptions keeping a kept
line to its monitor across docking and undocking; the window on a monitor at a fractional scale;
and the window moving to another monitor as a trial turns off the one it is on.
