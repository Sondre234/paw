/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* What the compositor's source files share: the server, its outputs and windows, and the
 * functions one file calls in another. Private to src/compositor. */
#pragma once

#ifndef _GNU_SOURCE
#define _GNU_SOURCE // accept4
#endif
#include "paw/backend.h"
#include "paw/effects.h"
#include "paw/effects_scene.h"
#include "paw/animation.h"
#include "paw/curve.h"
#include "paw/decoration.h"
#include "paw/shadow.h"
#include "paw/session.h"
#include "paw/output_state.h"
#include "paw/overview.h"
#include "paw/overview_scene.h"
#include "paw/tabs.h"
#include "paw/sleep.h"
#include "paw/login1.h"
#include "paw/swipe.h"
#include "paw/dynamic_rule.h"
#include "paw/border.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <dirent.h>
#include <signal.h>
#include <spawn.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/headless.h>
#include <wlr/backend/interface.h>
#include <wlr/backend/multi.h>
#include <wlr/backend/wayland.h>
#include <wlr/interfaces/wlr_ext_image_capture_source_v1.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/config.h>
#if WLR_HAS_LIBINPUT_BACKEND
#include <libinput.h>
#include <wlr/backend/libinput.h>
#endif
#if WLR_HAS_SESSION
#include <wlr/backend/session.h>
#endif
#include <wlr/render/allocator.h>
#include <wlr/render/swapchain.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_cursor_shape_v1.h>
#include <wlr/types/wlr_data_control_v1.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_export_dmabuf_v1.h>
#include <wlr/types/wlr_ext_data_control_v1.h>
#include <wlr/types/wlr_ext_foreign_toplevel_list_v1.h>
#include <wlr/types/wlr_ext_image_capture_source_v1.h>
#include <wlr/types/wlr_ext_image_copy_capture_v1.h>
#include <wlr/types/wlr_foreign_toplevel_management_v1.h>
#include <wlr/types/wlr_fractional_scale_v1.h>
#include <wlr/types/wlr_gamma_control_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_input_method_v2.h>
#include <wlr/types/wlr_output_management_v1.h>
#include <wlr/types/wlr_output_power_management_v1.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_keyboard_shortcuts_inhibit_v1.h>
#include <wlr/types/wlr_layer_shell_v1.h>
#include <wlr/types/wlr_linux_dmabuf_v1.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_presentation_time.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_screencopy_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_session_lock_v1.h>
#include <wlr/types/wlr_switch.h>
#include <wlr/types/wlr_tearing_control_v1.h>
#include <wlr/types/wlr_single_pixel_buffer_v1.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_tablet_pad.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/types/wlr_tablet_v2.h>
#include <wlr/types/wlr_text_input_v3.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_activation_v1.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_xdg_dialog_v1.h>
#include <wlr/types/wlr_virtual_keyboard_v1.h>
#include <wlr/types/wlr_virtual_pointer_v1.h>
#include <wlr/types/wlr_xdg_foreign_registry.h>
#include <wlr/types/wlr_xdg_foreign_v1.h>
#include <wlr/types/wlr_xdg_foreign_v2.h>
#include <wlr/types/wlr_xdg_output_v1.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_xdg_toplevel_icon_v1.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <wlr/xcursor.h>
#if WLR_HAS_XWAYLAND
#include <wlr/xwayland.h>
#include <xcb/xcb_icccm.h>
#if PAW_XWM_WAKER
#include <xcb/xfixes.h>
#endif
#endif
#include <xkbcommon/xkbcommon.h>
#include "paw-window-control-v1-protocol.h"

enum sh_cursor_mode {
    SH_CURSOR_PASSTHROUGH,
    SH_CURSOR_MOVE,
    SH_CURSOR_RESIZE,
};

enum sh_node_kind { SH_NODE_TOPLEVEL, SH_NODE_LAYER };
struct sh_node {
    enum sh_node_kind kind;
    void *owner;
};

#define SH_FRAME_RING 4096

/* A shadow image, painted for a look, a window size and a scale (sh_shadow_image_size's) and
 * shared by the windows showing it; `used` orders them for the cache to drop the oldest. */
struct sh_shadow_image {
    struct sh_shadow look;
    int width, height, scale;
    struct wlr_buffer *buffer;
    unsigned used;
};

/* Counters `get stats` reports, for the benchmark in tools/bench; cheap enough to keep on. */
struct sh_stats {
    uint64_t frames, frame_ns, frame_max_ns; /* output_frame calls and time spent in them */
    uint64_t commits, commit_ns;             /* window commits handled (refresh_frame) */
    uint64_t configures;                     /* windows placed by layouts (toplevel_configure) */
    uint64_t opacity_rules;                  /* times the window rules were matched for opacity */
    uint64_t motions, motion_ns;             /* pointer motion events handled and their time */
    uint64_t reflows, reflow_ns;             /* reflow_output runs that placed windows, and their time */
    /* The last SH_FRAME_RING frames: time spent in output_frame and time since the previous
     * frame, in microseconds, for `get frame_times` (percentiles in the benchmark). */
    uint32_t frame_us[SH_FRAME_RING], interval_us[SH_FRAME_RING];
    uint64_t last_frame_ns;
};

enum sh_night_mode { SH_NIGHT_AUTO, SH_NIGHT_OFF, SH_NIGHT_ON };
/* The overview (Expose). Thumbnails are scaled copies of the windows' scene nodes, kept live by
 * copying again when a window's fingerprint changes; see overview_scene.h. */
enum {
    OVERVIEW_MAX = 96,
    OVERVIEW_MINI_MAX = 128,
    OVERVIEW_WORKSPACES = 10,
    OVERVIEW_PAD = 6, /* the card around a thumbnail */
    OVERVIEW_TOP = 48 /* room above the strip for the shell's search box */
};
struct sh_toplevel;
struct sh_thumb {
    struct sh_toplevel *toplevel;
    struct wlr_scene_tree *tree;
    uint64_t fingerprint;
    double scale; /* the copy's scale; 0 before the first copy */
};
struct sh_overview {
    bool open;    /* taking input */
    bool visible; /* drawn: open, or gliding closed */
    bool closing;
    bool dirty; /* windows or workspaces changed: lay out again on the next step */
    bool armed; /* a step is scheduled */
    char output[64];
    struct wlr_box screen; /* the output, in layout coordinates */
    struct sh_rect area;   /* what the thumbnails use: the output less panels */
    struct wlr_scene_tree *tree, *strip, *cards_tree, *grid, *frames;
    struct wlr_scene_rect *backdrop, *frame[4];
    struct wl_event_source *timer;
    int viewed, current; /* the workspace the grid shows, and the one the output shows */
    int workspaces;
    char filter[128]; /* typed text; while set the grid lists matching windows everywhere */
    int count, selected;
    struct sh_toplevel *windows[OVERVIEW_MAX];
    struct sh_rect sizes[OVERVIEW_MAX];   /* the windows' sizes */
    struct sh_rect cells[OVERVIEW_MAX];   /* where the thumbnails rest */
    struct sh_rect origins[OVERVIEW_MAX]; /* where the windows are, or the cell without one */
    bool placed[OVERVIEW_MAX];            /* origins is a real place */
    struct wlr_scene_rect *cards[OVERVIEW_MAX];
    struct sh_thumb thumbs[OVERVIEW_MAX];
    int strip_count;
    struct sh_rect strip_cells[OVERVIEW_WORKSPACES];
    struct wlr_scene_rect *strip_back[OVERVIEW_WORKSPACES], *strip_mark[OVERVIEW_WORKSPACES][4];
    struct sh_thumb minis[OVERVIEW_MINI_MAX];
    int64_t started; /* milliseconds, CLOCK_MONOTONIC */
    int span;        /* milliseconds the current glide takes */
    double from, to, progress; /* 0: windows where they are, 1: thumbnails in the grid */
    int press;                 /* thumbnail under a pressed left button, else -1 */
    double press_x, press_y;
    bool dragging;
    struct sh_toplevel *dragged;
    int drop;         /* workspace cell under a dragged thumbnail, else -1 */
    uint32_t pressed; /* buttons the overview took; their releases are its too */
    double scroll;    /* wheel motion not yet enough to change the workspace */
    bool in_corner;   /* the pointer is in the hot corner: it opens the overview on entering */
    /* Snap Assist (overview_assist): the overview in the free slot beside a window just snapped,
     * `assist_from`, listing the output's other windows to put into `assist_slot` (an
     * arrangement); `area` is the slot, and its backdrop is rounded by `assist_radius`. */
    bool assist;
    enum sh_action assist_slot;
    struct sh_toplevel *assist_from;
    int assist_radius;
};

