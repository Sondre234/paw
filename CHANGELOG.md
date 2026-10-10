# Changelog

Notable changes, newest first. Dates are when the work landed. 0.1.0 is the first tagged
release; the dated sections below it record the work that led up to it, and all of it is in
0.1.0.

## Unreleased

The shell, redesigned after Windows 11: a start menu, a clock flyout, Quick Settings, restyled
menus and overlays, and motion throughout. And a second style after macOS, where paw now
starts: a menu bar, a dock, Launchpad and Spotlight, traffic-light window controls and window
shadows.

### The macOS style

- The shipped configuration starts in the macOS style, with the profiles `macos-light` (the
  first) and `macos-dark`; `default` and `light` are the taskbar as before. A file with
  `extends = "default"` and no `profiles` of its own starts there too unless it sets `profile`,
  and one with profiles of its own keeps its look.
- `shell.style = "macos"` lays the shell out as macOS does: a menu bar along the top with a
  system menu, the focused application's name and menu, a Window menu, the widgets, search,
  Control Center and the date; and a centred floating dock with the Launchpad button, pinned and
  running applications, dots under the running ones, name tags, a bounce while one starts and
  when one asks for attention, per-application menus (Keep in Dock, Hide, Quit) and a Trash.
  Switching profiles re-lays the shell out at once.
- In that style the launcher is Launchpad, a paged full-screen grid of every application with a
  search; the command palette is Spotlight; Quick Settings is Control Center; the clock's flyout
  is Notification Center; and menus, notification banners, buttons, sliders and the window
  switcher look like macOS's, in a light and a dark appearance. With no wallpaper set it draws
  one of its own.
- The shipped configuration no longer names one machine's monitors in `outputs.order` and
  `outputs.primary`; they are examples now.

### The look

- The shell's colours, type, corners, sizes and timings come from one set of design tokens
  derived from the appearance profile. Hovering, pressing and what is open or on now show on a
  light panel too, popups are opaque even when the bar is translucent, and the shell's motion
  follows `animations.enabled` and `animations.speed`.
- The shell draws through the GPU by default, for shadows and smooth motion at high refresh
  rates; `shell.renderer = "software"` keeps the CPU renderer for a weak machine. See
  [docs/performance.md](docs/performance.md). It needs Qt 6.9 or newer: its shadows are Qt Quick
  Effects' `RectangularShadow`, new in 6.9.
- Buttons, search fields, the crosses that close or clear things, and what an empty list says look
  the same everywhere, and a ring in the accent colour shows the button the keyboard is at.
- Many more windows get their application's icon: the shell matches a window's app id to a
  desktop entry in many more ways (versions as in `gimp-2.10`, `-bin` and `-desktop` suffixes,
  the program an entry runs, reverse-DNS names either way, Steam games, NixOS wrappers,
  AppImages, entries the menus leave out), and guesses an icon name when none matches. The
  taskbar's window buttons and the dock's running applications now show the matched
  application's icon rather than the one their app id names.
- Icons follow the desktop's icon theme (GNOME's setting, GTK's `settings.ini` or KDE's
  `kdeglobals`) when no Qt platform theme names one, rather than hicolor alone, and fall back to
  Breeze, Adwaita or Papirus, so that generic icons are found.

### The taskbar

- The taskbar's popups open in a surface of their own over the whole monitor instead of growing
  the bar's, so the bar never resizes and a popup can be as tall as the monitor allows. They show
  over fullscreen windows (the start menu Super + R opens shows over a video), fade in with a
  short slide from the bar, cast a shadow when drawn through the GPU, and close when the overview,
  the window switcher or the command palette opens. A press on the bar while one is open still
  opens another in one press, and one beside it closes it without reaching the window under it.
- The bar moves: button fills fade in and out, application icons shrink a little while pressed, a
  window's button fades and grows in as it opens and out as it closes while the others slide over,
  an application just pinned grows into its slot, the line under a button eases to its new length
  and colour, the current workspace's pill slides between workspaces, tooltips fade in, the
  clock's, the bell's and a stack's counts grow in and pop as they go up, and icons that follow a
  state crossfade. Nothing moves, or wakes the shell, while nothing changes.
- Resting the pointer on a window's taskbar button, or a stack's, shows a small picture of each of
  its windows on a card above it, as Windows 11 does, under the window's icon and title: the
  pictures follow the windows while shown, a click focuses one, a middle click or its cross closes
  it and a right click opens its menu. They take the place of the button's tooltip and of a
  stack's list, which still shows when the pictures would not fit across the monitor.
  `shell.thumbnails` sets the delay, the pictures' size and whether they follow the windows, and
  `enabled = false` turns them off. The macOS style's dock lists a stack's windows as before.
- The compositor scales a window's picture down to the card's size itself, on the GPU, and
  smoothly, so that text in it does not shimmer: the shell copies a few hundred kilobytes a frame
  instead of the whole window and has nothing left to scale, and a live picture follows its
  window 30 times a second instead of ten. `paw-window-control-v1` version 3 gives such a
  source with `get_scaled_capture_source`, and `paw msg get pictures` lists them.
- A window whose process plays sound has a speaker beside its title on its picture, as a
  browser's tab has: a click mutes the window's sound, and a crossed-out speaker stays to unmute
  it. Sound from a process the window's process started counts as the window's (a browser's
  audio process, a player run in a terminal) unless that process has a window of its own, and
  all windows of one process show it. The compositor names each window's process to the shell
  through `paw-window-control-v1` version 3.
- The card of window pictures opens on the pictures rather than on icons standing in for them:
  they are taken from halfway into the delay, and none for a button the pointer only crosses. It
  glides from one button to the next and eases to the width of their windows, as on Windows 11,
  and a picture that comes while it is open fades in over the icon standing in for it.
- Resting the pointer on a window's picture for half a second peeks at the window, as Windows'
  Aero Peek does: the other windows fade as for `peek`, and the window shows over them where it
  is, minimized or on another workspace too, without being focused, raised or restored. The peek
  moves at once to the next picture and ends with a fade as the pointer leaves the pictures, and
  clicking the picture focuses the window where it shows. `paw-window-control-v1` version 4
  peeks with `set_peek` and `unset_peek`, and `paw msg get window_peek` shows what it does.
- Something dragged from an application (a file, text, a link) brings windows forward as on
  Windows: resting it half a second on a window's taskbar button brings that window to the front,
  restored and its workspace shown, to carry the drag on and drop it there; on a stack's button
  it opens the card of their pictures, or their list, where resting on one brings it forward. A
  drag only crossing the bar does nothing, and the bar takes no drop, so the application sees it
  cancelled. The macOS style's dock does the same.
