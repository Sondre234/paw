// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "paw/backend.h"
#include "paw/swipe.h"
#include <array>
#include <filesystem>
#include <optional>
#include <regex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace paw {
using Command = std::vector<std::string>;
// Pixels a resize_* action moves an edge by, unless its binding or request gives `amount`.
constexpr int default_resize_amount = 40;
constexpr int max_resize_amount = 4000;
// Percent a volume or brightness step changes by, unless its binding or request gives `amount`.
constexpr int default_step_percent = 5;
// The most layouts a keymap has (xkbcommon's limit), for switch_layout's number.
constexpr int max_layouts = 32;

struct Binding {
    uint32_t modifiers;
    uint32_t keysym; // 0 for a mouse button binding
    uint32_t button = 0; // a Linux BTN_* code; 0 for a key binding
    // A button binding acts only over a window whose app ID matches `pattern` (its `app_id`, an
    // ECMAScript regex, searched) or, with `desktop`, over the bare desktop; with neither,
    // anywhere. Elsewhere the click reaches the application under the pointer.
    std::optional<std::regex> pattern;
    bool desktop = false;
    sh_action action;
    Command command;
    int workspace = 0; // for workspace and move_to_workspace, from 1
    sh_screenshot_mode screenshot = SH_SCREENSHOT_REGION; // for screenshot
    int amount = default_resize_amount; // resize_*: pixels; volume and brightness steps: percent
    int layout = 0; // for switch_layout: 0 the next layout, -1 the previous, N the Nth from 1
    std::string output; // for move_workspace_to_output and swap_workspaces: the target
    // Key bindings only: `locked` runs while the session is locked too (Hyprland's bindl), and
    // `repeats` runs again while the key is held (Hyprland's binde), by default for resize_*.
    bool locked = false;
    bool repeats = false;
    int mode = 0; // for mode: 0 for the bindings outside any mode, else modes[mode - 1]; for
                  // display_mode, an sh_display_mode
    int switch_type = -1; // the enum sh_switch of a switch binding; -1 for a key or button
    bool switch_on = false; // with switch_type: it acts as the switch turns on (a lid closing)
};

// A binding mode, as sway's modes and Hyprland's submaps: key bindings that take the place of
// `bindings` while the mode is in use.
struct Mode {
    std::string name;
    std::vector<Binding> bindings;
};

struct Launcher {
    std::string name;
    std::string icon;
    Command command;
};

// What rules do to a window as it opens; unset fields leave it to the defaults.
struct WindowActions {
    enum class Position { Unset, Center, At };
    std::optional<bool> floating, fullscreen, maximize, focus, sticky, above, allow_tearing;
    std::optional<bool> shortcuts_inhibit; // false: the window's inhibitors are refused
    std::optional<int> workspace;      // from 1
    std::optional<std::string> output; // connector, or "desc:" and the start of its description
    std::optional<std::pair<int, int>> size;
    Position position = Position::Unset;
    int x = 0, y = 0; // with Position::At, from the top-left of the output's usable area
    bool empty() const;
    // Fields `other` sets replace these.
    void merge(const WindowActions &other);
    sh_window_rule to_c() const;
    // The state a dynamic rule holds a window to while it matches (above, floating, sticky), and
    // the rest, which acts once as the window opens.
    WindowActions held() const;
    WindowActions once() const;
};

// Windows whose app ID and title match `app_id` and `title` (ECMAScript regexes, searched;
// an absent one matches anything) get these opacities and actions.
struct WindowRule {
    std::string app_id, title;
    std::optional<std::regex> pattern, title_pattern;
    bool sets_opacity = true; // takes part in window_opacity
    float opacity = 1, inactive_opacity = 1;
    WindowActions actions;
    // dynamic = true: what it holds of the window's state (WindowActions::held) follows its title
    // and app ID as they change, and is given back as it stops matching.
    bool dynamic = false;
    bool matches(const std::string &app_id, const std::string &title) const;
};