/* The power actions and what logind allows of them (power.c). One runs at a time, in steps. */
enum sh_power_step {
    SH_POWER_IDLE,
    SH_POWER_CLOSING, /* windows close before power off, reboot or log out */
    SH_POWER_LEAVING, /* their applications finish before the session ends */
    SH_POWER_LOCKING, /* the screen locks before suspend or hibernate */
    SH_POWER_CALLING, /* logind has been asked and has not answered yet */
};
struct sh_power {
    bool system_bus;          /* logind may be reached on the system bus (not --headless) */
    struct sh_login1 *login1; /* NULL while logind is out of reach */
    struct wl_event_source *bus, *bus_timer;
    struct wl_event_source *timer; /* how long a step may take */
    char answers[SH_LOGIN1_METHODS][16]; /* logind's Can* answers; "" until it gives one */
    enum sh_power_step step;
    enum sh_action action; /* the one under way, while step is not idle */
    /* A logind delay inhibitor holding off sleep until the screen is locked (-1 for none), one
     * being asked for, and whether logind has said the machine is about to sleep. */
    int sleep_delay;
    bool inhibiting, before_sleep;
    int64_t locker_started; /* when a locker started for a sleep, until the lock holds (ms) */
    /* The Wayland clients whose windows closed to log out, until they disconnect. */
    struct sh_power_client {
        struct sh_server *server;
        struct wl_client *client; /* NULL for a free slot */
        struct wl_listener destroy;
    } clients[64];
};

/* Power saving without an idle daemon (idle.c): when the last input came, the steps taken since
 * (a bit each), the timer for the next and whether it is set, and the black laid over every
 * output while the screens dim. */
enum sh_idle_step { SH_IDLE_DIM, SH_IDLE_DISPLAY_OFF, SH_IDLE_LOCK, SH_IDLE_SUSPEND, SH_IDLE_STEPS };
struct sh_idle {
    int64_t last_input; /* milliseconds, CLOCK_MONOTONIC */
    unsigned done;
    struct wl_event_source *timer;
    bool armed;
    struct wlr_scene_tree *tree; /* over everything, the lock too */
    struct wlr_scene_buffer *dim; /* NULL while the screens are not dimmed */
    struct sh_fade fade;
};

struct sh_window_object; // a paw_window_v1 (window_control.c)
struct sh_mirror;        // what a mirroring output shows (mirror.c)
struct wlr_color_manager_v1;

/* A workspace slide a touchpad swipe drives (workspace.c, for gestures.c): the output, the
 * workspace it showed as the swipe began and the one the fingers head for (out of range past
 * the first or last, `from` when they head nowhere), how far the slide is held, and held copies
 * of the windows coming in. */
struct sh_workspace_swipe {
    char output[64];
    int from, target;
    double shown;
    struct sh_anim *copies[64];
    int copy_count;
};

/* Touchscreens (touch.c): the devices, the finger standing in for the pointer (-1 for none),
 * and where the surface each other finger is on was in the layout as the finger came down. */
struct sh_touch_point {
    int32_t id;
    bool used;
    double origin_x, origin_y;
};
struct sh_touch {
    struct wl_list devices; // struct sh_touch_device
    int32_t pointer_id;
    struct sh_touch_point points[16];
};

/* Drawing tablets (tablet.c): tablet-v2, the tablets, pads and tools, and the cursor's tool
 * events and the keyboard's focus, which the pads follow. */
struct sh_tablets {
    struct wlr_tablet_manager_v2 *manager;
    struct wl_list tablets, pads, tools;
    struct wl_listener tool_proximity, tool_axis, tool_tip, tool_button, keyboard_focus_change;
};

/* Keyboard shortcuts inhibitors (shortcuts_inhibit.c): keyboard-shortcuts-inhibit, the
 * inhibitors clients asked for and X11 windows' keyboard grabs, and the one taking the keys now,
 * which the keyboard's focus decides. */
struct sh_shortcuts_inhibitor;
struct sh_shortcuts {
    struct wlr_keyboard_shortcuts_inhibit_manager_v1 *manager;
    struct wl_list inhibitors; // struct sh_shortcuts_inhibitor
    struct sh_shortcuts_inhibitor *effective;
    struct wl_listener new_inhibitor, keyboard_focus_change;
    struct wl_global *grab_manager; // xwayland-keyboard-grab, for X11 windows' grabs
};

/* Input methods (input_method.c): text-input-v3 for the applications, input-method-v2 for the
 * input method, one connected at a time, and the relay between them: the text inputs, the one
 * the input method is active for, its popups, and the trees they are drawn in, for windows and
 * for overlays; the listeners on the input method are the connected one's. */
struct sh_text_input;
struct sh_input_methods {
    struct wl_list text_inputs; // struct sh_text_input
    struct wl_list popups;      // struct sh_input_popup
    struct wlr_input_method_v2 *input_method;
    struct sh_text_input *active;
    struct wlr_scene_tree *popup_tree, *overlay_popup_tree;
    struct wl_listener new_text_input, new_input_method, keyboard_focus_change;
    struct wl_listener commit, new_popup, grab_keyboard, destroy, grab_destroy;
};

/* The touchpad swipe under way (gestures.c). */
enum sh_swipe_mode {
    SH_SWIPE_IDLE,      /* there is none */
    SH_SWIPE_WAITING,   /* it has no direction yet: held back from the windows until it has */
    SH_SWIPE_PASSED,    /* the window under the pointer's */
    SH_SWIPE_WORKSPACE, /* the workspaces follow the fingers */
    SH_SWIPE_OVERVIEW,  /* the overview opens or closes with them */
    SH_SWIPE_ACTION,    /* a request runs as the fingers lift past half way */
};
struct sh_gesture {
    enum sh_swipe_mode mode;
    struct sh_swipe swipe;
    /* When the swipe began, and how far it went as libinput tells it, while it waited: what the
     * window under the pointer is told at once if the swipe turns out to be its. */
    uint32_t began;
    double dx, dy;
    /* The swipe bound to the direction taken, and the one bound to the opposite, if any. */
    struct sh_swipe_binding bound, opposite;
    bool has_opposite;
    struct sh_workspace_swipe workspace;
    bool opening; /* the overview swipe opens it, rather than closing it */
};

struct sh_server {
    const struct sh_callbacks *callbacks;
    bool running;
    uint32_t grab_button;
    uint32_t bound_buttons; /* bit (code - BTN_MOUSE): pressed buttons a binding consumed */
    /* Where a button binding last spawned a program: the next window to open before
     * `spawn_until` (now_ms) is centered there. */
    double spawn_x, spawn_y;
    int64_t spawn_until;
    struct wlr_scene_tree *backgrounds;
    struct wlr_scene_tree *windows;
    // Over `windows`: floating windows with layout.floating_above_tiles, and windows kept above
    // the others (stacking.c).
    struct wlr_scene_tree *floating_windows, *above_windows;
    struct wlr_scene_tree *fullscreen;
    struct wlr_scene_tree *peek_layer;       // the window peeked at, over the others
    struct wlr_scene_tree *fullscreen_cover; // above the panels
    struct wlr_scene_tree *unmanaged;
    struct sh_animator *animator;
    struct sh_stats stats;
    unsigned config_generation; /* counts configurations loaded, starting at 1 */
    struct {                    /* one hit test shared by the steps of a motion event */
        bool caching, valid;
        double x, y, sx, sy;
        struct wlr_scene_node *node;
    } hit;
    int reflow_held;            /* while positive, reflow_output waits for the holder's own call */
#if WLR_HAS_XWAYLAND
    struct wlr_xwayland *xwayland;
    struct wl_listener xwayland_ready, new_xwayland_surface;
#if PAW_XWM_WAKER
    xcb_connection_t *xwm_waker;
    xcb_atom_t waker_atom;
    xcb_window_t xwm_window;
    struct wl_event_source *waker_timer, *waker_input;
#endif
#endif
    /* Each output shows one of its own workspaces (from 0) and tiles automatically or not.
     * Kept by output name, so an output that goes away (all of them do on a VT switch) comes
     * back on the same workspace, tiling as it was. */
    struct {
        char name[64];
        int current;
        int previous;    // the workspace shown before `current`, or -1 for none yet
        int tiling;      // -1 until output_tiles decides it from the config
        bool configured; // what the config said then, so a reload only applies changes
        // With layout.tiling_per_workspace, each workspace's own toggle: -1 follows `tiling`.
        signed char workspace_tiling[10];
    } output_workspaces[16];
    bool tiling_per_workspace; // the layout.tiling_per_workspace the tiling was last put in line with
    char active_output[64];           // of the last focused window, switched workspace, or click
    char placed_primary[64];          // the primary output the pointer was last put on, if any
    struct wlr_output *target_output; // set while a control request names an output
    struct sh_tiling *tiling;
    bool grab_retile;     // the grabbed window left the tiling to be moved; retile it on drop
    bool grab_fullscreen; // the grabbed window is fullscreen until it is dragged far enough
    unsigned scratchpad_serial;  // counts windows put into the scratchpad, to order them
    bool scratchpad_off_logged;  // said once that features.scratchpad is off
    struct wlr_output *grab_output; // where the grabbed window was when the grab began
    /* The window switcher: a list of every window taken as it opens, most recently focused
     * first. It stays open while `modifiers` (the opening binding's, less Shift) are all held,
     * or with none until confirmed or cancelled; the shell draws it on `output`. */
    struct {
        bool open;
        uint32_t modifiers;
        xkb_keysym_t key; // the opening binding's key, which selects the next window
        struct sh_toplevel *windows[128];
        int count, selected;
        char output[64];
    } switcher;
    struct sh_overview overview;
    struct wlr_scene_tree *overview_layer;
    /* Magnetic edges: guide lines over the windows while a dragged one is held by an edge. */
    struct wlr_scene_tree *guide_layer;
    struct wlr_scene_rect *guides[2]; /* a vertical and a horizontal line */
    /* Snapping a dragged window at the edges of its output (snap.c): the zone the pointer is
     * in (the arrangement a drop gives, SH_NONE for none) and its slot, the window's floating
     * box as the drag began, and the preview of the slot, which eases between places in `tween`
     * (x, y, width, height, opacity), drawn now as `drawn`, with corners of `radius`. */
    struct {
        enum sh_action zone;
        struct sh_rect slot;
        struct wlr_box start;
        struct wlr_scene_tree *preview;
        struct wlr_scene_rect *fill, *outline;
        struct sh_tween tween;
        float drawn[SH_TWEEN_VALUES];
        int radius;
    } snap;