- The taskbar works from the keyboard, as Windows' Win + T: Super + B (`taskbar_focus`) gives
  the bar on the focused monitor the keyboard at the focused window's button, ringed in the accent
  colour. Left and Right move along the buttons, each showing its windows at once, the card gliding
  along; Up goes into the card or a stack's list, where the picture selected peeks at its window
  after a moment and Delete closes it; Enter does what a click does; the Menu key or Shift + F10
  opens a menu; Escape gives the keyboard back to the window that had it. The pointer moving over
  the bar, or a press, hands the bar back to it. The macOS style's dock works the same way, as
  Control + F3 does on macOS.
- Alt + Tab shows each window as a card with its picture, as Windows 11 does: the application's
  icon and the window's title over the picture, the pictures at one height and each as wide as
  its window's proportions, in rows that wrap and shrink when there are many windows. The pictures
  follow the windows while the switcher is open, minimized ones and those on other workspaces
  too, and are asked for as it opens, so that they are there by the time it shows; the icon
  stands in until one has come. They follow `shell.thumbnails` (without it the switcher shows its
  grid of icons), and the macOS style keeps its icons. The compositor gives every window a number
  of its own, which `paw-window-control-v1` version 4's `id` event sends and each
  `switcher-window` line on the control socket now ends with, so that the shell finds a listed
  window's picture also when two windows share a title.
- A window's taskbar menu is headed by its application's icon and name over the window's title,
  and offers the application's desktop actions and a new window, then minimize or restore,
  maximize or restore, fullscreen, moving the window to another workspace or monitor, keeping it
  on all workspaces, floating it where its workspace tiles, pinning, and closing it, with icons
  and labels that follow the window's state. A stacked button's menu minimizes, moves and closes
  all its windows, and a pinned application's offers its actions too. The shell names the window
  to the compositor through `paw-window-control-v1`, a protocol of paw's own, so the
  menu acts on the window right-clicked even when several share a title, and without focusing it.
- A window's menu ends with Kill process (Kill 2 processes on a stack of windows from more than
  one), which ends the process that made it with SIGKILL, for an application that does not
  close; the dock's has macOS's Force Quit.
- Menus share one look and the keyboard: Up, Down, Home and End move, Enter or Space chooses, and
  Escape closes. Submenus open beside their entries instead of in their place (the bar menu's
  appearance profiles, a tray item's submenus, whose application hears of each level as it opens
  and closes), and stay open while the pointer heads for them across other entries. Escape in the
  power menu closes only it, whose entries now have icons. The desktop's right-click menu is one
  of them too, with Show desktop and the appearance profiles; its Refresh is gone, as the
  applications are read again by themselves.

### The start menu

