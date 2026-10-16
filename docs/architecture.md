# Architecture

How the code is laid out, and which files a change usually touches. [CONTRIBUTING](../CONTRIBUTING.md)
covers branches, building, testing and committing; [features.md](features.md) describes behavior.

## Processes

- **`paw`** (`src/main.cpp`) is the compositor. `main.cpp` loads the Lua configuration,
  watches it, runs scripts (screenshots, spawned programs) and answers the compositor's
  questions through a table of callbacks (`struct sh_callbacks` in
  `include/paw/backend.h`). Then it hands over to `sh_run`, the C compositor in
  `src/compositor/`, which calls back `startup` once it runs: the shell, the `startup` commands
  and, in a login session, the XDG autostart entries (`src/autostart.cpp`, tested by
  `autostart_tests`). Its `lock` callback starts the screen locker, the stock swaylock with
  arguments for the appearance profile's look (`src/lock_style.cpp`, tested by
  `lock_style_tests`). `paw msg ...` is the same binary acting as a control client.
- **`paw-shell`** (`shell/`) is the Qt Quick shell: panels, launcher, notifications, tray, OSD,
  and the text of the switcher and overview. It is an ordinary layer-shell client that
  connects to the control socket with `subscribe` and gets state lines and events.

## Layers

| Where | Language | What |
| --- | --- | --- |
| `src/compositor/` | C | The compositor proper, on wlroots. Shares one private header, `server.h`. |
| `src/*.c`, `include/paw/*.h` | C | Pieces the compositor uses that stand on their own: animations, window controls, shadows and tab strips (pixels), effect arithmetic, touchpad swipes' arithmetic, overview thumbnails, session files, the monitors' settings kept from the display settings window, the logind client, what dynamic window rules hold and give back, where the scratchpad's drop-down slides and is cut, the desktop cat's ways and its pixel art. Several are unit tested. |
| `src/*.c`, `include/paw/*.h` | C | Pieces the compositor uses that stand on their own: animations, window controls, shadows and tab strips (pixels), effect arithmetic, touchpad swipes' arithmetic, overview thumbnails, session files, the monitors' settings kept from the display settings window, the logind client, what dynamic window rules hold and give back, where the scratchpad's drop-down slides and is cut, where the exploded view's sheets go, where the scrolling layout's cube draws its strip, which strokes of the pointer make a shake and how large the cursor grows, how ambient particles drift and what they look like. Several are unit tested. |
| `src/config.cpp`, `src/config_schema.cpp`, `src/config_edit.cpp` | C++ | The Lua configuration: parsing, validation, the action table and the settings schema, and writing a setting back into the file, where it takes effect (the file's own, or the profile's in use), its comments and layout kept. |
| `src/tiling.cpp`, `src/layout.cpp`, `src/window_placement.cpp`, `src/overview_layout.cpp` | C++ | Pure geometry (tiling layouts, snapping, placement, the overview grid), with a C interface in `backend.h` and unit tests. |
| `src/import/` | C++ | `paw import` from Hyprland and Waybar. |
| `src/cli/` | C++ | The subcommands besides the compositor (`paw fetch`, ...), and `paw_cli`'s control socket client that `paw msg` uses too. |
| `shell/` | C++/QML | The shell. |

Keep geometry and arithmetic out of `src/compositor/` where you can: a pure function in its own
file can be unit tested without starting a compositor.

The configuration's Lua runs in a sandbox (`sandbox` in `src/config.cpp`): the base, string,
table, math and utf8 libraries without `load`, `dofile` or metatables, a budget of instructions,
and 64 MiB for what its code allocates. A call into a C function is one instruction however
long it runs, so whatever runs a configuration (`parse_config`, `configured_look`,
`shadowed_settings`) runs it first in a child process forked for it (`try_first`), killed after 3
seconds, and then for real, which gives the result or the error. The compositor and the shell
both load the file this way, so what that code calls must be safe in a child forked from a
process with threads: nothing that waits on a lock another thread may hold. The child process
and its time limit are `try_in_child` (`src/child_trial.cpp`), which `paw import` uses too for
a `hyprland.lua` (`src/import/hyprland.cpp`, a sandbox of its own).

Window rules' and bindings' patterns are compiled to be searched in time linear in the text
(`compile_pattern`), and in a title or app ID over 1 KiB only the first and last 512 bytes are
searched (`pattern_found`): a client sets its title as long as it likes.

## The compositor (`src/compositor/`)

Each file begins with a comment saying what it covers; `head -4 src/compositor/*.c` shows them
all. In short:

| File | Covers |
| --- | --- |
| `server.c` | Startup (creating every wlroots global and listener), shutdown, config reload, signals. |
| `server.h` | The shared types (`sh_server`, `sh_output`, `sh_toplevel`, ...) and, under a `/* file.c */` heading, every function one file calls in another. |
| `actions.c` | `run_action`: one `case` per action, handing it to the module that does it, and `shell_actions`, those the shell carries out. |
| `control.c` | The control socket: reading requests, commands that are not actions, subscribers and shell events. |
| `query.c` | `paw msg get ...`: one function per query, and the table that names them. |
| `headless_input.c` | Input devices without hardware for tests under `--headless`: keyboards, pointers that move and make touchpad gestures, touchscreens, drawing tablets with a pen, an eraser and a pad, and lid and tablet-mode switches. |
| `input.c` | Keyboards, key bindings, pointers' libinput settings, virtual devices, selection and drag-and-drop. |
| `input_method.c` | Input methods (fcitx5, ibus): text-input-v3 and input-method-v2 relayed between the application with the keyboard and the input method, its keyboard grab, and its popups beside the text cursor. |
| `key_release.c` | Key bindings with `release`, which run as their key comes up: a modifier key's (Super_L opening the launcher) only when it was tapped alone, any other key, button, wheel notch, gesture, finger or pen meanwhile (`key_release_cancel`) letting it go. |
| `keymap.c` | The keymap from the keyboard settings, given to every keyboard but virtual ones. |
| `cursor.c` | What is under the pointer, and the tile the user moved it onto (`pointed_tile`), focus on hover, button bindings, scrolling, the cursor image. |
| `cursor_hide.c` | The cursor hidden while typing (`mouse.hide_when_typing`) and once the pointer has rested (`mouse.hide_after`), as reasons of `cursor_hide`'s besides a screenshot's, until the pointer is used (`cursor_used`). |
| `cursor_shake.c` | Shaking the pointer to find it (`mouse.shake_to_find`): a shake (`src/shake.c`) grows the cursor, its image from the theme at three times the size given as a buffer at the scale of the moment, stepped on a timer; meanwhile what sets the cursor's image only notes it (`cursor_grown`), and the image it would have shows as it is back to its size; `get cursor_size`. |
| `gestures.c` | Touchpad gestures: the swipes `gestures` takes (workspaces and the overview following the fingers, requests), and the rest passed on to the surface under the pointer (pointer-gestures-unstable-v1). |
| `tablet.c` | Drawing tablets (tablet-v2): tools with pressure and tilt to the surfaces under them, the pointer where tablet input is not taken, pads to the surface with the keyboard, and the output a tablet is mapped to. |
| `touch.c` | Touchscreens: fingers to the surfaces under them (wl_touch), the pointer for clients without touch and the compositor's own controls, and the output each screen is mapped to. |
| `grab.c` | Moving and resizing with the pointer, magnetic edges, dropping. |
| `snap.c` | Snapping a window dragged to an edge of its output: the zones, the preview, the drop. |
| `tile_drop.c` | A tile moved with the pointer holding its place in a tiling, the other tiles gliding aside to open the place a drop would give it, and the drop into it. |
| `lift.c` | Lifting a window moved with the pointer: drawn a little larger about where it was taken, its shadow softer and further off, set down onto an edge its magnet holds, settling as it is dropped. |
| `stacking.c` | The layers windows are drawn in among themselves (tiles, floating windows over them with `layout.floating_above_tiles`, windows kept above the others) and keeping a window above. |
| `dynamic_rules.c` | Dynamic window rules: what they hold a window to as its title and app ID change, given back as they stop (with `src/dynamic_rule.c`). |
| `focus.c` | Keyboard focus and urgent windows. |
| `toplevel.c` | Windows: xdg-shell toplevels and popups, opening by window rules, maximize, fullscreen, minimize. |
| `xwayland.c` | X11 windows and the XWM waker. |
| `frame.c` | Borders, opacity, rounded corners, window controls, shadows, tab strips. |
| `gradient_border.c` | Borders in gradients: the pieces `src/border.c` paints, a strip along each side the scene stretches and a square at each corner, in place of the border's rects. |
| `placement.c` | Snapping, maximizing, reflowing, moving and resizing by keyboard. |
| `morph.c` | A window gliding to a new place and size together (maximized, snapped, restored, made fullscreen, moved or resized by keyboard), its client's commits meanwhile, and the size its frame is drawn at. |
| `tiling.c` | Glue between windows and the layouts in `src/tiling.cpp`. |
| `pseudo.c` | Pseudotiling (`pseudo_toggle`, Hyprland's pseudotile): a tile of dwindle shown at its floating size in the middle of its tile (`sh_tiling_pseudo`), resized by the pointer or the keyboard about that middle rather than by its splits. |
| `workspace.c` | Workspaces per output, sticky windows. |
| `output.c`, `output_moves.c` | Monitors and their configuration; windows and workspaces moving between outputs. |
| `output_power.c` | Monitors turned off and on in the layout: wlr-output-power-management. |
| `mirror.c` | Mirroring: a monitor out of the layout showing another's frames, scaled to fit, with its hardware cursor. |
| `hdr.c` | HDR (`outputs.monitors`' `hdr`): monitors driven in BT.2020 with PQ where they, the renderer and the test allow, and color-management-v1 for applications' colours. |
| `tearing.c` | Tearing (tearing-control-v1, `windows.allow_tearing`): which fullscreen window may have its frames shown at once, and the asynchronous page flips. |
| `display_mode.c` | The display_mode action (Windows' Win+P): extend, duplicate, internal, external, and the popup that steps through them. |
| `display_settings.c` | The display settings window's monitors: the settings it kept (`src/output_state.c`), laid over `outputs.monitors`, the primary monitor, `get monitors`, and `monitors apply` trying every monitor's settings at once, kept or taken back. |
| `layer_shell.c` | Panels and other layer surfaces. |
| `panel_drag.c` | The taskbar dragged to another edge, or by its inner edge to resize it (`shell.panel_draggable`): `panel_drag OUTPUT` takes the pointer while the left button is held, as a window move does, and tells the shell the edge of that monitor nearest it, or for `panel_drag resize EDGE` the pointer's distance from that edge, and where it is let go. |
| `group.c`, `scratchpad.c`, `swallow.c`, `switcher.c`, `overview.c`, `session.c` | One feature each. `session.c` also saves a login session as `last` as it ends (`session_save_last`, from the power actions and quit) and restores it after `startup` (`session_restore_last`, from `sh_run`), asking `sh_callbacks.started` which missing windows startup and autostart will open. |
| `switches.c` | Switch devices: the lid turning a laptop's panel off and on (clamshell), switches' bindings. |
| `effects.c` | Dimming, peeking at the desktop or at one window, night light, magnifier, hot corners. |
| `crossfade.c` | A new appearance profile crossfading in: a picture of what each monitor showed, held over the scene until the shell has drawn the new look, then faded out. |
| `lock.c` | Session lock and idle/sleep inhibitors. |
| `idle.c` | Power saving after a while without input: dimming, monitors off, locking, suspending. |
| `power.c` | The power actions: suspend, hibernate, reboot and power off through logind (`src/login1.c`), locking first, closing windows first, log out. |
| `foreign_toplevel.c` | Window lists for taskbars and single-window capture. |
| `window_control.c` | The shell's window menu, window pictures and windows' own icons, and the windows each workspace shows: paw-window-control-v1, which names a window by its taskbar handle. |
| `minimize.c` | Minimizing into the taskbar and restoring out of it: where taskbars show each window on each output (wlr-foreign-toplevel's `set_rectangle`, the window control's), the box a window shrinks into, and the copy of it that does. |
| `scaled_capture.c` | The capture source for a window's picture: the window scaled down to fit a size on the renderer, smoothly. |
| `type.c` | Typing text into what has the keyboard (`paw msg type`), on a keyboard and keymap of its own. |
| `window_icon.c` | The icons windows supply themselves (xdg-toplevel-icon-v1; X11 windows' from `xwayland.c`), kept for the window control to send the shell. |
| `volume.c` | The volume, microphone and brightness actions, which the shell carries out. |
| `binding_mode.c` | Binding modes: which set of key bindings is in use. |
| `shortcuts_inhibit.c` | Keyboard shortcuts inhibitors (keyboard-shortcuts-inhibit-unstable-v1): the keys the bindings take going to a virtual machine, a remote desktop or a game while it has the keyboard. |
| `toys.c` | Toys, which only draw: `shake`, `gravity` and `party` posing windows through the animator, a party's rainbow borders and confetti, each window let go as anything else happens to it; `get toys`. |
| `cube.c` | The scrolling layout's view turning as a cube (`layout.scroll.cube`): an arrangement that moves the view poses its tiles through the animator on the sides of a square turning a quarter (`src/cube.c`), each let go as anything else happens to it; `get cube`. |
| `rice.c` | A rice on trial (`paw rice try`): `rice try NAME` switching to it, the time before it goes back unless `rice keep` keeps it, and telling the shell. |
| `profile_schedule.c` | The day and night profile schedule (`profile_schedule`): the profile the last sunrise or sunset brings (`src/profile_schedule.c` finds which came last), picked as `profile NAME` through `sh_callbacks.command` and a reload, the last one applied kept in `$XDG_STATE_HOME/paw/profile-schedule`; a timer looking at the clock each minute and after each reload, `profile_auto`, and `get profile_schedule`. |
| `cat.c` | The desktop cat (`paw cat`): what it sees of the windows and monitors, stepping `src/cat.c`'s state machine on a timer of its own and drawing its pixel art (`src/cat_sprite.c`) in a tree over the windows that takes no input. |
| `ambient.c` | Ambient particles (`appearance.ambient`, the `ambient` action): sakura petals, snow or fireflies in a tree of scene buffers for each output, over the wallpaper and under the bottom layer, taking no input; stepped every 33 ms from a timer of its own while an output shows them, `src/ambient.c` moving them and painting their pictures; paused while the session is locked, the output is off or shows a fullscreen window or the screens are dimmed, and faded out after a while without input; `get ambient`. |
| `wrapped.c` | Counting for `paw wrapped` (`wrapped.enabled`): a call from each place something it counts happens, the time active and idle and each application's, saved to the day's file (`src/wrapped_day.c`) and listed by `get wrapped`. |
| `xray.c` | The exploded view (`paw xray`): the focused output's layers as live copies fanned out in a stack (`src/xray_layout.c`), which the pointer turns and the wheel spreads, and `get xray`. |
| `heatmap.c` | The pointer heatmap: how long the pointer rested and how often it clicked in each cell of each output's grid (`src/heatmap.c`), in memory for the session alone, `get heatmap`, and the heatmap action's overlay: a picture of each grid blurred and coloured, stretched over its output under the drag icons and the lock, taking no input, with a legend in the coolest corner, drawn again twice a second from a timer while it shows and fading with `heatmap_tick` from `output_frame`. |
| `color_picker.c` | The colour picker (`pick_color`): the cursor turned into a loupe (`src/color_picker.c` paints it) showing the pixels around the pointer, read from a picture of the output under it drawn on the renderer without the cursor, night light or a client's gamma table (as `crossfade.c` draws its pictures) and drawn again while the output draws; a click, Return or Space picking the pixel under the crosshair onto the clipboard (a data source of the compositor's own), to the subscribers (`color-picked #rrggbb`) and to each `paw msg pick_color` waiting on a copy of its connection; Escape or another button cancelling; `get color_picker`. |
| `blur.c` | The blur behind windows (`windows.blur`): each output's background (the background layer over `appearance.background`) drawn at half its pixels, read back and blurred on the CPU (`src/blur.c`) once it settles after a change, and under each window's surfaces the part of that picture it covers on the screen, placed again before each frame by `blur_frame` from `output_frame`; `get blur`. |

A function used by one file is `static`; one used by several is declared in `server.h` under
the file that defines it. The build warns (`-Wmissing-prototypes`) about one that is neither.
Code that only exists with XWayland is inside `#if WLR_HAS_XWAYLAND`.

### Naming a window from the shell

The shell knows windows only as wlr-foreign-toplevel handles, which carry a title and an app id
but no name the control socket could be asked about, and two windows may share both. Its window
menu reaches the compositor through a protocol of paw's own,
`protocols/paw-window-control-v1.xml`, whose requests name a window by the handle the shell
already holds, on the same Wayland connection. `paw_window_control_v1.get_window(handle)`
gives a `paw_window_v1` that sends the window's `output` (connector name), `workspace` (from
1) and `state` (sticky, floating, tiled, and whether its workspace tiles), then `done`, at once and
again after each change, and takes `move_to_workspace`, `move_to_output`, `set_sticky` /
`unset_sticky` and `set_floating` / `unset_floating`. Since version 2 its `get_capture_source`
gives the window's `ext_image_capture_source_v1`, and since version 3
`get_scaled_capture_source(width, height)` gives one whose frames the compositor scales down to
fit that many buffer pixels; the shell copies them with ext-image-copy-capture for the taskbar's
pictures of windows.

`window_control.c` finds the window by looking for the handle among the resources of each
window's `wlr_foreign_toplevel_handle_v1`, so a handle that is gone names nothing and its object
is inert; `unpublish_toplevel` makes a window's objects inert as its handles close. The requests
call what the actions do for the focused window (`move_toplevel_to_workspace`,
`move_toplevel_to_output`, `set_sticky`, `set_floating`), so they neither focus nor raise it,
and do nothing while the session is locked. A change reaches the objects through
`window_objects_changed`, which `notify_subscribers` and the tiling call: it sends what changed
from an idle callback, once the change is over. Like the foreign-toplevel manager, the global is
offered to every client; it lets a client do nothing to a window a taskbar cannot already do.
`tests/window_probe.c` is a client of it for `window_control_smoke`, `window_capture_smoke`,
`window_peek_smoke`, `window_icon_smoke` and `workspace_view_smoke`, and `TaskModel` the shell's.

A window's capture source is the one screen sharing gets for its ext-foreign-toplevel-list
handle, from `toplevel_capture_source` (`foreign_toplevel.c`): made from a private scene that
holds only the window's surfaces (`capture_scene`, which `publish_toplevel` builds), when first
asked for, and shared by every client; it renders the window at its logical size without its
frame, also while it is minimized or on a workspace not shown, and its sessions stop as the
window's handles close. While the session is locked, or once the window is gone, both ways give
an inert source, whose sessions stop at once, rather than none: a client's next request would
then name an object that does not exist, a protocol error that disconnects it. wlroots renders
the source again only where the window changed since it last did, and for every session at
once: a session started while another runs on the same window gets its first frame only when
the window next draws, so a client keeps one session per window.

A scaled capture source (`scaled_capture.c`, adapted from wlroots' source for a scene node)
renders the same capture scene through an output of its own, a `wlr_output` on an empty backend
that no client is offered, at a scale below 1. Each request makes one, so no two clients'
sessions share it. It goes once its client has destroyed the resource and no session
captures it, from an idle callback since wlroots stops a source from inside a session's
teardown, or with the capture scene as the window closes, which stops its sessions. A capture
scene takes 16 outputs at most (wlroots' scene asserts below 64); a request past that gets an
inert source. The renderer samples bilinearly without mipmaps, so one pass from a large window
to a picture a tenth its size would leave out most of its pixels, and text would shimmer as it
changes. Instead the scene renders at no less than half the density of the window's buffers,
then steps halve that on the renderer, each pixel the average of two by two
(`wlr_render_pass_add_texture` into a swapchain of each step's own), until it is within twice
the frame's size, and a last bilinear pass makes the frame. A frame's pixels thus stand for all
those they cover, so the shell asks for the size it shows and scales nothing. The steps' buffers
last while a session captures; a buffer the size of the output, such as a window's own at its
logical size, is taken as it is (direct scan-out). A frame is rendered when a session asks for
one and the window changed since, so a window that stops drawing costs nothing, and at most once
for each frame asked for, which paces a window shown nowhere else. A session owed what changed
since its last frame, one started while another runs or one that let frames go by, gets the frame
as it stands at once (the last one kept for it) rather than waiting for the window to draw.
`paw msg get pictures` lists these sources.

Measured on a headless compositor with the pixman renderer, a window of 3840 by 2108 pixels that
redraws whenever it may, minimized, with the card of its 240-pixel picture open: a frame was
32.4 MB at the window's size and is 127 KB scaled. The shell took 9 to 11 % of a core for ten
full-size frames a second, 0.7 % for ten scaled ones and 2.3 to 2.5 % for thirty, most of it
Qt Quick drawing the card in software. The compositor took 12 to 13 % for ten full-size frames,
10 % for ten scaled and 26 to 31 % for thirty, pixman rendering the steps on the CPU; a GPU
renderer does that on the GPU, and copies 127 KB back where it copied 32 MB.

Since version 3 a `paw_window_v1` also sends the window's `pid` with its state, before
`done`: `toplevel_pid` (`toplevel.c`), the Wayland client's peer credentials or the process XRes
names for an X11 window, 0 when unknown, for the shell to match the window with the sound
server's streams.

Since version 4 `set_peek` peeks at the window and `unset_peek` ends the peek (`effects.c`), for
the taskbar's card while the pointer rests on a picture. The other windows fade with
`peek_fade`, the desktop peek's, while the window's own `peek_shown` fade, which `peek_scale`
(the opacity `refresh_frame` gives its buffers) weighs against it, keeps it in full. It is drawn
over them from `peek_layer`, a tree between the fullscreen windows and the top layer, so that the
bar and the card stay over it, while an empty tree keeps its place among the windows; one
fullscreen over the panels stays where it is, over the others already. A hidden window
(minimized, or on a workspace its output does not show) has its node shown for the peek where it
is (`peek_lent`), fades in and out, and is hidden again once it has faded out (`tick_effects`);
`scene_node_at` passes over it, so it takes no input. Moving the peek to another window keeps the
others faded and crossfades the two; ending it fades the others back, a window shown anyway
staying in full until they are, and puts the window back in its place unless it was moved on
purpose meanwhile, as focusing raises it. Nothing else about the window changes. The peek ends
as the object that asked is destroyed (its client too), as the window closes
(`forget_window_peek` in `unmap_toplevel`) or leaves the taskbar (`window_objects_forget`), is
focused (`focus_toplevel_raise`, which a click on its picture does), as the session locks (at
once), and as the desktop peek starts. `paw msg get window_peek` lists what it does to each
window, and `tests/window_peek_smoke.py` tests it.

The window switcher peeks at its selected window through the same functions (`switcher.c`). The
shell's `Switcher.qml` asks for it as the switcher shows (`ShellController::switcherShown`, the
`switcher_peek` request, which `switcher_ask_peek` takes), but not with `peek.switcher` off, nor
in the macOS style but for one application's windows (`switcher_app`, whose `switcher` line ends
with `app`: `ShellController::switcherApp`, `Switcher.qml`'s `iconsOnly`). The first peek waits on `switcher.peek_timer` until `SWITCHER_PEEK_MS` after
the selection last moved (`selected_at`); while that peek goes on (`switcher_peeks`: the peek is
still at `switcher.peeked`, the window it peeked at), `switcher_select` moves it at once, and once
something else ended it (focus, the desktop peek, a taskbar's picture) the selection rests again
first. The selected window closing ends the peek (`forget_window_peek`); `switcher_forget` peeks
at the window selected in its place a millisecond later, once `unmap_toplevel` has moved the focus,
which may go to that window and would end a peek begun before. Its peek has no `peek_object`, so
no window object ends it, and it takes the peek over from a taskbar's; `switcher_close` ends it,
through `focus_toplevel_raise` when the window chosen is the one peeked at (in front already, the
others coming back around it), and otherwise with `end_window_peek` before focusing the window
chosen. The lock ends a peek before closing the switcher, so that it ends at once.
`peek.switcher` turned off by a reload ends the switcher's peek at its next step, a selection or
its closing. `tests/switcher_peek_smoke.py` tests it without the shell, sending
`switcher_peek` itself, and `tests/peek_shell_smoke.py` with it.

Version 4 also sends the window's `id` once, with its first state: `sh_toplevel.id`, a number
`publish_toplevel` gives a window the first time it publishes it (counting up from 1 in
`sh_server.last_window_id`) and gives no other window while the compositor runs. A window keeps
it when published again, as a swallowed terminal is. The control socket names windows by it
where the shell has to find them among its handles: the switcher's `switcher-window` lines end
with it.

Since version 5 a `paw_window_v1` also sends the icon the window supplies itself, for the
shell to show a window whose application it cannot find by its app id among the desktop entries
and icon themes, as for a game run through Wine or Proton, a Java program or an AppImage. A
Wayland window gives it through xdg-toplevel-icon-v1, whose global `sh_run` makes with the sizes
it prefers (16 to 256 pixels): `server_set_xdg_icon` (`window_icon.c`) keeps the icon's name and
a copy of the pixels of one of its buffers, argb8888 as it is or xrgb8888 made opaque (a buffer in
another format is passed over), so that no wlroots buffer is held past the request. wlroots hands
the icon over as the client asks, not at its next commit, which a taskbar's icon needs no lining
up with. An X11 window gives `_NET_WM_ICON`, which wlroots does not read but signals as
`set_icon`, also as the window is associated: `xwayland_set_icon` fetches it with
`wlr_xwayland_surface_fetch_icon` (xcb-ewmh), a round trip on the XWM's connection, and
premultiplies the colours, which X11 gives straight; it has no name. Of the sizes a window gives,
`icon_size_rank` prefers the largest that fits within 256 by 256 pixels, else the smallest larger
one, measured by the longer side. `set_toplevel_icon` keeps it on the window as an `sh_icon` (a
name, and premultiplied ARGB8888 pixels in rows without padding), counting changes in
`icon_serial` and passing over an icon the same as the one there, as an X11 window's is when it
is read again. `send_window` sends `icon` (the name, or null) and `icon_image` to each object whose
`icon_serial` is behind the window's, with its first state only when the window has an icon.
`icon_image` passes a memfd of width × height × 4 bytes for each client, sealed against writing;
libwayland sends a copy of it, so the compositor closes its own at once. `paw msg get
window_icons` lists each window's number, app id and icon's name and size, and
`tests/window_icon_smoke.py` tests them.

Since version 6 the `state` also has `above` while the window is kept above the others, sent only
to objects of version 6 as an older client knows no such bit, and `set_above` / `unset_above` call
`set_above` (`stacking.c`), as `toggle_above` does for the focused window, for the window menu's
Keep Above Others. A window kept above is drawn in `above_windows`, a tree between the other
windows (`windows`, and `floating_windows` with `layout.floating_above_tiles`) and the
fullscreen ones; `window_layer` names the tree a window belongs in, and everything that puts a
window back among the others (focus, leaving fullscreen, the overview, the peek) puts it there.

Since version 7 `set_rectangle(output, x, y, width, height)` says where the client shows the
window on an output, in its logical coordinates, for the window to minimize into and restore out
of: what `zwlr_foreign_toplevel_handle_v1.set_rectangle` says on a surface of the client's, which
paw's shell cannot send, its handles living on a Wayland connection apart from its surfaces.
`minimize.c` keeps both kinds on the window (`sh_window_rect`, a list with the last set first), one
per resource and output, until the resource that gave it goes (the window control object, or the
taskbar's surface); a foreign-toplevel rectangle on a layer surface becomes its output's
coordinates as it comes (`surface_place`), and one on any other surface is passed over. Unlike the
other requests it is heard while the session is locked, as it changes nothing of the window. The
rectangles go as the window's handles close (`unpublish_toplevel`). `paw msg get minimize` lists
them, and `tests/minimize_smoke.py` (with `window_probe`'s `taskbar` and `wayland_probe`'s
`PAW_PROBE_BUTTON`) tests them.

Since version 8 `paw_window_control_v1.get_workspace(output, number)` gives a
`paw_workspace_v1`, which lists the windows that workspace of the output shows, for the taskbar's
card of a workspace: the output's size, then each window from the bottom up by its `id`, with the
rectangle its picture covers from the output's corner, then `done`. `list_workspace`
(`window_control.c`) walks the trees `window_layers` names from the back, taking the empty node a
peek leaves in its place (`peek_place`) for the window peeked at, and passing over that window's
own tree meanwhile, wherever it is; it keeps a window that is published, mapped and neither
minimized (as a hidden scratchpad window is), behind another member of its group nor swallowed,
on that output and workspace or sticky. A window's rectangle is where it rests (its tree's place,
which animations leave alone) moved by the extents of its surfaces in its capture scene
(`window_picture_extents`, `scaled_capture.c`), which start at its geometry's corner as the
window's tree does: exactly what its picture shows, a client-side shadow included. The list is
sent at once, and again, whole, only when it differs from the last one the object sent, from an
idle callback that lists again the objects marked: `workspace_objects_changed` marks them all, and
is called by `window_objects_changed` (so by `notify_subscribers` and the tiling), by a window's
surface committing at a size other than the last (`workspace_window_committed`, from xdg-shell's
commits; an X11 window's geometry changing), and by a scaled capture source whose window's
extents changed as it rendered; each object's listener on its output's commits (what changes on
the workspace shown, moving or redrawing, comes with one) marks that object alone. While no object
is held this costs one check of an empty list. `paw msg get workspace_views` lists the objects,
and `tests/workspace_view_smoke.py` tests them with `window_probe --workspace`.

### Minimizing into the taskbar

`minimize_toplevel` calls `minimize_animate` first, while the window still shows as it is:
`copy_window` copies what `content` shows into a tree just above the window, at its place (the
shadow on its own, then the window and what is drawn on it in a tree with the window's rounded
clip, then the gradient border's pieces and the rounded frame, which round themselves outside
that clip, as on the window), and `sh_anim_fold_away` (`src/animation.c`) folds it into the box
`fold_target` gives, then destroys it: the window's button on its output, from the rectangles
above, else the middle of the strip a panel keeps along the output's edge (what `usable_area`
leaves out), else a third of the window's size below its middle. Once the focus has passed on,
`minimize_raise` puts the copy back over the window that came forward. Restored,
`focus_toplevel_raise` calls `restore_animate` once the window is shown where it goes, which
lands what ran on it and has `sh_anim_unfold` grow `content` itself out of the box: the window
draws as it is and takes input where it ends up (`sh_animator_rest_at` puts a folding window at
rest for a lookup). Neither runs while the session is locked, for a window in the scratchpad, or
for one peeked at (by the taskbar's picture, the switcher or the palette), which is shown
already, and no restore for one a peek showed while hidden and still shows as it fades
(`peek_lent`), whose copy still on its way in goes at once; the overview lands the restore of the
window it picks, whose thumbnail glides there.

A fold scales the tree's nodes about one point, so that the window's box shrinks evenly, keeping
its proportions, to fit the target box as its middle slides on a straight line to the box's
middle: at `at` of the way the scale is `1 + (s - 1) * at`, for `s` the scale at which the window
fits the box (kept between 0.001 and 0.8), about the point `rest + (into - rest) / (1 - s)` of
the line through the two middles, which that scaling leaves in place. Its opacity is `1 - at²`,
nearly whole for the first part of the way.
`sh_anim_folded` says how far along a fold is drawn and with which boxes, and a fold that turns
back goes from there in `sqrt` of the share of the time the way back takes: restoring a window
whose copy is still on its way in, at the same place, finishes the copy and grows the window from
as far in as the copy was (`restore_animate`); minimizing a window still growing out
(`sh_anim_settle`, which ends a fold and a fade but leaves a glide) folds its copy from there.
The rounded clip, the frame's rounding and the shadow's and gradient border's clips are not
nodes the animation moves: a fold calls back its owner after each step and as it ends
(`sh_fold.update`), which maps them with `sh_anim_fold_map` (`window_folded` for a window, from
the clips `window_clip` keeps as frame.c asked for them, which it puts back once the fold is over;
`copy_folded` for a copy, whose rounding is freed with its tree). For what is redrawn at rest on
every frame to be drawn folded, the animator steps its tweens (a focus fade, which redraws the
frame) before its animations, and `output_frame` ticks the effects' fades before the animator.

### Touchpad swipes

`gestures.c` hears the cursor's swipes. One of as many fingers as a swipe of `gestures` has waits
(`SH_SWIPE_WAITING`) until `src/swipe.c` gives it a direction, past `SH_SWIPE_THRESHOLD` of
travel along the axis it went most; a direction no swipe has hands it to the window under the
pointer, which hears its begin and the travel so far at once. `swipe.c` also measures how far
along a step the fingers are (`gestures.distance` a step), their speed over the last 80 ms before
they lift, and whether that finishes the step (`sh_swipe_finishes`: past half way, or a flick
toward it; never a flick away). `tests/swipe_tests.cpp` tests it.

Workspaces follow the fingers through the slide `switch_workspace` plays, held at the fingers'
progress instead of the clock: `workspace_swipe_hold` (`workspace.c`) holds each window of the
output's workspace with `sh_anim_hold` (`animation.c`), its `content` offset and its buffers
faded as far as the slide would have them, and the windows of the workspace it heads for, hidden
still, as held copies of their buffers (`sh_anim_hold_copy`). A held animation lies outside the
animator's running list, so neither the clock, a late frame nor turning animations off moves it,
and each frame lays the hold again over what a client drew. `workspace_swipe_end`, given the
fingers' speed as they lift, either switches (the leaving windows going on as copies with
`sh_anim_slide_away`, the arriving ones taking over from their copies with `sh_anim_slide_from`,
each in what remains of the slide's time) or slides everything back. The overview follows them
through `overview_hold` and `overview_release`, which set its `progress` in place of its timer's.

Past the first or the last workspace the target is out of range, and `workspace_swipe_hold` pulls
the output's windows toward it as a rubber band: `sh_swipe_past_end` (`swipe.c`) says how far,
the fingers' pull in steps times the limit L given as a rubber band gives it (p × L / (p + L)),
L being the slide's distance or a tenth of the output's width, whichever is less, so that the
windows start as the slide would (or as a slide of a tenth of the width would, for a longer one)
and a whole step takes them half of L; for a slide that only fades it dims them instead. Let go,
they glide back with `sh_anim_spring_back` (`animation.c`) in the `move` animation's time and
curve, from where they were held and at first as fast as the fingers moved them (their speed
through the band's slope, `rate`). The swipe notes the band's way and when its spring began
(`sprung`, `sprung_at`), and the next one on that output and workspace that finds the windows
still in that spring (`springing`) takes them over where they are drawn: `sh_swipe_pull_at` says
how far past the end fingers would be for their offset (or their opacity, for a slide that only
fades), and the first hold keeps the slide that far ahead of the fingers (`shift`) until they
lift, so that neither pulling further nor coming back jumps. `tests/touchpad_gestures_smoke.py`
drives all of it with a headless pointer, at the event times it gives, and
`tests/rubber_band_smoke.py` the rubber band, checking where the windows are drawn with `get
drawn`.

A step with nowhere to go, the panel's wheel past either end (`workspace_nudge` on the control
socket, which `ShellController::nudgeWorkspace` sends) or `workspace_next` with a single
workspace, nudges the output's windows (`workspace_nudge`, `workspace.c`) as far as the rubber band
gives a third of a step past the end. `sh_anim_nudge` starts a glide with no offset of its own but
a speed, which the glide's velocity term carries out to a turning point a third of the way through
and back to rest, in twice the workspace slide's time; a window gliding already starts from where
it is drawn and aims for the same turning point, and one on its way out that way goes on as it
is, so that a quick wheel does not pile nudges up.

### Animations and frames

An output draws a frame only when the scene changed on it (wlroots' scene damage asks for one),
and `output_frame` first advances every animation to the frame's time (`sh_animator_tick`). A
glide that moves less than a pixel between two frames, as a slow spring's start or a nudge from
rest does, changes nothing and so brings no frame: it would stall, and the late-frame rule would
land it unseen at the next tick, with anything started meanwhile. So while anything runs the
animator also ticks by itself from a timer every 16 ms (`TICK_MS`, `animation.c`; half of
`late_ms` when that is shorter, so that no tick of its own is taken for a late one), set as an
animation starts from idle and again from each tick until nothing runs; a window changing size that
waits for its client to draw the new size goes on the same way. What a tick changes damages
the outputs it shows on, which then draw at their own rate; an output where nothing moves draws
nothing, a commit that failed (a VT switched away) is not tried again for an animation, and a
tearing output's asynchronous flips come only as its picture changes. Nothing else asks outputs for
frames for animations. `get output_frames` counts each output's frames and `get drawn` says where
each window is drawn and how faded, for tests.

### Touchscreens

`touch.c` hears the cursor's touch events. A finger coming down asks `press_target_at`
(`cursor.c`) what a press there reaches: a client's surface, or nothing where the compositor takes
presses itself (window controls and their corner, tab strips, drag strips, resize bands) or there
is only the desktop. A surface whose client bound wl_touch (`wlr_surface_accepts_touch`) gets the
finger through the seat, and where the surface was in the layout is kept, so that the finger's
motion stays in its coordinates when it leaves it. Anything else gets the first finger as the
pointer: the cursor warps to it and `server_cursor_button` hears a left button as from a mouse, so
the controls, the drag strip, button bindings and the overview take it as a click.
`map_touchscreens`, which `arrange_outputs` calls, maps each device to its output.

`tablet.c` does the same for drawing tablets' tools: one over a surface that takes tablet-v2
(`wlr_surface_accepts_tablet_v2`) is near it, as wlroots' tablet tool, and keeps it while its tip
is down (an implicit grab, its motion in the surface's coordinates as a finger's); over anything
else it is the pointer, the cursor following it and `server_cursor_button` hearing its tip and
buttons. Pads follow the keyboard's focus through a listener of their own; wlroots would send
their buttons to the last surface they entered, so `pad_focused` checks it first.

### Snapping

Where a window dragged to an edge snaps (`sh_snap_zone`), a step of the Win+arrow cycle
(`sh_snap_cycle`), the quarters an arrangement covers and the slot Snap Assist offers
(`sh_snap_quarters`, `sh_snap_assist_slot`) are pure functions in `src/layout.cpp`, beside the
halves' and quarters' geometry (`sh_placement`), with unit tests in `tests/layout_tests.cpp`.
`snap.c` follows a move with the pointer (`snap_follow`, from `process_cursor_move`): the zone,
the slot (`placed_slot`, `placement.c`) and the preview, a tree of two rectangles that sits in
`windows` just under the dragged window while it shows and among the guides while it does not,
easing through an `sh_tween` of five values (place, size, opacity). `finish_grab` asks
`snap_drop` first; a drop that would take the window into a tiling (`drop_tiles`, `grab.c`)
snaps nowhere but at the top. `place_by_hand_on` puts a window into a slot of a given output,
and `snap_assist_offer` follows every snap into a half or a quarter. Snap Assist is the overview
in a mode of its own (`overview_assist`, `overview.c`, with `assist` set): its area is the slot,
it lists other windows, has no strip, draws its backdrop over the slot alone, leaves the pointer
outside the slot to the rest of the compositor, closes at once, and puts the window picked into
the slot. It announces itself as `overview-assist` rather than `overview`; the shell's
`OverviewView` draws both, and takes no input for either.

### Moving a window with the pointer

`grab.c` follows the move; `lift.c` and `tile_drop.c` add to it. A middle press in a window
`mouse.middle_drag` lists is held back from the client (`middle_pressed`, `cursor.c`): travelling
`SH_DRAG_THRESHOLD` starts the move, as if from the press, and a release before that, or another
button, sends the press after all, so that a click still pastes. The lift is the animator's
(`sh_anim_lift`, `src/animation.c`): a window's `sh_anim` holds how far up it is (`lift`, easing
as a glide does, keeping its velocity as it turns), the scale and the point it grows about, and
its bounds, the window's geometry. Every walk of its nodes, the opening animation's and a
gesture's hold too, draws them through the lift on top: what is inside the bounds scaled to the
bounds drawn larger, their edges on whole pixels, and what is outside (the border's hollow rect,
the shadow's slices, a client's own frame) moved with the nearest edge at its own size, so that
a border meets the window to the pixel. A lifted window sits in the animator's `lifted` list,
whose lifts every frame draws again (a client's commit puts its nodes back) apart from the
running animations: neither the timer nor a late frame touches one that is up, and it costs
nothing while nothing is drawn. Lookups put it at rest as they do running animations. Its
`update`, on each frame it moves, is `refresh_frame`, which draws the shadow as `sh_shadow_lift`
(`src/shadow.c`) says: a cast shadow's slices stretched away from the window
(`sh_shadow_slices_scaled`, the image's cut-out a pixel under the window, where sampling between
pixels would otherwise show a light seam), a printed one's image painted further off in whole
pixels; and the rounded clips frame.c asks for through `window_clip`, the window's, its shadow's
and its gradient border's, are laid over the box drawn (`lift_box`, from `apply_clip` in
`scratchpad.c`, under a drop's cut), since wlroots' clips take no part in the scaling. The
lift's bounds are the size a change of size draws the window at (`frame_geometry`), so that a
lift draws on top of a morph's stretch.

A tile moved with the pointer stays a leaf of its tree (`tile_drop_lift` takes it out of the
tiling but for that), which `place_tiled` passes over (`tile_drop_place`, noting its rect for the
snap preview, `tile_drop_slot`). `sh_tiling_drop_target` (`src/tiling.cpp`) says where a drop at
a point would put it, found in the arrangement without it (taken out and put back exactly, its
boxes too). `tile_drop_follow` waits `DROP_DWELL_MS` with the pointer on one such place before
`sh_tiling_drop` moves the window there and the outputs reflow, so that clients are configured
only as a place opens. `tile_drop_release` makes the move at once as the window is dropped, or
takes it out of a tiling the drop does not put it in, before `finish_grab` tiles it
(`tile_toplevel_at` finds it in the tree already) or snaps it; `tile_drop_end`, from
`reset_cursor_mode`, takes a window still holding a place out of its tiling. A switch of the
workspace of the moved window's output, by `switch_workspace` or a swipe's `workspace_swipe_end`,
does not end the move: `carry_grabbed` (`workspace.c`) closes the window's place with
`tile_drop_end`, gives it the new workspace, and once that shows `carry_on` follows the pointer
again, so that it looks for a place in the tiling there. `slides` leaves the moved window out of the
workspace slide and a swipe's hold, which would move it from under the pointer.

### Changing size

A window placed again at another size (`place_toplevel`, `restore_toplevel`, `unarrange_in_place`,
the keyboard's moves and resizes, a drag off a snap, floating a tile, fullscreen) glides there
with `toplevel_morph` (`morph.c`): its caller notes `morph_from` (where the window is drawn and
at what size) before it configures the window, and the morph starts from there with
`sh_anim_morph` (`src/animation.c`), so that a placement made within another (a reflow as
fullscreen ends) does not count the offset twice. That is the tiles' glide with a size along: the
drawn size eases on the glide's time and curve, and a retarget carries its speed as the place's
does, but for a glide begun in the same millisecond, which was never drawn. The client draws the
new size only after the configure, so the animator stretches its surfaces (`sh_toplevel.surfaces`,
the scene's tree of them in `content`) from the size they were drawn for to the size drawn, about
the window's corner, setting each stretched buffer's opaque region aside meanwhile (the scene takes
it unscaled), and leaves the rest of `content`, the frame, to `refresh_frame`, which the morph
calls as the drawn size changes and draws around `frame_geometry`, the geometry at that size.

The window's commit listener runs before the scene's on the window's own surface (an X11 window
has one from its map for this) and calls `morph_commit`: a new size makes a flattened copy of the
surfaces as they were (`sh_thumb_clone`, without opaque regions) just over them, which fades out
over the rest of the glide or half its time, unless one is fading already, and the surfaces go back
to rest and lose their records, since what the client draws next can be the very size the
animation drew. Subsurfaces in synchronized mode have taken their new buffers by then, before
their parent's commit is heard, so the copy can pair the window's old buffer with their new ones.
Each commit answering the configure (`morph_serial` acknowledged, or an X11 surface at its
configured size) at a size other than the one given retargets the morph to it; one at the size the
surfaces had when asked counts only once the wait is over, since a client may acknowledge first
and draw the new size later. With no answer `MORPH_WAIT_MS` past the glide (the wait follows each
retarget), the window eases to what the client last drew. A resize with the pointer lands the morph
first. A lookup that could find the window, drawn or where it rests (`MORPH_REACH` around the size
given or drawn by the client), rests the morph, its frame too, laid out for the size the client
last drew, and hides the copy.

`frame_shown` eases how much of the frame shows over a change into or out of being frameless
(maximized on a monitor without tiling, or fullscreen), on the morph's kind, scaling the border's
colour, the shadow's opacity and the corners' radius. Whether the window draws its own shadow
(`frame_own_shadow`), which decides paw's shadow and corners for it, is read afresh only while it
rests with a frame, so that a client-side frame dropping its shadow as the window is maximized does
not turn paw's on and off on the way. Input and `get windows` use the final place at once;
`get drawn` reports the drawn one.

### The scratchpad's drop-down

`scratchpad.c` slides a window shown from the scratchpad in from the edge of its monitor's usable
area along the bar, and one hidden back out there. Where it goes is arithmetic in
`src/dropdown.c` (`sh_drop_plan`, tested by `dropdown_tests`): the edge (the top where a bar
reserves a strip along it, else the side with the widest strip, else the top), the offset at which
its frame, its geometry and border, lies wholly past the cut, and the cut: the usable area's edge
moved into the bar's strip as deep as the window's rounded corners reach, no deeper than the
strip, so that the corners pass behind the bar while the sides come out straight.
`sh_drop_swept` gives the part of the way past the monitor's own edge, where another monitor
(`other_output_in`) would show the window: there it is cut at that edge, or, without clips,
stops at it.

The motion is `sh_anim_drop_in` and `sh_anim_drop_out` (`animation.c`) on the window's `content`,
the tree the other animations move: a glide in twice `open`'s time with its curve, or out in one
and a half times `close`'s with its curve played backwards (`SH_CURVE_REVERSED`, `curve.c`), and a
linear fade over its first or last 40 %. A drop under way turns back from where it is drawn and
as faded as it is, keeping a quarter of the speed a glide would carry (what the new curve lacks of
the speed it had, held within twice the distance per duration). A window sliding out rests where
it slides to (`base_x`, `base_y`) and is `absent`: `sh_animator_rest_at` hides it for lookups as it
hides closing copies; any other animation that takes the tree over (`start`) brings it back,
present for lookups, resting at 0, 0, its fade landed. Activating a window as it slides out turns
it back in (`focus_toplevel_raise`), also while a peek shows it, and turning the scratchpad off
lands its slide. `hide_in_scratchpad` minimizes the window at once, so focus and the taskbar
move on, then shows its node again for the slide, offset to where it was drawn before it floated
(a tile leaves from its tile). The animation's `track` callback, which the animator calls after
each frame and once more as the animation stops or another takes the tree over, hides the node
once it is out of sight and puts `content` back at 0, 0; it also asks the output for the next
frame, since a window wholly past the cut damages nothing as it moves and would leave the
animation waiting.

The cut is drawn with the rounded-corners patch's `wlr_scene_tree_set_rounded_clip`, which only
the GLES2 renderer honours (`can_clip`). Only the nearest clipped ancestor of a node applies to
it, so the cut cannot be one clip around the window: every clip `frame.c` gives the window's trees
(`content`'s rounded corners, and the shadow's and the gradient border's own) goes through
`window_clip`, which keeps it in `sh_toplevel.clips` and, while a drop runs, takes off what lies
past the cut, in the tree's coordinates as the animation draws it; `track` lays the clips again
after each frame. A rounded rect shapes itself and no clip reaches it, so the border's hollow rect
(`frame_hole`) is cut by its size instead (`cut_ring`, also after `refresh_frame` lays it out,
through `drop_refresh`). What else the window draws (controls, tab strip, dim, surfaces) is below
`content` and its clip. Where no bar is deep enough to hide the corners (the plan's `square`: a
thin bar, or a monitor's edge with another monitor beyond), the window crossing the cut is clipped
square and its border's rounded rect left out, rather than draw rounded corners and a line of
border along the cut. `paw msg get scratchpad` lists each window's drop, offset, opacity and cut,
the clip its content is drawn with and its radius, and the border's rounded rect, and
`tests/scratchpad_drop_smoke.py` tests them.

The same cut keeps a tile of the scrolling layout to its monitor, whose columns lie on a strip
wider than it, past edges where other monitors would show them. `bound_tile` (`placement.c`, from
`place_tiled` and wherever a window leaves the tiling) sets the window's `bounded` and `bound`,
its monitor's box. While the frame, where it is drawn, crosses that box's edges, `cut_box`
intersects each clip with it, square, and `place_tiled` sets the glide's `track` so the cut
follows each frame. At rest wholly past the edges, `content` is disabled, so the other monitor
neither draws it nor sends the surface its enter, scale and frames, and `scene_node_at`
(`cursor.c`) looks again past a bounded tile at a point outside its bound, as it does through a
border's hole. A tile inside its bound is not clipped at all, so a fullscreen one still scans
out. `paw msg get bounds` lists each window's bound, whether its content is drawn and its clip,
and `tests/scroll_bound_smoke.py` tests them.

### Monitors turned off

An output in `outputs` (the layout) may be `powered_off` (`output_power.c`): its `wlr_output` is
committed disabled, which stops its frame events and scan-out, while it stays in the output
layout, so its `wl_output` global, its place, workspaces, windows and layer surfaces stay as they
are. `set_output_power` is the one way in and out; turning on clears the flag and calls
`configure_output`, which until then leaves a powered-off output alone (a reload or a
wlr-output-management change waits for it), and taking an output out of the layout clears it.
`publish_output_configuration` reports such an output as enabled, as sway does, and
`send_locked_if_presented` waits for no frame from it. wlroots' `wlr_output_power_manager_v1`
reports the mode from `wlr_output->enabled` on every commit that changes it, so clients hear of
the actions' changes too. `input_activity` (`input.c`), which every key, button, scroll and motion
event calls, wakes them through `wake_displays` once every output in the layout is off.

### Mirroring

A monitor whose settings name another as `mirror` (`mirror_source`, `mirror.c`) leaves the layout
in `configure_output` as with `enabled = false`, its windows moving away as when it is unplugged,
but its `wlr_output` stays on with its own mode and transform, and its `sh_mirror` (`mirror_start`)
listens to the source's commits. Each commit with a buffer locks it in place of the last one (with
its source and destination boxes, for a direct scan-out of a client's buffer, and the source's
size and transform then) and schedules a frame on the mirror; `output_frame` hands a mirror's
frames to `mirror_frame`, which draws that buffer as one texture through
`wlr_output_begin_render_pass`, fitted and centred on black, turned from the source's transform to
the mirror's, and the source's hardware cursor over it, which no frame of the source holds (a
software cursor is in the frame already). The commit that moves a hardware cursor (wlroots asks
the source for a frame as it moves) schedules a mirror frame too, which draws only when the buffer
or the cursor changed. Another way would have been a second `wlr_scene_output` of the same scene,
placed over the source's area: it would render the whole scene again for each mirror, could not
letterbox (a scene output shows a rectangle of the scene, the neighbouring monitors where the
shapes differ), would leave out what the source draws outside the scene (the magnifier, night
light's colour transform), and its surfaces would enter the mirror's `wl_output` too and weigh on
their preferred scale. Copying the source's frames costs one scaled copy per frame and nothing
while the picture stands still, and shows what the source shows. wlroots' screencopy samples an
output's committed buffers the same way, relying on implicit sync.

`refresh_mirrors`, which `arrange_outputs` calls last (arranging again when it changed something),
configures again any output whose mirroring no longer matches its source being in the layout, so a
mirror joins the layout while its source is unplugged, disabled or behind the lid, and leaves it as
the source comes back. A mirror follows its source's power (`mirrors_follow_power`, from
`set_output_power`), `lid_holds_off` counts a mirror as another monitor on, and `lock.c` waits for
a mirror to show a frame the source committed since the session locked: until then it draws black.
A mirror has no `wl_output` (the layout destroys an output's global as it leaves), so tests read its
picture with `headless_output capture NAME PATH` (`mirror_capture`).

`display_mode.c` sets the monitors' runtime settings (`sh_output.override`, as wlr-output-management
does) for the display_mode action and calls `apply_output_settings`; its popup is `sh_server.display_mode`,
whose timer takes the choice shown, and the shell draws it from `display-mode` lines
(`DisplayModes`, `display_modes.cpp`, and `DisplayModeView`). `display_mode_key`, which
`handle_keybinding` asks before the bindings, takes the arrows, Return and Escape while it is open.

### Display settings

The monitors' settings in force (`output_monitor`, `output.c`) are a monitor's runtime settings
(`sh_output.override`, from wlr-output-management or `display_mode`) while it has them, else
`configured_monitor`: its `outputs.monitors` entry, or the display settings window's line for it
laid over that entry whole but for `tiling`, copied into `sh_output.configured`.
`sh_server.display_settings.saved` holds those lines (`struct sh_output_state`,
`src/output_state.c`, tested by `output_state_tests`), read from the state file by
`display_settings_load` at startup, before the backend starts, and as the configuration reloads;
`saved_output` finds a monitor's by its connector name and description (`output_description`).
`primary_output_name` gives the primary monitor, a line's or `outputs.primary`, to
`arrange_outputs` and `display_mode.c`, and `hdr_asked` counts the lines' `hdr` towards offering
colour management.

`monitors apply` (`control_monitors`) starts from every connected monitor's settings as the window
shows them (`shown_settings`: those in force, with a live monitor's mode, scale, transform and
adaptive sync as they are, and its place before the shift to 0, 0, `sh_output.x` and `y`), lays
the request's KEY=VALUE words over them with the state file's own parser
(`sh_output_state_set`), and checks what no monitor's test can (`check_apply`). `test_monitor`
(`output.c`) tests each monitor's state as `configure_output` would commit it, as
wlr-output-management's test does for each head; a refusal changes nothing. Then `apply_saved`
notes, as a trial starts, the kept lines and each monitor's runtime settings by name, puts the new
lines in `saved`, drops the runtime settings and calls `apply_output_settings`; `placed_primary`
is set first, so the pointer does not jump to a new primary monitor. `took_settings` checks that
every monitor took its mode, scale and transform (`configure_output` falls back to the defaults
where its own test fails, and ignores a failed commit), and `revert_trial` puts back what was
noted, at once where one did not, after `TRIAL_MS` on the timer, or on `monitors revert`.
`monitors keep` writes `saved` to the state file through a temporary file, or removes it when
`monitors reset` emptied it. A reload during a trial reads the file again, which is the trial's
end. One more rule makes a line's `enabled = off` hold at startup: the first monitor to appear is
kept on whatever its settings say (`sh_output.kept_on`), and `server_new_output` configures it
again once another one is there.

### Tearing

`output_frame` asks `output_commit_tearing` (`tearing.c`) before committing a frame as usual. With
`windows.allow_tearing` it finds the output's fullscreen window and whether it may tear: it asks
through tearing-control-v1 (`wlr_tearing_control_manager_v1`'s hint for its surface) or a rule's
`allow_tearing` names it (matched again only when the window or the configuration generation
changes), and nothing else is drawn over it: `wlr_scene_output_for_each_buffer` visits the
output's buffers bottom to top, and one outside the window's tree after the first of its own is
over it. The session locked, the output magnified, the overview, the switcher or a peek rule it
out at once. Then it builds the frame with `wlr_scene_output_build_state`, sets
`tearing_page_flip`, tests it and commits without it where the test, or the commit, refuses.
`sh_output.tearing` keeps why and the counts for `get tearing`.

### HDR and colour management

`configure_output` asks `output_want_hdr` (`hdr.c`) to put an image description of BT.2020 with
PQ into a monitor's state when its settings ask for HDR and `wlr_output.supported_primaries` and
`supported_transfer_functions` (from the EDID and the connector's properties, DRM only) and the
renderer's `output_color_transform` allow it, tests the state and drops it again where the test
fails (`output_drop_hdr`), then tries a 10-bit format on top. The image description in the output
state is all the scene needs: `wlr_scene_output_build_state` converts each buffer from its own
transfer function and primaries into the output's, and a supplied colour transform cannot go with
one (wlroots asserts), so `output_frame` gives an HDR output no night light. `color_management_update`,
at startup and after each reload, creates `wlr_color_manager_v1` once a monitor asks for HDR and
the renderer converts colours (`input_color_transform`, Vulkan alone in wlroots 0.20), with the
transfer functions and primaries the renderer lists, and hands it to the scene
(`wlr_scene_set_color_manager_v1`), which then gives each surface's buffer the colours its client
describes and tells clients the image description their outputs prefer. `PAW_TEST_HDR` makes
headless outputs claim BT.2020 with PQ, but the headless backend takes no image description, so a
test reaches the commit test's refusal at most.

### Crossfading to a new appearance profile

`reload_config` compares the profile in use (`sh_settings.profile`, which `parse_config` fills)
before and after the configuration is read again, and calls `crossfade_start` (`crossfade.c`)
when it changed, before the rest of the reload changes anything in the scene. For each output in
the layout that is on and does not show a fullscreen window, `take_picture` draws the scene into a
buffer of its own through `wlr_scene_output_build_state` and a swapchain made for it, as the
magnifier draws (`effects.c`), with direct scan-out locked out (`wlr_output_lock_attach_render`),
and the idle dimming, the drag icons and any software cursor hidden for that drawing alone (a
hardware cursor is in no frame), since they go on over the picture; the swapchain goes at once,
the buffer staying while the picture shows. A gamma table a client set is set aside for the
drawing too (fields wlroots keeps to itself): the scene would hand one not applied yet to the
drawing's state, which goes unused, and draw it into the picture on an output without gamma
hardware. The picture is in the scene's colours, given only the encoding wlroots uses anyway (or
an HDR output's image description), which has the scene put its colour transforms together again: laid back in the scene as a
`wlr_scene_buffer` in a tree just under the drag icons, and so under the lock, turned as its output
is (`transform`) and filling the output's box, it gets night light and the magnifier from the
output's frames as everything else does, and a mirror copies it with them. An HDR output's
picture is drawn in its image description, which the buffer is tagged with
(`wlr_scene_buffer_set_transfer_function`, `set_primaries`), so that the scene converts it back
into itself. Its opacity stays below 1 (0.9999): the scene takes an opaque buffer at 1 to hide
what is under it, whose surfaces then get no frame callbacks, and the shell could not draw under
it. The pictures take no input.

While the shell is subscribed (`shell_listening`) the pictures are held until it sends
`profile_drawn NAME` for the new profile, or for a second (`HOLD_MS`); otherwise they are let go
at once. Then `crossfade_tick`, which `output_frame` calls before each frame, fades them out from
the next frame over 400 ms divided by `animations.speed`, easing in and out, and takes them away at
the end. Held, nothing changes and no frame is drawn. The tick also takes away a picture whose
output moved, turned, or changed size or scale since, a picture's listener on its output's
commits one whose output turns off (it draws no frames to fade it in, and would show it stale once
on again), from an idle callback once that commit is over, `crossfade_forget_output` one whose
output goes, and `crossfade_end` all of them as the session locks; the last to go ends the
crossfade. A switch that cannot crossfade (without animations or a profile, or while locked)
ends one under way, the new look showing at once. A switch while they fade takes them
again from the scene, which shows the old ones as they are; one while they are held keeps them and
waits for the newer profile. `get crossfade` lists them.

The shell's side is `Restyle` (`shell/restyle.cpp`). `ShellController::reload` notes a change of
profile and sets `restyling` before it tells the QML of the new configuration, which zeroes
`Theme.motionScale`, so that the new look shows at once without transitions of its own. Then, after
any reload with a profile, `Restyle` follows every `QQuickWindow` shown (asking each to draw)
until, as `RestyleWait` decides (tested by `restyle_test`), each has swapped a frame since, and
again since an image in it finished loading (a wallpaper decodes off the GUI thread), none is
loading, and none has drawn for 50 ms, or 150 ms have passed since that held, as a bar the
compositor configures anew draws twice; 900 ms at most. `restyled` then clears `restyling` and
sends `profile_drawn` through `ask`, which says nothing when the compositor is not there (and nothing without a profile). It says
so after a reload that kept the profile too: switched there and back before the shell read the
file, the compositor waits for the profile the shell has had all along.

The desktop (`Desktop.qml`) draws the wallpaper, or the gradient (the macOS style's drawn
wallpaper) without one, in layers under `backdrop`: each change lays a new one on top, at no
opacity until its picture has loaded or failed, which then fades in over 450 ms (at once for the
first) and takes away the layers under it once it covers them; a reload that keeps the wallpaper
keeps its layer, reading and decoding nothing again. A change during a fade lays another layer over both, so that nothing
jumps. A layer whose picture failed shows the gradient, and lays a new one for it after a second,
then twice as long each time up to a minute, while it is the one wanted.

### A rice on trial

A rice (`src/rice.cpp`, `include/paw/rice.hpp`, tested by `rice_tests`) is a directory with
`rice.lua` and the files it names. `parse_rice_data` reads `rice.lua` with a parser of Lua's
table syntax of its own, so nothing in it runs, and `parse_rice` checks its shape: the settings
`rice_setting_allowed` lets through, and paths `rice_path_valid` takes, which `read_rice` finds
inside the directory as plain files, not through a link. `installed_rices` reads
`$XDG_STATE_HOME/paw/rices/*`, and `load_config` hands them to `parse_config`, which builds each
as a profile named after its directory (`rice_profile` makes its pictures' paths absolute) before
the configuration's own; one the configuration refuses, or whose name it has, goes to
`Config::rice_problems` instead, which `main.cpp` logs. `configured_look` gives the look settings
a profile has over the configuration, its theme and the defaults, for `paw rice pack`.

`paw rice try` (`src/cli/rice.cpp`; the archive in `src/cli/rice_archive.cpp`) asks `get
rice_trial` first, so that nothing changes outside a session, reads the rice into memory
(`rice_archive_files` takes only plain files inside the archive; `gzip` unpacks a compressed one
into memory), writes it into a hidden directory beside the installed rices (`stage_rice`, only the
files `rice.lua` names), checks that the configuration takes it with `parse_config`, and then
`begin_rice_trial` writes the trial down in `$XDG_STATE_HOME/paw/rice-trial` (the rice, the saved
profile before it, whether an installed rice of its name waits aside as `.NAME.previous`) before
it moves the rice into place and saves its name as the profile. Then it sends `rice try NAME`.
`control_rice` (`rice.c`) has `sh_callbacks.rice` note the compositor's pid in the record, reloads
the configuration, which crossfades to the profile, takes it back at once unless the profile in
use is the rice's, and otherwise sets a timer for `RICE_TRIAL_MS` and sends the shell `rice-trial
MILLISECONDS NAME`. `rice keep` drops the rice set aside and the record (`keep_rice_trial`); `rice
revert` and the timer write the previous profile back, remove the rice and put the one set aside
in its place (`revert_rice_trial`), then reload. The compositor ending during a trial reverts it as
it goes (`rice_finish`), and `main` reverts a record left by one that died
(`recover_rice_trial`) before it loads the configuration, unless the pid in it is a running
`paw`, a session a nested compositor runs in.

The shell's side is `RiceTrial` (`shell/rice_trial.cpp`), which reads the rice's author and
description from its `rice.lua` and counts the seconds down, and `RiceTrialView`, an overlay in the
middle of the output overlays belong on, holding the keyboard on Revert.

### The day and night profile schedule

`profile_schedule.c` keeps the time of the last sunrise or sunset it applied, and the profile that
one brought, in `$XDG_STATE_HOME/paw/profile-schedule`. `check` finds, on night light's clock
(`night_clock`, which `PAW_NIGHT_LIGHT_TIME` fixes for tests), the last sunrise or sunset of the
last three days (`sh_last_boundary` in `src/profile_schedule.c`, tested by `profile_schedule`, over
the schedule's own times, night light's, or `sh_solar_times`' for each day), and applies it when it
is later than the one applied, brings another profile (the schedule changed), or the one applied
lies ahead (the clock went back). A profile picked by hand changes neither, so it stays until the
next. Applying picks the profile as `paw msg profile NAME` does: `sh_callbacks.command` saves it
and answers `SH_RELOAD`, and `reload_config` crossfades. `sh_run` calls `profile_schedule_start`
before anything is set up from the configuration, where a pick only reads the configuration again
(`sh_callbacks.reload`), which starts no shell before `startup` has (`Runtime::startup_done`). Each
reload sets the timer for a millisecond (`profile_schedule_reloaded`), so a schedule just written
or changed applies at once without a reload inside another; otherwise it looks every minute, and
never with a fixed clock. Nothing is applied while a rice is on trial. `profile_auto` (also
`profile auto`, which `pick_profile` turns into it unless a profile has that name) applies the
current one at once. Subscribers hear `profile-schedule on` while the profile in use is the one the
schedule wants, which the shell's Appearance tile and profile menus show.

### Power saving when idle

`idle.c` takes the `idle` steps (`enum sh_idle_step`: dim, display_off, lock, suspend) with one
timer, `sh_server.idle`. `input_activity` calls `idle_activity` on every input event, which only
notes the time while no step has been taken; the timer, set for the next step's time on either
power source's table, finds when it fires whether input came meanwhile and sets itself again.
Each step taken sets its bit in `done` and is not taken again until input clears them, which
also fades the dimming out and turns on the outputs the display_off step turned off (`idle_off`
on `sh_output`, cleared whenever an output is turned on another way). The dimming is a stretched
`server->black` buffer (`sh_dim_create`) in a tree of its own created last at the scene's root,
over the lock too, faded by `tick_idle` from `output_frame`. `idle_held` (an idle inhibitor
that applies, or the session not active) disarms the timer, and `idle_hold_changed` (called from
`lock.c` as inhibitors start and stop applying and the VT changes) restarts the count once
released. `lock.c` keeps every inhibitor in `sh_server.idle_inhibitors`; `idle_inhibitors_follow`
works out which apply (a window's while it is shown, `toplevel_visible` on a monitor that is on
and the session unlocked; a layer surface's while mapped; any other surface's always) and sets
the idle notifier inhibited while any does. It runs from `notify_subscribers`, which every
change of windows and workspaces reaches, and from the few places that show or hide a window
without it: `minimize_toplevel`, restoring in `focus_toplevel_raise`, `set_output_power` and
layer surfaces mapping and unmapping. A new way of hiding a window that does not end in
`notify_subscribers` calls it too. Keep awake (`sh_server.keep_awake`, `set_keep_awake` in
`lock.c` from the keep_awake actions) holds the steps as an inhibitor that always applies would:
`idle_held` counts it, and the idle notifier stays inhibited while it is on. `idle_steps` reads
`src/power_supply.c`'s answer, from `$PAW_SYSFS` in the tests; power.c's waking up after
sleep calls `idle_activity` as input would.

### The lid

`switches.c` follows libinput's switches (`WLR_INPUT_DEVICE_SWITCH`), one `sh_switch_device`
each, keeping what each last said of the lid and of tablet mode from its toggle events, and
logind's `LidClosed` (`sh_login1_ask_lid`, which power.c asks as it connects, and its
PropertiesChanged): libinput tells of a lid already closed as its device appears only when a quirk
says the switch is reliable, and then never of its opening either. `sh_server.lid_closed` is any of
them saying the lid is closed. `configure_output` asks
`lid_holds_off` of every output, which applies `src/lid.c`'s decision (a built-in connector, the
lid closed, clamshell mode, a monitor that is not built in in the layout), and when the lid holds
an output off it goes out of the layout as with `enabled = false`, but its windows keep their
workspaces as when it is unplugged. `apply_lid` configures the built-in panels again, and arranges
the outputs and windows when one changed, as the lid changes and after an output is added or
destroyed (before an empty layout would end a nested or headless session). Each toggle counts
as input and runs the binding `sh_callbacks.switch_toggled` returns. Under `--headless`,
`headless_switch` adds switches for the tests (`headless_input.c`, `lid_smoke`).

### Toys

`toys.c` plays `shake`, `gravity` and `party` (`toys_start`, from the actions; `toys_shake` shakes
a given window, for other code to point at one). One toy plays at a time, `server->toys`, its
state private to the file; a new one ends the last (`toys_end`). Its arithmetic is
`src/toys.c` (`paw/toys.h`, tested by `toys_tests`): the shake's damped swing, a fall's bounces and
squash (`sh_fall_plan`), where windows pile up (`sh_pile`, the lowest landing first, on the floor
or on the windows landed below it, sinking a share of the usable height past the floor when they
rest on it already, so that tiles fall too), a party's bob and rainbow, and confetti stepped
through gravity and air (`sh_confetti_step`).

A toy draws a window through the animator's pose (`sh_anim_pose`, `src/animation.c`): `content`
moved by an offset from where it rests and its nodes stretched about a point, as the toy says on
each call, through the same records the other animations use. A pose runs alone: it is refused
while anything else runs on the window (a window opening or gliding joins the toy once that is
over), and any animation starting on it (`start`, a hold, a lift, `sh_anim_finish`) puts it back at
rest first, telling its owner through `sh_anim_posed`. The clock, a late frame and turning
animations off leave it alone, so that toys play with animations off. Lookups put posed windows at
rest as they do running ones (`rest_poses`), so input goes where windows really are, and each tick
draws the pose again over what clients and `refresh_frame` set. A stretch calls the pose's update,
`refresh_frame`, before the nodes are stretched, and `sh_anim_lift_box` maps a box through it as
it does through a lift, so that the rounded clips (`lift_box`) follow a squash; `morph_commit`
calls `sh_anim_pose_commit`, which puts the client's surfaces back at rest before the scene takes in
a commit, which gives a buffer its new size where the stretch had put it.

`toys_tick` moves the toy on from `output_frame`, before the animator, and from a timer of its own
every 16 ms while one plays. Each window plays while it is as it was when the toy began
(`still_plays`: shown, mapped, not fullscreen nor grabbed, at the same place and size, its monitor
showing the same workspace, and still posed once posed); otherwise `let_go` unposes it and draws
its frame again. The toy ends at its time, when no window is left (but a party, whose confetti
plays on), when its monitor shows another workspace or goes, as the overview opens, and as the
session locks (`server_new_lock`). `unmap_toplevel` calls `toys_forget` after the closing copy is
made, so that a window closes where the toy drew it. A party colours the border through
`toys_border`, which `refresh_frame` asks for after the focus fade: a colour in the border's rects
in place of a gradient's pieces, which come back as the party lets the window go. Its confetti is
a `wlr_scene_rect` per piece in a tree just above `fullscreen_cover`, which `scene_node_at` hides
from its lookups (`toys_confetti`). A button pressed while gravity holds windows down
(`toys_press`, from `server_cursor_button`) sends them back up from where they are drawn. `get
toys` (`describe_toys`) says what plays, and `tests/toys_smoke.py` tests it; `paw shake`, `paw
gravity` and `paw party` are `src/cli/toys.cpp`.
### The scrolling layout's cube

With `layout.scroll.cube`, a move of a workspace's view along its strip of columns draws the tiles
on the sides of a square that turns a quarter, rather than gliding them over. Its arithmetic is
`src/cube.c` (`paw/cube.h`, tested by `cube_tests`): `struct sh_cube` is the square, each side as
wide as the view (the tiling area less its outer gaps), turned by an angle, its sides along the
strip from a start; `sh_cube_move` gives the square a turn starts from as the view moves (a
quarter from rest, the side facing the eye showing the view as it was, the strip sliding along the
sides by what the move and a side's width differ), `sh_cube_at` the square part of the way to
rest, and `sh_cube_span` where a span of the strip is drawn: seen from in front with the square
drawn as wide as the view, straight across the sides it lies on (a chord across a corner), its
height smaller further from the eye and fainter turned away, nowhere on sides turned away. A move
under way moved again starts from the square as it is drawn, turned by quarters, which draw the
same, so that it turns the way the view moves now.

`cube.c` hooks into `reflow_output` (`placement.c`): `cube_before` notes the workspace's tiles
and its view (`sh_tiling_scroll_view`) before the arrangement and whether each is free to turn
(drawn by no animation but a turn), and `cube_after` turns them when the view moved and nothing
else happened: every tile the layout holds was there before (`sh_tiling_count`; a window that
opens has its own way in), free, at the same size and height, the workspace shown and nothing
covering it (lock, overview, a toy). It finishes the glides `place_tiled` started, as a toy's pose
needs nothing else running, and keeps a `struct cube_tile` per tile: where it rests and where its
left edge lies along the strip as the turn starts and at its end, in the view it turns to, so that
columns that also move along the strip go there over the turn. Anything else that the arrangement
does ends a turn under way, and columns moving without the view glide as any tile does.

The tiles are posed as a toy poses windows (`sh_anim_pose`): moved and stretched across the span
`sh_cube_span` gives their frame, their height scaled about the view's middle, and faded with the
pose's `fade`, which multiplies their nodes' opacity. `cube_tick`, from `output_frame` before the
animator and from a timer of its own every 16 ms while the turn goes on, draws them again; a tile
on sides turned away is drawn faded out, past the view's edges, its content drawn meanwhile as a
gliding tile's is (so that lookups, which put it at rest, find it), and `window_cut` makes the cut
at its monitor's edges (`scratchpad.c`, whose `crossing` maps the frame through `lift_box`) follow where it is
drawn. A tile is let go, back at rest, `bound_tile` hiding it again past its monitor's edge, when
it no longer rests where it was placed, is taken over by another animation, grabbed, made
fullscreen or leaves; the turn ends at its time, when no tile is left, when its monitor shows
another workspace or goes, as the overview opens, the session locks or a toy starts, and when the
setting goes off. `unmap_toplevel` calls `cube_forget` after the closing copy is made, so that a
window closes where the cube drew it. Lookups find posed windows at rest, so input goes where
the tiles rest and `get windows` has them there at once. Its duration and curve are
`animations.cube`'s (`SH_ANIM_CUBE`), two and a half times `animations.duration` unless set. `get
cube` (`describe_cube`) lists the turn and each tile's span, scale and fade, and
`tests/scroll_cube_smoke.py` tests it.

### The desktop cat

What the cat does is `src/cat.c`'s, without wlroots: a state machine (`enum sh_cat_state`) with
where the cat's feet are and how it moves, which `sh_cat_step` advances to a time in steps of at
most 16 ms, returning when it next needs a step, given a `struct sh_cat_world`:
the monitors (`sh_cat_area`, with the top and bottom of their free areas), the window tops it can
stand on and the floors, the focused monitor and window, the urgent window, and whether nobody has
used the computer for a while. `sh_cat_ledges` makes the tops from the windows' boxes, front to
back: each window's whole top edge, held down inside its monitor's free area by the cat's height
(so that it stands in a maximized window's title bar rather than off the screen), which holds the
cat up while it is there, and the parts of it no window in front of it covers just above it, the
only ones it lands on or goes to. Its idle choices follow a seeded random number, so that
`tests/cat_tests.c` can run it through a world of its own by the millisecond. `sh_cat_pointer`
tells a flick from a stroke: the pointer turning back along either axis over the cat
(`sh_cat_under`) three times within 1.5 s, short of a flick's travel, has it purr (`SH_CAT_PURR`,
`dozing` when it slept or yawned its way there) until two seconds after the last motion over it,
and eight seconds of it awake a swat (`SH_CAT_SWAT`) and a walk off; the pose lists the hearts
that rise from a purring cat, in cells, with how opaque each is. `src/cat_sprite.c` holds the
pictures, grids of letters a coat colours, and the heart's.

`cat.c` in the compositor shows the cat that world and draws it. Its tree sits just above
`above_windows`, under the fullscreen windows, the panels, the overlays and the lock, and holds
scene buffers for the cat, its "z" and three hearts (faded by their opacity), which take no input
(`point_accepts_input`) and are scaled up from the art's pixels without smoothing
(`WLR_SCALE_FILTER_NEAREST`); a picture is painted once per frame and coat and kept. A window's box is where it is drawn (its tree's place and `content`'s
offset, at `frame_geometry`'s size), with its border. The cat is stepped on a timer of its own:
every frame while it falls, jumps or runs, each 25 ms while it walks, each 40 ms while it purrs, and while it sits only for
its next blink or choice, a few times a minute, or as it gets sleepy; asleep, once a breath. What
it stands on moving is seen from the frames: whatever moves a window damages the outputs, and
`output_frame` calls `cat_frame`, which, unless the cat is stepped every frame anyway, compares why
it is hidden, the urgent window and the box of what it stands on with how they were at its last
step, and steps it at once if any changed. `input_activity` stirs a sleeping cat (`cat_input`),
which wakes `STIR_MS` later unless `process_cursor_motion`, which tells it of the pointer
(`cat_pointer_moved`) just after for a motion, finds the pointer on it meanwhile: those motions
are strokes. While it is asleep the world it is shown counts as idle, so that only input it hears
wakes it, not the strokes. It chases the pointer only after a flick close by: several motions in a
row travelling far within a short while. While it is hidden
(`hidden_why`: turned off, the session locked or another VT in front, no monitor, the overview,
its monitor off, a fullscreen window on its monitor or the focused one) its tree is disabled and
no timer runs; the next frame that shows things changed steps it again. `get cat` describes it.

### Ambient particles

What the particles do and look like is `src/ambient.c`'s (`paw/ambient.h`, tested by
`ambient_tests`), without wlroots: a `struct sh_ambient_field` is a monitor's area, its kind, the
time gone (which a gentle wind follows, `sh_ambient_wind`) and a seed; `sh_ambient_spawn` puts a
particle anywhere in it (as they first show, fading in) or above its top, and `sh_ambient_step`
moves them all on. Petals and flakes fall at their own pace, nearer ones (`depth`) larger and
faster, drift with the wind and sway, and come in again at the top or the other side; petals turn
and tumble, and fireflies wander, kept off the edges, and glow in cycles of their own.
`sh_ambient_look` says how one is drawn: its picture, turned by quarter turns, its box (a
petal's tumble narrows it, its other face showing past edge on, which is the petal mirrored) and
its opacity. `sh_ambient_paint` paints the pictures in premultiplied ARGB with smooth edges from
signed distances: petals in four pinks at three lengths, each turned in SH_AMBIENT_TURNS steps
of a quarter turn (the buffer transform turns them the other quarters), dots of snow at six
sizes, and a firefly's glow, brighter than its alpha so that it adds light.

`ambient.c` in the compositor keeps a `struct sh_ambient_sky` for each output in the layout
(`sh_output.ambient`; none for a mirroring one) in a tree just above the background layer
(`layer_trees[0]`) and under the bottom one, a scene buffer for each particle that takes no
input. So the blur behind windows, which draws the background layer alone, leaves them out. The
pictures are painted for the output's scale as each is first needed, and again as the scale
changes; a node is given a new buffer only when its picture changes. A timer of its own steps
every sky every 33 ms while any shows (`tick`), changing its nodes with its tree off and showing
it again once, as a party's confetti does. An output pauses, its tree off and nothing stepped
(`paused_why`), while the session is locked or on another VT, while it is off or out of the
layout, while it shows a fullscreen window and while the screens are dimmed; with every output
paused the timer stops, and `ambient_frame`, from `output_frame`, starts it again once the frame
something drew as it went shows the output may show them, and `ambient_input` once input comes
after they rested. They fade out (`presence`) toward none, toward another kind, which is put in
once they are gone and fades in, and after five minutes without input (`PAW_TEST_AMBIENT_IDLE_MS`
for tests). The kind is `appearance.ambient.kind` until the `ambient` action picks another
(`ambient_switch`), which stands until a reload changes the kind configured. `get ambient`
(`describe_ambient`) lists each output's kind, count and state, and with an output's name its
particles as drawn; `tests/ambient_smoke.py` tests them.
### Counting for paw wrapped

`wrapped.c` keeps `sh_server.wrapped` while `wrapped.enabled` is on in a standalone session
(`sh_server.session`; NULL otherwise, which every hook checks first), and the hooks elsewhere are a call each: `wrapped_count` adds one to a counter
(`enum sh_wrapped_counter`, `include/paw/wrapped_day.h`), `wrapped_motion`, `wrapped_button` and
`wrapped_axis` take the cursor's events, `wrapped_grab_done` (from `finish_grab`) tells a move or
resize from a press let go in place, and nothing is allocated per event. Time is counted lazily, with
no timer of its own: `settle` counts the time since it last did by what held since, input on
every input event (`wrapped_input`, from `input_activity`), and `wrapped_follow` settles before
taking in what changed: the focus, the lock, an idle inhibitor or the VT (`idle_hold_changed`),
monitors turned off or on. `sh_wrapped_active` says how much of a span was active: up to two
minutes after the last input, all of it while an inhibitor holds the session, none of it while
away. The active part goes to the application that had the focus last and to its streak, which
idle time, or active time with nothing focused, ends; the focus leaving a window and coming back
at once, as a click on it does, changes neither.

What was counted is added to the day's file by `sh_wrapped_save` (`src/wrapped_day.c`, in
`paw_cli`, which reads the files the same way): the file as it was and what is new go to
`DATE.new`, which replaces it. Whole units go to the file; what is less (pixels, milliseconds)
stays for the next save. A timer saves every five minutes when something was counted, and
`wrapped_save` does at once as an idle step is taken, the session locks, logind says the machine
is about to sleep, and `wrapped_finish` as the compositor ends. `settle` saves the old day first
once the wall clock passes midnight, the time since it last counted split at midnight. `get
wrapped` (`describe_wrapped`) lists what is not saved yet, the time up to then included, without
changing anything, and `paw wrapped` (`src/cli/wrapped.cpp`) reads it between two looks at the
number of saves, so that a save meanwhile counts nothing twice.
### The exploded view

`xray.c` takes the focused output apart into the layers paw draws (`paw xray`, the `xray` action).
`xray_collect` walks the scene's trees bottom to top: the output's background colour, the layer
shell's background and bottom layers, `windows`, `floating_windows`, `above_windows`, `fullscreen`,
`peek_layer`, the top layer, `fullscreen_cover`, `unmanaged` and the overlay layer, keeping each
layer surface on that output and each window whose box reaches into it, shown. `xray_plan` puts
them on sheets (`struct sh_xray_sheet`): one per layer with anything on it, and one per window
while six or fewer show. A sheet is known by its layer and its window, a pointer it only
compares, never follows, so a window gone meanwhile leaves nothing dangling; the sheets are planned
again whenever that changes, and kept as they are while the view closes, so that a window raised by
a click keeps its sheet on the way back. Where each sheet goes is `src/xray_layout.c`'s
(`sh_xray_layout`, tested by `xray_layout_tests`): the output scaled down to fit the area left
beside the shell's legend and above its caption (`sh_xray_area`), each sheet a gap further along
the way the stack rises, foreshortened that way and a little larger than the one under it, and
part of the way to there from the whole output as the view opens and closes. The pointer's travel
since it opened turns the stack (`sh_xray_tilt`), away from the pointer so that the low sheets'
edges come out under it, the wheel spreads it (`sh_xray_spread`), and the hovered sheet is lifted
along the way it rises while those over it fade, each easing with `sh_xray_ease` from the view's
own 16 ms timer. The pointer is tested against where the sheets rest fanned out, not where a lift
or the opening draws them, so that nothing under it moves away from it.

The view is a tree of its own placed just over `overview_layer`, under the overlay layer, which
holds the shell's text, and is destroyed as the view closes: the copies lock the clients' buffers.
A sheet's tree holds the shadow's slices (the windows' shadow images, `shadow_image` in
`frame.c`), a card (the background colour for the bottom sheet, else dark glass in its layer's
colour with a sheen, a one-pixel-wide gradient the scene stretches), the copy and an outline. The
copy is `sh_thumb_clone_cut` (`src/overview_scene.c`) of each surface's or window's tree, cut to
the output and scaled across and down apart, made again only when the trees' fingerprints, the
scale or the opacity change. Nothing of the view is quite opaque (0.9999 at most, as the
crossfade's pictures): the scene would hide what lies under an opaque node, and its surfaces,
getting no frame callbacks, would stop drawing what the copies show. `output_frame` and
`notify_subscribers` call `xray_touch`, which arms the timer, so that the copies follow the
clients and the sheets follow windows coming and going; while nothing changes nothing is drawn,
and while the view is closed `xray_touch` returns at once. The view takes the keyboard in
`handle_keybinding` (its own binding still closes it) and the pointer before the overview does in
`cursor.c`, and touch and tablet input as the pointer; a click on a window's sheet maps the
pointer back onto the output to find the window there. `xray_dismiss` closes it at once as the
session locks or its output goes, and it does not open while locked.

It tells subscribers what to name the sheets: `xray OUTPUT COUNT HOVERED X Y WIDTH HEIGHT LEGEND`
(the area and the legend's column) and a `xray-sheet LAYER\tCOLOUR\tNAME\tDETAIL` line per
sheet, again whenever they change; `xray-hover N` as the hovered sheet changes and `xray-close`.
The shell draws them with `Xray.qml` in an overlay surface whose namespace, `paw-xray`, no sheet
copies. `get xray` lists every output's stack with the sheet showing each part, which
`paw xray --list` (`src/cli/xray.cpp`) prints top first, and `tests/xray_smoke.py` tests it.

### The blur behind windows

`blur.c` keeps a picture of each output's background blurred (`sh_output.blur`), taken only as
the background changes, and shows the part of it each window covers under the window, as
Hyprland's blur with `xray = true` does: the windows behind are never blurred, and nothing is
blurred per frame. `take_picture` draws the buffers of the background layer (`layer_trees[0]`,
the shell's wallpaper) over `appearance.background` into a buffer of half the output's pixels in
the format of its frames, on the renderer, each texture bilinearly (an exact two by two average
of a wallpaper at the output's size), in the layout's orientation whatever the output's
transform. Then it halves that on the renderer as often as the gaussian still spans 1.5 cells
(`sh_blur_halvings`; each step two by two averages again), reads the small grid back as XRGB8888
(`wlr_texture_read_pixels`), and `sh_blur_grid` (`src/blur.c`, tested by `blur_tests`) blurs it
with three box blurs each way (clamped at the edges), saturates and brightens it, and darkens it
by half the grain to come. The grid is drawn back over the first buffer, stretched bilinearly,
and a tile of grain (`sh_blur_grain`: grey with no alpha, which premultiplied blending adds, the
same at each place every time so that a new picture does not flicker) laid over it again and
again. So the CPU touches a few thousand cells, and every window's scene buffer shares one
texture (wlroots imports a dmabuf once; a pixel buffer would be uploaded for each window). A
2560 by 1440 output took 2.5 to 11 ms on the GPU renderer here, most of it waiting for the GPU
to read back, and 7 to 19 on pixman, which draws the steps on the CPU.

A picture is due as a background layer surface on that output maps, unmaps, goes, or commits a
buffer or a new place (`blur_background_changed`, from `layer_shell.c`), as the output's box,
size, scale or transform differ from what it was taken at (`blur_frame`), and at once as a reload
changes the look (`blur_reload`: the settings and the background colour, compared). The timer
takes it once the background has not changed for `SETTLE_MS` (150), but at least every `MOST_MS`
(500) while it keeps changing, so a wallpaper fading in is blurred a few times rather than on
every frame. A crossfade to a new profile takes the pictures due at once as it releases its
pictures (`blur_flush`), so the new wallpaper's blur is in place under them.

Each window with a blur (`refresh_blur`, from `refresh_frame`: windows.blur or a rule's `blur`,
matched again only as the title, app ID or configuration change; never fullscreen) has two
buffers in `content`, under `surfaces` and so inside its rounded clip: `blur_probe`, with no
picture, which stands at the frame's box (`frame_geometry`, the size drawn while it changes size)
for the animations to place and size as they do everything else, and `blur`, which the animations
fade but leave where it is (`sh_anim.placed_apart`, `animation.c`). Before each frame, after the
animations moved the windows, `blur_frame` reads where the probe is drawn in the layout, takes
the output under its middle (or the nearest one) and puts `blur` over the part of the probe on
that output, showing the part of the picture under it (`sh_blur_source`): so the blur stays put
on the wallpaper while a window glides, slides with a workspace, scales open or closed, is
lifted or morphs, and a window hanging off its output or across two shows the blur only where
that output is. Setting what did not change changes nothing in the scene. `set_buffer_opacity`
passes the blur over, so the window's opacity leaves it whole; it is drawn at 0.9999 times what a
peek leaves of the window, since a buffer the scene takes for opaque would hide the desktop's
surface, which would then get no frame callbacks and could not draw a new wallpaper. Copies of a
window (the overview's thumbnails, the closing and minimizing ones, a swipe's held copies) take
the blur along as it was, and the probe, which has no buffer, not at all. `get blur` lists each
output's picture and each window's part of it, and `tests/blur_smoke.py` tests them, reading the
blur off the screen with grim where it is installed.

## The shell (`shell/`)

`ShellController` (`controller.cpp`) loads the configuration, keeps the compositor's state from
its control socket and holds the models; QML reaches it as the context property `shell`. It
starts applications: `shell.launch(id)`, and `shell.appActions(id)` with
`shell.launchAction(id, action)` for the actions a desktop entry offers besides starting it
(`[Desktop Action …]`, listed as `{action, name, icon}`), each failure shown across the panel.
`view.cpp` makes one Qt Quick window per surface and output (a layer surface each), all in one
QML engine. The QML is compiled into the binary (`qt_add_qml_module` in `shell/CMakeLists.txt`,
which lists every file).

The taskbar's popups are drawn in a surface of their own, a `PopoverWindow` (`view.cpp`) that
`Panel.qml` declares, so they stay in the panel's QML tree and state while the bar's surface
keeps its size. It is an overlay layer surface covering the output (exclusive zone -1, so its
coordinates are the output's), above fullscreen windows too, and hidden while nothing is open.
While a menu or popup is open, or the bar has the keyboard (below), it holds the keyboard and
takes every press but those on the bar's strip, where `inputRects` leaves a hole: a press on
another bar button still switches popups in one press, and a press beside the popups closes them.
The windows of a button shown on hover (the card of their pictures, or a stack's list) take only
the pointer over the card and down to the bar (`hoverArea`), where a drag reaches them too, and
leave the keyboard where it is. Losing the keyboard while it holds it (`dismissed`) closes the
popups. Without layer shell (`--preview-popup`, `shell_ui_test`) it is an ordinary window as large
as `ShellView::previewSize()`, and a preview's screenshot draws it over the bar.

`Panel.qml` loads the bars of `shell.style`. The taskbar (`Taskbar.qml`) fills the panel's surface.
The macOS style has two: the dock (`Dock.qml`) in the panel's surface, which is then at the bottom
whatever `shell.panel_position` says and reaches above the strip it reserves by half its height,
room for an icon to bounce in; and the menu bar (`TopMenuBar.qml`) in a `MenuBarWindow`
(`view.cpp`) that `Panel.qml` declares as it declares its popover: a layer surface on the top
layer along the output's top edge, reserving its strip, shown only in that style, in the panel's
QML tree and state. The panel's surface then takes the pointer only over the dock (`ShellView`'s
`inputRects`), so the desktop beside it and under that room stays reachable, and the popover leaves
holes for both bars (`popoverInput`), but takes every press while Launchpad covers them. A popup
of the bar with the widgets opens by `panel.barAnchor`, below the menu bar in the macOS style;
one of the dock (an application's menu, the windows of one) by `panel.dockAnchor` and
`panel.dockSide`, which on the taskbar are the bar's. A change of style makes and drops the
bars, and the surfaces follow at once.

Each monitor's bar has its own edge (`shell.panel_positions` over `panel_position`), which its
`ShellView` says as `panelEdge`, with `panelVertical`, `panelExtent` and `panelHeadroom`; the panel
reads them as `panel.edge` and the rest. Along a side (`panel.vertical`), the panel's surface runs
down the output and the taskbar stands upright: its `GridLayout` is a column, and the widgets stack
what they show by `panel.vertical`, a button `panel.barItemWidth` by `barItemHeight`. A popup's
place along the bar is then a y: `panel.alongBar(item, offset)` and `panel.lengthAlong(item)` say
where something on the bar is along it, which `barAnchor` and `dockAnchor` turn into the rectangle a
popup opens beside, on `popupSide`. A drag of the bar to another edge (`shell.panel_draggable`) asks
the compositor for the pointer (`panel_drag OUTPUT`, `src/compositor/panel_drag.c`), as a window's
move takes it: a surface the pointer went on to would never hear the button let go. The
`panel-drag` lines it sends move the outline the dragged bar's popover shows (every panel's while
every bar moves), taking no input, and `panel-dropped` has `ShellController::movePanel` write the
monitor's `shell.panel_positions` entry, or `shell.panel_position`, with `paw::save_setting`
(`src/config_edit.cpp`) and load the file again.

A press on the bar's inner edge (the `barResizeEdge` strip in `Taskbar.qml`, under the same
setting) resizes it the same way: `panel.startBarResize` works out how far the press is from the
output's edge the bar is along and asks `panel_drag resize EDGE`, and the compositor's
`panel-resize OUTPUT DISTANCE` lines, the pointer's distance from that edge of the output the
press was on, set `ShellController::panelHeight` to the height at the press plus the distance
travelled since, within the schema's range for `shell.panel_height`, in place of the
configuration's until the drag ends (`barResizing`). `panelHeight`, `panelExtent` and
`panelHeadroom` notify with `panelSizeChanged`, which every configuration loaded brings too, and
each `ShellView` lays out its surface again on it, so every bar and the exclusive zone it reserves
follow the drag, and the compositor makes the windows room as it goes. `panel-resized` has
`resizePanel` write `shell.panel_height` and load the file while the bar is still drawn at that
height, and `panel-drag-cancelled` (Escape, through `cancelBarDrag`) gives the configuration's back.
Qt never hears the button let go, so a MouseArea pressed when the compositor took the pointer
stays pressed and would take no new press: the strip is a `Loader`'s, made anew once the drag is
over.

While the panel's output tiles, with `shell.tiling_bar` on, `Panel.qml` shows the tiling bar
(`TilingBar.qml`) instead (`tilingBarShown`). The style's bars stay meanwhile, hidden and null
through `taskbar`, `dock` and `menuBar`, so that bars switch at once as tiling goes on and off,
and `macos` reads false, so that the popups are placed as on the taskbar while drawn in the
style's own way. The panel sets its `ShellView`'s `tilingBar`, which lays the surface along the
tiling bar's edge (`ShellView::edge`) and as tall as the strip it reserves
(`ShellController::tilingBarExtent`), without headroom. A module is a `Module`, a button in the
bar's own colours (`shell.tilingBar`, which `ShellController::tilingBar` resolves against the
shell's own), made by name in its group's row (`componentFor`) and found by the panel through
`module(name)` for the popups it opens: Quick Settings and the clock's flyout open centred on
theirs (`flyoutAnchor`), the power menu by its own (`barPowerMenu`). The `layout` module reads
the `layout NAME LAYOUT` lines of the compositor's state (`describe_state`, `control.c`).

The window switcher, the command palette, the power dialog and the overview's text are
`OverlayView`s (`view.cpp`), a layer surface each on every output's overlay layer. `present()`
shows one and sets its QML root's `shown`; `dismiss()` clears it, and the root animates its own
`progress` back to 0 as its `Reveal` (`Reveal.qml`) says, the view hiding once it is
there (at once with animations off). While it goes it is transparent for input and gives up the
keyboard, so the windows under it have both at once, and shown again it comes back from where it
was. What the compositor forgets as one closes (the switcher's windows, the power dialog's
question, the overview's search) its root holds with a `Binding` while `shown`, so the fade shows
what was there.

| File | Covers |
| --- | --- |
| `Theme.qml` | The design tokens (colours, type, radii, spacing, icon sizes, motion, whether effects can be drawn), derived from the appearance profile. A singleton: every file reads `Theme.surface`, `Theme.hover`, ... instead of colours and sizes of its own. |
| `Panel.qml` | The panel on one output: which popup is open and where, the bars of the style (a loader for each), and the popover with a loader for each popup. Every part below takes the panel as `panel` and reaches its state and functions through it; a popup is placed in `panel.popupLayer`, beside the part of the bar it belongs to (`panel.barAnchor(x, width)`). |
| `Taskbar.qml` | The taskbar: the bar along the panel's edge with the start button, the row of pinned applications and windows, the widgets and the clock, and its smaller buttons. |
| `TilingBar.qml` | The tiling bar (`shell.tiling_bar`), in the taskbar's place (or the dock's and the menu bar's) while the output tiles: its modules in three groups along it, as one bar, an island for each group or a pill for each module, in colours and type of its own. |
| `TopMenuBar.qml`, `Dock.qml`, `DockIcon.qml` | The bars of the macOS style (`shell.style`): the menu bar along the top in a `MenuBarWindow` of its own, with the system, application and Window menus, the widgets, search, Quick Settings and the clock; and the dock in the panel's surface, an icon for each application, pinned or running, with the applications button and the Trash. |
| `BarKeyboard.qml` | The keyboard on the bar (`taskbar_focus`): held in the popover, it walks the taskbar's buttons or the dock's icons and the windows they show. |
| `BindingMode.qml` | The name of the binding mode in use, on the bar while it is not the default one. |
| `TaskList.qml`, `TaskButton.qml`, `TrayButton.qml`, `WorkspaceIndicator.qml`, `VolumeButton.qml`, `ClockButton.qml`, `BatteryWidget.qml`, `NetworkWidget.qml`, `NotificationBell.qml`, `KeyboardLayout.qml`, `QuickSettingsButton.qml`, `WallpapersButton.qml`, `ProfilesButton.qml`, `TilingButton.qml`, `BarAppIcon.qml`, `Badge.qml`, `BarTip.qml` | Parts of the bar: widgets, an application's icon on it, a count on a pill, and the tooltip for things on it. |
| `ClockFlyout.qml`, `QuickSettings.qml`, `AudioMixer.qml`, `AudioOutputs.qml`, `ProfileList.qml`, `WallpaperPicker.qml`, `Launcher.qml`, `PowerMenu.qml`, `TaskbarMenu.qml`, `TrayMenu.qml`, `TilingMenu.qml`, `GroupList.qml`, `WindowThumbnails.qml`, `WorkspaceCard.qml`, `MenuBarMenu.qml` | Popups of the bar, each made by a loader in `Panel.qml` when first needed. |
| `WorkspaceWindows.qml` | The windows of one workspace on the card of a workspace, each a small copy of its picture where it lies, in the order they are drawn. |
| `DropGhost.qml` | What a window dragged from the bar onto a workspace carries: its picture, or its icon on a card, at the pointer, dropping into the workspace as it is let go. |
| `CalendarPopup.qml`, `NotificationHistory.qml` | The clock flyout's cards: the month calendar, and the notifications grouped by application. |
| `QuickTile.qml` | A tile of Quick Settings: a toggle, a list it opens, or a state. |
| `MediaCard.qml` | Quick Settings' card of what is playing: the current media player's cover, track, position and controls. |
| `BluetoothList.qml` | The Bluetooth devices under Quick Settings' Bluetooth tile: the paired ones to connect, disconnect and forget, those in range to pair with while looking, and what BlueZ asks as one pairs. |
| `WifiList.qml`, `WifiPopup.qml` | The Wi-Fi networks in range, to connect to (with a password field where one is needed) or disconnect from, under Quick Settings' Wi-Fi tile; and the network widget's popup on the bar with them under a switch for the radio. |
| `Icon.qml`, `FadingIcon.qml`, `SpeakerIcon.qml`, `BatteryIcon.qml` | Line icons (Lucide), drawn as vectors in any colour; one that crossfades as the state it shows changes, the loudspeaker for a volume, and a battery filled to its charge. |
| `FlatButton.qml`, `ButtonFill.qml` | The frameless button of the bar and of menus, and its background, which fades between the hover, pressed and active states. |
| `PushButton.qml` | A framed button with text: raised, or filled for what a click mostly does or for a destructive action, with a ring for the keyboard; a dialog's, a notification's, the start menu's. |
| `FocusRing.qml` | The ring that says the keyboard is at something, as `PushButton`'s: around a button on the bar, a picture or a row the keyboard on the bar selects. |
| `NotchWheel.qml` | The wheel in whole notches, a mouse's or a touchpad's, for what steps by it: workspaces, pages, the volume, the calendar's months. |
| `TextButton.qml` | A button that is only its text in the accent colour, as Today and Clear all over a card's list. |
| `CloseButton.qml` | The round cross that closes a card, a notification or a window in a stack's list, or clears the search. |
| `SearchInput.qml` | The start menu's and the command palette's search field. |
| `TextEntry.qml` | A field to type into in a list or a dialog: a password, a PIN, a scale. |
| `ToggleSwitch.qml` | A switch drawn as a track and a knob: Wi-Fi's radio, do not disturb, the display settings'. |
| `EmptyState.qml` | What a list says while it has nothing to show: an icon, a line and a hint. |
| `PopupCard.qml` | A popup's card: surface, outline, corners, a shadow through the GPU, the open and close animation, and its place beside what it belongs to. |
| `CardFrame.qml` | A card's shadow, surface, outline and the macOS style's inner edge, beneath what it holds: a popup's, an overlay's, a notification's; with `shell.blur` the frosted glass under a translucent surface, and the glass's light edge and sheen. |
| `Shadow.qml` | A shadow drawn through the GPU (`RectangularShadow`) under a card or a bar, which frees the shader effect it draws with as it goes: Qt's (6.11) makes that effect without a parent and never deletes it, so the shell gives it to the shadow (`ShellController::keepShadowMaterial`). |
| `GlassFill.qml` | A rectangle in a popup's colour that is a card of its own, drawn by hand rather than by `CardFrame` (a tooltip, Notification Center's cards): on the frosted glass, translucent over a `Glass`. |
| `Reveal.qml` | How far an overlay or a card is in view: its `progress`, easing to 1 while `shown` and back to 0 after, which the overlays and `PopupCard` take as their own. |
| `PopupMenu.qml`, `MenuRow.qml` | A menu of plain entries on popup cards, with cascading submenus and keyboard navigation, and one row of it. |
| `WindowMenu.js` | The entries of a menu about windows that move them to another workspace or monitor, and what the menus share: their sections, the appearance profiles. |
| `AudioSlider.qml`, `MuteButton.qml`, `StreamRow.qml` | Controls the mixer and Quick Settings use: a volume's slider, a mute button, and an application playing sound. |
| `StartHome.qml`, `StartAllApps.qml`, `StartSearch.qml`, `StartBestMatch.qml`, `StartTile.qml`, `StartRow.qml`, `UserAvatar.qml` | Parts of the start menu (`Launcher.qml`): its pinned and recent applications, every application from A to Z, what its search finds and the best match of it, a pinned application, a row of its lists, the user's picture. |
| `Launchpad.qml` | The launcher of the macOS style, in the start menu's place: every application on pages of a grid over the whole output, with a search. |
| `Desktop.qml` | The wallpaper, each new one fading in over those before, the desktop clock and the desktop's launchers, on the background layer. |
| `DrawnWallpaper.qml` | The wallpaper the macOS style draws while none is set, light or dark, from the background colour and the accent. |
| `DesktopClock.qml` | The desktop clock (`shell.desktop_clock`, experimental): the time, the date and a greeting over the wallpaper, outside the backdrop the frosted glass takes, changing once a minute. |
| `WallpaperPicture.qml` | The wallpaper's picture as `shell.wallpaper_mode` lays it out (cropped, fitted, stretched, centred or tiled at its own size, over the background colour where it leaves room), for the desktop, a workspace's card and Launchpad. |
| `Switcher.qml`, `Overview.qml`, `Palette.qml`, `PowerDialog.qml`, `NotificationCards.qml`, `Osd.qml`, `ConfigError.qml` | One overlay surface each. |
| `DisplayMode.qml` | The display mode popup (Windows' Win+P, `shell.displayModes`), an overlay surface of its own in the middle of the output: the four choices, the one the compositor's stepping shows selected; a click takes one. |
| `DisplaySettings.qml` | The display settings window (`shell.displaySettings`), an overlay surface of its own in the middle of the output: the monitors' arrangement to drag, the selected monitor's settings, Apply, and the question while they are on trial. |
| `AuthDialog.qml` | The polkit authentication dialog (`shell.authentication`), an overlay surface of its own: the request, the user to answer as, the password. |
| `RiceTrial.qml` | The question while a rice is on trial (`shell.riceTrial`), a card in the middle of the output, an overlay surface of its own: the rice's name, author and description, the countdown, Revert and Keep. |
| `ClipboardPicker.qml`, `EmojiPicker.qml` | The clipboard history's popup and the emoji picker, each in an overlay surface of its own (`PickerView`, `picker_view.cpp`). |
| `Shortcuts.qml` | The sheet of keyboard shortcuts (`shell.shortcuts`, `shortcuts.cpp`), in a `PickerView` that covers the output: every binding in effect as key caps beside what it does, in groups laid out in columns, with a search. |
| `SwitcherCards.qml` | The switcher's windows as cards with their pictures, in rows, with `shell.thumbnails` outside the macOS style (in it too for one application's windows). |
| `Xray.qml` | The names of the exploded view's sheets (`paw xray`), an overlay surface of its own over the compositor's stack: a legend of every sheet in its layer's colour, top first, and a caption for the one under the pointer. |
| `Caption.qml` | The caption `paw tour` narrates itself with (`shell.caption`, `caption.cpp`, from the compositor's `caption` lines), an overlay surface of its own (`CaptionView`) where the on-screen display shows, taking no input. |

The authentication dialog's model is `Authentication` (`authentication.cpp`): polkit's requests,
one at a time, each answered through an `AuthConversation` for the chosen user. A conversation
that ends in a wrong answer is followed by a new one from the event loop, never from inside its
`completed`, which polkit's session may emit inside `start()` when its helper cannot start; one
that ended without asking anything is followed by none until the user answers again or chooses
another user, and the third wrong answer of a user ends the request as `Failed`. The agent
(`polkit_agent.cpp`, with `PAW_POLKIT`) is a `PolkitAgentListener` subclass registered for
the session's `unix-session` subject, whose requests it hands to the model, and whose
conversations are polkit's `PolkitAgentSession`s; it completes polkit's `GTask` as the model is
done with a request, and withdraws one when polkit cancels it (from a queued call, as completing
disconnects the cancellable's handler, which would wait for itself). libpolkit-agent-1 is
GObject's and runs on the thread-default GLib main context, which Qt's event loop on Linux is
(`QEventDispatcherGlib`); the agent checks that before it registers. The compositor marks the
shell of a login session with `PAW_POLKIT_AGENT=1`, and only that one registers, so no test
reaches the machine's polkit. `tests/fake_polkitd.c` stands in for polkitd on a private bus.

The display settings window's model is `DisplaySettings` (`display_settings.cpp`, the
controller's `displaySettings`). It reads `get monitors` through `ShellController::ask` as the
window opens and after every trial, keeps each monitor's settings as the window changes them
(with `loaded_`, what is in force, for `changed`), and sends them all as one `monitors apply`;
the compositor tests them, puts them on trial and tells every subscriber of the trial's start and
end (`monitors-trial`, `monitors-kept`, `monitors-reverted`), which `handle` follows, so the
countdown the window shows is the compositor's and a trial a script started shows too. The
arrangement's arithmetic is pure, in `display_layout.cpp` (tested by `display_layout_test`):
`snap` puts a dragged monitor beside the others, overlapping none, edges lining up within a
threshold of eight of the drawing's pixels; `settle` puts them back side by side after one
changed size, each on the side of the others it was on; `normalise` starts them at 0, 0.
`display_settings_test` drives the model against a stand-in for the compositor, and `setAsk` lets
the preview answer for one. `DisplaySettingsView` (`view.cpp`) is an `OverlayView` on every output,
holding the keyboard, shown on the output the window was opened on, or on the primary screen while
that output is gone (`screenAdded` and `screenRemoved`), as a trial may turn it off.
`tests/display_settings_shell_smoke.py` follows it with a real compositor.

The models behind them: `task_model.cpp` (windows, from foreign-toplevel), `taskbar_model.cpp`
(the taskbar's one row of pinned applications and windows, in an order every panel shares, a
`TaskbarOrder` the source model owns, which while the windows have a button each places a window
among its application's by the place of its monitor, the `output` role's `QScreen`, as it opens
or moves, tested by `task_filter_test` and in a session by `tests/taskbar_monitor_smoke.py`; a
panel's `TaskbarModel` shows the entries its `TaskbarOrder::View` lets through, the windows of
its `output` alone and the launchers with `showLaunchers`, which `Panel.qml` sets from
`shell.taskbar_buttons` and the main monitor, `ShellController::primaryOutput`, from the
compositor's `primary` state line) and `task_filter.cpp` (an application's windows, for its
groups and menus, with `byMonitor` in the order of their monitors, as the card of a button's
windows lists them, and with `output` those on one monitor alone), `audio.cpp` with
`pulse_audio.cpp`, `system_status.cpp`
(battery, network), `tray*.cpp`, `notification*.cpp`, `osd.cpp`, `caption.cpp` and `backlight.cpp`,
`volume_keys.cpp` (what the compositor passes on from the volume, microphone and brightness keys),
`power.cpp`, `palette.cpp` (with `fuzzy.cpp`, and `calculator.cpp`, `file_index.cpp` and
`web_search.cpp`, which the start menu's search shares), `media.cpp` with `mpris.cpp`,
`power_mode.cpp` with `power_profiles_daemon.cpp`, `wifi.cpp` with `network_manager.cpp`,
`bluetooth.cpp` with `bluez.cpp`, `clipboard.cpp` (the clipboard history), `desktop_clock.cpp` (the
desktop clock's settings, and whether the `desktop_clock` action shows it). `preview.cpp` has
stand-ins for all of them for `--preview-popup`.

The services Quick Settings controls over D-Bus each have a model the QML reads, built into
everything that builds the controller (`PAW_SERVICE_SOURCES`), and a backend on the bus built
into the shell alone, as `audio.cpp` has `pulse_audio.cpp`: `media.cpp` (the media players in
order, the current one, its controls and its position between reads) with `mpris.cpp` on the
session bus, and `power_mode.cpp` with `power_profiles_daemon.cpp`, `wifi.cpp` with
`network_manager.cpp` and `bluetooth.cpp` with `bluez.cpp` on the system bus. The backend fills the
model and carries out its requests, virtual `send*` functions; without Qt's D-Bus module, or
without a bus, one that never finds anything takes its place (`makeMedia`, `makePowerMode`,
`makeWifi`, `makeBluetooth`). A system service's backend watches its name and is unavailable while
nobody owns it, so a machine without the service shows nothing of it. Each reaches `Panel.qml`
through a property (`mediaSource`, `powerModeSource`, `wifiSource`, `bluetoothSource`) that the
preview and `shell_ui_test` point at stand-ins, and each backend is tested against stand-in
services (`tests/fake_dbus.hpp`) on a private bus, which the tests hand the backend as its
connection. A backend reads a dictionary, a list or a structure from the bus only once its
signature is the one it expects, through `dbus::map` and `dbus::read` (`dbus_util.hpp`): QtDBus
reads an argument of another type out of bounds, so a mismatch counts as an empty answer.

`NetworkManager` mirrors the objects it needs (the manager, the devices and their Wi-Fi
interfaces, the access points, the active connections) from GetAll, follows them through the one
PropertiesChanged match it adds for the service, reading what a list property names anew and
dropping what it no longer does, and lists the known networks from the settings
(`ListConnections`, `GetSettings`) again as they change. Strengths change often, so it publishes
the state once a burst of changes is over. A list a row can be typed in (Wi-Fi's networks, with
their password field) is a `QAbstractListModel` updated in place (`WifiNetworks`), rows changed and
moved rather than made again, as `AudioStreams` is; a list rebuilt from a `QVariantList` would
drop what was typed, and the row whose handler is running, at every change. `RowModel`
(`row_model.hpp`) does that for Wi-Fi's networks and Bluetooth's devices.

`BlueZ` mirrors BlueZ's objects from its ObjectManager (`GetManagedObjects`, then
`InterfacesAdded`, `InterfacesRemoved` and PropertiesChanged, an invalidated property dropped) and
takes the first adapter. Pairing goes through an agent of its own, a `QDBusVirtualObject` at
`/org/paw/BluetoothAgent`, registered with `AgentManager1` as the default the first time the
user looks for devices or pairs, so that test shells and sessions that never pair leave BlueZ's
agents alone. A question the agent is asked (`RequestConfirmation`, `RequestPinCode`,
`RequestPasskey`, `RequestAuthorization`) is kept unanswered, its message copied, until the user
answers in the list (`Bluetooth::ask`, then `sendAnswer`), and one still waiting is cancelled as
another comes; `DisplayPinCode` and `DisplayPasskey` are answered at once and the code shown. The media actions reach the shell as `media VERB` lines, which the controller hands
to `Media::command`; `tests/mpris_probe.cpp` is a player for `media_smoke`, which follows a key
from the compositor to a player.

A window is known by its app id alone. `app_match.cpp` finds the desktop entry it belongs to: an
`app_match::Index` of the entries, which `refreshApps` builds, tries each way of matching from the
most exact down, each against every entry before the next, and `ShellController::appFor` keeps
what it found for each app id until the entries change. `iconFor` gives the entry's icon, else
that of an entry the menus leave out (a second index), else the first of `iconGuesses` the icon
theme has. QML names a window's icon through `shell.iconFor(appId)`, never by its app id, which
may be a path. `icons.cpp` serves `image://icons/NAME` and, at startup, picks the icon theme the
desktop names when no platform theme does (`useDesktopIconTheme`). `tests/app_icons_test.cpp`
tests both with real applications' entries and app ids.

An `ImageProvider` (`image_provider.hpp`) serves the pictures of the shell's models
(`image://notify`, `tray`, `windows`, `clipboard`) on the GUI thread alone, since the models have no
lock: an `Image` with `asynchronous: true` asks on Qt Quick's loading thread and gets nothing, so
one showing such a picture loads synchronously. `image://icons` holds `iconThemeLock()` and
`image://thumbs` (`wallpapers.cpp`) makes its pictures on a pool of threads, telling each response
done in the response's own thread, so those two may load asynchronously. The media card's cover
is asynchronous, so `mpris.cpp` takes only a `file:`, `http:`, `https:` or `data:` URL from a
player. `tests/image_providers_test.cpp` checks the providers on both threads.

The pictures of the windows on the taskbar's card (`WindowThumbnails.qml`, with `shell.thumbnails`)
are `TaskModel`'s: its `picture` role is `image://windows/<taskId>/<serial>` once a window has one,
`""` until then, the serial new with every picture so that an `Image` with `cache: false` loads it
again, and the `windows` image provider serves them by task id. The model takes pictures of a
window only while something watches it: `watchPicture(owner, taskId, pixelWidth, live)`, counted
per owner, until `unwatchPicture(owner, taskId)` or until the owner goes: as `place` does, the
model drops what an owner still watches on its `destroyed`, so that no order in which QML tears a
panel down can leave a capture running. It asks the window control for the window's capture
source scaled down to fit `pixelWidth` by 5/8 of it (`get_scaled_capture_source`, version 3; a new
width asks again), or with version 2 for it at the window's size (`get_capture_source`), captures
it with ext-image-copy-capture into shared memory, scales the picture down off the GUI thread
where the frame is larger than that (only ever a copy with version 3), and, when `live`, takes the
next as the window redraws, every 33 ms at most (100 ms at the window's size). The last picture
stays until the window closes, so the card opens with it.

Where the bar shows each window goes to the compositor through `TaskModel::place(owner, taskIds,
output, rect, layer)` and the window control's `set_rectangle` (version 7), for the window to
minimize into and restore out of: each `TaskButton` of the task list places the windows it stands
for (its stack's) at its rectangle on the output, as the list scrolls, slides and drags it and
the bar moves; a tile of the card places its window at its picture, at layer 1 so that it counts
over the button while the card is open; a dock icon places its application's windows at its
icon; and the tiling bar's window module places the focused window at its title. Each reads what
moves it (`placedRect`) and calls the panel's `placeWindows`, which a stand-in model without
`place` leaves alone; a bar hidden while the tiling bar stands in for it places nothing. The model
keeps each owner's rectangles, drops them as the owner goes, and sends the one that counts on each
output (the highest layer's, of a layer the owner that began placing the window last) once a
change is over, and before it activates or minimizes a window or shows the desktop, so that a
click on a picture restores the window out of it. `tests/minimize_shell_smoke.py` follows it.

On the taskbar `Panel.qml` alone calls them, for what tells it that it wants a window's picture
with `wantPicture(taskId, wanted)`: a tile of the card as it appears and as it goes, and the panel
itself for the windows of the button the pointer has rested on for half of `shell.thumbnailDelay`
(`warmGroup`, until the card opens or the pointer leaves), so that the card opens on their
pictures. It counts the wants and asks the model once a change is over (`syncPictures`), so that a
window the tiles take over from the panel as the card opens is neither let go nor asked for
again, which would start its capture anew, or without `live` take its one picture twice. The
panel is the owner of its watches: destroyed with a card open or a button's pictures warmed up
(its monitor unplugged, the lid closed while docked), it runs no `syncPictures` to let them go,
and the model lets go of them as it goes. A stand-in model (the preview's, the tests') has no
`watchPicture`, which the panel then does not call, and names pictures of its own
(`image://preview-windows/ID`, painted by `preview.cpp`).

The window switcher's cards (`SwitcherCards.qml`, which `Switcher.qml` loads in place of its grid
with `shell.thumbnails` outside the macOS style, or in it for one application's windows) show the
same pictures. The switcher's lines
name a window by its app id and title, which two windows may share, and by its number (the window
control's `id`, `TaskModel`'s `windowId` role); each card finds its task with a `TaskFilter`
whose `windowId` keeps that window alone, filtering again when the number comes after the window.
While the switcher is open on the view's output (`open`, `outputName`) a card watches its
picture itself with `watchPicture`, as the watch's owner, unless the model is a stand-in without
it, and unwatches it at `switcher-close`, though the cards stay while the switcher fades. The
cards are made as the list arrives, 120 ms before `SwitcherView` shows the switcher, so the
pictures are asked for early: with 20 windows on a headless compositor with pixman at 2560 by
1440 and a scale of 1.25, every one had a frame about 75 ms after the switcher opened. They are asked for twice as wide as the pictures' common height,
`shell.thumbnailSize` × 5/8, times the device pixel ratio, a box that holds any card's picture
(3:4 to 2:1) without scaling it up. A card is as wide as its picture's proportions, which it notes
in `aspects` once the picture shows (16:10 until then), and `arrange` lays the cards out in rows
at the largest scale, from 1 down to 0.6 in steps of 0.05, at which they fit in the room the
switcher has; past that a `Flickable` scrolls to keep the selected card in sight. As the cards
may widen once the surface shows, `SwitcherView` keeps the surface as large as its root whatever
size the compositor last configured, as a `PickerView` does.

Every listed window's picture is live with `shell.liveThumbnails`, as on Windows 11: a window
that does not redraw costs nothing, as the compositor makes a frame only once it has changed.
Measured on a headless compositor with pixman at 2560 by 1440, scale 1, with 20 windows on four
workspaces and the shell drawing in software, while the switcher was open: with none of the
windows redrawing, neither took any time; each window redrawing as fast as it may (1600 by 900
pixels) added about 7 % of a core to the compositor, pixman scaling it on the CPU, and 3 % to the
shell (one: 4 to 12 % and 2 to 6 %; five: 36 to 39 % and 16 to 17 %); without live pictures the
five cost nothing past the first frames. At a scale of 1.25 Qt's software renderer takes 8 to 10
ms for each frame of the switcher, against under one at a scale of 1 or 2, so that one window
redrawing costs the shell 35 to 37 % and five 42 to 55 % (the compositor 29 to 32 % and 43 to
49 %); drawn through the GPU, the shell and the compositor do that on the GPU.

A drag from an application never reaches the hover handlers: Wayland sends its events
(`wl_data_device`'s enter, motion and leave) to the surface under the pointer instead, and the
pointer stays with none while it lasts. `Panel.qml` follows it with a `DropArea` over the panel's
surface and one over the popover's (`dragOverBar`, `dragOverPopover`), which find the button under
it (an item with `dragWindows()`: a `TaskButton` or a `DockIcon`) or the tile or row of a window
on the open card or list (an item with `taskId`, `active` and `minimized`). After `dragDelay` a
button's one window is activated (`taskSource.activate`, unless it is in front already, which that
would minimize), a stack's windows shown (`openGroup`), or a tile's window activated. What is
open stays while `dragHolds`, which `groupHide` reads beside the hover state. Each `DropArea`
refuses every move (`drag.accepted = false`), and Qt answers the drag's source with the move's
answer, so the shell never takes a drop and the source sees it cancelled. Each fills its surface,
so that a drag enters it only as it comes onto the surface, when Qt follows the enter with a move
at once; an item entered by a later move would answer that move with its enter, which takes the
drag. `shell_ui_test` hands the windows drag events as Qt's Wayland platform does. A drag holds the keyboard in the compositor, and wlroots
lets no focus change through it: `drag_ended` (`input.c`) gives the keyboard to the window
focused meanwhile once the drag ends, and the pointer to the surface under it.

The speaker on a picture's tile is a `WindowSound` (`audio.cpp`), which the tile makes with the
panel's `audioSource` and `taskSource` and its window's `pid` (`TaskModel`'s role, from the window
control's version 3). The sound server's backend gives each stream the process that plays it
(`application.process.id`, or its client's when the stream names none, as a PipeWire
application's may not) and that process's parents, nearest first, read once per stream from
`/proc/PID/stat` by `processAncestry`, 64 at most and stopping before pid 1, as `swallow.c`
walks them. `soundOwner` gives a stream to the nearest of those processes that has a window among
the task source's `pid` roles, so a browser's audio process plays in its window and a player
started in a terminal in the terminal's, unless the player has a window. `playing` is a stream of
the window's that is open and not paused (corked) or muted, `muted` one that is muted while none
plays, and `toggleMute()` mutes or unmutes all of them through `Audio::setStreamMuted`. The
preview's and the tests' stand-ins give the `pid` roles, and the streams' `processes`, themselves.

The card of a workspace (`WorkspaceCard.qml`, `shell.thumbnails.workspaces`) is drawn by the shell
from the windows' pictures rather than captured whole: the compositor's scene shows only the
workspace each output shows, and a picture made of the windows' own lets the shell move each one
as the list changes. `TaskModel` keeps the lists: `watchWorkspace(output, number)`, counted, asks
`get_workspace` (version 8) until `unwatchWorkspace`, and `workspaceLayout(output, number)` gives
what the last `done` completed, `{width, height, windows: [{windowId, x, y, width, height}]}`,
kept once unwatched so that the card opens on it; `workspaceRevision` changes with each. The
panel holds the card's state (`workspaceCardOpen`, `workspaceCardNumber`, the workspace warmed up
halfway into the delay, `workspaceWarm`) as it holds the card of a button's windows, and the two
close each other: `openGroup` closes the one, `openWorkspaceCard` the other. The workspaces of
the taskbar's indicator and of the tiling bar call the same `hoverWorkspace`. The card's picture is
the output's proportions within one and a half times `shell.thumbnailSize` by 5/8 of that
(`workspaceMiniature`, `workspaceScale` card pixels to a logical one), the wallpaper drawn as the
desktop draws it, and two `WorkspaceWindows` layers over it, the one in front showing the card's
workspace: another workspace goes into the one behind, which comes forward once its list is in
(150 ms at most), the two crossfading, and the one behind lets its workspace go once it has faded
out. A layer is live, watching its workspace, from the warm-up until the card has faded out; it
keeps a `ListModel` of copies in step with the list by window number, so that each window keeps
its copy and glides as it moves, fades in as it comes, restacks as it is raised; each copy finds
its task with a `TaskFilter` by `windowId` and wants its picture through the panel's `wantPicture`
at `workspacePictureWidth`, the width whose box of 5/8 of it holds the whole card's picture, so
any window. The panel watches a window's picture at the widest any want asks (`wantedWidth`),
watching again at a new width before letting the old watch go so that the capture goes on.

A window's button dragged onto a workspace, or its picture dragged from the card of its windows,
moves the window there (`dropOnWorkspace`: `moveToOutput` first for a window on another output,
then `moveToWorkspace`). `workspaceAt` finds the workspace under a point of the panel's surface
(a workspace button has `dropsWindows`), which `dropWorkspace` lights. A button's drag is its
`DragHandler` in `TaskList.qml`, in the bar's surface: over a workspace it reorders nothing and
the button eases back into its row (`lift`), and letting go calls `endButtonDrag`. A picture's
drag is a `DragHandler` on its tile, in the popover: the compositor gives the pointer to whichever
surface is under it, button held or not, so while `pictureDrag` lasts the popover takes the
pointer over the whole output, the bar included, and keeps the card open (`groupHide`). What both
carry is the popover's `DropGhost`: the picture, or the icon on a card, under the pointer as it
was taken, docking (`dropDock`) beside the workspace's button off the bar, or in the middle of the
workspace's picture while its card is open (resting there opens it, the bar's buttons still
hearing the pointer during a button's drag), and landing into it.

A tile peeks at its window while the pointer rests on its picture: a `HoverHandler` on the
picture's box and a timer call the task source's `peek(taskId)` after 500 ms, or at once while
its `peekedTask` names a window already, so that the peek moves straight to the next picture, and
`endPeek(taskId)` 150 ms after the pointer leaves the picture or the card closes, time to cross to
the next one, whose peek takes over first (`endPeek` ends only the peek at that window). Half a
second is about Windows' wait, a little longer than the card's own, so that crossing a picture on
the way to the cross, the speaker or another tile does not fade every window on the screen.
`TaskModel` sends `set_peek` and `unset_peek` (version 4) for them; the compositor ends a peek by
itself too, as the window is focused (a click on the picture), which the model does not hear, so
`peekedTask` names the last window it peeked at until `endPeek`. A stand-in without `peek` peeks
at nothing, and the macOS style's dock and the stack's list of titles do not peek. The command
palette (`Palette.qml`) peeks through the task model the same way at the window result selected,
by the task id that is the result's target: 500 ms after the selection comes to it (restarted by
typing), once something was typed or the selection moved since the palette opened (`engaged`), and
at once while it peeks at another, ended 150 ms after the selection leaves the windows
and, as the palette closes, once the event loop has run the result (`Qt.callLater`), so that a
window it focuses ends the peek around itself first. The results are read as the palette opens,
so a window that closed meanwhile keeps its row; the model takes no peek at it (`peekedTask`
stays as it was), and the window peeked at so far is let go, as it no longer stands for the
selection. `shell.peekSelection` is `peek.switcher`.

The keyboard on the bar (`BarKeyboard.qml`, the panel's `barKeys`) is held in the popover. The
`taskbar_focus` action sends the shell `taskbar OUTPUT` for the focused output (`request_taskbar`,
`control.c`); the controller emits `taskbarRequested`, and that output's `ShellView` calls the
panel's `toggleBarKeyboard()` and logs `paw taskbar keyboard on|off on OUTPUT`. While
`barKeys.active` the popover is open and holds the keyboard as for a menu, and `BarKeyboard`, which
has the active focus there, takes the keys. Its stops are found anew as they are needed
(`stops()`): the task list's rows by number, since the list makes the buttons outside its view
only as they scroll in (`positionViewAtIndex` brings the selected one into view), or the dock's
icons. The selected `button` shows its windows with the panel's own `openGroup`, so the card
glides as on hover, and
`window` indexes the card's tiles or the list's rows (`windowAt(index)`). A button, tile or row
reads `keySelected` from it to draw a `FocusRing` and, on a tile, to stand in for the pointer in
the peek's `resting`. The window that had the keyboard is noted as `from` before the popover takes
it (the compositor deactivates the window as a layer surface takes the keyboard, so the task
source then shows none as active), and Enter minimizes it where a click on the focused window's
button would. Its menus (`keyMenu()`, with `menuOpened` set) close back to it (`resume()`), and
any other menu that opens ends it. While it is active `hoverGroup`, `hoverGroupList` and
`groupHide` leave the card alone; a `HoverHandler` on the bar and on the popover (over the card
only) and a `MouseArea` over each that lets presses through hand the bar back to the pointer
(`handOver`, `pointerTakesOver`) once the pointer moves more than two pixels from where it was
first seen, or is pressed. Losing the keyboard (`dismissed`) and `closeMenus()` end it.
`tests/taskbar_keyboard_smoke.py` follows the keyboard with `get seat` in a session.

The start menu (`Launcher.qml` and its `Start*.qml` parts) reads `shell.startMenu`, a `StartMenu`
(`start_menu.cpp`): its own pins, seeded from the taskbar's; the applications launched lately
(`launch_history.cpp`, which the controller tells of every launch); every application by letter;
its search, which takes the palette's windows, workspaces and actions from `Palette::entries` and
runs them with `Palette::run`; and the user's name and picture. It tells the controller to read
the applications again when GIO's monitor says they changed. Launchpad (`Launchpad.qml`), the
launcher of the macOS style, which the launcher's loader in `Panel.qml` makes in the start menu's
place, reads it too: every application by name, and what its search finds of them.

Both searches find files through the controller's `FileIndex` (`file_index.cpp`, `files()`),
which `ShellController::configureFiles` hands `shell.search`. `search()` matches the names read
last, on the GUI thread, and starts reading them again when they are out of date: never read, the
settings changed, five minutes old, or ten seconds after a `QFileSystemWatcher` on a root but the
home folder or on `recently-used.xbel` saw a change. `scan()`, a static function the tests call
too, runs on a `QThread` of its own at the lowest priority: the recent files, then the roots
breadth first with `openat`/`readdir`, never following a link below a root nor crossing to another
device. The result comes back with the thread's `finished`, is dropped if the settings changed
meanwhile, and `changed` makes the palette search again (keeping the entry the selection is on) and
the start menu bump `searchRevision`, which `StartSearch.qml`'s results read. A preview gives the
index stand-ins with `preview()`, after which it reads nothing. `FileIndex::open` opens a file, or
its folder, with GIO's default handler (`g_app_info_launch_default_for_uri`), and
`ShellController::openFile` shows a failure across the panel.

The clipboard history (`ClipboardHistory`, `clipboard.cpp`; `shell.clipboard`) is a client of
ext-data-control-v1 on a Wayland connection of its own, as `TaskModel` is (`connectDisplay`, which
`main.cpp` calls). Its device announces each offer with its types, then the selection. A
selection is read a type at a time into a pipe a `QSocketNotifier` watches (the secret hint first,
then the text, HTML, a list of files and a picture), each type given two seconds and a size cap,
and what was read goes to `record()`, which the tests call too. While the history's own data
source is what is copied (`restore()`), the selection announcing it is not read; another program
copying cancels that source first. A source hands each program pasting the bytes it asks for with
non-blocking writes, SIGPIPE held off, and gives up on one that takes none of them for ten seconds
(`Writing::timer`, started again by each write that gets some through), so that a program that
hangs, or never reads, keeps neither the pipe nor the bytes in the shell. `locked on|off` in the
control socket's state calls `setLocked`, and `clipboard OUTPUT` from the `clipboard_history`
action toggles `output`, which a `PickerView` (`picker_view.cpp`) follows: an `OverlayView` on
every output, as the palette's is, showing `ClipboardPicker.qml`, which filters the entries;
`image://clipboard/ID/SERIAL` (`clipboardImage`, through an `ImageProvider`, `image_provider.hpp`)
serves their pictures. Its file is touched only once the history is the session's (connected) or
was given a path, so tests and previews that make a controller never read or remove it.

The emoji picker (`EmojiPicker`, `emoji.cpp`) reads `vendor/emoji/emoji.tsv`, which
`shell/CMakeLists.txt` compiles into `paw_shell_qml` as `:/paw/emoji.tsv`, and gives
`EmojiPicker.qml` a group's emoji, a search's, and the recent ones, each in the skin tone chosen.
`emoji OUTPUT` from the `emoji_picker` action toggles it, in a `PickerView` as the clipboard's.
`pick()` closes it and keeps the emoji as pending; the `PickerView`, once the overlay has given
the keyboard back (its window deactivated while it goes), calls `keyboardReleased()`, or a timer
does half a second on, and `typeRequested` has the controller send the compositor `type TEXT`,
copying the emoji instead when the compositor answers an error. The compositor types in `type.c`:
a keymap of up to 200 keys made for the text's characters (`xkb_utf32_to_keysym`), on a
`wlr_keyboard` of its own set as the seat's keyboard for the keys, so that the focused client gets
that keymap, each key pressed and released, and the seat's own keyboard, keymap and modifiers
back after.

The sheet of keyboard shortcuts (`Shortcuts`, `shortcuts.cpp`; `shell.shortcuts`) reads the
bindings from the configuration the shell loads itself, not from the compositor. Its pure part,
`shortcuts::sheet`, makes groups of rows from `config.bindings`, and first from the binding mode in
use (`bindingMode`): an action's group and title come from a table there, the title from the
command palette's own where it has one (`Palette::actionTitle`), or from the binding's
`description`, or for a `spawn` its command shortened; the keys are named by `keyName`, for the
style in use, and bindings alike but for their key (the directions of one family, workspaces on
running digits) become one row. `shortcuts::filter` keeps the rows whose `search` text holds every
word typed; `tests/shortcuts_test.cpp` tests both and the model. `shortcuts OUTPUT` from the
`shortcuts` action toggles it, in a `PickerView` that covers the output (`cover`: anchored to
every edge, an exclusive zone of -1, its root sized to it), where `Shortcuts.qml` lays the groups
out in as many columns as the width allows, each group in the shortest so far.

### Frosted glass

With `shell.blur` the bars and the popups are frosted glass: each shows the wallpaper behind it
blurred, under its colour. It is the wallpaper as the desktop draws it that shows, never the
windows (Hyprland's xray), so that one blurred picture per output serves every surface there and
nothing is blurred while the shell draws. `Glass` (`glass.cpp`, the controller's `glass`) keeps
each output's: `Desktop.qml` hands it the wallpaper's layers once a new one has faded in, and as
the configuration changes how the gradient or the drawn wallpaper looks (`capture`, which grabs the
backdrop with `grabToImage` at twice the size the blur works at), and `blurWallpaper`
(`glass_blur.cpp`, tested by `glass_blur_test`) blurs it on a thread of its own: averaged down to a
pixel for a quarter of the Gaussian's spread (no more than a million pixels), three box blurs
across and down that come close to the Gaussian, the brightness and saturation applied. A preview,
which has no desktop, hands it the picture it draws its screenshot over (`previewWallpaper`), at
once. A new picture, or settings that change it, makes it anew; the glass turned off lets them go.

`Glass` in QML is a `GlassItem` (registered with `Paw`): a rounded rectangle, `radius` round,
whose texture coordinates are where each of its points lies on the output, so it shows the part of
the picture behind it. Where its window lies on the output is `surfaceOrigin` (`view.cpp`): the
panel's surface along its edge, the popover and the menu bar at the top left, a layer surface by its
anchors, margins and size within what the bars leave (as wlroots places it), a popup window beside
the window it belongs to, or a preview's overlay where `PreviewData::surfacePlace` says. It looks
again with each frame its window draws (`afterAnimating`), so that a card sliding in or the
switcher growing shows, at each step, what lies behind it, as a pane of glass would. Its window
holds one texture of the picture for all its glass, made on the render thread for each new picture
and let go with the window's graphics; a node keeps the one it draws until it takes the next. It is
inset half a pixel, under the antialiased edge of what is drawn over it, as its own edge is not
antialiased. It draws only through the GPU; Qt's software renderer draws nothing of it, and
`active` stays false, so that what is over it stays opaque. `active` says the glass shows: the
glass is on, the shell draws through the GPU (`effects`) and its output has a picture; a surface
over it reads it to be translucent (`Theme.onGlass`: its colour no more opaque than
`Theme.glassOpacity`, the bar's alpha but at least `Theme.glassFloor`, 0.75). `pending` says a new picture is on its way, which `Restyle` waits for
as for a picture loading.

`CardFrame` puts one under every card, `GlassFill` under a card drawn by hand, and the bars put one
under themselves (a child at `z: -1` draws under its parent's own fill). On the glass,
`Theme.surfaceRaised` and the macOS style's `moduleColor` are veils of the text colour or white
rather than colours of their own, so that the glass shows through what lies on a card too; what
covered a card's colour to shape something (the media card's corners, the notification list's
fading edges) masks it instead (`MultiEffect`), as no colour can cover a translucent card.

### Popups and menus

A popup is a `PopupCard`; a menu is a `PopupMenu` of plain entries. Both place themselves beside
an anchor rectangle, so a popup of the bar only says what it belongs to.

`PopupCard` draws the card (`Theme.popupSurface`, outline, `radius`, a shadow when `Theme.effects`;
none with `framed: false`, for content that draws cards of its own) and
fades in with a few pixels' slide from its anchor while `open`, out again when it is cleared,
staying visible until it has; `progress` is how far open it is. Its content goes inside it and
fills it. It places itself beside `anchorRect` (in its parent's coordinates) on the anchor's
`side` (`Qt.TopEdge`, `Qt.BottomEdge`, `Qt.LeftEdge`, `Qt.RightEdge`), `gap` away, lined up by
`alignment` (`Qt.AlignHCenter`, `Qt.AlignLeft`, `Qt.AlignRight`, or the vertical ones beside the
anchor); it flips to the other side when that one has more room (`placedSide` says where it went),
and stays `margin` inside `bounds`. Its size is its `implicitWidth` and `implicitHeight`, cut to
`availableWidth` and `availableHeight`. With `glides` (the card of window pictures), a card that
is shown eases to another place or size (`placedX`, `placedY`, `placedWidth`, `placedHeight`)
rather than jumping, and one that is not shown takes it at once, so that opening and closing keep
their own motion. `opened()` is emitted once each time it opens and shows,
when its content resets, and `initialFocus` (the card, unless set; `null` for none) then takes the
keyboard. Presses on it stay with it until it starts closing. With `anchored: false` it is only
the card, for a surface that places it itself.

`PopupMenu` fills its parent and draws its levels on popup cards. Its `entries` are objects:

    { text, icon, secondary, toggle, checked, enabled, danger, run, submenu, objectName }
    { separator: true }
    { header: "Section" }
    { title: "Name", icon, secondary }

`icon` is a Lucide name `Icon.qml` knows, else a theme icon's name or an image's URL; `secondary`
is muted text at the row's end (a shortcut, the value in use). A `title` is the menu's own heading
above its entries: the icon large, the name, and the secondary text under it, elided to the width
the entries need; `toggle` is `"check"` or `"radio"`
with `checked`; `enabled: false` greys an entry out and `danger: true` draws it in the danger
colour. `run` is called when the entry is chosen, and the menu then emits `dismissed()` unless it
returns `true`. `submenu` is an array of entries, or a function
returning one that is read as it opens and again when what it read changes. Placement is the
card's: `anchorRect`, `side`, `alignment`, `gap`, `bounds`; `minimumWidth`, `maximumWidth` and
`rowHeight` size the rows. A submenu opens beside its entry after the pointer rests there for
`submenuDelay` milliseconds, on a click, or with Right, Enter or Space; the pointer heading for an
open submenu across other entries (inside the triangle from where it was to the submenu's near
edge) leaves it open until it rests on one. Up, Down, Home and End move, Left or Escape closes a
submenu and Escape in the first level emits `dismissed()`. In the macOS style the rows are denser and
the highlighted one is filled with the accent, its text white, as macOS draws menus (`Theme.menu*`).
`initialIndex` highlights an entry as it opens (-1, none, for a menu opened with the pointer).
`openEntries` lists the entries whose submenus are open, `openSubmenu(index)` opens one,
`closeSubmenus()` closes them, and `card` is the first level's card. Rows are `MenuRow`s, named
`entryName` (`separatorName` for separators) unless an entry gives its `objectName`.

### A new popup of the taskbar

1. In `Panel.qml`, a property saying whether it is open (or a value of `audioPopup`), part of
   `menuOpen` if it takes the keyboard, cleared by `closeMenus()`, and a function that opens it
   by a bar item as `toggleAudioPopup` does: noting where (`audioPopupX`) and closing the others.
2. Its file: a `PopupCard` or `PopupMenu` with `required property var panel`,
   `parent: panel.popupLayer`, `open:` that property,
   `anchorRect: panel.barAnchor(x, width)`, `side: panel.popupSide` and
   `bounds: panel.popupArea` (the output but the bar), so that it opens away from the bar on a
   top panel as on a bottom one, and below the menu bar in the macOS style; one that belongs to
   the dock's icons takes `panel.dockAnchor(x, width)` and `panel.dockSide` instead. A menu's
   `onDismissed` clears the property.
3. A `PopupLoader` for it in the popover in `Panel.qml`, like the others: `wanted` while it is open,
   `early: root.warm` to make it a moment after startup, kept once made.
4. The file in `QML_FILES` in `shell/CMakeLists.txt` and in the table above; a name in
   `previewPopup` (`Panel.qml`; `previewMacos` for one only the macOS style has, which goes in
   `MACOS_POPUPS` too), `--preview-popup`'s help (`main.cpp`) and `POPUPS`
   (`tools/shell_gallery.py`), and a look at the gallery's pictures.
5. Tests in `shell_ui_test`: `find(view.rootObject(), NAME)` finds the popover's items too,
   `click(item)` clicks one in its own window, `inPopover(popup)` says it is open, settled and
   inside the popover, and keys go to `popover`.

### Drawing something in the shell

Take every colour, size and duration from `Theme`: `bar`, `surface`, `surfaceRaised` and
`surfaceRaisedHover` for backgrounds; `hover`, `pressed` and `selected` laid over them for
states; `border` and `divider`; `text`, `textMuted` and `textDisabled`; `accent`,
`accentHover`, `accentSubtle` and `textOnAccent`; `danger`, `dangerFill`, `textOnDanger` and
`dangerSurface`; `urgent` and `urgentSubtle`; `scrim`. Type is `fontFamily` with `fontSize`,
`fontSizeCaption`, `fontSizeSmall`, `fontSizeLarge`, `fontSizeTitle` and `fontSizeDisplay`;
shapes `radiusSmall`, `radiusMedium` and `radiusLarge`; spacing `spacingXS` to `spacingXXL`
(2, 4, 8, 12, 16, 20); icons `iconSizeSmall`, `iconSize`, `appIconSize` and
`appIconSizeLarge`; rows of menus and lists `rowHeight`, their headings `headingHeight`; buttons
on the bar `barButtonHeight` and, an icon's, `barButtonWidth`. Animations use `durationFast`,
`durationNormal`, `durationSlow` or `duration(ms)` with `easing` or `easingExit`, all 0 while
`animations.enabled` is off; something small growing in starts at `growFrom` of its size, and an
icon on the bar shrinks to `pressScale` while pressed. Nothing animates while nothing changes,
so that an idle shell wakes for nothing: motion is a `Behavior` or a transition on a change, and
an animation that runs by itself ends, as the urgent pulse does after a few beats. `effects` says whether shader effects
(shadows) can be drawn: only through the GPU, so draw them only when it is true, as a popup's
`shadow` colour, `shadowBlur` and `shadowOffset` are. `alpha()` and `mix()` derive a colour from
these. A button without a frame of its own is a `FlatButton`, one with a frame and text a
`PushButton`, a tooltip for something on the bar is a `BarTip`, a popup is a `PopupCard` and a
menu a `PopupMenu`.

`macos` says whether the macOS style (`shell.style`) is in use. Its popups have tokens of their
own, after `shadowMargin`: `popupSurface`, `popupOutline` and `popupInnerEdge` for a popup's card,
`textOnAccentFill` for text on the accent, and `menu*`, `launchpad*`, `spotlight*`, `module*`
(Control Center's cards), `notification*`, `switcher*`, `button*`, `field*`, `slider*`, `knob`
and `switchTrack`. One that stands for a token above is that token in the taskbar style, so a part
draws with it in both styles and the taskbar's keeps its look; branch on `Theme.macos` only where
the macOS layout differs (Control Center's modules, Spotlight's groups), and give a popup a file of
its own only where all of it does (Launchpad).

A `WheelHandler` takes `acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad`: since the
compositor offers pointer gestures, Qt puts every event of the seat's pointer down to a touchpad,
a mouse wheel's too, and a handler left at its default of the mouse alone hears none of them. One
that steps something by the wheel's notches is a `NotchWheel`, which does.

### Seeing a change

`paw-shell --config FILE --preview-popup NAME --screenshot OUT.png --quit-after 400`
renders the taskbar offscreen with one popup open, on stand-in windows, sound, tray items and
notifications, over the configured wallpaper (`--preview-popup` lists the names); in the macOS
style the dock and the menu bar, over the style's drawn wallpaper while none is set, with the
menu bar's menus as `system-menu`, `app-menu`, `window-menu` and `window-submenu`. The overlay
surfaces have names there too (`osd-volume`, `osd-text`, `osd-microphone`, `cards`,
`power-dialog`, `palette`, `palette-empty`, `shortcuts`, `shortcuts-search`, `shortcuts-mode`, `switcher`, `overview`, `snap-assist`, `display-mode`,
`display-settings`, `display-settings-trial`, `rice-trial`): `PreviewData` in `preview.cpp` shows one in a window of its own over the
`display-settings`, `display-settings-trial`, `xray`): `PreviewData` in `preview.cpp` shows one in a window of its own over the
`display-settings`, `display-settings-trial`, `caption`): `PreviewData` in `preview.cpp` shows one in a window of its own over the
bar alone, with stand-ins for what the compositor would tell it, and the screenshot draws it where
its layer surface would be (the overview over stand-ins for the compositor's thumbnails, the
exploded view's names over its sheets as `src/xray_layout.c` lays them out).
`--preview --preview-desktop` draws the desktop instead (the gallery's `desktop`, without the
wallpaper it sets for the others, so that the macOS style's drawn one shows).
`tools/shell_gallery.py BUILD_DIR OUT_DIR` does that for every popup in a light and a dark
theme of each style (`light`, `dark`, `macos-light`, `macos-dark`), both with the software renderer (`light-launcher.png`) and through the GPU
(`light-launcher-gpu.png`: Qt's OpenGL on Mesa's software implementation, in a private headless
compositor), in about ten seconds; `--renderer`, `--theme` and `--popup` narrow it down. Nothing
touches the session it runs in. The `shell_gallery` test runs it, with `--no-animations` and a
`--wait` of 150 ms a picture, where 400 is the default, and fails on any QML warning.

## Recipes

### A new binding action

1. Add `SH_<NAME>` to `enum sh_action` in `include/paw/backend.h`.
2. Name it in `action_table` in `src/config.cpp`. That makes it bindable, usable from
   `paw msg` and hot corners, and listed in the configuration reference.
3. If it takes an argument, parse it in the `command` callback in `src/main.cpp` (the
   `action_takes_*` helpers in `config.cpp` say which kinds exist); otherwise it is refused
   with "takes no argument". One that starts a program goes through the `launch` callback
   there, as `spawn` and `terminal` do, so a failure reaches the panel.
4. Add a `case` to `run_action` in `src/compositor/actions.c` that calls the module doing the
   work, or, for one the shell carries out alone, a line or a request for it in
   `shell_actions` there. If it acts on the window under the pointer when bound to a button,
   list it in `action_targets_window` in `cursor.c`.
5. Mention it in `README.md` or `docs/features.md`. The `docs_consistency` test fails until
   you do.

### A new setting

See "Lua settings" in [CONTRIBUTING](../CONTRIBUTING.md#lua-settings): an entry in
`src/config_schema.cpp`, parsing in `src/config.cpp`, a field in `struct sh_settings`
(`backend.h`), then regenerating `docs/config-reference.md`. The compositor reads settings
through `server_settings(server)`, and reacts to a reload in `reload_config` (`server.c`).

### A new query (`paw msg get NAME`)

Write `static void get_<name>(struct sh_server *server, int fd, const char *arguments)` in
`src/compositor/query.c` and add it to `queries[]`. Reply with `control_reply(fd, "ok\n")`
followed by tab-separated lines, or a line starting `error: `. `arguments` is NULL unless the
table entry accepts them. Queries answer while the session is locked, so they must not change
anything. Tests use queries to see the compositor's state, so a feature that is hard to test
often wants one.

### A new control command that is not an action

Write `static void control_<name>(struct sh_server *server, int fd, const char *arguments)` in
`src/compositor/control.c`, following `control_dnd` or `control_osd`, and add it to `commands[]`
there. `arguments` is the rest of the request from the space after the name, "" for the name
alone. Commands answer only while the session is unlocked.

### A new subcommand (`paw NAME`)

Declare `int NAME(int argc, char **argv)` in `include/paw/cli.hpp`, write it in
`src/cli/NAME.cpp` (added to `paw_cli` with a `target_sources` line of its own in
`CMakeLists.txt`), and name it in `subcommands[]` and `usage()` in `src/main.cpp`. It talks to
the running session with `paw::cli::request` or `paw::cli::query` (`$PAW_SOCKET`), finds paw's
state with `paw::cli::state_directory()`, and throws `std::runtime_error` for a usage error.
Keep its arithmetic and formatting in pure functions a unit test (`paw_unit_test(NAME LIBRARY
paw_cli)`) can call; a smoke test runs it against a headless compositor with `desktop.env`.

### A new Wayland protocol or global

Create the global in `sh_run` in `server.c`, keep its pointer and `wl_listener`s in
`struct sh_server` (`server.h`), and put the handlers in the module the protocol belongs to,
declaring the ones `server.c` connects in `server.h`. Add the `#include` for its wlroots
header to `server.h`. A protocol of paw's own is an XML file in `protocols/`, named in the
list `CMakeLists.txt` runs wayland-scanner on (server header and code for the compositor,
client header and code for the shell and the test probes), its global made with
`wl_global_create`, as `window_control.c` does.

### Telling the shell something

`send_event` (`control.c`) sends a line to every subscriber; `send_shell_line` and
`request_shell` are the shell-specific forms, and `report_failure` logs a failure the user no
longer waits on and shows it across the panel (`power-error`, `spawn-error`). `notice
SUMMARY<tab>BODY` tells the user something once, as a notification of the desktop's own
(`ShellController::notice`), or on the on-screen display while no card shows; a window taking the
keyboard's shortcuts is one (`shortcuts_inhibit.c`). The shell reads
them in `shell/controller.cpp`. It subscribes with `subscribe shell`, and `shell_listening` says
whether it has, for something the compositor does another way without it, as the volume keys run
`wpctl` (`volume.c`).

### A new test

- A pure function: a unit test next to the others in `tests/*_tests.c(pp)`, registered in
  `CMakeLists.txt` with `paw_unit_test(NAME)` for `tests/NAME_tests.cpp` (linked to `paw_config`)
  or `paw_c_unit_test(NAME)` for `tests/NAME_tests.c` with `src/NAME.c`; `tests/check.h` has its
  `CHECK` and `NEAR`, and `tests/config_check.hpp` a configuration test's `require`, `error_of`
  and `rejects`.
- Compositor behavior: a smoke test, `tests/<name>_smoke.py`. Copy a short one such as
  `sticky_smoke.py`. `with harness.Compositor(compositor, CONFIG) as desktop:` starts a headless
  compositor with the pixman renderer in a temporary `XDG_RUNTIME_DIR`; open windows with
  `desktop.open_window(wayland_probe, TITLE)`, or other clients (`x11_probe`) with
  `desktop.spawn([probe, ...])`, drive it with `desktop.msg` (and `desktop.keyboard()`,
  `desktop.virtual_pointer(pointer_probe, ...)`), reload it with `desktop.reload(LUA)`, and read
  the state back with `get` queries (`desktop.windows()` names the columns of `get windows`,
  `desktop.rows(NAME)` splits any) and what subscribers hear with `desktop.subscribe()`. On the
  way out it ends every client, checks that the compositor exits cleanly, and prints the logs if
  the test failed. Wait with `desktop.wait_for`, never with a fixed sleep; to check that
  something does not happen, which an animation or a client's commit could do a little later,
  use `desktop.stays`. Register it with `paw_smoke(NAME ARGUMENTS...)` (and `TIMEOUT SECONDS`
  for more than a minute, which the sanitizers' build triples) under `PAW_BUILD_COMPOSITOR` in `CMakeLists.txt`. A temporary
  directory's prefix stays at 26 characters or fewer: the control socket goes in it, a Unix
  socket's path is limited to about 107 bytes, and a Gentoo package build runs the tests in a
  `TMPDIR` of 43 characters or more.
  Under `--headless`, `paw msg headless_output`, `headless_keyboard`, `headless_pointer`
  and `headless_touch` plug in outputs, keyboards, pointers and touchscreens
  (`headless_keyboard key NAME CODE press` types on one, see `keymap_smoke.py`;
  `headless_pointer swipe NAME update DX DY [TIME]` moves a touchpad gesture's fingers, at a given
  time in milliseconds, see `pointer_gestures_smoke.py`; `headless_touch down NAME ID X Y` puts a
  finger on a screen, see `touchscreen_smoke.py`), and `headless_switch` a lid or tablet-mode
  switch (`headless_switch toggle NAME on` closes a lid; see `lid_smoke.py`). The `input_probe`
  window (or panel, with
  `--layer`) prints the input it gets, a line per event, and `wayland_probe --keymap` prints the
  keymap an application gets. A `wayland_probe` window with
  `PAW_PROBE_DRAG=source` drags a line of text on a button press of `pointer_probe`'s, and
  one with `=target` takes it, each printing what it hears; `get seat` says where the drag is
  (see `drag_focus_smoke.py`). One with `=bare` drags nothing, without a data source, the first
  time for a serial the press did not give, which the compositor turns down (see
  `drag_focus_smoke.py` and `xwayland_drag_smoke.py`). `headless_output capture NAME PATH` writes a mirroring output's
picture to a PPM file (see `mirror_smoke.py`), and `PAW_TEST_REFUSE_10BIT=NAME,...` makes
those headless outputs refuse a 10-bit render format, as a monitor without one does (see
`bit_depth_smoke.py`), and `PAW_TEST_REFUSE_TEARING` refuses their asynchronous page flips
(see `tearing_smoke.py`, whose windows ask for them with `PAW_PROBE_TEARING=async`);
`PAW_TEST_HDR` makes outputs claim HDR in their EDID (see `hdr_smoke.py`).
`PAW_TEST_DROP_CLIP` makes the scratchpad's drop-down cut windows on pixman as it does on GLES2,
and `PAW_PROBE_PANEL=top` stands a `wayland_probe`'s panel along the top (see
`scratchpad_drop_smoke.py`).
`PAW_TEST_TRIAL_MS` shortens a display settings trial, and `PAW_TEST_REFUSE_MODE=WxH`
and `PAW_TEST_REFUSE_COMMIT=WxH` make headless outputs refuse that size in the display
settings' test, or in `configure_output`'s as if the commit failed (see
`display_settings_smoke.py`). `PAW_TEST_LOCK_TIMEOUT_MS` shortens the time a locker gets to
lock the screen before a suspend gives up (see `power_smoke.py`), and
`PAW_TEST_CROSSFADE_HOLD_MS` sets how long a profile's crossfade waits for the shell (see
`crossfade_smoke.py`), and `PAW_TEST_RICE_TRIAL_MS` how long a rice is on trial (see
`rice_smoke.py`).
`PAW_TEST_CAT_SLEEP_MS` sets how long the desktop cat waits without input before it sleeps, in
place of `cat.sleep_after` (see `cat_smoke.py`), and `PAW_TEST_HEATMAP_REST_MS` how long a
resting pointer counts in the heatmap (see `heatmap_smoke.py`).
`PAW_TEST_AMBIENT_IDLE_MS` sets how long ambient particles wait without input before they fade
out (see `ambient_smoke.py`).
`PAW_PROBE_ICON` gives a `wayland_probe` window an icon
  through xdg-toplevel-icon-v1 and an `x11_probe` window `_NET_WM_ICON`, and their commands
  change it (see `window_icon_smoke.py`). `PAW_LOGIN_SESSION=1` makes a headless
  compositor start as a standalone session does, running XDG autostart from the directories
  `XDG_CONFIG_HOME` and `XDG_CONFIG_DIRS` name (see `autostart_smoke.py`), and saving and
  restoring the last session in `XDG_STATE_HOME` (see `session_restore_smoke.py`); without it only
  `--session` does, so no test starts the developer's own autostart entries.
  `PAW_TEST_WRAPPED=1` has a headless compositor count for paw wrapped as a standalone session
  does, `PAW_TEST_WRAPPED_SAVE_MS` and `PAW_TEST_WRAPPED_IDLE_MS` shorten the time between its
  saves and the time without input after which it counts idle time, and
  `PAW_TEST_WRAPPED_MIDNIGHT=SECONDS` has the day end that long after the compositor starts (see
  `wrapped_smoke.py`).

## A fast loop

```sh
cmake --build build --target paw          # only the compositor
ctest --test-dir build -j8 -R sticky           # only the tests you are working on
ctest --test-dir build -j8                     # everything, about 90 seconds
tools/check-all.sh                             # plus the sanitizer build, before merging
```

`build/compile_commands.json` is there for clangd. `paw --headless` with
`WLR_RENDERER=pixman` runs the compositor with no display, as the tests do.