// Where a panel widget that can move sits: on the bar, in the Quick Settings flyout, or nowhere.
enum class WidgetPlace { Hidden, Bar, Quick };

// Which widgets the panel shows, and where. Each is switched off with
// `shell.widgets = { name = false }`; one that can move goes on the bar with "bar" or into Quick
// Settings with "quick", and `true` puts it in its default place.
struct ShellWidgets {
    bool workspaces = true; // this monitor's workspace numbers
    WidgetPlace battery = WidgetPlace::Quick; // charge and state, only where a battery exists
    WidgetPlace network = WidgetPlace::Quick; // connection state, only where an interface exists
    WidgetPlace volume = WidgetPlace::Quick;  // the default output's volume, only with a sound server
    bool clock = true;
    bool calendar = true; // the month calendar in the clock's flyout
    WidgetPlace tiling = WidgetPlace::Bar;     // tiling on or off for the monitor
    WidgetPlace profiles = WidgetPlace::Quick; // the appearance profiles, only with two or more
    WidgetPlace wallpapers = WidgetPlace::Bar; // the wallpaper picker
    bool keyboard_layout = true; // the active keyboard layout, only with two or more
    bool power = true;      // the launcher's power button, only with something in its menu
    bool tray = true;       // the system tray, only while applications show icons in it
    // Do-not-disturb, as a tile, or a bell on the bar with the unread count; the clock shows both
    // either way.
    WidgetPlace notifications = WidgetPlace::Quick;
    bool media = true; // the now-playing card in Quick Settings, only while a media player is there
    bool power_mode = true; // a power mode tile in Quick Settings, only while power-profiles-daemon runs
    bool bluetooth = true;  // a Bluetooth tile in Quick Settings, only while BlueZ has an adapter
};

// Lua `shell.thumbnails`: pictures of the windows of a taskbar button resting under the pointer.
struct ShellThumbnails {
    bool enabled = true;
    int delay = 400; // milliseconds the pointer rests on the button first
    int size = 240;  // width of one picture in pixels; 5/8 of it tall
    bool live = true; // pictures follow the windows while shown; false: one just before the card opens
};

// Lua `shell.search`: what the command palette's and the start menu's search find besides
// applications, windows and actions.
struct ShellSearch {
    bool files = true; // recent files, and the names in the folders below
    // Absolute or "~/..."; empty: the home folder and the XDG user folders.
    std::vector<std::string> directories;
    int depth = 4;         // levels of folders read below each
    int max_files = 20000; // names read at most
    // The last result, Search the web for “…”, opens this address, %s the words typed; empty
    // (web = false) leaves it out.
    std::string web = "https://duckduckgo.com/?q=%s";
};

// Lua `shell.clipboard`: the history of what is copied, as Windows' Win + V keeps it.
struct ShellClipboard {
    bool enabled = true;
    int max_entries = 50; // kept besides the pinned ones, the oldest going first
    bool images = true;   // pictures copied too, not only text
    bool persist = false; // kept across sessions in $XDG_STATE_HOME/paw/clipboard
};

struct ShellConfig {
    bool enabled = true;
    bool macos_style = false;           // style = "macos": a menu bar and a dock
    int panel_height = 52;
    bool panel_top = false;             // panel_position = "top"
    int panel_margin[4] = {0, 0, 0, 0}; // top, right, bottom, left: a floating bar
    int panel_radius = 0;
    std::string font;   // family; empty: the Qt default
    int font_size = 12; // taskbar text, in pixels
    bool icons_only = true; // taskbar buttons show the window's icon, its title as a tooltip
    bool software_renderer = false; // renderer = "software": Qt Quick without the GPU, faster to start
    bool group_windows = true; // one taskbar button per application, its windows listed on hover
    // The session's polkit authentication agent: the dialog asking for the password a program
    // needs (pkexec, GParted), unless another agent serves the session already.
    bool polkit_agent = true;
    ShellThumbnails thumbnails;
    ShellSearch search;
    ShellClipboard clipboard;
    int workspaces_shown = 0;  // the workspace indicator shows this many, around the current; 0: all
    std::string accent = "#7da8ff";
    std::string panel_color = "#151e2c";
    std::string text_color = "#edf2fa";
    std::string wallpaper;
    std::string wallpapers; // the picker's folder: absolute or "~/..."; empty: $XDG_PICTURES_DIR/wallpapers
    ShellWidgets widgets;
    std::vector<Launcher> launchers;
};