- The application menu is now a start menu after Windows 11's: a search field on top; pages of
  pinned tiles, which drag into place, and the applications launched lately with how long ago;
  All apps from A to Z with a letter jump; and along the bottom the user's name and picture
  beside the power button. Its search finds applications by name, generic name, keywords and
  comment, and open windows, workspaces and actions as the command palette does, grouped under a
  best match with the application's desktop actions, which Right and Tab reach from the keyboard.
  The keyboard moves through all of it, and each application has a menu to open it, run its
  desktop actions and pin it to the start menu or the taskbar. The start menu keeps pins of its
  own (`$XDG_STATE_HOME/paw/start-pinned`, seeded from the taskbar's) and counts launches
  (`$XDG_STATE_HOME/paw/launches`). Applications installed or removed show at once: the
  Refresh button is gone.
- The shell can list an installed application's desktop actions ("New window", "New private
  window") and run one, a failure shown across the panel as for the application itself.

### Search

- The command palette (Spotlight in the macOS style) and the start menu's search calculate: an
  expression such as `2*(3+4)`, `2^10`, `sqrt(2)/3`, `15 % 4` or `1e3 + 0x1f`, with `pi`, `e` and
  the usual functions, shows its value as the first result (the start menu's best match), and
  Enter copies it to the clipboard. `5 km in mi`, `100 f in c` and `3 GiB in MB` convert between
  units, and `255 in hex` writes a whole number in another base. A leading `=` narrows the
  palette to the calculator.
- They find files and folders by name too: those used lately (`recently-used.xbel`) and those in
  the home folder and the XDG user folders, read in the background the first time a search asks
  and again when they are out of date, leaving out hidden, version-controlled and build trees.
  Enter opens a file in its default application and Ctrl + Enter (or a button) its folder; a
  leading `/` searches only files. `shell.search` turns it off or names other folders, and sets
  how deep and how many names are read.
- The last result is Search the web for “…”, which opens the default browser at DuckDuckGo with
  the words; `shell.search.web` names another engine's address, or `false` none. In the start
  menu it is the best match when nothing else is found.
- Spotlight lists its results by kind under their headings after the top hit, rather than a
  heading for every run of one kind.

### The clipboard history

- `clipboard_history` (Super + Shift + V) opens a history of what was copied in any application,
  as Windows' Win + V does: text and pictures, newest first, with a search. Enter copies an entry
  again, and entries can be pinned, forgotten one by one, or cleared but for the pinned. The
  shell keeps it as a client of the compositor's `ext-data-control-v1`, leaving out what a
  password manager marks as secret and anything copied while the session is locked, in memory
  only unless `shell.clipboard.persist` is set; `shell.clipboard` also sets how many entries it
  keeps, whether pictures too, and turns it off.
- The control socket's subscribers hear `locked on|off` as the session locks and unlocks.

### The emoji picker

- `emoji_picker` (Super + semicolon) opens an emoji picker, as Windows' Win + . does: a search by
  name and keyword, a tab for the emoji picked lately and one per group, and a skin tone. The emoji
  picked is typed into the window that had the keyboard, or copied when nothing has it. Its emoji
  are Unicode's Emoji 16.0 with CLDR's English keywords, vendored in `vendor/emoji`.
- `paw msg type TEXT` types text into what has the keyboard, emoji and accents too, on a
  keymap made for it, as wtype does.

### The clock and Quick Settings

- The clock opens one flyout at the bar's right end, as on Windows 11: the notifications above
  and a calendar below. The notifications are grouped by application, with pictures, progress,
  action buttons and a cross on hover, and a do-not-disturb switch and Clear all over them; a list
  longer than its card fades out at the edge where it goes on, and on a monitor too short for both
  cards the calendar leaves out its time and date for them. The calendar shows the time and
  today's date, pages by month with arrows or the wheel (sliding each month in), zooms out from its
  title to the months and the years, and has a Today button. `notification_history` (Super + N)
  opens the flyout. The clock shows the unread count, muted with do-not-disturb, which a
  right-click on it toggles; the bell is off unless `shell.widgets.notifications = "bar"`.
- Quick Settings, as on Windows 11: a button left of the clock with the volume and battery icons,
  and the network's only while its link is down (its wheel changes the volume, a middle click
  mutes), opens tiles for do-not-disturb, night light, tiling on the monitor, the appearance
  profiles and the wallpaper picker, the network's state on a disc of its own (it is only shown),
  a brightness slider where there is a backlight (through logind), the volume with its outputs and
  the applications' volumes, and the battery. The network, battery, volume, profiles and do-not-disturb are in it
  by default and the tiling button and the wallpaper picker on the bar; `shell.widgets.NAME = "bar"` or `"quick"`
  places each, `true` leaving it where it goes by default, so configurations with `true` and
  `false` keep working. `shell.widgets.notifications = "bar"` is the bell.
- Media controls, as on Windows 11 and KDE: Quick Settings (Control Center's Now Playing) shows
  what the media player playing most lately plays, through MPRIS: its cover, title and artist,
  the player, the position (which a press or a drag seeks), and previous, play or pause and next;
  arrows step to other players. The media keys (XF86AudioPlay, Pause, Next, Prev and Stop) are
  bound to the new actions `media_play_pause`, `media_next`, `media_previous` and `media_stop`,
  which the shell sends to the same player. `shell.widgets.media = false` leaves the card out and
  `-DPAW_MEDIA=OFF` builds without them.
- A power mode tile in Quick Settings while power-profiles-daemon runs: Power saver, Balanced or
  Performance, listed under the tile, saying when the daemon holds performance back.
  `shell.widgets.power_mode = false` leaves it out and `-DPAW_POWER_PROFILES=OFF` builds
  without it.
- Wi-Fi through NetworkManager, as on Windows 11 and KDE: where it has a Wi-Fi device, Quick
  Settings' Wi-Fi tile turns the radio on and off and its chevron lists the networks in range (the
  connected one first, with their signal and a lock when secured); a click connects to a known or
  open one, asks for the password of one secured with WPA, WPA2 or WPA3 Personal (asking again,
  and forgetting the connection added for it, when refused), or disconnects. The network widget
  names the network and its signal, and on the bar opens the same list under a switch. Without
  NetworkManager nothing changes; `-DPAW_NETWORKMANAGER=OFF` builds without it.
- Bluetooth through BlueZ: while it has an adapter, Quick Settings' Bluetooth tile turns it on and
  off and its chevron lists the paired devices (with their battery where BlueZ reports it) to
  connect, disconnect and forget, and, after Pair a new device, those in range to pair with, which
  are then trusted and connected. The shell is BlueZ's agent from the first time it is asked to
  pair, for passkeys to confirm, PINs and passkeys to type and codes to type on a keyboard.
  `shell.widgets.bluetooth = false` leaves it out and `-DPAW_BLUETOOTH=OFF` builds without it.

### Notifications and the overlays

- The on-screen display, the notification cards, the window switcher, the command palette and the
  power dialog take the popups' look: opaque cards with a shadow through the GPU, the theme's type
  and sizes, and each comes in on the theme's motion (at once with animations off). The switcher,
  the palette, the power dialog and the overview's text fade out as they close too, giving the
  pointer and the keyboard back at once. The overview's search box, labels, title bars and hint
  follow, legible over its backdrop in a light theme too.
- The on-screen display draws line icons (a sun for brightness, a crossed-out bell for
  do-not-disturb), glides its level to each new value and keeps the number in figures of one
  width.
- A notification card is headed by its application's icon and name and how long ago it came, sets
  its summary over its body beside its picture, shows its actions as buttons and its close button
  while the pointer is on it, runs a line down along its bottom edge as its timer runs, and is
  edged in red when critical. The pointer over a card's buttons now holds its timer too.
- The window switcher shows larger icons over two lines of title, glides its selection, fades and
  marks minimized windows, tints those asking for attention, and names the selected window's
  workspace in its caption. The command palette's results look like a menu's rows with their kind
  on a pill, and it says how to narrow a search that finds nothing. The power dialog's keyboard
  starts on its action's button, ringed, and Tab moves it to Cancel.
- The notification history marks what it shows as read also when it is opened in the first
  moments after the shell starts, and the network widget dims while the link is down rather
  than while it is pressed.

### The compositor

- Dragging a floating window to an edge of its monitor snaps it as it is dropped, as Windows'
  Aero Snap and KWin's quick tiling do: at the left or right edge into that half, in a corner
  into that quarter, and at the top edge maximized as before. A translucent preview of the slot,
  rounded as windows are, eases in under the window while a drop would snap it and fades as the
  pointer leaves the edge. Edges shared with another monitor do not snap, a tile lifted out of a
  tiling monitor still goes back into its tiling where it is dropped, and `restore` puts a window
  snapped by dragging back where it was before the drag. `windows.snap` turns it off and sets the
  distance, the corners, the preview and its colour; `paw msg get snap` tells the zone and
  the preview.
- Snapping from the keyboard: `snap_top_left`, `snap_top_right`, `snap_bottom_left` and
  `snap_bottom_right` put the focused window into a quarter, and Super + Alt + arrows
  (`snap_cycle_left`, `snap_cycle_right`, `snap_cycle_up`, `snap_cycle_down`) step it as
  Windows' Win + arrows do: left and right to that half and on to the next monitor, up to
  maximized, down back to its own size and then minimized, and between halves and quarters.
  The scroll layout's suggested `scroll_left` and `scroll_right` keys in the example
  configuration move to Super + Alt + H and L.
- Snap Assist: after a window snaps into a half or a quarter, by dragging or from the keyboard,
  the free part of the monitor beside it shows the monitor's other windows as live thumbnails
  with their titles, as Windows does; Return or a click puts one there, and Snap Assist comes
  back for the next free part, so four windows fill the quarters in turn. Escape, a click
  elsewhere or any other binding dismisses it, and `windows.snap.assist = false` turns it off.
  It is the overview in that part alone, so the overview's overlay in the shell now takes no
  input at all.
- The keyboard's volume, microphone and brightness keys work without setup: the shipped
  configuration binds them to the new actions `volume_up`, `volume_down`, `volume_mute`,
  `mic_mute`, `brightness_up` and `brightness_down` (the steps take `amount` in percent, 5 unless
  given). The shell carries them out on the default sound output and input and the backlight and
  shows the on-screen display, at the ends of the range too, with a microphone for the
  microphone's mute; without a shell the compositor runs `wpctl` and `brightnessctl`. The shell
  subscribes with `subscribe shell` to say it is there.
- A key binding with `locked = true` runs while the session is locked too, as Hyprland's `bindl`,
  and one with `repeats = true` runs again while its key is held, as Hyprland's `binde` (keyboard
  resizing did alone before, and still does unless `repeats = false`). The shipped volume,
  microphone and brightness keys work on the lock screen, and the steps repeat while held.
- Binding modes, as sway's modes and Hyprland's submaps: `modes` holds named lists of key bindings,
  and a binding with `action = "mode", mode = "NAME"` (or `paw msg mode NAME`) puts one in
  use, its bindings taking the place of the others until `mode = "default"`. The taskbar and the
  macOS menu bar show the mode in use on a pill, which a click leaves; `paw msg get mode`
  prints it, and subscribers hear `mode NAME`. A reload and locking the session leave it. The
  shipped configuration has a resize mode, commented out. An unknown mode, or one with no binding
  that leaves it, is refused with its line.