    /* Windows a session restore launched and has yet to place: the first new window with the
     * app ID takes the saved place, workspace and state, until the deadline (milliseconds on
     * the monotonic clock). */
    struct {
        struct sh_session_window window;
        int64_t deadline;
        bool used;
    } session_pending[32];
    /* A login session (standalone, or a test's with PAW_LOGIN_SESSION): it saves itself as
     * it ends and restores that as it starts (session.restore). */
    bool login_session;

    /* Urgent windows (see windows.activation): the last order number given, and the timer that
     * redraws their pulsing borders while they pulse. */
    unsigned urgent_serial;
    struct wl_event_source *urgent_timer;
    /* Configuration files changing: the watch, and a short wait so that a burst of writes
     * (an editor saving through a temporary file) reloads once. */
    struct wl_event_source *config_watch;
    struct wl_event_source *config_timer;

    int control_fd;
    struct wl_list subscribers; // control clients receiving state changes
    char sent_state[8192];      // the state they last received
    char control_path[108];
    struct wl_event_source *control_source;
    struct wlr_scene_tree *layer_trees[4];
    struct wl_list layers;
    struct wl_listener new_layer_surface;
    struct sh_layer *focused_layer;
    struct sh_toplevel *focused_toplevel;
    struct wlr_foreign_toplevel_manager_v1 *foreign_manager;
    uint32_t last_window_id; // the number the last window published was given
    struct wlr_ext_foreign_toplevel_list_v1 *toplevel_list; // windows offered for screen sharing
    /* paw-window-control-v1 (window_control.c): its global, the paw_window_v1 objects
     * clients hold, and the idle callback that tells them what changed. */
    struct wl_global *window_control;
    struct wl_list window_objects;
    struct wl_event_source *window_objects_idle;
    struct wl_listener new_capture_request;
    struct wl_listener set_xdg_icon; // a Wayland window giving its icon (window_icon.c)
    struct wlr_tearing_control_manager_v1 *tearing_control; // windows' tearing hints (tearing.c)
    /* color-management-v1 (hdr.c), offered once a monitor asks for HDR with a renderer that can
     * convert colours; whether that was refused already, said once. */
    struct wlr_color_manager_v1 *color_manager;
    bool color_manager_refused;

    /* Session lock: `locked` outlives a crashed locker so the screen stays covered. */
    bool locked;
    /* The binding mode in use, by name: "default" outside any (binding_mode.c). */
    char binding_mode[40];
    struct sh_lock *lock;
    struct wlr_scene_tree *lock_tree, *lock_blanks;
    struct wl_listener new_lock;
    struct wlr_idle_notifier_v1 *idle_notifier;
    struct wlr_output_manager_v1 *output_manager;
    struct wl_listener output_apply, output_test;
    /* The display mode popup (display_mode.c): whether it is open, on which output, the choice
     * it shows (enum sh_display_mode), and the timer that takes it once the key has rested. */
    struct {
        bool open;
        int shown;
        char output[64];
        struct wl_event_source *timer;
    } display_mode;
    /* The display settings window's monitors (display_settings.c): the settings it kept, read
     * from the state file and laid over outputs.monitors, or a trial's until it is kept. While a
     * trial runs, what goes back unless it is kept: the kept settings, each monitor's runtime
     * settings (sh_output.override) by name; and the timer that takes them back, at
     * `trial_ends` (now_ms()). */
    struct {
        struct sh_output_state saved;
        bool trial;
        struct sh_output_state kept;
        struct {
            char name[64];
            bool has_override;
            struct sh_monitor override;
        } before[SH_OUTPUT_STATE_MAX];
        int before_count;
        struct wl_event_source *timer;
        int64_t trial_ends;
    } display_settings;
    /* wlr-output-power-management (output_power.c), and when an action last turned monitors
     * off, which input does not undo for a moment. */
    struct wl_listener output_power_set_mode;
    int64_t displays_off_at;
    struct wl_listener new_inhibitor;
    int inhibitors;
    struct wl_display *wl_display;
    struct wlr_backend *backend;
    char pending_output_name[64]; // the name the next headless output takes, for tests
#if WLR_HAS_SESSION
    struct wlr_session *session;
    struct wl_listener session_active;
    int sleep_inhibitor; // logind inhibitor fd while this VT is in front, else -1
#endif
    struct sh_power power;
    struct sh_idle idle;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wlr_scene *scene;
    struct wlr_scene_output_layout *scene_layout;
    struct wl_listener new_xdg_toplevel;
    struct wl_listener new_xdg_popup;
    struct wl_listener new_decoration;
    struct wl_list toplevels;

    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *cursor_mgr;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;
    /* Touchpad gestures (gestures.c): pointer-gestures-unstable-v1, and the cursor's gestures. */
    struct wlr_pointer_gestures_v1 *pointer_gestures;
    struct wl_listener swipe_begin, swipe_update, swipe_end;
    struct wl_listener pinch_begin, pinch_update, pinch_end;
    struct wl_listener hold_begin, hold_end;
    struct sh_gesture gesture;
    struct sh_touch touch;
    struct wl_listener touch_down, touch_motion, touch_up, touch_cancel, touch_frame;
    struct sh_tablets tablet;
    struct sh_shortcuts shortcuts;
    struct sh_input_methods input_methods;