struct ScreenshotConfig {
    std::string directory; // absolute or "~/..."; empty: $XDG_PICTURES_DIR/Screenshots
    bool clipboard = true; // also copy the image with wl-copy
    bool notify = true;    // announce the file with notify-send, when it is installed
};

// Lua `power`.
struct PowerConfig {
    // The screen locker the `lock` action starts; empty for none.
    Command lock_command{"swaylock", "-f"};
    // Seconds the shell's confirmation of power off, reboot and log out counts down before it
    // goes ahead; 0 waits for a click.
    int countdown = 10;
};

// Where the notification cards and the on-screen display sit on the focused monitor.
enum class Corner { TopRight, TopLeft, BottomRight, BottomLeft };

struct NotificationsConfig {
    bool enabled = true;     // serve org.freedesktop.Notifications and show the cards
    Corner position = Corner::TopRight;
    int timeout = 6000;      // milliseconds before a card without its own timeout goes; 0: never
    int max_visible = 4;     // cards shown at once; the rest wait in a queue
    bool dnd = false;        // start in do-not-disturb: no cards, everything goes to the history
    int width = 360;         // card width in pixels
    int history = 100;       // notifications kept in the history popover
};

// Lua `autostart`: the XDG autostart entries a standalone session starts after `startup`.
struct AutostartConfig {
    bool xdg = true;                  // start them
    std::vector<std::string> exclude; // their file names ("foo.desktop") never started
};

struct OsdConfig {
    bool enabled = true;      // the on-screen display for volume, brightness and `paw msg osd`
    bool top = false;         // position = "top": near the top edge instead of the bottom
    int timeout = 1500;       // milliseconds shown before it fades out
    bool volume = true;       // show it when the default output's volume or mute changes
    bool brightness = true;   // show it when a backlight's brightness changes
};

