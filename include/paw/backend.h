// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "paw/curve.h"

#ifdef __cplusplus
extern "C" {
#endif

/* No wlroots types cross this boundary. C++ owns configuration and policy. */
enum sh_action {
    SH_NONE,
    SH_SPAWN,    /* starts a program, through sh_callbacks.launch */
    SH_TERMINAL, /* starts the configured terminal, or one that is installed; also launch */
    SH_QUIT,
    SH_CLOSE,
    SH_CYCLE,
    SH_SNAP_LEFT,
    SH_SNAP_RIGHT,
    SH_MAXIMIZE,
    SH_RESTORE,
    SH_TILE,
    SH_RELOAD,
    SH_FULLSCREEN,
    SH_WORKSPACE,         /* argument: workspace number, from 1 */
    SH_MOVE_TO_WORKSPACE, /* argument: workspace number, from 1 */
    SH_WORKSPACE_NEXT,
    SH_WORKSPACE_PREV,
    SH_TOGGLE_TILING,
    SH_TOGGLE_FLOATING,
    SH_LAUNCHER, /* asks the shell to toggle its application menu */
    SH_FOCUS_LEFT,
    SH_FOCUS_RIGHT,
    SH_FOCUS_UP,
    SH_FOCUS_DOWN,
    SH_SCREENSHOT, /* argument: enum sh_screenshot_mode */
    /* Hyprland's movewindow: a tile trades places with its neighbour, a floating window moves
     * to the edge of its output, and either one moves on to the next output from there. */
    SH_MOVE_LEFT,
    SH_MOVE_RIGHT,
    SH_MOVE_UP,
    SH_MOVE_DOWN,
    SH_WORKSPACE_BACK, /* the output's previously shown workspace, as sway's back_and_forth */
    /* Sway's scratchpad: hide the focused window there, or show (and cycle) the hidden ones. */
    SH_MOVE_TO_SCRATCHPAD,
    SH_SCRATCHPAD_SHOW,
    /* As sway's sticky: the window floats and shows on every workspace of its output. */
    SH_TOGGLE_STICKY,
    /* Keyboard resizing, argument: pixels. A tile's split on that side moves that way (else the
     * one on the opposite side); a floating window's right or bottom edge moves that way. */
    SH_RESIZE_LEFT,
    SH_RESIZE_RIGHT,
    SH_RESIZE_UP,
    SH_RESIZE_DOWN,
    /* The window switcher (Alt+Tab) over every window on every output and workspace, most
     * recently focused first: opens on the focused output, or selects the next or previous
     * window while open. Bound to a key, it focuses the selected window once the binding's
     * modifiers are released; opened from the control socket, it waits for switcher_confirm. */
    SH_SWITCHER_NEXT,
    SH_SWITCHER_PREV,
    SH_SWITCHER_CONFIRM, /* argument: the window's place in the list from 1, or 0: the selected */
    SH_SWITCHER_CANCEL,
    /* Tiling layouts of the focused output's current workspace. */
    SH_LAYOUT_NEXT,
    SH_LAYOUT_PREV,
    SH_SET_LAYOUT_DWINDLE, /* these five are in the order of enum sh_tile_layout */
    SH_SET_LAYOUT_MASTER,
    SH_SET_LAYOUT_SPIRAL,
    SH_SET_LAYOUT_MONOCLE,
    SH_SET_LAYOUT_SCROLL,
    SH_PROMOTE,     /* the focused tile swaps with the first, or with the second if it is first */
    SH_FOCUS_NEXT,  /* the next and previous tile in order, wrapping */
    SH_FOCUS_PREV,
    SH_SWAP_NEXT,   /* the focused tile trades places with the next or previous one */
    SH_SWAP_PREV,
    SH_MASTER_GROW, /* the master and spiral ratio, 5 percent of the area at a time */
    SH_MASTER_SHRINK,
    SH_MASTER_MORE, /* one more or fewer window in the master column */
    SH_MASTER_LESS,
    /* Focuses the window focused before the current one, on any workspace or output; repeated,
     * it flips between two windows. */
    SH_FOCUS_LAST,
    /* Focuses the window that has been urgent the longest (see windows.activation), on any
     * workspace or output; nothing happens when none is. */
    SH_FOCUS_URGENT,
    /* The overview (Expose): every window of the focused output's workspace as a live
     * thumbnail in a grid, with the workspaces in a strip above. Toggling opens or closes it;
     * confirm (argument: the thumbnail's place from 1, or 0: the selected one) focuses a
     * window; cancel closes it without changing anything. */
    SH_OVERVIEW_TOGGLE,
    SH_OVERVIEW_CONFIRM,
    SH_OVERVIEW_CANCEL,
    /* The scrolling layout: focus the column to the left or right; widen, narrow, or cycle the
     * width of the focused column through the presets; move the focused window into the column
     * on its left or right, or out of a stack into a column of its own; center the view on
     * the focused column. */
    SH_SCROLL_LEFT,
    SH_SCROLL_RIGHT,
    SH_COLUMN_WIDEN,
    SH_COLUMN_NARROW,
    SH_COLUMN_CYCLE_WIDTH,
    SH_CONSUME_LEFT,
    SH_CONSUME_RIGHT,
    SH_EXPEL,
    SH_CENTER_COLUMN,
    /* Peek: every window turns almost transparent to show the desktop. Bound to a key it lasts
     * while the key is held; from the control socket it toggles, like peek_toggle. */
    SH_PEEK,
    SH_PEEK_TOGGLE,
    /* Window groups: several windows share one slot (a tile, or a floating place) and show
     * one at a time under a strip of tabs. Toggle makes the focused window a group, or
     * dissolves its group; next and prev show another member; ungroup takes the focused window
     * out into a slot of its own; merge moves the focused window into the group of the window
     * beside it, that way (left, right, up, down in this order). */
    SH_GROUP_TOGGLE,
    SH_GROUP_NEXT,
    SH_GROUP_PREV,
    SH_UNGROUP,
    SH_GROUP_MERGE_LEFT,
    SH_GROUP_MERGE_RIGHT,
    SH_GROUP_MERGE_UP,
    SH_GROUP_MERGE_DOWN,
    /* Night light: flip between warm and neutral, force either, or go back to the schedule. */
    SH_NIGHT_LIGHT_TOGGLE,
    SH_NIGHT_LIGHT_ON,
    SH_NIGHT_LIGHT_OFF,
    SH_NIGHT_LIGHT_AUTO,
    /* Asks the shell for its command palette on the output under the pointer: one search over
     * windows, applications, workspaces, actions and saved sessions. */
    SH_PALETTE,
    /* Magnifier: one step in or out, or back to 1x. The view follows the pointer. */
    SH_ZOOM_IN,
    SH_ZOOM_OUT,
    SH_ZOOM_RESET,
    /* Window swallowing: the focused window takes the place of the terminal it was started
     * from and hides it, or, when it already swallowed one, gives the terminal a place beside
     * it again. */
    SH_SWALLOW_TOGGLE,
    /* The keyboard layout every keyboard but the virtual ones types in: with argument 0 the
     * next one of the keymap, -1 the previous (both wrapping), N > 0 the Nth. */
    SH_SWITCH_LAYOUT,
    /* Workspaces between outputs. The target (see sh_callbacks.action_target) is "left" or
     * "right" (the next output that way), "next" or "prev" (in order, wrapping), a connector
     * name, or "desc:" and the start of a description. move_workspace_to_output sends the
     * focused output's workspace there, trading places with the workspace of the same number
     * there; swap_workspaces trades every workspace of the two outputs, and the screens. */
    SH_MOVE_WORKSPACE_TO_OUTPUT,
    SH_SWAP_WORKSPACES,
    /* Do-not-disturb of the shell's notification daemon, and its history popover on the output
     * under the pointer. */
    SH_DND_TOGGLE,
    SH_DND_ON,
    SH_DND_OFF,
    SH_NOTIFICATION_HISTORY,
    /* Power, through logind (systemd-logind or elogind); logout ends the session as quit does. */
    SH_POWER_OFF,
    SH_REBOOT,
    SH_SUSPEND,
    SH_HIBERNATE,
    SH_LOGOUT,
    SH_LOCK, /* starts power.lock_command */
    /* Asks the shell for its power menu on the output under the pointer. */
    SH_POWER_MENU,
    /* Asks the shell to give the keyboard to the taskbar (or the dock) on the focused output, to
     * walk its buttons and their windows with the arrows, as Windows' Win+T; again, to leave. */
    SH_TASKBAR_FOCUS,
    /* Snapping into a quarter of the output, as SH_SNAP_LEFT and SH_SNAP_RIGHT into a half. */
    SH_SNAP_TOP_LEFT,
    SH_SNAP_TOP_RIGHT,
    SH_SNAP_BOTTOM_LEFT,
    SH_SNAP_BOTTOM_RIGHT,
    /* Windows' Win+arrow: one step of the focused window through the halves, quarters and
     * maximized state of its output, and on to the next output; see sh_snap_cycle. */
    SH_SNAP_CYCLE_LEFT,
    SH_SNAP_CYCLE_RIGHT,
    SH_SNAP_CYCLE_UP,
    SH_SNAP_CYCLE_DOWN,
    /* The keyboard's volume, microphone and brightness keys: the default sound output's volume
     * up or down (argument: percent), its mute and the default input's, the backlight up or down
     * (argument: percent). The shell carries them out and shows them; without one, wpctl and
     * brightnessctl do. */
    SH_VOLUME_UP,
    SH_VOLUME_DOWN,
    SH_VOLUME_MUTE,
    SH_MIC_MUTE,
    SH_BRIGHTNESS_UP,
    SH_BRIGHTNESS_DOWN,
    /* Binding modes: argument 0 brings back the bindings outside any mode, N those of the
     * configuration's Nth mode (see sh_callbacks.set_mode). */
    SH_MODE,
    /* Monitors off and on without leaving the layout: every one, or the one the action's target
     * names (sh_callbacks.action_target: a connector, or "desc:" and the start of a description).
     * Toggling turns them all off while any of them is on. */
    SH_DISPLAY_OFF,
    SH_DISPLAY_ON,
    SH_DISPLAY_TOGGLE,
    /* The media keys: the shell sends them to the media player playing most lately (MPRIS). */
    SH_MEDIA_PLAY_PAUSE,
    SH_MEDIA_NEXT,
    SH_MEDIA_PREVIOUS,
    SH_MEDIA_STOP,
    /* Turns off the focused surface's keyboard shortcuts inhibitor, which gives the compositor's
     * bindings their keys back, or on again. Its binding runs while the inhibitor holds every
     * other one. */
    SH_TOGGLE_SHORTCUTS_INHIBIT,
    /* Keeps the focused window above the others (floating ones and tiles), or lets it go back
     * among them: KDE's Keep Above Others, Hyprland's pin, PowerToys' Always on Top. */
    SH_TOGGLE_ABOVE,
    /* Windows' Win+P: every monitor extending the desktop, all showing one picture, the built-in
     * panel alone or the other monitors alone (enum sh_display_mode); without one, a popup that
     * steps through the four. */
    SH_DISPLAY_MODE,
    /* Asks the shell for its clipboard history on the output under the pointer, as Windows'
     * Win+V. */
    SH_CLIPBOARD_HISTORY,
    /* Asks the shell for its emoji picker on the output under the pointer, as Windows' Win+. */
    SH_EMOJI_PICKER,
    /* Asks the shell for its display settings window on the output under the pointer. */
    SH_DISPLAY_SETTINGS,
};

/* The display_mode action's choices. */
enum sh_display_mode {
    SH_DISPLAY_MODE_STEP,      /* the popup: open it, or step to the next choice */
    SH_DISPLAY_MODE_EXTEND,    /* every monitor in the layout */
    SH_DISPLAY_MODE_DUPLICATE, /* the others mirroring the built-in panel (or the primary) */
    SH_DISPLAY_MODE_INTERNAL,  /* the built-in panel (or the primary) alone */
    SH_DISPLAY_MODE_EXTERNAL,  /* every monitor but that one */
};

enum sh_screenshot_mode {
    SH_SCREENSHOT_REGION, /* the user selects it with slurp */
    SH_SCREENSHOT_OUTPUT, /* the output under the pointer */
    SH_SCREENSHOT_WINDOW, /* the focused window */
};

/* Modifier and edge bits intentionally match wlroots, without importing its headers. */
enum sh_modifier { SH_SHIFT = 1, SH_CTRL = 4, SH_ALT = 8, SH_LOGO = 64 };
enum sh_edge { SH_EDGE_TOP = 1, SH_EDGE_BOTTOM = 2, SH_EDGE_LEFT = 4, SH_EDGE_RIGHT = 8 };

/* Settings for one output, matched by connector name, or by "desc:" and the start of its
 * "make model serial". Zero fields keep the defaults. */
struct sh_monitor {
    char name[128];
    bool enabled;
    int width, height; /* 0: the preferred resolution */
    int refresh;       /* mHz; 0: the fastest at that resolution */
    float scale;       /* 0: 1 */
    bool positioned;   /* x, y are layout coordinates before the primary output shift */
    int x, y;
    int transform; /* enum wl_output_transform, which Hyprland's numbering matches */
    bool vrr;      /* adaptive sync, where the monitor supports it */
    int tiling;    /* automatic tiling: -1 follows sh_settings.tiling, else 0 or 1 */
    /* The monitor it shows the picture of, out of the layout itself: a connector name or
     * "desc:..." as `name` is; "" to join the layout. */
    char mirror[64];
    int bit_depth; /* bits per colour channel it is drawn in: 8, or 10 where the output and the
                    * renderer allow it; 0: 8 */
    bool hdr; /* driven in BT.2020 with PQ where the monitor, the renderer and the test allow */
};

/* layout.outputs: defaults for the workspaces of the outputs matching `name` (as sh_monitor's
 * name); tile_layout is negative, master_ratio and master_count 0, when unset. */
struct sh_output_layout {
    char name[64];
    int tile_layout;
    float master_ratio;
    int master_count;
};

/* Lua `peek`, `night_light`, `hot_corners` and `zoom`. */
struct sh_effect_settings {
    float peek_opacity; /* what windows fade to while peeking, 0 to 0.9 */
    int peek_duration;  /* milliseconds */
    /* night_light: minutes after midnight for sunrise and sunset, or -1 to take them from the
     * location (`located`) for the day. */
    bool night_light;
    int day_kelvin, night_kelvin;
    double sunrise, sunset;
    double latitude, longitude;
    bool located;
    double transition; /* minutes */
    /* hot_corners: the corner square in pixels, how long the pointer must stay in it in
     * milliseconds, and a bit per corner (sh_corner order) that has something to run. */
    int corner_size, corner_delay;
    unsigned corner_mask;
    /* zoom: the factor of one step, the largest level, the fade in milliseconds, and the
     * modifiers that turn the wheel into steps (0 for none). */
    float zoom_step, zoom_max;
    int zoom_duration;
    unsigned zoom_scroll_modifier;
};

/* Lua `gestures`: touchpad swipes the compositor takes for itself (swipe.h). A swipe of
 * `fingers` toward `direction` (enum sh_swipe_direction) runs `request`, a control request whose
 * action is `action`; workspace_next and workspace_prev slide the workspaces with the fingers,
 * toggle_overview and overview_cancel open and close the overview with them. */
struct sh_swipe_binding {
    int fingers;
    int direction;
    enum sh_action action;
    char request[256];
};
struct sh_gesture_settings {
    bool enabled;
    int distance; /* the fingers' travel, in libinput's units, for a whole step */
    bool invert;  /* swipes count the other way */
    struct sh_swipe_binding swipes[16];
    int swipe_count;
};

/* Lua `idle`: how long without input, in milliseconds, before each step of power saving; 0 for
 * never. */
struct sh_idle_steps {
    int dim;         /* the screens darken, until the next input */
    int display_off; /* every monitor turns off, staying in the layout */
    int lock;        /* the screen locks with power.lock_command */
    int suspend;     /* the machine suspends, locking first */
};

/* A border's colours: one colour, or a linear gradient through 2 to SH_GRADIENT_STOPS colours
 * spaced evenly, at `angle` degrees (0 runs from left to right, 90 from top to bottom), measured
 * on the frame's box as if it were square; premultiplied RGBA. */
enum { SH_GRADIENT_STOPS = 10 };
struct sh_gradient {
    float stops[SH_GRADIENT_STOPS][4];
    int count; /* 1 for one colour */
    float angle;
};

/* Lua outputs.lid: what closing a laptop's lid does. CLAMSHELL turns its built-in panel off
 * while another monitor shows the desktop, as unplugging the panel would, and back on as the lid
 * opens; IGNORE leaves the panel on. */
enum sh_lid_mode { SH_LID_CLAMSHELL, SH_LID_IGNORE };
/* The switches bindings act on: a laptop's lid (on: closed) and tablet mode (on: folded). */
enum sh_switch { SH_SWITCH_LID, SH_SWITCH_TABLET };

struct sh_settings {
    float background[4];
    uint32_t mouse_modifier;
    int repeat_rate;
    int repeat_delay;
    int gap_inner; /* between neighbouring windows */
    int gap_outer; /* between windows and the edges of the usable area */
    bool smart_gaps; /* a workspace's only tile has no gaps */
    /* layout.floating_above_tiles: floating windows always stack over the tiles of their output,
     * as in sway and Hyprland; off, the window raised last is on top, tile or not. */
    bool floating_above_tiles;
    char keyboard_layout[128];
    char keyboard_variant[128];
    char keyboard_model[128];
    char keyboard_options[128];
    char keyboard_rules[64]; /* "" for xkbcommon's default, "evdev" */
    /* keyboard.file as an absolute path: an XKB keymap used instead of the names above, which
     * stand in when it does not compile; "" for none. */
    char keyboard_file[1024];
    bool xwayland; /* read at startup; changing it needs a restart */
    bool tiling;   /* automatic tiling on outputs without their own; toggled per output */
    bool tiling_per_workspace; /* toggling tiling turns it on or off for one workspace */
    int tile_layout;     /* enum sh_tile_layout of workspaces that have not chosen another */
    float master_ratio;  /* share of the width of the master column and of a spiral's first tile */
    int master_count;    /* windows in the master column */
    int scroll_follow;   /* enum sh_scroll_follow */
    float scroll_width;  /* share of the width new columns of the scrolling layout take */
    float scroll_step;   /* what column_widen and column_narrow add or remove */
    float scroll_presets[8]; /* the widths column_cycle_width steps through */
    int scroll_preset_count;
    int workspaces;
    /* Outputs named here sit left to right in this order; others follow as they appear.
     * The primary output (or the leftmost, if unnamed) sits at the layout origin. */
    char output_order[8][32];
    int output_count;
    char primary_output[32];
    bool return_windows; /* windows return to an output that is plugged back in */
    struct sh_monitor monitors[8];
    int monitor_count;
    /* layout.outputs */
    struct sh_output_layout output_layouts[8];
    int output_layout_count;
    /* Drawn outside each window's geometry; placed windows shrink to keep it in their slot. */
    int border_width;
    /* Radius of the corners of tiled windows and their border; 0 keeps them square. */
    int corner_radius;
    /* windows.round = "always": windows are rounded on outputs without tiling too, but for
     * those that draw a shadow of their own. */
    bool round_always;
    float border_active[4], border_inactive[4]; /* premultiplied RGBA */
    /* The same, or gradients of which those are the first colours. */
    struct sh_gradient border_active_gradient, border_inactive_gradient;
    /* Pointer devices (libinput only). A negative value keeps the device's own default. */
    double pointer_speed; /* -1 to 1; used when pointer_speed_set */
    bool pointer_speed_set;
    int pointer_accel;        /* -1 default, 0 flat, 1 adaptive */
    int mouse_natural_scroll; /* -1, 0, 1 */
    int touchpad_natural_scroll, touchpad_tap, touchpad_dwt;
    bool focus_follows_mouse; /* hovering a window focuses it, without raising it */
    bool animations;          /* windows fade in and out, and tiles glide into place */
    float animation_speed;    /* multiplies the speed of every animation */
    int animation_late_ms;    /* a later frame finishes running animations; 0 never skips */
    float animation_slide;    /* workspace slide distance, as a share of the output's width */
    struct sh_anim_style animation_styles[SH_ANIM_KINDS]; /* resolved per kind */
    /* features.workspace_back_and_forth: SH_WORKSPACE naming the workspace already shown
     * switches back to the previous one, as sway's workspace_auto_back_and_forth. */
    bool workspace_back_and_forth;
    /* Lua `features`: optional behaviour that can be switched off. */
    bool scratchpad; /* move_to_scratchpad and scratchpad_show */
    /* features.sticky: toggle_sticky pins windows to every workspace of their output. Off
     * unsticks every sticky window. */
    bool sticky;
    bool keyboard_resize; /* features.keyboard_resize: the resize_* actions */
    /* features.window_rules: the actions of windows.rules (see sh_window_rule). Opacity rules
     * apply either way. */
    bool window_rules;
    /* features.groups: the group_* actions. Off dissolves every group. */
    bool groups;
    /* features.group_join_new: a window that opens while a group has focus joins it. */
    bool group_join_new;
    /* Lua `overview`. */
    bool overview;          /* the overview actions work */
    int overview_gap;       /* pixels between thumbnails */
    bool overview_animation; /* windows glide between their places and the grid */
    int overview_duration;  /* milliseconds */
    bool overview_strip;    /* a strip of the workspaces above the grid */
    int overview_hot_corner; /* 0 none, else 1 top-left, 2 top-right, 3 bottom-left, 4 bottom-right */
    float overview_dim;     /* opacity of the backdrop behind the thumbnails */
    /* windows.dim_inactive: black laid over windows without focus, this opaque (0: none), fading
     * in and out over dim_duration milliseconds. */
    float dim_inactive;
    int dim_duration;
    /* windows.allow_tearing: a fullscreen window that asks for it (tearing-control-v1, or a
     * rule's allow_tearing) is shown at once, tearing, while it is alone on its monitor. */
    bool allow_tearing;
    /* windows.activation: what a client asking for attention (xdg-activation, an X11 urgency
     * hint) gets. */
    int activation; /* enum sh_activation */
    float urgent_color[4]; /* the border of an urgent window, premultiplied RGBA */
    struct sh_effect_settings effects;
    /* windows.swallow: a window started from one of the terminals (matched by app_id, without
     * regard to case) takes the terminal's place and hides it while it lives. Exceptions are
     * app_ids that never swallow. */
    bool swallow;
    char swallow_terminals[32][64];
    int swallow_terminal_count;
    char swallow_exceptions[32][64];
    int swallow_exception_count;
    /* windows.magnet: while a floating window is dragged its edges stick to the edges of the
     * output, of its usable area and of other windows within `magnet_distance` pixels. The
     * `magnet_bypass` modifier (0: none) held during the drag turns it off; guides draw a line
     * along the edge that holds the window. */
    bool magnet;
    int magnet_distance;
    bool magnet_guides;
    uint32_t magnet_bypass;
    float magnet_guide_color[4]; /* premultiplied RGBA */
    /* windows.snap: a floating window dropped with the pointer within `snap_distance` pixels of
     * a side of its output fills that half, of two sides that quarter (with `snap_corners`), of
     * the top it is maximized (see sh_snap_zone); while it would, a preview in `snap_color` shows
     * where (with `snap_preview`). */
    bool snap;
    int snap_distance;
    bool snap_corners;
    bool snap_preview;
    float snap_color[4]; /* premultiplied RGBA */
    /* windows.snap.assist: after a window snaps into a half or a quarter, by the pointer or the
     * keyboard, the free part of its output beside it lists the output's other windows to fill
     * it with (Windows' Snap Assist). */
    bool snap_assist;
    /* windows.placement: enum sh_place_mode, where new floating windows open. */
    int placement;
    /* windows.drag_strip: how many pixels along the top of a window without a title bar move
     * it when dragged. */
    int drag_strip;
    /* windows.controls: enum sh_window_controls, how the controls of the windows the compositor
     * decorates look. */
    int window_controls;
    /* windows.shadow: a soft shadow under each window that draws none of its own, `shadow_blur`
     * pixels soft (as CSS's blur radius) and offset by (shadow_x, shadow_y), in shadow_color
     * under the focused window and shadow_inactive_color under others (premultiplied RGBA). */
    bool shadow;
    int shadow_blur;
    int shadow_x, shadow_y;
    float shadow_color[4], shadow_inactive_color[4];
    /* power.lock_before_sleep: the screen locks with power.lock_command before the machine
     * sleeps. */
    bool lock_before_sleep;
    /* power.close_windows: poweroff, reboot and logout ask every window to close first, and go
     * ahead once all have; after close_timeout milliseconds with some still open they give up,
     * or with close_force go ahead anyway. */
    bool close_windows;
    int close_timeout;
    bool close_force;
    /* session.restore: enum sh_session_restore, what a login session saves as it ends and puts
     * back as the next starts. */
    int session_restore;
    struct sh_gesture_settings gestures;
    /* touch.output: the output touchscreens are mapped to, by connector name or "desc:" and the
     * start of its description; "" for the one the device names, else a built-in panel, else
     * the whole layout. */
    char touch_output[128];
    /* tablet.output: the output drawing tablets are mapped to, as touch_output names one; ""
     * for the whole layout. */
    char tablet_output[128];
    /* idle: the steps on mains, and while the machine runs on battery. */
    struct sh_idle_steps idle, idle_battery;
    int lid; /* enum sh_lid_mode, outputs.lid */
    /* keyboard.shortcuts_inhibit: applications may ask for the keys the bindings take
     * (keyboard-shortcuts-inhibit), as virtual machines and remote desktops do. */
    bool shortcuts_inhibit;
};

/* What a login session keeps of the last one: nothing; where its windows were, for those that open
 * again (from startup, autostart or by hand); or that, and the programs missing started again. */
enum sh_session_restore {
    SH_SESSION_RESTORE_OFF,
    SH_SESSION_RESTORE_WINDOWS,
    SH_SESSION_RESTORE_LAUNCH,
};

/* What a mouse button was pressed over. */
enum sh_pointer_target {
    SH_POINTER_WINDOW,
    SH_POINTER_DESKTOP, /* no window, panel, or other surface but the wallpaper */
    SH_POINTER_OTHER,   /* a panel, bar, or other layer surface */
};

/* What windows.rules decide for a window as it opens. */
enum sh_rule_position { SH_RULE_POSITION_UNSET, SH_RULE_POSITION_CENTER, SH_RULE_POSITION_AT };
struct sh_window_rule {
    int floating;      /* -1 unset, 0 tile, 1 float */
    int workspace;     /* from 1; 0 unset */
    char output[128];  /* connector, or "desc:" and the start of "make model serial"; "" unset */
    int width, height; /* 0 unset */
    enum sh_rule_position position;
    int x, y; /* with SH_RULE_POSITION_AT, from the top-left of the output's usable area */
    bool fullscreen, maximize;
    bool no_focus;
    bool sticky; /* only with features.sticky */
    bool no_shortcuts_inhibit; /* shortcuts_inhibit = false: its inhibitors are refused */
    bool above;  /* kept above the other windows */
    bool allow_tearing; /* shown at once while fullscreen, with windows.allow_tearing */
};

/* What the dynamic window rules (`dynamic = true`) matching a window decide now, as its title
 * and app ID change: each -1 when none decides it, else 0 or 1. */
struct sh_dynamic_rule {
    int floating, sticky, above;
};

/* What a key binding asks for besides its action (sh_callbacks.binding_flags). */
enum sh_binding_flag {
    SH_BINDING_LOCKED = 1,  /* it runs while the session is locked too */
    SH_BINDING_REPEATS = 2, /* it runs again while its key is held */
};

struct sh_rect;
struct sh_callbacks {
    void *userdata;
    const struct sh_settings *(*settings)(void *);
    /* Returns the bound action; *argument receives its numeric argument, if any. */
    enum sh_action (*key)(void *, uint32_t modifiers, uint32_t keysym, int *argument);
    /* The same for a pressed mouse button (a Linux BTN_* code) over `target`; `app_id` is the
     * window's, or "". SH_NONE leaves the click to the application. */
    enum sh_action (*button)(void *, uint32_t modifiers, uint32_t button,
                             enum sh_pointer_target target, const char *app_id, int *argument);
    /* Parses a control-socket request into an action; SH_NONE with a message on error. */
    enum sh_action (*command)(void *, const char *request, int *argument, char *error,
                              size_t error_size);
    bool (*reload)(void *);
    void (*startup)(void *);
    void (*child_exited)(void *, int pid);
    /* Opacity for a window with this app ID and title (either may be ""), focused or not. */
    float (*opacity)(void *, const char *app_id, const char *title, bool active);
    /* Captures the screen in the background: `output` names the output for
     * SH_SCREENSHOT_OUTPUT, `box` is the window in layout coordinates for SH_SCREENSHOT_WINDOW.
     * Returns false with a message when it cannot start. */
    bool (*screenshot)(void *, enum sh_screenshot_mode mode, const char *output,
                       const struct sh_rect *box, char *error, size_t error_size);
    /* The rules for a window opening with this app ID and title (either may be ""). Returns
     * false, leaving `rule` unset, when none applies. */
    bool (*window_rule)(void *, const char *app_id, const char *title,
                        struct sh_window_rule *rule);
    /* What a hot corner (sh_corner order) runs, as a control request; like `command`, SH_NONE
     * means it could not be run. */
    enum sh_action (*hot_corner)(void *, int corner, int *argument);
    /* The output target of the action that key, button, command or hot_corner returned last,
     * for SH_MOVE_WORKSPACE_TO_OUTPUT and SH_SWAP_WORKSPACES; "" when it named none. */
    const char *(*action_target)(void *);
    /* A descriptor that turns readable when configuration files change, or -1 for none; asked
     * once at startup. */
    int (*config_watch)(void *);
    /* Reads what is pending on that descriptor; true when a configuration file changed and the
     * configuration should be reloaded. */
    bool (*config_changed)(void *);
    /* Whether power.lock_command can lock the screen: one is set and its program is installed
     * (as of the configuration's last load). With `start`, also starts it. False, with the
     * reason, when it cannot. */
    bool (*lock)(void *, bool start, char *error, size_t error_size);
    /* Starts the program of the SH_SPAWN that key, button, command or hot_corner returned
     * last, or for SH_TERMINAL the terminal. False, with the reason, when it cannot. */
    bool (*launch)(void *, enum sh_action action, char *error, size_t error_size);
    /* What XDG autostart did as the session started, for `get autostart`: a line per desktop
     * entry with its file name, "started", "skipped" or "failed", the command it ran or why
     * not, and its file, separated by tabs. "" when autostart did not run. */
    const char *(*autostart)(void *);
    /* Whether the window of a saved session with this app ID, whose process ran `program` (the
     * first word of its command line; either may be ""), is one that a program startup,
     * autostart or --exec started at startup will open again. */
    bool (*started)(void *, const char *app_id, const char *program);
    /* The sh_binding_flag bits of the binding that `key` returned last. */
    unsigned (*binding_flags)(void *);
    /* Makes the bindings of mode `mode` (0: those outside any mode, N: the configuration's Nth
     * mode) the ones `key` looks in, until another mode or a reload. Returns the mode's name
     * ("default" for 0), or NULL, changing nothing, when there is no such mode. */
    const char *(*set_mode)(void *, int mode);
    /* The action bound to a switch turning on (a lid closing, tablet mode starting) or off, as
     * `key` returns one; SH_NONE when none is. */
    enum sh_action (*switch_toggled)(void *, enum sh_switch type, bool on, int *argument);
    /* The keys of the first key binding of `action` outside any mode, as "Super + Shift +
     * Escape"; "" when no key is bound to it. Valid until the next call. */
    const char *(*binding_keys)(void *, enum sh_action action);
    /* What the dynamic rules matching a window with this app ID and title (either may be "")
     * decide now; every field -1 when none matches. */
    void (*dynamic_rule)(void *, const char *app_id, const char *title,
                         struct sh_dynamic_rule *rule);
};

enum sh_backend_mode { SH_BACKEND_NESTED, SH_BACKEND_HEADLESS, SH_BACKEND_SESSION };
int sh_run(const struct sh_callbacks *callbacks, enum sh_backend_mode mode);

struct sh_rect {
    int x, y, width, height;
};
bool sh_placement(enum sh_action action, struct sh_rect area, int gap, int index, int count,
                  struct sh_rect *result);
/* Where a window dragged with the pointer at (x, y) snaps when dropped there, `area` being the
 * output less its panels (a panel along an edge counts as that edge): SH_SNAP_LEFT or
 * SH_SNAP_RIGHT within `distance` pixels of that side, SH_MAXIMIZE of the top, and with
 * `corners` a quarter (SH_SNAP_TOP_LEFT, ...) within `distance` of two sides at once, where
 * without them the side wins. The bottom alone snaps nothing, and neither does an edge in
 * `shared` (enum sh_edge bits), which leads on to another output. SH_NONE where nothing
 * snaps. */
enum sh_action sh_snap_zone(struct sh_rect area, double x, double y, int distance, bool corners,
                            uint32_t shared);
/* What one press of a snap_cycle action (`direction`, SH_SNAP_CYCLE_LEFT to _DOWN) does to a
 * window arranged as `from`: SH_SNAP_LEFT, SH_SNAP_RIGHT, a quarter or SH_MAXIMIZE, anything else
 * (SH_NONE) for one at its own size. As Windows' Win+arrow: left and right go to that half, from
 * the other half back to its own size, and from that half on to the next output; up and down go
 * from a half to its top or bottom quarter and back, up from a top quarter or at its own size to
 * maximized, down from maximized to its own size, and from a bottom quarter or its own size to
 * minimized. Quarters keep their row going sideways. Sets *to to the arrangement it is given
 * (the one on the next output for SH_SNAP_STEP_NEXT_OUTPUT). */
enum sh_snap_step {
    SH_SNAP_STEP_STAY,
    SH_SNAP_STEP_PLACE,       /* arrange it as *to on its output */
    SH_SNAP_STEP_NEXT_OUTPUT, /* arrange it as *to on the next output that way */
    SH_SNAP_STEP_RESTORE,     /* back to its own size and place */
    SH_SNAP_STEP_MINIMIZE,
};
enum sh_snap_step sh_snap_cycle(enum sh_action from, enum sh_action direction,
                                enum sh_action *to);
/* The quarters of its output an arrangement covers, a bit each: 1 top-left, 2 top-right,
 * 4 bottom-left, 8 bottom-right; both of its side for a half, and none for anything else
 * (maximized included). */
unsigned sh_snap_quarters(enum sh_action arrangement);
/* The slot Snap Assist offers beside a window just snapped as `snapped` (a half or a quarter),
 * when the quarters in `taken` (sh_snap_quarters bits) hold windows snapped there: for a half,
 * the other half, else its top quarter, else its bottom one; for a quarter, the quarter beside
 * it in its row, else the one above or below it, else the one across. SH_NONE when every one of
 * those is taken, or `snapped` is not a half or a quarter. */
enum sh_action sh_snap_assist_slot(enum sh_action snapped, unsigned taken);

/* Where a new floating window of `width` x `height` opens in `area` (the output less its
 * panels), given the rectangles `others` of the windows already showing there. Cascade steps
 * the window 32 pixels down and right for each `index` (0, 1, 2, ..., wrapping every 8),
 * center centers it, and smart puts it where it overlaps the others least, centered in the
 * free space (in the largest gap that holds it), and cascades when nothing is free. Only the
 * position of `result` is set from the mode; its size is the one given. */
enum sh_place_mode { SH_PLACE_CASCADE, SH_PLACE_CENTER, SH_PLACE_SMART };
bool sh_place_window(enum sh_place_mode mode, struct sh_rect area, const struct sh_rect *others,
                     int other_count, int width, int height, int index, struct sh_rect *result);

/* Automatic tiling in the style of Hyprland's dwindle layout: one binary split tree per output
 * name and workspace. Each split divides its box along the longer side; a new window splits an
 * existing one. Windows are opaque pointers owned by the caller. */
struct sh_tiling;
struct sh_tiling *sh_tiling_create(void);
void sh_tiling_destroy(struct sh_tiling *tiling);
/* Splits `target` if it is tiled, else the window last arranged under the point (with
 * has_point), else the newest window of that tree. With a point inside the split window, the new
 * window takes the half nearer the point; otherwise the right or bottom half. */
void sh_tiling_insert(struct sh_tiling *tiling, const char *output, int workspace, void *window,
                      const void *target, bool has_point, double x, double y);
void sh_tiling_remove(struct sh_tiling *tiling, const void *window);
/* The output name of the window's tree, or NULL when it is not tiled. */
const char *sh_tiling_output(const struct sh_tiling *tiling, const void *window);
/* How many windows the tree of `workspace` on `output` holds; a window group is one. */
int sh_tiling_count(const struct sh_tiling *tiling, const char *output, int workspace);
typedef void (*sh_tile_place)(void *userdata, void *window, struct sh_rect rect);
void sh_tiling_arrange(struct sh_tiling *tiling, const char *output, int workspace,
                       struct sh_rect area, int gap, sh_tile_place place, void *userdata);
/* The tile `window` would get from sh_tiling_insert and sh_tiling_arrange with these arguments,
 * leaving the tree as it was. False when the window is already tiled or the area is empty. */
bool sh_tiling_preview(struct sh_tiling *tiling, const char *output, int workspace, void *window,
                       const void *target, bool has_point, double x, double y, struct sh_rect area,
                       int gap, struct sh_rect *result);
/* Moves the splits beside the window's given edges (enum sh_edge bits) to those edges of
 * `rect`, in the coordinates of the last arrangement. Returns whether anything changed. */
bool sh_tiling_resize(struct sh_tiling *tiling, const void *window, uint32_t edges,
                      struct sh_rect rect);
/* Keyboard resizing: moves one split beside the window `amount` pixels toward `direction` (one
 * enum sh_edge bit). The split on that side of the window moves outward, growing it; a window
 * with no split on that side (at the edge of its output) moves the one on the opposite side
 * instead, shrinking it. Splits keep the limits of sh_tiling_resize. Returns whether anything
 * changed. */
bool sh_tiling_resize_by(struct sh_tiling *tiling, const void *window, uint32_t direction,
                         int amount);

/* Layouts. Dwindle is the tree itself; the others arrange the tree's windows as a list in
 * order, ignoring where new windows are dropped (they join the end): MASTER puts the first
 * `count` windows in a column on the left, `ratio` of the width, and the rest stacked in a
 * column beside it; SPIRAL gives each window `ratio` of what the earlier ones left, turning
 * clockwise; MONOCLE gives every window the whole area. Each output and workspace has its
 * own, starting at the defaults. Nothing is arranged until sh_tiling_arrange. */
enum sh_tile_layout {
    SH_LAYOUT_DWINDLE,
    SH_LAYOUT_MASTER,
    SH_LAYOUT_SPIRAL,
    SH_LAYOUT_MONOCLE,
    SH_LAYOUT_SCROLL,
    SH_LAYOUT_COUNT,
};
/* SCROLL lays the windows in columns on an endless strip, in the style of niri and PaperWM:
 * a column has a width (a share of the area) and stacks its windows evenly, and the area is
 * a viewport onto the strip that follows the focused window. New windows open in a column
 * to the right of the focused one. */
/* What an unfocused client that asks for attention gets: URGENT marks the window (border,
 * taskbar, workspace indicator) and leaves focus alone; FOCUS focuses it, switching workspace
 * if need be; IGNORE drops the request. */
enum sh_activation {
    SH_ACTIVATION_URGENT,
    SH_ACTIVATION_FOCUS,
    SH_ACTIVATION_IGNORE,
};
/* The window controls of the windows the compositor decorates: FLAT buttons on a dark strip
 * at the top-right corner, or TRAFFIC_LIGHTS, three coloured circles at the top-left. */
enum sh_window_controls { SH_CONTROLS_FLAT, SH_CONTROLS_TRAFFIC_LIGHTS };
enum sh_scroll_follow {
    SH_SCROLL_FOLLOW_CENTER, /* the focused column is always centered */
    SH_SCROLL_FOLLOW_EDGE,   /* the view moves only as far as needed to show it */
    SH_SCROLL_FOLLOW_NEVER,  /* only scroll actions, new windows and center_column move it */
};
void sh_tiling_set_defaults(struct sh_tiling *tiling, enum sh_tile_layout layout, double ratio,
                            int count);
/* Trades everything two workspaces hold: their windows and splits, chosen layout, ratio and
 * count, and scroll columns. Choices not made stay with the output's defaults. False when they
 * are the same workspace. */
bool sh_tiling_exchange(struct sh_tiling *tiling, const char *output_a, int workspace_a,
                        const char *output_b, int workspace_b);
/* Defaults for the workspaces of one output, in place of the global ones: a layout (negative
 * for none), a master ratio (0 or less for none) and a master count (0 for none). Workspaces that
 * chose their own layout, ratio or count by hand keep it; the rest follow at once. */
void sh_tiling_set_output_defaults(struct sh_tiling *tiling, const char *output, int layout,
                                   double ratio, int count);
void sh_tiling_clear_output_defaults(struct sh_tiling *tiling);
/* What a workspace of `output` has unless it chose otherwise; NULL arguments are skipped. */
void sh_tiling_output_defaults(const struct sh_tiling *tiling, const char *output,
                               enum sh_tile_layout *layout, double *ratio, int *count);
enum sh_tile_layout sh_tiling_layout(const struct sh_tiling *tiling, const char *output,
                                     int workspace);
void sh_tiling_set_layout(struct sh_tiling *tiling, const char *output, int workspace,
                          enum sh_tile_layout layout);
/* Steps forward (or backward, when negative) through the layouts; returns the new one. */
enum sh_tile_layout sh_tiling_cycle_layout(struct sh_tiling *tiling, const char *output,
                                           int workspace, int step);
/* Adds to the ratio (kept between 0.1 and 0.9) and the master count (1 to 8). Returns whether
 * either changed. */
bool sh_tiling_adjust(struct sh_tiling *tiling, const char *output, int workspace,
                      double ratio_delta, int count_delta);
double sh_tiling_ratio(const struct sh_tiling *tiling, const char *output, int workspace);
int sh_tiling_master_count(const struct sh_tiling *tiling, const char *output, int workspace);
/* Trades the places of two windows of the same tree. False when either is untiled or they
 * are in different trees. */
bool sh_tiling_swap(struct sh_tiling *tiling, const void *a, const void *b);
/* Puts `replacement` (not tiled) into the slot of the tiled window `old_window`, which leaves the
 * tiling: a window group shows one member at a time in one slot. False when nothing changed. */
bool sh_tiling_replace(struct sh_tiling *tiling, const void *old_window, void *replacement);
/* The window `step` places after `window` in its tree's order, wrapping; NULL when it is alone
 * or untiled. */
void *sh_tiling_neighbour(const struct sh_tiling *tiling, const void *window, int step);
/* Scrolling layout settings: how the view follows focus, the width of new columns, the step of
 * column_widen and column_narrow, and the presets column_cycle_width steps through (up to 8,
 * kept between 0.1 and 1). Shares are of the area's width. */
void sh_tiling_set_scroll(struct sh_tiling *tiling, enum sh_scroll_follow follow, double width,
                          double step, const float *presets, int preset_count);
/* Tells the layout which window has focus. Returns whether the scrolling view may have to move,
 * so the caller arranges again. */
bool sh_tiling_set_focus(struct sh_tiling *tiling, const void *window);
/* The column of `window` counted from 0 (and with `row` its place in the stack), or -1 when it
 * is not tiled in the scrolling layout. */
int sh_tiling_scroll_column(struct sh_tiling *tiling, const void *window, int *row);
/* The widths of the columns of a workspace that tiles with the scrolling layout, left to right;
 * returns how many were written (at most `max`), 0 for another layout. */
int sh_tiling_scroll_widths(struct sh_tiling *tiling, const char *output, int workspace,
                            double *widths, int max);
/* Sets the columns of a workspace from a saved arrangement: window i goes to column columns[i],
 * row rows[i] (both from 0; equal columns share one, in the order of their rows), each column
 * taking widths[its saved number] when there is one. Windows not tiled there are ignored, and
 * tiles not listed keep their columns to the right of these. Returns whether it changed. */
bool sh_tiling_scroll_restore(struct sh_tiling *tiling, const char *output, int workspace,
                              void *const *windows, const int *columns, const int *rows,
                              int count, const double *widths, int width_count);
/* The window `columns` columns to the right of `window` (negative: left) at the same place in
 * its stack, or `rows` down the same column, without wrapping; NULL when there is none or the
 * layout is not scrolling. The view moves to show it once it is focused. */
void *sh_tiling_scroll_step(struct sh_tiling *tiling, const void *window, int columns, int rows);
/* Trades the column of `window` with its neighbour on the left (step -1) or right (1). */
bool sh_tiling_scroll_move(struct sh_tiling *tiling, const void *window, int step);
/* SH_COLUMN_*, SH_CONSUME_*, SH_EXPEL and SH_CENTER_COLUMN on the column of `window`. Returns
 * whether anything changed and the output needs arranging again. */
bool sh_tiling_scroll_action(struct sh_tiling *tiling, const void *window, enum sh_action action);
/* The first window of the tree, or NULL when it is empty. */
void *sh_tiling_master(const struct sh_tiling *tiling, const char *output, int workspace);

#ifdef __cplusplus
}
#endif