- Virtual machines, remote desktop clients and games can have the keys the bindings take, as they
  ask through keyboard-shortcuts-inhibit, so that Super, Alt + Tab and the rest reach the system
  they show while their window has the keyboard; an X11 window's keyboard grab does the same
  through Xwayland. Super + Shift + Escape (`toggle_shortcuts_inhibit`) still runs: it takes the
  keys back from the window and gives them to it again. The first time a window takes them a
  notification names the keys that take them back. `keyboard.shortcuts_inhibit = false` refuses
  every application and a window rule's `shortcuts_inhibit = false` one; `paw msg get
  shortcuts` tells who holds the keys. The example `display_off` binding moves to Super + Alt +
  Escape.
- Input methods: fcitx5 types Chinese, Japanese, Korean and compose-heavy layouts into GTK and Qt
  applications on Wayland, as paw now offers text-input-v3 and input-method-v2 and relays
  between them: the preedit and committed text, the input method's keyboard grab (bindings keep
  their keys) and its candidate window beside the text cursor, kept on the monitor. `paw msg
  get input_method` tells what it does.
- `windows.controls = "traffic_lights"` draws the controls of the windows paw decorates as
  red, yellow and green circles at their top-left, as macOS does: grey while the window has no
  focus, their symbols shown while the pointer is on one, darker while pressed.
- `windows.round = "always"` rounds floating windows too, on monitors that do not tile, except
  one that draws a shadow of its own (a GTK frame), which keeps its own corners.
- `windows.shadow = { enabled = true }` draws soft shadows under windows, darker under the
  focused one, following their corners and leaving out fullscreen and maximized windows; they
  take no input and are no part of a window's size. `paw msg get frames` tells each
  window's controls, corners and shadow.
- Windows no longer show thin seams between their parts while they scale open or closed.
- Rounded corners are as smooth on the right of a wide monitor as on its left. On NVIDIA,
  wlroots' shaders run at 16-bit precision, which stair-stepped corners and borders more than
  1024 pixels from the monitor's left or top edge; the rounded-corners patch
  (`packaging/patches/wlroots-rounded-corners.patch`) now measures from the window's edges
  instead. Copy it to `/etc/portage/patches/gui-libs/wlroots/` again and re-emerge wlroots.
- A tiled window alone on its workspace has no gaps around it and fills the space the panels
  leave, inside its border; a second tile brings the gaps back for both. `layout.smart_gaps =
  false` keeps them around a lone tile as before.
- `toggle_above` (Super + Ctrl + T) keeps the focused window above the others, as KDE's Keep
  Above Others, Hyprland's pinned windows and PowerToys' Always on Top do: over the floating
  windows and tiles of its monitor through focus changes, workspace switches, snapping, groups and
  sticky, and under fullscreen windows, the panels and the overlays. A window's menu on the
  taskbar has Keep above others and the macOS style's Window menu Keep Above Others, checked while
  it is; the window rule `above = true` keeps a window above as it opens. `paw msg get
  windows` says which are in a sixteenth column, and `get stacking` lists the windows front to
  back. paw-window-control-v1 is at version 6 for it.
- `windows.border_color` and `border_inactive_color` take a gradient of 2 to 10 colours at an
  angle, as Hyprland's borders do: `{ "#33ccffee", "#00ff99ee", angle = 45 }`. Borders then ease
  from one gradient to the other with focus and round with their windows, drawn from a strip per
  side and a square per corner rather than a buffer as large as the window. `paw msg get
  frames` says how each border is drawn in a last column, and `paw import` carries a
  Hyprland border's gradient over with its angle, where it took the first colour before.
- Window rules with `dynamic = true` follow a window's title and app ID as they change, as
  Hyprland's dynamic rules do: while they match, they hold the window to their `floating`,
  `sticky` and `above`, and once they no longer do the window gets back what it had, keeping
  what was changed by hand meanwhile. Their other actions still act once, as the window opens.
  `paw msg get dynamic_rules` tells what they hold each window to.
- `layout.floating_above_tiles = true` keeps floating windows over the tiles, as sway and Hyprland
  do. It is off by default: as before, the window brought forward last is in front, tile or not.
- A fullscreen window stays in front of its monitor while another monitor has the focus, as
  fullscreen a program asks for did already, until a window of its own monitor is brought
  forward.
- The compositor tells the shell whether night light is on and whether the schedule decides
  (`night-light ACTIVE MODE` on the control socket's state stream).
- `paw msg get layers` says which layer surface holds the keyboard, in a fifth column.
- `paw msg get seat` says what has the keyboard, what the pointer is on, and what a drag is
  over.

### Starting and ending a session

- A standalone session starts the XDG autostart entries after `startup`, so applications' "start
  at login" settings work: `~/.config/autostart` over `/etc/xdg/autostart` by file name, skipping
  `Hidden`, `OnlyShowIn`/`NotShownIn` that leave paw out, a missing `TryExec`, entries
  GNOME's setting turns off, another notification daemon or tray watcher while the shell's
  is on, and a sound server (PipeWire, PulseAudio) while one already runs. `autostart = { xdg = false }` turns it off, `autostart.exclude` leaves entries out by
  file name, and `paw msg get autostart` lists what was started or skipped, and why.
- The shell is the session's polkit authentication agent: `pkexec`, GParted, updaters and
  logind's `challenge` ask for a password in a dialog over the focused monitor, in either style,
  with a choice of user when several may answer, and another try after a wrong password. It
  stays out of the way of an agent already serving the session, and only a standalone session's
  shell is one. `shell.polkit_agent = false` turns it off; `-DPAW_POLKIT=OFF` builds without
  it, as does a system without libpolkit-agent-1.
- A standalone session saves itself as `last` as it ends (log out, quit, power off, restart),
  before its windows are asked to close, and the next one restores it after `startup` and
  autostart: `session.restore = "windows"` (the default) puts the windows that open again where
  they were, `"launch"` also starts the programs of the others again without doubling what
  startup and autostart started, and `"off"` keeps nothing.

### Touchpads, touchscreens and tablets

- Touchpad swipes, pinches and holds reach the window under the pointer
  (pointer-gestures-unstable-v1): browsers zoom on a pinch, and GTK applications get their
  gestures.
- Three fingers swiped sideways move to the next or previous workspace, the windows following the
  fingers through the workspace slide and going on or back as they lift (on past half way, or
  with a flick); three fingers up and down open and close the overview the same way. `gestures`
  sets the swipes the compositor takes (fingers and direction to a request, as hot corners take),
  the distance of a step and inversion; every other gesture still reaches the window.
  `paw msg get gesture` says what the swipe under way does.
- Touchscreens: each finger goes to the window, panel or popup under it through wl_touch, several
  at once, and focuses it as a click would. An application without touch support, the window
  controls, a window's drag strip (which a finger drags the window by), the overview and the
  desktop get the first finger as the pointer and its left button. `touch.output` names the
  monitor a touchscreen covers; unset, it is the one the device names, else the built-in panel,
  else every monitor. `paw msg get touch` lists the touchscreens and the fingers down.
- Drawing tablets (tablet-v2): a pen's or eraser's position, pressure, distance, tilt, rotation,
  slider, wheel, tip and buttons reach the window under it, for Krita's, GIMP's and Inkscape's
  pressure; its tip focuses the window and keeps it while down. Over a window without tablet
  input, the window controls or the desktop the pen is the pointer. Pads' buttons, rings and
  strips go to the window with the keyboard. `tablet.output` maps a tablet to one monitor, and
  `paw msg get tablet` lists tablets, pads and tools.

### Monitors off, idle and the lid

- A monitor can be turned off and on again while it stays in the layout with its windows,
  workspaces and panels, where `enabled = false` takes it out: through
  wlr-output-power-management, so `wlopm` works and swayidle can blank the monitors with it, and
  with the `display_off`, `display_on` and `display_toggle` actions, for every monitor or the one
  named. Once every monitor is off, a key press or the pointer turns them on again. `paw msg
  get outputs` ends each line with the monitor's power, `on` or `off`.
- Power saving when idle, without an idle daemon: the `idle` table sets the seconds without
  input after which the screens dim (fading to half their brightness, until the next input), the
  monitors turn off (the next key or the pointer turns them on), the screen locks and the machine
  suspends, with `idle.battery` for shorter ones on battery. By default the screens dim at nine
  and a half minutes and the monitors go off at ten; nothing locks or suspends unless set. Idle
  inhibitors (videos playing) hold every step off, swayidle and the like work as before through
  `ext-idle-notify-v1`, and `paw msg get idle` tells where the steps are.
- The clamshell mode KDE and Windows have: closing a laptop's lid while another monitor is on
  turns the built-in panel off, its windows moving to the other monitor as when it is unplugged,
  and opening it brings the panel and its windows back; a lid closed at startup holds the panel
  off from the start, as logind's `LidClosed` tells. `outputs.lid = "ignore"` leaves the panel on. Bindings act on the lid and on
  tablet mode: `{ switch = "lid", state = "close", action = "lock" }`. `paw msg get
  switches` tells what the switches say.
- Mirroring: `outputs.monitors[NAME].mirror = "eDP-1"` makes a monitor show another's picture, as
  Windows' Duplicate does, scaled to fit with black bars where the shapes differ, the pointer
  included. It leaves the layout while the monitor it mirrors is there, and joins it otherwise.
  `paw import` carries Hyprland's `mirror` over, and `paw msg get outputs` names the
  monitor a mirror shows in a last column.
- The `display_mode` action sets the monitors up as Windows' Win + P does: `extend`, `duplicate`,
  `internal` (the built-in panel, or the primary monitor, alone) or `external`, until the
  configuration is reloaded. A laptop's display key (XF86Display) runs it without a choice, which
  opens a popup that steps through the four and takes the one shown once the key rests.
- `outputs.monitors[NAME].bit_depth = 10` draws a monitor in 10 bits per channel where the monitor
  and the renderer take it, staying at 8 where they do not; `paw import` carries Hyprland's
  `bitdepth` over, and `paw msg get outputs` ends each line with the depth.
- HDR: `outputs.monitors[NAME].hdr = true` drives a monitor in BT.2020 with PQ, in 10 bits, where
  its EDID offers them, the renderer converts colours (Vulkan alone in wlroots 0.20) and the test of
  its settings passes, and leaves it SDR with the reason in the log otherwise. Applications may
  then describe their colours through color-management-v1, offered only then. Off by default;
  `paw import` carries Hyprland's `cm, hdr` over.
- Tearing for games: with `windows.allow_tearing = true`, a fullscreen window that asks for it
  through tearing-control-v1, or that a rule's `allow_tearing` names (as an X11 game cannot ask),
  has its frames shown at once rather than at the next refresh while nothing else is drawn on its
  monitor, falling back to normal page flips where the GPU refuses. Off by default; `paw
  import` carries Hyprland's `allow_tearing` and `immediate` rules over, and `paw msg get
  tearing` tells what each monitor's frames do.
- A display settings window, as Windows' and KDE's, opened from Quick Settings (or Control
  Center), the macOS style's system menu, the command palette or the `display_settings` action:
  the monitors drawn to scale, dragged into place where they snap beside one another, and the
  chosen one's resolution, refresh rate, scale, rotation, adaptive sync, mirroring, 10-bit colour,
  HDR (where it can be had, else why not) and whether it is the main one. Apply tests every
  monitor's settings at once and puts them on trial: "Keep these display settings?" counts down
  15 seconds and they go back by themselves unless kept, at once where a monitor does not take
  them. Kept settings go to `$XDG_STATE_HOME/paw/outputs`, laid over `outputs.monitors` at
  login and on each reload for the monitor they were kept for; Reset to configuration goes back
  to the configuration's, and no Lua file is ever edited. Scripts do the same with `paw msg
  monitors apply DP-3 scale=1.5 HDMI-A-1 mirror=DP-3`, `monitors keep` and `monitors reset`, and
  `paw msg get monitors` lists each monitor's settings, where they come from, its modes and
  why it cannot have HDR.

### Working on the shell

- `paw-shell --preview-popup NAME` shows one of the taskbar's popups or overlays on stand-in
  data, and `tools/shell_gallery.py` saves a picture of every one, in a light and a dark theme,
  drawn in software and through the GPU: the overlays as `osd-volume`, `osd-text`, `cards`,
  `power-dialog`, `palette`, `palette-empty`, `switcher` (with pictures of the windows in the
  taskbar style, and its grid of icons as `switcher-icons`) and `overview`, and a search finding
  nothing as `launcher-empty`.

### Fixes

- A keymap file (`keyboard.file`) saved in place while the compositor read it after a reload
  could end the session with SIGBUS; it is now read into memory before it is compiled.
- A monitor whose settings turn it off (`enabled = false`) goes off once another monitor
  appears, where it stayed on when it was the first to appear, as at startup.
- A program capturing one window (ext-foreign-toplevel-image-capture-source-v1, as a portal's
  window sharing does) is no longer disconnected for asking while the session is locked: it gets
  a source whose capture stops at once, as for a window that is gone.
- A window brought forward while something is dragged (from the taskbar, or by an application
  asking for it) has the keyboard once the drag ends, instead of the window the drag came from,
  and a click right after a drop, without moving, reaches the window under the pointer. A drag
  over the top edge of a window paw decorates goes to the window rather than nowhere.
- A switch's binding to the mode action (`{ switch = "lid", state = "close", action = "mode",
  mode = "resize" }`) enters the mode it names, where it entered the default one.
- A configuration error in a table inside another points at its own line, where it pointed at
  the first setting of that name anywhere in the file (`shell.thumbnails.delay` at
  `hot_corners.delay`). Those in `windows.swallow`, `windows.magnet`, `windows.snap`,
  `windows.shadow` and `shell.panel_margin` name the table too: `windows.magnet.distance must be
  between 0 and 200`, not `windows.distance`.

## 0.1.1 (2026-10-05)

Fixes to the tests, CI and the ebuild; the compositor and the shell behave as in 0.1.0.

- Where grim is not installed, as in an ebuild's test phase, the pixel tests leave out their
  pixel checks as they were meant to; they were handed `GRIM-NOTFOUND` to run instead, and five
  failed.
- `tray_smoke` waits for the tray to turn off before turning it on again: without grim nothing
  held the two reloads apart. The shell now logs `paw tray: off, its names released`.
- `notifications_smoke` checks the on-screen display's pill above its label, which wider fonts
  reached.
- CI installs grim, so the pixel checks run there too, and checks out with `actions/checkout`
  v7 (Node 24).
- The release ebuild suggests every terminal Super + Q looks for, not only kitty.

## 0.1.0 (2026-10-05)

The first release, and the first version I'm handing to other people to try. Since the last dated section: a
system tray, power controls, keyboard layouts and keymap files, a terminal action that finds
an installed terminal, failures to start a program shown on the panel, the `paw-session`
wrapper for display managers, and Gentoo ebuilds.

- `paw-session` starts a standalone session the way a first run needs: under
  `dbus-run-session` when there is no D-Bus session bus (usual without systemd, and needed by
  notifications, the tray and portals), with everything the session prints in
  `~/.local/state/paw/session.log` (`$XDG_STATE_HOME`), the previous session's kept as
  `session.log.old`. The display-manager entry runs it, and is no longer called experimental.
- A `terminal` action opens the terminal set by the new `terminal` setting (`terminal = {
  "foot" }`), else `$TERMINAL`, else the first of kitty, foot, alacritty, wezterm, ghostty,
  konsole, gnome-terminal and xterm that is installed. When there is none, the panel says so.
  The default configuration binds it to Super + Q, which ran kitty whether or not it was
  installed, and the command palette offers it as Open a terminal.
- A program that a binding, a hot corner, the command palette or `paw msg spawn` cannot
  start, because it is not installed, now says so across the panel (subscribers hear
  `spawn-error MESSAGE`), not only in the log.
- A compositor that cannot create its Wayland socket, because `XDG_RUNTIME_DIR` is unset, not
  writable, or too long for a socket's path, says which and exits with status 1, instead of
  aborting on a wlroots assertion.
- A system tray: the panel shows applications' status icons (StatusNotifierItem, as KDE and Qt
  applications, Electron applications and Ayatana's indicators use) beside the bell on every
  monitor, with their tooltips, attention and overlay icons. Left-click activates an application,
  right-click opens its menu in the panel's style (submenus, check boxes, radio buttons, icons),
  and middle-click and the wheel do what the application makes of them. The shell serves the
  StatusNotifierWatcher, or follows another program's. `shell.widgets.tray = false` hides the tray
  and leaves the bus names to another program; `-DPAW_TRAY=OFF` builds without it.
- Power controls. New actions `lock`, `suspend`, `hibernate`, `reboot`, `poweroff`, `logout`
  and `power_menu`, bound to Super + Shift + L (lock) and Super + Escape (the power menu). The
  panel has a power button (`shell.widgets.power`) and the command palette the same entries,
  listing only what logind (systemd-logind or elogind) allows; restart, power off and log out
  ask first with a dialog that counts down `power.countdown` seconds. `lock` starts
  `power.lock_command` (`swaylock -f` unless set), and with `power.lock_before_sleep` the screen
  locks before any sleep, whether paw, the lid or an idle daemon asks for it. Power off,
  reboot and log out first ask every window to close and give up, with the reason on the panel,
  when one is still open after `power.close_timeout` (an application asking whether to save),
  unless `power.force` is set. `paw msg get power` shows what may run. A headless
  compositor uses only the logind on the bus `PAW_LOGIN1_BUS` names, which the tests point
  at a fake one, so they can never power off the machine running them.
- Keyboard layouts: `keyboard = { layout = "us,no", variant = ",nodeadkeys" }` gives every
  keyboard two layouts, and the `switch_layout` action moves them all to the next (`layout =
  "prev"` or a layout's number picks another; `paw msg switch_layout`). An XKB option
  such as `grp:alt_shift_toggle` switches from the keyboard it is typed on, and the other
  keyboards follow. While there are two or more, the panel shows the active one (`us`, `no`)
  beside the clock; clicking it switches (`shell.widgets.keyboard_layout = false` hides it).
  Subscribers hear `keyboard-layout N COUNT SHORT NAME`, and `paw msg get keyboard` lists
  the layouts and the keyboards.
- `keyboard.file` uses an XKB keymap file instead of the layout names, and `keyboard.rules`
  sets the XKB rules. A keymap file that cannot be read or compiled is a configuration error,
  shown with its line in the configuration and, where xkbcommon knows it, in the keymap; names
  XKB does not know are now refused with its reason. Saving an `.xkb` file beside the
  configuration reloads it, as saving a `.lua` file does.
- A reload no longer resets the keyboards: keys held stay down, Caps Lock stays on, and the
  active layout stays (found by name in a changed keymap); a reload that leaves the keymap as it
  was leaves the keyboards alone.
- When the keyboard last typed on goes away (unplugged, or a virtual one such as wtype's
  finishing), the seat takes another at once, so applications that start meanwhile get a
  keymap.
- `paw import` carries Hyprland's `kb_variant`, `kb_model`, `kb_rules` and `kb_file` over.
- Gentoo ebuilds: `packaging/gentoo` is an ebuild repository with `gui-wm/paw-9999`
  (the newest `main`) and `paw-0.1.0`, with USE flags for the shell, notifications,
  the volume control and X11 applications. [docs/gentoo.md](docs/gentoo.md) shows how to
  use it.
- An installed paw starts again from builds made with `BUILD_SHARED_LIBS=ON`, which
  Gentoo's CMake eclass sets: the configuration library is always linked in, rather than
  built as a shared library that was never installed.
- `paw --version` names the wlroots version it was built with, and the git commit when
  built from a checkout (`paw 0.1.0 (git v0.1.0-3-g1234abc)`); `paw-shell --version`
  prints its version too, even without a display.
- Without a personal configuration, paw looks for the shipped one at
  `$PAW_DEFAULT_CONFIG` before the installed path, as `extends = "default"` already did,
  so a staged or relocated install can be tried before it is in place.
- `-DPAW_PULSEAUDIO=OFF` builds the shell without the volume control even where libpulse
  is installed.
- The configuration reference and the import guide are installed with the other documentation,
  under `docs/`, where the README's links expect them.
- The README describes installing, the optional programs paw uses, starting the first
  session from a display manager or a text console, where the log goes, and how to report a
  bug; GitHub issues offer a bug report template.
- CI runs on pushes to `main` (it waited for a `master` branch), runs `shell_preview` again, and
  checks that a staged install runs.

## 2026-10-03

- `layout.tiling_per_workspace = true` makes tiling a setting of each workspace rather than
  each monitor: Super + S, the panel button and `toggle_tiling` then switch only the current
  workspace, and a window moved to another workspace tiles or floats as that one does.
- Fullscreen a program asks for itself, like a browser's fullscreen video, stays over the
  panels when it loses focus, so a video on one monitor no longer shows the taskbar over it
  while you work on another. Raising another window on the same monitor still lowers it
  below that window and the panels.
- The project is now called paw everywhere, and nothing answers to the old `shaode`
  name: the executables are `paw` and `paw-shell` (`paw msg`,
  `paw import`), the configuration lives in `~/.config/paw`, state in
  `~/.local/state/paw`, and the environment variables and CMake options start with
  `PAW_` (`PAW_SOCKET`, `PAW_DEFAULT_CONFIG`, `PAW_BUILD_SHELL`, ...).
  The session sets `XDG_CURRENT_DESKTOP=paw` and installs `paw-portals.conf` and
  `paw.desktop`. To carry a setup over, move `~/.config/shaode` and
  `~/.local/state/shaode` to the new names, update launch scripts and key bindings that run
  `shaode`, and configure a fresh build directory.

## 2026-09-28

- The configuration reloads when it is saved, as in Hyprland: the compositor watches the `.lua`
  files in its directory (`auto_reload = false` turns this off). A file with an error, on a save
  or at startup, no longer leaves the old settings running or stops the session from starting:
  the default configuration loads in its place, and a red bar across the top of every monitor
  shows the error with its file and line until a save fixes it.
- Fullscreen from a binding or the title bar no longer covers the panel: the window fills the
  monitor above it. Fullscreen a program asks for itself, like a YouTube video, still covers the
  whole monitor, panel included.
- Clicking the focused window's taskbar button minimizes it from a bar on another monitor too:
  pointing at or clicking a panel there no longer counts as the bare desktop and takes the focus
  away first, which made the click restore the window instead.
- Appearance profiles: `profiles` names sets of `appearance`, `windows` and `shell` settings laid
  over the configuration, and `profile` picks the one to start with. Switch from the panel's
  right-click menu (Appearance), the command palette, or `paw msg profile NAME|next|prev`; the
  choice is saved in `$XDG_STATE_HOME/paw/profile` and kept across restarts. The example
  configuration ships `default` and `light`. A profile button on the panel (`shell.widgets.profiles`)
  lists them to switch with one click.
- Quality sweep: `tools/check-all.sh` builds, runs the whole suite, then runs it again under
  AddressSanitizer, UndefinedBehaviorSanitizer and leak detection (`--repeat N` hunts flaky
  tests). Fixes it led to: a client's app ID could forge lines in the overview description sent to
  the shell (a newline in it ran as a request); the overview's timer was never removed at
  shutdown; the control socket refused bursts of clients (listen backlog 8); one output that
  could not be magnified (rotated) switched the magnifier off everywhere until a restart; the
  volume control's tooltip was empty; the palette's selection jumped when the saved sessions
  arrived late; the example configuration's commented scroll bindings collided with default
  keys. New tests fuzz groups, configuration reloads and the control socket with seeds, and
  check that every binding action is in the README and every documentation example is accepted.
  `qmllint` is clean over the shell's QML. The shell also shows window titles and app names as
  plain text (a title such as `<b>x</b>` was read as markup) and no longer loads an icon from a
  file path a window names as its app ID.
- Notifications: the shell serves `org.freedesktop.Notifications` and shows cards (actions,
  markup, icons and images, progress, urgency, replacement, timeouts that pause on hover) in a
  corner of the focused monitor; the panel's bell keeps a history with an unread badge, and
  do-not-disturb (`dnd_toggle`, `dnd_on`, `dnd_off`, `paw msg dnd`) silences the cards. On-screen
  display for volume, brightness and `paw msg osd TEXT [PERCENT]`. See the `notifications` and
  `osd` settings and `notification_history` (Super + N).
- `windows.placement`: new floating windows can open `"cascade"`d (the default, as before),
  `"center"`ed, or `"smart"`, in the free space where they cover the other windows least.
- Magnetic edges (`windows.magnet`): a floating window being dragged or resized sticks to the edges of the
  output, the free area and other windows within `distance` pixels, with a guide line; a
  modifier (`bypass`, Shift) turns it off for a drag.
- Performance: while windows glide or fade, a pointer hit test no longer moves every running
  animation to its resting place and back (two scene updates each); it does so only for the
  animations that could be under the pointer. Pointer motion during a burst of layout changes with
  40 windows went from about 340 us to well under 100 us per event.
- Urgent windows: with `windows.activation = "urgent"` (the default) an unfocused application that
  asks for attention through xdg-activation, `_NET_WM_STATE_DEMANDS_ATTENTION` or an X11 urgency
  hint is marked urgent instead of taking focus. Its border pulses in `windows.urgent_color`
  (also without a `border_width`), and `focus_urgent` (Super + U) jumps to the one that asked
  first. `"focus"` restores the old behaviour, `"ignore"` drops requests. `paw msg get urgent`
  and the subscription report them. The shell marks urgent windows on the taskbar (also
  stacked buttons and their hover list), the workspace indicator, the overview, the window
  switcher and the command palette.
- Performance: a pointer motion event makes one scene hit test instead of up to four (window,
  controls, tabs, resize band); with 40 windows it takes about a third of the time. The
  benchmark has a `pointer` scenario (`--pointer-probe`) and `get stats` counts motion events.
- Performance: `get frame_times` reports the last 4096 frames' time in `output_frame` and the interval
  between frames; `tools/bench/bench.py` has a `smooth` scenario (animations on, windows
  redrawing, layout changes and workspace switches) that prints their p50/p95/p99.
- The default build type is `RelWithDebInfo` when `CMAKE_BUILD_TYPE` is unset (single-config
  generators), so a plain build and distribution packages are optimised.
- `move_workspace_to_output` (Super + Ctrl + comma / period) sends a workspace with its windows
  and layout state to the monitor on the left or right, by name, `next` or `prev`;
  `swap_workspaces` trades all workspaces of two monitors. Windows glide across.
- Sessions keep the columns of the scrolling layout: `session save` records each column's width
  and each tile's column and place in its stack, and `session restore` sets them again.
- Hotplug: windows of an unplugged monitor (fullscreen ones too) move to the nearest one (tiles
  rejoin its tiling, workspace numbers kept; a monitor turned off in the configuration hands its
  floating windows over the same way, onto the workspace the other one shows) and return when it is plugged back in (`outputs.return_windows`).
  Headless sessions can plug virtual outputs with `paw msg headless_output add|remove`.
- `layout.outputs`: per-monitor `tile_layout`, `master_ratio` and `master_count` for the
  workspaces not set by hand, applied live on reload and when a monitor returns. A ratio or
  master count changed by an action no longer freezes the workspace's layout against later
  configuration changes. `paw msg get layout OUTPUT [WORKSPACE]`.
- Window swallowing: with `windows.swallow.enabled` a window started from a terminal (found
  through process ancestry) takes its tile or floating place and hides it; closing it brings
  the terminal back. `swallow_toggle` does it by hand; `windows.swallow.terminals` and
  `exceptions` choose what swallows.
- Shell performance: the shell draws with Qt's software renderer by default (`shell.renderer`,
  `"gpu"` for the old behaviour), the QML is compiled ahead of time and one engine serves every
  view, popups are made on first use, the clock ticks once a minute, and battery and network
  changes come from kernel messages instead of a 5 s poll. Measurements in
  `docs/performance.md`; `tools/shell_perf.py` reproduces them.
- Overview (Exposé), Super + O / `toggle_overview`: live thumbnails of the monitor's windows in
  a grid with a workspace strip; type to filter, arrows or mouse to pick, middle click to
  close, drag a thumbnail onto the strip to move it. See the `overview` settings and
  `overview_confirm`, `overview_cancel`.
- `peek` (hold a key) and `peek_toggle` fade every window out to show the desktop; see the
  `peek` settings.
- Performance: `tools/bench/bench.py` benchmarks the compositor headless (`docs/performance.md`);
  window opacity rules are matched only when a window's inputs change, cutting the cost of a
  commit several times over.
- Command palette (Super + P, `palette`): fuzzy search over windows, applications, workspaces,
  compositor actions and saved sessions in the shell, with `>`, `@`, `#` and `%` filters. Sticky
  windows moved from Super + P to Super + Shift + P.
