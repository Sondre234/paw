-- SPDX-License-Identifier: GPL-3.0-or-later
-- paw configuration, API version 1.
--
-- Copy this file to ~/.config/paw/init.lua and edit it. Every setting is optional; a
-- line that is commented out shows the default. The full list of settings, types, defaults
-- and ranges is in docs/config-reference.md.
--
-- Check a file without starting the compositor:
--     paw --config init.lua --check-config
-- A misspelled setting, a value of the wrong type, or one out of range is reported with
-- its file and line (and a "did you mean" hint). Saving the file reloads it (auto_reload);
-- while it has an error, the default configuration stands in and the error shows across
-- the top of the screen. Super+Shift+R, or SIGHUP, also reloads it.
--
-- The file is plain Lua that returns a table. It can use variables, string, table and math
-- functions, but cannot read files or run programs, and has 3 seconds and 64 MiB of memory
-- to load in. Launch commands are argument arrays, never shell strings:
-- { "kitty", "--title", "term" }.
--
-- To keep your own file short, start from the shipped one and change only what you like:
--     return { extends = "default", mouse = { modifier = "Alt" } }
-- Its bindings yield to yours on the same keys, and action = "none" removes one.
-- Hyprland-style Super shortcuts. A nested session inside a host that grabs Super
-- (Hyprland, GNOME) never sees them; set mod = "Alt" there.
local mod = "Super"