    struct wlr_seat *seat;
    struct wl_listener new_input;
    struct wl_listener new_virtual_keyboard;
    struct wl_listener new_virtual_pointer;
    struct wl_listener request_cursor;
    struct wl_listener request_set_shape;
    uint32_t shape_edges; // edges of the client's single-edge resize shape, else 0
    uint32_t shown_edges; // edges of the resize cursor currently shown for it
    struct wl_listener pointer_focus_change;
    struct wl_listener request_set_selection, request_set_primary_selection;
    struct wl_listener request_start_drag, start_drag;
    struct wl_listener drag_end; // on the drag under way, which there is one of at most
    struct wlr_scene_tree *drag_icons; // follows the cursor during drag-and-drop
    struct wl_listener request_activate;
    struct wlr_relative_pointer_manager_v1 *relative_pointer;
    struct wlr_pointer_constraints_v1 *constraints;
    struct wlr_pointer_constraint_v1 *active_constraint; // on the keyboard-focused surface
    struct wl_listener new_constraint, keyboard_focus_change;
    struct wl_list keyboards;
    /* The keymap of the keyboard settings, shared by every keyboard but the virtual ones, and
     * the layout active on all of them (from 0). keymap.c sets `syncing_keyboards` while it
     * changes keyboards' state itself, and tells the seat afterwards. */
    struct xkb_keymap *keymap;
    bool keymap_from_file; // keyboard.file, rather than the names
    xkb_layout_index_t keyboard_layout;
    bool syncing_keyboards;
    struct wl_list headless_keyboards; // added by tests with "headless_keyboard add"
    struct wl_list headless_pointers;  // and "headless_pointer add" (headless_input.c)
    struct wl_list headless_touches;   // and "headless_touch add"
    struct wl_list headless_tablets;   // and "headless_tablet add"
    /* Switch devices (switches.c), whether any says the lid is closed, and the switches tests
     * add with "headless_switch add". */
    struct wl_list switches;
    bool lid_closed;
    bool logind_lid_closed; // logind's LidClosed, which counts as one more switch
    struct wl_list headless_switches;
    struct wl_list pointers; /* struct sh_pointer */
    enum sh_cursor_mode cursor_mode;
    struct sh_toplevel *grabbed_toplevel;
    double grab_x, grab_y;
    struct wlr_box grab_geobox;
    uint32_t resize_edges;
    /* Window controls: shared buffers by style, focus, hovered and pressed part, drawn at
     * deco_scale pixels per logical pixel; the window whose controls are hovered (and which
     * button) or revealed, and a button pressed but not yet released. */
    struct wlr_buffer *deco_buffers[2 * 2 * (SH_DECO_FULLSCREEN + 1) * (SH_DECO_FULLSCREEN + 1)];
    int deco_scale;
    /* The last few shadow images painted; windows' slices keep the ones they show. */
    struct sh_shadow_image shadow_images[8];
    unsigned shadow_uses;
    /* Peek: 0 to 1, how far windows have faded toward the desktop. It is held by the key with
     * evdev code `peek_keycode` on `peek_keyboard`, or toggled without one. */
    struct sh_fade peek_fade;
    bool peeking;
    double peek_applied; // what the windows were last given
    uint32_t peek_keycode;
    struct sh_keyboard *peek_keyboard;
    /* A peek at one window, which a paw_window_v1 (`peek_object`) asks for: the others
     * fade with peek_fade while it shows over them in peek_layer, an empty node keeping its
     * place among them (`peek_place`). `peek_lent` counts the windows a peek shows although
     * they are hidden. */
    struct sh_toplevel *peek_window;
    struct sh_window_object *peek_object;
    struct wlr_scene_tree *peek_place;
    int peek_lent;
    /* Night light: the schedule or an override picks a temperature; `night_transform` is the
     * matrix for it (NULL at neutral), handed to every output commit. */
    struct wl_event_source *night_timer;
    struct sh_fade zoom_fade; // the magnification, 1 for none
    double zoom_target, zoom_scroll;
    struct sh_corner_dwell corner_dwell; // hot corners
    struct wl_event_source *corner_timer;
    int night_mode; // enum sh_night_mode
    int night_kelvin;
    int night_announced; // whether it was warm and who decided, as subscribers last heard; -1 first
    double night_clock; // minutes after midnight when fixed for testing, else -1
    struct wlr_color_transform *night_transform;
    struct wlr_buffer *black;   // stretched over windows to dim them; made on first use
    struct sh_toplevel *deco_hovered, *deco_revealed, *deco_pressed;
    enum sh_deco_part deco_hovered_part;
    enum sh_deco_part deco_pressed_part;
    unsigned group_serial;              // counts group members and groups, to name and order them
    struct sh_toplevel *tabs_hovered;   // the window whose tab strip the pointer is on
    int tabs_hovered_index;

    struct wlr_output_layout *output_layout;
    struct wl_list outputs;          /* enabled, in the layout */
    struct wl_list disabled_outputs; /* turned off by outputs.monitors */
    struct wl_listener new_output;
};

/* Tearing on an output (tearing.c): what its frames do now (`tearing`, `refused`, or why its
 * window may not tear) and the window's title, the window the rules were last matched for (its
 * number, under a configuration generation) and their answer, and the frames flipped at once and
 * refused, for `get tearing`. */
struct sh_tearing {
    const char *why;
    char title[128];
    const struct sh_toplevel *rule_window;
    uint32_t rule_id;
    unsigned rule_generation;
    bool ruled, refusal_logged;
    uint64_t flips, refused;
};

struct sh_output {
    struct wlr_box usable;
    int x, y;                /* arrangement before the shift to the layout origin */
    struct wlr_box previous; /* where arrange_outputs found it; empty when newly added */
    bool disabled;           /* listed in disabled_outputs */
    /* Turned off in the layout (output_power.c): it keeps its windows, workspaces and panels,
     * but the wlr_output is disabled, so it neither scans out nor draws frames. */
    bool powered_off;
    bool idle_off; /* turned off by the idle display_off step (idle.c), which input undoes */
    bool kept_on;  /* on though its settings say off, as the only output (configure_output) */
    /* Settings a wlr-output-management client (wlr-randr, kanshi) or display_mode applied at
     * runtime. They replace the configured monitor until the configuration is reloaded or the
     * display settings window applies its own. */
    bool has_override;
    struct sh_monitor override;
    /* Its outputs.monitors entry with what the display settings window kept for it laid over
     * it, as configured_monitor last made it. */
    struct sh_monitor configured;
    struct wlr_scene_rect *background, *lock_blank;
    bool lock_presented;
    /* Magnifier: the scene is drawn into `zoom_swapchain` and the output shows a part of the
     * newest of those buffers (`zoom_source`, locked) enlarged. */
    struct wlr_swapchain *zoom_swapchain;
    struct wlr_buffer *zoom_source;
    bool zoomed; // the last frame was magnified
    bool zoom_failed; // it could not be magnified this time; it shows 1x until the zoom is reset
    /* Out of the layout, showing another output's picture (mirror.c); NULL otherwise. */
    struct sh_mirror *mirror;
    struct sh_tearing tearing;
    const char *hdr_refused; // why it stays SDR though asked for HDR, as last logged (hdr.c)
    struct wl_list link;
    struct sh_server *server;
    struct wlr_output *wlr_output;
    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_listener destroy;
};

/* An icon a window supplies itself (window_icon.c): the icon theme name it gave, and the pixels
 * of one of its sizes as wl_shm's ARGB8888 (premultiplied 0xAARRGGBB words, rows without
 * padding). Either may be missing; neither is an icon the window does not have. */
struct sh_icon {
    char *name;
    uint32_t *pixels;
    int width, height;
};

/* What a window's border in a gradient was painted for (gradient_border.c). */
struct sh_gradient_drawn {
    int width, height, border, radius, scale;
    float mix; // from the inactive gradient (0) to the active one (1)
    unsigned generation; // server->config_generation
};

struct sh_opacity_rule {
    unsigned generation; // server->config_generation it was computed under; 0 for never
    bool active;
    char *app_id, *title;
    float value;
};