struct Config {
    sh_settings settings{.background = {25 / 255.0F, 33 / 255.0F, 46 / 255.0F, 1.0F},
                         .mouse_modifier = SH_ALT,
                         .repeat_rate = 25,
                         .repeat_delay = 600,
                         .gap_inner = 8,
                         .gap_outer = 8,
                         .smart_gaps = true,
                         .floating_above_tiles = false,
                         .keyboard_layout = "us",
                         .keyboard_variant = "",
                         .keyboard_model = "",
                         .keyboard_options = "",
                         .keyboard_rules = "",
                         .keyboard_file = "",
                         .xwayland = true,
                         .tiling = false,
                         .tiling_per_workspace = false,
                         .tile_layout = 0,
                         .master_ratio = 0.55F,
                         .master_count = 1,
                         .scroll_follow = SH_SCROLL_FOLLOW_CENTER,
                         .scroll_width = 0.5F,
                         .scroll_step = 0.1F,
                         .scroll_presets = {1.0F / 3, 0.5F, 2.0F / 3, 1.0F},
                         .scroll_preset_count = 4,
                         .workspaces = 4,
                         .output_order = {},
                         .output_count = 0,
                         .primary_output = "",
                         .return_windows = true,
                         .monitors = {},
                         .monitor_count = 0,
                         .output_layouts = {},
                         .output_layout_count = 0,
                         .border_width = 0,
                         .corner_radius = 10,
                         .round_always = false,
                         .border_active = {0.49F, 0.66F, 1.0F, 1.0F},
                         .border_inactive = {0.25F, 0.29F, 0.36F, 1.0F},
                         .border_active_gradient = {{{0.49F, 0.66F, 1.0F, 1.0F}}, 1, 0},
                         .border_inactive_gradient = {{{0.25F, 0.29F, 0.36F, 1.0F}}, 1, 0},
                         .pointer_speed = 0,
                         .pointer_speed_set = false,
                         .pointer_accel = -1,
                         .mouse_natural_scroll = -1,
                         .touchpad_natural_scroll = -1,
                         .touchpad_tap = -1,
                         .touchpad_dwt = -1,
                         .focus_follows_mouse = true,
                         .animations = true,
                         .animation_speed = 1.0F,
                         .animation_late_ms = 80,
                         .animation_slide = 0.08F,
                         .animation_styles = {{120, {SH_CURVE_EASE_OUT, {}}},        // open
                                              {120, {SH_CURVE_EASE_OUT, {}}},        // close
                                              {120, {SH_CURVE_SPRING, {}}},          // move
                                              {120, {SH_CURVE_EASE_OUT_QUINT, {}}},  // workspace
                                              {120, {SH_CURVE_EASE_OUT, {}}},        // fullscreen
                                              {120, {SH_CURVE_EASE_OUT, {}}}},       // focus
                         .workspace_back_and_forth = false,
                         .scratchpad = true,
                         .sticky = true,
                         .keyboard_resize = true,
                         .window_rules = true,
                         .groups = true,
                         .group_join_new = true,
                         .overview = true,
                         .overview_gap = 24,
                         .overview_animation = true,
                         .overview_duration = 180,
                         .overview_strip = true,
                         .overview_hot_corner = 0,
                         .overview_dim = 0.86F,
                         .dim_inactive = 0,
                         .dim_duration = 180,
                         .allow_tearing = false,
                         .activation = SH_ACTIVATION_URGENT,
                         .urgent_color = {1.0F, 0.62F, 0.39F, 1.0F},
                         .effects = {.peek_opacity = 0.12F,
                                     .peek_duration = 150,
                                     .night_light = false,
                                     .day_kelvin = 6500,
                                     .night_kelvin = 3400,
                                     .sunrise = 7 * 60,
                                     .sunset = 20 * 60,
                                     .latitude = 0,
                                     .longitude = 0,
                                     .located = false,
                                     .transition = 30,
                                     .corner_size = 2,
                                     .corner_delay = 150,
                                     .corner_mask = 0,
                                     .zoom_step = 1.25F,
                                     .zoom_max = 8,
                                     .zoom_duration = 150,
                                     .zoom_scroll_modifier = 0},
                         .swallow = false,
                         .swallow_terminals = {"kitty", "foot", "footclient", "Alacritty",
                                               "org.wezfurlong.wezterm", "com.mitchellh.ghostty",
                                               "xterm", "URxvt", "org.kde.konsole",
                                               "org.gnome.Terminal"},
                         .swallow_terminal_count = 10,
                         .swallow_exceptions = {},
                         .swallow_exception_count = 0,
                         .magnet = true,
                         .magnet_distance = 12,
                         .magnet_guides = true,
                         .magnet_bypass = SH_SHIFT,
                         .magnet_guide_color = {0.49F * 0.7F, 0.66F * 0.7F, 0.7F, 0.7F},
                         .snap = true,
                         .snap_distance = 8,
                         .snap_corners = true,
                         .snap_preview = true,
                         .snap_color = {0.49F * 0.25F, 0.66F * 0.25F, 0.25F, 0.25F},
                         .snap_assist = true,
                         .placement = SH_PLACE_CASCADE,
                         .drag_strip = 6,
                         .window_controls = SH_CONTROLS_FLAT,
                         .shadow = false,
                         .shadow_blur = 30,
                         .shadow_x = 0,
                         .shadow_y = 10,
                         .shadow_color = {0, 0, 0, 0x59 / 255.0F},
                         .shadow_inactive_color = {0, 0, 0, 0x33 / 255.0F},
                         .lock_before_sleep = true,
                         .close_windows = true,
                         .close_timeout = 5000,
                         .close_force = false,
                         .session_restore = SH_SESSION_RESTORE_WINDOWS,
                         .gestures = {.enabled = true,
                                      .distance = 300,
                                      .invert = false,
                                      .swipes = {{3, SH_SWIPE_LEFT, SH_WORKSPACE_NEXT, "workspace_next"},
                                                 {3, SH_SWIPE_RIGHT, SH_WORKSPACE_PREV, "workspace_prev"},
                                                 {3, SH_SWIPE_UP, SH_OVERVIEW_TOGGLE, "toggle_overview"},
                                                 {3, SH_SWIPE_DOWN, SH_OVERVIEW_CANCEL, "overview_cancel"}},
                                      .swipe_count = 4},
                         .touch_output = "",
                         .tablet_output = "",
                         .idle = {.dim = 570000, .display_off = 600000, .lock = 0, .suspend = 0},
                         .idle_battery = {.dim = 570000, .display_off = 600000, .lock = 0,
                                          .suspend = 0},
                         .lid = SH_LID_CLAMSHELL,
                         .shortcuts_inhibit = true};
    // layout.workspace_names: the label of workspace N is names[N - 1]; "" or past the end: none.
    std::vector<std::string> workspace_names;
    // hot_corners: what each corner runs, as a control request; "" for nothing.
    std::array<std::string, 4> hot_corners;
    std::vector<Binding> bindings;
    // modes, sorted by name; a mode's number is its place from 1, the bindings outside any being 0.
    std::vector<Mode> modes;
    std::vector<Command> startup;
    AutostartConfig autostart;
    // The terminal the `terminal` action opens; empty: $TERMINAL or the first one installed.
    Command terminal;
    ShellConfig shell;
    NotificationsConfig notifications;
    OsdConfig osd;
    ScreenshotConfig screenshots;
    PowerConfig power;
    float opacity = 1, inactive_opacity = 1;
    std::vector<WindowRule> window_rules;
    // GTK's button layout for client-decorated windows (Firefox's tab strip); empty: GTK's own.
    std::string window_buttons = "appmenu:minimize,maximize,close";
    // profiles: their names, sorted, and the one in use ("" for none).
    std::vector<std::string> profiles;
    std::string profile;
    // auto_reload: reload when a Lua file beside the configuration is saved.
    bool auto_reload = true;

