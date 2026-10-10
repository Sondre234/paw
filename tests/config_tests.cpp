// SPDX-License-Identifier: GPL-3.0-or-later
#include "paw/config.hpp"
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <xkbcommon/xkbcommon-keysyms.h>

void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void rejects(const std::string &source) {
    try {
        (void)paw::parse_config(source);
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("invalid configuration was accepted: " + source);
}
int main(int argc, char **argv) {
    try {
        require(argc == 2, "example config path required");
        auto config = paw::load_config(argv[1]);
        require(config.bindings.size() == 86, "example shortcuts missing");
        auto *louder = config.binding(0, XKB_KEY_XF86AudioRaiseVolume);
        auto *microphone = config.binding(0, XKB_KEY_XF86AudioMicMute);
        auto *dimmer = config.binding(0, XKB_KEY_XF86MonBrightnessDown);
        require(louder && louder->action == SH_VOLUME_UP && louder->amount == 5 && microphone &&
                    microphone->action == SH_MIC_MUTE && dimmer &&
                    dimmer->action == SH_BRIGHTNESS_DOWN,
                "example volume, microphone and brightness keys missing");
        // They work on the lock screen, and the steps repeat while held; the mutes do not.
        require(louder->locked && louder->repeats && microphone->locked && !microphone->repeats &&
                    dimmer->locked && dimmer->repeats &&
                    !config.binding(SH_LOGO, XKB_KEY_q)->locked,
                "example volume, microphone and brightness keys not locked or repeating");
        // The display key steps the display mode popup, as Windows' Win+P.
        auto *display = config.binding(0, XKB_KEY_XF86Display);
        require(display && display->action == SH_DISPLAY_MODE && display->mode == SH_DISPLAY_MODE_STEP,
                "example display key missing");
        require(config.binding(SH_ALT, XKB_KEY_Tab)->action == SH_SWITCHER_NEXT &&
                    config.binding(SH_ALT | SH_SHIFT, XKB_KEY_Tab)->action == SH_SWITCHER_PREV,
                "example window switcher bindings missing");
        // The keys of an action's binding, as the compositor tells them: modifiers in a fixed
        // order, a letter in capitals; nothing for an action without a key.
        require(config.binding_keys(SH_TERMINAL) == "Super + Q" &&
                    config.binding_keys(SH_SWITCHER_PREV) == "Alt + Shift + Tab" &&
                    config.binding_keys(SH_RESIZE_LEFT) == "Super + Ctrl + Shift + Left" &&
                    config.binding_keys(SH_POWER_MENU) == "Super + Escape" &&
                    config.binding_keys(SH_TOGGLE_SHORTCUTS_INHIBIT) == "Super + Shift + Escape" &&
                    config.binding_keys(SH_VOLUME_MUTE) == "XF86AudioMute" &&
                    config.binding_keys(SH_ZOOM_IN).empty(),
                "binding_keys mismatch");
        auto clicked = paw::parse_config(
            "return {bindings={{button='side',action='close'},{mods={'Ctrl'},key='w',"
            "action='close'},{switch='lid',state='close',action='lock'}}}");
        require(clicked.binding_keys(SH_CLOSE) == "Ctrl + W" && clicked.binding_keys(SH_LOCK).empty(),
                "binding_keys gave a button's or a switch's binding");
        // The example starts in the macOS style; its `default` profile is the taskbar.
        require(config.profile == "macos-light" && config.shell.macos_style &&
                    config.shell.enabled && config.shell.panel_height == 64 &&
                    config.shell.launchers.empty() && config.window_buttons == "close,minimize,maximize:",
                "example does not start in the macOS style");
        std::ifstream example(argv[1]);
        std::stringstream example_source;
        example_source << example.rdbuf();
        auto taskbar = paw::parse_config(example_source.str(), argv[1],
                                              std::filesystem::path(argv[1]).parent_path(),
                                              "default");
        require(taskbar.profile == "default" && !taskbar.shell.macos_style &&
                    taskbar.shell.panel_height == 52 &&
                    taskbar.window_buttons == "appmenu:minimize,maximize,close",
                "example's default profile is not the taskbar");
        auto pinnedCommand = paw::parse_config(
            "return {shell={launchers={{name='Home',icon='user-home',command={'xdg-open','.'}}}}}");
        require(pinnedCommand.shell.launchers.size() == 1 &&
                    pinnedCommand.shell.launchers.front().command == paw::Command{"xdg-open", "."},
                "pinned command arguments changed");
        auto *terminal = config.binding(SH_LOGO, XKB_KEY_q);
        require(terminal && terminal->action == SH_TERMINAL && terminal->command.empty(),
                "Super + Q does not open a terminal");
        require(config.binding(SH_LOGO | SH_SHIFT | 2, XKB_KEY_R)->action == SH_RELOAD,
                "shifted shortcut or CapsLock normalization failed");
        require(!config.binding(SH_LOGO | SH_CTRL, XKB_KEY_q), "extra modifiers matched");
        for (auto [keysym, action] : {std::pair{XKB_KEY_XF86AudioPlay, SH_MEDIA_PLAY_PAUSE},
                                      {XKB_KEY_XF86AudioPause, SH_MEDIA_PLAY_PAUSE},
                                      {XKB_KEY_XF86AudioNext, SH_MEDIA_NEXT},
                                      {XKB_KEY_XF86AudioPrev, SH_MEDIA_PREVIOUS},
                                      {XKB_KEY_XF86AudioStop, SH_MEDIA_STOP}}) {
            auto *media = config.binding(0, keysym);
            require(media && media->action == action, "a media key is not bound");
            require(media->locked && !media->repeats, "a media key does not work on the lock screen");
        }
        auto *taskbar_keys = config.binding(SH_LOGO, XKB_KEY_b);
        require(taskbar_keys && taskbar_keys->action == SH_TASKBAR_FOCUS,
                "Super + B does not give the keyboard to the taskbar");
        auto *fullscreen = config.binding(SH_LOGO, XKB_KEY_f);
        require(fullscreen && fullscreen->action == SH_FULLSCREEN, "fullscreen binding missing");
        auto *move = config.binding(SH_LOGO | SH_SHIFT, XKB_KEY_3);
        require(move && move->action == SH_MOVE_TO_WORKSPACE && move->workspace == 3,
                "move-to-workspace binding missing");
        require(config.settings.workspaces == 4, "example workspace count changed");
        require(!config.settings.tiling, "example starts tiled");
        auto *toggle = config.binding(SH_LOGO, XKB_KEY_s);
        require(toggle && toggle->action == SH_TOGGLE_TILING, "tiling toggle binding missing");
        require(paw::parse_config("return {layout={tiling=true}}").settings.tiling,
                "layout.tiling not parsed");
        auto *focus = config.binding(SH_LOGO, XKB_KEY_Left);
        require(focus && focus->action == SH_FOCUS_LEFT, "directional focus binding missing");
        auto *move_window = config.binding(SH_LOGO | SH_SHIFT, XKB_KEY_Right);
        require(move_window && move_window->action == SH_MOVE_RIGHT, "move-window binding missing");
        auto *snap = config.binding(SH_LOGO | SH_ALT, XKB_KEY_Up);
        require(snap && snap->action == SH_SNAP_CYCLE_UP, "snap cycle binding missing");
        auto *resize = config.binding(SH_LOGO | SH_CTRL | SH_SHIFT, XKB_KEY_Right);
        require(resize && resize->action == SH_RESIZE_RIGHT &&
                    resize->amount == paw::default_resize_amount,
                "resize binding missing");
        require(config.settings.keyboard_resize, "keyboard resizing off by default");
        auto resizing = paw::parse_config(
            "return {features={keyboard_resize=false},"
            "bindings={{mods={'Alt'},key='l',action='resize_left',amount=15}}}");
        require(!resizing.settings.keyboard_resize && resizing.bindings[0].amount == 15,
                "features or resize amount not parsed");
        rejects("return {features={no_such_feature=true}}");
        rejects("return {features={true}}");
        rejects("return {features=true}");
        {
            auto moves = paw::parse_config(
                "return {bindings={{mods={'Super'},key='comma',action='move_workspace_to_output',"
                "output='left'},{mods={'Super'},key='x',action='swap_workspaces'},"
                "{mods={'Super'},key='y',action='swap_workspaces',output='desc:Dell U27'}}}");
            require(moves.bindings.size() == 3 && moves.bindings[0].action == SH_MOVE_WORKSPACE_TO_OUTPUT &&
                        moves.bindings[0].output == "left" &&
                        moves.bindings[1].action == SH_SWAP_WORKSPACES &&
                        moves.bindings[1].output == "next" &&
                        moves.bindings[2].output == "desc:Dell U27",
                    "workspace-to-output bindings not parsed");
            rejects("return {bindings={{key='l',action='move_workspace_to_output'}}}");
            rejects("return {bindings={{key='l',action='move_workspace_to_output',output=''}}}");
            rejects("return {bindings={{key='l',action='move_workspace_to_output',output=3}}}");
            rejects("return {bindings={{key='l',action='swap_workspaces',output='desc:'}}}");
            rejects("return {bindings={{key='l',action='close',output='left'}}}");
        }
        rejects("return {bindings={{key='l',action='close',amount=10}}}");
        rejects("return {bindings={{key='l',action='resize_up',amount=0}}}");
        rejects("return {bindings={{key='l',action='resize_up',amount=1.5}}}");
        {
            // The volume and brightness steps take `amount` in percent, 5 unless given.
            auto steps = paw::parse_config(
                "return {bindings={{key='XF86AudioRaiseVolume',action='volume_up'},"
                "{key='XF86AudioLowerVolume',action='volume_down',amount=2},"
                "{key='XF86MonBrightnessUp',action='brightness_up',amount=100},"
                "{key='XF86AudioMute',action='volume_mute'},"
                "{key='XF86AudioMicMute',action='mic_mute'}}}");
            const auto &s = steps.bindings;
            require(s.size() == 5 && s[0].action == SH_VOLUME_UP &&
                        s[0].amount == paw::default_step_percent &&
                        s[1].action == SH_VOLUME_DOWN && s[1].amount == 2 &&
                        s[2].action == SH_BRIGHTNESS_UP && s[2].amount == 100 &&
                        s[3].action == SH_VOLUME_MUTE && s[4].action == SH_MIC_MUTE,
                    "volume and brightness bindings not parsed");
            require(paw::parse_action("brightness_down") == SH_BRIGHTNESS_DOWN &&
                        paw::default_amount(SH_RESIZE_UP) == paw::default_resize_amount &&
                        paw::max_amount(SH_VOLUME_UP) == 100,
                    "volume and brightness actions missing");
            rejects("return {bindings={{key='l',action='volume_up',amount=101}}}");
            rejects("return {bindings={{key='l',action='brightness_down',amount=0}}}");
            rejects("return {bindings={{key='l',action='volume_mute',amount=5}}}");
            rejects("return {bindings={{key='l',action='mic_mute',amount=5}}}");
        }
        {
            // `locked` runs a key binding while the session is locked, `repeats` while its key is
            // held; keyboard resizing repeats unless told not to.
            auto flags = paw::parse_config(
                "return {bindings={"
                "{key='XF86AudioRaiseVolume',action='volume_up',locked=true,repeats=true},"
                "{key='XF86AudioMute',action='volume_mute',locked=true},"
                "{mods={'Super'},key='Left',action='resize_left'},"
                "{mods={'Super'},key='Right',action='resize_right',repeats=false},"
                "{key='a',action='close'},{button='side',action='close'}}}");
            const auto &b = flags.bindings;
            require(b.size() == 6 && b[0].locked && b[0].repeats && b[1].locked && !b[1].repeats &&
                        !b[2].locked && b[2].repeats && !b[3].repeats && !b[4].locked &&
                        !b[4].repeats && !b[5].locked && !b[5].repeats,
                    "locked and repeats not parsed");
            rejects("return {bindings={{button='side',action='close',locked=true}}}");
            rejects("return {bindings={{button='side',action='close',repeats=false}}}");
            rejects("return {bindings={{key='a',action='close',locked='yes'}}}");
            rejects("return {bindings={{key='a',action='close',repeats=1}}}");
        }
        {
            // Binding modes: named lists of key bindings, sorted by name, which a binding with
            // the mode action puts in use; "default" is the bindings outside any.
            auto modes = paw::parse_config(
                "return {modes={resize={{key='Left',action='resize_left'},"
                "{key='Escape',action='mode',mode='default'},"
                "{key='Return',action='mode',mode='default'},{key='x',action='none'}},"
                "launch={{key='t',action='terminal'},{key='Escape',action='mode',mode='default'},"
                "{key='r',action='mode',mode='resize'}}},"
                "bindings={{mods={'Super'},key='r',action='mode',mode='resize'},"
                "{mods={'Super'},key='o',action='mode',mode='launch'},"
                "{button='side',action='mode',mode='resize'},"
                "{key='Print',action='screenshot',mode='window'}}}");
            require(modes.modes.size() == 2 && modes.modes[0].name == "launch" &&
                        modes.modes[1].name == "resize" && modes.mode_number("default") == 0 &&
                        modes.mode_number("launch") == 1 && modes.mode_number("resize") == 2 &&
                        modes.mode_number("other") == -1 && modes.mode_number("") == -1,
                    "modes not read in name order");
            require(modes.mode_names() ==
                        std::vector<std::string>{"default", "launch", "resize"},
                    "mode names not listed");
            auto *enter = modes.binding(SH_LOGO, XKB_KEY_r);
            require(enter && enter->action == SH_MODE && enter->mode == 2 &&
                        modes.bindings[2].action == SH_MODE && modes.bindings[2].mode == 2 &&
                        modes.bindings[3].screenshot == SH_SCREENSHOT_WINDOW,
                    "the mode action not read");
            // A mode's keys are its own: the others are not bound in it, and "none" drops one.
            auto *left = modes.mode_binding(2, 0, XKB_KEY_Left);
            require(left && left->action == SH_RESIZE_LEFT && left->repeats &&
                        modes.mode_binding(2, 0, XKB_KEY_Escape)->mode == 0 &&
                        !modes.mode_binding(2, SH_LOGO, XKB_KEY_r) &&
                        !modes.mode_binding(2, 0, XKB_KEY_x) &&
                        modes.modes[1].bindings.size() == 3 &&
                        modes.mode_binding(1, 0, XKB_KEY_r)->mode == 2 &&
                        !modes.binding(0, XKB_KEY_Left),
                    "a mode's bindings not kept apart");
            // Mode 0, or a number no mode has, is the bindings outside any.
            require(modes.mode_binding(0, SH_LOGO, XKB_KEY_r) == enter &&
                        modes.mode_binding(7, SH_LOGO, XKB_KEY_r) == enter,
                    "the bindings outside any mode not found");
            const std::string leave = "{key='Escape',action='mode',mode='default'}";
            rejects("return {bindings={{key='r',action='mode'}}}");
            rejects("return {bindings={{key='r',action='mode',mode='resize'}}}");
            rejects("return {bindings={{key='r',action='mode',mode=3}}}");
            rejects("return {bindings={{key='r',action='close',mode='default'}}}");
            rejects("return {modes={resize={{key='Left',action='resize_left'}}}}");
            rejects("return {modes={resize={}}}");
            rejects("return {modes={resize=true}}");
            rejects("return {modes={" + leave + "}}");
            rejects("return {modes={default={" + leave + "}}}");
            rejects("return {modes={['two words']={" + leave + "}}}");
            rejects("return {modes={resize={" + leave + ",{button='side',action='close'}}}}");
            rejects("return {modes={resize={" + leave + "," + leave + "}}}");
            rejects("return {modes={resize={" + leave + ",{key='q',action='mode',mode='other'}}}}");
            std::string many = "return {modes={";
            for (int i = 0; i < 17; ++i)
                many += "m" + std::to_string(i) + "={" + leave + "},";
            rejects(many + "}}");
        }
        auto *launcher = config.binding(SH_LOGO, XKB_KEY_r);
        require(launcher && launcher->action == SH_LAUNCHER, "launcher binding missing");
        require(paw::parse_action("toggle_floating") == SH_TOGGLE_FLOATING,
                "toggle_floating action missing");
        {
            auto defaults = paw::parse_config("return {}").settings;
            require(defaults.tile_layout == SH_LAYOUT_DWINDLE && defaults.master_count == 1 &&
                        defaults.master_ratio > 0.5F && defaults.master_ratio < 0.6F,
                    "layout defaults wrong");
            auto layouts = paw::parse_config(
                "return {layout={tile_layout='spiral',master_ratio=0.6,master_count=2}}");
            require(layouts.settings.tile_layout == SH_LAYOUT_SPIRAL &&
                        layouts.settings.master_ratio == 0.6F &&
                        layouts.settings.master_count == 2,
                    "layout settings not parsed");
            rejects("return {layout={tile_layout='grid'}}");
            rejects("return {layout={master_ratio=1}}");
            rejects("return {layout={master_count=0}}");
            require(defaults.scroll_follow == SH_SCROLL_FOLLOW_CENTER &&
                        defaults.scroll_width == 0.5F && defaults.scroll_preset_count == 4,
                    "scroll defaults wrong");
            auto scroll = paw::parse_config(
                "return {layout={tile_layout='scroll',scroll={follow='never',width=0.4,step=0.05,"
                "presets={0.25,0.5}}}}");
            require(scroll.settings.tile_layout == SH_LAYOUT_SCROLL &&
                        scroll.settings.scroll_follow == SH_SCROLL_FOLLOW_NEVER &&
                        scroll.settings.scroll_width == 0.4F &&
                        scroll.settings.scroll_step == 0.05F &&
                        scroll.settings.scroll_preset_count == 2 &&
                        scroll.settings.scroll_presets[1] == 0.5F,
                    "scroll settings not parsed");
            auto per_output = paw::parse_config(
                "return {layout={outputs={['DP-1']={tile_layout='scroll',master_ratio=0.6},"
                "['desc:Dell']={master_count=3}}}}");
            require(per_output.settings.output_layout_count == 2, "layout.outputs not parsed");
            for (int i = 0; i < 2; ++i) {
                const auto &entry = per_output.settings.output_layouts[i];
                if (std::string(entry.name) == "DP-1")
                    require(entry.tile_layout == SH_LAYOUT_SCROLL && entry.master_ratio == 0.6F &&
                                entry.master_count == 0,
                            "layout.outputs entry wrong");
                else
                    require(std::string(entry.name) == "desc:Dell" && entry.tile_layout < 0 &&
                                entry.master_ratio == 0 && entry.master_count == 3,
                            "layout.outputs description entry wrong");
            }
            require(defaults.output_layout_count == 0, "layout.outputs has a default");
            require(defaults.return_windows &&
                        !paw::parse_config("return {outputs={return_windows=false}}")
                             .settings.return_windows,
                    "outputs.return_windows not parsed");
            rejects("return {layout={outputs={['DP-1']={tile_layout='grid'}}}}");
            rejects("return {layout={outputs={['DP-1']={master_ratio=2}}}}");
            rejects("return {layout={outputs={['DP-1']={master_count=0}}}}");
            rejects("return {layout={outputs={['DP-1']={tilelayout='scroll'}}}}");
            rejects("return {layout={outputs={'DP-1'}}}");
            rejects("return {layout={outputs={['']={tile_layout='scroll'}}}}");
            rejects("return {layout={scroll={follow='sometimes'}}}");
            rejects("return {layout={scroll={width=0}}}");
            rejects("return {layout={scroll={step=1}}}");
            rejects("return {layout={scroll={presets={}}}}");
            rejects("return {layout={scroll={presets={2}}}}");
            rejects("return {layout={scroll={presets={'half'}}}}");
            rejects("return {layout={scroll={presets={0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5,0.5}}}}");
            rejects("return {layout={scroll={colour='red'}}}");
            for (auto name : {"layout_next", "layout_prev", "layout_dwindle", "layout_master",
                              "layout_scroll", "scroll_left", "scroll_right", "column_widen",
                              "column_narrow", "column_cycle_width", "consume_left",
                              "consume_right", "expel", "center_column",
                              "layout_spiral", "layout_monocle", "promote", "focus_next",
                              "focus_prev", "swap_next", "swap_prev", "master_grow",
                              "master_shrink", "master_more", "master_less"})
                require(paw::parse_action(name) != SH_NONE, "layout action missing");
        }
        auto *back = config.binding(SH_LOGO, XKB_KEY_Tab);
        require(back && back->action == SH_WORKSPACE_BACK && back->workspace == 0,
                "workspace_back binding missing");
        require(!config.settings.workspace_back_and_forth,
                "example turns on workspace back-and-forth");
        require(!paw::parse_config("return {}").settings.workspace_back_and_forth &&
                    !paw::parse_config("return {features={}}").settings.workspace_back_and_forth,
                "workspace back-and-forth is on by default");
        require(paw::parse_config("return {features={workspace_back_and_forth=true}}")
                    .settings.workspace_back_and_forth,
                "features.workspace_back_and_forth not parsed");
        require(config.settings.sticky && paw::parse_config("return {}").settings.sticky &&
                    paw::parse_config("return {features={}}").settings.sticky,
                "sticky windows are off by default");
        require(!paw::parse_config("return {features={sticky=false}}").settings.sticky,
                "features.sticky not parsed");
        auto *sticky = config.binding(SH_LOGO | SH_SHIFT, XKB_KEY_p);
        require(sticky && sticky->action == SH_TOGGLE_STICKY, "sticky binding missing");
        auto *above = config.binding(SH_LOGO | SH_CTRL, XKB_KEY_t);
        require(above && above->action == SH_TOGGLE_ABOVE, "keep-above binding missing");
        auto *palette = config.binding(SH_LOGO, XKB_KEY_p);
        require(palette && palette->action == SH_PALETTE, "palette binding missing");
        rejects("return {bindings={{mods={'Super'},key='Tab',action='workspace_back',workspace=2}}}");
        auto *region = config.binding(0, XKB_KEY_Print);
        auto *whole = config.binding(SH_SHIFT, XKB_KEY_Print);
        auto *window = config.binding(SH_LOGO, XKB_KEY_Print);
        require(region && region->action == SH_SCREENSHOT &&
                    region->screenshot == SH_SCREENSHOT_REGION && whole &&
                    whole->screenshot == SH_SCREENSHOT_OUTPUT && window &&
                    window->screenshot == SH_SCREENSHOT_WINDOW,
                "screenshot bindings missing");
        require(config.screenshots.directory.empty() && config.screenshots.clipboard &&
                    config.screenshots.notify,
                "example screenshot settings changed");
        auto shots = paw::parse_config(
            "return {screenshots={directory='~/Shots',clipboard=false,notify=false},"
            "bindings={{mods={'Alt'},key='Print',action='screenshot'}}}");
        require(shots.screenshots.directory == "~/Shots" && !shots.screenshots.clipboard &&
                    !shots.screenshots.notify &&
                    shots.bindings[0].screenshot == SH_SCREENSHOT_REGION,
                "screenshot settings not parsed");
        require(paw::parse_screenshot_mode("window") == SH_SCREENSHOT_WINDOW,
                "screenshot mode names differ");
        require(paw::parse_config("return {screenshots={directory='/tmp/x'}}")
                        .screenshots.directory == "/tmp/x",
                "absolute screenshot directory not parsed");
        rejects("return {screenshots={directory='Shots'}}");
        require(paw::parse_config("return {}").window_buttons ==
                    "appmenu:minimize,maximize,close",
                "default window buttons changed");
        require(paw::parse_config("return {windows={buttons='close'}}").window_buttons ==
                        "close" &&
                    paw::parse_config("return {windows={buttons=''}}").window_buttons.empty(),
                "windows.buttons not parsed");
        rejects("return {windows={buttons=\"close'\"}}");
        rejects("return {screenshots={directory=1}}");
        rejects("return {screenshots={format='jpeg'}}");
        rejects("return {bindings={{mods={},key='Print',action='screenshot',mode='screen'}}}");
        rejects("return {bindings={{mods={},key='Print',action='close',mode='window'}}}");
        auto outputs =
            paw::parse_config("return {outputs={order={'HDMI-A-1','DP-3'},primary='DP-3'}}");
        require(outputs.settings.output_count == 2 &&
                    std::string(outputs.settings.output_order[1]) == "DP-3" &&
                    std::string(outputs.settings.primary_output) == "DP-3",
                "output order or primary not parsed");
        rejects("return {outputs={order={'DP-1','DP-1'}}}");
        rejects("return {outputs={order={''}}}");
        rejects("return {outputs={order={'1','2','3','4','5','6','7','8','9'}}}");
        rejects("return {outputs={primary=1}}");
        rejects("return {outputs={position={}}}");
        auto monitors = paw::parse_config(
            "return {outputs={monitors={['DP-3']={mode='2560x1440@143.98',scale=1.25,"
            "position={x=-2048,y=0},transform=1},['HDMI-A-1']={enabled=false}}}}");
        require(monitors.settings.monitor_count == 2, "monitors not parsed");
        for (int i = 0; i < 2; ++i) {
            const auto &m = monitors.settings.monitors[i];
            if (std::string(m.name) == "DP-3")
                require(m.enabled && m.width == 2560 && m.height == 1440 && m.refresh == 143980 &&
                            m.scale == 1.25F && m.positioned && m.x == -2048 && m.y == 0 &&
                            m.transform == 1,
                        "monitor settings mismatch");
            else
                require(std::string(m.name) == "HDMI-A-1" && !m.enabled && m.width == 0 &&
                            !m.positioned,
                        "disabled monitor mismatch");
        }
        auto plain = paw::parse_config("return {outputs={monitors={X={mode='800x600'}}}}");
        require(plain.settings.monitors[0].refresh == 0 && plain.settings.monitors[0].enabled,
                "mode without refresh mismatch");
        rejects("return {outputs={monitors={'DP-1'}}}");
        rejects("return {outputs={monitors={['']={}}}}");
        rejects("return {outputs={monitors={X={mode='2560x1440@'}}}}");
        rejects("return {outputs={monitors={X={mode='2560x1440x'}}}}");
        rejects("return {outputs={monitors={X={mode='0x1440'}}}}");
        rejects("return {outputs={monitors={X={scale=0}}}}");
        rejects("return {outputs={monitors={X={transform=8}}}}");
        rejects("return {outputs={monitors={X={position={x=1}}}}}");
        rejects("return {outputs={monitors={X={position={x=1,y=2,z=3}}}}}");
        rejects("return {outputs={monitors={X={refresh=60}}}}");
        auto described = paw::parse_config(
            "return {outputs={monitors={['desc:ASUSTek COMPUTER INC VG27AQ3A']={vrr=true}}}}");
        require(std::string(described.settings.monitors[0].name) ==
                        "desc:ASUSTek COMPUTER INC VG27AQ3A" &&
                    described.settings.monitors[0].vrr,
                "description key or vrr not parsed");
        rejects("return {outputs={monitors={X={vrr='on'}}}}");
        auto tiled =
            paw::parse_config("return {layout={tiling=true},"
                                 "outputs={monitors={A={tiling=false},B={tiling=true},C={}}}}");
        require(tiled.settings.tiling && tiled.settings.monitor_count == 3,
                "per-monitor tiling not parsed");
        for (int i = 0; i < 3; ++i) {
            const auto &m = tiled.settings.monitors[i];
            int expected = std::string(m.name) == "A" ? 0 : std::string(m.name) == "B" ? 1 : -1;
            require(m.tiling == expected, "per-monitor tiling mixed up");
        }
        require(plain.settings.monitors[0].tiling == -1, "monitor tiling should follow layout");
        rejects("return {outputs={monitors={X={tiling='yes'}}}}");
        require(paw::parse_action("workspace_next") == SH_WORKSPACE_NEXT,
                "control action names differ from Lua");
        rejects("return {bindings={{mods={'Alt'},key='1',action='workspace'}}}");
        rejects("return {bindings={{mods={'Alt'},key='1',action='workspace',workspace=5}}}");
        rejects("return {layout={workspaces=2},bindings={{mods={'Alt'},key='1',"
                "action='move_to_workspace',workspace=3}}}");
        rejects("return {bindings={{mods={'Alt'},key='1',action='close',workspace=1}}}");
        auto computed = paw::parse_config("local gap = 3; return {layout={gap=gap*2}}");
        require(computed.settings.gap_inner == 6 && computed.settings.gap_outer == 6,
                "Lua evaluation failed");
        auto gaps = paw::parse_config("return {layout={gap=4,gap_outer=10}}");
        require(gaps.settings.gap_inner == 4 && gaps.settings.gap_outer == 10,
                "gap_inner/gap_outer not parsed");
        require(gaps.settings.smart_gaps &&
                    !paw::parse_config("return {layout={smart_gaps=false}}").settings.smart_gaps,
                "smart_gaps not parsed");
        require(!gaps.settings.floating_above_tiles &&
                    paw::parse_config("return {layout={floating_above_tiles=true}}")
                        .settings.floating_above_tiles,
                "floating_above_tiles not parsed, or not off by default");
        rejects("return {layout={floating_above_tiles='yes'}}");
        auto windows = paw::parse_config(
            "return {windows={border_width=2,border_color='#ff000080',"
            "border_inactive_color='#00ff00',opacity=0.95,inactive_opacity=0.8,"
            "rules={{app_id='^firefox$',opacity=0.9},{app_id='code',opacity=0.7,"
            "inactive_opacity=0.6}}}}");
        const auto &ws = windows.settings;
        require(ws.border_width == 2 && ws.border_active[0] > 0.50F &&
                    ws.border_active[0] < 0.51F && ws.border_active[3] > 0.50F &&
                    ws.border_active[3] < 0.51F && ws.border_inactive[1] == 1.0F &&
                    ws.border_inactive[3] == 1.0F,
                "border settings not parsed or not premultiplied");
        require(ws.border_active_gradient.count == 1 && ws.border_inactive_gradient.count == 1 &&
                    ws.border_inactive_gradient.stops[0][1] == 1.0F,
                "a border of one colour is not a gradient of one stop");
        // Gradients: 2 to 10 colours, premultiplied, at an angle; the first is the colour too.
        auto gradient = paw::parse_config(
            "return {windows={border_color={'#33ccffee','#00ff99ee',angle=45},"
            "border_inactive_color={'#000000','#ffffff','#ff0000'}}}");
        const auto &gs = gradient.settings;
        require(gs.border_active_gradient.count == 2 && gs.border_active_gradient.angle == 45 &&
                    std::abs(gs.border_active_gradient.stops[1][3] - 0xee / 255.0F) < 1e-6F &&
                    std::abs(gs.border_active_gradient.stops[1][1] - 0xee / 255.0F) < 1e-6F &&
                    gs.border_active[2] == gs.border_active_gradient.stops[0][2] &&
                    gs.border_inactive_gradient.count == 3 &&
                    gs.border_inactive_gradient.angle == 0 &&
                    gs.border_inactive_gradient.stops[2][0] == 1.0F,
                "gradient borders not parsed");
        require(paw::parse_config("return {windows={border_color='#123456'}}")
                        .settings.border_active_gradient.count == 1,
                "a colour after a gradient's default is not one colour");
        rejects("return {windows={border_color={'#33ccff'}}}");
        rejects("return {windows={border_color={'#000000','#000000','#000000','#000000',"
                "'#000000','#000000','#000000','#000000','#000000','#000000','#000000'}}}");
        rejects("return {windows={border_color={'#33ccff','blue'}}}");
        rejects("return {windows={border_color={'#33ccff','#00ff99',angle='45deg'}}}");
        rejects("return {windows={border_color={'#33ccff','#00ff99',angle=400}}}");
        rejects("return {windows={border_color={'#33ccff','#00ff99',spin=4}}}");
        rejects("return {windows={border_color={'#33ccff',[3]='#00ff99'}}}");
        require(windows.window_opacity("firefox", true) == 0.9F &&
                    windows.window_opacity("firefox", false) == 0.9F &&
                    windows.window_opacity("code-oss", false) == 0.6F &&
                    windows.window_opacity("firefox-esr", true) == 0.95F &&
                    windows.window_opacity("", false) == 0.8F,
                "window opacity rules mismatch");
        auto opaque = paw::parse_config("return {}");
        require(opaque.window_opacity("x", false) == 1 && opaque.settings.border_width == 0,
                "window defaults changed");
        require(opaque.settings.corner_radius == 10, "tiled windows should default to rounded");
        require(paw::parse_config("return {windows={corner_radius=0}}").settings.corner_radius ==
                    0,
                "windows.corner_radius not parsed");
        require(opaque.settings.scratchpad && config.settings.scratchpad,
                "the scratchpad should default to on");
        require(!paw::parse_config("return {features={scratchpad=false}}").settings.scratchpad,
                "features.scratchpad not parsed");
        rejects("return {features={scratchpad=1}}");
        auto *hide = config.binding(SH_LOGO | SH_SHIFT, XKB_KEY_minus);
        require(hide && hide->action == SH_MOVE_TO_SCRATCHPAD, "scratchpad binding missing");
        auto *show = config.binding(SH_LOGO, XKB_KEY_minus);
        require(show && show->action == SH_SCRATCHPAD_SHOW, "scratchpad_show binding missing");
        auto input = paw::parse_config(
            "return {mouse={speed=-0.5,acceleration='flat',natural_scroll=false,"
            "focus_follows=false},"
            "touchpad={natural_scroll=true,tap_to_click=true,disable_while_typing=false}}");
        const auto &is = input.settings;
        require(is.pointer_speed_set && is.pointer_speed == -0.5 && is.pointer_accel == 0 &&
                    is.mouse_natural_scroll == 0 && is.touchpad_natural_scroll == 1 &&
                    is.touchpad_tap == 1 && is.touchpad_dwt == 0 && is.mouse_modifier == SH_ALT &&
                    !is.focus_follows_mouse,
                "pointer settings not parsed");
        const auto &defaults = opaque.settings;
        require(!defaults.pointer_speed_set && defaults.pointer_accel == -1 &&
                    defaults.mouse_natural_scroll == -1 && defaults.touchpad_tap == -1 &&
                    defaults.focus_follows_mouse,
                "pointer defaults must leave devices alone");
        // Every kind of animation lasts animations.duration unless it has its own.
        auto lasting = [](const sh_settings &settings, int duration) {
            for (const auto &style : settings.animation_styles)
                if (style.duration != duration)
                    return false;
            return true;
        };
        require(defaults.animations && lasting(defaults, 120),
                "animations should be on for 120 ms by default");
        require(config.settings.animations, "example turns animations off");
        auto still = paw::parse_config("return {animations={enabled=false,duration=200}}");
        require(!still.settings.animations && lasting(still.settings, 200),
                "animations not parsed");
        require(paw::parse_config("return {animations={duration=80}}").settings.animations,
                "a duration alone should keep animations on");
        rejects("return {animations={duration=0}}");
        // The overview.
        const auto &overview_defaults = paw::parse_config("return {}").settings;
        require(overview_defaults.overview && overview_defaults.overview_gap == 24 &&
                    overview_defaults.overview_animation &&
                    overview_defaults.overview_duration == 180 && overview_defaults.overview_strip &&
                    overview_defaults.overview_hot_corner == 0 &&
                    std::abs(overview_defaults.overview_dim - 0.86F) < 0.001F,
                "overview defaults");
        auto custom_overview = paw::parse_config(
            "return {overview={enabled=false,gap=0,animation=false,duration=40,strip=false,"
            "hot_corner='bottom-right',dim=0.5}}");
        require(!custom_overview.settings.overview && custom_overview.settings.overview_gap == 0 &&
                    !custom_overview.settings.overview_animation &&
                    custom_overview.settings.overview_duration == 40 &&
                    !custom_overview.settings.overview_strip &&
                    custom_overview.settings.overview_hot_corner == 4 &&
                    std::abs(custom_overview.settings.overview_dim - 0.5F) < 0.001F,
                "overview settings not parsed");
        require(paw::parse_config("return {overview={hot_corner='top-left'}}")
                        .settings.overview_hot_corner == 1 &&
                    paw::parse_config("return {overview={hot_corner='top-right'}}")
                            .settings.overview_hot_corner == 2 &&
                    paw::parse_config("return {overview={hot_corner='bottom-left'}}")
                            .settings.overview_hot_corner == 3 &&
                    paw::parse_config("return {overview={gap=40}}").settings.overview,
                "overview corners");
        rejects("return {overview={hot_corner='middle'}}");
        rejects("return {overview={hot_corner=1}}");
        rejects("return {overview={duration=5}}");
        rejects("return {overview={dim=1.5}}");
        rejects("return {overview={colour='red'}}");
        for (auto [name, action] : {std::pair{"toggle_overview", SH_OVERVIEW_TOGGLE},
                                    {"overview_confirm", SH_OVERVIEW_CONFIRM},
                                    {"overview_cancel", SH_OVERVIEW_CANCEL}})
            require(paw::parse_action(name) == action, "overview action name");
        rejects("return {animations={duration=1.5}}");
        rejects("return {animations={duration=5000}}");
        rejects("return {animations={curve='fast'}}");
        rejects("return {animations={curve='bezier(2,0,0,1)'}}");
        rejects("return {animations={curve=3}}");
        rejects("return {animations={speed=0}}");
        rejects("return {animations={speed=50}}");
        rejects("return {animations={late_frame_ms=-1}}");
        rejects("return {animations={open=true}}");
        rejects("return {animations={open={duration=2000}}}");
        rejects("return {animations={open={duration=1.5}}}");
        rejects("return {animations={open={curve='sluggish'}}}");
        rejects("return {animations={open={speed=2}}}");
        rejects("return {animations={fade={duration=100}}}");
        {
            const auto &s = defaults;
            require(s.animation_speed == 1 && s.animation_late_ms == 80, "animation defaults");
            require(s.animation_styles[SH_ANIM_MOVE].curve.kind == SH_CURVE_SPRING &&
                        s.animation_styles[SH_ANIM_OPEN].curve.kind == SH_CURVE_EASE_OUT,
                    "default curves are chosen per kind");
            auto tuned = paw::parse_config(
                "return {animations={duration=200, speed=2, late_frame_ms=0, curve='linear',"
                " move={duration=300, curve='bezier(0.2, 0.9, 0.1, 1)'}, open={curve='overshoot'},"
                " close={duration=0}, focus={duration=50}}}").settings;
            const auto &st = tuned.animation_styles;
            require(tuned.animation_speed == 2 && tuned.animation_late_ms == 0, "speed, late");
            require(st[SH_ANIM_WORKSPACE].duration == 200 &&
                        st[SH_ANIM_WORKSPACE].curve.kind == SH_CURVE_LINEAR,
                    "the base duration and curve reach every kind");
            require(st[SH_ANIM_MOVE].duration == 300 && st[SH_ANIM_MOVE].curve.kind == SH_CURVE_BEZIER &&
                        st[SH_ANIM_MOVE].curve.p[1] == 0.9F,
                    "a kind's own duration and curve win");
            require(st[SH_ANIM_OPEN].duration == 200 && st[SH_ANIM_OPEN].curve.kind == SH_CURVE_OVERSHOOT,
                    "a kind may override only the curve");
            require(st[SH_ANIM_CLOSE].duration == 0, "duration 0 turns one kind off");
            require(st[SH_ANIM_FOCUS].duration == 50, "focus duration");
            auto unset = paw::parse_config("return {animations={duration=90}}").settings;
            require(unset.animation_styles[SH_ANIM_MOVE].curve.kind == SH_CURVE_SPRING &&
                        unset.animation_styles[SH_ANIM_MOVE].duration == 90,
                    "kinds keep their default curve when only the duration is set");
        }
        rejects("return {animations=true}");
        rejects("return {mouse={speed=2}}");
        rejects("return {mouse={acceleration='fast'}}");
        rejects("return {touchpad={tap_to_click=1}}");
        rejects("return {touchpad={scroll_factor=2}}");
        rejects("return {windows={border_color='red'}}");
        rejects("return {windows={opacity=0}}");
        rejects("return {windows={opacity=1.5}}");
        rejects("return {windows={rules={{app_id='('}}}}");
        rejects("return {windows={rules={{opacity=0.5}}}}");
        rejects("return {windows={rules={{app_id='x',class='y'}}}}");

        // Rule actions: every matching rule applies in order, later ones winning.
        auto ruled = paw::parse_config(
            "return {layout={workspaces=5},windows={opacity=0.95,rules={"
            "{app_id='^pavucontrol$',floating=true,size={640,480},position='center'},"
            "{app_id='pavu',title='Volume',focus=false,position={x=10,y=20}},"
            "{title='^Mail',workspace=2,output='desc:Dell U2720Q',floating=false},"
            "{app_id='^mpv$',fullscreen=true,maximize=true,opacity=0.5},"
            "{app_id='^mpv$',output='DP-1',size={width=100,height=200},fullscreen=false},"
            "{app_id='^mpv$',sticky=true},{app_id='^mpv$',above=true}}}}");
        require(ruled.settings.window_rules, "window rules should be on by default");
        auto pavu = ruled.window_actions("pavucontrol", "Volume Control");
        require(pavu.floating == true && pavu.size == std::pair{640, 480} &&
                    pavu.position == paw::WindowActions::Position::At && pavu.x == 10 &&
                    pavu.y == 20 && pavu.focus == false && !pavu.workspace && !pavu.output,
                "window rule actions did not merge in order");
        auto pavu_c = ruled.window_actions("pavucontrol", "Other").to_c();
        require(pavu_c.floating == 1 && pavu_c.width == 640 && pavu_c.height == 480 &&
                    pavu_c.position == SH_RULE_POSITION_CENTER && !pavu_c.no_focus &&
                    pavu_c.workspace == 0 && !pavu_c.output[0] && !pavu_c.fullscreen,
                "title regex matched the wrong window or C rule mismatch");
        auto mail = ruled.window_actions("thunderbird", "Mail - Inbox").to_c();
        require(mail.floating == 0 && mail.workspace == 2 &&
                    std::string(mail.output) == "desc:Dell U2720Q" &&
                    mail.position == SH_RULE_POSITION_UNSET,
                "title-only rule mismatch");
        auto mpv = ruled.window_actions("mpv", "").to_c();
        require(!mpv.fullscreen && mpv.maximize && std::string(mpv.output) == "DP-1" &&
                    mpv.width == 100 && mpv.height == 200 && mpv.floating == -1 && mpv.sticky &&
                    mpv.above,
                "later rule did not override an earlier one");
        require(!pavu_c.above, "a rule kept the wrong window above");
        require(ruled.window_actions("kitty", "My Mail").empty(), "a rule matched the wrong window");
        require(!mpv.no_shortcuts_inhibit && !pavu_c.no_shortcuts_inhibit,
                "a rule without shortcuts_inhibit refused inhibitors");
        // shortcuts_inhibit = false alone is a rule's action, which a later rule may undo.
        auto inhibit = paw::parse_config(
            "return {windows={rules={{app_id='^remmina$',shortcuts_inhibit=false},"
            "{app_id='^remmina$',title='^Trusted',shortcuts_inhibit=true}}}}");
        require(!inhibit.window_actions("remmina", "Host").empty() &&
                    inhibit.window_actions("remmina", "Host").to_c().no_shortcuts_inhibit &&
                    !inhibit.window_actions("remmina", "Trusted host").to_c().no_shortcuts_inhibit &&
                    inhibit.window_actions("kitty", "").empty(),
                "shortcuts_inhibit rules mismatch");
        // Opacity: the first rule that sets it wins; action-only rules leave it alone.
        require(ruled.window_opacity("pavucontrol", "Volume", true) == 0.95F &&
                    ruled.window_opacity("mpv", "", true) == 0.5F,
                "action-only rules changed opacity");
        auto titled = paw::parse_config(
            "return {windows={rules={{title='^Picture',opacity=0.8},{app_id='x'}}}}");
        require(titled.window_opacity("firefox", "Picture-in-Picture", true) == 0.8F &&
                    titled.window_opacity("firefox", "Other", true) == 1 &&
                    titled.window_opacity("x", "", false) == 1,
                "opacity rules by title mismatch");
        // features.window_rules = false drops the actions but keeps opacity rules.
        auto off = paw::parse_config(
            "return {features={window_rules=false},windows={rules={"
            "{app_id='^mpv$',floating=true,opacity=0.5}}}}");
        require(!off.settings.window_rules && off.window_actions("mpv", "").empty() &&
                    off.window_opacity("mpv", "", true) == 0.5F,
                "features.window_rules = false not honoured");
        rejects("return {features=false}");
        rejects("return {windows={rules={{floating=true}}}}");
        rejects("return {windows={rules={{title='('}}}}");
        rejects("return {windows={rules={{app_id='x',floating='yes'}}}}");
        rejects("return {windows={rules={{app_id='x',focus=0}}}}");
        rejects("return {windows={rules={{app_id='x',sticky='yes'}}}}");
        rejects("return {windows={rules={{app_id='x',shortcuts_inhibit=0}}}}");
        rejects("return {windows={rules={{app_id='x',above=1}}}}");
        rejects("return {windows={rules={{app_id='x',dynamic='yes'}}}}");
        // Dynamic rules hold above, floating and sticky while they match, later ones winning;
        // their other actions act once, as the window opens, and static rules take no part.
        auto dynamic = paw::parse_config(
            "return {windows={rules={"
            "{title='Meeting',dynamic=true,above=true,floating=true,workspace=2,opacity=0.9},"
            "{app_id='^zoom$',dynamic=true,floating=false},"
            "{title='^Share',dynamic=true,sticky=true},"
            "{title='Video',above=true},"
            "{app_id='^zoom$',dynamic=true}}}}");
        auto meeting = dynamic.dynamic_actions("zoom", "Meeting with Robin");
        require(meeting.above == true && meeting.floating == false && !meeting.sticky &&
                    !meeting.workspace && !meeting.size,
                "dynamic rules did not merge what they hold in order");
        require(dynamic.dynamic_actions("firefox", "Meeting").floating == true &&
                    dynamic.dynamic_actions("firefox", "Share screen").sticky == true &&
                    dynamic.dynamic_actions("firefox", "Share screen").above == std::nullopt,
                "a dynamic rule matched the wrong window");
        require(dynamic.dynamic_actions("firefox", "Video").empty() &&
                    dynamic.dynamic_actions("firefox", "Inbox").empty(),
                "a static rule took part in the dynamic ones");
        auto opening = dynamic.window_actions("firefox", "Meeting");
        require(opening.workspace == 2 && !opening.above && !opening.floating && !opening.sticky,
                "a dynamic rule's state acted once as the window opened, or its workspace did not");
        require(dynamic.window_actions("firefox", "Video").above == true,
                "a static rule's state did not act as the window opened");
        require(dynamic.window_opacity("firefox", "Meeting", true) == 0.9F,
                "a dynamic rule's opacity does not count");
        auto dynamic_off = paw::parse_config(
            "return {features={window_rules=false},windows={rules={"
            "{title='Meeting',dynamic=true,above=true}}}}");
        require(dynamic_off.dynamic_actions("x", "Meeting").empty(),
                "features.window_rules = false left the dynamic rules on");
        // A rule that only keeps a window above takes no part in the opacity.
        auto kept = paw::parse_config(
            "return {windows={rules={{app_id='^mpv$',above=true},{app_id='mpv',opacity=0.5}}}}");
        require(kept.window_actions("mpv", "").to_c().above &&
                    kept.window_opacity("mpv", "", true) == 0.5F,
                "an above rule took part in the opacity");
        rejects("return {windows={rules={{app_id='x',fullscreen='true'}}}}");
        rejects("return {windows={rules={{app_id='x',workspace=5}}}}");
        rejects("return {windows={rules={{app_id='x',workspace=0}}}}");
        rejects("return {windows={rules={{app_id='x',workspace=1.5}}}}");
        rejects("return {windows={rules={{app_id='x',output=''}}}}");
        rejects("return {windows={rules={{app_id='x',output='desc:'}}}}");
        rejects("return {windows={rules={{app_id='x',output=3}}}}");
        rejects("return {windows={rules={{app_id='x',size={640}}}}}");
        rejects("return {windows={rules={{app_id='x',size={640,480,1}}}}}");
        rejects("return {windows={rules={{app_id='x',size={0,480}}}}}");
        rejects("return {windows={rules={{app_id='x',size={w=640,h=480}}}}}");
        rejects("return {windows={rules={{app_id='x',size='640x480'}}}}");
        rejects("return {windows={rules={{app_id='x',position='middle'}}}}");
        rejects("return {windows={rules={{app_id='x',position={x=1}}}}}");
        rejects("return {windows={rules={{app_id='x',position={1.5,2}}}}}");
        rejects("return {windows={rules={{app_id='x',position=true}}}}");
        rejects("return {keyboard={repeat_rate='25'}}");
        rejects("return {keyboard={layout='us',variant='nosuchvariant'}}");
        rejects("return {appearance={background='#oops00'}}");
        rejects("return {layuot={gap=2}}");
        rejects("return {version=2}");
        rejects("return {shell={panel_height=0}}");
        auto bar = paw::parse_config(
            "return {shell={panel_position='top',panel_margin={top=6,left=10,right=10},"
            "panel_radius=12,font='JetBrainsMono Nerd Font',font_size=13,icons_only=false,"
            "group_windows=false,workspaces_shown=3,panel_color='#151e2ccc'}}");
        require(bar.shell.panel_top && bar.shell.panel_margin[0] == 6 &&
                    bar.shell.panel_margin[1] == 10 && bar.shell.panel_margin[2] == 0 &&
                    bar.shell.panel_margin[3] == 10 && bar.shell.panel_radius == 12 &&
                    bar.shell.font == "JetBrainsMono Nerd Font" && bar.shell.font_size == 13 &&
                    bar.shell.panel_color == "#151e2ccc" && !bar.shell.icons_only &&
                    !bar.shell.group_windows && bar.shell.workspaces_shown == 3,
                "bar settings not parsed");
        auto even = paw::parse_config("return {shell={panel_margin=8}}");
        require(even.shell.panel_margin[0] == 8 && even.shell.panel_margin[3] == 8 &&
                    !even.shell.panel_top && even.shell.icons_only && even.shell.group_windows &&
                    even.shell.workspaces_shown == 0,
                "single panel margin not parsed");
        require(!even.shell.software_renderer, "the shell does not draw through the GPU by default");
        require(!paw::parse_config("return {shell={renderer='gpu'}}").shell.software_renderer &&
                    paw::parse_config("return {shell={renderer='software'}}")
                        .shell.software_renderer,
                "renderer not parsed");
        require(!even.shell.macos_style, "the shell is a taskbar by default");
        require(paw::parse_config("return {shell={style='macos'}}").shell.macos_style &&
                    !paw::parse_config("return {shell={style='taskbar'}}").shell.macos_style,
                "style not parsed");
        rejects("return {shell={style='gnome'}}");
        rejects("return {shell={renderer='vulkan'}}");
        rejects("return {shell={workspaces_shown=11}}");
        rejects("return {shell={renderer=true}}");
        rejects("return {shell={panel_position='left'}}");
        rejects("return {shell={panel_margin=-1}}");
        rejects("return {shell={panel_margin={middle=1}}}");
        rejects("return {shell={font_size=2}}");
        rejects("return {shell={group_windows=1}}");
        // The taskbar's window pictures: on by default, each setting read, and their ranges.
        require(even.shell.thumbnails.enabled && even.shell.thumbnails.delay == 400 &&
                    even.shell.thumbnails.size == 240 && even.shell.thumbnails.live,
                "the taskbar's window pictures do not default to on, 400 ms, 240 pixels and live");
        auto pictures = paw::parse_config(
            "return {shell={thumbnails={enabled=false,delay=0,size=480,live=false}}}");
        require(!pictures.shell.thumbnails.enabled && pictures.shell.thumbnails.delay == 0 &&
                    pictures.shell.thumbnails.size == 480 && !pictures.shell.thumbnails.live,
                "shell.thumbnails not parsed");
        require(paw::parse_config("return {shell={thumbnails={size=120}}}").shell.thumbnails.enabled,
                "a partial shell.thumbnails table does not keep the other defaults");
        rejects("return {shell={thumbnails=true}}");
        rejects("return {shell={thumbnails={delay=2001}}}");
        rejects("return {shell={thumbnails={size=119}}}");
        rejects("return {shell={thumbnails={live='yes'}}}");
        rejects("return {shell={thumbnails={width=240}}}");
        // What the search finds besides applications: files, by default in the home and user
        // folders, four levels down and 20000 names at most.
        require(even.shell.search.files && even.shell.search.directories.empty() &&
                    even.shell.search.depth == 4 && even.shell.search.max_files == 20000,
                "the search's files do not default to on, the user folders, 4 levels and 20000");
        auto search = paw::parse_config(
            "return {shell={search={files=false,directories={'~/Documents','/srv/share','~'},"
            "depth=2,max_files=500}}}");
        require(!search.shell.search.files &&
                    search.shell.search.directories ==
                        std::vector<std::string>{"~/Documents", "/srv/share", "~"} &&
                    search.shell.search.depth == 2 && search.shell.search.max_files == 500,
                "shell.search not parsed");
        rejects("return {shell={search=true}}");
        rejects("return {shell={search={files='yes'}}}");
        rejects("return {shell={search={directories={'Documents'}}}}");
        rejects("return {shell={search={directories='~/Documents'}}}");
        rejects("return {shell={search={depth=0}}}");
        rejects("return {shell={search={max_files=200001}}}");
        rejects("return {shell={search={folders={'~'}}}}");
        // The web search's engine: DuckDuckGo by default, another address, or none.
        require(even.shell.search.web == "https://duckduckgo.com/?q=%s",
                "the web search does not default to DuckDuckGo");
        require(paw::parse_config("return {shell={search={web='https://www.google.com/search?q=%s'}}}")
                        .shell.search.web == "https://www.google.com/search?q=%s" &&
                    paw::parse_config("return {shell={search={web=false}}}").shell.search.web.empty(),
                "shell.search.web not parsed");
        rejects("return {shell={search={web=true}}}");
        rejects("return {shell={search={web='https://example.org/search'}}}");
        rejects("return {shell={search={web='file:///etc/%s'}}}");
        rejects("return {shell={search={web=1}}}");
        // The clipboard history: on, 50 entries, pictures too, in memory only by default.
        require(even.shell.clipboard.enabled && even.shell.clipboard.max_entries == 50 &&
                    even.shell.clipboard.images && !even.shell.clipboard.persist,
                "the clipboard history does not default to on, 50 entries, pictures, in memory");
        auto clipboard = paw::parse_config(
            "return {shell={clipboard={enabled=false,max_entries=500,images=false,persist=true}}}");
        require(!clipboard.shell.clipboard.enabled && clipboard.shell.clipboard.max_entries == 500 &&
                    !clipboard.shell.clipboard.images && clipboard.shell.clipboard.persist,
                "shell.clipboard not parsed");
        rejects("return {shell={clipboard=true}}");
        rejects("return {shell={clipboard={max_entries=0}}}");
        rejects("return {shell={clipboard={max_entries=501}}}");
        rejects("return {shell={clipboard={persist='yes'}}}");
        rejects("return {shell={clipboard={size=10}}}");
        using paw::WidgetPlace;
        auto widgets = paw::parse_config(
            "return {shell={widgets={battery=false,calendar=false,workspaces=false}}}");
        require(widgets.shell.widgets.battery == WidgetPlace::Hidden && !widgets.shell.widgets.calendar &&
                    !widgets.shell.widgets.workspaces && widgets.shell.widgets.network != WidgetPlace::Hidden &&
                    widgets.shell.widgets.volume != WidgetPlace::Hidden && widgets.shell.widgets.clock &&
                    widgets.shell.widgets.tiling != WidgetPlace::Hidden &&
                    widgets.shell.widgets.profiles != WidgetPlace::Hidden && widgets.shell.widgets.tray &&
                    paw::parse_config("return {shell={widgets={profiles=false}}}")
                            .shell.widgets.profiles == WidgetPlace::Hidden &&
                    !paw::parse_config("return {shell={widgets={tray=false}}}").shell.widgets.tray,
                "shell widgets not parsed");
        require(paw::parse_config("return {shell={}}").shell.widgets.battery != WidgetPlace::Hidden,
                "widgets not on by default");
        require(paw::parse_config("return {shell={}}").shell.widgets.media &&
                    !paw::parse_config("return {shell={widgets={media=false}}}").shell.widgets.media,
                "the media card is not on by default, or not switched off");
        require(paw::parse_config("return {shell={}}").shell.widgets.power_mode &&
                    !paw::parse_config("return {shell={widgets={power_mode=false}}}").shell.widgets.power_mode,
                "the power mode tile is not on by default, or not switched off");
        require(paw::parse_config("return {shell={}}").shell.widgets.bluetooth &&
                    !paw::parse_config("return {shell={widgets={bluetooth=false}}}").shell.widgets.bluetooth,
                "the Bluetooth tile is not on by default, or not switched off");
        // By default the status widgets, the profiles and do-not-disturb are in Quick Settings
        // and tiling and the wallpapers on the bar; `true` puts each there.
        const auto placed_by_default = paw::parse_config("return {shell={}}").shell.widgets;
        require(placed_by_default.network == WidgetPlace::Quick &&
                    placed_by_default.battery == WidgetPlace::Quick &&
                    placed_by_default.volume == WidgetPlace::Quick &&
                    placed_by_default.tiling == WidgetPlace::Bar &&
                    placed_by_default.profiles == WidgetPlace::Quick &&
                    placed_by_default.notifications == WidgetPlace::Quick &&
                    placed_by_default.wallpapers == WidgetPlace::Bar,
                "the widgets are not in their default places");
        const auto all = paw::parse_config(
                             "return {shell={widgets={network=true,battery=true,volume=true,tiling=true,"
                             "profiles=true,notifications=true,wallpapers=true}}}")
                             .shell.widgets;
        require(all.network == WidgetPlace::Quick && all.notifications == WidgetPlace::Quick &&
                    all.wallpapers == WidgetPlace::Bar,
                "true does not put a widget in its default place");
        // A widget that can move goes where "bar" or "quick" says; nothing else is a place.
        auto placed = paw::parse_config(
            "return {shell={widgets={network='quick',volume='bar',wallpapers='quick',tiling=true}}}");
        require(placed.shell.widgets.network == WidgetPlace::Quick &&
                    placed.shell.widgets.volume == WidgetPlace::Bar &&
                    placed.shell.widgets.wallpapers == WidgetPlace::Quick &&
                    placed.shell.widgets.tiling == paw::ShellWidgets{}.tiling,
                "widget places not parsed");
        rejects("return {shell={widgets={network='taskbar'}}}");
        rejects("return {shell={widgets={volume=1}}}");
        rejects("return {shell={widgets={clock='bar'}}}");
        rejects("return {shell={widgets={keyboard_layout='quick'}}}");
        rejects("return {shell={widgets={airplane_mode=false}}}");
        rejects("return {shell={widgets=true}}");
        rejects("return {shell={accent='#12345'}}");
        rejects("return {appearance={background='#11223344'}}");
        rejects("return {shell={accent='red'}}");
        rejects("return {shell={launchers={{name='',command={'kitty'}}}}}");
        rejects("return {shell={launchers={{name='Terminal',command='kitty'}}}}");
        rejects("return true");
        rejects("return {startup={{}}}");
        rejects("return {startup={{'kitty', [3]='bad'}}}");
        rejects("return {startup={{'kitty\\0bad'}}}");
        rejects("return {bindings={{mods={'Hyper'}, key='a', action='quit'}}}");
        rejects("return {bindings={{mods={}, key='NotAKey', action='quit'}}}");
        rejects("return {bindings={{mods={}, key='a', action='unknown'}}}");
        rejects("local b={mods={'Alt'},key='a',action='quit'}; return {bindings={b,b}}");
        // A configuration extending the defaults holds only its changes.
        setenv("PAW_DEFAULT_CONFIG", argv[1], 1);
        auto bare = paw::parse_config("return {extends='default'}");
        require(bare.bindings.size() == 86 && bare.shell.launchers.empty() &&
                    bare.settings.workspaces == 4,
                "extends did not supply the defaults");
        auto layered = paw::parse_config(
            "return {extends='default', layout={gap=3}, bindings={"
            "{mods={'Super'}, key='v', action='none'},"
            "{mods={'Super'}, key='q', action='spawn', command={'foot'}},"
            "{mods={'Super'}, key='e', action='spawn', command={'dolphin'}}}}");
        require(layered.settings.gap_inner == 3 && layered.settings.workspaces == 4,
                "extending configuration settings not layered over the defaults");
        require(layered.bindings.size() == 86 && !layered.binding(SH_LOGO, XKB_KEY_v),
                "action none did not remove a default binding");
        require(layered.binding(SH_LOGO, XKB_KEY_q)->command == paw::Command{"foot"} &&
                    layered.binding(SH_LOGO, XKB_KEY_e)->command == paw::Command{"dolphin"},
                "own bindings did not take their keys from the defaults");
        require(layered.binding(SH_LOGO, XKB_KEY_s)->action == SH_TOGGLE_TILING,
                "untouched default binding missing");
        require(paw::parse_config("return {bindings={{mods={}, key='a', action='none'}}}")
                    .bindings.empty(),
                "action none left a binding");
        require(!bare.settings.workspace_back_and_forth &&
                    paw::parse_config(
                        "return {extends='default', features={workspace_back_and_forth=true}}")
                        .settings.workspace_back_and_forth,
                "features not layered over the defaults");
        rejects("return {extends='default', features={bogus=true}}");
        rejects("return {extends='other'}");
        // `terminal` names the program the terminal action opens; unset, one is looked for.
        require(config.terminal.empty() && bare.terminal.empty() &&
                    paw::parse_config("return {extends='default', terminal={'foot','-s'}}")
                            .terminal == paw::Command{"foot", "-s"},
                "terminal not parsed");
        require(paw::parse_config("return {bindings={{key='t', action='terminal'}}}")
                        .binding(0, XKB_KEY_t)
                        ->action == SH_TERMINAL,
                "terminal action not bound");
        rejects("return {terminal='foot'}");
        rejects("return {terminal={}}");
        rejects("return {terminal={''}}");
        rejects("return {terminal={'foot', 1}}");
        rejects("return {bindings={{key='t', action='terminal', command={'foot'}}}}");
        rejects("local b={mods={'Alt'},key='a',action='quit'}; "
                "return {extends='default', bindings={b,b}}");
        auto buttons = paw::parse_config(
            "return {bindings={"
            "{button='side', app_id='^kitty$', desktop=true, action='close'},"
            "{button='extra', app_id='^kitty$', action='spawn', command={'kitty'}},"
            "{button='extra', desktop=true, action='spawn', command={'foot'}},"
            "{button='middle', app_id='^firefox$', action='none'},"
            "{button='middle', action='fullscreen'},"
            "{mods={'Super'}, button='left', action='quit'}}}");
        require(buttons.bindings.size() == 6, "button bindings missing");
        require(buttons.button_binding(0, 0x113, SH_POINTER_WINDOW, "kitty")->action == SH_CLOSE &&
                    buttons.button_binding(0, 0x113, SH_POINTER_DESKTOP, "")->action == SH_CLOSE,
                "side button binding did not match a terminal or the desktop");
        require(!buttons.button_binding(0, 0x113, SH_POINTER_WINDOW, "firefox") &&
                    !buttons.button_binding(0, 0x113, SH_POINTER_OTHER, "") &&
                    !buttons.button_binding(SH_LOGO, 0x113, SH_POINTER_WINDOW, "kitty"),
                "side button binding took another window's, a panel's, or a modified click");
        require(buttons.button_binding(0, 0x114, SH_POINTER_WINDOW, "kitty")->command ==
                        paw::Command{"kitty"} &&
                    buttons.button_binding(0, 0x114, SH_POINTER_DESKTOP, "")->command ==
                        paw::Command{"foot"},
                "bindings sharing a button did not match in order");
        require(!buttons.button_binding(0, 0x112, SH_POINTER_WINDOW, "firefox") &&
                    buttons.button_binding(0, 0x112, SH_POINTER_OTHER, "")->action ==
                        SH_FULLSCREEN,
                "button action none or an unconditional button binding misbehaved");
        require(buttons.button_binding(SH_LOGO, 0x110, SH_POINTER_WINDOW, "x")->action == SH_QUIT &&
                    !buttons.binding(0, 0),
                "modified button binding missing, or a button matched as a key");
        rejects("return {bindings={{button='wheel', action='close'}}}");
        rejects("return {bindings={{button='side', key='a', action='close'}}}");
        rejects("return {bindings={{key='a', app_id='x', action='close'}}}");
        rejects("return {bindings={{key='a', desktop=true, action='close'}}}");
        rejects("return {bindings={{button='side', app_id='(', action='close'}}}");
        std::cout << "Configuration validation and bindings passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