struct sh_toplevel {
    struct sh_node node;
    enum sh_action arrangement;
    bool minimized;
    int workspace;   // one of the workspaces of `output`
    char output[64]; // the output the window was placed on, by name; empty before that
    /* Set while the window is away from an output that was unplugged: the output and workspace
     * it came from, and whether it was tiled there. It goes back when that output returns. */
    char home_output[64];
    int home_workspace;
    bool home_tiled;
    struct wlr_foreign_toplevel_handle_v1 *foreign;
    /* The window's number, given as it is first published and to no other window while the
     * compositor runs: the window control's id event and the switcher's lines name it by this,
     * for the shell to match the two. 0 until then. */
    uint32_t id;
    /* Window capture: a private scene holding only this window's surfaces, so sharing one
     * window never shows what overlaps it. */
    struct wlr_ext_foreign_toplevel_handle_v1 *listed;
    struct wlr_scene *capture_scene;
    struct wlr_ext_image_capture_source_v1 *capture_source;
    /* The icon the window supplies itself, and how many times it has changed, which the window
     * control's objects hold against what they last sent. */
    struct sh_icon icon;
    unsigned icon_serial;
    struct wl_listener title_changed, app_id_changed;
    struct wl_listener foreign_activate, foreign_close, foreign_maximize, foreign_minimize;
    struct wl_listener foreign_fullscreen;
    bool fullscreen;
    bool fullscreen_cover; // the client asked for fullscreen itself, so it covers the panels too
    struct wlr_box fullscreen_restore;
    struct wl_listener request_minimize;
    /* xdg-decoration: the window leaves its title bar to us and gets the window controls instead. */
    struct wlr_xdg_toplevel_decoration_v1 *decoration;
    struct wl_listener decoration_mode, decoration_destroy;
    struct wlr_box restore_box;
    bool arranged;
    bool tiled;      // in the tiling tree; restore_box keeps its floating geometry
    bool floating;   // kept out of the tiling (dialogs, or toggled by the user)
    bool placed;     // floating only because it was snapped or maximized by hand
    bool tile_sized; // first configured at its predicted tile, so it has no floating size yet
    /* In the scratchpad: floating, and hidden (minimized) until scratchpad_show brings it to
     * the focused output. Hidden ones show in the order they were put there. */
    bool scratchpad;
    unsigned scratchpad_order;
    /* Shown on every workspace of its output, always floating; its workspace follows the
     * output's current one. `sticky_floating` is what `floating` was before, for unsticking. */
    bool sticky, sticky_floating;
    /* Kept above the other windows (stacking.c): drawn in above_windows while not fullscreen in
     * front. */
    bool above;
    /* What the dynamic window rules hold the window to, and what it gets back as they stop
     * (dynamic_rules.c). */
    struct sh_held_value held_floating, held_sticky, held_above;
    /* Asked for attention while it had no focus; cleared when it is focused or unmapped.
     * urgent_order says which asked first, urgent_since (milliseconds) when it began pulsing. */
    bool urgent;
    unsigned urgent_order;
    int64_t urgent_since;
    /* A window group: members share one slot and show one at a time (the others are
     * group_hidden, out of the tiling and off the screen). 0: not in a group. group_order
     * is the tab order. */
    unsigned group, group_order;
    bool group_hidden;
    /* Window swallowing: a terminal that lets a window it started take its place is
     * `swallowed` (hidden, and off the taskbar) and its `swallow_peer` is that window, whose
     * own peer is the terminal. */
    bool swallowed;
    struct sh_toplevel *swallow_peer;
    struct wlr_scene_buffer *tabs; // the strip of tabs over the shown member's top edge
    int tabs_width, tabs_count, tabs_active, tabs_hover, tabs_scale; // what `tabs` shows
    struct wl_list link;
    struct sh_server *server;
    struct wlr_xdg_toplevel *xdg_toplevel; // NULL for X11 windows
    bool unmanaged;                        // an X11 override-redirect menu or tooltip
#if WLR_HAS_XWAYLAND
    struct wlr_xwayland_surface *xsurface; // NULL for xdg-shell windows
    bool associated;
    struct wl_listener x_associate, x_dissociate, x_configure, x_activate, x_geometry;
    struct wl_listener x_decorations, x_attention, x_hints, x_icon;
    bool x_hint_urgent; // the client's WM_HINTS ask for attention
#endif
    /* scene_tree sits at the window's place; content holds everything drawn for it, so an
     * animation can move, scale, and fade it without changing where the window is. */
    struct wlr_scene_tree *scene_tree, *content;
    struct sh_anim anim;
    struct sh_tween fade; // opacity and border color following focus
    bool shown; // has opened (and started its opening animation) since it last mapped
    struct wlr_scene_buffer *deco;    // window controls; NULL when the client decorates itself
    struct wlr_scene_rect *border[4]; // top, bottom, left, right; NULL without a border
    /* A border in a gradient, in place of `border` (gradient_border.c): its pieces, sides then
     * corners, in a tree of their own, and what they were painted for; NULL without one. */
    struct wlr_scene_tree *gradient;
    struct wlr_scene_buffer *gradient_pieces[SH_BORDER_PIECES];
    struct sh_gradient_drawn gradient_drawn;
    bool border_gradient; // the focus fade last eased between gradients, not colours
    int frame_hole; // with rounded corners, the width of the frame border[0] draws; else 0
    int corner_radius; // of the rounded clip on `content`; 0 while the window is square
    /* The shadow: a tree at the bottom of `content` holding the slices of a shared image, NULL
     * without one; the image and the size of frame they were laid out for, and for `get frames`
     * the box they cover (from the window's top-left corner) and how dark they are at most. */
    struct wlr_scene_tree *shadow;
    struct wlr_scene_buffer *shadow_slices[SH_SHADOW_SLICES];
    struct wlr_buffer *shadow_image;
    int shadow_width, shadow_height;
    struct wlr_box shadow_box;
    float shadow_alpha;
    float opacity;                    // last applied to the window's buffers
    struct wlr_scene_buffer *dim;     // black over the window while it is dimmed, else NULL
    struct sh_fade dim_fade;          // how opaque that black is, and where it is heading
    /* A peek at this window: how far it shows through the peek's fade (1 in full), what it was
     * last drawn with, and whether the peek showed its node although the window is hidden. */
    struct sh_fade peek_shown;
    double peek_shown_applied;
    bool peek_lent;
    /* The opacity the window rules gave for these inputs: matching regexes on every commit
     * would cost more than the commit, so it is redone only when one of them changes. */
    struct sh_opacity_rule opacity_rule;
    /* The user was told the window took the keyboard's shortcuts (shortcuts_inhibit.c), which
     * they hear once. */
    bool shortcuts_told;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;
};

struct sh_layer {
    struct sh_node node;
    struct sh_server *server;
    struct wlr_layer_surface_v1 *surface;
    struct wlr_scene_layer_surface_v1 *scene;
    struct wl_list link;
    struct wl_listener commit, map, unmap, destroy, new_popup;
};

struct sh_lock {
    struct sh_server *server;
    struct wlr_session_lock_v1 *lock;
    bool locked_sent;
    struct wl_listener new_surface, unlock, destroy;
};

struct sh_pointer {
    struct wl_list link;
    struct sh_server *server;
    struct wlr_input_device *device;
    struct wl_listener destroy;
};

/* A switch device (switches.c): whether it says the lid is closed, and tablet mode is on. */
struct sh_switch_device {
    struct wl_list link; // sh_server.switches
    struct sh_server *server;
    struct wlr_switch *wlr_switch;
    bool lid_closed, tablet_mode;
    struct wl_listener toggle, destroy;
};

struct sh_keyboard {
    bool consumed[KEY_MAX + 1];
    struct wl_list link;
    struct sh_server *server;
    struct wlr_keyboard *wlr_keyboard;
    bool is_virtual; // wtype and the like, which send their own keymap
    /* A held key whose binding `repeats` (keyboard resizing's, unless told otherwise) runs its
     * action again at the keyboard's repeat rate, as clients repeat keys themselves. */
    struct wl_event_source *repeat_timer;
    uint32_t repeat_keycode;
    enum sh_action repeat_action;
    int repeat_argument;
    bool repeat_locked; // its binding runs while the session is locked too
    /* Keys that went down to the input method's keyboard grab, which come up there too. */
    bool to_input_method[KEY_MAX + 1];

    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;
};

static const uint32_t ALL_EDGES = WLR_EDGE_TOP | WLR_EDGE_BOTTOM | WLR_EDGE_LEFT | WLR_EDGE_RIGHT;

/* server.c */
uint64_t now_ns(void);
int64_t now_ms(void);
int spawn_program(char *const argv[]);
void add_listener(struct wl_signal *signal, struct wl_listener *listener,
                  wl_notify_func_t notify);
const struct sh_settings *server_settings(struct sh_server *server);
void reload_config(struct sh_server *server);

/* actions.c */
bool take_screenshot(struct sh_server *server, enum sh_screenshot_mode mode, char *error,
                     size_t error_size);
bool launch_program(struct sh_server *server, enum sh_action action, char *error,
                    size_t error_size);
void run_action(struct sh_server *server, enum sh_action action, int argument);

/* control.c */
void control_reply(int fd, const char *text);
void drop_partial_utf8(char *text);
void flatten_field(char *text);
void copy_field(char *out, size_t size, const char *text);
struct wlr_backend *headless_backend(struct sh_server *server);
void notify_subscribers(struct sh_server *server);
void send_event(struct sh_server *server, const char *text, size_t length);
void request_shell(struct sh_server *server, const char *what);
void send_shell_line(struct sh_server *server, const char *line);
bool shell_listening(struct sh_server *server);
void report_failure(struct sh_server *server, const char *event, const char *text);
void request_launcher(struct sh_server *server);
void request_palette(struct sh_server *server);
void request_taskbar(struct sh_server *server);
void open_control_socket(struct sh_server *server, const char *wayland_socket);
void close_control_socket(struct sh_server *server);

/* binding_mode.c */
const char *binding_mode(struct sh_server *server);
void set_binding_mode(struct sh_server *server, int mode);

/* cursor.c */
struct wlr_output *pointer_output(struct sh_server *server);
uint32_t corner_edges(struct sh_toplevel *toplevel, uint32_t edges);
void cursor_request_set_shape(struct wl_listener *listener, void *data);
void process_cursor_motion(struct sh_server *server, uint32_t time);
void server_cursor_motion(struct wl_listener *listener, void *data);
void server_cursor_motion_absolute(struct wl_listener *listener, void *data);
void server_cursor_button(struct wl_listener *listener, void *data);
void server_cursor_axis(struct wl_listener *listener, void *data);
void server_cursor_frame(struct wl_listener *listener, void *data);
void seat_request_cursor(struct wl_listener *listener, void *data);
void set_default_cursor(struct sh_server *server);
struct sh_toplevel *toplevel_at(struct sh_server *server, double x, double y);
struct wlr_surface *press_target_at(struct sh_server *server, double x, double y, double *sx,
                                    double *sy, struct sh_node **owner);
void seat_pointer_focus_change(struct wl_listener *listener, void *data);

/* dynamic_rules.c */
bool open_dynamic_rules(struct sh_toplevel *toplevel, struct sh_window_rule *rule, bool ruled);
void follow_dynamic_rules(struct sh_toplevel *toplevel);
void describe_dynamic_rules(struct sh_server *server, int fd, const char *arguments);

/* display_mode.c */
bool display_mode_choose(struct sh_server *server, int mode, char *error, size_t error_size);
bool display_mode_key(struct sh_server *server, xkb_keysym_t sym);
void describe_display_mode(struct sh_server *server, int fd, const char *arguments);
void display_mode_finish(struct sh_server *server);

