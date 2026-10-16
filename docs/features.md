# Features

What paw does, area by area. The default key bindings are in the [README](../README.md#default-bindings),
and every Lua setting, with its type, default and range, is in the
[configuration reference](config-reference.md).

## Shell

`paw-shell` draws the desktop and, on every monitor, either a menu bar and a dock in
[the macOS style](#the-macos-style), where the shipped configuration starts, or a taskbar with its
[start menu](#start-menu) (`shell.style = "taskbar"`, the profiles `default` and `light`). Both
have [Quick Settings](#quick-settings), the [clock's flyout](#clock-and-calendar) and the
[system tray](#system-tray). While a monitor [tiles](#tiling), its bars give way to the
[tiling bar](#the-tiling-bar), a bar in Hyprland's style. The shell draws the overlays the compositor asks for: the
[window switcher](#window-switcher), the [overview](#overview)'s text, the
[command palette](#command-palette), the [power](#power) dialog, and the
[notification cards and on-screen display](#notifications-and-on-screen-display). The desktop
shows the wallpaper and the configured launchers as shortcuts, which open on a double-click; a
right click on it offers Show desktop and the appearance profiles. A new wallpaper, from the
wallpaper picker or the configuration, fades in over the one shown once it has loaded, also from
the drawn one or to it, and one picked during the fade comes in over both. Installed applications are read
from desktop entries through GIO, and again as soon as one is installed or removed.

### The macOS style

With `shell.style = "macos"`, as in the shipped `macos-light` and `macos-dark` profiles, the
shell is laid out and drawn as macOS's, in the profile's colours, and the panel settings apply to
the dock.

- **Menu bar**, along the top of every monitor. A spiral at the left opens the system menu:
  Appearance (the profiles), Wallpaper…, Displays… (the [display
  settings](#display-settings)), Sleep, Restart…, Shut Down…, Lock Screen and Log Out…;
  the `power_menu` action opens it too. The focused application's name, in bold ("Desktop" when
  no window has focus), opens its menu: its desktop actions, New Window, Hide, Hide Others and
  Quit. The **Window** menu acts on the focused window: Minimize, Zoom, Tile Window to Left or
  Right of Screen, Enter Full Screen, Float, Keep on All Workspaces, Keep Above Others (checked
  while the window is [kept above](#keeping-windows-above)), Move to a workspace or monitor, the
  application's other windows, and Close Window. While one menu is open, pointing at
  another title or pressing Left and Right switches to it. At the right are the widgets
  `shell.widgets` puts on the bar, a search button (the command palette, drawn as Spotlight), a
  Control Center button (Quick Settings) and the date and time, which open Notification Center:
  the notifications as cards with the calendar under them, and the unread count beside the time.
  Applications' own menus (File, Edit, ...) are not shown.
- **Dock**, centred along the bottom: the Launchpad button, the pinned applications, the running
  ones that are not pinned, and the Trash (which opens `trash:///`). A dot sits under each running
  application and its name shows above it on hover. A click starts an application, its icon
  bouncing until a window opens, or brings its windows forward; one asking for attention bounces
  three times. A right click lists its windows and actions, Keep in Dock or Remove from Dock, Hide,
  Quit and Force Quit, which kills its processes as the taskbar's Kill process does. Something dragged from an application and resting on an icon brings its window
  forward, or lists its windows to rest on one, as on the [taskbar](#taskbar), its name showing
  meanwhile. `panel_height` sets its height (the icons are 16 pixels smaller), `panel_radius` its
  corners and `panel_margin.bottom` how far it floats above the edge; it is always at the bottom
  and as wide as its icons, and the desktop beside it takes clicks. Super + B (`taskbar_focus`,
  Control + F3 on macOS) walks its icons from the keyboard as on the [taskbar](#taskbar), the
  Launchpad button and the Trash among them: the icon selected is ringed and named on its tag,
  Enter clicks it, the Menu key opens its menu, and Up goes into a stack's list of windows.
- **Launchpad** (Super + R, the `launcher` action, or the dock's first button): every application
  on pages of a 7 × 5 grid over the whole screen. Type to search, use the arrows, Page Up and
  Page Down, the wheel, dragging or the dots to page, Enter to launch, and right-click for an
  application's actions or Keep in Dock; Escape clears the search and then closes it, as does a
  click beside the icons. Past the first or last page a drag stretches the pages like a rubber
  band and the wheel bumps them, rather than stop dead.
- **Spotlight** is the command palette (Super + P) as a large search field a quarter of the way
  down the screen, its results grouped by kind under headings after the top hit; **Control Center** is Quick Settings as
  rounded modules, with Display and Sound under their own headings; notifications show as banners
  at the top-right, and Alt + Tab shows large application icons in a row.
- **Menus** have dense rows, the highlighted one filled with the accent colour.
- **Wallpaper**: without `shell.wallpaper`, the shell draws an abstract one of soft hills in the
  accent's hues over a sky in `appearance.background`'s, light or dark with the profile.

The shipped macOS profiles also give windows macOS's controls and shadows
(`windows.controls = "traffic_lights"`, `round = "always"`, `shadow = { enabled = true }`, see
[Decorations](#decorations-focus-and-fullscreen)) and put GTK's close, minimize and maximize on
the left (`windows.buttons`, read as paw starts). They name the Inter font, the closest free
match for macOS's; without it the default sans-serif stands in. Switching to or from a macOS
profile re-lays the shell out at once.

### The tiling bar

While a monitor [tiles](#tiling), its taskbar (or the macOS style's dock and menu bar) gives way to
the tiling bar, a bar in Hyprland's style as Waybar draws it: thin, floating along the top, with
the workspaces at the left, the focused window's title in the middle and the system's state at
the right. Turning tiling off brings the taskbar back at once; with
`layout.tiling_per_workspace`, the bar follows the workspace shown. The windows tile in the room it
leaves, and its popups are the taskbar's: the start menu, Quick Settings, the clock's flyout with
the calendar and the notifications, the volume's, the Wi-Fi networks, the tray's menus, the
appearance profiles and the wallpapers, each opening by its module, and a right click beside the
modules opens the bar's menu (tiling off, the start menu, show desktop, the profiles). The wheel
on the bar pages through the workspaces, nudging the windows past either end as the taskbar's does.

Everything about it is set in `shell.tiling_bar`, unset values following the shell's own
(`panel_color`, `text_color`, `accent`, `font`, `font_size`), so it takes an appearance profile's
colours by itself; `shell.tiling_bar = false` keeps the taskbar while tiling. The modules stand in
three groups, `left`, `center` and `right`; a `modules` table lays out the whole bar, a group it
leaves out being empty:

| Module | What it shows, and does |
| --- | --- |
| `launcher` | The start menu (Launchpad in the macOS style), as `shell.start_icon`, else a grid in the accent colour. |
| `workspaces` | The monitor's workspaces (`shell.workspaces_shown` of them), the current one on a pill in the accent colour (`workspace_style = "numbers"`, their names from `layout.workspace_names`) or drawn long (`"dots"`); a click shows one, and resting on one shows a [picture of it](#workspace-pictures). |
| `layout` | The [tiling layout](#tiling-layouts) of the workspace shown: a click steps to the next, a right click to the one before, the wheel either way. |
| `mode` | The [binding mode](#binding-modes), while it is not the default one; a click leaves it. |
| `window` | The focused window's title, cut short past a third of the bar. |
| `clock` | The time, in `clock_format` (Qt's format: `HH:mm`, `ddd d MMM  HH:mm`, `h:mm:ss AP`); a click opens the calendar and the notifications below it, a right click toggles do-not-disturb. |
| `tray` | The [system tray](#system-tray), while it has icons. |
| `keyboard` | The [keyboard layout](#keyboard-layouts), while there are two or more; a click switches. |
| `network` | Wi-Fi, a wired link or none; a click lists the [Wi-Fi](#wi-fi) networks. |
| `volume` | The volume: a click shows each application's, a right click the outputs, a middle click mutes, the wheel turns it. |
| `battery` | The charge, only where there is a battery. |
| `notifications` | A bell with the unread count; a click opens them, a right click toggles do-not-disturb. |
| `tiling` | Tiling, which a click turns off. |
| `profiles`, `wallpapers` | The [appearance profiles](#appearance-profiles) (with two or more) and the wallpaper picker. |
| `quick_settings` | [Quick Settings](#quick-settings). |
| `power` | The [power](#power) menu by it. |

```lua
shell = {
    tiling_bar = {
        position = "top",         -- or "bottom"
        height = 34,              -- 20 to 100
        margin = { top = 6, right = 8, bottom = 0, left = 8 }, -- or one number all round
        radius = 10,
        style = "islands",        -- "bar": one bar; "islands": a pill for each group; "pills": one each
        color = "#1e1e2ecc",      -- unset: shell.panel_color
        text_color = "#cdd6f4",   -- unset: shell.text_color
        accent = "#f5c2e7",       -- unset: shell.accent
        border_width = 2,
        border_color = "#89b4fa", -- unset: the accent
        font = "JetBrainsMono Nerd Font", font_size = 13,
        spacing = 4,
        workspace_style = "dots", -- or "numbers"
        clock_format = "ddd d MMM  HH:mm",
        modules = {
            left = { "workspaces", "layout", "mode", "window" },
            center = { "clock" },
            right = { "tray", "network", "volume", "battery", "power" },
        },
    },
}
```

The taskbar is kept, hidden, while the tiling bar shows, so that the bars switch at once. The
keyboard on the bar (Super + B) walks the taskbar alone, and does nothing while the tiling bar
shows.

### Taskbar

Along the bar, from the left: the start button, one row of the pinned applications and the open
windows, the workspace indicator, the widgets placed on the bar, the tray, the Quick Settings
button, the clock, and a sliver at the end that shows the desktop. A button shows a tooltip when
the pointer rests on it, but for a window's, which shows pictures of its windows instead
([below](#window-pictures)). Lua configures the panel's height, the edge of the monitor it is
along (`panel_position`: top, bottom, left or right), margins that make it float (`panel_margin`,
one number or `{ top, right, bottom, left }`), corner radius, font and text size, colors
(`#RRGGBB`, or `#RRGGBBAA` for a translucent panel), wallpaper, a picture for the start button in
place of its four squares (`start_icon`, an SVG, PNG or other image), and pinned commands, which
run from your home directory.

Along the left or right edge the bar stands upright, as Windows 10's could: the start button at
the top, the buttons of pinned applications and windows below it as icons alone (their titles in
tooltips), then the widgets one above the other, the clock with the hours over the minutes, and
the sliver at the bottom; tooltips and popups open beside it, the clock's notifications above its
calendar.

Each monitor's bar can have its own edge: `panel_positions = { ["DP-1"] = "left", ["HDMI-A-1"] =
"top" }` names them by the monitors' names (`paw msg get outputs`), and `panel_position` stays the
edge of the monitors left out. `panel_per_monitor = false` puts every bar along `panel_position`
instead.

With `panel_draggable = true` the bar can be dragged to another edge, as on Windows with the
taskbar unlocked: press an empty part of it and drag, and an outline shows where it goes, the
edge of its monitor nearest the pointer (past an edge it shares with another monitor, that edge).
Let go there and it moves, and paw writes the new edge into your configuration file: that
monitor's `panel_positions` entry, the other monitors' bars staying where they are, or with
`panel_per_monitor = false` the `panel_position` of every bar. It goes where it takes effect: the
profile in use, when that sets it, else the `shell` table, which gains the setting (or is added)
when it lacks it. The rest of the file is left as it was, comments and all. Escape, or letting go
at its own edge, leaves the bar where it was.

The same setting lets you resize the bar by its inner edge, the one facing the desktop (its top
edge along the bottom of the monitor): the pointer turns into a resize cursor over it, and dragging
it makes the bar taller or shorter, or wider or narrower down a side, between 24 and 100 pixels.
Every monitor's bar follows as you drag, its buttons growing with it, and maximized and tiled
windows make room as it goes. Let go and paw writes the new `panel_height` into your configuration
file the way it writes the bar's edge; Escape puts the old size back. The macOS style's dock and
the tiling bar keep their sizes.

A floating bar docks while a window reaches the edge of the monitor's free area it is along (a
maximized window, one snapped or dragged against that edge, and any tile, gaps or not, so on a
tiling workspace it docks whenever a window there is not floating), as KDE's floating panels do: it eases out to fill the strip it reserves from edge to edge,
its corners squared and its outline and shadow gone but for a line along its inner edge, its
buttons where they were, and floats again once no window there does. Windows do not move.

Clicking a window's button focuses it, or minimizes it when it is focused already, as on Windows,
from any monitor's bar: it shrinks into its button, and grows out of it as it comes back
([Animations](#animations)). A window open on another workspace brings that workspace up, while a
minimized one comes back onto the workspace the clicked bar's monitor shows, taking a tile there
where it tiles, wherever it was minimized. A middle click closes the window, a right click opens its
[menu](#taskbar-menus), and dragging the button moves it along the bar. An application's windows
share one button, stacked with a count when there are several: clicking it cycles through them,
and resting on it shows them to pick one, close it with its cross or a middle click, or open its
menu (`group_windows = false` gives every window its own button). A line under a button marks its
window: long in the accent colour for the focused one, short for the others, dimmed while
minimized; a window asking for attention tints its button in the urgent colour. Installed
applications can be pinned to the taskbar from a window's menu or the start menu, and unpinned
from their button's menu; those pins are kept in `$XDG_STATE_HOME/paw/pinned`
(`~/.local/state/paw/pinned`), one desktop id per line, and stay off the desktop. Pinning
changes nothing about an application's windows: a pinned application shows its launcher while it
has no window, its first window opens in the launcher's place and any other at the end, as any
window does, and each window's button moves along the bar on its own, wherever it is dragged among
the others. With no window left, the launcher shows again in its place; dragging the launcher moves
the pin. Grouped, a pinned application's button stands for all its windows in the launcher's
place, and dragging it moves the pin.

Something dragged from an application (a file, text, a link) brings windows forward as on Windows:
resting the drag on a window's button for half a second brings that window to the front, restored
onto the workspace shown if it was minimized, else with its own workspace shown, so that the drag
goes on onto it and drops there.
Resting on a stack's button shows its windows, as the pointer resting there does, and resting on
one of them brings it forward; they stay while the drag is over them or the button, and go once it
has left both or ended. The button, picture or title under the drag is lit as under the pointer.
A drag only crossing the bar brings nothing forward, and nothing can be dropped on the bar or on
the windows it shows: the application sees the drop cancelled, so a file moved that way is not
lost, and a pinned application's button without windows does nothing with it. Once the drag ends,
the window brought forward has the keyboard.

The bar works from the keyboard too, as Windows' Win + T has it: Super + B (the `taskbar_focus`
action, or `paw msg taskbar_focus`; Super + T arranges windows here) gives the keyboard to
the bar on the focused monitor, with the button of the window that had it selected, else the
first. A ring in the accent colour marks the selection. Left and Right, Home and End move along
the buttons of windows and of pinned applications (the start button and the widgets are left out),
and a button with windows shows them at once, as resting the pointer on it does, the card gliding
from one button to the next; a pinned application's button shows its name. Enter or Space
does what a click does and gives the keyboard back: it brings the window up (a minimized one
restored onto the workspace shown, another with its own workspace shown), brings up a stack's
next window, starts a pinned application, or minimizes the window that had the keyboard, which
counts as the focused one. Up (Down on a bar along the top)
goes into the card of [pictures](#window-pictures) or a stack's list, and Down (Up) back to the
button; along a side Up and Down move along the bar, and Right (Left beside a bar along the right)
goes into the card or the list. The Menu key or Shift + F10 opens the button's
[menu](#taskbar-menus), or the menu of the
window selected on the card, with its first entry highlighted, and closing it comes back to the
bar. Escape, or Super + B again, gives the keyboard back to the window that had it and changes
nothing. Moving the pointer over the bar or the card, or a press anywhere, hands the bar back to
the pointer: what it is on shows as on hover, a click on the bar or the card goes on to what it
lands on, and one elsewhere only gives the keyboard back, as beside a menu. While the bar has the
keyboard no window has it, so no button is marked as focused. A screen reader names the selected
button or window as the selection moves.

The workspace indicator lists the monitor's workspaces, the current one on a pill, with a dot
under those with windows: click one to switch to it, or scroll anywhere on the bar to page through
them. Resting the pointer on one shows a picture of it ([below](#workspace-pictures)), and
dragging a window's button onto one moves the window there. A right click on the bar's empty space opens its menu: tiling on or off, the start menu,
show desktop, and the appearance profiles beside their entry.

The bar moves with what happens on it, briefly and without holding anything up: a button's fill
fades in under the pointer and out after it, an application's icon shrinks a little while it is
pressed, a window's button fades and grows in as the window opens and shrinks away as it closes
while the others slide over, an application just pinned grows in, the
line under a button eases to its new length and colour, the workspace pill slides to the workspace
shown, a workspace a window is dragged onto grows a little and lights up and pops as the window
drops into it, a count grows in and pops as it goes up, and an icon that follows a state (the network, the
volume, do-not-disturb, tiling) crossfades as the state changes.

### Window pictures

Resting the pointer on a window's button, or a stack's, opens a card above it (below a bar along
the top) as on Windows 11: a small picture of each window side by side, under the application's
icon and the window's title. The pictures follow the windows as they redraw while the card is
open (30 times a second at most, so a video moves), show the window alone, without its frame or
what covers it,
and show a minimized window or one on another workspace too; until one has come, the
application's icon stands in, and the first fades in over it. They are taken from halfway into
the delay, so that the card opens on them, and none is taken for a button the pointer only
crosses on its way. Alt + Tab shows the same pictures, a card for each window in the
[window switcher](#window-switcher). Clicking a picture
focuses its window, or minimizes it when it is focused already; a middle click or the cross shown
over the one under the pointer closes it, and a right click opens its [menu](#taskbar-menus). The
focused window's picture is marked as its button is. The pointer can cross from the button onto
the card, which closes a moment after it has left both, or at once when a button is pressed or a
menu opens; moving onto another button shows that one's windows at once, the card gliding over
to it and easing to their width, as it eases to a new width when a window opens or closes. A
drag resting on a stack's button opens the card too, and resting on a picture brings its window
forward ([above](#taskbar)). Dragging a picture onto a workspace of the indicator moves its window
there ([below](#workspace-pictures)). From the keyboard on the bar ([above](#taskbar)), Left and Right
move between the pictures, the selected one ringed and showing its cross; Delete closes its
window, as the cross does, and Enter brings it up. The pictures get narrower when more windows
than fit across the monitor share a button, down to 60 % of their width, and past that the stack
lists its windows by title instead. The macOS style's
dock lists a stack's windows by title, pictures or not.

While a window's process plays sound, a speaker shows beside its title on the card, as on a
browser's tab; clicking it mutes the window's sound, and the speaker stays, crossed out, while it
is muted, to unmute it. The sound is that of the sound server's streams (PulseAudio's, or
PipeWire's) played by the process that made the window, or by one it started that has no window
of its own: a browser playing from a child process shows it, and so does a terminal running a
player, unless the player has a window. Windows of one process cannot be told apart, so all of a
browser's windows show the speaker when one of its tabs plays, as do all of a terminal's that
serves every window from one process (foot's server, GNOME Terminal), and muting one mutes them
all. A stream counts while it is open and not paused: an application that keeps one open while it
plays nothing, as some games and voice-chat applications do, shows the speaker until it closes
it. An application in a sandbox with a process namespace of its own (Flatpak) names its processes
by numbers that mean nothing outside it, so its windows show no speaker.

Resting the pointer on a picture for half a second peeks at its window, as Windows' Aero Peek
does: every other window fades as for the desktop [peek](#effects), to `peek.opacity` over
`peek.duration`, and the window shows over them in full where it is, a minimized one where it
was and one on another workspace over the workspace shown. The bar and the card stay as they are.
Nothing about the window changes: it is not focused, raised, restored or moved to the workspace
shown, and while it shows only for the peek, clicks go through it. Moving onto another picture
takes the peek over at once, without the others coming back in between; leaving the pictures, or
the card closing, ends it with a fade. A picture the keyboard on the bar selects peeks the same
way, half a second after the selection comes to it, and at once while another is peeked at.
Clicking the picture focuses the window where the peek shows it. The peek also ends as the window closes or is focused another way, as the session
locks, or as the desktop peek starts. The [switcher](#window-switcher) and the
[palette](#command-palette) peek at their selected window the same way. The macOS style's dock and a stack's list of titles have no
pictures, and do not peek.

```lua
shell = {
    thumbnails = {
        enabled = true, -- false: a tooltip with the title, and a stack's list of titles
        delay = 400,    -- milliseconds the pointer rests on the button first (0 to 2000)
        size = 240,     -- width of one picture in pixels (120 to 480), 5/8 of it tall
        live = true,    -- false takes one picture of each window just before the card opens
        workspaces = true, -- false: a workspace of the indicator has its tooltip instead
    },
}
```

### Workspace pictures

Resting the pointer on a workspace of the indicator opens a card above it (below a bar along the
top), as a window's button does: a picture of the workspace under its name, its wallpaper and
the windows it shows where they are, in the order they are drawn, at the monitor's proportions.
Each window is drawn from its own picture, so the picture follows the windows as they redraw (30
times a second at most) and as they open, close, move, change size or are raised: a window that
comes onto the workspace fades in, one that moves glides there. A sticky window shows on every
workspace, a fullscreen one covers it, and minimized ones and those in the scratchpad do not
show. The workspace shown on the monitor is ringed in the accent colour, and the header says
how many windows the workspace has. The pictures are taken from halfway into the delay, so the
card opens on them; moving along the indicator shows the next workspace at once, the card gliding
over to it while one workspace's windows crossfade into the other's, and moving onto a window's
button shows its windows' card instead, and back. The pointer can cross onto the card, which
closes a moment after it has left both, or at once when a workspace is pressed. The pointer on a
window rings it and names it in the header; a click brings that window up, and a click elsewhere
on the card shows the workspace.

A window's button dragged onto a workspace of the indicator, or its picture dragged there from the
card of its windows, moves the window to that workspace of the monitor, as dropping a thumbnail
on the overview's strip does; a stack's button moves all its windows. The workspace under the
drag grows a little and lights up, and what the drag carries, the window's picture or its icon,
docks beside it, gliding from one workspace to the next. A button's drag resting there opens the
workspace's card, and the window's icon then floats into its picture; a picture's drag keeps the
card of its windows open instead. Let go there, it drops into the workspace, which pops, and an
open card shows the window arriving. Over a workspace a dragged
button goes back to its place in the row; let go anywhere else, the drag moves no window.

### Clock and calendar

Beside the time, the clock shows how many notifications have not been seen, muted (with a
crossed-out bell) while do-not-disturb is on, which a right click on it toggles. Clicking the clock
opens its flyout at the bar's right end, as on Windows 11: the
[notifications](#notifications-and-on-screen-display) on one card, and under it a calendar with
the time and today's date over the month, today marked and the weeks starting on the locale's
first day. The arrows or the mouse wheel page through the months, each sliding in; the title zooms
out to the year's months and then to a decade's years, where a pick zooms back in; Today returns
to this month. On a monitor too short for both cards, the calendar leaves out the time and the
date for the notifications, whose list scrolls in what room is left, fading out at the edge where
it goes on.

### Quick Settings

Left of the clock, as on Windows 11, the Quick Settings button shows the volume and the battery in
small icons, and the network only while its link is down, dimmed and struck through as a warning
(a link that is up shows only in Quick Settings); its wheel changes the volume and a middle click
mutes. Clicking it opens Quick Settings at the bar's right end: what is playing
([media controls](#media-controls)), tiles for [Wi-Fi](#wi-fi) where NetworkManager has a Wi-Fi
device, [Bluetooth](#bluetooth) while BlueZ has an adapter, do-not-disturb, night light (on or off
against its schedule, through the compositor), tiling on this monitor (greyed out where tiling is
not available), the [power mode](#power-mode) while power-profiles-daemon runs, the appearance
profile (listing the profiles under it) and the wallpaper (opening the same picker as the bar's
button), and without NetworkManager's Wi-Fi the network's state on a disc rather than a tile, as it
is only shown (Wi-Fi, wired or down, and the interface, or NetworkManager's name for the
connection); a brightness slider where the screen has a backlight
(set through logind's `SetBrightness`, so no privileges are needed) and under it the [display
settings](#display-settings); the volume with its mute, the
outputs to play through and each application's volume a click away; and the battery's charge
along the foot.

### Media controls

What is playing shows at the top of Quick Settings, as on Windows 11 (Now Playing in the macOS
style's Control Center): the cover, the title and the artists, the player's icon and name, where
the track is and how long it is, and previous, play or pause and next, greyed out where the player
does not offer them. A press or a drag on the position seeks, where the player can; a click on the
player's name brings its window forward. With several players, arrows beside the name step
through them, and the one picked stays until another starts playing. The card is there only
while a player is (`shell.widgets.media = false` leaves it out), and the Quick Settings button
stays on the bar for it even when every other widget is placed there. The position moves on
while the flyout is open; closed, nothing is read or redrawn.

The media keys (Play/Pause, Next, Previous and Stop, bound to the actions `media_play_pause`,
`media_next`, `media_previous` and `media_stop`, which `paw msg` runs too) control the same
player, as on Windows 11 and KDE, on the lock screen too (the shipped bindings are `locked`). The
shell finds players through MPRIS (`org.mpris.MediaPlayer2.*` on the session bus), which music and
video players offer, and browsers for a tab playing sound or video. The players playing come first,
the one that started last ahead; then those that played, by when they stopped; then those already
paused or stopped when the shell found them. A player stopped with nothing loaded is left out. The
keys do nothing while there is no player, and the shell needs Qt's D-Bus module at build time for
any (`-DPAW_MEDIA=OFF` builds without them).

### Power mode

While power-profiles-daemon runs, Quick Settings has a Power mode tile, as GNOME and KDE have: it
shows the profile in use (a leaf for Power saver, a gauge for Balanced, a bolt for Performance), lit
while that is not Balanced, and a click lists the profiles the daemon offers to pick from. Where
the daemon holds performance back (a laptop on a lap, or too hot), Performance says so. The shell
asks the daemon on the system bus (`org.freedesktop.UPower.PowerProfiles`, or
`net.hadess.PowerProfiles` before version 0.20), which lets the active session's user switch
without a password; a switch the daemon refuses is said across the panel and the tile shows the
profile in use again. Without the daemon the tile is not there, and `shell.widgets.power_mode =
false` leaves it out; `-DPAW_POWER_PROFILES=OFF` builds without it.

### Wi-Fi

Where NetworkManager runs and has a Wi-Fi device, the shell manages Wi-Fi as Windows 11 and KDE do,
through NetworkManager's D-Bus API on the system bus. In Quick Settings (with
`shell.widgets.network = "quick"`, the default) the Wi-Fi tile takes the network state's place:
a click turns the radio on or off, and its chevron lists the networks in range under it. With the
network on the bar (`"bar"`, as the macOS profiles have it), the network widget opens the same list
under a switch for the radio, as macOS's Wi-Fi menu. The list shows one entry for each network
name at its strongest access point: its signal in bars, a lock when it is secured, and whether it
is connected; the one connected comes first, the one being connected to next, the others by
signal, and the devices look for networks again as it opens. A click on a network opens it:

- one NetworkManager knows, or an open one (Enhanced Open too), connects with Connect;
- one secured with a key (WPA or WPA2 Personal, or WPA3 Personal) asks for its password first,
  Connect waiting for the eight characters WPA takes; NetworkManager adds a connection for it
  (`AddAndActivateConnection`) and keeps the password for the next time. A password it refuses
  is asked for again, the connection added for it removed, and what went wrong said across the
  panel;
- the one connected disconnects with Disconnect, and NetworkManager does not connect again by
  itself until asked;
- one that needs an enterprise sign-in (802.1X) or WEP says so, to be set up with NetworkManager's
  own tools.

NetworkManager lets the active session's user do all of this without a password, as its polkit
rules have it. A switch on the machine that holds the radio off (rfkill) greys the tile out. Hidden
networks are not listed. Without NetworkManager everything stays as described above, read from
`/sys/class/net`; `-DPAW_NETWORKMANAGER=OFF` builds without it.

### Bluetooth

While BlueZ runs with an adapter, Quick Settings has a Bluetooth tile, as Windows 11 and GNOME
have: a click turns the adapter on or off, and its chevron lists the paired devices under it, the
connected ones first, each with its kind (headphones, a speaker, a keyboard, a mouse, a phone, a
game controller, a computer) and, where BlueZ reports it, its battery's charge. The tile names the
device connected, or how many are. A click opens a device: Connect or Disconnect, and Forget, which
removes the pairing. Pair a new device looks for the devices in range while the list is open, the
strongest first and only those that give a name; a click on one and Pair pairs with it, then trusts
it (so that it connects by itself from then on) and connects. What BlueZ asks while a device pairs
shows over the list: a passkey to compare with the one the device shows, a PIN or a passkey to
type, a code to type on a keyboard, or whether a device may pair; Cancel says no, and a pairing the
user refused is not reported as a failure. Pairing "just works" where the device asks nothing.

The shell answers BlueZ as an agent of its own (`org.bluez.Agent1`, with the KeyboardDisplay
capability), registered as the default agent the first time the user looks for devices or pairs,
so that a shell never asked takes no part in pairing; a device that asks to pair by itself before
then is handled by another agent, or refused. A connection or pairing that fails is said across the
panel. Without BlueZ the tile is not there, and `shell.widgets.bluetooth = false` leaves it out;
`-DPAW_BLUETOOTH=OFF` builds without it.

### Where the widgets go

Each widget is switched off from Lua: `shell = { widgets = { battery = false, calendar = false } }`,
with `workspaces`, `battery`, `network`, `volume`, `clock`, `calendar` (the clock stays, the
calendar goes), `tiling`, `profiles` (the appearance profile picker), `wallpapers`,
`notifications` (do-not-disturb), `keyboard_layout`, `power` (the [power button](#power) in the
start menu), `tray` (the [system tray](#system-tray)), `media` (the card of what is playing, in
Quick Settings), `power_mode` (its [power mode](#power-mode) tile) and `bluetooth` (its
[Bluetooth](#bluetooth) tile) all on by default. Those that can move sit
on the bar with `"bar"` or in Quick Settings with `"quick"`, and `true` leaves them in their default
place: `network`, `battery`, `volume`, `profiles` and `notifications` are in Quick Settings and
`tiling` and `wallpapers` on the bar. Placed on the bar, each has its button there
(`notifications = "bar"` is a bell with the unread count, which the clock otherwise stands for),
and the Quick Settings button goes once nothing is placed in it:

```lua
shell = { widgets = { volume = "bar", network = "bar", battery = "bar",
                      profiles = "bar", notifications = "bar", wallpapers = "quick" } },
```

On the bar, the volume shows the default output's level (its wheel changes it, a middle click
mutes, a click lists each application's volume and a right click the outputs); the battery shows
the charge of the first battery in `/sys/class/power_supply` (red when nearly empty,
accent-coloured while charging) and is left out on machines without one; the network shows Wi-Fi,
a wired link, or a dimmed struck-through icon when the interface is down, read from
`/sys/class/net` (physical interfaces only, wired preferred), and is left out without any
interface. Both follow the kernel's notices of changes as they come. Where NetworkManager runs,
the network shows the connection the machine goes out through instead, its bars following the
Wi-Fi signal and its tip naming the network, and a click lists the [Wi-Fi](#wi-fi) networks. With
two or more
[keyboard layouts](#keyboard-layouts), the active one's short name (`us`, `no`) sits beside the
clock, and clicking it switches every keyboard to the next.

### Look and feel

Everything takes the panel's `accent`, `panel_color`, `text_color`, `font` and `font_size`:
hovering, pressing and what is open or on are shown by laying the text color over the panel at a
low opacity, which shows on a light panel as on a dark one. Popups (the start menu, menus, the
mixer, the flyouts and the rest) are drawn in the panel color made opaque, even when the bar is
translucent, unless they are frosted glass (below): a window showing through would make them hard
to read. They open in a surface of their own over the whole monitor, above fullscreen windows too, so
the start menu Super + R opens shows over a video; they fade in with a short slide from the bar and
fade out, and cast a soft shadow when the shell draws through the GPU. While one is open, the
keyboard is in it and a press anywhere but on the bar or the popup closes it without reaching what
is under it; a press on another bar button opens that one's popup at once. The overview, the
window switcher and the command palette close it.

Menus (the bar's, a window's, the tray's, the power menu, the desktop's) take the keyboard: Up and
Down (wrapping), Home and End move, Enter or Space chooses, Right opens a submenu and Left or
Escape closes it, and Escape closes the menu; resting the pointer on an entry with a submenu opens
it beside the entry, the menu staying open, and moving the pointer towards an open submenu across
other entries keeps it open. Buttons, search fields, crosses and empty lists look the same
wherever they are, and a ring in the accent colour shows the button the keyboard is at.

The overlays (the switcher, the palette, the power dialog and the overview) come in each time they
show and fade out quicker as they close: the pointer and the keyboard go to what is under them at
once, so nothing waits for the fade. All the shell's motion is short and eases out; it follows
`animations.enabled` (off makes every change instant) and `animations.speed`, and nothing moves,
or wakes the shell, while nothing changes.

`shell.frame = "windows"` frames the shell as the windows are: a floating bar, the dock and
every popup get the windows' border (`windows.border_width` pixels in `border_inactive_color`,
a gradient's first colour) in place of the shell's hairline, and with `windows.shadow` on, its
shadow in place of the shell's own: the focused window's under a popup, an unfocused window's
under the bar and the dock, as soft, as far off and as dark. So a profile with hard shadows
(`blur = 0` and an offset) gives the shell the same printed look as its windows. The bar's
surface reaches past the strip it reserves as far as its shadow falls towards the middle of the
output, so windows still come up to the bar; that room takes no input.

```lua
windows = { border_width = 2, border_inactive_color = "#2b221c",
            shadow = { enabled = true, color = "#2b221cff", inactive_color = "#2b221c66",
                       blur = 0, offset = { 7, 7 } } },
shell = { frame = "windows", panel_position = "top", panel_radius = 12,
          panel_margin = { top = 14, left = 26, right = 26, bottom = 10 } },
```

`shell.blur` makes the shell frosted glass: the bar (the taskbar, the tiling bar, the macOS
style's menu bar and dock) and the popups (the start menu and Launchpad, menus, Quick Settings and
Control Center, the clock's flyout and Notification Center, the notification cards, the on-screen
display, the command palette, the window switcher and the window pictures' cards, the clipboard,
emoji and wallpaper pickers, tooltips, the power dialog and the rest) show the wallpaper behind
them blurred, under their colour. The bar keeps `panel_color`'s translucency; the popups take it
too, but stay at least 75 % opaque, so their text reads over a bright wallpaper; the cards and
fields inside them are veils that the glass shows through too. A pane of glass catches the light:
a faint light edge inside each popup's outline and a sheen down from its top. It is the wallpaper
that shows through, as the desktop draws it on that monitor (a picked wallpaper, the macOS
style's drawn one, the background's gradient), not the windows behind, as Hyprland's xray: it is
blurred once each time it changes, for each monitor, and every surface on that monitor shows the
part of it that lies behind it, following a popup as it slides in. `radius` is how far the blur
spreads (40 pixels by default), `brightness` and `saturation` darken, lighten or colour the
blurred wallpaper, which helps text on it stand out. An appearance profile can turn it on and off;
an opaque `panel_color` hides it. It is drawn through the GPU: with `renderer = "software"` the
shell looks as it does without it.

```lua
shell = { panel_color = "#1e1e2eb3", text_color = "#cdd6f4", accent = "#cba6f7",
          blur = { enabled = true, radius = 40, brightness = 0.9, saturation = 1.2 } },
```

The shell draws through the GPU; `renderer = "software"` draws on the CPU instead, for a weak
machine: it starts faster and uses less memory, but cannot draw effects such as shadows or frosted
glass. In a
nested session, applications that reuse an existing process or D-Bus service can open in the host
session instead.

### Application icons

The shell finds the application a window belongs to by its app id, so that its button, dock icon,
switcher entry and menu show the application's icon and name. Most windows give their desktop
file's name, but many spell it otherwise, and the shell tries each way from the most exact down:
the desktop file's name in any case, its `StartupWMClass` (as X11 programs, Electron apps and Wine
give it), a Steam game's number (`steam_app_570` for the game's shortcut), the last part of a
reverse-DNS name (`nautilus` for `org.gnome.Nautilus`), the name without what programs add to it
(a version as in `gimp-2.10`, `-bin`, `-desktop`, `-browser`, an AppImage's architecture, NixOS's
`.name-wrapped`), the program the entry runs (`gnome-calculator`, `obs`), the vendor's domain the
entry lacks (`org.xfce.Thunar` for `thunar.desktop`), and the entry's name; a few programs whose
windows name nothing like their entries (`steamwebhelper`, `soffice`) are known by name. A window
whose program has an entry the menus leave out (a password prompt's, a portal's file dialog's)
gets that entry's icon, and one with no entry an icon the theme has by a name guessed the same
ways (`steam_icon_570`), else a generic one.

Icons come from the icon theme the desktop names: Qt's platform theme's (qt6ct, KDE), else GNOME's
`icon-theme` setting where it is set (GNOME Settings, `gsettings`, nwg-look), else
`gtk-icon-theme-name` in GTK's `settings.ini`, else KDE's `kdeglobals`. After it the shell looks
in Breeze, Adwaita or Papirus, whichever is installed, so that the generic icons applications and
the shell name are found under any theme. A change of theme shows after the shell restarts.

### Taskbar menus

Right-clicking a window's button opens its menu, headed by the application's icon and name (from
its desktop entry; the window's app id without one) over the window's title. Then come:

- the application's desktop actions (Firefox's "New Private Window"), with their icons, and
  **New window**, which starts the application again (left out when one of its own actions is a
  new window);
- **Minimize**, or **Restore** for a minimized window, which is all a minimized one offers of
  these three; **Maximize**, or **Restore** for a maximized one; **Fullscreen**, checked while it
  is;
- **Move to workspace**, beside it the workspaces of its monitor (named as
  `layout.workspace_names` names them), the one it is on marked; **Move to monitor**, with two or
  more monitors, from left to right; **Keep on all workspaces**, making it
  [sticky](#sticky-windows) (not with `features = { sticky = false }`); **Keep above others**,
  checked while it is [kept above](#keeping-windows-above) the other windows; and where its
  workspace tiles, **Float**, checked while it is kept out of the tiling;
- **Pin to taskbar** or **Unpin from taskbar** for an installed application, **Close window**
  in the danger colour, and last **Kill process**, which ends the process that made the window
  with `SIGKILL`, for one that does not close (only once its process is known; never the shell's
  or the compositor's).

None of these focuses the window or raises it: a focused window moved off the workspace on screen
passes the focus on, as `move_to_workspace` does. The window menu names the window itself, never
by its title, so it acts on the right one when several share a title.

A stacked button's menu is about all its windows: its title counts them, and it offers
**Minimize all** (or **Restore all**), moving them all to a workspace or monitor (their own
marked when they share it), **Close all 2 windows**, and **Kill process** (or **Kill 2
processes** when they come from more than one). Right-clicking a window in its hover
list opens that window's own menu. A pinned application's button without windows offers its
desktop actions, **Open** and **Unpin from taskbar**; the bar's own menu has icons too.

These menus open away from the bar, so along the top the title is nearest it and **Kill
process** farthest. `shell.mirror_menus = true` turns them around there, so they read outwards
from the bar as they do along the bottom: **Kill process** and **Close window** nearest the bar,
the title last. Submenus keep their order.

### Start menu

The bar's first button (or Super + R) opens the start menu, after Windows 11's, above the button.
Its search field is on top and has the keyboard, so typing searches at once; along the bottom are
the user's name and picture and the [power button](#power). Under the search field are the
applications pinned to the start menu, six to a row on pages of up to three rows (the wheel or the
dots beside them turn the pages; dragging a tile moves it), and under them Recent: the
applications launched lately, with how long ago ("5 min ago", "Yesterday"). All apps › lists every
application from A to Z under its letter, and clicking a letter shows them all to jump to one.

The search finds applications by name, generic name ("Web Browser"), keywords, desktop id and
comment, open windows, workspaces and actions as the [command palette](#command-palette) does
(its `>`, `@`, `#` and `/` too), and [files](#files), grouped under Best match, Apps, Open windows,
Actions and Files, with Search the web last (see [Web search](#web-search)). The best
match is on a card of its own, with an application's desktop actions ("New Private Window") as
buttons beside Open; a calculation's value is the best match, with Copy (see
[Calculator](#calculator)). Among equal matches, what is launched more often comes first; what matches
far worse than the best match is left out. A search that finds nothing offers the web search as
its best match, or, without one, says it found nothing, with the prefixes that narrow a search.

The arrows, Tab and Page Up and Page Down move through the tiles, the lists and the results, and
Enter opens what the keyboard is at (the best match, as the search starts); at the best match,
Right and Tab move on to its buttons (Open, then its desktop actions) and Left back, Enter
pressing the one ringed. Escape clears the search, then closes the menu. Right-clicking an
application, or the menu key, gives its menu: Open, its desktop actions, Pin to Start or Unpin
from Start, a tile's Move to front, and Pin to taskbar or Unpin from taskbar.

The start menu's pins are its own, as on Windows, kept in `$XDG_STATE_HOME/paw/start-pinned`,
a desktop id per line. Until they are first changed they are the taskbar's pins, then common
applications up to six: the default web browser, file manager, a terminal, text editor, mail
client, image viewer and media player. Every launch of an installed application from the shell
(the start menu, the taskbar, the palette, desktop actions) is counted in
`$XDG_STATE_HOME/paw/launches`: its desktop id, how many times and when last, the 200 most
recent kept. Applications installed or removed show at once, without a refresh. The picture is
`~/.face` or `~/.face.icon`, else AccountsService's when the shell is built with D-Bus, else the
name's first letter on the accent colour; the name is the full name from the password database,
else the login name.

## Windows

### Decorations, focus and fullscreen

Windows that leave decorations to the window manager (Wayland applications that support
server-side decorations, such as kitty, and X11 applications such as Spotify) get no title
bar. Instead, a strip of three flat buttons sits over their top-right corner (minimize,
fullscreen, and close, from left to right) and appears when the pointer nears that corner, so
it never covers text. With `windows.controls = "traffic_lights"` they are macOS's red, yellow and
green circles (close, minimize, fullscreen) at the top-left instead, grey while the window has
no focus and showing their symbols while the pointer is on one. Dragging the window's top edge (its top 6 pixels, as a title bar would;
`windows.drag_strip` changes how many) moves the window; a file or text dragged from another
window over it goes to the window. In a terminal, holding the middle button and dragging moves
the window from anywhere on it, while a middle click let go in place still pastes
(`mouse.middle_drag` lists the windows; a modifier held passes the press to the terminal). Other
windows, and windows that ask to draw their own frame, decorate themselves. The `minimize` action (`paw msg minimize`, or a binding) minimizes the focused window
as its minimize button does, into its taskbar button (see [Animations](#animations)).

Dropping a dragged window with the pointer at the top edge of the screen, or on a panel along
it, maximizes it below the panels, and at a side or in a corner snaps it into that half or
quarter (see [Snapping](#snapping)); dragging it away again restores its earlier size. Hovering a
window focuses it without raising it (`mouse.focus_follows = false` turns this off), except
while dragging, while a menu or popup is open, or while a panel or launcher has the keyboard.

A window you fullscreen with a binding or its title bar fills its monitor except the panel and
other bars, which stay shown. Fullscreen a program asks for itself, like a video player or a
browser's fullscreen video, covers the whole monitor, panels included, and keeps covering it
while you work on another monitor; bringing another window forward on its monitor puts it
behind that window and shows the panels again. A new window opens in front of it the same way,
and it stays fullscreen behind; only a new tile, on a workspace that tiles, takes it out of
fullscreen instead of opening under it, and turning tiling on takes it out of fullscreen into a
tile, in front of the others or behind them. Either kind stays in front of the other windows
of its monitor, those [kept above](#keeping-windows-above) too, while you work on another
monitor, until a window of its own monitor is brought forward. Behind that window, it takes no
focus from the pointer hovering it, as the focus would bring it back in front: a click on it, its
taskbar button or the keyboard does that.

Limitations: window placement during interactive resize is immediate, without waiting for the
client's next buffer.

### Tearing

A game can ask to have its frames shown as soon as it draws them instead of at the monitor's next
refresh (tearing-control-v1), for the least latency, as Hyprland's `allow_tearing` and its
`immediate` window rule allow. paw does so only with `windows.allow_tearing = true`, and then
only while the window is fullscreen and nothing else is drawn on its monitor: no panel over it
(a fullscreen the game asked for itself covers them), no notification, menu, on-screen display,
overview, switcher or other window, the screen not locked and not magnified. The picture may tear
then, a line where one frame meets the next. A rule's `allow_tearing = true` does the same for a
window that does not ask, as an X11 game run through Wine or Proton cannot:

```lua
windows = {
    allow_tearing = true,
    rules = { { app_id = "^cs2$", allow_tearing = true } },
},
```

Where the GPU or its driver refuses an asynchronous page flip, the frames go out at the refresh
as before, and the log says so once. `paw msg get tearing` prints a line per monitor: what
its frames do (`tearing`, `refused`, or why not: `off`, `no fullscreen window`, `not asked`,
`covered`, `overlay`, `locked`, `magnified`), how many were flipped at once and refused, and the
window's title.

Actions without a section of their own: `quit` ends the session (Super + M), and `snap_left` and
`snap_right` fill half of the monitor with the focused window (no default binding; see
[Snapping](#snapping) for the quarters and Super + Alt + arrows). `focus_last` (Super + `) focuses the window focused before this one, on any workspace; repeated,
it flips between two windows.

### Keeping windows above

`toggle_above` (Super + Ctrl + T, the keys of PowerToys' Always on Top) keeps the focused window
above the others, as KDE's Keep Above Others, Hyprland's pinned windows and PowerToys do: it stays
over the floating windows and tiles of its monitor while they are focused and brought forward,
and toggled again it goes back among them, in front. Windows kept above stack among themselves as focus raises them. A window
keeps it through workspace switches, snapping, tiling (a tile kept above stays a tile), being
sticky, and window groups, whose windows share it as they share their slot: a window joining a
group takes the group's, and toggling one toggles them all. A fullscreen window covers the
windows kept above on its monitor; the panels, their popups and menus, and the overlays (the
switcher, the start menu, notifications) stay over all of them. A window's menu on the taskbar
has **Keep above others** and the macOS style's Window menu **Keep Above Others**, checked while
the window is kept above, and the window rule `above = true` keeps a window above as it opens.

### Borders, opacity and rounded corners

`layout.gap` sets the space around tiles; `gap_inner` (between windows) and `gap_outer`
(at the output's edges) set them separately, and a tile alone on its workspace has none unless
`layout.smart_gaps = false` (see [Tiling](#tiling)). Hyprland's `gaps_in` is half of `gap_inner`,
since Hyprland adds it on both sides. The `windows` table draws a border around each window
(`border_width`, `border_color` for the focused one, `border_inactive_color`) and sets
`opacity` and `inactive_opacity`, per application too with `rules`. Fullscreen windows have no
border and stay opaque. Windows on a monitor with tiling on (floating ones too) and their
border get rounded corners (`corner_radius`, 10 by default, 0 for square ones) when wlroots is
built with `packaging/patches/wlroots-rounded-corners.patch`; stock wlroots keeps them square.
`round = "always"` rounds every window, on any monitor, but for one that draws a shadow of its
own around it (a GTK frame), which keeps its own corners; fullscreen and maximized windows stay
square.

Either border colour may be a gradient, as Hyprland's `col.active_border = rgba(33ccffee)
rgba(00ff99ee) 45deg` is:

```lua
windows = {
    border_width = 3,
    border_color = { "#33ccffee", "#00ff99ee", angle = 45 },
    border_inactive_color = { "#595959aa", "#303030aa", angle = 90 },
},
```

A gradient runs through 2 to 10 colours spaced evenly, at `angle` degrees: 0 (the default) from
left to right, 90 from top to bottom, measured on the box of the window and its border as if it
were square, so that 45 runs from the top-left corner to the bottom-right one whatever the
window's shape, as in Hyprland. Colours mix as they are, premultiplied by their alpha. While
either colour is a gradient, every border is drawn in pieces: a strip along each side holding the
colours along it, which the compositor stretches over the side, as a border hardly changes across
its width, and a square at each corner painted pixel by pixel, rounded with the window. A border
eases from the inactive gradient to the active one as its window is focused; an urgent window's
stays in the urgent colour. `paw msg get frames` ends each line with how the border is
drawn: `none`, `color` or `gradient`.

`windows.shadow = { enabled = true }` draws a soft shadow under each window that has none of its
own, following its corners: `color` under the focused window, the lighter `inactive_color`
under the others, fading out over `blur` pixels and falling `offset` pixels down (or
`{ x, y }`). Fullscreen and maximized windows have none, and a shadow takes no input and is no
part of the window's size, so snapping, tiling and the magnet ignore it. The shadow is cut from
one blurred image, so resizing a window costs nothing. While a window is
moved with the pointer its shadow grows softer and falls further, or a printed one moves further
off (`windows.lift`, see [Animations](#animations)); stretching the same image away from the
window does that without blurring again.

`windows.blur = { enabled = true }` blurs the desktop's background behind each window, as
Hyprland's blur does with `xray = true`: a window that lets what is under it show through
(`opacity` or `inactive_opacity` below 1, a rule's opacity, or a terminal's own background
opacity, as kitty's `background_opacity`) shows the wallpaper frosted under it, inside its
rounded corners, rather than sharp, and never the windows behind it. `radius` sets how soft it
is (each point spreads over about that many pixels, 30 by default), `noise` a little grain so
that dark gradients show no bands, and `brightness` and `saturation` scale its colours (above 1,
the vivid look of macOS's vibrancy). The window's own opacity leaves the blur whole under it.

Only the background is blurred, the wallpaper (the shell's, or any background layer surface)
over `appearance.background`: drawn at half the monitor's pixels and blurred once each time it
changes, once a wallpaper fading in or a profile switching settles, never on every frame. Each
window shows the part of that picture it covers on the screen, wherever it is drawn, so moving,
resizing and animating windows costs nothing more. Fullscreen windows have none, a window across
two monitors shows the blur of the one under its middle (on that monitor alone), and
`windows.rules[].blur` turns it off (`false`) or on (`true`, with `enabled` off) for the windows
a rule matches. Profiles can set it, so a translucent look can bring its blur along:

```lua
profiles = {
    glass = {
        windows = {
            opacity = 0.92, inactive_opacity = 0.85,
            blur = { enabled = true, radius = 40, brightness = 0.9, saturation = 1.2 },
        },
    },
},
```

`paw msg get blur` lists each monitor's picture and the part of it under each window.

### Window rules

Each entry of `windows.rules` matches windows by `app_id`, `title`, or both, each a regular
expression searched in the window's app ID or title (X11 windows match their `WM_CLASS` class
as `app_id`). A window takes its `opacity` and `inactive_opacity` from the first matching rule
that sets either. Rules can also act on a window once, as it opens (like sway's `for_window`
and `assign`):

```lua
windows = {
    rules = {
        { app_id = "^org.pulseaudio.pavucontrol$", floating = true, size = { 800, 500 },
          position = "center" },
        { app_id = "^thunderbird$", workspace = 2, focus = false },
        { title = "^Picture-in-Picture$", floating = true, size = { 480, 270 },
          position = { 1400, 40 }, focus = false },
        { app_id = "^mpv$", output = "HDMI-A-1", fullscreen = true },
    },
},
```

| Action | Effect |
| --- | --- |
| `floating = true` / `false` | Keeps the window out of the tiling, or tiles it even if it is a dialog. |
| `workspace = N` | Opens it on workspace N of its monitor, without switching there or focusing it. |
| `output = "NAME"` | Opens it on that monitor: a connector name, or `"desc:"` and the start of its "make model serial", as in `outputs.monitors`. A monitor that is not connected is ignored. |
| `size = { w, h }` | Its floating size (also `{ width = w, height = h }`), at most the usable area. |
| `position = "center"` or `{ x, y }` | Its floating place, centred or from the top-left of the monitor's usable area (panels excluded; also `{ x = x, y = y }`). |
| `fullscreen = true` | Opens it fullscreen. |
| `maximize = true` | Opens it maximized, floating over the tiles. |
| `focus = false` | Leaves the focus where it was. A fullscreen window on the current workspace still takes it. |
| `sticky = true` | Opens it sticky (see below) on its monitor's current workspace, whatever `workspace` says. Ignored with `features = { sticky = false }`. |
| `above = true` | Opens it [kept above](#keeping-windows-above) the other windows, as `toggle_above` does. |
| `allow_tearing = true` | With `windows.allow_tearing`, shows its frames at once while it is fullscreen and alone on its monitor, whether or not it asks for that (see [Tearing](#tearing)). |

Every matching rule applies, in order; where two set the same action, the later one wins. A
tiled window keeps `size` and `position` as the place it floats to when toggled. Rules see the
app ID and title the window has when it opens; later title changes do not apply them again,
but for opacity, which follows the title and app ID as they change, and dynamic rules.
`features = { window_rules = false }` turns the actions off and leaves opacity rules working.

A rule with `dynamic = true` follows the window, as Hyprland's dynamic rules do: while the
window's title and app ID match it, as they change, it holds the window to its `floating`,
`sticky` and `above`, and once they no longer match the window gets back what it had without the
rule. A browser's video call floats over the other windows while its title says "Meeting", and
tiles again as the call ends:

```lua
windows = {
    rules = {
        { app_id = "^firefox$", title = "Meeting", dynamic = true, floating = true, above = true },
    },
},
```

The rule holds the window from the moment it matches, as the window opens too. Changed by hand
meanwhile (toggled with Super + V, Super + Shift + P or Super + Ctrl + T), the window keeps the
change, also as the rule stops matching; matching anew, the rule holds it again. Its other
actions (`workspace`, `output`, `size`, `position`, `fullscreen`, `maximize`, `focus`) still act
once, as the window opens, if it matches then. A window in a group stays as its group has it,
and one in the scratchpad or hidden by a window it started stays as it is. A reload applies the
rules as they are now, letting go of what a rule no longer holds; so does `features = {
window_rules = false }`. `paw msg get dynamic_rules` says, per window, what the dynamic rules
decide and what they hold it to.

### Window placement

`windows.placement` chooses where a new floating window opens (a tile goes where the layout
puts it, and a window rule's `position` wins over all of these). `"cascade"`, the default, steps
each window 32 pixels down and right from 40 pixels in, one step for every window already open,
wrapping after eight. `"center"` opens it in the middle of the area the panels leave free on
the pointer's monitor. `"smart"` looks for the place where it covers the other windows on that
monitor and workspace least: it tries the positions flush against the screen's and the windows'
edges and in the middle of each gap between them, takes the one that covers nothing with the
most room around it (a window beside another lands in the middle of the free part of the
screen, the first window in the middle of the screen), and cascades when there is no free space
for it. Windows on other workspaces and monitors, and hidden ones, do not count. The choice is
made from the size the window asked for when it opens; the algorithm has unit tests
(`tests/window_placement_tests.cpp`) and the modes are checked headless with a panel
(`tests/placement_smoke.py`).

### Snapping

A floating window dragged with the pointer (Super + drag, a client-decorated window's own title
bar, or the drag strip of one paw decorates) snaps when it is dropped at an edge of its
monitor, as Windows' Aero Snap and KWin's quick tiling do: at the left or right edge it fills
that half, in a corner that quarter, and at the top edge it is maximized below the panels. The
pointer decides, not the window: it has to be within `windows.snap.distance` (8 pixels) of the
edge, or on a panel along it, and in a corner within that distance of both edges. The halves
and quarters keep `layout.gap_outer` from the screen's edges and `gap_inner` between them, as
`snap_left` and `snap_right` do, and they follow the panels as those come and go.

While a drop would snap, a translucent preview of the slot shows under the dragged window, in
`windows.snap.color` (the focused border's colour, a quarter opaque) with an outline of the same
colour, more opaque, and rounded as a window there would be. It grows out of the window into
the slot as the pointer reaches the edge, glides to the next slot as the pointer moves into a
corner, fades out where it is as the pointer leaves the edge, and fades out under the window
dropped into the slot as the window glides there from where it was dropped.
It moves with the duration and curve of `animations.move` and fades with those of
`animations.close`, at once with animations off. The outline needs the GLES2 renderer and
wlroots with the rounded-corners patch; elsewhere the preview is the fill alone, and square
without the patch.

An edge shared with another monitor does not snap where the other monitor continues, so the
pointer crosses it, and a window snaps on the monitor under the pointer, even one it mostly
lies beside. Dragging a snapped or maximized window gives it back the size it had before as
it starts to move, the pointer keeping its place across it, and `restore` puts a window snapped
by dragging back where it floated before the drag. On a monitor that tiles, a tile lifted out
with Super + drag goes back into the tiling where it is dropped, splitting the tile under the
pointer, at a side edge too; dropping it at the top edge still maximizes it, floating. A window
floating on purpose there snaps as on any other monitor. The magnetic edges below work
alongside: the window sticks to an edge while it is dragged, and the pointer decides the snap
when it is dropped.

From the keyboard, `snap_left` and `snap_right` put the focused window into that half of its
monitor, `snap_top_left`, `snap_top_right`, `snap_bottom_left` and `snap_bottom_right` into that
quarter, and `maximize` fills it; `restore` gives a snapped or maximized window its size and
place back. Snapping a window to the half it is in already moves it to the near half of the next
monitor that way. These float a tile, as dragging one does.

`snap_cycle_left`, `snap_cycle_right`, `snap_cycle_up` and `snap_cycle_down` (Super + Alt +
arrows) step the focused window as Windows' Win + arrows do. Left and right put a window at its
own size or maximized into that half; from the other half they give it its own size back, and
from that half they move it on to the next monitor that way, into the half facing back (where
there is none, nothing happens). Up maximizes a window at its own size, and down minimizes it; down gives a maximized
window its own size back. Up and down take a half to its top or bottom quarter and a quarter
back to its half; up from a top quarter maximizes, and down from a bottom quarter minimizes.
Left and right keep a quarter in its row, moving it on to the next monitor from its own side. A
fullscreen window leaves fullscreen first, and a tile counts as a window at its own size.

The macOS style's Window menu (Tile Window to Left or Right of Screen) runs `snap_left` and
`snap_right` for the window it names, as the bindings do, and the command palette offers the
halves and quarters. `tests/snap_keys_smoke.py` steps a window through the quarters and the
cycle on two monitors, and the cycle has unit tests in `tests/layout_tests.cpp`.

`windows.snap = { enabled = false }` turns snapping by dragging off, the top edge too;
`corners = false` leaves the corners to the side edges; `preview = false` snaps without the
preview. `paw msg get snap` says which zone the pointer is in while a window is moved, the
slot a drop gives, and where the preview is drawn and how far it has faded in.
`tests/snap_smoke.py` checks the zones, slots and drops with a virtual pointer on one and on two
monitors, a panel, a tiling monitor, the settings, and the preview's easing, corners and pixels;
the zones have unit tests in `tests/layout_tests.cpp`.

### Snap Assist

After a window snaps into a half or a quarter of its monitor, dragged to an edge, with a snap
action or Super + Alt + arrows, or from the macOS style's Window menu, the free part beside it
shows the monitor's other windows as live thumbnails, as Windows' Snap Assist does: for a half,
the other half; for a quarter, the quarter beside it in its row, else the one above or below it,
else the one across; parts that other snapped windows hold are not free. It lists the windows on
the monitor's workspace, the most recently used first, minimized ones included, but not those
snapped into a half or a quarter already, nor fullscreen ones.

Return, or a click on a thumbnail, puts that window there (back from minimized) and gives it the
focus. Snapped there, it brings Snap Assist back for the next free part while there are windows
left to offer, so that three more windows fill the quarters around a first one in turn. The
arrows, Tab, Home and End move the selection, and Delete or a middle click closes a window.
Escape, a click anywhere else (which goes on to what is there), any other binding and any other
action from the control socket dismiss it and leave the part empty; so does the snapped window
closing.

It is the [overview](#overview) in that part alone: the compositor draws the thumbnails over the
part darkened and rounded as a window there would be, and the shell draws the titles and a hint,
without the search. The rest of the screen keeps working, the pointer reaching the windows beside
it through the shell's overlay, but the keyboard is Snap Assist's until it goes. It does not open
on a monitor that tiles, whose tiling places windows itself, nor with no other window to offer.
`windows.snap = { assist = false }` turns it off. `paw msg get overview` describes it,
beginning with an `overview-assist` line (the slot, in the monitor's coordinates), and
`overview_confirm`, `overview_cancel` and `overview select N` drive it as they drive the overview.
`tests/snap_assist_smoke.py` tests it headless with a keyboard and a pointer, and
`tests/snap_assist_shell_smoke.py` with the shell; `snap-assist` previews the shell's part
(`--preview-popup`).

### Magnetic edges

A floating window that is dragged (Alt + left drag, or a client-decorated window's own title
bar) or resized (Alt + right drag, or its border) sticks to nearby edges, so windows line up
without pixel hunting. Within `windows.magnet.distance` (12 px by default) an edge of the window
lands on the edge of the output, of the area the panels leave free, or of another window, and a
translucent guide line (`guide_color`) is drawn along the edge that holds it, from one window to
the other. It works per axis (a corner can catch both), and against a window only where the two
overlap in the other direction, so a window far above or below does not pull. The window follows
the pointer's own position, so it stays held until the pointer has moved the distance away, then
follows it again; hold the `bypass` modifier (Shift by default; `"none"` for no modifier) to
drag freely. `windows.magnet = { enabled = false }` turns it off, and `guides = false` leaves
the lines out. Tiles and windows lifted out of the tiling to be dropped somewhere are not
affected, and dropping a window at an edge of the screen still snaps it (that goes by the
pointer). A window [lifted](#animations) as it is dragged sets down onto the edge that holds it,
and lifts again as the pointer pulls it away. When resizing, only the edges being pulled stick.
It does not act on keyboard moves or resizes, or on snap actions. Tested headless with a virtual
pointer and keyboard, including the guide's pixels (`tests/magnet_smoke.py`); the feel of the
distance with a real mouse has not been judged.

### Window groups

A group puts several windows in one slot, a tile or a floating place, and shows one at a time.
`group_toggle` (Super + G) makes the focused window a group of one; with `features.group_join_new`
(on by default) every window that opens while a group has focus joins it as a new tab and takes
its slot. `group_next` and `group_prev` (Super + ] and [) step through the tabs, wrapping, and
`ungroup` (Super + Shift + G) takes the focused window out into a tile of its own beside the
group. `group_merge_left`, `group_merge_right`, `group_merge_up` and `group_merge_down` move the focused window into the group of
the window beside it in that direction (making that window a group if it is none), where it
becomes the shown tab; they have no default binding. `group_toggle` inside a group dissolves it:
the hidden windows tile beside the shown one again. Closing the shown window shows the next tab,
and a group left with a single window dissolves.

The group's windows are drawn as a strip of tabs, a thin bar over the top edge of the shown
window, one segment per member with the shown one lit. A click on a segment brings that window
forward. The strip carries no titles: the compositor has no text rendering, so the taskbar (which
lists every member, and switches tabs when a hidden one is activated) and the window switcher
name the windows. `paw msg get windows` ends each line with the window's group number (0 for
none). Hidden members are off the screen, in no tiling, and follow the shown window to another
workspace or monitor. `features.groups = false` turns the actions off and dissolves every group
on reload. `session save` keeps only the shown window of a group, not the group.

### Urgent windows

An application that is not focused can ask for attention: a chat client with a new message, a
terminal's bell, a browser opening a link from another program. Wayland clients do it with
xdg-activation, X11 clients with `_NET_WM_STATE_DEMANDS_ATTENTION` or the urgency flag of
`WM_HINTS`. `windows.activation` decides what happens:

| value | effect |
|-------|--------|
| `"urgent"` (default) | The window is marked urgent and focus stays where it is, so nothing steals it. |
| `"focus"` | The window is focused, switching to its workspace, as before this setting existed. |
| `"ignore"` | The request is dropped. |

An urgent window's border pulses for four seconds in `windows.urgent_color` (orange by default)
and then holds it, also on windows without a `border_width`, where a two pixel frame is drawn
inside the window's edge without moving anything. Its taskbar button (or the stack it is in) gets
a tinted background and a pulsing dot, the workspace indicator marks its workspace, the overview
frames its thumbnail and colours its workspace, the window switcher puts a dot on its icon and
tints its cell, and the command palette lists it first with "needs attention" and a dot on its
icon. `focus_urgent` (Super + U, `paw msg focus_urgent`) focuses the window that
has been urgent the longest, switching workspace and showing it if it was minimized or in the
scratchpad; a window that gets focus any other way, or closes, is no longer urgent. The focused
window is never urgent, and a client that clears its X11 hint or demand ends it too.

`paw msg get urgent` prints the urgent windows, the longest waiting first, in the columns of
`get windows`. Subscribers to the control socket receive `urgent COUNT` in every state, then an
`urgent-output NAME 1,3` line per monitor with the workspaces holding urgent windows and an
`urgent-window OUTPUT WORKSPACE APP_ID TITLE` line (tab-separated after the name) for each of the
first sixteen, so a script or a panel can follow them without polling. The shell finds a task
by app ID and title, which the foreign-toplevel protocol has no urgent state for; two windows of
one application with the same title are told apart only by their order.

### Window swallowing

With `windows.swallow = { enabled = true }` a window started from a terminal takes the terminal's
place and hides it, as in Hyprland and dwm's swallow patch: run `mpv clip.mkv` or `imv photo.png`
in a terminal and the player opens in the terminal's tile (or floating rectangle, size and
place), and closing it brings the terminal back there, with focus. It is off by default.

The match is by process ancestry, not by guessing: the new window's process (its Wayland
client's pid, or the X11 window's) has to descend from the process of a terminal's window, however many shells and
launchers lie between, read from `/proc`. Only windows whose `app_id` (X11 class) is in
`windows.swallow.terminals` (compared without regard to case; by default kitty, foot, footclient,
Alacritty, wezterm, ghostty, xterm, URxvt, konsole and GNOME Terminal) can be swallowed. A window that is
itself a terminal never swallows (so a terminal started from a terminal opens beside it), nor
does a dialog, an `app_id` in `windows.swallow.exceptions`, or a window a rule sends elsewhere
(floating, another workspace or monitor, sticky). When a process has several windows (a
`foot --server`), the focused one, else the last focused, is the one swallowed. A terminal that is minimized, in the
scratchpad, sticky or in a group is left alone.

A swallowed terminal is hidden, leaves the taskbar, the window switcher and the overview, and
follows its window to another workspace or monitor; the window is an ordinary one (it can be
floated, moved and resized), and the terminal returns to wherever it is when it closes. If the
terminal closes first, the window keeps the place. `swallow_toggle` (no default binding; it
works with `enabled = false` too) does it by hand: pressed on a window that swallowed a terminal it
gives the terminal a place beside it again, otherwise the window takes the place of the terminal
it was started from, or of the terminal last focused on the workspace. `paw msg get swallow`
lists each window with whether it is swallowed and its partner. Only applications that stay
descendants of the terminal can be swallowed: one that hands the request to a running instance
(a browser opening a new window) belongs to the other process. This is checked headless with
process ancestry through a shell, and with an X11 window (`tests/swallow_smoke.py`,
`tests/swallow_x11_smoke.py`); with real terminals (foot, kitty) it has not been.

## Window switcher

Alt + Tab (`switcher`) opens the window switcher in the middle of the focused monitor. It
lists every window, on every monitor and workspace and including minimized ones, most
recently focused first, with the window focused before the current one selected. Keep Alt
held and press Tab to move on (Shift + Tab, `switcher_prev`, or the arrow keys, to move
back); releasing Alt focuses the selected window, switching its monitor to its workspace and
restoring it if minimized. Return picks at once, Escape cancels, and clicking a window picks
it. While the switcher is open, keys do not reach applications. The compositor keeps the
switcher and the shell draws it, only once it has been open for a moment, so a quick
Alt + Tab flips between two windows without it flashing up; without the shell it still
switches, unseen. Bound to a key without modifiers, or sent as
`paw msg switcher`, it stays open until `switcher_confirm [N]` (the Nth window in the
list), `switcher_cancel`, Return, or Escape. A minimized window's icon is faded and marked with a
dash, and the caption under the grid gives the selected window's full title, its workspace (by
name too) and monitor, and whether it is minimized or asking for attention. `cycle` is the older action that raises the
least recently focused window on the spot.

With [window pictures](#window-pictures) (`shell.thumbnails`, on by default) the taskbar style
shows each window as a card instead, as Windows 11 does: the application's icon and the window's
title over a picture of the window. The pictures are as tall as the taskbar's
(`shell.thumbnails.size`, 5/8 of it) and each as wide as its window's proportions make it, from
3:4 to 2:1, in rows that wrap and are centred; with many windows they get smaller, down to 60 % of
that height, and past that the rows scroll to the selection. The selected card is framed in the
accent colour, one asking for attention is tinted with a dot on its picture, and a minimized
window's picture is faded and marked with a dash. The pictures follow their windows as they
redraw (`shell.thumbnails.live`), minimized ones and those on other workspaces and monitors too;
the shell asks for them as the switcher opens, before it shows, so that they are there by then,
and until one has come the application's icon stands in, the first fading in over it. The
caption, the keys, the mouse and the moment it shows are as with the grid. The macOS style keeps its large icons, as macOS switches
applications.

Resting on a window in the switcher peeks at it, as Windows' Alt + Tab does: once the switcher has
shown and the selection has stayed on a window for half a second, every other window fades to
`peek.opacity` over `peek.duration` and the selected one shows over them in full where it is, a
minimized one where it was and one on another workspace over the workspace shown, as for a
picture on the taskbar's [card](#window-pictures). From then on the peek follows the selection at
once as Tab or the arrows move it, the others staying faded, while a quick Alt + Tab, or Tab
pressed on through the list, fades nothing. The others fade on every monitor, so that the selected
window stands out wherever it is, the switcher on its own monitor over them. Releasing Alt, or
Return, focuses the window where the peek shows it, already in front, as the others come back
around it; a click on another window brings that one up as they come back, and Escape fades them
back. Nothing about a window changes until it is chosen. The selected window closing moves the
peek on with the selection, to the window selected in its place, and the session locking or the
desktop peek end it as they end the card's; after the desktop peek, or the window focused another
way, the next selection rests again before it peeks. The shell tells the compositor as it shows the switcher (`paw msg switcher_peek`), so
without the shell nothing peeks; the macOS style's switcher, which switches applications as macOS
does, does not peek either. `peek.switcher = false` turns the peek off, the palette's too.

## Overview

Super + O (`toggle_overview`) shows the focused monitor's workspace as an Exposé: every
window as a live, scaled thumbnail in a grid that fits them as large as possible (never larger
than the window itself) and keeps each one's proportions, with a strip of the monitor's
workspaces above it. The compositor draws it from scaled copies of the windows' own scene
nodes, so the thumbnails move with the windows' contents (video, a terminal) and cost no more
than a few scene nodes each; the shell draws only text over it. The thumbnails glide out of
the windows' places when it opens and back into them when it closes. It changes nothing until
you pick something:

- Type to filter by title or application (several words must all match; the grid then lists
  matching windows from every workspace and minimized ones). Backspace deletes, Escape clears
  the filter, and a second Escape closes the overview.
- The arrow keys move the selection through the grid (Tab and Shift + Tab step through it,
  Home and End jump to the ends). Return, or a click on a thumbnail, focuses that window,
  switching to its workspace if needed. The mouse selects by pointing.
- Delete, or a middle click on a thumbnail, closes that window.
- The strip shows each workspace with its windows; Page Up and Page Down, Ctrl + Left and
  Right, or the mouse wheel show another workspace in the grid without switching to it. A
  click on a workspace in the strip shows it; a second click, or Return on an empty one,
  switches to it. Dragging a thumbnail onto a workspace in the strip moves the window there.
- Escape, Super + O again, or a click on the backdrop closes it without changing anything.
- Resting the pointer in a screen corner opens it on that monitor (`overview.hot_corner`), as a
  [hot corner](#effects) runs its request: after `hot_corners.delay`, in a `hot_corners.size`
  square, and not over a fullscreen window, during a drag or with a button held. It only opens
  the overview, where a hot corner bound to `toggle_overview` closes it too; a corner can be one
  or the other, not both.

It takes the appearance profile's colours: the backdrop, the cards behind the thumbnails and
the strip are dark shades of `appearance.background`'s hue (a warm black on a paper-coloured
desktop, blue-black on the default one), and the window and workspace picked are ringed in the
focused window's border colour (a gradient's first).

Keys and the mouse do not reach applications while it is open. Fullscreen windows are lowered
under it, so it opens over them. Settings are in the `overview` table (`enabled`, `gap`,
`animation`, `duration`, `strip`, `hot_corner`, `dim`; see the [configuration reference](config-reference.md)).
Scripts can drive it: `paw msg toggle_overview`, `overview_confirm [N]`, `overview_cancel`,
`overview filter TEXT`, `overview select N`, `overview view N` (from 1), and `get overview`,
which lists the state, the selection and the rectangle of every thumbnail and strip cell.
Subscribers get `overview` lines (see `overview_describe` in `src/compositor/overview.c`) as
it opens and changes.

## Command palette

`palette` (Super + P, or `paw msg palette`) opens a search box in the shell, on the monitor
under the pointer, that reaches everything from one place: open windows, installed applications,
workspaces (switching to one, or moving the focused window there), compositor actions such as
Open a terminal (`terminal`), `layout_monocle` or `group_toggle`, the [power actions](#power) that may run (Restart, Power off
and Log out asking first, as from the panel), saved sessions (restore, restore and launch what is
missing, and save the current arrangement under the name typed), and [files](#files), with a
[calculator](#calculator) and a [web search](#web-search). Type to narrow the list; each
word must match, in any order, as letters in sequence of the title or its small print, favouring
runs of letters and word starts (`gc` finds Google Chrome, `lay mon` finds Layout: monocle).
Up and Down, Tab and Shift + Tab, Ctrl + N and Ctrl + P, or the pointer select; Enter or a click
runs the entry; Escape, or clicking elsewhere, closes it. A leading `>` searches only actions, `@`
windows, `#` workspaces and `%` sessions, `/` [files](#files) and `=` asks the
[calculator](#calculator); when nothing matches, the palette says so and lists these. A window
selected among the results peeks after half a second, as in the [switcher](#window-switcher):
the others fade while it shows alone where it is, a minimized one where it was. Moving the
selection to another window moves the peek at once; leaving the windows, or Escape, fades the
others back, and Enter focuses the window where the peek shows it. The window selected as the
palette opens does not peek until something is typed or the selection moves, and typing puts the
first peek off until it pauses. Spotlight, the macOS style's palette, does not peek, and
`peek.switcher = false` turns it off. The palette needs the shell (`paw-shell`), which
draws it, and the compositor's control socket for actions and sessions. On a machine with 600
entries a keystroke re-ranks them in under a millisecond (an optimized build; see
`tests/palette_test.cpp`). Super + P used to toggle sticky windows; that moved to Super + Shift + P.

### Calculator

The palette and the [start menu](#start-menu)'s search calculate what is typed: `2*(3+4)` shows
14 as the first result (the start menu's best match, with a Copy button), and Enter copies the
value to the clipboard, as its small print says. An expression has numbers (`1.5`, `.5`, `1e3`,
and `0x1f`, `0b101` or `0o17` in another base), the constants `pi` (or `π`), `tau` and `e`, the
operators `+`, `-`, `*`, `/`, `%` or `mod` (the remainder), `^` or `**` (power, from the right, so
`2^3^2` is 512 and `-2^2` is -4), a postfix `!` (factorial) and `√`, parentheses, and the
functions `sqrt`, `cbrt`, `abs`, `round` (`round(x, digits)` too), `floor`, `ceil`, `trunc`,
`sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `sinh`, `cosh`, `tanh`, `log` (base 10, or
`log(x, base)`), `log2`, `ln`, `exp`, `min` and `max`, called with parentheses; `2pi` and
`3(1 + 2)` multiply without the sign, and `×`, `÷` and `−` stand for `*`, `/` and `-`. Angles are
in radians. A value is shown with up to 12 significant digits, so binary rounding never shows
(`0.1 + 0.2` is 0.3, and `sin(pi)` is 0), a whole number in full below 2^53, and very large or
small ones as `1.07150860719e301`.

A unit and `in`, `to`, `as` or `->` another of the same kind convert: `5 km in mi`, `100 f in c`,
`3 GiB in MB`, `90 deg to rad`, `100 km/h in mph`. The units are those of length (`mm` to `km`,
`in`, `ft`, `yd`, `mi`, `nmi`), mass (`mg` to `t`, `oz`, `lb`, `st`), temperature (`C`, `F`, `K`),
data (`B`, `kB` to `PB`, `KiB` to `PiB`, `bit`, `Kb` to `Tb`), time (`ms` to `yr`), volume (`ml`
to `l`, `m3`, US `gal`, `qt`, `pt`, `cup`, `floz`, `tbsp`, `tsp`), speed (`m/s`, `km/h`, `mph`,
`kn`), angle (`rad`, `deg`, `grad`, `turn`), area (`cm2` to `km2`, `ha`, `acre`, `ft2`, `mi2`) and
energy (`J`, `kJ`, `cal`, `kcal`, `Wh`, `kWh`), by symbol or by name (`miles`, `celsius`); a symbol
in the wrong case is understood where it is not another unit's, `mb` being megabytes. `in hex`,
`in bin`, `in oct` and `in dec` write a whole number in another base (`255 in hex` is 0xff). Enter
copies the number without its unit.

A lone number or constant (`42`, `e`) is searched for rather than calculated, as is anything that
is not a whole expression, while it is being typed (`2*(`) or when it has no value (`1/0`,
`sqrt(-1)`); a leading `=` asks for the value of anything (`=pi`) and narrows the palette to the
calculator. `tests/calculator_test.cpp` tests the parser.

### Files

The palette and the start menu's search find files and folders by name: those used lately, from
`$XDG_DATA_HOME/recently-used.xbel` (where GTK, KDE and most applications note what they open,
wherever it is), and the names in the XDG user folders (Documents, Downloads, Pictures, ...) and
the home folder, or in `shell.search.directories`. Every word typed must be in the name, in any
case (`report 2024`); a word with a slash in it must be in the path instead (`work/plan`, `~/doc`).
Among equal matches a file used lately comes first, the more so the more lately. In the palette at
most five files go among the rest by how well they match, and a leading `/` searches files alone;
in the start menu they are listed last under Files, and are the best match only when nothing else
matches as well. A file's small print is its folder, `~` standing for the home folder.

Enter or a click opens a file in the default application for its type, as GIO (and `gio open`)
choose it, and a folder in the file manager; Ctrl + Enter, the folder button on a file's row in the
palette (on the chosen row in Spotlight), or the best match's Open folder opens the folder the file
is in. When nothing opens it the panel says why.

The folders are read on a thread of the shell's own, at low priority, the first time a search
asks for files and not before, so a session that never searches reads nothing; the search finds
what was read last meanwhile, and the results fill in as the names arrive. They are read again
when a search asks once they are five minutes old, or ten seconds after one of the folders searched
from (but the home folder, whose own files change all the time) or the recent files changed.
Breadth first, down to `shell.search.depth` levels (4) and `shell.search.max_files` names (20000),
the reading leaves out hidden files and folders, other file systems, and the contents of
version-controlled trees (a folder holding `.git`, `.hg`, `.svn`, `.bzr` or `_darcs`), build and
cache trees (`CMakeCache.txt`, or the `CACHEDIR.TAG` a cache folder has), `node_modules` and
`__pycache__`, though each such folder is found by its own name; symbolic links are found but not
followed. Only names are read, never what is in the files, and nothing is kept on disk.
`shell.search.files = false` turns it off. `tests/file_index_test.cpp` tests the reading, the
search and opening in a home folder of its own.

### Web search

Below what the palette and the start menu's search find comes Search the web for “…”, the words
typed with their spaces tidied, which opens the default browser (GIO's handler for `https`) at
`shell.search.web` with `%s` replaced by the words, encoded for an address (`c++ tips` as
`c%2B%2B%20tips`). Its small print names the engine by its host. In the start menu it is the best
match when nothing else is found, with a Search button; a search that starts with one of the
prefixes (`>`, `@`, `#`, `%`, `=`, `/`) leaves it out, and so says it found nothing when it did.
DuckDuckGo is the default (`"https://duckduckgo.com/?q=%s"`); any `http` or `https` address with
`%s` in it will do, and `shell.search.web = false` leaves the entry out.

## Clipboard history

`clipboard_history` (Super + Shift + V, or `paw msg clipboard_history`) opens the shell's
history of what was copied, as Windows' Win + V does, on the monitor under the pointer: a card
near the top of the screen, as the command palette's, in either style. It lists what was copied in
any application, the pinned entries first and each part newest first: a text by its first three
lines, a picture small with its size, and when each was copied. Typing searches the texts (every
word must be in one); Up and Down (or Tab) choose, and Enter or a click copies the entry again and
closes the card, so that the next paste pastes it. Ctrl + P pins or unpins the entry, Delete (with
the cursor at the end of the search) forgets it, and Escape clears the search, then closes. A
row's buttons pin and forget it, and Clear all forgets every entry but the pinned ones.

The shell follows the clipboard as a client of the compositor's `ext-data-control-v1`, on a
Wayland connection of its own, without taking the keyboard. Of what is copied it keeps the text
(by its best name, up to 1 MB), HTML and a list of files when they come with it, and a picture
(PNG, JPEG, WebP, GIF, BMP or TIFF, up to 16 MB), and copies them back under the names they came
with, the text also as `text/plain`, `UTF8_STRING`, `TEXT` and `STRING`, so that a program pastes
them as it would the original. Copying something kept already moves it up. It keeps nothing a
password manager marks as secret (`x-kde-passwordManagerHint` set to `secret`, as KeePassXC does)
and nothing copied while the session is locked, which the compositor tells it (`locked on|off`).
The entries are kept in memory only, `shell.clipboard.max_entries` (50) of them besides the
pinned ones, the oldest going first, unless `shell.clipboard.persist = true` keeps them in
`$XDG_STATE_HOME/paw/clipboard`, which only the user may read and which is removed while
persistence is off. `shell.clipboard.images = false` keeps text alone, and
`shell.clipboard.enabled = false` keeps nothing and forgets what was kept. Without the
compositor's data-control the history keeps nothing, and copying an entry again goes through Qt's
clipboard.

Choosing an entry copies it but does not paste it into the window that had the keyboard, as
Windows does: no keystroke pastes in every program (terminals take Ctrl + Shift + V, Emacs
Ctrl + Y), and typing what was copied into the window would lose pictures and formatting, so the
paste stays the user's. `tests/clipboard_smoke.py` and `tests/clipboard_shell_smoke.py` test it
with a program copying and pasting through data-control.

## Emoji picker

`emoji_picker` (Super + semicolon, or `paw msg emoji_picker`) opens the shell's emoji picker,
as Windows' Win + . and Win + ; do, on the monitor under the pointer: a card near the top of the
screen, as the command palette's, in either style. Along its top are a search field, a tab for the
emoji picked lately (once there are any; it opens on them) and one for each of Unicode's groups
(Smileys & Emotion, People & Body, ..., Flags), and the skin tone; under them a grid of emoji and
the name of the one chosen. Typing searches by name and by CLDR's English keywords, every word
starting a word of either (`thumbs`, `lol`, `pizz`, `flag norway`), names first; an emoji pasted
finds itself. The arrows move through the grid (Left and Right while the search is empty, or at
its ends), Tab and Shift + Tab too, Page Down and Page Up change tabs, Ctrl + T or the hand at the
end of the tabs changes the skin tone (none, then light to dark) of the emoji that take one, Enter
or a click picks the emoji, and Escape clears the search, then closes.

The emoji picked is typed into the window that had the keyboard, as Windows does: the picker
closes, and once it has given the keyboard back the compositor types it (`paw msg type`, see
[Control socket](#control-socket)) on a keymap of its own, so that every emoji, a flag or a family
of several characters joined too, arrives whole whatever the keyboard layout, in any program that
takes typed text. When nothing has the keyboard to type it into, it is copied to the clipboard
instead and the panel says so. The skin tone and the last 32 emoji picked are kept in
`$XDG_STATE_HOME/paw/emoji`.

The emoji come from a table compiled into the shell, `vendor/emoji/emoji.tsv`: the 1906 emoji of
Unicode's Emoji 16.0 (their names, groups and skin-tone forms, from `emoji-test.txt`) with CLDR 46's
English keywords, under the Unicode License v3; `vendor/emoji/README.md` says how
`tools/emoji_table.py` makes it again. They are drawn in the system's colour emoji font (Noto Color
Emoji, say); without one some show as boxes. `tests/emoji_test.cpp` tests the table and the search,
and `tests/emoji_shell_smoke.py` the picker typing into a window.

## Terminal

The `terminal` action (`paw msg terminal`) opens a terminal: the program and arguments of
the `terminal` setting when there is one (`terminal = { "foot" }`), else `$TERMINAL` when it
names an installed program, else the first of kitty, foot, alacritty, wezterm, ghostty,
konsole, gnome-terminal and xterm found on `PATH`. A configured terminal that is not installed
is an error rather than a reason to open another. When no terminal is found, or the one chosen
cannot start, the panel says why for eight seconds, as it does for any program a binding, a hot
corner or the command palette cannot start. Each of these terminals is in
`windows.swallow.terminals` by default, so [swallowing](#window-swallowing) works with whichever
opens; another terminal needs its app ID added there. `tests/launch_smoke.py` checks the choice
with stand-in programs.

## Workspaces

Every monitor has its own workspaces, numbered 1 to `layout.workspaces` (1–10), and all
start on 1. Workspace shortcuts switch the focused monitor: the one whose window was
focused, whose workspace was switched, or that was clicked last. A monitor's bare desktop
counts as a window there: pointing at it (with focus following the mouse) or clicking it
focuses that monitor, and the window on the other monitor loses the keyboard, so Super + minus
or Super + R then act on the empty monitor. A window belongs to the
monitor it is on; moved to another monitor, it joins the workspace showing there. The
panel on each monitor shows that monitor's workspaces, marking the current one and those
with windows; scrolling over it pages through them and clicking a number switches to it. Resting
on one shows a picture of it, and a window's button or picture dragged onto one moves the window
there ([Workspace pictures](#workspace-pictures)).
`layout.workspace_names = { "web", "code" }` labels workspaces 1, 2, ...: the panel shows the name
instead of the number, and `workspace` and `move_to_workspace` accept it, in bindings
(`workspace = "web"`) and from the control socket (`paw msg workspace web`; a name with spaces
is written as is).
The taskbar lists windows from every workspace, and activating one switches its monitor
to its workspace. Window shortcuts act only on visible windows.

Each monitor also remembers the workspace it showed before, however it was switched, and
the `workspace_back` action (Super + Tab) returns to it, like sway's
`workspace back_and_forth`. With `features = { workspace_back_and_forth = true }`, as sway's
`workspace_auto_back_and_forth`, a `workspace N` action for the workspace already shown
does the same, so pressing Super + 2 twice flips between workspace 2 and the one before.
It is off by default.

`workspace_prev` and `workspace_next` (Super + Ctrl + Left / Right) step to the previous and next
workspace, wrapping around from the last to the first and back. Super and the wheel anywhere on a
monitor do the same for that monitor, up to the next workspace and down to the previous one (see
[Mouse button bindings](#mouse-button-bindings)).

A window being moved with the pointer comes along when its monitor switches workspace, as on
Hyprland: Super and the wheel, a key, a click on the panel's workspaces or a touchpad's swipe
show the other workspace with the window still under the pointer and still moved, and dropped
it stays there. A tile's place in the tiling it left closes, and it tiles on the new workspace
as it would on another monitor; a window floating on purpose floats there. A change of size with
the pointer ends instead.

`move_to_workspace_prev` and `move_to_workspace_next` (Super + Ctrl + Alt + Left / Right) take the
focused window along to the previous or next workspace of its monitor, wrapping around the same
way, and `move_to_workspace_empty` to the first workspace there with no windows on it, as
Hyprland's `movetoworkspace, r+1` and `movetoworkspace, empty` do; nothing happens when every
workspace holds some. Unlike `move_to_workspace`, which leaves the view where it is, the monitor
switches to the workspace the window went to, and it keeps the focus there.

Where there is nowhere further to go, the windows say so rather than stop dead. Past the first or
the last workspace a touchpad swipe stretches them like a rubber band (see [Touchpad
gestures](#touchpad-gestures)), and scrolling over the panel past either end nudges the windows of
that monitor a little the way the slide would take them (left past the last workspace, right
before the first) and back, as far as the rubber band gives a third of a step past the end (2 % of
the monitor's width with the default slide), in twice the `workspace` animation's time; the
taskbar's mark of the current workspace bumps toward the workspace that is not there with them. So
do `workspace_next` and `workspace_prev` with a single workspace. A slide that only fades
(`animations.workspace.distance = 0`) nudges nothing.

### Scratchpad

The scratchpad works as in sway. `move_to_scratchpad` (Super + Shift + minus) floats the
focused window and hides it. `scratchpad_show` (Super + minus) hides the focused scratchpad
window again; otherwise it focuses a scratchpad window already shown on the focused monitor, or
brings the one hidden longest to the middle of that monitor's current workspace, so repeated
presses cycle through them. A shown scratchpad window stays in the scratchpad until it is moved
to a workspace or tiled (Super + V). Hidden windows stay in the taskbar as minimized windows,
so the mouse can still find them; activating one there shows it as `scratchpad_show` would.
`features = { scratchpad = false }` turns both actions off, and a reload then brings hidden
windows back to the current workspace.

It drops down as a quake console does. A window shown slides in from the edge of the monitor's
free area along the bar: down from under a bar along the top (the macOS style's menu bar
counts), up from a bar along the bottom, in from a bar along a side, and down from the top of
the monitor without a bar. It fades in over the first part of the way and is cut along the bar's
edge, so that it comes out from behind the bar rather than across it; the cut lies a little
inside the bar, as deep as the window's rounded corners reach, so that its sides come out
straight. Hidden, it slides back out the same way, speeding up as it goes and fading at the end:
from where it is drawn, so a tile moved to the scratchpad leaves straight from its tile. Shown or
hidden again part way, it turns back from where it is drawn, keeping a little of its speed. It is
in its place for everything but the drawing at once: focus moves to it, or away from it, as the
key is pressed, clicks and the pointer find it where it lands, and one sliding out is no longer
there for them. A scratchpad window always floats, so none slides into a tile: Super + V on a
shown one tiles it, gliding there as tiles move if it is still dropping in, and takes it out of
the scratchpad. Dropping in takes twice
`animations.open`'s duration with its curve, and sliding out one and a half times
`animations.close`'s, with its curve played backwards (an ease-in, by default); `animations.speed`
scales both, and with animations off, or either duration 0, the window shows or hides at once.
Only the GLES2 renderer with the rounded-corners patch for wlroots draws the cut; under other
renderers the window slides uncut behind the bar, and goes no further than its own monitor's
edge where another monitor lies beyond it. Along a monitor's edge with no bar but another monitor
beyond, or a bar too thin to hide the corners, it is cut square while it crosses the line, its
rounded corners and a plain border left out there. `paw msg get scratchpad` tells each window's
drop.

### Sticky windows

A sticky window, as with sway's `sticky enable`, shows on every workspace of its monitor:
`toggle_sticky` (Super + Shift + P, `paw msg toggle_sticky`, or a mouse button binding for the
window under the pointer) floats it, and switching that monitor's workspace keeps it shown
and raises it over the workspace's windows. Moved to another monitor, it stays sticky there;
`move_to_workspace` unsticks it and moves it. Unsticking a window that was a tile tiles it
again on the current workspace (Super + V does the same). The taskbar keeps listing it, and
activating it does not switch workspaces. `features = { sticky = false }` turns the action
off, and a reload that sets it returns sticky windows to their monitor's current workspace.

## Tiling

Tiling is a setting of each monitor. The tiling button on a monitor's panel (next to the
clock) switches that monitor between floating windows and automatic tiling, and while it tiles
the panel is the [tiling bar](#the-tiling-bar) (`shell.tiling_bar = false` keeps the taskbar);
Super + S
or `paw msg toggle_tiling` switches the focused monitor, and
`paw msg output HDMI-A-1 toggle_tiling` a named one. Lua `layout.tiling = true` starts
every monitor tiled, and `tiling` in a monitor's `outputs.monitors` entry overrides it:

```lua
layout = { tiling = false },
outputs = { monitors = { ["DP-3"] = { tiling = true } } }, -- only DP-3 tiles
```

With `layout.tiling_per_workspace = true`, toggling switches only the workspace the monitor
shows: the others keep their own state, and workspaces never toggled follow the monitor's
setting. A window moved to another workspace tiles or floats as that workspace does, and
swapping workspaces between monitors takes their tiling state along. Turning the setting off
again puts every workspace back on its monitor's setting.

A monitor keeps its toggled state across reloads, and across being unplugged, until its
configured setting changes; a reload then applies the new setting. Tiling follows Hyprland's default *dwindle*
layout: every output and workspace has its own binary split tree, each split divides
its space along the longer side, and a new window opens on the output under the
pointer, splitting the focused window there (or the one under the pointer) on the side
nearer the pointer. Floating windows also open on the pointer's output. Closing a window gives its
space back to its neighbour.

A tile alone on its workspace has no gaps: it fills the space the panels leave, inside its
border. A second tile brings `gap_inner` and `gap_outer` back for both, and they go again when it
closes, floats or leaves (`layout.smart_gaps`, on by default, like sway's `smart_gaps`; `false`
keeps the gaps around a lone tile too). A window group is one tile; floating, sticky and
maximized windows are not tiles, and two windows in monocle keep their gaps. A lone column of the
scroll layout loses its gaps and keeps its width. `layout.lone_margin` keeps a lone tile that far
from the edges instead, so a single window on a wide monitor need not fill it: one number, or
`{ top, right, bottom, left }`, e.g. wide at the sides and narrow above and below.

- Mod + right drag on a tile, or dragging its edge, moves the split lines around it.
- Moving a tile (Mod + left drag or its title bar) lifts it out, its place in the tiling kept
  open, so that the other tiles stay where they are; the [snap preview](#snapping) shows the
  place. Once the pointer rests a moment (150 ms) on another tile, the tiles glide aside (with
  `animations.move`) to open the place a drop there gives it, splitting that tile on the side
  nearer the pointer, as the tiling will be after the drop, and the preview glides there;
  crossing tiles on the way moves nothing, and clients are asked for a new size only as a place
  opens, not at every motion. Dropping it puts it in the place it holds, at once where the
  pointer is if the tiles have not moved yet; dropped back on its own place, it is as it was.
  Over another monitor that tiles the place opens there; taken to a monitor that does not tile,
  or to the top edge, which maximizes it, the place closes and the tiles fill it again, as they
  do when the move is cut short (locking, or the window sent to another workspace), the window
  floating then where it is. Its monitor switching workspace meanwhile takes it along, still
  moved: its place closes, and one opens in the tiling of the workspace shown (see
  [Workspaces](#workspaces)). In the master, spiral and monocle layouts the place opens at the
  end of the list, and dropping a tile where it was lifted from keeps its place there too. The
  scrolling layout keeps no place: its tile leaves the strip as it is lifted, and a drop gives
  it a column of its own, at the end of the strip on its own monitor and right of the focused
  column on another, or at the start of the strip when dropped over the first column. A window
  dropped on another monitor that tiles joins the tiling there even if snapping or maximizing,
  or its old monitor not tiling, had floated it; one floated with Super + V stays floating. A
  tile dropped on a monitor that does not tile floats there.
- Dialogs and fixed-size windows float. Super + V (`toggle_floating`) floats
  or tiles the focused window; snapping or maximizing a tile also floats it. A window
  opening into the tiling of a workspace, or restored into it from minimized, brings the
  windows maximized there back into the tiling instead of covering them, as it does
  fullscreen ones, and turning tiling on brings
  back every window that floated only because it was snapped or maximized
  (`tests/maximized_tile_smoke.py`), and fullscreen ones into tiles of their own
  (`tests/fullscreen_panel_smoke.py`).
- A floating window between two monitors belongs to the one its middle is on. Turning tiling
  on tiles it when that is the monitor turned on; when it is the other one, the window moves
  wholly onto its own monitor instead of staying under the new tiles.
- Turning tiling off returns every window of that monitor to its floating position and
  size. A window
  now tiled on another monitor keeps its size and its place relative to that monitor,
  shrunk and moved to fit inside it.
- Minimized windows leave the tiling and rejoin it when restored, as a window opening there
  does, one that floated only because it was snapped or maximized by hand too, while one
  floated on purpose stays floating (`tests/taskbar_restore_tiling_smoke.py`); windows moved to
  another workspace join that workspace's tiling on the same output.
- Super + arrows focus the neighbouring tile that way, or the next monitor that way when
  there is none, even an empty one, where new windows then open; a window on a monitor
  further on never skips the empty one between. Super + Shift + arrows swap
  the focused tile with it (`focus_left` … `focus_down`, `move_left` … `move_down`). With
  `focus_follows` on, these and Alt+Tab put the pointer just inside the window's bottom-right
  corner, out of the way.
- Super + Ctrl + Shift + arrows (`resize_left`, `resize_right`, `resize_up`, `resize_down`) resize from the keyboard, by
  40 pixels or the binding's `amount` (`paw msg resize_right 80`). On a tile the arrow
  moves a split beside it that way: the one on that side of the tile if there is one, growing
  it, else the one on its other side, shrinking it. With two tiles side by side, Right always
  moves the line between them right. A tile in the middle of three columns only grows this
  way; shrink it by resizing a neighbour. Splits stop at 10% and 90% of their space, as with
  the mouse. A floating (or sticky) window moves its right or bottom edge that way, growing no
  further than its monitor's edge and shrinking no smaller than 64 pixels; a snapped window
  leaves its snapped place at its current size. Maximized and fullscreen windows do not
  change. Holding the keys keeps resizing, at the keyboard's `repeat_delay` and
  `repeat_rate`. `features = { keyboard_resize = false }` turns the actions off.

A window brought forward comes in front of the others on its monitor, tile or not, so that a tile
clicked covers the floating windows over it. With `layout.floating_above_tiles = true` the
windows out of the tiling (dialogs, windows floated with Super + V, snapped, maximized and sticky
ones) always stay over the tiles, as in sway and Hyprland, and a tile brought forward comes in
front of the other tiles only; a tile that floats goes up among them, and a floating window
that tiles goes down among the tiles, in front. It is off by default. [Windows kept
above](#keeping-windows-above) stay over both.

Windows tiled on a
monitor that is disabled in the config hands its tiles to the nearest one, where they float if
that one does not tile. Unplugging a monitor does the same with all its windows (floating ones
keep their relative place, tiles join the tiling of the nearest monitor, workspace numbers are
kept), and with `outputs.return_windows` (on by default) they go back to the same workspace and
into its tiling when the monitor is plugged in again, unless it is turned off or the windows were
put elsewhere by hand. With no other monitor left, windows stay where they are.

### Workspaces between monitors

`move_workspace_to_output` sends the focused monitor's current workspace to another monitor:
its windows, and the layout, master ratio and count, splits or scroll columns it had, all
travel, and the other monitor shows it. The workspace with the same number there takes its
place, so nothing is merged or lost. `swap_workspaces` trades all workspaces of two monitors, and
with them what each one shows. Both take a target: `left` or `right` (the neighbouring monitor
that way), `next` or `prev` (left to right, wrapping), a connector name, or `desc:` and the start
of "make model serial". Bind them with `output = ...` (`swap_workspaces` defaults to `next`), or
run `paw msg move_workspace_to_output left`, `paw msg output DP-1 swap_workspaces DP-2`.
Super + Ctrl + comma / period are bound to the first with `left` / `right`; `swap_workspaces` has
no default binding.

Floating windows keep their place relative to the monitor's usable area and glide across;
tiles glide to their new places. On a monitor that does not tile, the arriving tiles float, and
floating windows sent from one that does not tile join the tiling. Sticky windows and the
scratchpad stay with their monitor.

### Tiling layouts

Dwindle is the default, but every output and workspace can tile with another layout, chosen
with `layout_next` / `layout_prev` (Super + Space, Super + Shift + Space), which cycle
dwindle, master, spiral, monocle, scroll, or by name with `layout_dwindle`, `layout_master`,
`layout_spiral`, `layout_monocle` and `layout_scroll` (unbound by default; `paw msg layout_master` works
too). `paw msg get layout` prints the focused monitor's current workspace as `NAME RATIO
MASTER_COUNT`.

- **master**: the first `master_count` tiles share a column on the left, `master_ratio` of
  the width; the rest are stacked in a column beside it. With no more tiles than masters,
  the master column takes the whole width.
- **spiral**: each tile takes `master_ratio` of the space the earlier ones left, turning
  clockwise (left, top, right, bottom, ...), the last tile getting what remains.
- **monocle**: every tile fills the area, the focused one on top; Super + J / K (`focus_next`
  / `focus_prev`) or the arrows step between them, and Alt + Tab lists them as usual.

- **scroll**: see [Scrolling layout](#scrolling-layout).

A right click on the panel's tiling button opens its menu: what the workspace the monitor shows
does now, Floating or the layout it tiles with, at the top with its icon, then the other
layouts, each with an icon of how it lays windows out, and Floating while it tiles. A layout
picked while the workspace floats turns tiling on with it. The button itself shows the same icon
as the menu's title: the layout's while the workspace tiles, two overlapping windows while it
floats.

The layouts other than dwindle order the tiles as a list (the dwindle tree read left to
right): a new window joins the end wherever it is opened or dropped, and Super + Shift +
arrows trade places with the next or previous one in the list rather than the geometric
neighbour. `promote` (Super + Return) swaps the focused tile with the first, or with the
second when it is the first; `swap_next` / `swap_prev` (Super + Shift + J / K) trade with the
next or previous; `focus_next` / `focus_prev` step focus in order, wrapping, in every layout.
`master_grow` / `master_shrink` (Super + L / H) move the ratio by 5% of the width and
`master_more` / `master_less` (Super + comma / period) add or remove a master, from 1 to 8;
in master, `resize_right` / `resize_left` and dragging the boundary move the ratio too. A
ratio stays between 10% and 90%. Switching layouts never loses the dwindle tree: swaps
stay, but its splits return exactly as they were.

```lua
layout = {
    tile_layout = "master",  -- what workspaces start with: dwindle, master, spiral, monocle, scroll
    master_ratio = 0.55,     -- 0.1 to 0.9
    master_count = 1,        -- 1 to 8
},
```

A workspace keeps the layout, ratio and master count it was given until the compositor
restarts; a reload changes the starting values only for workspaces that have not chosen
one.

Each monitor can start with its own values in `layout.outputs`, keyed by connector name or
`"desc:"` and the start of "make model serial" as in `outputs.monitors`:

```lua
layout = {
    outputs = {
        ["DP-1"] = { tile_layout = "scroll" },
        ["desc:Dell U2720"] = { tile_layout = "master", master_ratio = 0.6, master_count = 2 },
    },
},
```

These apply to the monitor's workspaces that were not set by hand (a layout chosen with an
action, or a ratio changed, is kept), also to windows already open when the configuration is
reloaded, and again when the monitor is plugged back in. `paw msg get layout OUTPUT
[WORKSPACE]` prints another monitor's or workspace's values.

### Scrolling layout

`layout_scroll` tiles like niri and PaperWM: windows live in columns on a strip much wider
than the screen, and the screen is a view onto it that follows focus, gliding like any
other tile move. A new window opens in a column of its own right of the focused one, or, with
the pointer on a window of the first column, as the new first column. Only a pointer you moved
onto a tile yourself counts: not one a key moving the focus brought along
(`mouse.focus_follows`), nor one on a launcher or a floating window over the column or left
there as it closed, nor one beside the column or on another monitor. A column has a width, a
share of the screen (`layout.scroll.width`, half by default), and stacks its windows evenly. A
column alone on its workspace fills the screen, and takes its width back when another
joins it; `fill_lone = false` keeps it at its width, and a width set by hand (by an action or by
dragging its side) stays, alone too. Beside its own actions, the usual keys work: `focus_left` /
`focus_right` step between columns and `focus_up` / `focus_down` within one, `move_left` /
`move_right` move the focused column along the strip and `move_up` / `move_down` trade
places within a stack, `focus_next` / `focus_prev` step through every window in order,
`master_grow` / `master_shrink` (Super + L / H) and dragging a column's side resize it,
and the Alt + Tab switcher brings any window into view.

| Action | Does |
| --- | --- |
| `scroll_left`, `scroll_right` | focus the column to the left or right (the view moves to show it, even with `follow = "never"`) |
| `column_cycle_width` | step the focused column through `presets` (a third, half, two thirds, full), wrapping |
| `column_widen`, `column_narrow` | change its width by `step` (10% of the screen), from 10% to 100% |
| `consume_left`, `consume_right` | stack the focused window into the column on that side; a column left empty disappears |
| `expel` | move the focused window out of a stack into a column of its own on the right |
| `center_column` | center the focused column in the view |

```lua
layout = {
    tile_layout = "scroll",
    scroll = {
        follow = "center",                  -- center: always centered; edge: only as far as
                                            -- needed; never: only scroll actions, new windows
                                            -- and center_column move the view
        width = 0.5,                        -- width of a new column, 0.1 to 1
        fill_lone = true,                   -- a column alone on its workspace fills the screen
        step = 0.1,                         -- column_widen / column_narrow
        presets = { 1/3, 1/2, 2/3, 1 },     -- column_cycle_width; up to 8 widths
        cube = false,                       -- the view turns as a cube as it moves
    },
},
```

With `cube = true` the view turns like a cube as it moves along the strip, rather than gliding
the columns over: the screen as it was lies on one side of a square that turns a quarter, away
toward the side the view leaves, while the screen as it will be comes round on the next side, the
corner between them nearest you half way. A column lying on a side turned partly away is drawn
narrower, a little shorter the further off it is and fainter, and a column across the corner is
drawn straight across it. The turn takes `animations.cube`'s `duration` (two and a half times
`animations.duration`, 300 ms by default) and `curve` (`"ease-out"`); a `duration` of 0, or
`animations.enabled = false`, glides the columns again. Moving the view again during a turn turns
on from where it is drawn, the same way a quarter further, or back the other way. Only a move of
the view in which every column keeps its size turns it: a window opening, a column changing width,
windows stacking, and columns traded while the view stays glide as before. As with any animation,
`paw msg get windows`, clicks and hovering have the windows where they rest at once, and the
turn ends as the workspace changes, the overview opens or the screen locks.

With `mouse.focus_follows` on, columns that scroll under a still pointer can take focus on
its next move; keyboard-driven scrolling takes the pointer along, but if the mouse causes
trouble, use `follow = "edge"`, which moves the view least.

Columns past the screen's edges stay on their own monitor: nothing of them shows on the monitor
beside it, a column sliding past an edge is cut there (square while it crosses, its rounded
border left out, where the GLES2 renderer draws clips), one at rest wholly past it is not drawn,
and the pointer passes it by to reach the other monitor's windows there. With `width = 0.5`
and `follow = "edge"` the view shows two whole columns and scrolls by one as focus moves past
them.

The scroll layout is per workspace like the others; windows opened under another layout get
a column each, at the end, when it is switched to. The columns are remembered when you
switch away and back. Only the scroll actions need key bindings, none are bound by default
(`config/init.lua` has commented examples).

## Animations

Windows fade in while growing slightly when they open, and fade out while shrinking slightly
when they close (drawn from a copy of their last frame). Tiles glide to their new place when
the layout changes; their new size shows as soon as the application draws it. The scene graph
has no transform for a whole window, so the scaling resizes each of its surfaces about the
window's center. `animations = { enabled = false }` turns this off, and `duration` sets the
length in milliseconds (default 120, 10–1000). Window positions reported by `paw msg` are
always the final ones, and clicks and hovering use them too: input never waits for an
animation or lands in the middle of one.

A window that gets another place and size glides to them together, its size easing on the same
curve as its place: maximized and restored (by a binding, the application's own title bar or
button, or the taskbar), snapped from the keyboard or by Super + Alt + arrows, dropped into a snap at an
edge (gliding from where it was dropped into the slot, over the preview fading out under it),
dragged off a snap (shrinking back to its own size under the pointer), resized or moved by the
keyboard, floated out of the tiling, moved to the next monitor floating, or made fullscreen and
back.
These take `animations.move`, and fullscreen `animations.fullscreen`. An application draws its
new size only once it is told it, so meanwhile what it drew last is stretched to the size the
window is drawn at, and once it draws the new size what it drew before fades out over it, so
that neither the size nor the picture jumps. The border, the shadow and the rounded corners
follow the size drawn; becoming maximized or fullscreen, where a window has none, they fade out
and the corners square on the way, and fade back in leaving it. Past the glide, a window waits
at its new size for an application slow to draw it, up to 300 milliseconds, and then eases to
the size the application last drew, until it draws the new one; an application that answers at
its own size instead has the window ease to that. Reversed or changed again part way, a window goes on from
where it is drawn, at the speed it has. `paw msg get drawn` prints where each window is drawn
now, in the order of `get windows`: app ID, title, its place and size as drawn, the size its
surface is drawn at, what its change of size does (`rest`, `moving`, `waiting` for the
application, `fading` what it drew before), how much of its frame shows and the factor an
animation or a gesture gives its opacity (both in thousandths), for tests; `tests/morph_smoke.py`
follows the changes with it.

Animations are interruptible: a tile that is gliding when the layout changes again continues
from where it is drawn, at the speed it has, and never snaps. Each kind (`open`, `close`,
`move`, `workspace`, `fullscreen`, `focus`, `minimize`, and `cube`, the
[scrolling layout's](#scrolling-layout) view turning) can have its own `duration` (0 turns just
that kind off) and easing `curve`: `"linear"`, `"ease-in"`, `"ease-out"`, `"ease-in-out"`,
`"ease-out-quint"`, `"overshoot"`, `"spring"`, or `"bezier(x1, y1, x2, y2)"`. Moves use the
spring by default. `speed` scales every animation (2 is twice as fast), and a frame later
than `late_frame_ms` (default 80) finishes what is running rather than stuttering through it.
Switching workspace slides the old windows out (copies of their last frames) and the new
ones in, fading, toward the side of the higher-numbered workspace; `animations.workspace.distance`
sets the slide as a share of the monitor's width (default 0.08, 0 only fades). Sticky windows
stay put. A [scratchpad](#scratchpad) window drops in from behind the bar and slides back out
there, on the `open` and `close` kinds.
When focus moves, a window's opacity and border color fade to their new values (the `focus`
kind) instead of switching.

Minimizing a window shrinks it into its button on the taskbar of its monitor, as on Windows 11
and macOS: a copy of its last frame slides on a straight line to the button as it shrinks to the
button's size, keeping its proportions, its rounded corners and its border, and fades out over
the last part of the way. Restoring it grows it out of its button into its place, from where it
is restored to, fading in over the first part. Restored by a click on its picture on the
taskbar's [card](#window-pictures), it grows out of the picture instead, and minimized from there
it goes into it; in the macOS style it goes into its application's icon on the dock, and on the
tiling bar the focused window goes into its title. Restored while still on its way in (a second
click on its button), it turns back from where it is drawn, and so does one minimized again on
its way out; minimizing or restoring all of a stack's windows, or showing the desktop, sends each
into its own button at once. The window is minimized, or
back in its place, at once: input never waits for the animation. A window whose button the
taskbar does not show (the shell is not running, or another taskbar does not say where its
buttons are) shrinks toward the middle of the strip a panel keeps along an edge of its monitor,
or, with no panel, where it is. It is the `minimize` kind, which lasts 300 ms unless set (two and
a half times `duration`, at most 1000) on `"bezier(0.2, 0, 0, 1)"`: a moment to set off, then
most of the way at once and a long, soft settle. A window in the [scratchpad](#scratchpad) comes
and goes its own way, one a peek shows (the taskbar's picture's, the window switcher's or the
palette's), or still shows as it fades, comes back where it shows it, and one picked in the [overview](#overview) comes back
with its thumbnail. Other taskbars say where their buttons are with wlr-foreign-toplevel's
`set_rectangle`; paw's shell says where its buttons, its pictures and the dock's icons are as
they move.
The shell's own motion (the taskbar's buttons coming, going and sliding aside, its fills, lines,
counts and icons, the popups, the notification cards, the on-screen display, and the switcher,
the palette, the power dialog and the overview coming in and going) follows `enabled` and `speed`
too, and so do the wallpaper fading in and the crossfade to a new
[appearance profile](#appearance-profiles).

A window moved with the pointer is lifted once the pointer has taken it a few pixels (a click on
its title bar does not lift it): it grows by a fortieth, but no more than 24 pixels along its
longer side, about the point the pointer holds it by, so it stays under the pointer as it was
taken, and its shadow grows half as soft again and falls further, a little lighter. A printed
shadow (with no blur) moves two fifths further off instead, as crisp. A translucent window's
cast shadow stays as it rests, as stretched it would show through the window's edge. Its border
and rounded corners stay as they are around it. It eases up with the duration and curve of
`move`, stays up while the window is moved (drawing nothing while the pointer rests), sets down
onto an edge its [magnet](#magnetic-edges) holds and lifts again as the pointer pulls it away,
and settles back as it is dropped, in half as long again, from wherever it is drawn; a drop that
snaps or maximizes it settles it as it moves to its place. Where the window is and what the
pointer reaches are those of the window at its own size throughout. Resizing does not lift a
window. `windows = { lift = false }` turns this off; with animations off the window lifts and
settles at once. `paw msg get lift` lists the windows lifted or settling: title, how far up
(thousandths), the box the window is drawn in from its top-left corner, and the point it grows
about.

## Effects

Small effects that are off, or harmless, until you ask for them. Each has settings in
[docs/config-reference.md](config-reference.md).

**Dim inactive windows.** `windows.dim_inactive = 0.25` lays black over windows without focus,
so the focused one stands out; it works for every application (unlike `inactive_opacity`,
nothing shows through) and fades over `windows.dim_duration` milliseconds (`animations.enabled =
false` or a duration of 0 switches at once). Fullscreen windows are never dimmed, and clicks go
through the black to the window.

**Peek.** Bind `peek` to a key and every window fades to `peek.opacity` (0.12 by default; 0 hides
them) for as long as the key is held, showing the desktop behind them; borders, the panel and
dimming go with it. `peek_toggle` (and `paw msg peek_toggle`) switches it on and off instead.
The fade takes `peek.duration` milliseconds, at `animations.speed`. Resting on a window's picture
on the taskbar's card peeks at that window the same way, the others fading while it shows over
them (see [Window pictures](#window-pictures)), and so does resting on a window in the
[switcher](#window-switcher) or the [palette](#command-palette); the desktop peek takes over from
them.

**Night light.** `night_light = { enabled = true, night_temperature = 3400 }` turns the screen
warm in the evening and neutral again in the morning, easing over `transition` minutes around
sunset and sunrise. Give the times (`sunrise = "06:30"`, `sunset = "21:00"`) or a `latitude` and
`longitude` and paw works them out for each day. The actions `night_light_toggle`,
`night_light_on`, `night_light_off` and `night_light_auto` (back to the schedule) override it,
also as `paw msg night_light_toggle` and the tile in the shell's Quick Settings, which shows
whether it is on (subscribers to the control socket hear `night-light on|off auto|on|off`);
`paw msg get night_light` prints the temperature in
kelvin, the override (0 schedule, 1 neutral, 2 warm) and whether the schedule is enabled. The
colours go through the output's gamma table, the way `wlsunset` does it, and a client using
`wlr-gamma-control` still works alongside.

**Magnifier.** `zoom_in`, `zoom_out` and `zoom_reset` (bind them, or `paw msg zoom_in`)
magnify the screen by `zoom.step` (1.25) per step up to `zoom.max` (8), easing over
`zoom.duration` milliseconds. The view follows the pointer and keeps it over the point it clicks:
the pointer moving across the screen pans across the whole desktop. With `zoom.scroll_modifier =
"Super"`, Super plus the scroll wheel steps in and out, in place of switching workspaces (the
modifier takes the wheel before any binding of it). Only the output the pointer is on is
magnified, and outputs that are rotated stay at 1x. Screenshots and screen recordings of the
output see the magnified picture. `paw msg get zoom` prints the level and its target in
thousandths.

**Hot corners.** Push the pointer into a screen corner and leave it there for
`hot_corners.delay` milliseconds (150) and the corner's request runs, once per visit:

```lua
hot_corners = {
    top_left = "toggle_overview",
    top_right = "spawn foot",
    bottom_left = "peek_toggle",
    bottom_right = "workspace_next",
}
```

A request is written as for `paw msg`: an action with its argument, or `spawn PROGRAM ARGS`,
and acts on the monitor whose corner it is. The corner is `hot_corners.size` pixels (2) square.
Nothing runs while a button is held, a drag is under way, the session is locked, or a fullscreen
window covers the output. `overview.hot_corner` makes one more corner, which opens the overview
(see [the overview](#overview)); a corner it takes cannot be bound here too.

## Monitors

Monitors sit side by side, top-aligned. `outputs.order` lists connector names
(such as `DP-3`) left to right; unlisted monitors follow on the right in the order
they appear. The cursor starts on `outputs.primary`, else on the leftmost monitor. By default each monitor runs its preferred resolution at the fastest
refresh rate available for it. `outputs.monitors`, keyed by connector name, overrides
that per monitor:

```lua
outputs = {
    primary = "DP-3",
    monitors = {
        ["DP-3"] = { mode = "2560x1440@200", scale = 1.25, position = { x = 0, y = 0 } },
        ["HDMI-A-1"] = { mode = "2560x1440@144", position = { x = -2048, y = 0 } },
        ["DP-1"] = { enabled = false },
    },
},
```

`mode` is `WIDTHxHEIGHT` or `WIDTHxHEIGHT@HZ`; the closest refresh rate at that
resolution wins. `scale` is fractional, `transform` takes Hyprland's (and Wayland's)
0–7, `enabled = false` turns a monitor off (never the last one), `vrr = true` enables
adaptive sync where the monitor supports it, and `tiling` overrides `layout.tiling` (see
[Tiling](#tiling)). A key such as `["desc:ASUSTek COMPUTER INC
VG27AQ3A"]` matches the start of a monitor's "make model serial" (listed by `paw msg get
outputs`), as Hyprland's `desc:` does; a connector-name key wins over it. Monitors with a
`position` go there, in logical pixels after scaling; the rest follow in a row to their
right. The whole layout then shifts so its top-left corner is 0, 0, because X11 apps
get no input at negative coordinates; windows move with their monitor. `paw msg get
outputs` prints what each monitor ended up with. Reloading
applies changes without restarting.

Monitors can also be changed while running with any wlr-output-management client (`wlr-randr`,
`kanshi`, `wdisplays`): mode, scale, rotation, position, and turning outputs on or off. Those
settings replace the `outputs.monitors` entry of the outputs they touch until the Lua
configuration is reloaded, which brings the configured setup back. The last enabled output
cannot be turned off. The [display settings](#display-settings) window changes them too, and
keeps what it is told to keep across logins; that section says which settings win where several
say something of a monitor.

### Colour depth

`bit_depth = 10` in a monitor's entry draws it with 10 bits per colour channel instead of 8, as
Hyprland's `bitdepth` does, for smoother gradients on a monitor that shows them:

```lua
outputs = { monitors = { ["DP-3"] = { bit_depth = 10 } } },
```

The desktop is then drawn in a 10-bit format (`XRGB2101010`, else `XBGR2101010`, which the GLES
renderer may have alone), where the monitor, the cable and the GPU take it: paw tests the
monitor's settings with it first, and where that fails it stays at 8 bits and logs `DP-3 cannot be
drawn in 10 bits; it stays at 8`. `paw msg get outputs` ends each line with the depth a
monitor is drawn in, 8 or 10. A 10-bit format does not by itself mean the monitor receives 10 bits:
that is up to the driver and the link. A wlr-output-management change keeps the configured depth.

### HDR

`hdr = true` in a monitor's entry drives it in HDR, BT.2020 colours with the PQ curve, as
Hyprland's `cm = hdr` does, and draws it in 10 bits where it can be:

```lua
outputs = { monitors = { ["DP-3"] = { hdr = true } } },
```

It takes three things, and without any of them the monitor stays SDR and the log says which:
the monitor's EDID offering BT.2020 and PQ (with the driver's colour space and HDR metadata
properties on its connector); a renderer that converts colours, which in wlroots 0.20 is the
Vulkan renderer alone (`WLR_RENDERER=vulkan` in the session's environment; GLES, the usual one,
and pixman cannot); and the test of the monitor's settings passing. Switching it on or off may
blank the monitor for a moment, as a mode change does. `paw msg get outputs` ends each line
with `hdr` or `sdr`.

Once a monitor asks for HDR, and only with the Vulkan renderer, applications may describe their
own colours through color-management-v1 (mpv and games run through Proton can):
parametric descriptions with the gamma 2.2, PQ, linear, BT.1886 and power 2.4 curves, sRGB
and BT.2020 primaries, and the mastering display's primaries and luminances, with the perceptual
render intent. Each window is converted from what it says into what its monitor shows, SDR
windows into HDR on an HDR monitor and HDR video into SDR on another. Without `hdr` on any
monitor nothing is offered, and applications draw as before.

What wlroots 0.20 does not do: SDR windows on an HDR monitor are shown with a reference white of
203 cd/m², which no setting changes (Hyprland's `sdrbrightness` and `sdrsaturation` have no
counterpart), and wide-gamut SDR (`cm = wide`) is not offered; ICC profiles are not taken.
Night light leaves an HDR monitor alone. A mirror of an HDR monitor shows its picture converted
to SDR, and a mirror is itself never HDR. Screenshots of an HDR monitor hold its PQ-encoded
pixels, which look dark and washed out to programs that take them for sRGB.

### Mirroring

A monitor can show another's picture, as Windows' Duplicate and KDE's display mirroring do:

```lua
outputs = {
    monitors = {
        ["HDMI-A-1"] = { mirror = "eDP-1" }, -- the projector shows the laptop's panel
    },
},
```

`mirror` names the monitor to show, by connector name or `"desc:"` description as the keys of
`outputs.monitors` do. The mirroring monitor leaves the layout: it has no workspaces, windows or
panels of its own, and applications do not see it as an output. It shows the whole picture of the
monitor it mirrors, everything on it with the pointer, the magnifier, night light and the lock
screen, scaled to fit its own mode, with black bars where the shapes differ, and upright whichever
of the two is rotated. Its own `mode` and `transform` apply; its `scale` does not matter. While
the monitor it mirrors is not in the layout (unplugged, `enabled = false`, or behind a closed
lid), it joins the layout as any other monitor does, and leaves it again when that one comes
back, its windows moving as when a monitor is unplugged. It turns off and on with the monitor it
mirrors (see [Turning monitors off](#turning-monitors-off)), and `paw msg get outputs` names
that monitor in a column of its own. One mirroring a monitor that mirrors another shows that other
one. wlr-output-management clients list it as enabled, at the place of the monitor it mirrors:
left there, it goes on mirroring; moved elsewhere, it joins the layout there.

The picture is copied from the frames the mirrored monitor shows, one scaled copy for each, so a
mirror costs little, and nothing while the picture stands still. Closing a laptop's lid while
another monitor mirrors its panel turns the panel off as in [clamshell mode](#the-laptop-lid),
and the other monitor then shows the desktop itself.

### Display modes (Win + P)

The `display_mode` action sets the monitors up as Windows' Win + P does: `extend` puts every
connected monitor in the layout, `duplicate` has the others mirror the main monitor, `internal`
keeps the main monitor alone (Windows' "PC screen only") and `external` every monitor but it
("Second screen only"). The main monitor is a laptop's built-in panel; on a machine without one,
the primary monitor (`outputs.primary`, else the first connected one in `outputs.order`, else the
first by connector name). A choice keeps each monitor's mode, scale and position, and lasts until
the configuration is reloaded or the display settings window applies its settings, as
wlr-output-management changes do. With one monitor connected, only `extend` can be had.

XF86Display, the key a laptop sends for Fn and its display key, runs `display_mode` without a
choice: a popup on the focused monitor shows the four in Windows' order (PC screen only,
duplicate, extend, second screen only) with the one in force selected, each further press moves
the selection on, and a second and a half after the last press the selected one is taken. While
the popup is open the arrow keys move the selection, Return takes it at once and Escape closes
the popup without changing anything; a click takes a choice too, and while the pointer rests on
the popup it stays open. Win + P itself opens the command palette.

```lua
bindings = {
    { mods = {}, key = "XF86Display", action = "display_mode" },
    { mods = { "Super", "Ctrl" }, key = "p", action = "display_mode", mode = "duplicate" },
},
```

`paw msg display_mode duplicate` picks one from a script (and says why it cannot, with one
monitor), and `paw msg get display_mode` prints the choice in force and, while the popup is
open, the one it shows and its monitor (`-` for both while it is closed).

### Display settings

A window sets the monitors up, as Windows' Display settings and KDE's Display Configuration do.
It opens from Quick Settings (Display settings…, under the brightness; Display Settings… in Control
Center's Display module), from the macOS style's system menu (Displays…), from the command
palette, and with the `display_settings` action, which opens it on the monitor under the pointer:

```lua
bindings = {
    { mods = { "Super", "Ctrl" }, key = "d", action = "display_settings" },
},
```

At its top the monitors that show a desktop of their own are drawn to scale where they stand,
each with its connector name, its make and model ("Built-in display" for a laptop's panel), a
star on the primary one, and the monitors mirroring it; a laptop's panel the closed lid holds off
is drawn faded. A click chooses one, and a drag moves it: it snaps beside the others, an edge of
it lining up with theirs (tops, bottoms, centres) as it comes near, never overlapping one or
leaving a gap, and an outline shows where it will go. The monitors that are off or mirror another
are listed under the arrangement, to choose too. The chosen monitor's settings follow: whether
it is on (never the last one showing the desktop), its picture (a desktop of its own, or the same
as another monitor's: [mirroring](#mirroring)), its resolution and refresh rate from the modes it
offers (its preferred one marked recommended), its scale (100 % to 300 % in steps, or Custom… for
any from 25 % to 1000 %), its rotation, adaptive sync where it has any, 10-bit colour, HDR where
the compositor says it can be had, else why not (its EDID, or the renderer), and Make this the
main display. Changing a size moves the others to keep them side by side as they were. A line
under the settings says what they cannot show: that the lid holds a panel off, that a mirror's
scale does not matter, that a monitor asked for 10 bits or HDR and stayed at 8 or SDR.

Nothing changes until Apply, which puts every monitor's settings on trial at once. Then "Keep
these display settings?" counts down 15 seconds, and unless Keep changes is clicked they go back
by themselves; Revert, Escape and closing the window take them back at once. The keyboard starts
on Revert, as on Windows, so that a stray Enter on a monitor showing nothing keeps nothing. Where
the window's own monitor goes off, the window moves to another, and comes back with the monitor.
Undo changes forgets what was not applied, and Reset to configuration, offered once the window's
settings are kept, puts the configuration's settings on trial as Apply does.

Settings applied there are tried first, as on Windows and KDE: every monitor's settings are
tested at once, as a wlr-output-management client's are, and nothing changes where a monitor
refuses them; then they are applied on trial for 15 seconds, and go back by themselves unless
they are kept. They go back at once where a monitor does not take them after all, and when the
configuration is reloaded during the trial. Kept, they are written to
`$XDG_STATE_HOME/paw/outputs` (else `~/.local/state/paw/outputs`), a line per monitor,
which is laid over `outputs.monitors` every time the configuration loads, at login and on each
reload. Each line holds for the monitor it was kept for: the same connector, and the same "make
model serial" as `paw msg get outputs` prints it, so that another monitor plugged into that
connector, as at another desk, keeps the configuration's settings. paw never edits the Lua
files: Reset to configuration puts the configuration's settings alone on trial, and removes the
file once they are kept; deleting the file by hand does the same at the next reload.

Where a monitor's settings come from, the first that has any for it winning:

1. A wlr-output-management client's change (`wlr-randr`, `kanshi`, `wdisplays`) or a
   `display_mode` choice, for the monitors it touches, until the configuration is reloaded or
   the display settings window applies settings of its own.
2. The display settings window's, on trial or kept, for the monitor they were kept for. The
   window sets every setting of every monitor at once, so they replace its `outputs.monitors`
   entry whole, but for `tiling`, which stays the configuration's; the primary monitor chosen
   there wins over `outputs.primary`.
3. `outputs.monitors`, `outputs.primary` and `outputs.order` in the configuration.
4. The defaults: the preferred resolution at the fastest refresh rate, at scale 1, side by side.

Scripts reach the same through the control socket. `paw msg monitors apply` takes monitors
by connector name, each followed by the settings that change, as the state file writes them:
`enabled`, `mode` (as in `outputs.monitors`), `scale`, `transform` (0 to 7), `position` (`X,Y`),
`vrr`, `mirror` (a connector name, or `-` for none), `bit_depth` (8 or 10), `hdr` and `primary`
(`on` or `off`), and prints how many milliseconds the trial lasts; the monitors it leaves out, and
their settings it leaves out, stay as they are. `paw msg monitors keep` keeps the settings on
trial, `monitors revert` takes them back at once, and `monitors reset` puts the configuration's
settings on trial.

```sh
paw msg monitors apply DP-3 scale=1.5 position=0,0 HDMI-A-1 mirror=DP-3
paw msg monitors keep
```

A request that cannot be had says why and changes nothing: a monitor that is not connected, a
mode it does not offer, a monitor mirroring one that mirrors another, or none left showing the
desktop (on, and mirroring none). `paw msg get monitors_trial` prints the milliseconds left
on trial, or `-`, and subscribers hear `monitors-trial MILLISECONDS` as a trial starts,
`monitors-kept`, and `monitors-reverted REASON`: `asked`, `timeout`, `reload`, or `refused NAME`
for a monitor that did not take its settings.

`paw msg get monitors` prints what the window shows of each monitor, a tab-separated line
each in the order of `get outputs`: its connector name, "make model serial", whether it is a
laptop's built-in panel (1 or 0), where its settings come from (`override` for a
wlr-output-management client's or `display_mode`'s, `window`, `config` or `default`), whether
it is to be on (1 or 0), whether it shows a picture (`on`, `off`, or `lid` while the lid holds it
off), the monitor it mirrors or `-`, its place in the arrangement (x and y, before the layout is
moved to start at 0, 0), its mode, scale and transform, adaptive sync (`on`, `off`, or `-` where
the monitor has none), the bits per colour channel asked for and drawn in, HDR asked for (`on` or
`off`) and driven (`hdr` or `sdr`), why it cannot have HDR whatever it asks (its EDID, or the
renderer) or `-`, whether it is the primary monitor (1 or 0), and the modes it offers, separated
by commas, its preferred one marked with `*`. Where it shows a picture, its mode, scale,
transform and adaptive sync are what it has now.

### Turning monitors off

A monitor can also be turned off without leaving the layout: it keeps its place, its windows,
workspaces and panels, and only stops showing a picture, as in a monitor's power saving, where
`enabled = false` takes it out of the layout and sends its windows to another. The
`display_off`, `display_on` and `display_toggle` actions turn every monitor off or on, or the
one they name (`paw msg display_off HDMI-A-1`, or `output = "HDMI-A-1"` in a binding, a
connector name or `"desc:"` description as in `outputs.monitors`); toggling turns them all off
while any is on. Clients of wlr-output-power-management, such as `wlopm`, do the same, and so do
idle daemons through them (swayidle with `wlopm --off \*` and `wlopm --on \*`).

```lua
bindings = {
    { mods = { "Super", "Alt" }, key = "Escape", action = "display_off" },
    { mods = { "Super", "Ctrl" }, key = "F12", action = "display_toggle", output = "HDMI-A-1" },
},
```

Once every monitor is off, whoever turned them off, a key press, a click, a scroll or the
pointer moving turns them all on again, as on KDE and Windows; the key that does it does nothing
else, since nobody saw what it would do. For a second after an action turned them off input
leaves them off, so that the keys that did it, coming up, or a mouse nudged on the way do not
bring them straight back. A monitor turned off while another stays on stays off until it is
turned on.

A monitor that is off draws no frames, so the windows on it alone are not asked to draw
either, and `paw msg get outputs` ends its line with `off`. wlr-output-management clients
still list it as enabled, as sway does, and a reload or `wlr-randr` leaves it off: turned on
again, it takes whatever settings they gave it meanwhile. A screen lock does not wait for a
monitor that is off; the lock covers it from its first frame once it is on.

### The laptop lid

Closing a laptop's lid while another monitor is on turns its built-in panel (a connector named
eDP, LVDS or DSI, as `eDP-1`) off, as KDE and Windows do: the panel leaves the layout and its
windows move to another monitor, keeping their workspaces, as when a monitor is unplugged.
Opening the lid brings the panel back and, with `outputs.return_windows`, its windows with it.
The panel follows the other monitors too: one plugged in while the lid is closed turns the panel
off, and unplugging the last one brings the panel back on. A lid that is closed as paw
starts holds the panel off from the start: logind says so (its `LidClosed`, which paw reads
and follows over the system bus), and libinput does as the lid switch appears when it knows that
switch to be reliable.
With no other monitor nothing changes: the panel stays on, and logind decides what closing the
lid does (by default it suspends, and paw locks the screen first, see [Power](#power)).
`outputs.lid = "ignore"` leaves the panel on whatever the lid does. Opening the lid counts as
input, so monitors the [idle steps](#power-saving-when-idle) turned off come on with it.

Bindings can act on a switch, as Hyprland's `bindl = , switch:on:Lid Switch` does: `switch`
is `"lid"`, with `state` `"close"` or `"open"`, or `"tablet"` (a convertible's tablet mode), with
`"on"` or `"off"`. They take no key, button or modifiers, and run even while the session is
locked, so `locked` and `repeats` are for key bindings alone; a [binding
mode](#binding-modes) holds none.

```lua
bindings = {
    { switch = "lid", state = "close", action = "lock" },
    { switch = "tablet", state = "on", action = "spawn", command = { "wvkbd-mobintl" } },
},
```

`paw msg get switches` prints `lid closed` or `lid open`, then a line per switch device: its
name, whether it says the lid is closed and whether it says tablet mode is on (1 or 0).

## Input

The `keyboard` table sets the XKB `layout`, `variant`, `model`, `options` and `rules`, and the
`repeat_rate` and `repeat_delay` clients see. An unknown combination is rejected with the rest of
the file, with xkbcommon's reason. `keyboard.file` names an XKB keymap to use instead, absolute,
under `~/`, or relative to the configuration: what `xkbcli compile-keymap --layout us,no >
keymap.xkb` writes, edited to taste, or a hand-written `xkb_keymap { ... }`. A file that cannot
be read or does not compile is an error like any other, shown with the line of the setting and,
where xkbcommon knows it, the line of the keymap (`init.lua:4: keyboard.file:
/home/me/us-custom.xkb:12:5: syntax error`); should it break while the session runs, the
keyboard falls back to the names. Saving an `.xkb` file beside the configuration reloads it, as
saving a `.lua` file there does, so a fixed keymap brings the configuration back.

Pointer devices in a standalone `--session` take `mouse.speed` (-1 to 1),
`mouse.acceleration` (`"flat"` or `"adaptive"`), and `mouse.natural_scroll`; touchpads also
take `touchpad.natural_scroll`, `tap_to_click`, and `disable_while_typing`. Unset settings
keep each device's defaults, and a reload applies changes. Nested sessions get their pointer
from the host, so these do nothing there.

The cursor is the theme and size `$XCURSOR_THEME` and `$XCURSOR_SIZE` name. Without them, paw
takes those the desktop's settings name, the ones GTK applications draw over their windows:
`org.gnome.desktop.interface`'s `cursor-theme` and `cursor-size` in gsettings (what GNOME's
settings, nwg-look or HyDE set), else `gtk-cursor-theme-name` and `gtk-cursor-theme-size` in
GTK's `settings.ini`, provided the theme is installed (in `~/.local/share/icons`, `~/.icons` or
`/usr/share/icons`). Failing that it is Adwaita at 24 pixels. paw exports both variables, so that
the programs it starts draw the same cursor, and reads them as it starts: a change takes a
restart.

### Keyboard layouts

Several layouts are a comma-separated list, with variants in the same places:

```lua
keyboard = { layout = "us,no", variant = ",nodeadkeys", options = "grp:alt_shift_toggle" },
```

Every keyboard types in the same layout. The `switch_layout` action moves them all to the next
one, wrapping; a binding's `layout = "prev"` goes back instead, and `layout = 2` picks the second
(`paw msg switch_layout`, `switch_layout prev`, `switch_layout 2`; a number past the last
layout is refused). An XKB option such as `grp:alt_shift_toggle` switches from the keyboard
itself, and the other keyboards follow it. Virtual keyboards (wtype, on-screen keyboards) keep
the keymap and layout they bring. Unbound by default:

```lua
{ mods = { "Super", "Alt" }, key = "space", action = "switch_layout" },
```

A reload that changes the keymap gives it to every keyboard at once, keeping the keys held and
the locks such as Caps Lock, and the active layout where the new keymap has it (by name, else
by place); one that leaves the keymap as it was leaves the keyboards alone.
`paw msg get keyboard` prints where the keymap comes from (`source rules`, or `source file
PATH`), a line per layout (`layout`, its number, 1 for the active one, the short name the panel
shows, such as `us` or `no`, and its name), and a line per keyboard (`keyboard`, the layout it
types in, how many its keymap has, 1 for a virtual one, the modifiers it holds and has locked as
bits: Shift 1, Caps Lock 2, Ctrl 4, Alt 8, Num Lock 16, Super 64, and its name), tab-separated.

### Input methods

An input method (fcitx5) types Chinese, Japanese, Korean and compose-heavy layouts into Wayland
applications, as on sway, Hyprland and KDE: applications ask for text through
text-input-unstable-v3, the input method connects through input-method-unstable-v2, and the
compositor relays between them. Start it with the session, from `startup = { { "fcitx5", "-d" } }`
or its XDG autostart entry in a standalone session; one input method is connected at a time, and
another is told it is unavailable.

The text input of the window with the keyboard turns the input method on and off as the
application's text field gains and loses focus, and tells it the text around the cursor, what
kind of text it is (a password, a number, an e-mail address) and where the text cursor is. What
the input method composes is shown in the field as it is typed (the preedit), and the text it
commits goes in. While it grabs the keyboard, which fcitx5 does while it is on, every key no
binding takes goes to it rather than to the application, and it types what it does not use on a
virtual keyboard of its own, which reaches the application as wtype's keys do; the bindings keep
their keys. Its candidate window sits under the text cursor, moved in from the sides of the
monitor and above the cursor where there is no room below, over the windows and panels and
under the shell's overlays, but over an overlay's own search field, such as the start menu's. As
the keyboard moves to another window the input method follows it, and as an application or the
input method goes away the other side forgets it. The session locked, the input method is off and
the lock screen gets the keys as typed.

GTK 3 and 4 and Qt 6 applications use text-input-v3 by themselves, with `GTK_IM_MODULE` and
`QT_IM_MODULE` unset (set to `fcitx` they talk to fcitx5 over D-Bus instead, which works too but
places the candidate window less well). Chromium and Electron applications need
`--enable-wayland-ime --wayland-text-input-version=3`; older versions speak only
text-input-unstable-v1, which paw does not offer. X11 applications use XIM through Xwayland
with `XMODIFIERS=@im=fcitx`. ibus's own Wayland frontend is made for the older
input-method-unstable-v1 of KDE and Weston, which paw does not offer either, so ibus reaches
applications through its GTK and Qt modules (`GTK_IM_MODULE=ibus`), as on sway.

`paw msg get input_method` prints `input_method` with 1 or 0 for whether one is connected,
whether it is on and whether it grabs the keyboard; a line per text input, `text_input`, whether
it is enabled, whether the input method serves it, and the surface it is on as `get seat` names
it (`-` for none); and a line per candidate window, `popup`, whether it shows, its place and its
size; tab-separated. `input_method_smoke` tests it with `input_probe --text-input` and
`input_method_probe`.

### Mouse button bindings

Bindings can use a mouse button (`left`, `right`, `middle`, `side`, `extra`, `forward`,
`back`; most mice send `side` and `extra` from their back and forward thumb buttons) in place
of `key`, with or without `mods`. `app_id`, a regular expression, limits one to windows under
the pointer whose app ID matches, and `desktop = true` to the bare desktop; with neither it
acts anywhere. Everywhere else the click reaches the application as usual. Several bindings may
share a button, and the first that matches wins (`action = "none"` hands the click back).
Close, fullscreen, and other window actions act on the window under the pointer, and do nothing
over the desktop. This closes terminals and opens new ones from the thumb buttons, while
browsers keep their own back and forward:

```lua
{ button = "side", app_id = "^(kitty|foot)$", desktop = true, action = "close" },
{ button = "extra", app_id = "^(kitty|foot)$", desktop = true, action = "terminal" },
```

The first window to open within five seconds of a button binding's `spawn` or `terminal` opens centered on
the click, kept inside the area the panels leave free, unless a window rule gives its
`position` (`tests/spawn_at_pointer_smoke.py`). A tile still goes where the layout puts it.

The wheel binds the same way, as Hyprland's `mouse_up` and `mouse_down`: `button = "wheel_up"`
or `"wheel_down"` runs the action once for each notch the wheel turns that way, and the
application under the pointer does not get the turn. A touchpad's scrolling and a
high-resolution wheel add up to notches (15 units of a touchpad's scrolling make one), and
`natural_scroll` turns the way around as it does for applications. A window action acts on the
window under the pointer; any other on the monitor under it, focusing nothing, so that Super +
the wheel pages through the workspaces of whichever monitor it is on. The default configuration
binds that, Super + `wheel_up` to `workspace_next` and Super + `wheel_down` to `workspace_prev`,
which wrap around past the last and the first workspace; `action = "none"` in a configuration
of your own hands the wheel back to applications. With `zoom.scroll_modifier = "Super"` the
wheel zooms instead, as that setting takes the wheel first.

```lua
{ mods = { "Super" }, button = "wheel_up", action = "workspace_next" },
{ mods = { "Super" }, button = "wheel_down", action = "workspace_prev" },
```

### Touchpad gestures

Three fingers swiped sideways on a touchpad move to the next or previous workspace of the monitor
under the pointer, as on Hyprland, GNOME and macOS: the windows follow the fingers through the
workspace slide (`animations.workspace`), sliding and fading out as the next workspace's slide in,
and once the fingers lift the slide goes on to that workspace if they went past half of
`gestures.distance`, or back if not. A flick, a quick swipe however short, goes on; one flicked
back the other way at the end stays. Back past where they began, the fingers head for the other
neighbour. There is none before the first workspace or after the last, and rather than wrap around
as `workspace_next` does, the windows stretch that way like a rubber band, showing the desktop
behind them: they follow the fingers as far as the slide would at first, then less and less the
further the fingers go, never as far as the slide's whole distance or a tenth of the monitor's
width, whichever is less, and a whole step of `gestures.distance` takes them half that far (a
slide longer than a tenth of the width starts as one of a tenth would). With
`animations.workspace.distance = 0`, a slide that only fades, they dim a little instead. Back the other way they come back as they went, and on past where they rest the slide
toward the other neighbour begins. Once the fingers lift they spring back with the `move`
animation, going on at first as fast as the fingers went, and fingers that come back to the
touchpad meanwhile take them over where they are. Three fingers up open the overview the same way,
the windows gliding into its grid with the fingers, and down close it; sideways while it is open,
it shows the next or previous workspace as the fingers lift, as its strip does. These are the
defaults; `gestures` changes them:

```lua
gestures = {
    distance = 300, -- the fingers' travel for a whole workspace, in the touchpad's units
    invert = false, -- true: swipes count the other way, and the workspaces move against them
    swipes = {
        { fingers = 3, direction = "left", action = "workspace_next" },
        { fingers = 3, direction = "right", action = "workspace_prev" },
        { fingers = 3, direction = "up", action = "toggle_overview" },
        { fingers = 3, direction = "down", action = "overview_cancel" },
        { fingers = 4, direction = "down", action = "peek_toggle" },
    },
},
```

A list of `swipes` replaces the default one. A swipe's `action` is a request as a hot corner's
is (`workspace 2`, `spawn foot`, `move_to_workspace 3`), run as the fingers lift past half of
`distance` or flick; `none` takes the swipe for nothing. `workspace_next` and `workspace_prev`
follow the fingers as above, and so do `toggle_overview` (which opens the overview, or closes it
when it is open) and `overview_cancel` (which closes it). A swipe is the compositor's only once it
has a direction, after about 16 pixels of travel: until then nothing moves and the window under
the pointer hears nothing, and if its fingers and direction are no swipe's, the window gets it
from its beginning, the travel it waited through at once. Every swipe of other finger counts,
every pinch and every hold goes to the window, as does everything with `gestures.enabled = false`
or while the screen is locked. `paw msg get gesture` says what the swipe under way is
(`waiting`, `passed`, `workspace`, `overview`, `action`, or `none`), with its fingers, direction
and progress in thousandths of a step, and for workspaces a line with the output, the workspace
it began on and the one it heads for (from 1, 0 for none), how far the slide is held, how many
windows are held and coming in, and past the first or last workspace how far the windows stretch
(in pixels, negative to the left) and their opacity (in thousandths).

Swipes of three or more fingers, pinches and holds the compositor does not take go to the window
under the pointer through pointer-gestures-unstable-v1: Firefox and Chromium zoom a page on a
pinch, Firefox stops kinetic scrolling when fingers come to rest on the touchpad (a hold), and
GTK 3 and 4 applications get their swipes and pinches. The pointer stays where it is while the
fingers move, and the window it is on gets the gesture whether it has focus or not; a window that
never asked for gestures gets none. Two fingers scroll, as before.

### Touchscreens

A touchscreen's fingers go to what is under them, as on sway: each to the window, panel or popup
it comes down on, through wl_touch, several at once on one window or on several, and the window
(or a panel that takes the keyboard) a finger comes down on takes focus as it would from a click.
A finger keeps the surface it came down on until it lifts, wherever it moves. GTK, Qt, Firefox and
Chromium take touch themselves, the shell's panels and menus too, and X11 applications through
Xwayland. An application that never asks for touch gets the first finger as the pointer and its
left button instead, and so do the window controls (a tap on one acts as a click), a window's
drag strip (a finger on it drags the window), the overview and the bare desktop; one finger at a
time stands in for the pointer, which follows it.

A touchscreen covers the monitor `touch.output` names, by connector or `desc:` as
`outputs.monitors` does; else the monitor its device names (udev's `WL_OUTPUT`); else the
built-in panel, a connector whose name starts with eDP, LVDS or DSI; else every monitor at once.
Plugging a monitor in or out, or a reload, maps it again:

```lua
touch = { output = "eDP-1" },
```

Applications see a touch capability only while a touchscreen is plugged in, so that none binds
wl_touch on a desktop without one. `paw msg get touch` lists each touchscreen and the
monitor it is mapped to (`-` for every one), the finger standing in for the pointer (`pointer
ID`), and each other finger down with the surface it went to (`point ID`, as `get seat` names
surfaces).

### Drawing tablets

A drawing tablet's pen, or its eraser, brush or airbrush, reaches the window under it through
tablet-v2, which Krita, GIMP, Inkscape and GTK and Qt applications read pressure from: its
position, pressure, distance from the tablet, tilt, rotation, slider and wheel, its tip touching
down and lifting, and its buttons. The cursor follows the pen. The tip coming down focuses the
window as a click would, and the window keeps the pen while the tip is down, wherever it goes, so
that a stroke may leave its edge. Over a window that never asks for tablet input, the window
controls, a drag strip, the overview or the desktop, the pen is the pointer instead: its tip the
left button, its lower button the right one and its upper button the middle one. A pad's buttons
(the keys beside the tablet's area), rings and strips go to the window with the keyboard, when it
takes tablet input.

A tablet's area spans every monitor, or the one `tablet.output` names, by connector or `desc:` as
`outputs.monitors` does; a reload, or the monitor coming or going, maps it again:

```lua
tablet = { output = "DP-1" },
```

`paw msg get tablet` lists the tablets and the monitor each is mapped to (`-` for every
one), the pads and their tablets, and each tool that came near one: its type, whether it is near
now, and what it is over (a surface as `get seat` names it, `pointer`, or `-`). Every tool is
absolute, a tablet's mouse too, and its area keeps the tablet's whole surface: there are no
settings yet for a part of it, its proportions, a left-handed turn or a pad's modes.

### Bindings on the lock screen, and held keys

A key binding with `locked = true` runs while the session is locked too, as Hyprland's `bindl`
does: the shipped volume, microphone and brightness keys are such, so they change the sound and
the screen from the lock screen. Every other key goes to the lock screen as before, and the
control socket still refuses actions while it is locked. Any action may be marked; one with
nothing to do while the session is locked (focusing or moving windows, the switcher, the
overview) does nothing, and one that ends the session (`quit`, `logout`) ends it from the lock
screen, so mark those with care.

A key binding with `repeats = true` runs again while its key is held, after
`keyboard.repeat_delay` and at `keyboard.repeat_rate`, as Hyprland's `binde` does; the shipped
volume and brightness steps do, and keyboard resizing does unless `repeats = false`. (Lua keeps
the word `repeat` for its own loops.) One that is not `locked` stops as the session locks. Mouse
button bindings take neither.

```lua
{ mods = {}, key = "XF86AudioRaiseVolume", action = "volume_up", locked = true, repeats = true },
{ mods = { "Super" }, key = "F12", action = "night_light_toggle", locked = true },
```

`locked_bindings_smoke` tests both, holding keys on and off the lock screen.

### Binding modes

Binding modes are sway's modes and Hyprland's submaps: named lists of key bindings in `modes`, one
of which may be in use at a time. A binding with `action = "mode", mode = "NAME"` puts one in use,
from a key, a mouse button or `paw msg mode NAME`; its bindings then take the place of
`bindings`, so that bare keys can do the work, and every other key reaches the focused application
as usual. `mode = "default"` (`paw msg mode default`) brings the bindings outside any mode
back. Mouse button bindings and `mouse.modifier` stay as they are. The shipped configuration has a
resize mode, commented out, entered with Super + Alt + R:

```lua
modes = {
    resize = {
        { key = "Left", action = "resize_left" },
        { key = "Right", action = "resize_right" },
        { key = "Up", action = "resize_up" },
        { key = "Down", action = "resize_down" },
        { key = "Escape", action = "mode", mode = "default" },
        { key = "Return", action = "mode", mode = "default" },
    },
},
```

A mode's bindings take keys only, with any action and `locked` or `repeats` as elsewhere (resizing
repeats while held). A mode's name is up to 32 letters, digits, `-` and `_`, not `default`, and
there are at most 16 modes. A mode without a binding with `action = "mode"` to leave it is refused,
since it would keep every other binding away, and so is a `mode` that no mode has, with a
suggestion for a misspelled one.

While a mode is in use the taskbar, and the macOS style's menu bar, show its name on a pill in the
accent colour; a click on it leaves the mode. `paw msg get mode` prints the mode in use
(`default` outside any), and subscribers hear `mode NAME` as it changes. A reload leaves the mode,
since the modes may have changed, and so does locking the session, so that the keys the lock
screen leaves to bindings, the volume keys among them, are the ones outside any mode.
`binding_modes_smoke` tests them, and `shell_ui` the pill.

### Applications that take the shortcuts

A virtual machine (virt-manager, QEMU, GNOME Boxes), a remote desktop client (Remmina, FreeRDP,
Moonlight) or a game may ask for the keys the bindings take, through
keyboard-shortcuts-inhibit-unstable-v1, so that Super, Alt + Tab and the rest reach the system
it shows. While its window has the keyboard no binding runs and every key goes to it; once
another window has the keyboard, or the window lets go or closes, the bindings are back. The one
binding that still runs is `toggle_shortcuts_inhibit`, Super + Shift + Escape: it takes the keys
back from the focused window, which hears that its request is no longer honoured, and gives them
to it again, the on-screen display saying which. A window turned off this way stays off until the
binding turns it on again or it asks anew. The first time a window takes the keys, a notification
says so and names the binding ("Shortcuts go to win11 on QEMU/KVM", "Super + Shift + Escape gives
them back"); while notifications are off or do not disturb is on, the on-screen display says the
first part. A window that asks again, as a GTK 3 application may each time it grabs the
keyboard, is not announced again.

Switching virtual terminals (Ctrl + Alt + F1 to F12) and mouse button bindings work as ever, and
so does the lock screen: the `locked` bindings run there whatever a window asked for. A binding
mode in use is left as a window takes the keys, as locking leaves it, so that the binding that
takes them back is the one outside any mode.

An X11 window that grabs the keyboard (XGrabKeyboard), as VirtualBox, Xephyr and X11 remote
desktop clients do, holds the keys the same way: Xwayland passes the grab on through
xwayland-keyboard-grab-unstable-v1, which only Xwayland is offered, and while the window has the
keyboard its grab is a request like any other, ending as the window lets go. A grab never moves
the keyboard to its window.

`keyboard.shortcuts_inhibit = false` refuses every application, and a window rule's
`shortcuts_inhibit = false` the windows it matches; a refused window never hears that its request
is honoured, and the binding does not turn it on. A reload applies a change to either at once.

```lua
keyboard = { shortcuts_inhibit = true },
windows = { rules = { { app_id = "^org.remmina.Remmina$", shortcuts_inhibit = false } } },
```

`paw msg get shortcuts` prints `inhibited 1` while the keys go to the focused window (else
`0`), then a line per request: `inhibitor` (`grab` for an X11 window's), `active`, `off` (turned
off with the binding) or `refused`, 1 when its window has the keyboard, and the window as `get
seat` names it (`window` and its title), tab-separated. `shortcuts_inhibit_smoke` tests it with
windows of `input_probe --inhibit`, and `xwayland_grab_smoke` with `x11_probe`'s grab.

### Nested sessions

The host compositor can consume shortcuts before the nested compositor receives
them: a host that grabs Super (Hyprland, GNOME) keeps these, so set `mod = "Alt"` in
the Lua file for nested sessions. SIGHUP also requests a reload, and
SIGINT/SIGTERM requests shutdown. A reload does not rerun startup commands.

## Appearance profiles

A profile is a named set of look settings, from `appearance`, `windows` and `shell`, that is laid
over the rest of the configuration (and its theme and defaults) while it is in use. Settings a
profile leaves out keep their usual values, and switching profiles changes nothing else:

```lua
profile = "default", -- the one to start with
profiles = {
    default = {},    -- the configuration as it is
    light = {
        appearance = { background = "#dfe4ec" },
        windows = { border_color = "#3d6fd9", border_inactive_color = "#c3cad6" },
        shell = { accent = "#3d6fd9", panel_color = "#f4f6fa", text_color = "#1b2230" },
    },
},
```

Pick one from the panel: the profile button beside the tiling button (three swatches of the
current accent, background and text colours) lists them, the one in use marked, whenever there
are two or more (`shell.widgets.profiles = false` hides it). Right-clicking empty bar space and
pointing at **Appearance** lists them too, beside the menu (the one in use is marked). The command palette (Super + P) lists them as
**Appearance: NAME** too, and scripts and hot corners can use `paw msg profile NAME`, or
`profile next` and `profile prev` to step through them in name order. A pick is saved in
`$XDG_STATE_HOME/paw/profile` (`~/.local/state/paw/profile`) and reloads the
configuration, so the compositor and the shell change together, and it stays after a restart.
While that file names a profile the configuration still has, it wins over `profile`; delete it
to go back to `profile`. Every profile is checked when the configuration loads, so a mistake in
one that is not in use is still reported, with its line. The example configuration ships
`macos-light`, where it starts, and `macos-dark` ([the macOS style](#the-macos-style)), the
taskbar's `default` and `light`, and `washi` and `sumi`, a rice of paper and ink by day and by
night: a floating bar framed as the windows are (`shell.frame`), hard printed shadows and
gradient borders in light blue and pink, on sakura paper by day and indigo by moonlight, the bar
in the paper's own colour, a chibi neko on the start button (`shell.start_icon`), and wallpapers of
their own in [`rices/washi`](../rices/washi/README.md) beside the configuration, with themes
for kitty, fastfetch, Firefox, Discord, Claude Code and editors that its `install.sh` puts in
place; and `mochi`, [Catppuccin](https://github.com/catppuccin/catppuccin) Mocha under frosted
glass with a round cream cat on the start button, whose wallpapers and themes (the terminal,
editors and Firefox toolbars translucent for paw to blur behind, and GTK 4 too) are in
[`rices/mochi`](../rices/mochi/README.md) with an `install.sh` of the same kind; a file with
`extends = "default"` gets them, and starts on `macos-light` unless it sets
`profile`, only when it defines no `profiles` of its own. A profile wins over a theme.lua that
`paw import` wrote, so pick `default` to see that. A rice installed with [`paw rice`](#paw-rice)
is a profile too, named after it, and `paw rice pack` packs a profile into a file to share.

A profile can take applications between their light and dark styles with it:
`appearance.color_scheme = "light"` or `"dark"` sets `org.gnome.desktop.interface color-scheme`
to `prefer-light` or `prefer-dark` through `gsettings` as the configuration loads and whenever a
profile with another scheme is picked. xdg-desktop-portal passes it on, so GTK 4 and libadwaita
applications, Firefox, Chromium and Electron applications such as Discord (with Appearance → Sync
with computer) change at once, and washi's Firefox and Discord themes turn to sumi and back with
it. `macos-light`, `light` and `washi` are light, `macos-dark`, `sumi` and `mochi` dark. A
profile without it leaves the preference as it is, and a choice made in other settings meanwhile
stands until a profile with another scheme is picked. Only a login session (`--session`) sets
it: a nested or headless paw leaves the desktop it runs in alone.

Switching profiles is one crossfade on every monitor. Each keeps showing what it showed, a picture
of it laid over everything, while the compositor and the shell take up the new look under it, so
that no frame shows the old bar with the new borders; once the shell has drawn the new look, its
wallpaper loaded (a moment, typically a quarter of a second), the picture fades out over 400
milliseconds, easing in and out. `animations.speed` scales the fade, and with
`animations.enabled = false` the profile changes at once. The pointer and the keyboard reach the
new look at once, as with every animation: the pointer moves over the picture, and what is under
it takes the clicks. Without the shell the picture fades at once, and a shell that has not drawn
the new look within a second is not waited for longer. A monitor that is off, or shows a
fullscreen window (outside the overview and the window switcher), gets no picture, so that a
video plays on, and one turned off meanwhile loses its own; the idle dimming lies over the picture
as over everything; a mirror shows its monitor's crossfade, and a magnified, turned or scaled
monitor's picture lies exactly where the monitor showed it. Switching again during the fade
starts from what shows. Locking the session takes the picture away at once, and no profile
crossfades in while the session is locked.

## Importing a Hyprland setup

`paw import ~/.config` carries an existing Hyprland/Waybar setup over: monitors, colors
(wallbash or pywal), bar look, gaps, borders, opacity, input, animations on/off, and
wallpaper. It runs
`hyprland.lua` in a sandbox (or parses `hyprland.conf`), writes `theme.lua` beside the
configuration, and reports where each value came from and what it skipped. `init.lua` loads
it with `theme = "theme.lua"` and overrides any of it; `--dry-run` only prints. See
[docs/dotfile-import.md](dotfile-import.md) for the details and for the settings paw
still lacks (rounding, shadows).

## Notifications and on-screen display

The shell is a notification daemon. It owns `org.freedesktop.Notifications` on the session bus
(spec 1.2: `Notify`, `CloseNotification`, `GetCapabilities`, `GetServerInformation`, and the
`NotificationClosed` and `ActionInvoked` signals), so `notify-send`, browsers, chat clients and
`paw`'s own screenshot message reach it without setup. Each notification is a card in the top
right corner of the monitor that has the focus (`notifications.position` moves it to another
corner); further cards stack under it and slide in and out.

- The card is headed by the application's icon and name (from the `desktop-entry` hint or the
  application's name, else `app_icon`) and how long ago it came. Under that come the summary, the
  body (the spec's markup: `<b>`, `<i>`, `<u>`, `<a href>` for web and mail links, `<br>`; anything
  else is dropped) and a progress bar for the `value` hint, beside the picture (the `image-path`
  or `image-data` hint, else `app_icon` when the application's own icon heads the card), and a
  button for each action. A click on the card runs the action named `default`, or dismisses the
  card when there is none; the × that shows while the pointer is on the card dismisses it too.
  A critical card is edged in red.
- `replaces_id` updates a notification in place, and so does a repeated
  `x-canonical-private-synchronous` (or `synchronous`, `x-dunst-stack-tag`) hint from the same
  application, which volume and brightness scripts use. `resident` keeps a notification after
  one of its actions runs, and `transient` keeps it out of the history.
- Cards leave after `notifications.timeout` (6 s), or the application's own timeout, which a
  line along the card's bottom edge runs down (with animations on). Critical notifications stay
  until dismissed. The pointer anywhere on a card, its buttons too, pauses its timer and the
  line, which continue with the time it had left. At most `notifications.max_visible` cards show; the oldest makes room.
- The clock's flyout keeps the last `notifications.history` notifications, by application (the
  one with the newest first; one with more than two shows its newest two until expanded), each
  with its summary, body, picture, progress, how long ago it came and its actions' buttons. The
  clock counts those not yet seen; opening the flyout marks them seen. Clicking an entry runs its
  default action, the × that shows while the pointer is over it removes it, and Clear all empties
  the list. The flyout opens on the monitor under the pointer with `notification_history` too
  (Super + N), and from a bell on the bar with `shell.widgets.notifications = "bar"`.
- Do-not-disturb (`dnd_toggle`, `dnd_on`, `dnd_off`, `paw msg dnd [on|off|toggle]`, a
  right-click on the clock or the bell, the switch in the flyout, the tile in Quick Settings, or
  `notifications.dnd = true` to start that
  way) keeps cards away and lets everything reach the history; critical notifications still show.
  A reload of the configuration does not undo what was toggled.

The on-screen display is a small pill near the bottom centre of the focused monitor with an icon,
a label and a level, which fades out after `osd.timeout` (1.5 s). It appears when the default
sound output's volume or mute changes, whether from the panel's volume control, a media key bound
to `wpctl` or `pactl`, or another program; when a backlight's brightness changes (a laptop's
keys, `brightnessctl`); when do-not-disturb changes; and for
`paw msg osd TEXT [PERCENT]`, where a last word from 0 to 100 (optionally with `%`) is the
level, e.g. `paw msg osd "Keyboard light" 60`. Set `osd.enabled = false`, or only
`osd.volume`/`osd.brightness` to false, to turn it off.

A caption is a larger card in the same place, a title with the keys that do what it tells of and a
sentence under them, for as long as it is asked: `paw msg caption SECONDS
TITLE<tab>SENTENCE<tab>KEYS` shows one on the focused monitor for 1 to 600 seconds (the sentence
and the keys may be left out), a new one taking its place, and `paw msg caption hide` takes it
away. [`paw tour`](#paw-tour) narrates itself with it. It is refused while the shell is not
running, so that a script says it another way.

```lua
notifications = { position = "top-right", timeout = 6000, max_visible = 4, dnd = false },
osd = { position = "bottom", timeout = 1500 },
```

Set `notifications.enabled = false` to run another daemon (mako, dunst): the shell then leaves
the bus name alone, and if another daemon already holds it the shell logs that and serves
nothing (the flyout shows only the calendar). The daemon needs Qt's D-Bus module at build time; without it (or with
`-DPAW_NOTIFICATIONS=OFF`) the shell builds without notifications and the display still works.
Settings are in the [configuration reference](config-reference.md).

The tests start a dbus-daemon of their own on a private address and never touch the session bus:
`notifications_test` (the model, timers and markup), `notifications_dbus_test` (the interface on
that bus), `notifications_smoke` (a headless compositor and shell: cards, a click, expiry, hover,
do-not-disturb, the display and the history), and the configuration tests.

### Volume and brightness keys

The keyboard's volume, microphone and brightness keys work without setup, as on Windows and KDE:
the shipped configuration binds XF86AudioRaiseVolume, XF86AudioLowerVolume, XF86AudioMute,
XF86AudioMicMute, XF86MonBrightnessUp and XF86MonBrightnessDown, and a configuration with
`extends = "default"` gets them unless it binds those keys itself (one without `extends` has only
its own bindings). They work on the lock screen too (`locked = true`), and the steps repeat while
their key is held (`repeats = true`). Their actions:

- `volume_up` and `volume_down` change the default sound output's volume by `amount` percent (5
  unless the binding, or `paw msg volume_up 10`, gives another, up to 100), no further than
  0 and 100 %, and unmute it.
- `volume_mute` mutes the default output, or unmutes it; `mic_mute` does the same for the default
  input, the microphone.
- `brightness_up` and `brightness_down` change the backlight by `amount` percent, stopping at 1 %
  on the way down, since a backlight at 0 is off on some screens.

The compositor hands them to the shell, which changes the sound through the sound server, as the
panel's volume control does, and the backlight through logind, as Quick Settings' slider does,
and shows the on-screen display: the volume, `Muted`, `Microphone muted` or `Microphone on` (with
`osd.volume`), or the brightness (with `osd.brightness`), also when the level is at 0 or 100 %
already. While the shell cannot reach the sound server it runs `wpctl` for the sound instead.
While no shell listens (`shell.enabled = false`, or the shell has exited), the compositor runs
`wpctl` (WirePlumber's, keeping the volume at 100 % at most) and `brightnessctl` itself, where
they are installed; without them the keys do nothing but say why in the log. A step of 2 %:

```lua
{ mods = {}, key = "XF86AudioRaiseVolume", action = "volume_up", amount = 2 },
{ mods = {}, key = "XF86AudioLowerVolume", action = "volume_down", amount = 2 },
```

`volume_keys_smoke` checks the compositor's side with stand-ins for `wpctl` and `brightnessctl`,
`volume_keys_shell_smoke` the keys reaching the shell, and `shell_volume_keys` what the shell does
with them, on a stand-in sound server and a backlight in a made-up sysfs.

## System tray

The panel shows the status icons applications put in a system tray, on every monitor's bar, in the order they appeared. These are StatusNotifierItems, the kind KDE and Qt
applications, Electron applications and Ayatana's indicator library show; the older X11 tray
icons (XEmbed) are not shown. An icon is the item's named icon, looked up first in the folder the
item names and then in the icon theme, or else the pictures it sends, at the size nearest the
panel's. While the item needs attention it shows its attention icon, and an overlay icon sits in
its bottom right corner. Items that say they are passive stay hidden, and so does the tray when
none is left. Hovering an icon shows its tooltip.

- Left-click activates the application, which usually shows or hides its window. An item that is
  only a menu opens its menu instead, and so does one that cannot be activated, as Ayatana's
  (nm-applet's, for one) cannot.
- Right-click opens the item's menu above the icon (below it on a top panel), in the style of the
  panel's own menus: separators, check marks, radio buttons, icons, greyed-out entries, and
  submenus, which open beside their entries, the menu staying open. The application hears of each
  level as it opens and closes, so one that fills a submenu in when asked can. Clicking an entry
  runs it and closes the menu; so do a click elsewhere, Escape, and another right-click on the
  icon. An item without a menu is asked to show its own.
- Middle-click is the item's secondary action, and the wheel scrolls it a notch at a time, which
  some applications use for the volume or to switch between things.

The shell serves `org.kde.StatusNotifierWatcher`, the registry applications register their items
with, on the session bus, and registers as a tray (`org.kde.StatusNotifierHost-PID`). When another
program serves the watcher already, such as another panel running alongside, the shell shows that
watcher's items instead, and takes the name over if that program quits; applications register
again by themselves. `shell = { widgets = { tray = false } }` hides the tray and gives up both
names, so that applications do not think their icons are shown; a reload follows the setting. The
tray needs Qt's D-Bus module at build time; without it (or with `-DPAW_TRAY=OFF`) the shell
builds without a tray.

The tests use a bus of their own and `tray_probe`, an item with a menu of every kind of entry:
`tray_dbus_test` (the watcher, the host, the icons, menus, and a seeded fuzz of malformed items and
menus), `shell_ui` (clicks, the wheel and the menu in the panel) and `tray_smoke` (a headless
compositor with two monitors and the shell, checked with screenshots).

## Starting the session

`startup` in the configuration lists commands the compositor starts once, as it starts (not on
a reload). A standalone session (`--session`, as a display manager or `paw-session` starts
it) then starts the XDG autostart entries: the desktop files applications put in
`~/.config/autostart` (`$XDG_CONFIG_HOME/autostart`) when told to start at login, and those the
system has in `/etc/xdg/autostart` (the `autostart` directory of each of `$XDG_CONFIG_DIRS`). As
the [specification](https://specifications.freedesktop.org/autostart-spec/latest/) says, a file in
your directory hides the system's of the same name, so a copy that says `Hidden=true` turns a
system-wide entry off. An entry is skipped when it says `Hidden=true` or
`X-GNOME-Autostart-enabled=false`, when its `OnlyShowIn` leaves out `paw` (the session's
`XDG_CURRENT_DESKTOP`) or its `NotShownIn` names it, when the program its `TryExec` names is not
installed, and when it would run in a terminal (`Terminal=true`). Its `Exec` line is split into
arguments as the specification says (single quotes too, as GLib and KDE take them), the field codes
for files and URLs (`%f`, `%F`, `%u`, `%U`) are left out, and the program starts in the entry's
`Path` when it has one. A nested session starts none of them: the host session has already.

Entries for what the shell does itself are skipped as well, so that two programs do not fight
over it: another notification daemon while the shell's is on (`notifications.enabled`), and
another tray watcher (`org.kde.StatusNotifierWatcher`) while the shell's tray is on
(`shell.widgets.tray`). They are told by the program the entry runs (after an `env` and its
settings), its `TryExec` or its file name: dunst, mako, swaync, fnott, notify-osd, wired, and any
name with `notifyd`, `notificationd`, `notification-daemon` or `notification-center` in it
(xfce4-notifyd, lxqt-notificationd, mate-notification-daemon) are notification daemons; snixembed,
status-notifier-watcher and the indicator-application-service of Ayatana and Ubuntu are tray
watchers. xembedsniproxy, which shows the old X11 tray icons through the shell's watcher, is
started. Another polkit authentication agent is skipped while the shell's is on
(`shell.polkit_agent`, see [Administrator passwords](#administrator-passwords)): any program or
file name with `polkit` or `policykit` in it (polkit-gnome-authentication-agent-1,
polkit-kde-authentication-agent-1, lxpolkit, lxqt-policykit-agent, mate-polkit, xfce-polkit,
hyprpolkitagent), and soteria. An entry that starts a sound server (any name with `pipewire` or
`pulseaudio` in it, and wireplumber, so Gentoo's `gentoo-pipewire-launcher restart` too) is
skipped while one already runs, its socket (`pipewire-0` or `pulse/native`) being in
`$XDG_RUNTIME_DIR`: started again, it would restart the sound under every program playing, as
when the script that started the session has started PipeWire itself.

```lua
autostart = {
    xdg = true, -- false starts none of them
    exclude = { "org.kde.discover.notifier.desktop" }, -- file names, in either directory
},
```

`paw msg get autostart` prints a tab-separated line per entry: its file name, `started`,
`skipped` or `failed`, the command it ran or why not, and the file that counts. It prints nothing
when autostart did not run (a nested session, or `xdg = false`); the session log counts what was
started and skipped, and says why an entry failed to start.

## Sessions

`paw msg session save NAME` writes what the desktop looks like to
`$XDG_STATE_HOME/paw/sessions/NAME` (`~/.local/state/paw/sessions`): each monitor's
current workspace, tiling on or off and the tiling layouts of its workspaces, and for every
window its monitor, workspace, floating place, tiled, floating, minimized, sticky, maximized,
fullscreen, scratchpad, kept-above and focus state, the app ID and title to find it by, and the command line
it was started with. `session restore NAME` puts every window that matches back where it was (app
ID and title first, then app ID alone; a window is matched once), switches the monitors to their
saved workspaces and refocuses the window that had focus. Windows of applications that are no
longer running are left alone, unless the request is `session restore NAME launch`, which
starts their saved command lines and places each new window as it opens (the first window with
the app ID, within 30 seconds). `session list` prints `NAME`, its window count and when it
was saved; `session delete NAME` removes one. Names are letters, digits, `.`, `_` and `-`.
Applications that do not restart from their own command line (Flatpak launchers, single-instance
programs that hand over to a running copy) are matched but may not relaunch. A window that
floated only because it was snapped or maximized on a monitor that tiles is restored as a tile,
maximized again if it was, so turning tiling on brings it back like any other such window.

For workspaces that tile with the scrolling layout the session also keeps the width of each
column and which column, and place in its stack, each tile had; a restore puts the windows it
finds back into those columns. A window started by `restore ... launch` arrives later and opens
as usual, right of the focused column.

### The last session

As KDE does, a standalone session saves itself as it ends and the next one starts from it. Before
a log out, a quit, a power off or a restart asks the windows to close, the session is saved as
`last` (`~/.local/state/paw/sessions/last`); the next standalone session restores it once
the `startup` commands and the [autostart](#starting-the-session) entries have been started.
`session.restore` says how much of it comes back:

- `"windows"` (the default) puts the windows that open again back where they were: their
  monitor, workspace, place and state, each monitor's workspace and tiling, and the focus. Those
  of a program `startup` or autostart started are placed as they open, for up to a minute, so an
  application started at login comes back on its workspace. Nothing is started.
- `"launch"` does that too, and starts the programs of the other windows again by the command
  line they ran, placing their windows as they open. A window of a program `startup`, autostart
  or `--exec` started already is waited for rather than started a second time: it is known by its
  app ID or its program's name being one of the words of what they started (or an autostart
  entry's file name or `StartupWMClass`).
- `"off"` saves and restores nothing.

```lua
session = { restore = "launch" },
```

`"windows"` is the default because it never starts anything: restarting programs by the command
line they ran also restarts what should not run twice or unasked, such as an installer, a
program run with a file it has since dealt with, one that needs a password at login, or one that
brought the last session down. KDE restores by default, but asks applications how to restart
themselves rather than rerunning their command lines.

A nested session neither saves nor restores the last session, as its windows are the host's
business. A session ended otherwise (`loginctl terminate-session`, a shutdown from a terminal,
SIGTERM) keeps the last one saved before it: its applications may be ending at the same time.
`last` is an ordinary saved session, which `session restore last` puts back by hand too.

## Control socket

A control socket runs any Lua action from scripts or other tools:
`paw msg workspace 2`, `paw msg toggle_tiling`, `paw msg spawn foot`,
`paw msg screenshot window`, `paw msg resize_left 80`. Prefixing
`output NAME` makes workspace and tiling actions switch that monitor instead of the focused
one: `paw msg output HDMI-A-1 workspace_next`. `paw msg type TEXT` types TEXT into
what has the keyboard, as wtype does: each character on a key of a keymap made for it (200 to a
keymap; a longer text takes several), the window's own keymap back after, so that accents, emoji
and their sequences come out as they are whatever the keyboard layout; the request is a line of at
most 512 bytes, and it says when nothing has the keyboard. `paw msg workspace_nudge OUTPUT
next|prev` nudges that monitor's windows as a step to its next or previous workspace would start
to slide them, and back, as the panel's wheel does past either end (see
[Workspaces](#workspaces)). The query
`paw msg get workspace` prints the focused monitor's workspace, `paw msg get workspaces`
prints one tab-separated line per monitor (name, current workspace, focused, the
workspaces holding windows, such as `1,3`, or `-`, and tiling, `on` or `off`),
`paw msg get tiling` prints `on` or `off` for the focused monitor, `paw msg get outputs` prints one tab-separated line per monitor (name,
enabled, x, y, logical width and height, scale, transform, mode, "make model serial",
whether it shows a picture, `on`, or is [turned off](#turning-monitors-off) or disabled, `off`,
the monitor it [mirrors](#mirroring), or `-`, the bits per channel it is [drawn
in](#colour-depth), 8 or 10, and whether it is driven in [HDR](#hdr), `hdr` or `sdr`), and
`paw msg get windows` prints one tab-separated line per window:
workspace, focused, minimized, tiled, x, y, width, height, app ID, title, monitor,
visible, scratchpad (a window hidden there is also minimized), sticky, its window group
(a number; 0 for none), and whether it is [kept above](#keeping-windows-above). `paw msg
get stacking` lists the windows front to back as they are drawn, hidden ones where they would
be: app ID, title, monitor, and the layer it is in (`fullscreen_cover` for fullscreen over the
panels, `peek` for the window peeked at, `fullscreen`, `above`, `floating` for floating
windows kept over tiles, `normal`). `paw msg get dynamic_rules` prints a line per window in
the order of `get windows`: app ID, title, what the [dynamic rules](#window-rules) decide of
floating, sticky and kept above (`on`, `off`, or `-` while none decides it), and which of them
they hold the window to (`floating,sticky,above`, or `-`), one changed by hand being no longer
held. `paw msg get pid_at X Y` prints the process ID of the window
drawn at that layout point, or nothing over bare desktop. `paw msg get layers` prints one line per panel or other layer-shell surface:
namespace, output, layer (0 background to 3 overlay), whether it is shown, and whether it holds
the keyboard.
`paw msg get power` prints one line per power action logind carries out (`poweroff`,
`reboot`, `suspend`, `hibernate`) with its answer: `yes`, `no`, `challenge` (after a password),
`na`, `unknown` until it has answered, or `unavailable` without logind.
`paw msg get animations` prints the number of running animations and of window trees in
the scene (closing and minimizing windows count until their animation ends), and of focus fades,
mainly for tests; `paw msg get drawn` where each window is drawn while it glides to a new place
and size (see [Animations](#animations)), and `paw msg get output_frames` each monitor's name and
how many frames it has drawn. `paw msg get frame_times OUTPUT` prints how many frames that
monitor has drawn, then a line for each of its last 512: the time drawing it took and the time
since its previous frame, in microseconds; without OUTPUT it prints the last 4096 frames of every
monitor together. `paw msg get commits` prints a line per window, the oldest first, then one per
panel or other layer surface: `window` or `layer`, its number (a window's is the one the switcher
and the window control give it), its process ID, how many commits of its surface the compositor
has handled and the time handling them took in nanoseconds, its monitor, its workspace (0 for a
layer surface), its app ID or namespace, and its title (empty for a layer surface).
[`paw top`](#paw-top) shows both. `paw msg get minimize` prints where the taskbars show each window, a
`rect` line per window and monitor (the window's number, the monitor, and x, y, width and height
in the monitor's coordinates, the one that counts first), then a `fold` line per window on its
way into or out of its button (its number, `minimize` or `restore`, how far into the button it
is drawn in thousandths, the box in layout coordinates, what that box is: `button`, `panel` or
`place`, and where the window's box is drawn now), also mainly for tests. `paw msg get pictures`
prints one line per capture source a client asked for to
picture a window at a size, as the taskbar does: the size asked for, the frame's size, how many
sessions capture it, and the window's title, also mainly for tests. `paw msg get
window_peek` prints one line per window for a peek at one window (the taskbar's card's, the
switcher's, the palette's): whether it is the
one peeked at, whether it is drawn, where it is stacked among the windows (0 at the bottom), how
far it shows through the peek and its opacity (both in thousandths), and its title. `paw msg
get scratchpad` prints one line per window in the [scratchpad](#scratchpad) or sliding in or out
of it: its title, whether it is hidden and whether it is drawn, the drop under way (`in`, `out`
or `-`), the edge it goes by (`top`, `bottom`, `left` or `right`), the offset from its place it
is drawn at (x and y), how opaque it is drawn (thousandths), the line it is cut along (`-` while
nothing is cut), the clip its content is drawn with, cut or not (`X,Y,WIDTH,HEIGHT`), and its
radius, and the box its border's rounded rect covers (each `-` for none). `paw msg get bounds`
prints one line per window: its title, the box of the monitor it keeps to as a tile of the
[scrolling layout](#scrolling-layout) (`X,Y,WIDTH,HEIGHT`, or `-`), whether its content is drawn
(`0` while it rests past that monitor's edges), and the clip its content is drawn with
(`X,Y,WIDTH,HEIGHT`) and its radius (each `-` for none). `paw msg get
workspace_views` prints one line per list of a workspace's windows a client watches, as the
card of a workspace does: the output, the workspace and how many windows it last listed. `paw
msg get seat` prints what has the keyboard, what the pointer is on and, while something is
dragged, what the drag is over: a line each, `keyboard`, `pointer` or `drag`, followed by
`window` and its title, `layer` and its namespace, `other` (a popup, the lock screen) and `-`,
or `-` twice for nothing; during a drag the pointer is on nothing. `paw msg get cursor` prints
`hidden` while a screenshot hides the cursor, else `shown`, and `paw msg get cursor_image` the
image it shows: the name of an xcursor (`default`, `grabbing`, `ns-resize`, ..., one paw shows or
a shape a client asks for), `client` for a client's own picture, or `none`. `paw msg get crossfade` prints one
line per monitor with a picture of the old look over it while an
[appearance profile](#appearance-profiles) crossfades in: the monitor, `held` while it waits for
the shell or `fading`, the profile, and the picture's opacity in thousandths; the shell tells the
compositor it has drawn a profile's look with `paw msg profile_drawn NAME`. `paw msg get gesture`,
`get touch` and `get tablet` say what a touchpad's swipe, a touchscreen's fingers and a drawing
tablet's tools are doing (see [Touchpad gestures](#touchpad-gestures),
[Touchscreens](#touchscreens) and [Drawing tablets](#drawing-tablets)), and `get tearing` what
each monitor's frames do (see [Tearing](#tearing)). `paw msg get wrapped` prints whether [paw
wrapped](#paw-wrapped) counts, `enabled yes`, or `enabled no` and why, and while it does the day it counts
for (`day YYYY-MM-DD`), how many times this session saved and when it last did (`saves N`, `saved
SECONDS` since 1970, 0 for never), what the time counts as now (`state active`, `idle` or `away`),
the focused application and its streak so far in seconds (`focus APP SECONDS`, `-` for none), and
then what it counted and has not saved yet, as the lines of a day file. A client
each monitor's frames do (see [Tearing](#tearing)). `paw msg get actions` prints a line per
action the configuration knows: its name and the keys of its first key binding outside any
[binding mode](#binding-modes), as `Super + Tab`, or `-` for none. `paw msg get profiles` prints
a line per [appearance profile](#appearance-profiles), in name order: its name and `1` for the one
in use, else `0`; and `paw msg get terminal` the command line the `terminal` action starts, its
words separated by tabs, or nothing when there is no terminal to start. A client
that sends `subscribe` keeps its connection and receives `tiling on|off` and
`workspace N` (the focused monitor's), one `output NAME N USED TILING` line per monitor
(as in `get workspaces`), one `layout NAME LAYOUT` line per monitor with the tiling layout of
the workspace it shows (`dwindle`, `master`, `spiral`, `monocle` or `scroll`, as in
`get layout NAME`), one `touched NAME EDGES` line per monitor with the edges of its free
area a window shown there reaches (`top,left`, or `-`; a floating bar docks along them), and `keyboard-layout N COUNT SHORT NAME` (the active
[keyboard layout](#keyboard-layouts), from 1, of how many, as in `get keyboard`) after every
change, plus `launcher OUTPUT` when the `launcher` action
(Super + R) asks the panel on that monitor to open or close its start menu; the panel uses this. The window switcher sends `switcher OUTPUT SELECTED COUNT` followed by
COUNT lines `switcher-window APP_ID TITLE OUTPUT WORKSPACE MINIMIZED URGENT ID` (tab-separated) when it
opens or a listed window closes, `switcher-select N` as the selection moves (both counting
from 0), and `switcher-close`. ID is the window's number, which the compositor gives each window as
it appears and never gives another; a taskbar gets the same number for its handle of the window
from `paw-window-control-v1` (version 4), and so finds the window the line means where two
share a title. The shell sends `switcher_peek` once it has drawn the switcher, whose selected
window then peeks once the selection rests ([above](#window-switcher)); while the switcher is
closed the request is refused. The state tells whether the session is locked, `locked on|off`, again as it locks
and unlocks (the shell's clipboard history records nothing meanwhile), and ends with
`power ACTIONS`, the power actions that may
run (`lock,suspend,reboot,poweroff,logout`, in the order menus list them, or `-`), a power
action that fails or is cancelled after it was accepted sends `power-error MESSAGE`, a program
that `spawn` or `terminal` could not start sends `spawn-error MESSAGE` (the panel shows either
across itself for eight seconds), `power_menu` sends `power-menu OUTPUT`, and `taskbar_focus`
sends `taskbar OUTPUT` for the focused monitor, whose panel takes the keyboard to walk its
buttons, or gives it back. The shell subscribes with `subscribe shell`, saying it carries out
the volume, microphone and brightness actions, which send `volume up PERCENT`, `volume down
PERCENT`, `volume mute`, `microphone mute`, `brightness up PERCENT` or `brightness down PERCENT`
to every subscriber; while no client subscribed that way, the compositor runs `wpctl` and
`brightnessctl` for them itself. The state also says the [binding mode](#binding-modes) in use
as `mode NAME` (`default` outside any). Children of the session find the socket through
`PAW_SOCKET`. Actions are
refused while the session is locked.

## Commands

Besides `paw msg` and `paw import`, the `paw` binary has subcommands to find the configuration,
to show the desktop off and to find out what is wrong with it. All but `paw config` talk to the
running session through its control socket, as `paw msg` does; `paw --help` lists them.

### paw config

`paw config` prints the path of the configuration paw starts with: `$XDG_CONFIG_HOME/paw/init.lua`
(`~/.config/paw/init.lua`) where it exists, else the installed default, in which case it also
says so on standard error. So `$EDITOR "$(paw config)"` opens the file in use.

`paw config edit` opens the personal configuration in `$VISUAL`, else `$EDITOR`, else
`xdg-open`. The variable is a shell command, as for git, so `EDITOR="code --wait"` works. When
there is no personal configuration yet it makes one first: a file that extends the default
configuration and names where that is, so that it only needs what you change; paw reads it from
its next start. The editor gets Ctrl+C. Once it exits, a file that changed is checked as
`paw --check-config` does, and an error is printed with its line, the exit status then 1. The
running session reloads the file as it is saved either way.

### paw doctor

`paw doctor` looks the running session over for what has gone wrong in paw desktops before, and
prints a line per check under a heading for each part: ✓ where all is well, ! for something to
look at and ✗ for something broken, the last two followed by the command or step that fixes it.
It fixes nothing itself, and only looks: a `get` query to the compositor, files and `/proc`,
`wpctl status`, `paw --version`, and D-Bus calls that change nothing and start no service. Run it
in a terminal of the session; it checks:

- **Session**: that the compositor answers on `PAW_SOCKET`, and which version it is; that the
  desktop shell runs (when it has gone, the screen shows only the background colour, and the fix
  is `kill -HUP` with the compositor's PID, a reload, which starts it again); that the
  compositor runs the paw installed, not an older build replaced since; when an update script
  keeps a log in `$XDG_STATE_HOME/paw/update.log` (lines `update-paw: DATE TIME building COMMIT`
  and `update-paw: DATE TIME installed COMMIT`), that its last update was installed; that the
  configuration is valid, as `paw --check-config` says, and has the appearance profile last
  picked.
- **D-Bus**: that the session bus answers; that paw's shell shows the notifications, not another
  notification daemon such as mako, dunst or swaync; in a session started with `--session`, that
  no other polkit agent runs beside the shell's; and that logind answers, for suspending.
- **Sound**: that PipeWire and WirePlumber run, WirePlumber on a session bus that still answers,
  and that there is a sound output, not only PipeWire's Dummy Output; the fix restarts them with
  `gentoo-pipewire-launcher restart` where that is installed, else with systemd's user units.
- **Screen sharing**: that `XDG_CURRENT_DESKTOP` has `paw`; that xdg-desktop-portal finds paw's
  `paw-portals.conf`, looked for as it does (portals.conf(5)) with the environment it runs in, the
  fix linking the one paw installed into `$XDG_CONFIG_HOME/xdg-desktop-portal`; that the backend
  it names for screen sharing is installed; and that a running xdg-desktop-portal started with
  `paw` in its `XDG_CURRENT_DESKTOP` and offers screen sharing.
- **X11**: that Xwayland is installed, unless `xwayland = false`.

In a [nested session](#nested-sessions) notifications, password prompts and screen sharing are the
host desktop's, and it says so instead of checking them. On a terminal the report is in colour, unless `NO_COLOR` is set or `--no-color` given. `--plain`
prints a line per check for scripts instead: `ok`, `warning` or `error`, the check's name
(`shell`, `portal-config`, ...), the finding and the fix, separated by tabs. `paw doctor` exits
with 1 when something is broken, else 0.
### paw fetch

`paw fetch` is a fastfetch of the compositor's own: a paw print, and beside it what only paw
knows of the session, where fastfetch tells of the machine.

```
     ▄██▄    ▄██▄        paw 0.1.1 (61f4e8d) · wlroots 0.20.2
    ██████  ██████       ────────────────────────────────────
 ▄▄ ▀████▀  ▀████▀ ▄▄    profile   sumi · dark
████  ▀▀      ▀▀  ████   session   4h12m · 359k frames
▀██▀    ▄▄██▄▄    ▀██▀   frames    avg 0.41ms · p99 1.2ms · worst 25ms
     ▄██████████▄        windows   7 on 3 workspaces · dwindle
    ██████████████       monitors  DP-3 2560x1440@200Hz ×1.25
    ▀██▀ ▀██▀ ▀██▀                 HDMI-A-1 2560x1440@144Hz ×1.25
                         idle      inhibited · dim 9m30s
                         top app   Firefox · 28 launches
                         sessions  last, work
```

The paw print is drawn in the colours of the [appearance profile](#appearance-profiles) in use:
the focused window's border, a gradient laid across the print at its angle as the border draws
it, and a strip under the fields adds the unfocused border's colour and the urgent one. Each
colour is taken just far enough toward black or white to stand out from the profile's
background, which a terminal themed with the profile has too: washi's light blue darkens on its
paper. The fields, each where it is known:

- the version of `paw` and of the wlroots it was built with, and as `running` the session's
  where it runs another (a build installed since it started), or `a paw older than paw fetch`
  where it cannot tell, which then gives only the frames drawn as `session`;
- `profile`: the appearance profile in use and its `color_scheme`;
- `session`: how long the compositor has run, and the frames it has drawn;
- `frames`: how long a frame took on average, at most for 99 frames in a hundred (of the last
  4096), and at worst;
- `windows`: how many are open, on how many workspaces, and the tiling layout of the focused
  monitor's workspace, or `floating`;
- `monitors`: each one's connector, mode, scale, and HDR or 10-bit colour where it is drawn so;
  one turned off says so, and a mirror names the monitor it shows;
- `idle`: the timeouts of [power saving](#power-saving-when-idle)'s steps, and `inhibited`
  while something holds them off, `dimmed`, `on battery`;
- `night light`: the colour temperature while [night light](#effects) warms the screens,
  `scheduled` while it waits; `mode`: the [binding mode](#binding-modes) outside the default;
- `top app`: the application launched most from the shell (the `launches` file in paw's state
  directory), by its desktop entry's name;
- `sessions`: the [sessions](#sessions) saved.

Outside a session (no `PAW_SOCKET`, or one that does not answer) it says `not in a paw
session`, takes the profile and its colours from the configuration paw would start with
(`--config PATH` names another; one with an error gives paw's own colours), and the rest from
the state files. Through a pipe, with `--no-color`, or with `NO_COLOR` set, it writes no
colours; `--no-logo` leaves the paw print out, as a terminal too narrow for it beside the
fields does. It asks the session `paw msg get about`, a line each for the `version`, `wlroots`
(the library's version), `uptime` (milliseconds) and `profile`, and `paw msg get palette`, the
profile's `scheme`, `background`, `border` and `border_inactive` (each an angle and its
colours), `urgent` and `shadow` colours as #RRGGBBAA, besides the queries above.

### paw showcase

`paw showcase` takes the screenshot to show the desktop off with, as an r/unixporn post has it:
the terminal open among the windows with `paw fetch` in it. It saves the arrangement of the
windows as the [session](#sessions) `showcase-PID`, opens the terminal the `terminal` action
opens (the configuration's `terminal`, else `$TERMINAL`, else the first one installed;
`--terminal COMMAND` names another) running `paw fetch` under the title `paw fetch`, with the
text cursor hidden, and waits for its window to show, for the windows' animations to end and for
`--delay SECONDS` more (1 by default). Then it takes the focused monitor, or `--output NAME`, with
[`grim`](https://sr.ht/~emersion/grim/), the pointer's cursor hidden as the
[screenshot action](#screenshots-and-screen-sharing) hides it, and saves the picture as
`paw-showcase-YYYY-MM-DD-HHMMSS.png` in `$XDG_PICTURES_DIR` (from the environment or
`user-dirs.dirs`, else `~/Pictures`), or as `-o FILE`, printing where. Last it closes the
terminal, which puts the other windows back as they were; where something else changed
meanwhile, the saved session restores them. Restoring a session tiles its windows again one by
one, which can change the order of the tiles, so it is left out where closing the terminal was
enough. The session is deleted either way, and `paw fetch` leaves it out of its `sessions` while
it is there. With `--keep` the terminal stays open, handed to the user's shell, and the windows
as they are. A failure, Ctrl+C, a hang-up or a TERM closes the terminal and puts the windows back
the same way, leaving no picture.

The terminal is given what to run after `-e`, as alacritty, ghostty, konsole and xterm take it;
kitty and foot take it as it is, GNOME's terminals after `--` and WezTerm after `start --`. What
runs in it ends as it reads a word from a pipe paw showcase holds, or as the pipe closes when paw
showcase ends however it ends, and the terminal closes with it, so a terminal that keeps one
process for all its windows (`kitty --single-instance`, `footclient`) closes the right window
too.
### paw top

`paw top` shows what the compositor is doing, as htop shows a machine's processes: it looks at
the running session four times a second and redraws the terminal until `q`, Escape or Ctrl+C. `p`
pauses it, and `s` changes the order of the windows. A compositor that has not answered in two
seconds is reported under the first line, and the keys still work.

- The first line names the version, how long the compositor has run, how much of a processor it
  used over the last second, how many monitors it drives and the appearance profile saved last.
- Each monitor has a line with its mode, the frames it draws a second, and the average, 99th
  percentile and worst time the compositor took to draw one of its last 512 frames: green under
  half of the monitor's frame budget (its refresh interval), yellow up to all of it, red over
  it. Under it a graph shows those frames, the newest on the right, against the scale on its
  left.
- Activity is what the compositor did each second: frames drawn, windows' commits, windows
  placed by the layouts, pointer motions, reflows of the tiling and opacity rules matched, with
  the time the frames, commits, motions and reflows took.
- The table lists every window and layer surface (panels, the shell's) with the commits it makes
  a second, the time the compositor spends handling them each second and on each one, its
  workspace, its app ID or namespace, and its title, the most time first; `s` sorts them by
  commits, by app ID or by workspace instead.

The time a commit takes is the compositor's own work as it arrives (the window's frame, its
glide to a new size, the panels' places), not the client's drawing nor the frame it brings; a
popup's or a subsurface's commits (a video some players draw in one) are not counted. Rates are
over the last second. `paw top --once` prints one look, its rates over one interval: as wide as
the terminal, or where it does not print to one as `COLUMNS` says (else 100 columns) and without
colours, as with `--no-color` or `NO_COLOR` set. `--interval MS` sets how often it looks (250
ms). It reads `get stats`, `get outputs`, `get output_frames`, `get frame_times OUTPUT` and `get
commits` (see [Control socket](#control-socket)); from a compositor older than it, the frame
times and the table say they need a newer one.
### paw shake, paw gravity, paw party

Toys, for fun and to point at a window. `paw shake` wobbles the focused window from side to side
for 400 milliseconds, a damped swing dying away: this one, I mean this one. `paw shake N` shakes
the window numbered N instead (the numbers `paw msg get window_icons` lists), and bound to a mouse
button it shakes the window under the pointer. `paw gravity` lets every window of the focused
monitor's workspace fall to the bottom of its usable area: they pile up there and on each other,
the lowest landing first, bounce a few times lower each time, squash a little as they come down,
rest a moment and glide back up, about two seconds in all. A window resting on the bottom already,
a tile, sinks a fifth of the monitor's height past it all the same, behind the bar, and the windows
above land on it. `paw party` throws a party of four seconds on the focused monitor: its windows bob
and wiggle out of step, their borders go round the rainbow, each in its own colour of the moment,
and a couple of hundred pieces of confetti come down from the top, spinning, swaying and fading,
over the windows and the panels. The binding actions `shake`, `gravity` and `party` do the same,
as do `paw msg shake [N]` and the command palette.

A toy only draws: it never moves a window, changes its size or tells its application anything,
`paw msg get windows` saying the same before, during and after, and everything is as it was once it
ends, a gradient border in its gradient again. One plays at a time, a new one ending the last. A
window that closes (it fades out where it is drawn), moves, changes size, is grabbed, minimized or
made fullscreen, or that another animation takes over, drops out of the toy at once, back in its
place, and the toy ends as its monitor shows another workspace, as the overview opens and as the
session locks. A window still in another animation as the toy begins joins it once that is over.
Input goes where the windows really are, as during any animation, and a click while gravity holds
them down sends them back up from where they are, so that what it reaches is what shows there next.
The toys play whatever `animations.enabled` and `animations.speed` say, being asked for by name;
paw has no setting for reduced motion. A window without a border bobs at a party without one.

`paw msg get toys` prints a `toy` line, with the toy playing (`shake`, `gravity`, `party`, or
`none`), how long it has played and how long it lasts in milliseconds, and its monitor and
workspace; a `window` line for each window in it, with its number, its title, whether the toy
draws it yet, the offset it is drawn at (x and y), how it is stretched (across and down, in
thousandths) and its border's colour (`#rrggbb`, or `-` for its own); and a `confetti` line with
how many pieces show. `tests/toys_smoke.py` tests them.

`paw msg get cube` prints a `cube` line, with the monitor and workspace whose view turns as a
cube in the [scrolling layout](#scrolling-layout) (or `-` and 0 when none does), how long the turn
has gone on and how long it lasts in milliseconds, and how far the square is turned (thousandths
of a radian, 0 at rest, positive turning back to the left) and where its sides begin along the
strip; and a `tile` line for each window turning, with its number, its title, whether it is drawn
turned and shown, where it is drawn along the view and how wide (from the view's left edge), and
the scale of its height and how much fainter it is (thousandths). `tests/scroll_cube_smoke.py`
tests them.
### paw rice

A rice is a look to share: an appearance profile's settings and its pictures, in one file.
`paw rice pack` packs the profile in use, or the one it names, into `NAME.rice.tar` in the
current directory (`-o FILE` names another, compressed when it ends in `.gz`):

```sh
paw rice pack                     # the profile in use
paw rice pack sumi -o sumi.tar.gz --author "me" --description "Ink by moonlight."
paw rice pack sumi --extra ~/.config/foot/sumi.ini
```

It takes every `appearance`, `windows` and `shell` setting that says how the desktop looks, as
the profile has it over the configuration, its `theme.lua` and the default configuration it
extends; the wallpaper (the one picked for that profile in the wallpaper picker, if any) and the
start button's picture; a kitty theme named for the profile (`~/.config/kitty/themes/NAME.conf`);
and each `--extra` file. It prints what went in and which settings it left out. `--name` names
the rice (the profile's name), `--author` gives its author (your login name) and `--description`
says what it is.

`paw rice try` tries one on: an archive (`.tar` or `.tar.gz`), a directory holding a
`rice.lua`, or an `https://` URL, which `curl` downloads (50 MB at most).

```sh
paw rice try sumi.tar.gz
paw rice try https://example.org/washi.rice.tar
```

It checks the rice and prints its name, author and description and each setting it changes,
from what to what; installs it in `$XDG_STATE_HOME/paw/rices/NAME` (`~/.local/state/paw/rices`)
as a profile of its name (`NAME-2` where the configuration has a profile of that name; one
installed already of that name waits aside meanwhile); and switches to it, crossfading as a
profile does. In the middle of the screen, "Keep this look?" counts 15 seconds down: **Keep this
look** keeps it, and **Revert**, Escape or the time running out bring back the profile in use
before (with its wallpaper), the rice going again. `paw rice keep` and `paw rice revert` do the
same from a terminal. A kept rice is a profile like the others, in the profile menus and the
command palette, and `paw msg profile NAME` picks it; what the shell writes into the
configuration (the taskbar dragged to another edge or resized) cannot change it, so copy its
settings from its `rice.lua` into a profile of your own to make it yours. The trial is written
down before anything changes, so a session that ends during it starts again in the previous look.

`paw rice list` lists the installed rices, saying which is in use or on trial, and `paw rice
remove NAME` removes one that is neither. `paw msg get rice_trial` prints the rice on trial and
the milliseconds before it goes back, by a tab, or `-`; subscribers hear `rice-trial MILLISECONDS
NAME`, `rice-kept NAME` and `rice-reverted REASON` (`asked`, `timeout`, or `refused` when the
configuration did not take it).

A rice's `rice.lua` names it and holds the profile:

```
return {
    format = 1,
    name = "washi",
    author = "shao",
    description = "Sakura paper and navy ink.",
    profile = {
        appearance = { background = "#f6eef0", color_scheme = "light" },
        windows = { border_width = 2, border_color = { "#8cc8ff", "#ffb7c5", angle = 45 } },
        shell = { accent = "#e0607e", wallpaper = "wallpaper.jpg", start_icon = "start.svg" },
    },
    extras = { "kitty/washi.conf" },
}
```

A rice is data: it cannot run anything, or reach outside itself. `rice.lua` is never run as Lua,
but read by a parser of Lua's table syntax alone, which takes strings, numbers, booleans and tables
and refuses names, calls and operators, so `os.execute`, `io.open`, `require` or `load` in it are
errors, not calls. It sets only how the desktop looks: colours, borders, corners, opacity, shadows,
the blur behind windows, the bar's style, size, place, font and widgets, the tiling bar, the
wallpaper and the start button. Launchers, window rules, the search's folders and web address, the
clipboard, the wallpaper picker's folder, the rest of `windows` and `shell` and every other section
are refused, and `paw rice pack` leaves them out. The pictures and other files it names are paths
inside it, of letters, digits, `.`, `-` and `_`, without `..`, and neither links nor through one;
an archive holding a link, a device or a path out of it is refused whole, and only the files
`rice.lua` names are taken from it. Its pictures are PNG, JPEG, WebP or SVG, which the shell opens
as it opens any wallpaper. Files for other programs, such as a terminal's colours, stay in its
directory for you to put in place; `paw rice try` says where.
### paw cat

`paw cat` lets a cat out onto the desktop, as oneko does, but drawn by the compositor itself, so
that it knows where the windows are. The pixel-art cat sits on the top edge of the focused window,
on its border, or inside its title bar where the window reaches the top of the screen. It wanders
along it, grooms, scratches behind an ear, yawns, and a few seconds after the focus moves it hops
over to the window that has it. When the window it sits on goes from under it (moved, tiled,
minimized, closed, another workspace shown), it starts, falls and lands on the next window top
below, or on the bottom of the monitor, above the taskbar, with a small squash as it lands; it
never lands on a part of a window's top that another window covers. Flick the pointer past it and
it chases the pointer, sitting down beside it once the pointer rests, or pawing at it and dropping
where there is nothing to sit on. A window that asks for attention ([urgent
windows](#urgent-windows)) has it run over and pat that window's top until the window gets the
focus. After `cat.sleep_after` seconds without input (90 unless set), or once the screens dim, it
curls up and sleeps, breathing out a "z" now and then; any input wakes it with a stretch.

It stays on the focused monitor: it crosses to another only along a window that reaches across
both, and otherwise drops in from the top of the monitor the focus moves to. It takes no input, so
it never comes between a click and a window, and it is drawn over the windows but under fullscreen
windows, the panels, the launcher and other overlays, and the lock screen. It hides while the
session is locked, while its monitor or the focused one shows a fullscreen window, while the
overview shows and while its monitor is off, and then nothing of it runs. It draws frames only while it moves: sitting, it
wakes a few times a minute to blink or to think of something to do, and asleep once every two
seconds for its "z".

`paw cat` lets it out or calls it in, and `paw cat on` and `paw cat off` say which; each says
where the cat is then. The `cat` action does the same, from a binding with `mode = "on"`, `"off"`
or `"toggle"` (the default) or as `paw msg cat [on|off|toggle]`, and `cat.enabled` lets it out as
the session starts. Its coat follows the [appearance profile](#appearance-profiles): a black cat
with yellow eyes on a light desktop (`appearance.color_scheme = "light"`), a cream one with blue
eyes on a dark one, and a ginger tabby where neither is set; `cat.coat` picks one, `"grey"`
included, and `cat.scale` its size, in logical pixels to a pixel of its 20 by 16 art. It stays
sharp at any monitor scale.

```lua
bindings = {
    { mods = { "Super", "Ctrl" }, key = "n", action = "cat" },
},
cat = { enabled = true, coat = "auto", scale = 2, sleep_after = 90 },
```

`paw msg get cat` prints a line: `on` or `off`; `shown`, or why it is not drawn (`off`, `locked`,
`away` while another VT is on the screen, `no_output`, `overview` while the overview shows,
`display_off`, `fullscreen`); the monitor it is on, where its feet are (x and y in
layout coordinates), what it is doing (`sit`, `look`, `walk`, `groom`, `scratch`, `yawn`, `sleep`,
`wake`, `jump`, `fall`, `land`, `chase`, `pounce`, `rush`, `knock`), the picture it shows, its
coat, and what it stands on: the window's title, `floor`, or `-` while it is in the air.
### paw wrapped

`paw wrapped` tells you about your time at the desktop as Spotify Wrapped tells you about a year
of music: a bold, coloured card for each thing worth saying, with a comparison to go with it.
"You moved your mouse 4.1 km — about 39 football pitches", your top applications by the time
they had the focus, with bars, your longest focus streak and when it was, your busiest day, how
many keys you pressed against the length of a novel, how often you clicked, scrolled, switched
workspaces and dragged windows about. A card shows only when there is something on it; with
nothing counted yet it says so.

```sh
paw wrapped           # the last 365 days, or all there is when that is less
paw wrapped --today   # or --week and --month (the last 7 and 30 days), --all, --since 2026-03-01
paw wrapped --plain   # lines instead of boxes, as without a terminal or with NO_COLOR set
paw wrapped --json    # the totals, day by day too, for scripts
```

A standalone session (`paw --session`, from a display manager or a TTY) counts, while
`wrapped.enabled` is on, which it is unless the configuration says otherwise; a nested paw counts
nothing, its input being the desktop's it runs in. It counts:

- how far the pointer travelled, in layout pixels and in millimetres on the monitor (from the
  size its EDID gives, else at 96 dots an inch), clicks by button, and how far you scrolled;
- how many keys were pressed: a count only, never which keys, and not those a virtual keyboard
  such as wtype types;
- touchpad swipes and pinches;
- windows opened and closed, moved and resized with the pointer (a press let go in place is not
  a move), and snapped to a half or a quarter by pointer or key; workspace switches; the overview
  and the window switcher opened; programs the `spawn` and `terminal` actions started;
- the time each application, by its app ID, had the focus while you were at the desktop, and the
  longest the focus stayed with one application without a break, and when that began;
- when each session started, and the time active (input in the last two minutes, or something
  keeping the screen awake, as a video playing does) against the time idle, away, locked, with
  every monitor off or another VT in front, none of which counts for an application.

It stays on this machine, in plain text: a file per day in `$XDG_STATE_HOME/paw/wrapped`
(`~/.local/state/paw/wrapped` without it), named `YYYY-MM-DD` and readable by you alone, each line
tab-separated: a counter and its value (`keys 1234`), an application and its seconds (`app firefox
3600`), or the day's longest streak (`streak kitty 8040 START`, START in seconds since 1970). A
session adds what it counted to the day's file every five minutes, as it goes idle, locks or the
machine sleeps, and as it ends, so that a crash loses a few minutes at most; one that runs past
midnight starts the next day's file. `paw wrapped` adds what the running session counted since it
last saved, which `paw msg get wrapped` lists. `wrapped = { enabled = false }` stops the counting,
saving what was counted first; deleting the directory forgets what was.
### paw xray

The `xray` action takes the focused monitor apart into the layers paw draws, so that what lies
over what shows, and closes it again; bind it as `{ mods = { "Super" }, key = "x", action = "xray"
}`. The picture comes apart into a stack of sheets, bottom to top: the background (the desktop's
colour and the wallpaper), the bottom layer, the windows, floating windows (with
`layout.floating_above_tiles`), windows kept above, fullscreen windows, the panels (the top
layer), a fullscreen window covering the panels, X11 menus and the overlays. A layer with nothing
on that monitor has no sheet, and while six windows or fewer show, each has a sheet of its own.
Each sheet is a live copy of what its layer draws there, scaled down, on a card edged in the
layer's colour with a shadow, fanned out from the one under it like a deck of cards seen from the
side. It comes apart from the flat screen and goes back into it in `animations.minimize`'s time
and curve, half as long again to open, and at once with `animations.enabled = false`.

Moving the pointer turns the stack: it leans away from the pointer, so that the edges of the
sheets low in it come out where the pointer is. The wheel, or + and -, spreads the sheets apart and
together, and 0 sets the stack straight again. The sheet under the pointer is lifted out of the
stack and those over it fade, and the shell names it under the stack ("Windows · firefox — Mozilla
Firefox"), beside a legend of every sheet, top first, in its layer's colour. The arrow keys and Tab go up and down the stack, Return or a click
on a window goes to that window, and Escape, a click beside the stack or the action again closes
the view. While open it takes the keyboard and the pointer, as the overview does, and changes
nothing else; closed, nothing of it is left. It does not open while the session is locked, and
closes as the session locks or its monitor goes. `paw xray` sends the action from a terminal.

`paw xray --list` prints what every monitor draws as text, layer by layer from the top down, with
each surface's or window's kind, namespace or app id, title and place, which says why one thing is
drawn over another (in a terminal, a square of each layer's colour leads its line):

    HEADLESS-1, top first:
      top         surface  paw-panel                  1600×52 at 0,848
      above       window   firefox — Mozilla Firefox  320×240 at 72,72
      windows     window   kitty — ~/dev              320×240 at 40,40
      background  surface  paw-desktop                1600×900 at 0,0
      background  colour   paw — #19212e              1600×900 at 0,0

`paw msg get xray` gives the same as tab-separated lines, bottom to top, after a line saying
whether the view is open: output, layer, its colour, kind (colour, surface, window or x11), name,
title, place, and while the view shows that monitor the sheet showing it and where that is
drawn.
### paw heatmap

Where the pointer has been. While the session runs, the compositor counts for each monitor how
long the pointer rested in each part of it and how often it was clicked there, in a grid of cells
8 logical pixels square. A pointer at rest counts for at most ten seconds after it last moved or
clicked, so that time away from the desktop does not outweigh everything else, and nothing
counts while the session is locked. The counts stay in the compositor's memory: they are never
written to disk, and they are gone when the session ends or with `heatmap reset`. Nothing is
counted at all with

```lua
heatmap = { enabled = false },
```

The `heatmap` action shows them over every monitor, over the windows and panels and under the
lock screen: the grid blurred and coloured from cold to hot (clear, blue, cyan, green, yellow,
red, white) on one scale for every monitor, up to the hottest place, the desktop showing
through, with a legend that says what it shows and how much the hottest cell holds, in the
bottom-right corner unless another corner hides much less of the heat. The scale is logarithmic, so that a path the pointer only passed along
shows beside a place it rested at for minutes. The overlay takes no input, so the desktop works
as usual under it; it is drawn again twice a second while it shows, the rest under way included,
and fades in and out with the [animations](#animations). `heatmap` (or `heatmap toggle`) shows
where the pointer rested, or the clicks if those showed last, and hides it again; `heatmap
motion` and `heatmap clicks` show that one, from the other too, or hide it; `heatmap reset`
forgets everything counted. In a binding, `mode` gives the choice:

```lua
bindings = {
    { mods = { "Super", "Shift" }, key = "h", action = "heatmap" },
    { mods = { "Super", "Shift", "Ctrl" }, key = "h", action = "heatmap", mode = "clicks" },
},
```

`paw msg get heatmap [OUTPUT] [motion|clicks]` prints, for each monitor in the layout or the one
named, a line with its name, `motion` or `clicks`, the grid's columns and rows, the cell's size,
and the monitor's place and size in the layout, then a line for each row of cells: the
milliseconds the pointer rested in each, or the clicks there, tab-separated. `paw msg get
heatmap_overlay` prints a line per monitor while the overlay is there: the monitor, `shown`, or
`hiding` while it fades out, what it shows, its opacity in thousandths, what the legend says at
the hot end, and how many times it has been drawn.

`paw heatmap [toggle|motion|clicks|reset]` runs the action from a terminal or a script. `paw
heatmap --term` draws the heatmap there instead, every monitor at its place in the layout,
fitted to the terminal: two pixels a character, in 24-bit colour with half blocks (▀), or in
shades (░ ▒ ▓ █) when `NO_COLOR` is set, with the scale and the hottest cell's count below. `paw
heatmap --export FILE` writes it as an image of a pixel for each logical pixel of the layout: a
`.png` or a `.pam`, clear where the pointer never was, or a `.ppm` over black. `--clicks` shows
the clicks in place of the rest, and `--output NAME` one monitor alone. The counts leave the
compositor only that way, to the terminal or the file named.
### paw tour

`paw tour` shows paw off on the running desktop in about a minute and a half, and then puts it
back as it was. On an empty workspace of the focused monitor it opens three terminals of its own
(`paw fetch`'s card where there is one, a clock and drifting colours) and takes them through
nineteen steps: windows placing themselves and a grid, snapping with Snap Assist, tiling, the
tiling layouts, the scrolling layout, the overview with its search, the window switcher, a tab
group, the scratchpad's drop-down, workspaces, a sticky window, appearance profiles crossfading,
night light, the desktop cat, the x-ray view, the toys and the pointer heatmap. Each step has a caption, a title and a sentence with the keys that do it as
your configuration binds them (`paw msg ACTION` for an action bound to none), which the shell
shows as a card near the bottom of the screen ([`caption`](#notifications-and-on-screen-display))
and the terminal prints.

Before it starts it saves the arrangement as the session `tour` (see [Sessions](#sessions)) and
notes the workspace, the monitor's tiling and the layout of its workspace, night light and the
appearance profile. At the end it closes its own windows, and only those, puts each of those back
(the profile's saved choice in `~/.local/state/paw/profile` and the light or dark preference
applications were told too), restores the session and deletes it. It does the same when stopped:
Ctrl+C in its terminal, closing one of its windows, an error, or the session locking (it then
waits for the unlock to put things back). It refuses to start while the session is locked, while
a window of an earlier tour is open, and when every workspace of the monitor holds windows, and
asks first when a fullscreen window has the focus.

| Option | Does |
| --- | --- |
| `--list` | Prints the steps, with the keys and the steps this session would skip when it can ask one |
| `--step` | Waits for Enter after each step instead of going on by itself |
| `--from N` | Starts at step N |
| `--speed FACTOR` | Goes FACTOR times as fast (0.25 to 100) |
| `--no-restore` | Leaves the desktop as the tour leaves it, for a recording; `paw msg session restore tour` puts the windows back later |
| `--client COMMAND` | Opens the windows with COMMAND, run by `sh`, instead of the terminal; `{title}` in it stands for the window's title, which the tour knows its windows by |

A step needs nothing but what paw has: one that needs a second empty workspace, another
appearance profile, or the scratchpad (while windows of yours are in it) is skipped without it.
The steps are a table in `src/cli/tour_steps.cpp`, one line each: the caption, the actions whose
keys it names, the control requests it runs, how long it stays, and the actions a step for a
newer feature needs (`needs ACTION`), without which it is skipped. `tests/tour_smoke.py` runs the
tour quickly against a headless compositor, interrupted and not, and checks that it leaves
everything as it was.

## Screen locking and idle

Screen locking uses the standard `ext-session-lock-v1` protocol, so lockers such
as swaylock or gtklock work; the `lock` action starts the one `power.lock_command` names
(`{ "swaylock", "-f" }` unless set; `{}` for none), and is refused while that program is not
installed. The desktop is covered
before the locker draws, only the locker receives input, and if it crashes the
session stays locked until a new locker takes over. Idle notification and idle
inhibition (`ext-idle-notify-v1`, `idle-inhibit-unstable-v1`) let swayidle lock
or blank after inactivity while video players keep the session awake; it blanks the monitors
with `wlopm` (see [Turning monitors off](#turning-monitors-off)). Key bindings marked
`locked = true`, the volume, microphone and brightness keys among them, still run while the
session is locked ([Bindings on the lock screen, and held
keys](#bindings-on-the-lock-screen-and-held-keys)). While a standalone
session is on screen it holds a logind sleep inhibitor, so an idle daemon left running by
another desktop on a different VT cannot suspend the machine; switching VTs away releases it.
This needs sd-bus from libsystemd, libelogind, or basu at build time.

### Power saving when idle

paw saves power by itself after a while without input, as KDE and Windows do, so no idle
daemon is needed. The `idle` table gives the seconds after the last key press, click, scroll or
pointer motion at which each step is taken, once until the next input; 0 never takes it:

- `dim`: the screens darken by half, fading in over a second, over everything, the lock screen
  too. Any input brightens them at once. Unset, it is 30 seconds before `display_off` (halfway
  when that is under a minute), so the screens warn before they go dark.
- `display_off`: every monitor turns off as the `display_off` action does
  ([Turning monitors off](#turning-monitors-off)), keeping its windows and panels. A key press
  or the pointer turns them on again, and the key does nothing else; a monitor that was off
  already, turned off by hand, stays off. 600 (ten minutes) unless set.
- `lock`: the screen locks with `power.lock_command`. Off unless set.
- `suspend`: the machine suspends as the `suspend` action does, through logind and locking
  first. When it wakes up, the monitors come on and the steps count again from then. Off unless
  set.

```lua
idle = {
    display_off = 600, -- the screens dim at 570 seconds and go off at 600
    lock = 900,
    suspend = 1800,
    battery = { display_off = 300, suspend = 900 }, -- dimming at 270
},
```

`battery` holds the steps while the machine runs on battery: a battery of its own discharging
(not a mouse's or a headset's) and no mains or USB charger online, as
`/sys/class/power_supply` says. Each step it leaves out is the one above, but for `dim`, which
follows its own `display_off`. The power source is read again at each step and after input, so
unplugging the charger shows from the next step on.

An idle inhibitor (`idle-inhibit-unstable-v1`, which video players, browsers playing video and
games hold) holds every step off, and the screens brighten if they had dimmed; once the last one
goes, the time without input counts from then. So does switching to another VT and back:
nothing happens while another VT is in front. `paw msg get idle` prints a line `state`
with the milliseconds since the last input, whether something holds the steps off, whether the
machine runs on battery and how dark the screens are (thousandths), then a line per step with
its timeout in milliseconds on that power source and whether it has been taken since the last
input.

The defaults turn the monitors off after ten minutes and lock and suspend nothing, as the
screens going dark is what everyone wants and locking and suspending are choices KDE and Windows
also leave open on a desktop. Idle daemons keep working as before: swayidle and hypridle hear of
idleness through `ext-idle-notify-v1`, which these steps do not change, so a setup that runs
swayidle sees only the dimming and the monitors going off after ten minutes, if it does not turn
them off sooner itself. `idle = { display_off = 0 }` turns both off and leaves everything to the
idle daemon; set `power.lock_before_sleep = false` if it also locks before sleep.

## Power

The power button in the bottom-right corner of the start menu, as on Windows, opens a menu above
it of what may run now, each with its icon: Lock screen (with a locker installed),
Suspend, Hibernate, Restart, Power off (as logind allows them) and Log out; the button is left
out when nothing may. A click elsewhere in the start menu closes only the power menu. Its entries, and what is refused, come from the
compositor, which also reports across the panel an action that fails or is cancelled later.
Restart, Power off and Log out ask first: a dialog in the middle of the monitor counts down
`power.countdown` seconds (10; 0 waits for a click) and then goes ahead, as do its button and
Enter, while Escape, Cancel or a click beside it gives up. The keyboard starts on the action's
button, ringed, and Tab moves it to Cancel, where Enter gives up. The actions themselves, bound to keys
or sent with `paw msg`, do not ask. The `power_menu` action opens the start menu
with the power menu up on the monitor under the pointer, with the keyboard in it: Up and Down
choose, Enter runs, Escape closes the power menu.

The `suspend`, `hibernate`, `poweroff` and `reboot` actions ask logind (systemd-logind or
elogind, on the system bus) to suspend, hibernate, power off or restart the machine, letting it
ask for a password when its policy wants one. An action logind does not allow on this machine
(`CanHibernate` answering `no` or `na`, say, without swap to hibernate to) is
refused with that reason; one it turns down later, such as a password not given, is reported
across the panel. `paw msg get power` shows what logind allows. This needs sd-bus from
libsystemd, libelogind, or basu at build time. A headless compositor (`--headless`, as the
tests run it) never uses the machine's logind: only one on the bus that `PAW_LOGIN1_BUS`
names. `logout` ends the session, as `quit` does, and needs no logind.

`poweroff`, `reboot` and `logout` first ask every window to close, as its close button would,
so that applications save their state, and go ahead once the last has gone (a log out also
waits for those applications to disconnect, so one saving as it quits is not cut off). A
window still open after `power.close_timeout` milliseconds (5000), typically an application
asking whether to save, cancels the action and stays on screen with the reason across the
panel; answering it in time lets the action go on. `power.force = true` goes ahead after the
timeout anyway, ending such applications without saving, and `power.close_windows = false`
skips the closing.

`suspend` and `hibernate` first lock the screen with `power.lock_command` and ask logind only
once the lock holds on every monitor, so the machine never wakes up unlocked; a locker that has
not locked within five seconds cancels the suspend. A sleep something else asks for (closing
the lid, an idle daemon, `loginctl suspend`) locks the same way: paw holds a logind delay
inhibitor and lets it go once the lock holds, or when logind stops waiting (its
`InhibitDelayMaxSec`, five seconds by default). `power.lock_before_sleep = false` sleeps
without locking, and so does a session with no locker installed; turn it off if an idle daemon
already locks before sleep (swayidle's `before-sleep`), or two lockers race.

```lua
power = {
    lock_command = { "swaylock", "-f", "-c", "000000" },
    close_timeout = 10000, -- ten seconds for applications to close
    countdown = 5,         -- the confirmation's seconds
},
```

## Administrator passwords

The shell is the session's polkit authentication agent, as KDE's and GNOME's are. When a program
asks polkit for something only an administrator may do (`pkexec`, GParted, an updater, a disk
tool, or powering off while others are logged in, which logind answers with `challenge`), a
dialog in the middle of the focused monitor says what is asked (polkit's message, and the command
`pkexec` would run), names the user to answer as, or lists them to choose from when several may
(the one logged in chosen first, else root), and asks for that user's password. Enter or
Authenticate answers, Up and Down choose another user, Escape or Cancel gives the request up, and
a click beside the dialog does nothing, so that a stray click loses no typing. A wrong password
shakes the dialog and asks again. A request that comes while one is open waits its turn, and one
its program gives up closes. The password goes to polkit's own helper (`polkit-agent-helper-1`),
which checks it with PAM.

Only the shell of a standalone session is the agent, for its login session; a nested one leaves
that to the host's. When another agent serves the session already (polkit-gnome started by hand,
say), the shell says so in the log and stays out of its way; the XDG autostart of a standalone
session leaves other agents out while the shell's is on (see
[Starting the session](#starting-the-session)). `shell = { polkit_agent = false }` leaves it to
another agent, and a reload follows the setting. The agent needs libpolkit-agent-1 at build time;
without it (or with `-DPAW_POLKIT=OFF`) the shell builds without it.

The tests run the agent against a stand-in for polkitd on a private bus (`shell_polkit_agent`),
and the dialog in its view (`shell_auth_dialog`) and its model (`shell_authentication`) with
stand-ins for polkit's helper; none of them reaches the machine's polkit.

## Screenshots and screen sharing

The `screenshot` action (Print, or `paw msg screenshot region|output|window`) runs
[`grim`](https://sr.ht/~emersion/grim/), with [`slurp`](https://github.com/emersion/slurp)
to select a region, and saves `Screenshot_<date>_<time>.png` in `$XDG_PICTURES_DIR/Screenshots`
(from the environment or `user-dirs.dirs`), else `~/Pictures/Screenshots`. The `region` mode
works as Hyprland's grimblast does: slurp offers the windows shown and the monitors, so a click
takes the window under the pointer or, on the bare desktop, the monitor, and a drag takes the
region dragged; Escape cancels. Everything is greyed but what a click or the drag would take,
which shows clear, outlined in the focused window's border colour. A window that one in front
covers whole, as a fullscreen window does, is not offered, and each window is cut to its
monitor. The `output` mode captures the monitor under the pointer and `window` the focused
window's area as it appears on screen, including anything overlapping it. The cursor is left
out: paw hides it while grim takes the picture (for a region, once slurp is done), since a
monitor that draws it into its frames, as some drivers' do, would hand it to grim. A screenshot
taken another way can hide it too: `paw msg cursor hide` hides it until `cursor show`, or for
five seconds at most, as [paw showcase](#paw-showcase) does. It also
copies the image to the clipboard with
`wl-copy` and announces the file with `notify-send`, when they are installed. Lua
`screenshots = { directory = "~/Shots", clipboard = false, notify = false }` changes that; in a
binding, `mode = "output"` picks the mode (default `region`). Without grim (or slurp for a
region), `paw msg screenshot` fails with a message and a key binding logs one.

Screenshots and screen sharing use wlr-screencopy, export-dmabuf, and
ext-image-copy-capture, so `grim` works directly and Discord, OBS, or a browser share a
monitor or a single window through xdg-desktop-portal-wlr. A shared window is drawn on
its own, without whatever overlaps it, and keeps streaming while minimized or on another
workspace; the taskbar's pictures of windows are taken the same way. While the screen is
locked no capture of a window can start: a program asking is told its capture stopped.

## Application compatibility

Browsers and Electron applications (Firefox, Chromium, Discord) get the protocols they
look for: GPU buffers through linux-dmabuf with explicit sync where the driver supports it,
viewporter, fractional scaling, presentation timing, xdg-output, middle-click paste
(primary selection), clipboard managers (`wl-clipboard`, data-control), drag-and-drop,
pointer lock and relative motion for games, tearing-control for games that want their frames
shown at once (see [Tearing](#tearing)), and xdg-foreign for portal dialogs.
xdg-activation lets an application raise itself, so a link clicked in a chat brings the
browser forward; paw honours every valid token and does not prevent focus stealing.
Popup menus are kept on the output of their window.

Firefox and GTK applications draw their own minimize, maximize, and close buttons, laid
out by GTK's `button-layout` setting, which desktops without title bar buttons (HyDE on
Hyprland, for one) leave empty. paw gives the applications it starts a dconf profile
(`DCONF_PROFILE`) that locks that one setting to `windows.buttons`, by default
`"appmenu:minimize,maximize,close"`; `""` keeps the desktop's value. Every other GTK
setting still comes from, and is saved to, your own dconf database, so other sessions
see no change. This needs `dconf` at startup.

### Portals

Portals run as D-Bus services, started with the bus's environment rather than the
compositor's. A standalone `--session` therefore exports `WAYLAND_DISPLAY`, `DISPLAY`,
`XDG_CURRENT_DESKTOP=paw`, `XDG_SESSION_TYPE`, and `PAW_SOCKET` with
`dbus-update-activation-environment --systemd` before it starts anything; the nested mode
leaves the host's portals alone, so its applications share and pick files through the host.
The installed `paw-portals.conf` selects `xdg-desktop-portal-wlr` for screen sharing and
screenshots and `xdg-desktop-portal-gtk` for everything else. Install both, plus PipeWire
and `slurp` (the wlr portal's monitor picker on multi-monitor setups). An
`xdg-desktop-portal` that is already running keeps the desktop it started with; after
leaving another desktop, run `systemctl --user restart xdg-desktop-portal` once or log out
fully. The session also sets `MOZ_ENABLE_WAYLAND=1`, `ELECTRON_OZONE_PLATFORM_HINT=auto`,
and `_JAVA_AWT_WM_NONREPARENTING=1` unless they are already set.

### XWayland

X11 applications run through XWayland when wlroots is built with X support and
`Xwayland` is installed. `DISPLAY` is set from the start, but Xwayland only
starts when the first X11 client connects and exits again once idle (Lua
`xwayland = false` disables it; changing it needs a restart). X11 windows take
part in focus, the taskbar, snapping, maximize, and fullscreen like Wayland windows.

wlroots' X11 window manager can leave events unprocessed, which loses the
first window after Xwayland starts. Until wlroots fixes this, paw nudges the
window manager every 250 ms while Xwayland runs. With wlroots patched by
`packaging/patches/wlroots-xwm-drain.patch`, configure with
`-DPAW_XWM_WAKER=OFF` to drop the workaround.