return {
    version = 1,
    -- `paw import ~/.config` writes theme.lua from Hyprland, Waybar, wallbash, or pywal
    -- files. It fills in whatever this file leaves out, so the look settings below are
    -- comments showing their defaults; set one here to override the import.
    theme = "theme.lua",
    -- appearance = { background = "#19212e" },
    -- Particles drifting over the wallpaper, under the windows: kind = "sakura", "snow" or
    -- "fireflies" (none unless set; a profile may set them too), count on each monitor (30
    -- petals, 60 flakes, 24 fireflies unless set), speed (1 their own pace):
    -- appearance = { ambient = { kind = "sakura", count = 30, speed = 1 } },
    -- Appearance profiles: each lays its appearance, windows and shell settings over the rest
    -- of the configuration while it is in use. Pick one from the panel's right-click menu
    -- ("Appearance"), the command palette, or `paw msg profile NAME|next|prev`; the choice
    -- is kept across restarts. `profile` is the one to start with. A profile's
    -- `appearance.color_scheme` ("light" or "dark") switches applications that follow the
    -- system's style with it: GTK, Firefox, Chromium, Discord and other Electron apps.
    --
    -- paw starts in the macOS style: a menu bar along the top, a dock at the bottom,
    -- Launchpad, Spotlight, traffic-light window controls and soft window shadows. `default` and
    -- `light` are the taskbar style (a bar with a start menu), and `default` is also the one that
    -- shows a theme.lua from `paw import` as it is, since a profile wins over it. The
    -- macOS profiles name the Inter font, which looks closest to macOS's; without it installed
    -- the default sans-serif stands in.
    profile = "macos-light",
    profiles = {
        ["macos-light"] = {
            -- The drawn wallpaper's sky, and applications in their light style
            appearance = { background = "#9dbbe6", color_scheme = "light" },
            windows = {
                controls = "traffic_lights", round = "always", shadow = { enabled = true },
                -- Close, minimize and maximize on the left of GTK's own title bars too (read
                -- as paw starts)
                buttons = "close,minimize,maximize:",
            },
            shell = {
                style = "macos", accent = "#007aff", panel_color = "#f6f6f8bf",
                text_color = "#1d1d1f", font = "Inter", font_size = 13,
                -- The dock: 48-pixel icons, floating 6 pixels above the bottom edge
                panel_height = 64, panel_margin = { bottom = 6 }, panel_radius = 20,
                widgets = { workspaces = false, network = "bar", battery = "bar",
                            volume = "quick", tiling = "quick", wallpapers = "quick",
                            profiles = "quick", notifications = "quick" },
            },
        },
        ["macos-dark"] = {
            appearance = { background = "#1b2440", color_scheme = "dark" },
            windows = {
                controls = "traffic_lights", round = "always", shadow = { enabled = true },
                buttons = "close,minimize,maximize:",
            },
            shell = {
                style = "macos", accent = "#0a84ff", panel_color = "#232326bf",
                text_color = "#f5f5f7", font = "Inter", font_size = 13,
                panel_height = 64, panel_margin = { bottom = 6 }, panel_radius = 20,
                widgets = { workspaces = false, network = "bar", battery = "bar",
                            volume = "quick", tiling = "quick", wallpapers = "quick",
                            profiles = "quick", notifications = "quick" },
            },
        },
        default = {}, -- the taskbar: this file and its theme as they are
        light = {
            appearance = { background = "#dfe4ec", color_scheme = "light" },
            windows = { border_color = "#3d6fd9", border_inactive_color = "#c3cad6" },
            shell = { accent = "#3d6fd9", panel_color = "#f4f6fa", text_color = "#1b2230" },
        },
        -- washi (和紙) and sumi (墨): handmade paper a cat walked across, by day and by night.
        -- washi is soft sakura paper with navy ink, sumi the same sheet soaked in ink by
        -- moonlight; both have their details in light blue and pink. Outlines and printed
        -- shadows, with the bar and the popups framed as the windows are. Their wallpapers sit beside this file
        -- in rices/washi, with themes for kitty, fastfetch, Firefox, Discord, Claude Code and
        -- editors that its install.sh puts in place. They name Space Grotesk (install.sh
        -- --fonts fetches it); without it the default sans-serif stands in.
        washi = {
            appearance = { background = "#f4f0f3", color_scheme = "light" },
            -- Sakura petals drifting down over the paper: add to appearance above
            --     ambient = { kind = "sakura" },
            windows = {
                border_width = 2, corner_radius = 12, round = "always",
                border_color = { "#8fc1ec", "#f2a0be", angle = 135 },
                border_inactive_color = "#4a5170",
                -- No blur and an offset: a shadow printed beside the window, not under it.
                shadow = { enabled = true, color = "#a9c9ecff", inactive_color = "#a9c9ec99",
                           blur = 0, offset = { 7, 7 } },
                urgent_color = "#d9608c",
                magnet = { guide_color = "#d9608ccc" },
                snap = { color = "#8fc1ec40" },
            },
            shell = {
                -- The bar in the paper's own colour, set apart by its outline and shadow alone.
                frame = "windows", accent = "#5e9edb", panel_color = "#f0ecf0",
                text_color = "#363c58", font = "Space Grotesk", font_size = 14,
                panel_height = 46, panel_position = "top", panel_radius = 12,
                panel_margin = { top = 12, left = 24, right = 24 },
                wallpaper = "rices/washi/wallpaper/washi.jpg",
                start_icon = "rices/washi/start/neko.svg",
                -- desktop_clock = { enabled = true, greeting = "ja" },
            },
        },
        sumi = {
            appearance = { background = "#15141d", color_scheme = "dark" },
            -- Fireflies glowing in the ink-dark night: add to appearance above
            --     ambient = { kind = "fireflies" },
            windows = {
                border_width = 2, corner_radius = 12, round = "always",
                border_color = { "#8fc1ec", "#f2a7c3", angle = 135 },
                border_inactive_color = "#34334a",
                shadow = { enabled = true, color = "#000000cc", inactive_color = "#00000080",
                           blur = 36, offset = { 0, 14 } },
                dim_inactive = 0.12,
                urgent_color = "#e8789f",
                magnet = { guide_color = "#e8789fcc" },
                snap = { color = "#8fc1ec40" },
            },
            shell = {
                frame = "windows", accent = "#8fc1ec", panel_color = "#1b1c27",
                text_color = "#e2e0ee", font = "Space Grotesk", font_size = 14,
                panel_height = 46, panel_position = "top", panel_radius = 12,
                panel_margin = { top = 12, left = 24, right = 24 },
                wallpaper = "rices/washi/wallpaper/sumi.jpg",
                start_icon = "rices/washi/start/kuroneko.svg",
            },
        },
        -- mochi (餅): Catppuccin Mocha's palette under frosted glass, and a round cream cat as
        -- soft as the rice cake. The bar, the popups and translucent windows (kitty, editors) show
        -- the wallpaper blurred behind them. Its wallpapers, start button and Mochi icons sit in
        -- rices/mochi, with themes for kitty, fastfetch, Firefox, Discord, Claude Code, editors
        -- and GTK that its install.sh puts in place. It names Nunito (install.sh --fonts);
        -- without it the default sans-serif stands in. Colours from Catppuccin Mocha (MIT).
        mochi = {
            appearance = { background = "#1e1e2e", color_scheme = "dark" },
            -- Soft snow falling past the moon: add to appearance above
            --     ambient = { kind = "snow" },
            windows = {
                border_width = 2, corner_radius = 14, round = "always",
                border_color = { "#cba6f7", "#f5c2e7", angle = 135 },
                border_inactive_color = "#45475a",
                shadow = { enabled = true, color = "#11111bcc", inactive_color = "#11111b80",
                           blur = 40, offset = { 0, 12 } },
                -- The wallpaper frosted behind see-through windows, a little darker and more
                -- vivid so text on them stays clear.
                blur = { enabled = true, radius = 36, noise = 0.02, brightness = 0.9,
                         saturation = 1.3 },
                urgent_color = "#f38ba8",
                magnet = { guide_color = "#cba6f7cc" },
                snap = { color = "#cba6f740" },
            },
            shell = {
                accent = "#cba6f7", panel_color = "#1e1e2ea6", text_color = "#cdd6f4",
                font = "Nunito", font_size = 14,
                panel_height = 44, panel_position = "top", panel_radius = 14,
                panel_margin = { top = 10, left = 16, right = 16 },
                blur = { enabled = true, radius = 48, brightness = 0.85, saturation = 1.2 },
                wallpaper = "rices/mochi/wallpaper/mochi.jpg",
                start_icon = "rices/mochi/start/mochi.svg",
                -- desktop_clock = { enabled = true, position = "left", greeting = "ja" },
            },
        },
    },
    -- Experimental: washi by day and sumi by night, switched at sunrise and sunset (night_light's
    -- times or latitude and longitude, even with night light off, unless sunrise and sunset are
    -- given here). A profile picked by hand stays until the next one; `paw msg profile auto`
    -- hands the choice back to the schedule.
    -- profile_schedule = { day = "washi", night = "sumi" },
    -- profile_schedule = { day = "washi", night = "sumi", sunrise = "07:00", sunset = "20:00" },
    -- keyboard = { layout = "us", variant = "", model = "", options = "", repeat_rate = 25, repeat_delay = 600 },
    -- Several layouts: layout = "us,no", variant = ",nodeadkeys"; or file = "keymap.xkb", a
    -- whole XKB keymap beside this file.
    mouse = {
        modifier = mod, -- modifier + left drag moves; right drag resizes
        -- focus_follows = true, -- hovering a window focuses it (without raising it)
        -- hide_when_typing = true, -- typing into a window hides the cursor until it moves
        -- hide_after = 5, -- seconds the pointer rests before the cursor hides (0: never)
        -- shake_to_find = true, -- shaking the pointer grows the cursor a moment, as on macOS
        -- Standalone sessions only; unset keeps each device's default:
        -- speed = 0.0 (-1 to 1), acceleration = "flat" or "adaptive", natural_scroll = false
    },
    -- touchpad = { natural_scroll = true, tap_to_click = true, disable_while_typing = true },
    -- The monitor touchscreens map to; unset, the one the device names, else the built-in
    -- panel (eDP, LVDS, DSI), else every monitor:
    -- touch = { output = "eDP-1" },
    -- The monitor a drawing tablet's area covers; unset, every monitor:
    -- tablet = { output = "DP-1" },
    layout = {
        -- gap = 8, -- sets both; or gap_inner (between windows) and gap_outer (at the edges)
        -- smart_gaps = true, -- no gaps around a tiled window alone on its workspace
        -- lone_margin = 0, -- pixels around a tiled window alone on its workspace instead;
        --                     or by side: { top = 12, right = 160, bottom = 12, left = 160 }
        workspaces = 4,
        tiling = false, -- automatic tiling on every monitor; outputs.monitors can override it,
        -- and each monitor's panel button toggles it there
        tiling_per_workspace = false, -- toggling tiling affects only the current workspace
        -- Layout a workspace tiles with until layout_next and friends change it: "dwindle",
        -- "master", "spiral", "monocle" or "scroll"; master_ratio is the master column's share of the
        -- width (also a spiral's first tile), master_count the windows in that column.
        tile_layout = "dwindle",
        master_ratio = 0.55,
        master_count = 1,
    },
    windows = {
        -- border_width = 0, -- drawn around each window; tiles shrink to keep it in their slot
        -- corner_radius = 10, -- rounds windows on monitors that tile, and their border; 0: square
        -- round = "tiling", -- or "always": floating windows on any monitor too
        -- controls = "flat", -- or "traffic_lights": macOS's red, yellow and green, top-left
        -- shadow = { enabled = false, color = "#00000059", inactive_color = "#00000033",
        --            blur = 30, offset = 10 },
        -- border_color = "#7da8ff", -- the focused window; #RRGGBBAA also works
        -- border_inactive_color = "#404a5c",
        -- opacity = 1.0,
        -- inactive_opacity = 1.0,
        -- The wallpaper blurred behind every window, for translucent ones (opacity above, or a
        -- terminal's own background opacity): radius in pixels, noise a little grain against
        -- banding, brightness and saturation (0 to 2) scale its colours. Rules' blur = false
        -- leaves a window out, blur = true blurs one with it off:
        -- blur = { enabled = false, radius = 30, noise = 0.02, brightness = 1.0,
        --          saturation = 1.0 },
        -- dim_inactive = 0.0, -- 0 to 0.9: black laid over windows without focus, faded in and out
        -- dim_duration = 180, -- milliseconds of that fade
        -- Games asking for it (or that a rule's allow_tearing = true names) show their frames at
        -- once, tearing, while fullscreen and alone on their monitor:
        -- allow_tearing = false,
        -- What an unfocused application asking for attention gets: "urgent" marks its window
        -- (pulsing border, taskbar badge, workspace dot) and leaves focus alone, "focus" raises
        -- it and switches to its workspace, "ignore" drops the request. Super+U (focus_urgent)
        -- jumps to the window that has waited longest.
        -- activation = "urgent",
        -- urgent_color = "#ff9e64",
        -- Window swallowing: a window started from a terminal (by process ancestry) takes the
        -- terminal's place and hides it until the window closes. terminals are app_ids, matched
        -- without regard to case; exceptions never swallow. The swallow_toggle action does it
        -- by hand, enabled or not, e.g. { mods = { mod }, key = "w", action = "swallow_toggle" }.
        -- swallow = { enabled = false, terminals = { "kitty", "foot", "Alacritty" },
        --             exceptions = {} },
        -- Where a new floating window opens: "cascade" (each 32 pixels down and right of the
        -- last), "center", or "smart" (in the free space, covering the other windows least).
        -- placement = "cascade",
        -- Magnetic edges: a floating window being dragged sticks to the edges of the screen, of
        -- the area panels leave free and of other windows within distance pixels; guides draws a
        -- line along the edge holding it; the bypass modifier ("Shift", "Ctrl", "Alt", "Super"
        -- or "none") held while dragging turns it off.
        -- magnet = { enabled = true, distance = 12, guides = true, guide_color = "#7da8ffb3",
        --            bypass = "Shift" },
        -- Snapping: a floating window dropped with the pointer within distance pixels of the
        -- left or right edge fills that half, of a corner that quarter, of the top edge the
        -- screen; a preview of the slot (color: the focused border's, a quarter opaque) shows
        -- while it would. assist: Snap Assist offers the other windows for the free part.
        -- snap = { enabled = true, distance = 8, corners = true, preview = true, assist = true },
        -- Buttons of windows that draw their own (Firefox's tab strip, GTK apps), in GTK's
        -- button-layout format; "" keeps the desktop's setting. Applications started from
        -- paw see it; restart to change:
        -- buttons = "appmenu:minimize,maximize,close",
        -- Per application, by app_id and/or title (regular expressions; X11 windows match
        -- their class as app_id). Opacity comes from the first match that sets it. Actions
        -- apply as a window opens, every matching rule in order, later ones winning:
        -- floating, workspace (on its monitor, without switching there), output (connector
        -- or "desc:..."), size = { w, h }, position = "center" or { x, y } (from the
        -- monitor's top-left, panels excluded), fullscreen, maximize, focus = false, sticky,
        -- scratchpad (opens hidden there, for scratchpad_show). The look follows the title as
        -- it changes: shadow, border (false: none drawn), border_color, border_inactive_color
        -- (a gradient too), corner_radius; and so does idle_inhibit, which keeps the machine
        -- awake while the window is open ("always"), shows ("visible"), has the focus
        -- ("focus") or is fullscreen ("fullscreen"), or with "none" refuses its own inhibitor.
        -- rules = {
        --     { app_id = "^firefox$", opacity = 0.9, inactive_opacity = 0.85 },
        --     { app_id = "^org.pulseaudio.pavucontrol$", floating = true, size = { 800, 500 },
        --       position = "center" },
        --     { app_id = "^thunderbird$", workspace = 2, focus = false },
        --     { app_id = ".*", idle_inhibit = "fullscreen" }, -- any window while fullscreen
        --     { app_id = "^dropterm$", scratchpad = true, size = { 1000, 500 } },
        --     { title = "Picture-in-Picture", sticky = true, border = false, shadow = false },
        --     { app_id = "^kitty$", border_color = { "#f5c2e7", "#89b4fa", angle = 45 } },
        -- },
    },
    -- Desktop effects, all off until you bind or enable them (see the README, "Effects"):
    -- peek = { opacity = 0.12 },          -- bind action "peek" to a key held to see the desktop
    -- night_light = { enabled = true, night_temperature = 3400, latitude = 59.9, longitude = 10.7 },
    -- zoom = { step = 1.25, scroll_modifier = "Super" }, -- and actions zoom_in/zoom_out/zoom_reset
    -- hot_corners = { bottom_left = "peek_toggle", bottom_right = "night_light_toggle" },
    -- The overview (Super + O): thumbnails glide into a grid over `duration` milliseconds, with a
    -- strip of workspaces; `hot_corner` opens it when the pointer rests in that screen corner, as
    -- a hot corner runs its request (in a corner hot_corners leaves free).
    -- overview = { gap = 24, duration = 180, strip = true, dim = 0.86, hot_corner = "top-left" },
    -- Touchpad swipes: three fingers sideways move between workspaces, the windows following
    -- the fingers, and up and down open and close the overview. A list of swipes replaces these;
    -- an action is a request as a hot corner's ("workspace 2", "spawn foot"). Other swipes,
    -- pinches and holds go to the window under the pointer.
    -- gestures = { enabled = true, distance = 300, invert = false, swipes = {
    --     { fingers = 3, direction = "left", action = "workspace_next" },
    --     { fingers = 3, direction = "right", action = "workspace_prev" },
    --     { fingers = 3, direction = "up", action = "toggle_overview" },
    --     { fingers = 3, direction = "down", action = "overview_cancel" } } },
    -- Windows fade in and out and tiles glide into place, over `duration` milliseconds:
    -- animations = { enabled = true, duration = 120, speed = 1, curve = "ease-out",
    --                move = { curve = "spring" }, open = { duration = 160 } },
    outputs = {
        -- Left to right by connector name; unlisted monitors follow on the right, e.g.:
        -- order = { "HDMI-A-1", "DP-3", "DP-1" },
        -- primary = "DP-3", -- the cursor starts here; without it, the leftmost monitor
        -- Per-monitor mode, scale, position, rotation (0-7), vrr, tiling, or enabled = false,
        -- keyed by connector or by "desc:" and the start of "make model serial", e.g.:
        -- monitors = { ["DP-3"] = { mode = "2560x1440@200", scale = 1.25, tiling = true } },
        -- What the display settings window (Quick Settings) keeps wins over these, for the
        -- monitor it was kept for; its Reset to configuration brings these back.
        -- A laptop's lid closed with another monitor on turns its panel off ("ignore": not):
        -- lid = "clamshell",
    },
    screenshots = {
        -- Saved as Screenshot_<date>_<time>.png; "~/" means your home directory. Empty or unset:
        -- $XDG_PICTURES_DIR/Screenshots, else ~/Pictures/Screenshots.
        -- directory = "~/Pictures/Screenshots",
        clipboard = true, -- also copy the image (needs wl-copy)
        notify = true, -- announce it with notify-send, when that is installed
    },
    -- Optional behaviours; the values shown are the defaults.
    features = {
        -- Sway's scratchpad: move_to_scratchpad (Super+Shift+minus) and scratchpad_show
        -- (Super+minus); false turns them off, and a reload brings hidden windows back.
        scratchpad = true,
        -- toggle_sticky (Super+P) pins a window to every workspace of its monitor; false turns
        -- the action off and returns sticky windows to their monitor's current workspace.
        sticky = true,
        -- resize_left/right/up/down (Super+Ctrl+Shift+arrows); false turns them off.
        keyboard_resize = true,
        -- The actions of windows.rules (floating, workspace, output, size, ...); opacity rules
        -- apply either way.
        window_rules = true,
        -- As sway's workspace_auto_back_and_forth: the `workspace` action for the workspace
        -- already shown on a monitor switches it back to the one it showed before. The
        -- `workspace_back` action (Super+Tab) does that whether this is on or not.
        workspace_back_and_forth = false,
    },
    xwayland = true, -- run X11 applications; Xwayland starts on first use (restart to change)
    auto_reload = true, -- reload when this file (or another .lua file beside it) is saved
    -- The terminal Super+Q opens. Unset, it is $TERMINAL, else the first installed of kitty,
    -- foot, alacritty, wezterm, ghostty, konsole, gnome-terminal and xterm.
    -- terminal = { "foot" },
    shell = {
        enabled = true,
        -- style = "taskbar", -- or "macos": a menu bar along the top and a dock at the bottom,
        --                      the panel settings below applying to the dock
        -- panel_height = 52,
        -- panel_position = "bottom", -- or "top", "left" or "right"
        -- panel_positions = { ["DP-1"] = "left" }, -- a monitor's own edge, by its name
        -- panel_per_monitor = true, -- false: every monitor's bar along panel_position
        -- panel_draggable = false, -- true: drag the bar by its empty space to another edge, or
        --                            by its inner edge to resize it, which is written back into
        --                            this file as its monitor's panel_positions entry (or
        --                            panel_position) or panel_height
        -- panel_dock = true, -- false: a floating bar stays floating while windows reach it;
        --                       "maximized": it docks only for a maximized window, not tiling
        -- mirror_menus = false, -- true: along the top, the bar's right-click menus run the other
        --                         way, closing and killing a window the nearest the bar
        -- panel_margin = 0, -- or { top = 8, right = 12, bottom = 0, left = 12 } to float
        -- panel_radius = 0,
        -- Frosted glass: the bar and the popups show the wallpaper behind them blurred, under
        -- panel_color's translucency (the popups' at least 75 % opaque); drawn through the GPU:
        -- blur = {
        --     enabled = false,
        --     radius = 40,      -- how far the blur spreads, in pixels (2-200)
        --     brightness = 1.0, -- the blurred wallpaper darker below 1, lighter above (0-2)
        --     saturation = 1.0, -- greyer below 1, more vivid above (0-2)
        -- },
        -- font = "", -- family name; empty keeps the default
        -- font_size = 12,
        -- renderer = "gpu", -- or "software" for a weak machine (no effects); read at shell start
        -- icons_only = true, -- taskbar buttons as small icons; false adds each window's title
        -- group_windows = true, -- one button per application, its windows listed on hover;
        --                          false gives every window its own button
        -- taskbar_buttons = "all", -- experimental: which windows each monitor's taskbar shows,
        --                           "main_and_own" (all on the main monitor's, else that monitor's)
        --                           or "own" (each its monitor's); pins on the main one's alone
        -- polkit_agent = true, -- ask for administrator passwords (pkexec, GParted); false
        --                         leaves that to another polkit agent
        -- Resting the pointer on a window's taskbar button shows a small picture of it (a stack's
        -- of each of its windows); the macOS style's dock lists them by name instead:
        -- thumbnails = {
        --     enabled = true, -- false: a tooltip with the title, and a stack's list of titles
        --     delay = 400,    -- milliseconds the pointer rests on the button first (0-2000)
        --     size = 240,     -- width of one picture in pixels (120-480)
        --     live = true,    -- the pictures follow the windows while shown; false takes one
        --     workspaces = true, -- resting on a workspace shows a picture of it too
        -- },
        -- The command palette's and the start menu's search find files too (a leading / finds
        -- only them): those used lately, and the names in these folders, read in the background:
        -- search = {
        --     files = true,
        --     directories = { "~/Documents", "~/Projects" }, -- unset: home and the user folders
        --     depth = 4,          -- levels of folders read below each (1-10)
        --     max_files = 20000,  -- names read at most (100-200000)
        --     web = "https://duckduckgo.com/?q=%s", -- Search the web for “…”; false: none
        -- },
        -- The clipboard history (Super + Shift + V), kept in memory only unless persist:
        -- clipboard = { enabled = true, max_entries = 50, images = true, persist = false },
        -- widgets = { workspaces = true, battery = true, network = true, volume = true,
        --             clock = true, calendar = true, tiling = true, wallpapers = true,
        --             keyboard_layout = true }, -- false hides one; battery, network, volume,
        --             -- tiling, profiles, wallpapers and notifications also take "bar" or
        --             -- "quick" (Quick Settings), and true is their default place
        -- accent = "#7da8ff",
        -- panel_color = "#151e2c", -- #RRGGBBAA makes it translucent
        -- text_color = "#edf2fa",
        -- wallpaper = "", -- absolute path, or relative to this configuration file
        -- wallpaper_mode = "fill", -- or "fit", "center", "tile" or "stretch"; around a picture
        --                            that leaves room, appearance.background shows
        -- Experimental: a large clock on the desktop, over the wallpaper, in the text colour with
        -- a soft shadow; the desktop_clock action (paw msg desktop_clock) shows or hides it:
        -- desktop_clock = {
        --     enabled = false,
        --     position = "center", -- or "top", "bottom", "left", "right", "top-left", ...
        --     time_format = "HH:mm", date_format = "dddd d MMMM", -- Qt's; "" shows no date
        --     size = 96,         -- the time's height in pixels (24-320)
        --     -- color = "#edf2fa", font = "", -- unset: text_color and font
        --     greeting = false,  -- "ja": おはよう, こんにちは, こんばんは by the hour; or "en"
        -- },
        -- wallpapers = "~/Pictures/wallpapers", -- the panel's wallpaper picker browses this folder
        -- Pin applications from the launcher or a window's right-click menu. Launchers here are
        -- for commands without a desktop file; they stay pinned ahead of those, e.g.:
        -- launchers = { { name = "Home", icon = "user-home", command = { "xdg-open", "." } } },
    },
    -- Notifications (the shell answers org.freedesktop.Notifications) and the on-screen display:
    -- notifications = { position = "top-right", timeout = 6000, max_visible = 4, dnd = false },
    -- osd = { position = "bottom", timeout = 1500, volume = true, brightness = true },
    -- The screen locker the lock action starts ({} for none). The stock swaylock wears the
    -- profile's wallpaper, colours and font unless swaylock has a config of its own or
    -- lock_style = false:
    -- power = { lock_command = { "swaylock", "-f" }, lock_style = true },
    -- Power saving after so many seconds without input (0 never): the screens dim (unset, 30
    -- seconds before display_off), the monitors turn off, the screen locks, the machine
    -- suspends; `battery` may hold shorter ones for a laptop on battery, as
    -- battery = { display_off = 300 }. An idle inhibitor (a video playing) holds them off
    -- while its window shows.
    -- display_off = 0 leaves everything to an idle daemon (swayidle).
    -- idle = { display_off = 600, lock = 0, suspend = 0 },
    startup = {}, -- e.g. { { "kitty" } }
    -- A standalone session then starts the XDG autostart entries (~/.config/autostart,
    -- /etc/xdg/autostart), but for those named here:
    -- autostart = { xdg = true, exclude = { "org.kde.discover.notifier.desktop" } },
    -- What a standalone session restores of the last one: "windows" puts back the windows
    -- that open again, "launch" also starts the programs of the others, "off" nothing:
    -- session = { restore = "windows" },
    -- `paw wrapped` reports a year at the desktop from what paw counts on this machine (never
    -- which keys), in ~/.local/state/paw/wrapped; false counts nothing:
    -- wrapped = { enabled = true },
    -- Binding modes, as sway's modes and Hyprland's submaps: while one is in use its bindings take
    -- the place of those below, so bare keys can do the work, and the bar shows its name. A
    -- binding with action = "mode" enters one, and mode = "default" leaves it; each mode needs
    -- such a way out. For a resize mode on Super+Alt+R, add to the bindings below
    --     { mods = { mod, "Alt" }, key = "r", action = "mode", mode = "resize" },
    -- and:
    -- modes = {
    --     resize = {
    --         { key = "Left", action = "resize_left" },
    --         { key = "Right", action = "resize_right" },
    --         { key = "Up", action = "resize_up" },
    --         { key = "Down", action = "resize_down" },
    --         { key = "Escape", action = "mode", mode = "default" },
    --         { key = "Return", action = "mode", mode = "default" },
    --     },
    -- },
    bindings = {
        -- Mouse buttons bind too: left, right, middle, side, extra (most mice's back and
        -- forward thumb buttons), forward, back. app_id (a regular expression) limits one to
        -- windows under the pointer, desktop = true to the bare desktop; elsewhere the click
        -- reaches the application, so a browser keeps its own back and forward:
        -- { button = "side", app_id = "^kitty$", desktop = true, action = "close" },
        -- { button = "extra", app_id = "^kitty$", desktop = true, action = "terminal" },
        -- The wheel binds as wheel_up and wheel_down, once a notch, and the window under the
        -- pointer does not get it: Super + the wheel pages through the workspaces of the
        -- monitor under the pointer (up to the next one).
        { mods = { mod }, button = "wheel_up", action = "workspace_next" },
        { mods = { mod }, button = "wheel_down", action = "workspace_prev" },
        { mods = { mod }, key = "q", action = "terminal" },
        { mods = { mod }, key = "r", action = "launcher" },
        -- Tap Super alone for the launcher (the start menu, or Launchpad in the macOS style), as
        -- Windows' Start key: `release = true` runs a binding as its key comes up, a modifier
        -- key's only when nothing else was pressed, clicked or scrolled while it was held.
        -- { key = "Super_L", release = true, action = "launcher" },
        { mods = { mod }, key = "c", action = "close" },
        { mods = { mod }, key = "m", action = "quit" },
        { mods = { mod }, key = "v", action = "toggle_floating" },
        { mods = { mod, "Shift" }, key = "v", action = "clipboard_history" }, -- copied lately
        { mods = { mod }, key = "semicolon", action = "emoji_picker" }, -- as Windows' Win + ;
        { mods = { mod }, key = "f", action = "fullscreen" },
        { mods = { mod }, key = "t", action = "tile" },
        { mods = { mod }, key = "s", action = "toggle_tiling" },
        { mods = { mod, "Shift" }, key = "p", action = "toggle_sticky" }, -- show on every workspace
        { mods = { mod, "Ctrl" }, key = "t", action = "toggle_above" }, -- keep above the others
        -- The command palette: one search over windows, apps, workspaces, actions and sessions.
        { mods = { mod }, key = "p", action = "palette" },
        -- A sheet of every binding in effect, grouped, with a search; Super + / again closes it.
        -- On a keyboard layout where / takes Shift, bind key = "F1" instead.
        { mods = { mod }, key = "slash", action = "shortcuts" },
        -- A binding's description names it there in your words, as Hyprland's bindd does;
        -- a spawn without one shows its command:
        -- { mods = { mod }, key = "e", action = "spawn", command = { "dolphin" }, description = "Files" },
        -- The taskbar (or the dock) from the keyboard, as Windows' Win+T: the arrows walk its
        -- buttons and the windows they show, Enter picks, Escape gives the keyboard back.
        { mods = { mod }, key = "b", action = "taskbar_focus" },
        { mods = { mod }, key = "u", action = "focus_urgent" }, -- the window asking for attention
        { mods = { mod }, key = "n", action = "notification_history" }, -- the bell's list
        { mods = { mod, "Shift" }, key = "n", action = "dnd_toggle" }, -- do not disturb
        -- The window switcher lists every window on every monitor and workspace, most recently
        -- used first, on the focused monitor; releasing Alt focuses the selected one. Tab and
        -- the arrows move, Return picks, Escape cancels. `cycle` raises the next window at once.
        { mods = { "Alt" }, key = "Tab", action = "switcher" },
        { mods = { "Alt", "Shift" }, key = "Tab", action = "switcher_prev" },
        -- The same over the focused window's application alone, as macOS's Cmd + ` and GNOME's
        -- Alt + `: its windows on every monitor and workspace; ` moves on while Alt is held.
        { mods = { "Alt" }, key = "grave", action = "switcher_app" },
        { mods = { "Alt", "Shift" }, key = "grave", action = "switcher_app_prev" },
        { mods = { mod }, key = "Left", action = "focus_left" },
        { mods = { mod }, key = "Right", action = "focus_right" },
        { mods = { mod }, key = "Up", action = "focus_up" },
        { mods = { mod }, key = "Down", action = "focus_down" },
        { mods = { mod, "Shift" }, key = "Left", action = "move_left" },
        { mods = { mod, "Shift" }, key = "Right", action = "move_right" },
        { mods = { mod, "Shift" }, key = "Up", action = "move_up" },
        { mods = { mod, "Shift" }, key = "Down", action = "move_down" },
        -- Windows' Win+arrows: left and right snap to that half, then on to the next monitor;
        -- up maximizes (from a half, the top quarter); down gives a maximized window its own
        -- size back and minimizes one at its own size (from a half, the bottom quarter).
        { mods = { mod, "Alt" }, key = "Left", action = "snap_cycle_left" },
        { mods = { mod, "Alt" }, key = "Right", action = "snap_cycle_right" },
        { mods = { mod, "Alt" }, key = "Up", action = "snap_cycle_up" },
        { mods = { mod, "Alt" }, key = "Down", action = "snap_cycle_down" },
        -- Resize by 40 pixels, or by `amount = N` added to a binding. A tile moves its split on
        -- that side that way (growing), or the one on its other side when it touches the screen
        -- edge (shrinking); a floating window moves its right or bottom edge that way.
        { mods = { mod, "Ctrl", "Shift" }, key = "Left", action = "resize_left" },
        { mods = { mod, "Ctrl", "Shift" }, key = "Right", action = "resize_right" },
        { mods = { mod, "Ctrl", "Shift" }, key = "Up", action = "resize_up" },
        { mods = { mod, "Ctrl", "Shift" }, key = "Down", action = "resize_down" },
        -- Tiling layouts of the current workspace: layout_next and layout_prev cycle through
        -- dwindle, master, spiral and monocle (or pick one: layout_dwindle, layout_master,
        -- layout_spiral, layout_monocle). promote swaps the focused tile with the first;
        -- focus_next/prev and swap_next/prev step through the tiles in order. master_grow and
        -- master_shrink change the master (or spiral) ratio, master_more and master_less the
        -- number of windows in the master column.
        { mods = { mod }, key = "space", action = "layout_next" },
        { mods = { mod, "Shift" }, key = "space", action = "layout_prev" },
        -- With several keyboard layouts (keyboard.layout = "us,no"), switch_layout moves every
        -- keyboard to the next one; layout = "prev", or a layout's number, picks another.
        -- { mods = { mod, "Alt" }, key = "space", action = "switch_layout" },
        { mods = { mod }, key = "Return", action = "promote" },
        { mods = { mod }, key = "j", action = "focus_next" },
        { mods = { mod }, key = "k", action = "focus_prev" },
        { mods = { mod, "Shift" }, key = "j", action = "swap_next" },
        { mods = { mod, "Shift" }, key = "k", action = "swap_prev" },
        { mods = { mod }, key = "l", action = "master_grow" },
        { mods = { mod }, key = "h", action = "master_shrink" },
        { mods = { mod }, key = "comma", action = "master_more" },
        { mods = { mod }, key = "period", action = "master_less" },
        -- Hyprland's dwindle controls, in dwindle only (experimental): split_toggle turns the
        -- focused tile's split from side by side to stacked and keeps it so, split_swap trades
        -- its halves, pseudo_toggle keeps the window at its floating size in the middle of its
        -- tile. Hyprland's Super+J and Super+P are focus_next and the palette here:
        -- { mods = { mod, "Ctrl" }, key = "j", action = "split_toggle" },
        -- { mods = { mod, "Ctrl" }, key = "k", action = "split_swap" },
        -- { mods = { mod, "Alt" }, key = "p", action = "pseudo_toggle" },
        -- The scroll layout (layout_scroll; see layout.scroll): columns on an endless strip,
        -- whose view turns as a cube as it moves with layout.scroll.cube = true.
        -- master_grow and master_shrink resize the focused column there. Bind these to taste:
        -- { mods = { mod, "Alt" }, key = "h", action = "scroll_left" },
        -- { mods = { mod, "Alt" }, key = "l", action = "scroll_right" },
        -- { mods = { mod, "Ctrl" }, key = "r", action = "column_cycle_width" },
        -- { mods = { mod, "Ctrl" }, key = "c", action = "center_column" },
        -- { mods = { mod, "Ctrl" }, key = "bracketleft", action = "consume_left" },
        -- { mods = { mod, "Ctrl" }, key = "bracketright", action = "consume_right" },
        -- { mods = { mod }, key = "e", action = "expel" },
        -- Also available: snap_left, snap_right (half the screen), snap_top_left,
        -- snap_top_right, snap_bottom_left, snap_bottom_right (a quarter), maximize, restore.
        -- Lock with power.lock_command (swaylock -f unless set); the power menu offers lock,
        -- suspend, hibernate, restart, power off and log out. suspend, hibernate, reboot,
        -- poweroff and logout bind directly too, without asking, e.g.:
        -- { mods = { mod, "Ctrl" }, key = "Escape", action = "suspend" },
        { mods = { mod, "Shift" }, key = "l", action = "lock" },
        { mods = { mod }, key = "Escape", action = "power_menu" },
        -- A virtual machine, a remote desktop or a game may ask for the keys bound here while it
        -- has the keyboard (keyboard.shortcuts_inhibit). This binding still runs: it takes them
        -- back, and gives them to it again.
        { mods = { mod, "Shift" }, key = "Escape", action = "toggle_shortcuts_inhibit" },
        -- Turn every monitor off until a key or the mouse wakes it (display_on and
        -- display_toggle too; output = "HDMI-A-1" picks one monitor), e.g.:
        -- { mods = { mod, "Alt" }, key = "Escape", action = "display_off" },
        -- Keep awake: no dimming, no monitors going off and no sleep until toggled off again
        -- (keep_awake_on and keep_awake_off too; also a tile in Quick Settings), e.g.:
        -- { mods = { mod, "Alt" }, key = "c", action = "keep_awake_toggle" },
        -- Switches act too: the lid ("close", "open") and tablet mode ("on", "off"), e.g.:
        -- { switch = "lid", state = "close", action = "lock" },
        { mods = { mod }, key = "1", action = "workspace", workspace = 1 },
        { mods = { mod }, key = "2", action = "workspace", workspace = 2 },
        { mods = { mod }, key = "3", action = "workspace", workspace = 3 },
        { mods = { mod }, key = "4", action = "workspace", workspace = 4 },
        { mods = { mod, "Shift" }, key = "1", action = "move_to_workspace", workspace = 1 },
        { mods = { mod, "Shift" }, key = "2", action = "move_to_workspace", workspace = 2 },
        { mods = { mod, "Shift" }, key = "3", action = "move_to_workspace", workspace = 3 },
        { mods = { mod, "Shift" }, key = "4", action = "move_to_workspace", workspace = 4 },
        { mods = { "Ctrl", mod }, key = "Right", action = "workspace_next" },
        { mods = { "Ctrl", mod }, key = "Left", action = "workspace_prev" },
        -- Take the focused window along to the next or previous workspace (or with
        -- move_to_workspace_empty to the first one with no windows); the view goes with it.
        { mods = { "Ctrl", "Alt", mod }, key = "Right", action = "move_to_workspace_next" },
        { mods = { "Ctrl", "Alt", mod }, key = "Left", action = "move_to_workspace_prev" },
        { mods = { mod }, key = "Tab", action = "workspace_back" }, -- the previous workspace
        -- Send the focused monitor's workspace, windows and layout to the monitor on the left or
        -- right (which then shows it); swap_workspaces trades all workspaces of two monitors.
        { mods = { "Ctrl", mod }, key = "comma", action = "move_workspace_to_output", output = "left" },
        { mods = { "Ctrl", mod }, key = "period", action = "move_workspace_to_output", output = "right" },
        -- { mods = { "Ctrl", mod }, key = "x", action = "swap_workspaces", output = "next" },
        { mods = { mod }, key = "grave", action = "focus_last" }, -- the window focused before
        -- The overview (Expose): live thumbnails of the monitor's windows in a grid, workspaces
        -- in a strip above. Type to filter, arrows or the mouse pick, Return or a click focuses,
        -- a middle click or Delete closes, dragging onto the strip moves to that workspace.
        { mods = { mod }, key = "o", action = "toggle_overview" },
        -- Window groups (tabs): Super+G makes the focused window a group, so windows opened next
        -- join it as tabs in one tile, and dissolves a group; [ and ] show another tab.
        -- group_merge_left/right/up/down move a window into its neighbour's group.
        { mods = { mod }, key = "g", action = "group_toggle" },
        { mods = { mod, "Shift" }, key = "g", action = "ungroup" },
        { mods = { mod }, key = "bracketright", action = "group_next" },
        { mods = { mod }, key = "bracketleft", action = "group_prev" },
        -- Sway's scratchpad: hide the focused window there; show a hidden one floating in the
        -- middle of the focused monitor, hide it again, or show the next one.
        { mods = { mod, "Shift" }, key = "minus", action = "move_to_scratchpad" },
        { mods = { mod }, key = "minus", action = "scratchpad_show" },
        { mods = { mod, "Shift" }, key = "r", action = "reload" },
        -- Screenshots run grim. With Print, slurp lets you drag out a region or click a window
        -- or monitor.
        { mods = {}, key = "Print", action = "screenshot", mode = "region" },
        { mods = { "Shift" }, key = "Print", action = "screenshot", mode = "output" },
        { mods = { mod }, key = "Print", action = "screenshot", mode = "window" },
        -- Pick a colour off the screen: a loupe follows the pointer, and a click copies the
        -- pixel's #rrggbb (Escape or a right click cancels; `paw msg pick_color` prints it), e.g.:
        -- { mods = { mod, "Shift" }, key = "c", action = "pick_color" },
        -- The keyboard's volume, microphone and brightness keys, 5 % a press (`amount = N` for
        -- another step). The shell carries them out and shows them; without it, wpctl and
        -- brightnessctl do. `locked = true` runs a binding while the screen is locked too (every
        -- other key goes to the lock screen), and `repeats = true` again while its key is held.
        { mods = {}, key = "XF86AudioRaiseVolume", action = "volume_up", locked = true, repeats = true },
        { mods = {}, key = "XF86AudioLowerVolume", action = "volume_down", locked = true, repeats = true },
        { mods = {}, key = "XF86AudioMute", action = "volume_mute", locked = true },
        { mods = {}, key = "XF86AudioMicMute", action = "mic_mute", locked = true },
        { mods = {}, key = "XF86MonBrightnessUp", action = "brightness_up", locked = true, repeats = true },
        { mods = {}, key = "XF86MonBrightnessDown", action = "brightness_down", locked = true, repeats = true },
        -- The media keys go to the player playing most lately (MPRIS), the one Quick Settings
        -- shows; on the lock screen too.
        { mods = {}, key = "XF86AudioPlay", action = "media_play_pause", locked = true },
        { mods = {}, key = "XF86AudioPause", action = "media_play_pause", locked = true },
        { mods = {}, key = "XF86AudioNext", action = "media_next", locked = true },
        { mods = {}, key = "XF86AudioPrev", action = "media_previous", locked = true },
        { mods = {}, key = "XF86AudioStop", action = "media_stop", locked = true },
        -- A laptop's display key (Fn and the key with a monitor on it), as Windows' Win+P: a popup
        -- steps through PC screen only, duplicate, extend and second screen only, and takes the
        -- one shown once the key rests. mode = "duplicate" (or "extend", "internal", "external")
        -- picks one at once.
        { mods = {}, key = "XF86Display", action = "display_mode" },
        -- A desktop cat that lives on the focused window's top edge (cat.enabled lets it out as
        -- the session starts; mode = "on" or "off" rather than toggling):
        -- { mods = { mod, "Ctrl" }, key = "n", action = "cat" },
        -- Sakura petals, snow or fireflies over the wallpaper, each in turn and then none
        -- (appearance.ambient sets them for good; mode = "sakura", "snow", "fireflies", "off" or
        -- "toggle" picks one):
        -- { mods = { mod, "Ctrl" }, key = "a", action = "ambient", mode = "next" },
    },
}