/* display_settings.c */
void display_settings_load(struct sh_server *server);
const struct sh_output_saved *saved_output(const struct sh_output *output);
const char *primary_output_name(struct sh_server *server);
bool hdr_asked(struct sh_server *server);
void describe_monitors(struct sh_server *server, int fd, const char *arguments);
void describe_monitors_trial(struct sh_server *server, int fd, const char *arguments);
void control_monitors(struct sh_server *server, int fd, const char *arguments);
void display_settings_finish(struct sh_server *server);

/* effects.c */
void update_dim(struct sh_toplevel *toplevel);
bool tick_effects(struct sh_server *server);
void night_light_update(struct sh_server *server);
int night_light_tick(void *data);
void hot_corner_check(struct sh_server *server);
int hot_corner_tick(void *data);
double zoom_level(struct sh_server *server, int64_t now);
void zoom_by(struct sh_server *server, int steps);
void zoom_moved(struct sh_server *server);
void output_release_zoom(struct sh_output *output);
bool output_commit_zoomed(struct sh_output *output, struct wlr_scene_output *scene_output,
                          const struct wlr_scene_output_state_options *options,
                          double level);
void set_peek(struct sh_server *server, bool on);
float peek_scale(struct sh_toplevel *toplevel, int64_t now);
bool shown_for_peek(struct sh_toplevel *toplevel);
void peek_at_window(struct sh_toplevel *toplevel);
void end_window_peek(struct sh_server *server, bool fade);
void forget_window_peek(struct sh_toplevel *toplevel);

/* focus.c */
void deactivate_toplevel(struct sh_server *server);
void keyboard_enter(struct wlr_seat *seat, struct wlr_surface *surface);
void focus_toplevel_raise(struct sh_toplevel *toplevel, bool raise);
void focus_toplevel(struct sh_toplevel *toplevel);
float urgent_pulse(struct sh_toplevel *toplevel, int64_t now);
int urgent_tick(void *data);
void set_urgent(struct sh_toplevel *toplevel, bool urgent);
void activation_requested(struct sh_toplevel *toplevel);
void focus_urgent(struct sh_server *server);
bool hover_focuses(struct sh_server *server, struct sh_toplevel *toplevel);
void focus_previous(struct sh_server *server);
void focus_last(struct sh_server *server);
void focus_top_on(struct sh_server *server, struct wlr_output *output);
void focus_desktop(struct sh_server *server, struct wlr_output *output);
void focus_layer(struct sh_layer *layer);
struct sh_toplevel *current_toplevel(struct sh_server *server);
struct sh_toplevel *toplevel_toward(struct sh_toplevel *from_toplevel, bool horizontal,
                                    int sign, bool tiles_only);
void pointer_follow(struct sh_toplevel *toplevel);
void focus_direction(struct sh_server *server, enum sh_action action);
void request_activate(struct wl_listener *listener, void *data);

/* foreign_toplevel.c */
void toplevel_title_changed(struct wl_listener *listener, void *data);
void toplevel_app_id_changed(struct wl_listener *listener, void *data);
struct wlr_ext_image_capture_source_v1 *toplevel_capture_source(struct sh_toplevel *toplevel);
void server_new_capture_request(struct wl_listener *listener, void *data);
void publish_toplevel(struct sh_toplevel *toplevel);
void unpublish_toplevel(struct sh_toplevel *toplevel);

/* frame.c */
int pixel_scale(struct sh_server *server);
enum wlr_xdg_toplevel_decoration_v1_mode
decoration_mode(struct wlr_xdg_toplevel_decoration_v1 *decoration);
bool wants_decoration(struct sh_toplevel *toplevel);
enum sh_deco_style deco_style(struct sh_server *server);
bool in_deco_corner(struct sh_toplevel *toplevel, double x, double y);
void refresh_decoration(struct sh_toplevel *toplevel);
void refresh_tabs(struct sh_toplevel *toplevel);
void refresh_frame(struct sh_toplevel *toplevel);
void forget_decoration(struct sh_toplevel *toplevel);
void server_new_decoration(struct wl_listener *listener, void *data);

/* gestures.c */
void describe_gesture(struct sh_server *server, int fd);
void gestures_init(struct sh_server *server);
void gestures_finish(struct sh_server *server);

/* grab.c */
void reset_cursor_mode(struct sh_server *server);
bool drop_tiles(struct sh_server *server, struct wlr_output *output);
void finish_grab(struct sh_server *server);
void process_cursor_move(struct sh_server *server);
void process_cursor_resize(struct sh_server *server);
void begin_interactive(struct sh_toplevel *toplevel, enum sh_cursor_mode mode,
                       uint32_t edges);

/* gradient_border.c */
bool gradient_borders(struct sh_server *server);
void refresh_gradient_border(struct sh_toplevel *toplevel, bool on, int border, int radius,
                             double mix, float opacity);

/* group.c */
bool groups_enabled(struct sh_server *server);
int group_size(struct sh_server *server, unsigned group);
int group_index(struct sh_toplevel *from);
void group_follow(struct sh_toplevel *toplevel);
void hand_over_slot(struct sh_toplevel *from, struct sh_toplevel *to);
void group_show(struct sh_toplevel *toplevel);
void group_detach(struct sh_toplevel *toplevel);
bool groupable(struct sh_toplevel *toplevel);
void group_join(struct sh_toplevel *toplevel, unsigned group);
void group_toggle(struct sh_server *server, struct sh_toplevel *current);
void group_cycle(struct sh_server *server, struct sh_toplevel *current, int step);
void ungroup(struct sh_server *server, struct sh_toplevel *current);
void group_merge(struct sh_server *server, enum sh_action action);
void dissolve_groups(struct sh_server *server);

/* hdr.c */
void color_management_update(struct sh_server *server);
bool output_want_hdr(struct sh_output *output, const struct sh_monitor *monitor, bool mirrors,
                     struct wlr_output_state *state);
void output_drop_hdr(struct sh_output *output, struct wlr_output_state *state);
bool output_is_hdr(const struct sh_output *output);
const char *hdr_unavailable(struct sh_output *output);

/* headless_input.c */
void control_headless_pointer(struct sh_server *server, int fd, const char *arguments);
void control_headless_touch(struct sh_server *server, int fd, const char *arguments);
void control_headless_tablet(struct sh_server *server, int fd, const char *arguments);
void destroy_headless_inputs(struct sh_server *server);

/* idle.c */
int idle_step_timeout(const struct sh_idle_steps *steps, enum sh_idle_step step,
                      const char **name);
const struct sh_idle_steps *idle_steps(struct sh_server *server, bool *battery);
bool idle_held(struct sh_server *server);
bool tick_idle(struct sh_server *server);
bool idle_activity(struct sh_server *server);
void idle_hold_changed(struct sh_server *server);
void idle_reload(struct sh_server *server);
void idle_init(struct sh_server *server);
void idle_finish(struct sh_server *server);

/* input.c */
bool input_activity(struct sh_server *server, bool wakes);
void control_headless_keyboard(struct sh_server *server, int fd, const char *arguments);
void destroy_headless_keyboards(struct sh_server *server);
void configure_pointer(struct sh_server *server, struct wlr_input_device *device);
void server_new_input(struct wl_listener *listener, void *data);
void server_new_virtual_keyboard(struct wl_listener *listener, void *data);
void server_new_virtual_pointer(struct wl_listener *listener, void *data);
void seat_request_set_selection(struct wl_listener *listener, void *data);
void seat_request_set_primary_selection(struct wl_listener *listener, void *data);
void seat_request_start_drag(struct wl_listener *listener, void *data);
void seat_start_drag(struct wl_listener *listener, void *data);
void server_new_constraint(struct wl_listener *listener, void *data);
void seat_keyboard_focus_change(struct wl_listener *listener, void *data);

/* input_method.c */
bool input_method_key(struct sh_keyboard *keyboard, const struct wlr_keyboard_key_event *event);
bool input_method_modifiers(struct sh_keyboard *keyboard);
void describe_input_method(struct sh_server *server, int fd,
                           void (*surface)(struct sh_server *, int, const char *,
                                           struct wlr_surface *));
void input_method_init(struct sh_server *server);
void input_method_finish(struct sh_server *server);

/* keymap.c */
bool configure_keyboard(struct sh_server *server, struct wlr_keyboard *keyboard);
void update_keymap(struct sh_server *server);
void follow_keyboard_layout(struct sh_server *server, struct sh_keyboard *keyboard);
void switch_keyboard_layout(struct sh_server *server, int choice);
void layout_short_name(struct sh_server *server, xkb_layout_index_t layout, char *name,
                       size_t size);

/* layer_shell.c */
void arrange_layers(struct sh_server *server);
void server_new_layer_surface(struct wl_listener *listener, void *data);