    // The workspace number, from 1, named `name` (or written as a number in range), else 0.
    int workspace_number(const std::string &name) const;
    // "N" or "N name": how a workspace is labelled.
    std::string workspace_label(int workspace) const;

    const Binding *binding(uint32_t modifiers, uint32_t keysym) const;
    // The key binding of mode `mode` (0, or a number no mode has: the bindings outside any).
    const Binding *mode_binding(int mode, uint32_t modifiers, uint32_t keysym) const;
    // 0 for "default", a mode's number for its name, else -1.
    int mode_number(const std::string &name) const;
    // "default" and every mode's name.
    std::vector<std::string> mode_names() const;
    // The keys of the first key binding of `action` outside any mode, as people read them:
    // "Super + Shift + Escape"; empty when no key is bound to it.
    std::string binding_keys(sh_action action) const;
    // The binding of a switch turning on or off, "none" included; nullptr when there is none.
    const Binding *switch_binding(sh_switch type, bool on) const;
    // The first button binding for what lies under the pointer, or nothing (the click belongs
    // to the application, as does one whose first match is action = "none").
    const Binding *button_binding(uint32_t modifiers, uint32_t button, sh_pointer_target target,
                                  const std::string &app_id) const;
    // The first matching rule that sets opacity decides; otherwise the windows.opacity defaults.
    float window_opacity(const std::string &app_id, const std::string &title, bool active) const;
    float window_opacity(const std::string &app_id, bool active) const {
        return window_opacity(app_id, "", active);
    }
    // The actions of every matching rule, later rules winning; none with
    // features.window_rules = false. Dynamic rules give only their actions that act once.
    WindowActions window_actions(const std::string &app_id, const std::string &title) const;
    // What the dynamic rules matching now hold the window to (WindowActions::held), later rules
    // winning; none with features.window_rules = false.
    WindowActions dynamic_actions(const std::string &app_id, const std::string &title) const;
};

