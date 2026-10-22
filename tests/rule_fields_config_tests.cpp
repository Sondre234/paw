// SPDX-License-Identifier: GPL-3.0-or-later
// Configuration of the window rules' look, idle inhibition and scratchpad: shadow, border,
// controls, border_color, border_inactive_color, corner_radius, idle_inhibit and scratchpad.
#include "paw/config.hpp"
#include <iostream>
#include <stdexcept>

#include "config_check.hpp"

namespace {
bool near(float a, float b) { return a > b - 0.01F && a < b + 0.01F; }
} // namespace

int main() {
    try {
        // Unset, every field leaves the window to the defaults.
        auto plain = paw::parse_config("return {windows={rules={{app_id='^x$',floating=true}}}}")
                         .window_actions("x", "")
                         .to_c();
        require(plain.shadow == -1 && plain.border == -1 && plain.controls == -1 &&
                    plain.corner_radius == -1 &&
                    plain.border_color.count == 0 && plain.border_inactive_color.count == 0 &&
                    plain.idle_inhibit == SH_IDLE_RULE_UNSET && !plain.scratchpad,
                "a rule without the new fields set them");

        // Each field, and later rules winning over earlier ones as for the actions.
        auto ruled = paw::parse_config(
            "return {windows={rules={"
            "{app_id='^mpv$',shadow=false,border=false,corner_radius=0,idle_inhibit='always'},"
            "{app_id='^mpv$',title='Pause',idle_inhibit='none',shadow=true},"
            "{app_id='^kitty$',border_color='#ff0000',"
            "border_inactive_color={'#00ff0080','#0000ff',angle=90},corner_radius=16},"
            "{app_id='^dropterm$',scratchpad=true,idle_inhibit='focus'},"
            "{app_id='^firefox$',controls=true},{app_id='^firefox$',title='Video',controls=false},"
            "{title='^Game',idle_inhibit='fullscreen'},{title='^Board',idle_inhibit='visible'}}}}");
        auto mpv = ruled.window_actions("mpv", "Film").to_c();
        require(mpv.shadow == 0 && mpv.border == 0 && mpv.corner_radius == 0 &&
                    mpv.idle_inhibit == SH_IDLE_RULE_ALWAYS && mpv.border_color.count == 0,
                "the look and idle_inhibit of one rule");
        auto paused = ruled.window_actions("mpv", "Pause").to_c();
        require(paused.shadow == 1 && paused.border == 0 &&
                    paused.idle_inhibit == SH_IDLE_RULE_NONE,
                "a later rule did not win");
        auto kitty = ruled.window_actions("kitty", "").to_c();
        require(kitty.border_color.count == 1 && near(kitty.border_color.stops[0][0], 1) &&
                    near(kitty.border_color.stops[0][1], 0) && kitty.corner_radius == 16,
                "a rule's border_color");
        const auto &inactive = kitty.border_inactive_color;
        require(inactive.count == 2 && near(inactive.angle, 90) &&
                    near(inactive.stops[0][1], 0.5F) && near(inactive.stops[0][3], 0.5F) &&
                    near(inactive.stops[1][2], 1),
                "a rule's border_inactive_color gradient, premultiplied");
        require(kitty.shadow == -1 && kitty.border == -1 &&
                    kitty.idle_inhibit == SH_IDLE_RULE_UNSET,
                "a rule set what it does not say");
        require(ruled.window_actions("firefox", "").to_c().controls == 1 &&
                    ruled.window_actions("firefox", "Video").to_c().controls == 0 &&
                    kitty.controls == -1,
                "a rule's controls");
        auto drop = ruled.window_actions("dropterm", "").to_c();
        require(drop.scratchpad && drop.idle_inhibit == SH_IDLE_RULE_FOCUS, "scratchpad rule");
        require(ruled.window_actions("x", "Game").to_c().idle_inhibit == SH_IDLE_RULE_FULLSCREEN &&
                    ruled.window_actions("x", "Board").to_c().idle_inhibit == SH_IDLE_RULE_VISIBLE,
                "idle_inhibit's values");
        // A rule only for these takes no part in the opacity, as one only for actions.
        auto looks = paw::parse_config(
            "return {windows={rules={{app_id='^mpv$',shadow=false},{app_id='mpv',opacity=0.5}}}}");
        require(looks.window_opacity("mpv", "", true) == 0.5F, "a look rule took part in opacity");
        // A dynamic rule gives them too: they follow the title whatever the rule.
        auto dynamic =
            paw::parse_config("return {windows={rules={{title='Meeting',dynamic=true,floating=true,"
                              "idle_inhibit='visible',border_color='#00ff00'}}}}");
        auto meeting = dynamic.window_actions("firefox", "Meeting").to_c();
        require(meeting.idle_inhibit == SH_IDLE_RULE_VISIBLE && meeting.border_color.count == 1 &&
                    meeting.floating == -1,
                "a dynamic rule's look and idle_inhibit");
        // features.window_rules = false turns them off with the actions.
        auto off = paw::parse_config(
            "return {features={window_rules=false},windows={rules={{app_id='^mpv$',"
            "shadow=false,idle_inhibit='always'}}}}");
        require(off.window_actions("mpv", "").empty(), "features.window_rules = false ignored");

        rejects("return {windows={rules={{app_id='x',shadow='no'}}}}",
                "windows.rules[1].shadow must be a boolean");
        rejects("return {windows={rules={{app_id='x',border=0}}}}",
                "windows.rules[1].border must be a boolean");
        rejects("return {windows={rules={{app_id='x',controls='flat'}}}}",
                "windows.rules[1].controls must be a boolean");
        rejects("return {windows={rules={{app_id='x',scratchpad=1}}}}",
                "windows.rules[1].scratchpad must be a boolean");
        rejects("return {windows={rules={{app_id='x',corner_radius=41}}}}",
                "windows.rules[1].corner_radius must be between 0 and 40, not 41");
        rejects("return {windows={rules={{app_id='x',corner_radius=-1}}}}", "corner_radius");
        rejects("return {windows={rules={{app_id='x',corner_radius=2.5}}}}", "corner_radius");
        rejects("return {windows={rules={{app_id='x',border_color='red'}}}}",
                "windows.rules[1].border_color must be #RRGGBB");
        rejects(
            "return {windows={rules={{app_id='x'},{app_id='y',border_inactive_color={'#fff'}}}}}",
            "windows.rules[2].border_inactive_color must be #RRGGBB, #RRGGBBAA, or a gradient");
        rejects("return {windows={rules={{app_id='x',border_color={'#ffffff','#000000',"
                "angle=400}}}}}",
                "angle");
        rejects("return {windows={rules={{app_id='x',idle_inhibit='fulscreen'}}}}",
                "did you mean 'fullscreen'?");
        rejects("return {windows={rules={{app_id='x',idle_inhibit=true}}}}",
                "windows.rules[1].idle_inhibit must be a string");
        rejects("return {windows={rules={{app_id='x',no_shadow=true}}}}",
                "unknown setting 'no_shadow'");
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "window rule look, idle_inhibit and scratchpad configuration tests passed\n";
}
