// SPDX-License-Identifier: GPL-3.0-or-later
#include "paw/config_schema.hpp"

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string_view>

namespace paw {
namespace {
constexpr double none = 0;
// Order is the order of the generated reference.
const Option options[] = {
    {"version", "integer", "1", "1", 1, 1, "Configuration API version; only 1 exists."},
    {"extends", "string", "unset", "\"default\"", none, none,
     "`\"default\"` layers this file over the shipped default configuration: it supplies every "
     "setting and binding this file omits, and a binding with `action = \"none\"` removes a "
     "default one."},
    {"theme", "string", "unset", "\"theme.lua\"", none, none,
     "A Lua file, relative to this one, that supplies every setting this file omits; "
     "`paw import` writes one. A missing file is ignored."},
    {"profile", "string", "unset", "", none, none,
     "The appearance profile to start with, a name from `profiles`. One picked from the "
     "panel's menu, the command palette or `paw msg profile NAME` replaces it until "
     "another is picked; that choice is kept in `$XDG_STATE_HOME/paw/profile`."},
    {"profiles", "table of tables", "unset", "", none, none,
     "Appearance profiles, keyed by a name of up to 32 letters, digits, `-` and `_` (not "
     "`next` or `prev`), at most 32. Each holds `appearance`, `windows` and `shell` settings "
     "that replace this file's own (and its theme's and defaults') while the profile is in "
     "use, e.g. `light = { shell = { panel_color = \"#f2f4f8\" } }`. Every profile is "
     "checked as the file loads. A file with `extends = \"default\"` gets the default "
     "configuration's profiles only when it has none of its own."},
    {"modes", "table of lists", "unset", "", none, none,
     "Binding modes, as sway's modes and Hyprland's submaps: lists of key bindings (as in "
     "`bindings`, keys only) keyed by a name of up to 32 letters, digits, `-` and `_` (not "
     "`default`), at most 16. A binding with `action = \"mode\", mode = \"NAME\"` puts one in "
     "use: its bindings take the place of `bindings`, which `mode = \"default\"` brings back, "
     "and other keys reach the application. Each mode needs a binding with `action = \"mode\"` "
     "that leaves it."},
    {"xwayland", "boolean", "true", "true", none, none,
     "Run X11 applications; Xwayland starts on first use. Restart to change."},
    {"auto_reload", "boolean", "true", "false", none, none,
     "Reload when this file, or another `.lua` or `.xkb` file in its directory (or the "
     "`keyboard.file` there), is saved. A file with an error loads the default configuration "
     "instead and shows the error on screen."},
    {"terminal", "list of strings", "unset", "{ \"foot\" }", none, none,
     "The terminal the `terminal` action (Super + Q) opens, as a program and its arguments. "
     "Unset, it is `$TERMINAL` when that program is installed, else the first of kitty, foot, "
     "alacritty, wezterm, ghostty, konsole, gnome-terminal and xterm found on `PATH`."},

    {"appearance", "table", "", "", none, none, "Desktop look."},
    {"appearance.background", "color", "\"#19212e\"", "\"#19212e\"", none, none,
     "Background color behind everything, `#RRGGBB`."},

    {"keyboard", "table", "", "", none, none, "Keyboard layout and key repeat."},
    {"keyboard.layout", "string", "\"us\"", "\"us\"", none, none,
     "XKB layout name, e.g. `\"us\"` or `\"no\"`."},
    {"keyboard.variant", "string", "\"\"", "\"intl\"", none, none,
     "XKB layout variant, e.g. `\"intl\"`."},
    {"keyboard.model", "string", "\"\"", "\"pc105\"", none, none, "XKB keyboard model."},
    {"keyboard.options", "string", "\"\"", "\"caps:escape\"", none, none,
     "XKB options, e.g. `\"caps:escape\"`."},
    {"keyboard.rules", "string", "\"\"", "\"evdev\"", none, none,
     "XKB rules the other names are looked up in; `\"\"` uses xkbcommon's default, `evdev`."},
    {"keyboard.file", "string", "\"\"", "", none, none,
     "An XKB keymap file, such as `xkbcli compile-keymap` writes or a hand-written "
     "`xkb_keymap { ... }`, used instead of the names above: absolute, starting with `~/`, or "
     "relative to this file. One that cannot be read or compiled is a configuration error; "
     "should it break while the session runs, the names above stand in for it."},
    {"keyboard.repeat_rate", "integer", "25", "25", 0, 100, "Key repeats per second."},
    {"keyboard.repeat_delay", "integer", "600", "600", 0, 5000,
     "Milliseconds a key is held before it repeats."},
    {"keyboard.shortcuts_inhibit", "boolean", "true", "true", none, none,
     "Let applications that ask for it (virtual machines, remote desktops, some games) have the "
     "keys the bindings take while they have the keyboard; `toggle_shortcuts_inhibit` (Super + "
     "Shift + Escape) takes them back. `false` refuses every application, a window rule's "
     "`shortcuts_inhibit = false` one."},

    {"mouse", "table", "", "", none, none, "Pointer settings."},
    {"mouse.modifier", "enum", "\"Alt\"", "\"Super\"", none, none,
     "Modifier for window drags: modifier + left drag moves, right drag resizes. One of "
     "`Alt`, `Super`, `Ctrl`, `Shift`."},
    {"mouse.speed", "number", "unset", "0.0", -1, 1,
     "Pointer speed in standalone sessions; unset keeps the device default."},
    {"mouse.acceleration", "enum", "unset", "\"flat\"", none, none,
     "`\"flat\"` or `\"adaptive\"`; unset keeps the device default."},
    {"mouse.natural_scroll", "boolean", "unset", "false", none, none,
     "Natural scrolling for mice; unset keeps the device default."},
    {"mouse.focus_follows", "boolean", "true", "true", none, none,
     "Hovering a window focuses it, without raising it."},

    {"touchpad", "table", "", "", none, none, "Touchpad settings (standalone sessions)."},
    {"touchpad.natural_scroll", "boolean", "unset", "true", none, none,
     "Natural scrolling; unset keeps the device default."},
    {"touchpad.tap_to_click", "boolean", "unset", "true", none, none,
     "Tap to click; unset keeps the device default."},
    {"touchpad.disable_while_typing", "boolean", "unset", "true", none, none,
     "Ignore the touchpad while typing; unset keeps the device default."},

    {"touch", "table", "", "", none, none,
     "Touchscreens (standalone sessions): each finger goes to what is under it."},
    {"touch.output", "string", "unset", "\"eDP-1\"", none, none,
     "The monitor touchscreens are mapped to, by connector name or `\"desc:\"` and the start of its "
     "description as in `outputs.monitors`. Unset, a touchscreen goes to the monitor its device "
     "names, else to the built-in panel (eDP, LVDS or DSI), else across every monitor."},

    {"tablet", "table", "", "", none, none,
     "Drawing tablets (standalone sessions): pens with pressure and tilt, and their pads."},
    {"tablet.output", "string", "unset", "\"DP-1\"", none, none,
     "The monitor a tablet's area is mapped to, by connector name or `\"desc:\"` and the start of "
     "its description as in `outputs.monitors`. Unset, or naming none that is plugged in, the "
     "tablet spans every monitor."},

    {"layout", "table", "", "", none, none, "Gaps, workspaces, and tiling."},
    {"layout.gap", "integer", "8", "8", 0, 100,
     "Pixels between windows and at the screen edges; `gap_inner` and `gap_outer` override it."},
    {"layout.gap_inner", "integer", "`gap`", "8", 0, 100, "Pixels between tiled windows."},
    {"layout.gap_outer", "integer", "`gap`", "8", 0, 100, "Pixels between tiles and the edges."},
    {"layout.smart_gaps", "boolean", "true", "false", none, none,
     "No gaps around a tiled window that is alone on its workspace."},
    {"layout.floating_above_tiles", "boolean", "false", "true", none, none,
     "Floating windows always stack over tiles, as in sway and Hyprland; `false` puts the "
     "window brought forward last in front, tile or not."},
    {"layout.workspaces", "integer", "4", "4", 1, 10, "Workspaces per monitor."},
    {"layout.workspace_names", "list of strings", "unset", "{ \"web\", \"code\" }", none, none,
     "Names for workspaces 1, 2, ...; at most `workspaces` of them, each up to 32 characters, "
     "not a number and not repeated (\"\" leaves one unnamed). The panel shows them, and "
     "`workspace` and `move_to_workspace` (bindings with `workspace = \"web\"`, or "
     "`paw msg workspace web`) accept them."},
    {"layout.tiling", "boolean", "false", "false", none, none,
     "Start every monitor with automatic tiling; `outputs.monitors.<name>.tiling` overrides it."},
    {"layout.tiling_per_workspace", "boolean", "false", "false", none, none,
     "Toggling tiling (`toggle_tiling`, the panel button) turns it on or off for the current "
     "workspace only, instead of every workspace of the monitor; workspaces not toggled follow "
     "the monitor's setting."},
    {"layout.tile_layout", "enum", "\"dwindle\"", "\"dwindle\"", none, none,
     "Tiling layout: `\"dwindle\"`, `\"master\"`, `\"spiral\"`, `\"monocle\"`, or "
     "`\"scroll\"`."},
    {"layout.master_ratio", "number", "0.55", "0.55", 0.1, 0.9,
     "Share of the screen the master area takes in the master layout."},
    {"layout.master_count", "integer", "1", "1", 1, 8, "Windows in the master area."},
    {"layout.outputs", "table of tables", "unset", "", none, none,
     "Layout defaults per monitor, keyed by connector name or by `\"desc:\"` and the start of "
     "\"make model serial\" as in `outputs.monitors`, e.g. `[\"DP-1\"] = { tile_layout = "
     "\"scroll\" }`. They apply to the workspaces of that monitor that were not set by hand, "
     "and follow a reload and a monitor's return."},
    {"layout.outputs.<name>.tile_layout", "enum", "`layout.tile_layout`", "", none, none,
     "This monitor's tiling layout: `\"dwindle\"`, `\"master\"`, `\"spiral\"`, `\"monocle\"`, or "
     "`\"scroll\"`."},
    {"layout.outputs.<name>.master_ratio", "number", "`layout.master_ratio`", "", 0.1, 0.9,
     "This monitor's master ratio."},
    {"layout.outputs.<name>.master_count", "integer", "`layout.master_count`", "", 1, 8,
     "This monitor's master count."},
    {"layout.scroll", "table", "", "", none, none,
     "The scrolling layout: columns on an endless strip, the screen a view onto it."},
    {"layout.scroll.follow", "enum", "\"center\"", "\"edge\"", none, none,
     "How the view follows focus: `\"center\"` keeps the focused column centered, `\"edge\"` "
     "moves only as far as needed to show it, `\"never\"` moves only for scroll actions, new "
     "windows and `center_column`."},
    {"layout.scroll.width", "number", "0.5", "0.5", 0.1, 1,
     "Width of a new column, as a share of the screen."},
    {"layout.scroll.step", "number", "0.1", "0.1", 0.01, 0.5,
     "What `column_widen` and `column_narrow` add to or take from a column's width."},
    {"layout.scroll.presets", "list of numbers", "{ 1/3, 1/2, 2/3, 1 }",
     "{ 0.4, 0.6, 1.0 }", none, none,
     "Widths `column_cycle_width` steps through, each 0.1 to 1; at most 8."},

    {"outputs", "table", "", "", none, none, "Monitor arrangement."},
    {"outputs.order", "list of strings", "unset", "{ \"DP-1\", \"DP-2\" }", none, none,
     "Connector names from left to right; unlisted monitors follow on the right. At most "
     "8, without duplicates."},
    {"outputs.primary", "string", "leftmost", "\"DP-1\"", none, none,
     "Connector where the cursor starts."},
    {"outputs.return_windows", "boolean", "true", "false", none, none,
     "When a monitor is unplugged its windows move to the nearest one, keeping their workspace "
     "numbers; with this on they go back, to the same workspace and place, when it returns."},
    {"outputs.lid", "enum", "\"clamshell\"", "\"clamshell\"", none, none,
     "What closing a laptop's lid does: `\"clamshell\"` turns its built-in panel (eDP, LVDS, "
     "DSI) off while another monitor is on, its windows moving there as when it is unplugged, and "
     "back on as the lid opens; `\"ignore\"` leaves the panel on. With no other monitor, the "
     "panel stays on either way and logind decides whether the machine sleeps."},
    {"outputs.monitors", "table of tables", "unset", "", none, none,
     "Per-monitor settings, keyed by connector name or by `\"desc:\"` and the start of "
     "\"make model serial\", e.g. `[\"DP-3\"] = { mode = \"2560x1440@144\" }`."},
    {"outputs.monitors.<name>.enabled", "boolean", "true", "", none, none,
     "`false` turns the monitor off."},
    {"outputs.monitors.<name>.mode", "string", "preferred", "", none, none,
     "`\"WIDTHxHEIGHT\"` or `\"WIDTHxHEIGHT@HZ\"`, e.g. `\"2560x1440@144\"`."},
    {"outputs.monitors.<name>.scale", "number", "auto", "", 0.25, 10, "Output scale factor."},
    {"outputs.monitors.<name>.position", "table", "auto", "", none, none,
     "`{ x = N, y = N }` in layout pixels; both are required."},
    {"outputs.monitors.<name>.position.x", "integer", "", "", -65536, 65536, "Horizontal position."},
    {"outputs.monitors.<name>.position.y", "integer", "", "", -65536, 65536, "Vertical position."},
    {"outputs.monitors.<name>.transform", "integer", "0", "", 0, 7,
     "Rotation and flip, as `wl_output.transform`: 0 normal, 1-3 rotated 90/180/270, 4-7 the "
     "same flipped."},
    {"outputs.monitors.<name>.vrr", "boolean", "false", "", none, none,
     "Variable refresh rate."},
    {"outputs.monitors.<name>.tiling", "boolean", "unset", "", none, none,
     "Automatic tiling on this monitor; unset follows `layout.tiling`."},
    {"outputs.monitors.<name>.bit_depth", "integer", "8", "10", 8, 10,
     "Bits per colour channel the monitor is drawn in: 8, or 10 for smoother gradients on a "
     "monitor that takes them (a render format of `XRGB2101010`, else `XBGR2101010`). Where the "
     "monitor, its connection or the renderer refuses 10 bits it stays at 8."},
    {"outputs.monitors.<name>.hdr", "boolean", "false", "true", none, none,
     "Drive the monitor in HDR, BT.2020 colours with the PQ curve, where its EDID offers them, "
     "the renderer can convert colours (Vulkan alone in wlroots 0.20) and the test of its "
     "settings passes; else it stays SDR. Drawn in 10 bits where it can be. With any monitor "
     "asking for it, applications may describe their colours (color-management-v1)."},
    {"outputs.monitors.<name>.mirror", "string", "unset", "\"eDP-1\"", none, none,
     "Show another monitor's picture, a connector name or `\"desc:\"` as the keys are: this one "
     "leaves the layout (no workspaces, windows or panels of its own) and shows that monitor's "
     "whole picture scaled to fit, with black bars where the shapes differ. While that monitor "
     "is not in the layout, this one joins it."},

    {"windows", "table", "", "", none, none, "Window borders, opacity, and rules."},
    {"windows.border_width", "integer", "0", "0", 0, 20,
     "Border in pixels around each window; tiles shrink to keep it in their slot."},
    {"windows.corner_radius", "integer", "10", "10", 0, 40,
     "Radius in pixels of the corners of windows (which ones, `round` says) and of their "
     "border; 0 keeps them square. Needs wlroots built with paw's rounded-corners patch."},
    {"windows.round", "enum", "\"tiling\"", "\"always\"", none, none,
     "Which windows get rounded corners: `\"tiling\"`, those on a monitor with tiling on, "
     "floating ones included; `\"always\"`, every window, but for one on a monitor without "
     "tiling that draws a shadow of its own around it (a client-side frame, as GTK's), which "
     "keeps its own corners. Fullscreen and maximized windows stay square."},
    {"windows.border_color", "color or gradient", "\"#7da8ff\"", "\"#7da8ff\"", none, none,
     "Focused window border, `#RRGGBB` or `#RRGGBBAA`, or a gradient through 2 to 10 such "
     "colours spaced evenly, `{ \"#33ccff\", \"#00ff99\", angle = 45 }`: `angle` in degrees "
     "(-360 to 360, 0 by default) runs from left to right at 0 and from top to bottom at 90, "
     "measured on the border's box as if it were square."},
    {"windows.border_inactive_color", "color or gradient", "\"#404a5c\"", "\"#404a5c\"", none,
     none, "Other windows' border, as `border_color`."},
    {"windows.opacity", "number", "1.0", "1.0", 0.05, 1, "Focused window opacity."},
    {"windows.inactive_opacity", "number", "`opacity`", "1.0", 0.05, 1,
     "Unfocused window opacity."},
    {"windows.dim_inactive", "number", "0", "0.25", 0, 0.9,
     "How much darker windows without focus are, 0 for not at all: black laid over them, so it "
     "works for every application and tints nothing. Fades over `dim_duration`."},
    {"windows.allow_tearing", "boolean", "false", "true", none, none,
     "Let a fullscreen window that asks for it (tearing-control-v1, as games do) or that a rule's "
     "`allow_tearing` names show each frame at once, for the least latency, rather than at the "
     "monitor's next refresh, while nothing else shows on its monitor. The picture may tear."},
    {"windows.dim_duration", "integer", "180", "180", 0, 2000,
     "Milliseconds the dimming takes to fade in and out; 0, or `animations.enabled = false`, "
     "switches it at once."},
    {"windows.activation", "string", "\"urgent\"", "\"urgent\"", none, none,
     "What an unfocused application asking for attention (xdg-activation, an X11 urgency "
     "hint) gets: `\"urgent\"` marks the window (border, taskbar, workspace indicator) and "
     "leaves focus alone, `\"focus\"` focuses it and switches to its workspace, `\"ignore\"` "
     "drops the request. `focus_urgent` jumps to the oldest urgent window."},
    {"windows.urgent_color", "color", "\"#ff9e64\"", "\"#ff9e64\"", none, none,
     "Border of an urgent window, `#RRGGBB` or `#RRGGBBAA`; it pulses for a few seconds, "
     "also on windows without a `border_width`."},
    {"windows.swallow", "table", "", "", none, none,
     "Window swallowing: a window started from a terminal takes its place and hides it while it "
     "lives; closing the window brings the terminal back. Matched by process ancestry, so the "
     "application has to be a descendant of the terminal's process. `swallow_toggle` does it "
     "by hand."},
    {"windows.swallow.enabled", "boolean", "false", "true", none, none,
     "Swallow automatically as windows open; `swallow_toggle` works either way."},
    {"windows.swallow.terminals", "list of strings", "kitty, foot, footclient, Alacritty, "
     "org.wezfurlong.wezterm, com.mitchellh.ghostty, xterm, URxvt, org.kde.konsole, "
     "org.gnome.Terminal",
     "{ \"kitty\", \"foot\" }", none, none,
     "The `app_id`s (X11 class) of terminals that can be swallowed, compared without regard to "
     "case; at most 32. A window that is itself one of these never swallows."},
    {"windows.swallow.exceptions", "list of strings", "unset", "{ \"xdg-desktop-portal-gtk\" }",
     none, none, "`app_id`s that never swallow their terminal, at most 32."},
    {"windows.magnet", "table", "", "", none, none,
     "Magnetic edges: while a floating window is dragged or resized, its edges stick to the edges of the "
     "screen, of the area panels leave free, and of other windows nearby, and a guide line "
     "shows which one holds it. Tiles, and drags that will retile the window, are not "
     "affected; half-screen snapping and dropping at the top edge work as before."},
    {"windows.magnet.enabled", "boolean", "true", "true", none, none,
     "Turns the magnetism off with `false`."},
    {"windows.magnet.distance", "integer", "12", "12", 0, 200,
     "How near, in pixels, an edge has to come to be caught; it stays held until the pointer "
     "has moved that far away again."},
    {"windows.magnet.guides", "boolean", "true", "true", none, none,
     "Draw a line along the edge that holds the window."},
    {"windows.magnet.guide_color", "color", "\"#7da8ffb3\"", "\"#7da8ffb3\"", none, none,
     "Color of the guide line, `#RRGGBBAA`."},
    {"windows.magnet.bypass", "enum", "\"Shift\"", "\"Shift\"", none, none,
     "Holding this modifier while dragging turns the magnetism off: `Shift`, `Ctrl`, `Alt`, "
     "`Super`, or `none`."},
    {"windows.snap", "table", "", "", none, none,
     "Snapping by dragging: a floating window dropped with the pointer at the left or right "
     "edge of its monitor fills that half, in a corner that quarter, and at the top edge it is "
     "maximized; a translucent preview shows where while the pointer is there. Edges shared "
     "with another monitor do not snap. On a monitor that tiles, a window lifted out of the "
     "tiling splits the tile it is dropped on instead, but for the top edge."},
    {"windows.snap.enabled", "boolean", "true", "true", none, none,
     "`false` turns snapping by dragging off, at the top edge too; the snap actions still "
     "work."},
    {"windows.snap.distance", "integer", "8", "8", 1, 100,
     "How near, in pixels, the pointer has to come to an edge, or be on a panel along it."},
    {"windows.snap.corners", "boolean", "true", "true", none, none,
     "Corners snap into quarters; with `false` a corner belongs to the side edge."},
    {"windows.snap.preview", "boolean", "true", "true", none, none,
     "Show where the window will go while dropping it would snap it."},
    {"windows.snap.color", "color", "`border_color`, a quarter opaque", "\"#7da8ff40\"", none,
     none,
     "The preview's fill, `#RRGGBBAA`; its outline is the same colour, more opaque."},
    {"windows.snap.assist", "boolean", "true", "true", none, none,
     "Snap Assist: after a window snaps into a half or a quarter, by dragging or from the "
     "keyboard, the free part of the monitor beside it shows the monitor's other windows; "
     "picking one puts it there. Escape or a click elsewhere dismisses it. Not on monitors that "
     "tile."},
    {"windows.placement", "enum", "\"cascade\"", "\"smart\"", none, none,
     "Where a new floating window opens (tiles go where the layout puts them, and rules with a "
     "`position` win): `\"cascade\"` steps each one 32 pixels down and right, `\"center\"` "
     "opens it in the middle of the screen's free area, `\"smart\"` where it covers the other "
     "windows least, centered in the largest gap that holds it, and cascading when nothing is "
     "free."},
    {"windows.drag_strip", "integer", "6", "24", 0, 100,
     "Pixels along the top of a window without a title bar (kitty, X11 applications) that move "
     "it when dragged, as a title bar would; they no longer reach the application. 0 turns "
     "this off."},
    {"windows.controls", "enum", "\"flat\"", "\"traffic_lights\"", none, none,
     "How the controls of the windows paw decorates look; they show while the pointer is "
     "near their corner. `\"flat\"`: minimize, fullscreen and close buttons on a dark strip at "
     "the top-right. `\"traffic_lights\"`: close, minimize and fullscreen as red, yellow and "
     "green circles at the top-left, grey while the window has no focus, their symbols shown "
     "while the pointer is on one."},
    {"windows.shadow", "table", "", "", none, none,
     "A soft shadow under each window that draws none of its own (a client-side frame with a "
     "shadow keeps that one), following its rounded corners; none under fullscreen and "
     "maximized windows. It takes no input and is no part of the window's size or place."},
    {"windows.shadow.enabled", "boolean", "false", "true", none, none,
     "Draw the shadows."},
    {"windows.shadow.color", "color", "\"#00000059\"", "\"#00000059\"", none, none,
     "The shadow under the focused window where it is darkest, `#RRGGBBAA`."},
    {"windows.shadow.inactive_color", "color", "\"#00000033\"", "\"#00000033\"", none, none,
     "The shadow under other windows, `#RRGGBBAA`."},
    {"windows.shadow.blur", "integer", "30", "30", 0, 100,
     "How soft the shadow's edge is: it fades out over about this many pixels, as CSS's blur "
     "radius; 0 for a hard edge."},
    {"windows.shadow.offset", "integer or table", "10", "10", -50, 50,
     "How far the shadow falls below the window in pixels, or `{ x, y }` to move it sideways "
     "too (right and down)."},
    {"windows.buttons", "string", "\"appmenu:minimize,maximize,close\"",
     "\"appmenu:minimize,maximize,close\"", none, none,
     "GTK button layout for windows that draw their own frame; lowercase letters, `_`, `,` "
     "and `:` only. Restart to change."},
    {"windows.rules", "list of tables", "unset", "", none, none,
     "Per-application rules, at most 256. Opacity comes from the first matching rule that "
     "sets it; actions apply as a window opens, every matching rule in order, later ones "
     "winning. A rule needs `app_id` or `title`."},
    {"windows.rules[].app_id", "regex", "any", "", none, none,
     "ECMAScript regular expression searched in the application ID (X11: the class)."},
    {"windows.rules[].title", "regex", "any", "", none, none,
     "ECMAScript regular expression searched in the window title."},
    {"windows.rules[].opacity", "number", "", "", 0.05, 1, "Opacity of a matching focused window."},
    {"windows.rules[].inactive_opacity", "number", "`opacity`", "", 0.05, 1,
     "Opacity of a matching unfocused window."},
    {"windows.rules[].floating", "boolean", "unset", "", none, none, "Open floating or tiled."},
    {"windows.rules[].workspace", "integer", "unset", "", 1, 10,
     "Open on this workspace of its monitor, without switching there (at most "
     "`layout.workspaces`)."},
    {"windows.rules[].output", "string", "unset", "", none, none,
     "Open on this monitor: a connector, or `\"desc:\"` and the start of its description."},
    {"windows.rules[].size", "list or table", "unset", "", none, none,
     "`{ width, height }` or `{ width = W, height = H }`, 1 to 16384."},
    {"windows.rules[].position", "string or table", "unset", "", none, none,
     "`\"center\"`, or `{ x, y }` / `{ x = X, y = Y }` from the top-left of the monitor's "
     "usable area, -16384 to 16384."},
    {"windows.rules[].fullscreen", "boolean", "unset", "", none, none, "Open fullscreen."},
    {"windows.rules[].maximize", "boolean", "unset", "", none, none, "Open maximized."},
    {"windows.rules[].focus", "boolean", "unset", "", none, none,
     "`false` opens the window without focusing it."},
    {"windows.rules[].sticky", "boolean", "unset", "", none, none,
     "Show on every workspace of its monitor."},
    {"windows.rules[].shortcuts_inhibit", "boolean", "unset", "", none, none,
     "`false` refuses the window's requests for the keys the bindings take (see "
     "`keyboard.shortcuts_inhibit`)."},
    {"windows.rules[].above", "boolean", "unset", "", none, none,
     "Keep above the other windows, as `toggle_above` does."},
    {"windows.rules[].dynamic", "boolean", "false", "", none, none,
     "Hold the window to the rule's `above`, `floating` and `sticky` while its title and app ID "
     "match, as they change, and give back what it had once they no longer do; its other "
     "actions still act once, as the window opens."},
    {"windows.rules[].allow_tearing", "boolean", "unset", "", none, none,
     "With `windows.allow_tearing`, show the window's frames at once while it is fullscreen and "
     "alone on its monitor, whether or not it asks for that (as an X11 game cannot)."},

    {"peek", "table", "", "", none, none,
     "Peek: `peek` (hold a key) and `peek_toggle` make every window nearly transparent to show "
     "the desktop, with borders and the panel left alone. Resting on a window's picture on the "
     "taskbar's card fades every other window the same way."},
    {"peek.opacity", "number", "0.12", "0.12", 0, 0.9,
     "Opacity windows fade to while peeking; 0 hides them completely."},
    {"peek.duration", "integer", "150", "150", 0, 2000,
     "Milliseconds of the fade in and out; 0, or `animations.enabled = false`, switches at once."},

    {"night_light", "table", "", "", none, none,
     "Night light: the screen turns warm after sunset, easing over `transition` minutes. The "
     "actions `night_light_toggle`, `night_light_on`, `night_light_off` and `night_light_auto` "
     "override the schedule."},
    {"night_light.enabled", "boolean", "false", "false", none, none,
     "Follow the schedule below."},
    {"night_light.day_temperature", "integer", "6500", "6500", 1000, 10000,
     "Colour temperature in kelvin by day; 6500 leaves colours alone."},
    {"night_light.night_temperature", "integer", "3400", "3400", 1000, 10000,
     "Colour temperature in kelvin at night; lower is warmer."},
    {"night_light.sunrise", "string", "\"07:00\"", "\"07:00\"", none, none,
     "Time the screen turns cool again, as \"HH:MM\". Written out together with `sunset` it "
     "wins over the location."},
    {"night_light.sunset", "string", "\"20:00\"", "\"20:00\"", none, none,
     "Time the screen turns warm, as \"HH:MM\"."},
    {"night_light.latitude", "number", "unset", "59.9", -90, 90,
     "Degrees north; with `longitude` (set both) it gives sunrise and sunset for each day."},
    {"night_light.longitude", "number", "unset", "10.7", -180, 180,
     "Degrees east."},
    {"night_light.transition", "number", "30", "30", 0, 240,
     "Minutes over which the temperature changes, centred on sunrise and sunset."},

    {"hot_corners", "table", "", "", none, none,
     "Hot corners: pushing the pointer into a screen corner and leaving it there runs a request, "
     "written as for `paw msg` (an action name and its argument, or `spawn PROGRAM ARGS`). "
     "Nothing runs over a fullscreen window or while a button is held."},
    {"hot_corners.size", "integer", "2", "2", 1, 64,
     "Side of the corner square in pixels."},
    {"hot_corners.delay", "integer", "150", "150", 0, 5000,
     "Milliseconds the pointer must stay in the corner; it must leave before the corner can run again."},
    {"hot_corners.top_left", "string", "unset", "\"toggle_overview\"", none, none,
     "What the top-left corner runs, for instance `toggle_overview` or `workspace 2`."},
    {"hot_corners.top_right", "string", "unset", "\"spawn foot\"", none, none,
     "What the top-right corner runs."},
    {"hot_corners.bottom_left", "string", "unset", "\"peek_toggle\"", none, none,
     "What the bottom-left corner runs."},
    {"hot_corners.bottom_right", "string", "unset", "\"night_light_toggle\"", none, none,
     "What the bottom-right corner runs."},

    {"zoom", "table", "", "", none, none,
     "Screen magnifier: `zoom_in` and `zoom_out` change the magnification by one step, "
     "`zoom_reset` returns to 1x. The magnified view follows the pointer, which stays over what it "
     "clicks."},
    {"zoom.step", "number", "1.25", "1.25", 1.05, 4,
     "Factor of one `zoom_in` or `zoom_out` step."},
    {"zoom.max", "number", "8", "8", 1.5, 32, "Largest magnification."},
    {"zoom.duration", "integer", "150", "150", 0, 2000,
     "Milliseconds a zoom change takes; 0, or `animations.enabled = false`, switches at once."},
    {"zoom.scroll_modifier", "string", "\"\"", "\"Super\"", none, none,
     "Holding this modifier turns the scroll wheel into zoom steps (up zooms in); \"\" leaves the "
     "wheel alone. One of Alt, Super, Ctrl, Shift."},

    {"animations", "table", "", "", none, none, "Window and shell animations."},
    {"animations.enabled", "boolean", "true", "true", none, none,
     "Windows fade in and out and tiles glide into place; `false` also stills the shell's "
     "animations."},
    {"animations.duration", "integer", "120", "120", 10, 1000, "Animation length in milliseconds."},
    {"animations.speed", "number", "1.0", "1.0", 0.1, 10,
     "Speed multiplier for every animation, the shell's too: 2 makes them twice as fast, 0.5 "
     "half as fast."},
    {"animations.curve", "enum", "per kind", "\"ease-out\"", none, none,
     "Easing for every animation that does not set its own: `\"linear\"`, `\"ease-in\"`, "
     "`\"ease-out\"`, `\"ease-in-out\"`, `\"ease-out-quint\"`, `\"overshoot\"`, `\"spring\"`, or "
     "`\"bezier(x1, y1, x2, y2)\"` (x between 0 and 1, y between -2 and 3). Unset, moves use "
     "`\"spring\"`, workspace slides `\"ease-out-quint\"`, and the rest `\"ease-out\"`."},
    {"animations.late_frame_ms", "integer", "80", "80", 0, 1000,
     "A frame later than this finishes running animations instead of stuttering through them; "
     "0 never skips."},
    {"animations.open", "table", "", "", none, none, "A window appearing. Overrides the animation-wide settings."},
    {"animations.open.duration", "integer", "`duration`", "", 0, 1000,
     "Length in milliseconds; 0 turns this animation off."},
    {"animations.open.curve", "enum", "`curve`", "", none, none, "Easing curve, as `animations.curve`."},
    {"animations.close", "table", "", "", none, none, "A window going. Overrides the animation-wide settings."},
    {"animations.close.duration", "integer", "`duration`", "", 0, 1000,
     "Length in milliseconds; 0 turns this animation off."},
    {"animations.close.curve", "enum", "`curve`", "", none, none, "Easing curve, as `animations.curve`."},
    {"animations.move", "table", "", "", none, none, "A tile or window moving to a new place. Overrides the animation-wide settings."},
    {"animations.move.duration", "integer", "`duration`", "", 0, 1000,
     "Length in milliseconds; 0 turns this animation off."},
    {"animations.move.curve", "enum", "`curve`", "", none, none, "Easing curve, as `animations.curve`."},
    {"animations.workspace", "table", "", "", none, none, "The view sliding to another workspace. Overrides the animation-wide settings."},
    {"animations.workspace.duration", "integer", "`duration`", "", 0, 1000,
     "Length in milliseconds; 0 turns this animation off."},
    {"animations.workspace.curve", "enum", "`curve`", "", none, none, "Easing curve, as `animations.curve`."},
    {"animations.workspace.distance", "number", "0.08", "0.08", 0, 1,
     "How far windows slide when the workspace changes, as a share of the monitor's width; they "
     "fade as they go. 0 only fades. Large values slide over neighbouring monitors."},
    {"animations.fullscreen", "table", "", "", none, none, "A window entering or leaving fullscreen. Overrides the animation-wide settings."},
    {"animations.fullscreen.duration", "integer", "`duration`", "", 0, 1000,
     "Length in milliseconds; 0 turns this animation off."},
    {"animations.fullscreen.curve", "enum", "`curve`", "", none, none, "Easing curve, as `animations.curve`."},
    {"animations.focus", "table", "", "", none, none, "Border and opacity changing with focus. Overrides the animation-wide settings."},
    {"animations.focus.duration", "integer", "`duration`", "", 0, 1000,
     "Length in milliseconds; 0 turns this animation off."},
    {"animations.focus.curve", "enum", "`curve`", "", none, none, "Easing curve, as `animations.curve`."},

    {"overview", "table", "", "", none, none,
     "The overview: every window of a monitor's workspace as a live thumbnail, opened by the "
     "`toggle_overview` action."},
    {"overview.enabled", "boolean", "true", "true", none, none,
     "The overview actions work; `false` turns them off."},
    {"overview.gap", "integer", "24", "24", 0, 200, "Pixels between thumbnails."},
    {"overview.animation", "boolean", "true", "true", none, none,
     "Windows glide between their places and the grid as it opens and closes."},
    {"overview.duration", "integer", "180", "180", 10, 1000,
     "Milliseconds the opening and closing glide takes."},
    {"overview.strip", "boolean", "true", "true", none, none,
     "A strip of the monitor's workspaces above the grid, to switch to or to drop a window on."},
    {"overview.hot_corner", "enum", "\"none\"", "\"top-left\"", none, none,
     "Pushing the pointer into this screen corner opens the overview: `\"none\"`, "
     "`\"top-left\"`, `\"top-right\"`, `\"bottom-left\"`, or `\"bottom-right\"`."},
    {"overview.dim", "number", "0.86", "0.86", 0, 1,
     "How opaque the backdrop behind the thumbnails is, from 0 (clear) to 1."},

    {"gestures", "table", "", "", none, none,
     "Touchpad swipes the compositor takes for itself. Every other swipe, pinch and hold goes to "
     "the window under the pointer."},
    {"gestures.enabled", "boolean", "true", "false", none, none,
     "The swipes in `swipes` act; `false` hands every gesture to the windows."},
    {"gestures.distance", "integer", "300", "300", 50, 2000,
     "How far the fingers travel for a whole workspace, or for the overview to open, in the "
     "touchpad's units (about a pointer's pixels). Letting go past half of it, or flicking, "
     "finishes the step."},
    {"gestures.invert", "boolean", "false", "true", none, none,
     "Read swipes the other way: fingers moving left count as a swipe right, so the workspaces "
     "move against them."},
    {"gestures.swipes", "list of tables",
     "three fingers: left `workspace_next`, right `workspace_prev`, up `toggle_overview`, down "
     "`overview_cancel`", "", none, none,
     "The swipes the compositor takes, at most 16, in place of the defaults: a swipe of "
     "`fingers` toward `direction` runs `action`. `workspace_next` and `workspace_prev` slide "
     "the workspaces with the fingers, stopping at the first and the last; `toggle_overview` and "
     "`overview_cancel` open and close the overview with them; anything else runs as the "
     "fingers lift past half of `distance`."},
    {"gestures.swipes[].fingers", "integer", "3", "", 3, 5, "How many fingers swipe."},
    {"gestures.swipes[].direction", "enum", "", "", none, none,
     "`\"left\"`, `\"right\"`, `\"up\"` or `\"down\"`, the way the fingers go. No two swipes "
     "may share fingers and a direction."},
    {"gestures.swipes[].action", "string", "", "", none, none,
     "A request, written as for `paw msg` and `hot_corners`: an action name and its "
     "argument, or `spawn PROGRAM ARGS`; `\"none\"` takes the swipe and does nothing."},

    {"bindings", "list of tables", "unset", "", none, none,
     "Shortcuts, at most 512, checked in order. A binding has a `key` or a `button`, an "
     "`action`, and optionally `mods`. Keyboard bindings must not repeat a key and modifier "
     "combination."},
    {"bindings[].mods", "list of strings", "{}", "", none, none,
     "Up to four of `Alt`, `Super`, `Ctrl`, `Shift`, without repeats."},
    {"bindings[].key", "string", "", "", none, none,
     "An XKB keysym name, e.g. `\"q\"`, `\"Left\"`, `\"Print\"`, `\"minus\"`. Not with `button` "
     "or `switch`."},
    {"bindings[].button", "enum", "", "", none, none,
     "A mouse button: `left`, `right`, `middle`, `side`, `extra`, `forward`, `back`. Not "
     "with `key`."},
    {"bindings[].switch", "enum", "", "", none, none,
     "A switch: `\"lid\"`, a laptop's lid, or `\"tablet\"`, a convertible's tablet mode, which "
     "acts as `state` says it changes, as Hyprland's `bindl = , switch:...` does. Not with `key`, "
     "`button` or `mods`. It runs even while the session is locked."},
    {"bindings[].state", "enum", "", "", none, none,
     "With `switch` only, and needed there: `\"close\"` or `\"open\"` for the lid, `\"on\"` or "
     "`\"off\"` for tablet mode."},
    {"bindings[].app_id", "regex", "any", "", none, none,
     "Only with `button`: act only over windows whose app ID matches."},
    {"bindings[].desktop", "boolean", "false", "", none, none,
     "Only with `button`: also act over the bare desktop."},
    {"bindings[].action", "enum", "", "", none, none,
     "What the binding does; see the action list below. `\"none\"` unbinds (with `extends`) "
     "or hands a button click to the application."},
    {"bindings[].command", "list of strings", "", "", none, none,
     "With `spawn` only: the program and its arguments, never a shell string."},
    {"bindings[].workspace", "integer", "", "", 1, 10,
     "With `workspace` and `move_to_workspace` only: a number up to `layout.workspaces`, or a name "
     "from `layout.workspace_names`."},
    {"bindings[].output", "string", "\"next\" for `swap_workspaces`", "", none, none,
     "With `move_workspace_to_output` (required) and `swap_workspaces`: the other monitor, "
     "`\"left\"` or `\"right\"` of the focused one, `\"next\"` or `\"prev\"` in order with wrap, or "
     "a connector name or `\"desc:\"` description as in `outputs.monitors`. With `display_off`, "
     "`display_on` and `display_toggle`: the monitor, a connector name or `\"desc:\"` "
     "description; without one, every monitor."},
    {"bindings[].mode", "enum", "\"region\"", "", none, none,
     "With `screenshot`: `\"region\"`, `\"output\"`, or `\"window\"`. With `mode` (required): "
     "`\"default\"` or the name of one of `modes`. With `display_mode`: `\"extend\"`, "
     "`\"duplicate\"`, `\"internal\"` or `\"external\"`; without one, the popup that steps "
     "through them."},
    {"bindings[].amount", "integer", "40; 5 for volume and brightness", "", 1, 4000,
     "With `resize_*`: pixels moved per press. With `volume_up`, `volume_down`, `brightness_up` "
     "and `brightness_down`: percent per press, up to 100."},
    {"bindings[].layout", "string or integer", "\"next\"", "", none, none,
     "With `switch_layout` only: `\"next\"` or `\"prev\"` (wrapping), or a layout's number "
     "from 1, in the order of `keyboard.layout`."},
    {"bindings[].locked", "boolean", "false", "", none, none,
     "With `key` only: the binding runs while the session is locked too, as Hyprland's `bindl`; "
     "every other key goes to the lock screen then. An action with nothing to do while the "
     "session is locked (focusing, moving windows, the switcher, the overview) does nothing."},
    {"bindings[].repeats", "boolean", "true for `resize_*`, else false", "", none, none,
     "With `key` only: while the key is held, the action runs again at `keyboard.repeat_rate` "
     "after `keyboard.repeat_delay`, as Hyprland's `binde`. (Lua keeps `repeat` for its own "
     "loops.)"},

    {"notifications", "table", "", "", none, none,
     "Notifications: the shell serves `org.freedesktop.Notifications` on the session bus and shows "
     "each one as a card on the focused monitor; clicking a card runs its default action, "
     "hovering pauses its timer, and the clock's flyout keeps a history. Critical notifications "
     "stay until dismissed. The actions `dnd_toggle`, `dnd_on` and `dnd_off` (or `paw msg "
     "dnd toggle`) silence the cards."},
    {"notifications.enabled", "boolean", "true", "true", none, none,
     "Serve notifications. Turn it off to run another daemon such as mako or dunst."},
    {"notifications.position", "enum", "\"top-right\"", "\"top-right\"", none, none,
     "Corner of the focused monitor the cards stack from: `\"top-right\"`, `\"top-left\"`, "
     "`\"bottom-right\"` or `\"bottom-left\"`."},
    {"notifications.timeout", "integer", "6000", "6000", 0, 600000,
     "Milliseconds before a card goes when the application asked for the default; 0 keeps cards "
     "until dismissed. An application's own timeout wins, and critical ones never expire."},
    {"notifications.max_visible", "integer", "4", "4", 1, 10,
     "Cards shown at once; newer ones push the oldest into the history."},
    {"notifications.dnd", "boolean", "false", "false", none, none,
     "Start in do-not-disturb: no cards, but everything still reaches the history."},
    {"notifications.width", "integer", "360", "360", 200, 800, "Card width in pixels."},
    {"notifications.history", "integer", "100", "100", 0, 1000,
     "Notifications the clock's flyout keeps; 0 keeps none."},

    {"osd", "table", "", "", none, none,
     "On-screen display: a small pill near the bottom centre of the focused monitor that shows "
     "a label and a level for volume and brightness changes and for `paw msg osd TEXT "
     "[PERCENT]`, then fades out."},
    {"osd.enabled", "boolean", "true", "true", none, none, "Show the display."},
    {"osd.position", "enum", "\"bottom\"", "\"bottom\"", none, none,
     "`\"bottom\"` or `\"top\"` edge of the monitor."},
    {"osd.timeout", "integer", "1500", "1500", 200, 10000,
     "Milliseconds the display stays before it fades out."},
    {"osd.volume", "boolean", "true", "true", none, none,
     "Show it when the default output's volume or mute changes, by keys or from any application."},
    {"osd.brightness", "boolean", "true", "true", none, none,
     "Show it when a backlight's brightness changes."},

    {"power", "table", "", "", none, none,
     "Power controls: the `lock`, `suspend`, `hibernate`, `reboot`, `poweroff` and `logout` "
     "actions. All but `lock` and `logout` go through logind (systemd-logind or elogind)."},
    {"power.lock_command", "list of strings", "{ \"swaylock\", \"-f\" }", "{ \"gtklock\" }", none,
     none,
     "The screen locker `lock` starts, an `ext-session-lock-v1` client, as a program and its "
     "arguments; `{}` for none. Locking is offered only while the program is installed."},
    {"power.lock_before_sleep", "boolean", "true", "true", none, none,
     "Lock the screen with `lock_command` before the machine sleeps, whether `suspend`, "
     "`hibernate`, the lid or an idle daemon sends it to sleep, and hold the sleep until the "
     "lock holds."},
    {"power.close_windows", "boolean", "true", "true", none, none,
     "`poweroff`, `reboot` and `logout` first ask every window to close, as its close button "
     "would, and go ahead once all have, so that applications save their state."},
    {"power.close_timeout", "integer", "5000", "5000", 500, 60000,
     "Milliseconds the windows get to close. One still open then (an application asking "
     "whether to save) cancels the power off, unless `force` is set; answering it in time lets "
     "the power off go on."},
    {"power.countdown", "integer", "10", "10", 0, 300,
     "Seconds the shell's confirmation of power off, restart and log out counts down before "
     "it goes ahead (Escape, Cancel or a click beside it gives up); 0 waits for a click."},
    {"power.force", "boolean", "false", "false", none, none,
     "Go ahead with the power off, reboot or log out even when windows are still open after "
     "`close_timeout`; their applications are then ended without saving."},

    {"idle", "table", "", "", none, none,
     "Power saving after a while without input, as KDE and Windows have, with no idle daemon: "
     "the screens dim, the monitors turn off, the screen locks and the machine suspends, each "
     "a number of seconds after the last key press, click, scroll or pointer motion; 0 never "
     "takes that step. An idle inhibitor (a video playing, a game) holds every step off. "
     "`ext-idle-notify-v1` works as before, so an idle daemon such as swayidle runs beside "
     "these; `display_off = 0` leaves the monitors to it."},
    {"idle.dim", "integer", "`display_off` − 30", "570", 0, 86400,
     "Seconds before the screens darken by half, fading in; any input brings them back. "
     "Unset, 30 seconds before `display_off` (halfway, under a minute), or never without it."},
    {"idle.display_off", "integer", "600", "600", 0, 86400,
     "Seconds before every monitor turns off, as `display_off` does: they keep their windows "
     "and panels, and a key or the pointer turns them on again."},
    {"idle.lock", "integer", "0", "300", 0, 86400,
     "Seconds before the screen locks with `power.lock_command`."},
    {"idle.suspend", "integer", "0", "1800", 0, 86400,
     "Seconds before the machine suspends, as `suspend` does: through logind, locking first "
     "with `power.lock_before_sleep`."},
    {"idle.battery", "table", "unset", "", none, none,
     "The same steps while the machine runs on battery: a battery discharging and no mains or "
     "USB supply online in `/sys/class/power_supply`. Each one left out is the one above, but "
     "for `dim`, which follows this table's `display_off` when that is set."},
    {"idle.battery.dim", "integer", "`idle.dim`", "", 0, 86400, "Seconds before the screens dim."},
    {"idle.battery.display_off", "integer", "`idle.display_off`", "300", 0, 86400,
     "Seconds before the monitors turn off."},
    {"idle.battery.lock", "integer", "`idle.lock`", "", 0, 86400,
     "Seconds before the screen locks."},
    {"idle.battery.suspend", "integer", "`idle.suspend`", "900", 0, 86400,
     "Seconds before the machine suspends."},

    {"startup", "list of commands", "unset", "", none, none,
     "Commands started once when the compositor starts (not on reload), at most 32; each is "
     "an argument list, e.g. `{ { \"kitty\" } }`."},

    {"autostart", "table", "", "", none, none,
     "XDG autostart: the desktop entries applications put in `~/.config/autostart` and "
     "`/etc/xdg/autostart` to start at login. A standalone session (`--session`) starts them "
     "after `startup`; a nested one leaves them to the host, which has started them already. "
     "`paw msg get autostart` lists what was started or skipped, and why."},
    {"autostart.xdg", "boolean", "true", "true", none, none, "Start the autostart entries."},
    {"autostart.exclude", "list of strings", "unset", "{ \"org.kde.discover.notifier.desktop\" }",
     none, none,
     "File names of autostart entries never to start, at most 128, whichever directory holds "
     "them."},

    {"session", "table", "", "", none, none,
     "The last session: what a standalone session saves as it ends (log out, quit, power off, "
     "restart) and puts back as the next one starts, after `startup` and autostart."},
    {"session.restore", "enum", "\"windows\"", "\"launch\"", none, none,
     "`\"windows\"` puts the windows that open again back where they were: those already open "
     "and those `startup` and autostart start, matched by app ID and title, with each monitor's "
     "workspace and tiling. `\"launch\"` also starts the programs of the other windows again, "
     "by the command line they ran. `\"off\"` saves and restores nothing. The session is "
     "`last`, among those `paw msg session list` shows."},

    {"shell", "table", "", "", none, none, "The desktop shell: panel, wallpaper, launchers."},
    {"shell.enabled", "boolean", "true", "true", none, none,
     "Start the shell with the compositor."},
    {"shell.style", "enum", "\"taskbar\"", "\"macos\"", none, none,
     "How the shell is laid out: `\"taskbar\"` (one bar with a start menu, as on Windows) or "
     "`\"macos\"` (a menu bar along the top and a dock at the bottom, the panel settings "
     "applying to the dock)."},
    {"shell.panel_height", "integer", "52", "52", 24, 100, "Panel height in pixels."},
    {"shell.panel_position", "enum", "\"bottom\"", "\"bottom\"", none, none,
     "`\"top\"` or `\"bottom\"`."},
    {"shell.panel_margin", "integer or table", "0", "0", none, none,
     "Pixels around the panel, 0 to 200: one number, or `{ top, right, bottom, left }` by "
     "name, to make it float."},
    {"shell.panel_margin.top", "integer", "0", "0", 0, 200, "Top margin."},
    {"shell.panel_margin.right", "integer", "0", "0", 0, 200, "Right margin."},
    {"shell.panel_margin.bottom", "integer", "0", "0", 0, 200, "Bottom margin."},
    {"shell.panel_margin.left", "integer", "0", "0", 0, 200, "Left margin."},
    {"shell.panel_radius", "integer", "0", "0", 0, 50, "Panel corner radius."},
    {"shell.font", "string", "\"\"", "\"\"", none, none,
     "Font family; empty keeps the Qt default."},
    {"shell.font_size", "integer", "12", "12", 6, 48, "Taskbar text size in pixels."},
    {"shell.renderer", "enum", "\"gpu\"", "\"gpu\"", none, none,
     "How the shell draws: `\"gpu\"` (Qt Quick's OpenGL or Vulkan renderer, which effects such "
     "as shadows need) or `\"software\"` (the CPU, without effects, but it starts about three "
     "times faster and uses about half the memory, for a weak machine). Read at shell start, so "
     "a change needs a restart."},
    {"shell.icons_only", "boolean", "true", "true", none, none,
     "Taskbar buttons show only the window icon, the title as a tooltip."},
    {"shell.group_windows", "boolean", "true", "true", none, none,
     "One taskbar button per application; `false` gives every window its own."},
    {"shell.polkit_agent", "boolean", "true", "true", none, none,
     "Be the session's polkit authentication agent: a dialog asks for the password a program "
     "needs for what only an administrator may do (`pkexec`, GParted, an updater). It stays "
     "out of the way while another agent serves the session, and XDG autostart then leaves "
     "other agents out. `false` leaves it to another agent."},
    {"shell.thumbnails", "table", "", "", none, none,
     "Pictures of the windows on the taskbar and in the window switcher: resting the pointer on "
     "a button with windows shows a card beside it with a small picture of each, and Alt + Tab "
     "shows each window as a card with its picture, as Windows does. The taskbar style only; "
     "the macOS style's dock lists a stack's windows by name, and its switcher shows icons."},
    {"shell.thumbnails.enabled", "boolean", "true", "true", none, none,
     "Show the pictures; `false` gives a window's button its title as a tooltip again, a stack "
     "the list of its windows' titles, and the switcher its grid of icons."},
    {"shell.thumbnails.delay", "integer", "400", "400", 0, 2000,
     "Milliseconds the pointer rests on the button before the card opens."},
    {"shell.thumbnails.size", "integer", "240", "240", 120, 480,
     "Width of one picture in pixels, which is 5/8 as tall. With more windows than fit across "
     "the monitor they get narrower, down to 60 % of it, and past that the card lists the "
     "windows by title instead. The switcher's pictures are as tall, each as wide as its "
     "window's proportions make it, and get smaller, down to 60 %, when there are many."},
    {"shell.thumbnails.live", "boolean", "true", "true", none, none,
     "The pictures follow their windows while the card or the switcher is open, at most 30 "
     "times a second; `false` takes one of each window just before the card opens, or as the "
     "switcher opens."},
    {"shell.search", "table", "", "", none, none,
     "What the command palette's and the start menu's search find besides applications, "
     "windows, workspaces and actions."},
    {"shell.search.files", "boolean", "true", "true", none, none,
     "Find files and folders by name: those used lately (`recently-used.xbel`), and those in "
     "`directories`, read in the background the first time a search asks and again once they "
     "are five minutes old or something changed. A leading `/` searches only files."},
    {"shell.search.directories", "list of strings", "unset", "{ \"~/Documents\", \"~/Projects\" }",
     none, none,
     "The folders whose files are found, at most 32, absolute or starting with `~/`; unset reads "
     "the XDG user folders (Documents, Downloads, ...) and the home folder. Hidden files and "
     "folders, other file systems, and what is inside a version-controlled tree (`.git`), a "
     "build tree (`CMakeCache.txt`, `CACHEDIR.TAG`), `node_modules` or `__pycache__` are left "
     "out."},
    {"shell.search.depth", "integer", "4", "4", 1, 10,
     "How many levels of folders below each of `directories` are read."},
    {"shell.search.max_files", "integer", "20000", "20000", 100, 200000,
     "How many names are read at most, the folders nearest the top first."},
    {"shell.search.web", "string or false", "\"https://duckduckgo.com/?q=%s\"",
     "\"https://www.startpage.com/do/search?q=%s\"", none, none,
     "The search engine of the last result, Search the web for “…”, which opens the default "
     "browser at this http or https address, `%s` replaced by the words typed (URL-encoded); "
     "`false` leaves it out."},
    {"shell.clipboard", "table", "", "", none, none,
     "The history of what is copied, which `clipboard_history` (Super + Shift + V) opens, as "
     "Windows' Win + V does."},
    {"shell.clipboard.enabled", "boolean", "true", "true", none, none,
     "Keep the history, as a client of the compositor's data-control protocol. What a password "
     "manager marks as secret (`x-kde-passwordManagerHint`), and anything copied while the "
     "session is locked, is never kept; `false` keeps nothing."},
    {"shell.clipboard.max_entries", "integer", "50", "50", 1, 500,
     "How many entries are kept besides the pinned ones, the oldest going first."},
    {"shell.clipboard.images", "boolean", "true", "true", none, none,
     "Keep pictures copied too (PNG, JPEG and the like, up to 16 MB), not only text."},
    {"shell.clipboard.persist", "boolean", "false", "false", none, none,
     "Keep the history across sessions in `$XDG_STATE_HOME/paw/clipboard`, readable by "
     "the user alone; `false` keeps it in memory only, and removes that file."},
    {"shell.workspaces_shown", "integer", "0", "3", 0, 10,
     "How many of the monitor's workspaces the workspace indicator shows, the current one in "
     "the middle: `3` shows it with the one before and the one after, or the first or last "
     "three at either end. 0 shows them all. Scrolling still reaches every workspace."},
    {"shell.accent", "color", "\"#7da8ff\"", "\"#7da8ff\"", none, none,
     "Accent color, `#RRGGBB` or `#RRGGBBAA`."},
    {"shell.panel_color", "color", "\"#151e2c\"", "\"#151e2c\"", none, none,
     "Panel color; `#RRGGBBAA` makes it translucent."},
    {"shell.text_color", "color", "\"#edf2fa\"", "\"#edf2fa\"", none, none,
     "Panel text color, `#RRGGBB` or `#RRGGBBAA`."},
    {"shell.wallpaper", "string", "\"\"", "\"\"", none, none,
     "Image path, absolute or relative to the configuration file."},
    {"shell.wallpapers", "string", "unset", "\"~/Pictures/wallpapers\"", none, none,
     "The wallpaper picker's folder, searched with its subfolders: absolute, or starting with "
     "`~/`; unset uses `$XDG_PICTURES_DIR/wallpapers`, else `$XDG_PICTURES_DIR`."},
    {"shell.widgets", "table", "", "", none, none,
     "Panel widgets; each is switched off with `false`. One that can move takes `\"bar\"` to "
     "sit on the bar or `\"quick\"` to sit in the Quick Settings flyout instead, and `true` "
     "puts it in its default place."},
    {"shell.widgets.workspaces", "boolean", "true", "true", none, none,
     "This monitor's workspace numbers."},
    {"shell.widgets.battery", "boolean or string", "true", "\"quick\"", none, none,
     "Charge and state, only where a battery exists: `\"bar\"` or `\"quick\"` (along Quick "
     "Settings' foot). By default in Quick Settings."},
    {"shell.widgets.network", "boolean or string", "true", "\"quick\"", none, none,
     "Connection state, only where a network interface exists: `\"bar\"` or `\"quick\"` (a "
     "tile, and an icon on the Quick Settings button only while the link is down). Where "
     "NetworkManager has Wi-Fi, the tile turns it on and off and lists the networks to connect "
     "to, as a click on the bar's icon does. By default in Quick Settings."},
    {"shell.widgets.volume", "boolean or string", "true", "\"quick\"", none, none,
     "The default output's volume, only with a sound server: `\"bar\"` or `\"quick\"` (a "
     "slider, with the outputs and the applications' volumes). By default in Quick Settings."},
    {"shell.widgets.clock", "boolean", "true", "true", none, none, "The clock."},
    {"shell.widgets.calendar", "boolean", "true", "true", none, none,
     "The month calendar in the clock's flyout."},
    {"shell.widgets.tiling", "boolean or string", "true", "\"quick\"", none, none,
     "Tiling on or off for the monitor: `\"bar\"` (a button) or `\"quick\"` (a tile). By "
     "default on the bar."},
    {"shell.widgets.profiles", "boolean or string", "true", "\"quick\"", none, none,
     "The appearance profile picker, which lists `profiles` to switch between; shown only when "
     "there are two or more: `\"bar\"` (a button) or `\"quick\"` (a tile). By default in "
     "Quick Settings."},
    {"shell.widgets.wallpapers", "boolean or string", "true", "\"bar\"", none, none,
     "The wallpaper picker: thumbnails of the images in `shell.wallpapers`; the one picked "
     "replaces `shell.wallpaper` for the profile in use until the configured one changes. "
     "`\"bar\"` (a button) or `\"quick\"` (a tile that opens the same picker). By default on "
     "the bar."},
    {"shell.widgets.keyboard_layout", "boolean", "true", "true", none, none,
     "The active keyboard layout's short name, such as `us`; clicking it switches to the next. "
     "Shown only when the keymap has two or more layouts."},
    {"shell.widgets.power", "boolean", "true", "true", none, none,
     "The power button in the application menu's bottom-right corner: lock, suspend, "
     "hibernate, restart, power off and log out, as far as `power.lock_command` and logind "
     "allow them."},
    {"shell.widgets.tray", "boolean", "true", "true", none, none,
     "The system tray: applications' status icons (StatusNotifierItem), shown while there are "
     "any. `false` also leaves the tray's D-Bus names to another program."},
    {"shell.widgets.notifications", "boolean or string", "true", "\"bar\"", none, none,
     "Do-not-disturb and the unread notifications: `\"bar\"` (a bell that opens them, with "
     "their count; a right-click toggles do-not-disturb) or `\"quick\"` (a do-not-disturb "
     "tile). The clock does both either way. By default in Quick Settings."},
    {"shell.widgets.media", "boolean", "true", "true", none, none,
     "The now-playing card at the top of Quick Settings: the cover, title and artist of the "
     "media player playing most lately (MPRIS), with previous, play or pause, next and the "
     "position; shown while a player is there."},
    {"shell.widgets.power_mode", "boolean", "true", "true", none, none,
     "The power mode tile in Quick Settings: Power saver, Balanced or Performance, through "
     "power-profiles-daemon; shown while the daemon runs."},
    {"shell.widgets.bluetooth", "boolean", "true", "true", none, none,
     "The Bluetooth tile in Quick Settings, through BlueZ: it turns the adapter on and off, and "
     "lists the paired devices to connect and those in range to pair with; shown while BlueZ has "
     "an adapter."},
    {"shell.launchers", "list of tables", "unset", "", none, none,
     "Pinned commands for programs without a desktop file, at most 64."},
    {"shell.launchers[].name", "string", "", "", none, none, "Label, 1 to 128 bytes. Required."},
    {"shell.launchers[].icon", "string", "\"application-x-executable\"", "", none, none,
     "Icon theme name."},
    {"shell.launchers[].command", "list of strings", "", "", none, none,
     "Program and arguments. Required."},

    {"screenshots", "table", "", "", none, none, "Screenshot bindings' output."},
    {"screenshots.directory", "string", "unset", "\"~/Pictures/Screenshots\"", none, none,
     "Absolute, or starting with `~/`; unset uses `$XDG_PICTURES_DIR/Screenshots`."},
    {"screenshots.clipboard", "boolean", "true", "true", none, none,
     "Also copy the image with `wl-copy`."},
    {"screenshots.notify", "boolean", "true", "true", none, none,
     "Announce the file with `notify-send`, when it is installed."},

    {"features", "table", "", "", none, none,
     "Optional behaviours, each a boolean."},
    {"features.workspace_back_and_forth", "boolean", "false", "false", none, none,
     "The `workspace` action for the workspace already shown switches back to the previous one."},
    {"features.scratchpad", "boolean", "true", "true", none, none,
     "The `move_to_scratchpad` and `scratchpad_show` actions."},
    {"features.sticky", "boolean", "true", "true", none, none, "The `toggle_sticky` action."},
    {"features.keyboard_resize", "boolean", "true", "true", none, none,
     "The `resize_left/right/up/down` actions."},
    {"features.window_rules", "boolean", "true", "true", none, none,
     "The actions of `windows.rules`; opacity rules apply either way."},
    {"features.groups", "boolean", "true", "true", none, none,
     "The `group_toggle`, `group_next`, `group_prev`, `ungroup` and `group_merge_*` actions; off dissolves every group."},
    {"features.group_join_new", "boolean", "true", "true", none, none,
     "A window that opens while a window group has focus joins that group as a new tab."},
};

bool is_child(std::string_view parent, std::string_view path) {
    if (parent.empty())
        return path.find_first_of(".[") == std::string_view::npos;
    if (path.size() <= parent.size() + 1 || path.substr(0, parent.size()) != parent ||
        path[parent.size()] != '.')
        return false;
    return path.substr(parent.size() + 1).find_first_of(".[") == std::string_view::npos;
}
// Edit distance where swapping two neighbouring letters counts as one edit.
size_t distance(const std::string &a, const std::string &b) {
    std::vector<std::vector<size_t>> d(a.size() + 1, std::vector<size_t>(b.size() + 1));
    for (size_t i = 0; i <= a.size(); ++i)
        d[i][0] = i;
    for (size_t j = 0; j <= b.size(); ++j)
        d[0][j] = j;
    for (size_t i = 1; i <= a.size(); ++i)
        for (size_t j = 1; j <= b.size(); ++j) {
            d[i][j] = std::min({d[i - 1][j] + 1, d[i][j - 1] + 1,
                                d[i - 1][j - 1] + (a[i - 1] != b[j - 1])});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1])
                d[i][j] = std::min(d[i][j], d[i - 2][j - 2] + 1);
        }
    return d[a.size()][b.size()];
}
} // namespace

