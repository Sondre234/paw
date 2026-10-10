// SPDX-License-Identifier: GPL-3.0-or-later
// Configuration of windows.swallow (and the other window-management settings of `windows`).
#include "paw/config.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>

#include "config_check.hpp"

int main() {
    try {
        // Swallowing is off by default and knows the common terminals.
        auto defaults = paw::parse_config("return {}");
        require(!defaults.settings.swallow && defaults.settings.swallow_terminal_count == 10 &&
                    !std::strcmp(defaults.settings.swallow_terminals[0], "kitty") &&
                    defaults.settings.swallow_exception_count == 0,
                "swallow defaults changed");
        auto swallow = paw::parse_config(
            "return {windows={swallow={enabled=true,terminals={'foo','Bar'},exceptions={'baz'}}}}");
        require(swallow.settings.swallow && swallow.settings.swallow_terminal_count == 2 &&
                    !std::strcmp(swallow.settings.swallow_terminals[1], "Bar") &&
                    swallow.settings.swallow_exception_count == 1 &&
                    !std::strcmp(swallow.settings.swallow_exceptions[0], "baz"),
                "swallow settings not read");
        auto none = paw::parse_config("return {windows={swallow={terminals={}}}}");
        require(!none.settings.swallow && none.settings.swallow_terminal_count == 0,
                "an empty terminal list should clear the defaults");
        require(paw::parse_config("return {windows={swallow={enabled=true}}}")
                        .settings.swallow_terminal_count == 10,
                "enabling keeps the default terminals");
        rejects("return {windows={swallow={terminals='kitty'}}}");
        rejects("return {windows={swallow={terminals={1}}}}");
        rejects("return {windows={swallow={terminal={'kitty'}}}}");
        rejects("return {windows={swallow=true}}");
        std::string many = "return {windows={swallow={terminals={";
        for (int i = 0; i < 33; ++i)
            many += "'t" + std::to_string(i) + "',";
        rejects(many + "}}}}");
        rejects("return {windows={swallow={exceptions={'" + std::string(70, 'x') + "'}}}}");
        require(!paw::parse_config("return {bindings={{mods={'Super'},key='w',"
                                      "action='swallow_toggle'}}}").bindings.empty(),
                "swallow_toggle not accepted");
        // Magnetic edges are on, 12 pixels, with guides and Shift to bypass.
        require(defaults.settings.magnet && defaults.settings.magnet_distance == 12 &&
                    defaults.settings.magnet_guides && defaults.settings.magnet_bypass == SH_SHIFT &&
                    defaults.settings.magnet_guide_color[3] > 0.69F &&
                    defaults.settings.magnet_guide_color[3] < 0.71F,
                "magnet defaults changed");
        auto magnet = paw::parse_config(
            "return {windows={magnet={enabled=false,distance=30,guides=false,bypass='Super',"
            "guide_color='#ff000080'}}}");
        require(!magnet.settings.magnet && magnet.settings.magnet_distance == 30 &&
                    !magnet.settings.magnet_guides && magnet.settings.magnet_bypass == SH_LOGO &&
                    magnet.settings.magnet_guide_color[0] > 0.49F &&
                    magnet.settings.magnet_guide_color[0] < 0.51F,
                "magnet settings not read (premultiplied color)");
        require(paw::parse_config("return {windows={magnet={bypass='none'}}}")
                        .settings.magnet_bypass == 0,
                "bypass = none");
        require(paw::parse_config("return {windows={magnet={distance=0}}}")
                        .settings.magnet_distance == 0 &&
                    paw::parse_config("return {windows={magnet={distance=200}}}")
                            .settings.magnet_distance == 200,
                "distance limits");
        rejects("return {windows={magnet={distance=1.5}}}");
        rejects("return {windows={magnet={enabled=1}}}");
        rejects("return {windows={magnet={bypass='Hyper'}}}");
        rejects("return {windows={magnet={guide_color='blue'}}}");
        rejects("return {windows={magnet={dist=3}}}");
        rejects("return {windows={magnet=false}}");
        // Snapping by dragging is on, 8 pixels from the edge, with corners and a preview in the
        // focused border's colour, a quarter opaque, unless given one.
        require(defaults.settings.snap && defaults.settings.snap_distance == 8 &&
                    defaults.settings.snap_corners && defaults.settings.snap_preview &&
                    defaults.settings.snap_color[3] > 0.24F &&
                    defaults.settings.snap_color[3] < 0.26F && defaults.settings.snap_assist,
                "snap defaults changed");
        auto snap = paw::parse_config(
            "return {windows={snap={enabled=false,distance=20,corners=false,preview=false,"
            "color='#ff000080',assist=false}}}");
        require(!snap.settings.snap && snap.settings.snap_distance == 20 &&
                    !snap.settings.snap_corners && !snap.settings.snap_preview &&
                    !snap.settings.snap_assist &&
                    snap.settings.snap_color[0] > 0.49F && snap.settings.snap_color[0] < 0.51F &&
                    snap.settings.snap_color[1] == 0,
                "snap settings not read (premultiplied color)");
        auto bordered = paw::parse_config("return {windows={border_color='#00ff00'}}");
        require(bordered.settings.snap_color[1] > 0.24F && bordered.settings.snap_color[1] < 0.26F &&
                    bordered.settings.snap_color[0] == 0,
                "the snap preview follows the border colour");
        require(paw::parse_config("return {windows={snap={distance=1}}}")
                        .settings.snap_distance == 1,
                "snap distance 1");
        rejects("return {windows={snap={distance=0}}}");
        rejects("return {windows={snap={distance=101}}}");
        rejects("return {windows={snap={corners='yes'}}}");
        rejects("return {windows={snap={color='blue'}}}");
        rejects("return {windows={snap={assits=true}}}");
        rejects("return {windows={snap={assist='no'}}}");
        rejects("return {windows={snap=true}}");
        // windows.placement: cascade unless told otherwise.
        require(defaults.settings.placement == SH_PLACE_CASCADE, "placement default");
        for (auto [name, mode] : {std::pair{"cascade", SH_PLACE_CASCADE},
                                  {"center", SH_PLACE_CENTER}, {"smart", SH_PLACE_SMART}}) {
            require(paw::parse_config(std::string("return {windows={placement='") + name + "'}}")
                            .settings.placement == mode,
                    "placement not read");
        }
        rejects("return {windows={placement='random'}}");
        rejects("return {windows={placement=1}}");
        rejects("return {windows={placement=true}}");
        // windows.controls: the flat strip unless told otherwise, and a profile may switch it.
        require(defaults.settings.window_controls == SH_CONTROLS_FLAT, "controls default");
        require(paw::parse_config("return {windows={controls='traffic_lights'}}")
                        .settings.window_controls == SH_CONTROLS_TRAFFIC_LIGHTS,
                "controls not read");
        require(paw::parse_config("return {profile='mac',profiles={mac={windows={"
                                       "controls='traffic_lights'}}}}")
                        .settings.window_controls == SH_CONTROLS_TRAFFIC_LIGHTS,
                "a profile's controls not applied");
        // windows.round: tiling monitors unless told otherwise.
        require(!defaults.settings.round_always, "round default");
        require(paw::parse_config("return {windows={round='always'}}").settings.round_always,
                "round not read");
        require(!paw::parse_config("return {windows={round='tiling'}}").settings.round_always,
                "round = tiling not read");
        rejects("return {windows={round='floating'}}");
        rejects("return {windows={round=true}}");
        // windows.shadow: off, 30 pixels soft and 10 down, darker under the focused window.
        require(!defaults.settings.shadow && defaults.settings.shadow_blur == 30 &&
                    defaults.settings.shadow_x == 0 && defaults.settings.shadow_y == 10 &&
                    defaults.settings.shadow_color[3] > defaults.settings.shadow_inactive_color[3],
                "shadow defaults changed");
        auto shadow = paw::parse_config(
            "return {windows={shadow={enabled=true,color='#ff000080',inactive_color='#00000010',"
            "blur=12,offset={-3,4}}}}");
        require(shadow.settings.shadow && shadow.settings.shadow_blur == 12 &&
                    shadow.settings.shadow_x == -3 && shadow.settings.shadow_y == 4 &&
                    shadow.settings.shadow_color[0] > 0.49F && shadow.settings.shadow_color[0] < 0.51F &&
                    shadow.settings.shadow_inactive_color[3] < 0.07F,
                "shadow settings not read (premultiplied colors)");
        auto down = paw::parse_config("return {windows={shadow={offset=7}}}");
        require(down.settings.shadow_x == 0 && down.settings.shadow_y == 7 && !down.settings.shadow,
                "a shadow offset of one number is how far down");
        require(paw::parse_config("return {windows={shadow={offset={x=5,y=-6}}}}")
                        .settings.shadow_x == 5,
                "a shadow offset by name");
        require(paw::parse_config("return {profile='mac',profiles={mac={windows={"
                                       "shadow={enabled=true}}}}}")
                    .settings.shadow,
                "a profile's shadow not applied");
        rejects("return {windows={shadow=true}}");
        rejects("return {windows={shadow={blur=101}}}");
        rejects("return {windows={shadow={offset=51}}}");
        rejects("return {windows={shadow={offset={1,2,3}}}}");
        rejects("return {windows={shadow={offset='down'}}}");
        rejects("return {windows={shadow={color='black'}}}");
        rejects("return {windows={shadow={spread=3}}}");
        rejects("return {windows={controls='macos'}}");
        rejects("return {windows={controls=true}}");
        std::cout << "Window feature configuration passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