- Window groups: `group_toggle`, `group_next`, `group_prev`, `ungroup` and `group_merge_*` put
  several windows in one slot under a strip of clickable tabs; windows opened while a group has
  focus join it (`features.groups`, `features.group_join_new`).
- Magnifier: `zoom_in`, `zoom_out`, `zoom_reset` and `zoom.scroll_modifier` magnify the output
  around the pointer with an eased level.
- Hot corners: `hot_corners` runs an action or command when the pointer rests in a screen
  corner.
- Night light: `night_light` settings warm the screen on a schedule (fixed times or a location)
  with the actions `night_light_toggle`, `night_light_on`, `night_light_off`, `night_light_auto`.
- Sessions: `paw msg session save|restore|list|delete NAME` keeps window placement,
  layouts and workspaces, and `restore NAME launch` starts applications that are missing.
- Named workspaces: `layout.workspace_names` labels them in the panel and lets bindings and
  `paw msg workspace NAME` reach them by name.
- Scrolling tile layout (`layout_scroll`, `tile_layout = "scroll"`), after niri and PaperWM:
  columns on an endless strip, a view that follows focus (`layout.scroll.follow`), column
  width presets, stacked windows, and the actions `scroll_left`, `scroll_right`,
  `column_widen`, `column_narrow`, `column_cycle_width`, `consume_left`, `consume_right`,
  `expel` and `center_column`.