/* lock.c */
void send_locked_if_presented(struct sh_server *server);
void lock_output_presented(struct sh_output *output);
void server_new_lock(struct wl_listener *listener, void *data);
#if WLR_HAS_SESSION
void session_active(struct wl_listener *listener, void *data);
#endif
void server_new_inhibitor(struct wl_listener *listener, void *data);

/* mirror.c */
struct sh_output *mirror_source(struct sh_server *server, struct sh_output *output);
void mirror_start(struct sh_output *output, struct sh_output *source);
void mirror_stop(struct sh_output *output);
void mirrors_forget_source(struct sh_output *source);
struct sh_output *mirrored_output(const struct sh_output *output);
bool refresh_mirrors(struct sh_server *server);
void mirrors_follow_power(struct sh_output *source, bool on);
void mirror_frame(struct sh_output *output);
bool mirror_capture(struct sh_output *output, const char *path, char *error, size_t error_size);

/* output.c */
bool test_names_output(struct sh_output *output, const char *variable);
bool output_named(const struct sh_output *output, const char *name);
void output_description(const struct wlr_output *output, char *text, size_t size);
bool output_key_matches(const char *key, const struct wlr_output *output);
struct wlr_output *find_output_key(struct sh_server *server, const char *key);
const struct sh_monitor *monitor_settings(const struct sh_settings *settings,
                                          const struct wlr_output *output);
const struct sh_monitor *output_monitor(struct sh_output *output);
void arrange_outputs(struct sh_server *server);
void configure_output(struct sh_server *server, struct sh_output *output);
void apply_output_settings(struct sh_server *server);
bool test_monitor(struct sh_output *output, const struct sh_monitor *monitor);
bool deep_format(uint32_t format);
void output_config_test(struct wl_listener *listener, void *data);
void output_config_apply(struct wl_listener *listener, void *data);
void server_new_output(struct wl_listener *listener, void *data);
struct wlr_output *find_output(struct sh_server *server, const char *name);
struct sh_output *sh_output_for(struct sh_server *server, struct wlr_output *wlr_output);
struct wlr_output *first_output(struct sh_server *server);
struct sh_rect usable_area(struct sh_server *server, struct wlr_output *output);

/* output_moves.c */
void evacuate_output(struct sh_server *server, const char *name, struct wlr_box gone,
                     bool keep_workspaces);
void schedule_evacuation(struct sh_server *server, struct sh_output *output);
void return_home_windows(struct sh_server *server);
void move_toplevel_to_output(struct sh_toplevel *toplevel, struct wlr_output *to);
void move_workspace_to_output(struct sh_server *server, const char *target);
void swap_output_workspaces(struct sh_server *server, const char *target);

/* output_power.c */
bool set_output_power(struct sh_output *output, bool on);
void output_power_set_mode(struct wl_listener *listener, void *data);
bool display_action(enum sh_action action);
bool display_power(struct sh_server *server, enum sh_action action, char *error,
                   size_t error_size);
bool wake_displays(struct sh_server *server);

/* overview.c */
size_t overview_describe(struct sh_server *server, char *text, size_t size);
void overview_select(struct sh_server *server, int index);
void overview_touch(struct sh_server *server, bool relayout);
void overview_forget(struct sh_toplevel *toplevel);
void overview_open(struct sh_server *server);
void overview_assist(struct sh_server *server, struct sh_toplevel *from, struct wlr_output *output,
                     enum sh_action slot, struct sh_rect area, int radius);
void overview_close(struct sh_server *server, struct sh_toplevel *chosen, int workspace);
void overview_dismiss(struct sh_server *server);
void overview_hold(struct sh_server *server, double progress);
void overview_release(struct sh_server *server, bool open);
void overview_confirm(struct sh_server *server, int index);
void overview_view(struct sh_server *server, int workspace);
void overview_set_filter(struct sh_server *server, const char *text);
void overview_key(struct sh_server *server, uint32_t modifiers, xkb_keysym_t sym);
bool overview_button(struct sh_server *server, const struct wlr_pointer_button_event *event);
bool overview_motion(struct sh_server *server);
void overview_hot_corner(struct sh_server *server);
bool overview_axis(struct sh_server *server, const struct wlr_pointer_axis_event *event);

/* placement.c */
struct sh_rect tiling_area(struct sh_server *server, struct wlr_output *output, int workspace,
                           int joining, int *gap);
bool frameless(struct sh_toplevel *toplevel, struct wlr_output *output);
struct sh_rect inside_border(struct sh_server *server, struct sh_rect rect);
struct wlr_output *toplevel_output(struct sh_toplevel *toplevel);
struct wlr_box rebase_box(struct sh_server *server, struct wlr_box box,
                          struct wlr_output *output);
void restore_toplevel(struct sh_toplevel *toplevel);
void place_maximized(struct sh_toplevel *toplevel);
void place_by_hand(struct sh_toplevel *toplevel, enum sh_action action);
bool placed_slot(struct sh_server *server, enum sh_action action, struct wlr_output *output,
                 struct sh_rect *slot);
void place_by_hand_on(struct sh_toplevel *toplevel, enum sh_action action,
                      struct wlr_output *output);
void arrange_windows(struct sh_server *server, enum sh_action action);
void reflow_output(struct sh_server *server, struct wlr_output *output);
struct sh_rect floating_area(struct sh_server *server, struct wlr_output *output);
void unarrange_in_place(struct sh_toplevel *toplevel);
void move_window(struct sh_server *server, enum sh_action action);
void resize_window(struct sh_server *server, enum sh_action action, int amount);

/* power.c */
void power_init(struct sh_server *server);
void power_finish(struct sh_server *server);
void power_reload(struct sh_server *server);
void power_locked(struct sh_server *server);
void power_window_closed(struct sh_server *server);
bool power_action(enum sh_action action);
bool power_start(struct sh_server *server, enum sh_action action, char *error,
                 size_t error_size);
void power_run(struct sh_server *server, enum sh_action action);
bool power_describe(struct sh_server *server, size_t index, const char **name,
                    const char **status);
const char *power_pending(struct sh_server *server, char *text, size_t size);
void power_available(struct sh_server *server, char *list, size_t size);

/* query.c */
bool run_query(struct sh_server *server, int fd, const char *request);

/* scaled_capture.c */
void create_scaled_capture_source(struct wl_client *client, uint32_t id,
                                  struct sh_toplevel *toplevel, uint32_t width,
                                  uint32_t height);
/* A scaled capture source of a window, as `get pictures` describes it. */
struct sh_picture_source {
    int box_width, box_height; // asked for
    int width, height;         // the frame's, 0 before the first
    size_t sessions;
};
size_t list_picture_sources(struct sh_toplevel *toplevel, struct sh_picture_source *sources,
                            size_t size);

/* scratchpad.c */
bool scratchpad_enabled(struct sh_server *server);
void center_scratchpad(struct sh_toplevel *toplevel, struct wlr_output *output);
void hide_in_scratchpad(struct sh_toplevel *toplevel);
void scratchpad_show(struct sh_server *server);
void empty_scratchpad(struct sh_server *server);

/* session.c */
bool make_directories(char *path);
bool session_save(struct sh_server *server, const char *name, int *windows, char *error,
                  size_t error_size);
bool session_restore(struct sh_server *server, const char *name, bool launch,
                     int *restored, int *launched, int *missing, char *error,
                     size_t error_size);
/* As a login session ends and starts, with session.restore on: the session saved as `last`, and
 * put back once startup and autostart have started what they start. */
void session_save_last(struct sh_server *server);
void session_restore_last(struct sh_server *server);
bool session_claim(struct sh_server *server, struct sh_toplevel *toplevel,
                   struct sh_window_rule *rule, bool ruled);

/* shortcuts_inhibit.c */
bool shortcuts_inhibited(struct sh_server *server);
void toggle_shortcuts_inhibit(struct sh_server *server);
void shortcuts_reload(struct sh_server *server);
void describe_shortcuts_inhibitors(struct sh_server *server, int fd,
                                   void (*surface)(struct sh_server *, int, const char *,
                                                   struct wlr_surface *));
void shortcuts_inhibit_init(struct sh_server *server);
void shortcuts_inhibit_finish(struct sh_server *server);

/* snap.c */
const char *snap_zone_name(enum sh_action zone);
void snap_follow(struct sh_server *server);
bool snap_drop(struct sh_server *server);
void snap_preview_hide(struct sh_server *server);
void snap_cycle(struct sh_server *server, enum sh_action direction);
void snap_assist_offer(struct sh_toplevel *toplevel);

/* stacking.c */
struct wlr_scene_tree *window_layer(struct sh_toplevel *toplevel);
bool is_window_layer(struct sh_server *server, const struct wlr_scene_tree *tree);
void restack_toplevel(struct sh_toplevel *toplevel);
void restack_windows(struct sh_server *server);
void set_above(struct sh_toplevel *toplevel, bool above);
void describe_stacking(struct sh_server *server, int fd, const char *arguments);