std::span<const Option> config_options() { return options; }

std::vector<const Option *> config_children(const std::string &parent) {
    std::vector<const Option *> result;
    for (const auto &option : options)
        if (is_child(parent, option.path))
            result.push_back(&option);
    return result;
}

std::vector<std::string> config_modifier_names() { return {"Alt", "Super", "Ctrl", "Shift"}; }
std::vector<std::string> config_button_names() {
    return {"left", "right", "middle", "side", "extra", "forward", "back"};
}

std::string closest_match(const std::string &word, const std::vector<std::string> &candidates) {
    auto lower = [](std::string text) {
        for (auto &c : text)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text;
    };
    std::string best;
    size_t best_distance = SIZE_MAX;
    auto wanted = lower(word);
    for (const auto &candidate : candidates) {
        auto d = distance(wanted, lower(candidate));
        if (d < best_distance) {
            best_distance = d;
            best = candidate;
        }
    }
    // A typo is a couple of edits, fewer for short words.
    if (best_distance <= std::min<size_t>(2, std::max<size_t>(1, word.size() / 3)))
        return best;
    return {};
}

std::string config_reference_markdown() {
    std::ostringstream out;
    out << "<!-- Generated from src/config_schema.cpp by tests/config_diagnostics_tests.cpp. Do not edit by\n"
           "     hand: change the schema, then run `PAW_UPDATE_DOCS=1 ctest -R config`. -->\n"
           "# Configuration reference\n\n"
           "paw reads a Lua file that returns a table (`config/init.lua` is a commented "
           "example). Every setting is optional. An unknown setting, a value of the wrong type, "
           "or a value out of range is an error naming the file and line, with a suggestion "
           "for a misspelled name; `paw --check-config` reports it. The running desktop "
           "reloads the file when it is saved; one with an error loads the default "
           "configuration instead and shows the error across the top of the screen until "
           "the file is fixed.\n\n"
           "Ranges are inclusive. Colors are `\"#RRGGBB\"` (with `#RRGGBBAA` where noted).\n";
    std::string section = "?"; // nothing yet
    auto flush_section = [&](const Option &option) {
        std::string top = option.path;
        top = top.substr(0, top.find_first_of(".["));
        bool general = top == option.path && std::string(option.type) != "table" &&
                       std::string(option.type) != "list of tables" &&
                       std::string(option.type) != "list of commands";
        if (general)
            top = "";
        if (top == section)
            return;
        section = top;
        out << "\n## " << (top.empty() ? "General" : "`" + top + "`") << "\n\n"
            << "| Setting | Type | Default | Range | Description |\n"
            << "| --- | --- | --- | --- | --- |\n";
    };
    for (const auto &option : options) {
        flush_section(option);
        std::string range;
        if (option.min != option.max) {
            std::ostringstream text;
            text << option.min << " to " << option.max;
            range = text.str();
        }
        std::string path = option.path;
        out << "| `" << path << "` | " << option.type << " | "
            << (option.default_value[0] ? option.default_value : "-") << " | "
            << (range.empty() ? "-" : range) << " | " << option.description << " |\n";
    }
    out << "\n## Binding actions\n\n";
    bool first = true;
    for (const auto &name : config_action_names()) {
        out << (first ? "" : ", ") << '`' << name << '`';
        first = false;
    }
    out << "\n\n`spawn` needs `command`; `workspace` and `move_to_workspace` need `workspace`; "
           "`screenshot` takes `mode` and `mode` needs it; `resize_*`, `volume_up`, `volume_down`, "
           "`brightness_up` and `brightness_down` take `amount`; a `switch` needs `state`.\n\n"
           "Modifiers: ";
    first = true;
    for (const auto &name : config_modifier_names()) {
        out << (first ? "" : ", ") << '`' << name << '`';
        first = false;
    }
    out << ".\n";
    return out.str();
}
} // namespace paw
