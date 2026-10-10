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
  `autostart_tests`). `paw msg ...` is the same binary acting as a control client.
- **`paw-shell`** (`shell/`) is the Qt Quick shell: panels, launcher, notifications, tray, OSD,
  and the text of the switcher and overview. It is an ordinary layer-shell client that
  connects to the control socket with `subscribe` and gets state lines and events.

## Layers

| Where | Language | What |
| --- | --- | --- |
| `src/compositor/` | C | The compositor proper, on wlroots. Shares one private header, `server.h`. |
| `src/*.c`, `include/paw/*.h` | C | Pieces the compositor uses that stand on their own: animations, window controls, shadows and tab strips (pixels), effect arithmetic, touchpad swipes' arithmetic, overview thumbnails, session files, the monitors' settings kept from the display settings window, the logind client, what dynamic window rules hold and give back. Several are unit tested. |
| `src/config.cpp`, `src/config_schema.cpp` | C++ | The Lua configuration: parsing, validation, the action table and the settings schema. |
| `src/tiling.cpp`, `src/layout.cpp`, `src/window_placement.cpp`, `src/overview_layout.cpp` | C++ | Pure geometry (tiling layouts, snapping, placement, the overview grid), with a C interface in `backend.h` and unit tests. |
| `src/import/` | C++ | `paw import` from Hyprland and Waybar. |
| `shell/` | C++/QML | The shell. |