// Maps a Lua/control-socket action name; throws for unknown names.
sh_action parse_action(const std::string &name);
bool action_takes_workspace(sh_action action);
// resize_*: pixels, 1 to max_resize_amount; volume_up, volume_down, brightness_up and
// brightness_down: percent, 1 to 100.
bool action_takes_amount(sh_action action);
// The amount such an action takes when none is given, and the most it takes.
int default_amount(sh_action action);
int max_amount(sh_action action);
// move_workspace_to_output and swap_workspaces: an output target, "left", "right", "next",
// "prev", a connector name, or "desc:..."
bool action_takes_output(sh_action action);
// Whether `target` reads as one of those (it says nothing of whether such an output exists).
bool valid_output_target(const std::string &target);
// display_off, display_on and display_toggle: a monitor's connector name or "desc:...", which
// may be left out for every monitor.
bool action_takes_display(sh_action action);
// "region", "output", or "window"; throws for other names.
sh_screenshot_mode parse_screenshot_mode(const std::string &name);
// "extend", "duplicate", "internal" or "external"; throws for other names.
sh_display_mode parse_display_mode(const std::string &name);
// switch_layout's "next" (0), "prev" (-1), or a layout's number from 1; throws for others.
int parse_layout_choice(const std::string &word);
// `words` with `separator` between them.
std::string join(std::span<const std::string> words, std::string_view separator = ", ");

// Parse into a fresh value; callers replace the active configuration only on success.
// A configuration's `theme = "FILE"` (relative to `directory`) supplies every setting it omits.
// `extends = "default"` then supplies what both omit from the default configuration
// ($PAW_DEFAULT_CONFIG, else the installed one); its bindings yield to the configuration's own
// on the same keys, and a binding with action = "none" removes a default one.
Config load_config(const std::filesystem::path &path);
// load_config, except that a configuration with an error gives the default one instead
// ($PAW_DEFAULT_CONFIG, else the installed one, else the source tree's when paw runs
// uninstalled, else the built-in values). `error` receives
// what was wrong, or "" when `path` loaded.
Config load_config_or_default(const std::filesystem::path &path, std::string &error);
// `profiles` lay settings over everything else: the one named `chosen` when there is such a
// profile, else the one `profile` names. load_config chooses the saved profile.
Config parse_config(const std::string &source, const std::string &name = "config",
                    const std::filesystem::path &directory = {}, const std::string &chosen = {});
// The shipped default configuration: $PAW_DEFAULT_CONFIG, else the installed one.
std::filesystem::path default_config_path();
// Where the profile picked from the shell or `paw msg profile` is kept:
// $XDG_STATE_HOME/paw/profile, else ~/.local/state/paw/profile; empty without either.
std::filesystem::path profile_state_path();
// The saved profile name, or "" when none was saved.
std::string saved_profile();
// Saves `name` as the profile to use; throws when it cannot be written.
void save_profile(const std::string &name);
// Settings, as "shell.accent", that the configuration at `path` sets itself although its theme
// file sets them too. Nothing when the configuration names no theme.
std::optional<std::vector<std::string>> shadowed_settings(const std::filesystem::path &path);
} // namespace paw