- `focus_last` (Super + `): flip between the two most recently focused windows, across
  workspaces.
- Animations: per-kind duration and easing curve (cubic-bezier, spring, overshoot, and named
  presets), a global `speed`, glides that keep their velocity when retargeted, and late
  frames that finish animations instead of stuttering; focus changes fade a window's
  opacity and border color, switching workspace slides windows out and in, and fullscreen
  toggles glide the window (`animations.fullscreen`).
- `windows.dim_inactive` dims windows without focus with an animated fade.
- Taskbar: an application's windows share one stacked button (`group_windows = false`
  turns it off), reorderable by dragging; the default cursor theme is Adwaita.
- Alt + Tab window switcher over every window on every monitor and workspace.
- Focus follows the pointer between monitors, including empty ones.
- Monitors can be changed at runtime by wlr-output-management clients (`wlr-randr`, `kanshi`).
- `keyboard.variant` and `keyboard.model` settings.
- Wallpapers that fail to load are retried and reloaded on configuration changes.

## 2026-09-27

- Sway-style features, each behind a `features` toggle: scratchpad, sticky windows,
  workspace back-and-forth, window rule actions, and keyboard resizing of tiles and
  floating windows.
- Pin installed applications to the taskbar; icons in pinned slots; drag to reorder.
- Volume control in the panel.

## 2026-09-26

- Automatic tiling is a per-monitor setting (Super + S).
- A configuration can `extends = "default"` and hold only its changes.
- Mouse bindings, optionally restricted to matching windows or the desktop.
- Super + arrows move focus; Super + Shift + arrows move windows.
- Virtual keyboard and pointer support, so wtype and wlrctl can drive a session.
- X11 fixes: windows that map before association, fixed-size and dialog windows float.
- Windows may keep their own frame; flat window-control buttons.

## 2026-09-25

- `paw import` carries Hyprland, Waybar, wallbash, and pywal looks into `theme.lua`.
- Per-monitor mode, scale, position, rotation, disable, and adaptive sync settings.
- Per-output workspaces, shown on each panel.
- Window open/close animations and tile glide.
- Print-key screenshots (region, output, window) through grim and slurp.
- Panel placement, floating margins, radius, translucency, and font settings; gaps,
  borders, and per-app opacity; pointer and touchpad settings.
- Right-click menus on empty bar space; the machine stays awake while a session is shown.

## 2026-09-24

- Hyprland-style dwindle tiling with a panel toggle.
- XWayland on demand, with a wlroots patch in `packaging/patches`.
- Browser and Electron protocols (linux-dmabuf, primary selection, xdg-activation,
  cursor-shape) and screen capture for screenshots and screen sharing through portals.
- Qt Quick shell: taskbar, launcher, desktop shortcuts, wallpaper, workspaces, control
  socket (`paw msg`), fullscreen, session lock, idle notification and inhibit.
- Standalone `--session` backend (DRM/libinput) that survives VT switches; first
  physical tests on an AMD laptop and an NVIDIA three-monitor desktop.
- Gentoo QEMU/KVM test VM scripts; GPL-3.0-or-later license.

## 2026-09-23

- Validated Lua configuration, snapping and grid placement, and the nested wlroots
  compositor with focus follows mouse, move/resize, and reload.
- Installable build with DESTDIR staging and an opt-in session entry.