Keep geometry and arithmetic out of `src/compositor/` where you can: a pure function in its own
file can be unit tested without starting a compositor.

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
| `keymap.c` | The keymap from the keyboard settings, given to every keyboard but virtual ones. |
| `cursor.c` | What is under the pointer, focus on hover, button bindings, scrolling, the cursor image. |
| `gestures.c` | Touchpad gestures: the swipes `gestures` takes (workspaces and the overview following the fingers, requests), and the rest passed on to the surface under the pointer (pointer-gestures-unstable-v1). |
| `tablet.c` | Drawing tablets (tablet-v2): tools with pressure and tilt to the surfaces under them, the pointer where tablet input is not taken, pads to the surface with the keyboard, and the output a tablet is mapped to. |
| `touch.c` | Touchscreens: fingers to the surfaces under them (wl_touch), the pointer for clients without touch and the compositor's own controls, and the output each screen is mapped to. |
| `grab.c` | Moving and resizing with the pointer, magnetic edges, dropping. |
| `snap.c` | Snapping a window dragged to an edge of its output: the zones, the preview, the drop. |
| `stacking.c` | The layers windows are drawn in among themselves (tiles, floating windows over them with `layout.floating_above_tiles`, windows kept above the others) and keeping a window above. |
| `dynamic_rules.c` | Dynamic window rules: what they hold a window to as its title and app ID change, given back as they stop (with `src/dynamic_rule.c`). |
| `focus.c` | Keyboard focus and urgent windows. |
| `toplevel.c` | Windows: xdg-shell toplevels and popups, opening by window rules, maximize, fullscreen, minimize. |
| `xwayland.c` | X11 windows and the XWM waker. |
| `frame.c` | Borders, opacity, rounded corners, window controls, shadows, tab strips. |
| `gradient_border.c` | Borders in gradients: the pieces `src/border.c` paints, a strip along each side the scene stretches and a square at each corner, in place of the border's rects. |
| `placement.c` | Snapping, maximizing, reflowing, moving and resizing by keyboard. |
| `tiling.c` | Glue between windows and the layouts in `src/tiling.cpp`. |
| `workspace.c` | Workspaces per output, sticky windows. |
| `output.c`, `output_moves.c` | Monitors and their configuration; windows and workspaces moving between outputs. |
| `output_power.c` | Monitors turned off and on in the layout: wlr-output-power-management. |
| `mirror.c` | Mirroring: a monitor out of the layout showing another's frames, scaled to fit, with its hardware cursor. |
| `hdr.c` | HDR (`outputs.monitors`' `hdr`): monitors driven in BT.2020 with PQ where they, the renderer and the test allow, and color-management-v1 for applications' colours. |
| `tearing.c` | Tearing (tearing-control-v1, `windows.allow_tearing`): which fullscreen window may have its frames shown at once, and the asynchronous page flips. |
| `display_mode.c` | The display_mode action (Windows' Win+P): extend, duplicate, internal, external, and the popup that steps through them. |
| `display_settings.c` | The display settings window's monitors: the settings it kept (`src/output_state.c`), laid over `outputs.monitors`, the primary monitor, `get monitors`, and `monitors apply` trying every monitor's settings at once, kept or taken back. |
| `layer_shell.c` | Panels and other layer surfaces. |
| `group.c`, `scratchpad.c`, `swallow.c`, `switcher.c`, `overview.c`, `session.c` | One feature each. `session.c` also saves a login session as `last` as it ends (`session_save_last`, from the power actions and quit) and restores it after `startup` (`session_restore_last`, from `sh_run`), asking `sh_callbacks.started` which missing windows startup and autostart will open. |
| `switches.c` | Switch devices: the lid turning a laptop's panel off and on (clamshell), switches' bindings. |
| `effects.c` | Dimming, peeking at the desktop or at one window, night light, magnifier, hot corners. |
| `lock.c` | Session lock and idle/sleep inhibitors. |
| `idle.c` | Power saving after a while without input: dimming, monitors off, locking, suspending. |
| `power.c` | The power actions: suspend, hibernate, reboot and power off through logind (`src/login1.c`), locking first, closing windows first, log out. |
| `foreign_toplevel.c` | Window lists for taskbars and single-window capture. |
| `window_control.c` | The shell's window menu, window pictures and windows' own icons: paw-window-control-v1, which names a window by its taskbar handle. |
| `scaled_capture.c` | The capture source for a window's picture: the window scaled down to fit a size on the renderer, smoothly. |
| `type.c` | Typing text into what has the keyboard (`paw msg type`), on a keyboard and keymap of its own. |
| `window_icon.c` | The icons windows supply themselves (xdg-toplevel-icon-v1; X11 windows' from `xwayland.c`), kept for the window control to send the shell. |
| `volume.c` | The volume, microphone and brightness actions, which the shell carries out. |
| `binding_mode.c` | Binding modes: which set of key bindings is in use. |
| `shortcuts_inhibit.c` | Keyboard shortcuts inhibitors (keyboard-shortcuts-inhibit-unstable-v1): the keys the bindings take going to a virtual machine, a remote desktop or a game while it has the keyboard. |

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
`window_peek_smoke` and `window_icon_smoke`, and `TaskModel` the shell's.

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
and each frame lays the hold again over what a client drew. `workspace_swipe_end` either switches
(the leaving windows going on as copies with `sh_anim_slide_away`, the arriving ones taking over
from their copies with `sh_anim_slide_from`, each in what remains of the slide's time) or slides
everything back. The overview follows them through `overview_hold` and `overview_release`, which
set its `progress` in place of its timer's. `tests/touchpad_gestures_smoke.py` drives all of it
with a headless pointer, at the event times it gives.

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

### Power saving when idle

`idle.c` takes the `idle` steps (`enum sh_idle_step`: dim, display_off, lock, suspend) with one
timer, `sh_server.idle`. `input_activity` calls `idle_activity` on every input event, which only
notes the time while no step has been taken; the timer, set for the next step's time on either
power source's table, finds when it fires whether input came meanwhile and sets itself again.
Each step taken sets its bit in `done` and is not taken again until input clears them, which
also fades the dimming out and turns on the outputs the display_off step turned off (`idle_off`
on `sh_output`, cleared whenever an output is turned on another way). The dimming is a stretched
`server->black` buffer (`sh_dim_create`) in a tree of its own created last at the scene's root,
over the lock too, faded by `tick_idle` from `output_frame`. `idle_held` (an idle inhibitor, or
the session not active) disarms the timer, and `idle_hold_changed` (called from `lock.c` as
inhibitors come and go and the VT changes) restarts the count once released. `idle_steps` reads
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
| `TopMenuBar.qml`, `Dock.qml`, `DockIcon.qml` | The bars of the macOS style (`shell.style`): the menu bar along the top in a `MenuBarWindow` of its own, with the system, application and Window menus, the widgets, search, Quick Settings and the clock; and the dock in the panel's surface, an icon for each application, pinned or running, with the applications button and the Trash. |
| `BarKeyboard.qml` | The keyboard on the bar (`taskbar_focus`): held in the popover, it walks the taskbar's buttons or the dock's icons and the windows they show. |
| `BindingMode.qml` | The name of the binding mode in use, on the bar while it is not the default one. |
| `TaskList.qml`, `TaskButton.qml`, `TrayButton.qml`, `WorkspaceIndicator.qml`, `VolumeButton.qml`, `ClockButton.qml`, `BatteryWidget.qml`, `NetworkWidget.qml`, `NotificationBell.qml`, `KeyboardLayout.qml`, `QuickSettingsButton.qml`, `WallpapersButton.qml`, `ProfilesButton.qml`, `TilingButton.qml`, `BarAppIcon.qml`, `Badge.qml`, `BarTip.qml` | Parts of the bar: widgets, an application's icon on it, a count on a pill, and the tooltip for things on it. |
| `ClockFlyout.qml`, `QuickSettings.qml`, `AudioMixer.qml`, `AudioOutputs.qml`, `ProfileList.qml`, `WallpaperPicker.qml`, `Launcher.qml`, `PowerMenu.qml`, `TaskbarMenu.qml`, `TrayMenu.qml`, `GroupList.qml`, `WindowThumbnails.qml`, `MenuBarMenu.qml` | Popups of the bar, each made by a loader in `Panel.qml` when first needed. |
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
| `EmptyState.qml` | What a list says while it has nothing to show: an icon, a line and a hint. |
| `PopupCard.qml` | A popup's card: surface, outline, corners, a shadow through the GPU, the open and close animation, and its place beside what it belongs to. |
| `CardFrame.qml` | A card's shadow, surface, outline and the macOS style's inner edge, beneath what it holds: a popup's, an overlay's, a notification's. |
| `Reveal.qml` | How far an overlay or a card is in view: its `progress`, easing to 1 while `shown` and back to 0 after, which the overlays and `PopupCard` take as their own. |
| `PopupMenu.qml`, `MenuRow.qml` | A menu of plain entries on popup cards, with cascading submenus and keyboard navigation, and one row of it. |
| `WindowMenu.js` | The entries of a menu about windows that move them to another workspace or monitor. |
| `AudioSlider.qml`, `MuteButton.qml`, `StreamRow.qml` | Controls the mixer and Quick Settings use: a volume's slider, a mute button, and an application playing sound. |
| `StartHome.qml`, `StartAllApps.qml`, `StartSearch.qml`, `StartBestMatch.qml`, `StartTile.qml`, `StartRow.qml`, `UserAvatar.qml` | Parts of the start menu (`Launcher.qml`): its pinned and recent applications, every application from A to Z, what its search finds and the best match of it, a pinned application, a row of its lists, the user's picture. |
| `Launchpad.qml` | The launcher of the macOS style, in the start menu's place: every application on pages of a grid over the whole output, with a search. |
| `Desktop.qml` | The wallpaper and the desktop's launchers, on the background layer. |
| `DrawnWallpaper.qml` | The wallpaper the macOS style draws while none is set, light or dark, from the background colour and the accent. |
| `Switcher.qml`, `Overview.qml`, `Palette.qml`, `PowerDialog.qml`, `NotificationCards.qml`, `Osd.qml`, `ConfigError.qml` | One overlay surface each. |
| `DisplayMode.qml` | The display mode popup (Windows' Win+P, `shell.displayModes`), an overlay surface of its own in the middle of the output: the four choices, the one the compositor's stepping shows selected; a click takes one. |
| `DisplaySettings.qml` | The display settings window (`shell.displaySettings`), an overlay surface of its own in the middle of the output: the monitors' arrangement to drag, the selected monitor's settings, Apply, and the question while they are on trial. |
| `AuthDialog.qml` | The polkit authentication dialog (`shell.authentication`), an overlay surface of its own: the request, the user to answer as, the password. |
| `ClipboardPicker.qml`, `EmojiPicker.qml` | The clipboard history's popup and the emoji picker, each in an overlay surface of its own (`PickerView`, `picker_view.cpp`). |
| `SwitcherCards.qml` | The switcher's windows as cards with their pictures, in rows, with `shell.thumbnails` outside the macOS style. |

The authentication dialog's model is `Authentication` (`authentication.cpp`): polkit's requests,
one at a time, each answered through an `AuthConversation` for the chosen user. The agent
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
`TaskbarOrder` the source model owns) and `task_filter.cpp` (an application's windows, for its
groups and menus), `audio.cpp` with `pulse_audio.cpp`, `system_status.cpp`
(battery, network), `tray*.cpp`, `notification*.cpp`, `osd.cpp` and `backlight.cpp`,
`volume_keys.cpp` (what the compositor passes on from the volume, microphone and brightness keys),
`power.cpp`, `palette.cpp` (with `fuzzy.cpp`, and `calculator.cpp`, `file_index.cpp` and
`web_search.cpp`, which the start menu's search shares), `media.cpp` with `mpris.cpp`,
`power_mode.cpp` with `power_profiles_daemon.cpp`, `wifi.cpp` with `network_manager.cpp`,
`bluetooth.cpp` with `bluez.cpp`, `clipboard.cpp` (the clipboard history). `preview.cpp` has
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
connection.

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

The pictures of the windows on the taskbar's card (`WindowThumbnails.qml`, with `shell.thumbnails`)
are `TaskModel`'s: its `picture` role is `image://windows/<taskId>/<serial>` once a window has one,
`""` until then, the serial new with every picture so that an `Image` with `cache: false` loads it
again, and the `windows` image provider serves them by task id. The model takes pictures of a
window only while something watches it: `watchPicture(taskId, pixelWidth, live)`, counted, until
`unwatchPicture(taskId)`. It asks the window control for the window's capture source scaled down to
fit `pixelWidth` by 5/8 of it (`get_scaled_capture_source`, version 3; a new width asks again), or
with version 2 for it at the window's size (`get_capture_source`), captures it with
ext-image-copy-capture into shared memory, scales the picture down off the GUI thread where the
frame is larger than that (only ever a copy with version 3), and, when `live`, takes the next as
the window redraws, every 33 ms at most (100 ms at the window's size). The last picture stays until
the window closes, so the card opens with it.

On the taskbar `Panel.qml` alone calls them, for what tells it that it wants a window's picture
with `wantPicture(taskId, wanted)`: a tile of the card as it appears and as it goes, and the panel
itself for the windows of the button the pointer has rested on for half of `shell.thumbnailDelay`
(`warmGroup`, until the card opens or the pointer leaves), so that the card opens on their
pictures. It counts the wants and asks the model once a change is over (`syncPictures`), so that a
window the tiles take over from the panel as the card opens is neither let go nor asked for
again, which would start its capture anew, or without `live` take its one picture twice. A
stand-in model (the preview's, the tests') has no `watchPicture`, which the panel then does not
call, and names pictures of its own (`image://preview-windows/ID`, painted by `preview.cpp`).

The window switcher's cards (`SwitcherCards.qml`, which `Switcher.qml` loads in place of its grid
with `shell.thumbnails` outside the macOS style) show the same pictures. The switcher's lines
name a window by its app id and title, which two windows may share, and by its number (the window
control's `id`, `TaskModel`'s `windowId` role); each card finds its task with a `TaskFilter`
whose `windowId` keeps that window alone, filtering again when the number comes after the window.
While the switcher is open on the view's output (`open`, `outputName`) a card watches its
picture itself with `watchPicture`, unless the model is a stand-in without it, and unwatches it
at `switcher-close`, though the cards stay while the switcher fades. The cards are made as the
list arrives, 120 ms before `SwitcherView` shows the switcher, so the pictures are asked for
early: with 20 windows on a headless compositor with pixman at 2560 by 1440 and a scale of 1.25,
every one had a frame about 75 ms after the switcher opened. They are asked for twice as wide as the pictures' common height,
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
at nothing, and the macOS style's dock and the stack's list of titles do not peek.

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
non-blocking writes, SIGPIPE held off. `locked on|off` in the control socket's state calls
`setLocked`, and `clipboard OUTPUT` from the `clipboard_history` action toggles `output`, which a
`PickerView` (`picker_view.cpp`) follows: an `OverlayView` on every output, as the palette's is,
showing `ClipboardPicker.qml`, which filters the entries; `image://clipboard/ID/SERIAL`
(`clipboardImage`, through an `ImageProvider`, `image_provider.hpp`) serves their pictures. Its file is touched only once the history is the
session's (connected) or was given a path, so tests and previews that make a controller never
read or remove it.

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
returns `true`; `triggered(entry)` comes first. `submenu` is an array of entries, or a function
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
`power-dialog`, `palette`, `palette-empty`, `switcher`, `overview`, `snap-assist`, `display-mode`,
`display-settings`, `display-settings-trial`): `PreviewData` in `preview.cpp` shows one in a window of its own over the
bar alone, with stand-ins for what the compositor would tell it, and the screenshot draws it where
its layer surface would be (the overview over stand-ins for the compositor's thumbnails).
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
  for more than a minute) under `PAW_BUILD_COMPOSITOR` in `CMakeLists.txt`. A temporary
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
  (see `drag_focus_smoke.py`). `headless_output capture NAME PATH` writes a mirroring output's
picture to a PPM file (see `mirror_smoke.py`), and `PAW_TEST_REFUSE_10BIT=NAME,...` makes
those headless outputs refuse a 10-bit render format, as a monitor without one does (see
`bit_depth_smoke.py`), and `PAW_TEST_REFUSE_TEARING` refuses their asynchronous page flips
(see `tearing_smoke.py`, whose windows ask for them with `PAW_PROBE_TEARING=async`);
`PAW_TEST_HDR` makes outputs claim HDR in their EDID (see `hdr_smoke.py`).
`PAW_TEST_TRIAL_MS` shortens a display settings trial, and `PAW_TEST_REFUSE_MODE=WxH`
and `PAW_TEST_REFUSE_COMMIT=WxH` make headless outputs refuse that size in the display
settings' test, or in `configure_output`'s as if the commit failed (see
`display_settings_smoke.py`). `PAW_TEST_LOCK_TIMEOUT_MS` shortens the time a locker gets to
lock the screen before a suspend gives up (see `power_smoke.py`).
`PAW_PROBE_ICON` gives a `wayland_probe` window an icon
  through xdg-toplevel-icon-v1 and an `x11_probe` window `_NET_WM_ICON`, and their commands
  change it (see `window_icon_smoke.py`). `PAW_LOGIN_SESSION=1` makes a headless
  compositor start as a standalone session does, running XDG autostart from the directories
  `XDG_CONFIG_HOME` and `XDG_CONFIG_DIRS` name (see `autostart_smoke.py`), and saving and
  restoring the last session in `XDG_STATE_HOME` (see `session_restore_smoke.py`); without it only
  `--session` does, so no test starts the developer's own autostart entries.

## A fast loop

```sh
cmake --build build --target paw          # only the compositor
ctest --test-dir build -j8 -R sticky           # only the tests you are working on
ctest --test-dir build -j8                     # everything, about 25 seconds
tools/check-all.sh                             # plus the sanitizer build, before merging
```

`build/compile_commands.json` is there for clangd. `paw --headless` with
`WLR_RENDERER=pixman` runs the compositor with no display, as the tests do.