/* swallow.c */
struct sh_toplevel *swallow_host(struct sh_toplevel *child, bool terminals_only);
bool swallow_wanted(struct sh_toplevel *child);
void swallow_attach(struct sh_toplevel *host, struct sh_toplevel *child);
void swallow_release(struct sh_toplevel *child);
void swallow_end(struct sh_toplevel *toplevel);
void swallow_toggle(struct sh_server *server, struct sh_toplevel *current);

/* switches.c */
bool lid_holds_off(struct sh_server *server, struct sh_output *output);
void apply_lid(struct sh_server *server);
void server_new_switch(struct sh_server *server, struct wlr_input_device *input);
void lid_from_logind(struct sh_server *server, bool closed);
void control_headless_switch(struct sh_server *server, int fd, const char *arguments);
void destroy_headless_switches(struct sh_server *server);

/* switcher.c */
void switcher_close(struct sh_server *server, int index);
void switcher_open(struct sh_server *server, bool backward, uint32_t modifiers,
                   xkb_keysym_t key);
void switcher_forget(struct sh_toplevel *toplevel);
void switcher_key(struct sh_server *server, uint32_t modifiers, xkb_keysym_t sym);

/* tablet.c */
void map_tablets(struct sh_server *server);
void server_new_tablet(struct sh_server *server, struct wlr_input_device *device);
void server_new_tablet_pad(struct sh_server *server, struct wlr_input_device *device);
void describe_tablets(struct sh_server *server, int fd,
                      void (*surface)(struct sh_server *, int, const char *, struct wlr_surface *));
void tablet_init(struct sh_server *server);
void tablet_finish(struct sh_server *server);

/* tearing.c */
bool output_commit_tearing(struct sh_output *output, struct wlr_scene_output *scene_output,
                           const struct wlr_scene_output_state_options *options);
void describe_tearing(struct sh_server *server, int fd, const char *arguments);

/* tiling.c */
struct wlr_output *tiled_output(struct sh_toplevel *toplevel);
enum sh_tile_layout toplevel_layout(struct sh_toplevel *toplevel);
bool output_default_tiling(struct sh_server *server, struct wlr_output *output);
bool workspace_tiles(struct sh_server *server, struct wlr_output *output, int workspace);
bool output_tiles(struct sh_server *server, struct wlr_output *output);
bool tiles_for(struct sh_toplevel *toplevel, struct wlr_output *output);
struct wlr_output *home_output(struct sh_toplevel *toplevel);
bool wants_tiling(struct sh_toplevel *toplevel, struct wlr_output *output);
void tile_toplevel_at(struct sh_toplevel *toplevel, struct wlr_output *output,
                      struct sh_toplevel *target, bool has_point, double x, double y);
void tile_toplevel(struct sh_toplevel *toplevel, struct wlr_output *output,
                   struct sh_toplevel *target, bool at_cursor);
void untile_toplevel(struct sh_toplevel *toplevel, bool restore);
void set_floating(struct sh_toplevel *toplevel, bool floating, bool at_cursor);
void rehome_tiles(struct sh_server *server);
void set_tiling(struct sh_server *server, struct wlr_output *output, bool enabled);
void set_output_tiling(struct sh_server *server, struct wlr_output *output, bool enabled);
void move_tile(struct sh_toplevel *toplevel, struct sh_toplevel *neighbour,
               struct wlr_output *output, bool horizontal, int sign);
void configure_layouts(struct sh_server *server);
void apply_output_layout(struct sh_server *server, const struct wlr_output *output);
void reconfigure_tiling(struct sh_server *server);
void layout_action(struct sh_server *server, enum sh_action action);

/* touch.c */
void map_touchscreens(struct sh_server *server);
void server_new_touch(struct sh_server *server, struct wlr_input_device *input);
void describe_touch(struct sh_server *server, int fd);
void touch_init(struct sh_server *server);
void touch_finish(struct sh_server *server);

/* toplevel.c */
struct wlr_surface *toplevel_surface(struct sh_toplevel *toplevel);
bool toplevel_mapped(struct sh_toplevel *toplevel);
struct wlr_box toplevel_geometry(struct sh_toplevel *toplevel);
void toplevel_set_activated(struct sh_toplevel *toplevel, bool activated);
void toplevel_configure(struct sh_toplevel *toplevel, int x, int y, int width, int height);
void toplevel_configure_box(struct sh_toplevel *toplevel, struct wlr_box box);
struct wlr_box toplevel_box(struct sh_toplevel *toplevel);
void toplevel_set_position(struct sh_toplevel *toplevel, int x, int y);
void toplevel_set_states(struct sh_toplevel *toplevel, bool maximized, uint32_t tiled);
void toplevel_refresh(struct sh_toplevel *toplevel);
void toplevel_close(struct sh_toplevel *toplevel);
const char *toplevel_title(struct sh_toplevel *toplevel);
const char *toplevel_app_id(struct sh_toplevel *toplevel);
bool toplevel_accepts_keyboard(struct sh_toplevel *toplevel);
void maximize_toplevel(struct sh_toplevel *toplevel, bool maximized);
void map_toplevel(struct sh_toplevel *toplevel, bool fullscreen, bool maximized);
void unmap_toplevel(struct sh_toplevel *toplevel);
void free_toplevel(struct sh_toplevel *toplevel);
void fit_fullscreen(struct sh_toplevel *toplevel);
void refit_fullscreen(struct sh_server *server);
void set_fullscreen(struct sh_toplevel *toplevel, bool fullscreen);
void set_client_fullscreen(struct sh_toplevel *toplevel, bool fullscreen);
void server_new_xdg_toplevel(struct wl_listener *listener, void *data);
void create_popup(struct sh_server *server, struct wlr_xdg_popup *xdg_popup,
                  struct wlr_scene_tree *parent_tree);
void server_new_xdg_popup(struct wl_listener *listener, void *data);
void minimize_toplevel(struct sh_toplevel *toplevel);
struct wlr_box fullscreen_box(struct sh_toplevel *toplevel, struct wlr_output *output);
struct wlr_scene_tree *fullscreen_tree(struct sh_toplevel *toplevel);
pid_t toplevel_pid(struct sh_toplevel *toplevel);
bool toplevel_is_dialog(struct sh_toplevel *toplevel);

/* type.c */
/* Types UTF-8 `text` into what has the keyboard; false with what went wrong in `error`. */
bool type_text(struct sh_server *server, const char *text, char *error, size_t error_size);

/* volume.c */
void volume_action(struct sh_server *server, enum sh_action action, int percent);

/* window_control.c */
void window_objects_changed(struct sh_server *server);
void window_objects_forget(struct sh_toplevel *toplevel);
void window_control_init(struct sh_server *server);

/* window_icon.c */
int icon_size_rank(uint32_t width, uint32_t height);
void set_toplevel_icon(struct sh_toplevel *toplevel, struct sh_icon icon);
void free_icon(struct sh_icon *icon);
void server_set_xdg_icon(struct wl_listener *listener, void *data);

/* workspace.c */
int output_slot(struct sh_server *server, const char *name);
int *output_workspace(struct sh_server *server, const char *name);
bool toplevel_visible(struct sh_toplevel *toplevel);
void show_workspaces(struct sh_server *server);
void set_active_output(struct sh_server *server, const char *name);
void show_workspace(struct sh_server *server, const char *output, int workspace);
struct wlr_output *focused_output(struct sh_server *server);
void set_toplevel_output(struct sh_toplevel *toplevel, struct wlr_output *output);
void follow_output(struct sh_toplevel *toplevel);
void set_toplevel_workspace(struct sh_toplevel *toplevel, int workspace);
void switch_workspace(struct sh_server *server, struct wlr_output *output, int workspace);
void workspace_swipe_begin(struct sh_server *server, struct sh_workspace_swipe *swipe,
                           struct wlr_output *output);
void workspace_swipe_hold(struct sh_server *server, struct sh_workspace_swipe *swipe, int target,
                          double t);
void workspace_swipe_end(struct sh_server *server, struct sh_workspace_swipe *swipe, bool finish);
void set_sticky(struct sh_toplevel *toplevel, bool sticky, bool retile);
void move_toplevel_to_workspace(struct sh_server *server, struct sh_toplevel *toplevel,
                                int workspace);
void move_to_workspace(struct sh_server *server, int workspace);
void occupied_workspaces(struct sh_server *server, struct wlr_output *output, char *text,
                         size_t size);
int focused_workspace(struct sh_server *server);

/* xwayland.c */
#if WLR_HAS_XWAYLAND
void server_new_xwayland_surface(struct wl_listener *listener, void *data);
#if PAW_XWM_WAKER
void close_xwm_waker(struct sh_server *server);
#endif
void xwayland_ready(struct wl_listener *listener, void *data);
#endif
