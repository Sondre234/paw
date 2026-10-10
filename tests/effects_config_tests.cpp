// SPDX-License-Identifier: GPL-3.0-or-later
// Configuration of the desktop effects: dimming, peek, night light, hot corners, zoom.
#include "paw/config.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

#include "config_check.hpp"

int main() {
    try {
        // Dimming is off by default and fades over 180 ms.
        auto defaults = paw::parse_config("return {}");
        require(defaults.settings.dim_inactive == 0 && defaults.settings.dim_duration == 180,
                "dimming defaults");
        auto dim = paw::parse_config(
            "return {windows={dim_inactive=0.3,dim_duration=250}}");
        require(dim.settings.dim_inactive > 0.29F && dim.settings.dim_inactive < 0.31F &&
                    dim.settings.dim_duration == 250,
                "dimming not parsed");
        require(paw::parse_config("return {windows={dim_inactive=0.9}}").settings.dim_inactive >
                    0.89F,
                "the strongest dimming is 0.9");
        require(paw::parse_config("return {windows={dim_duration=0}}").settings.dim_duration == 0,
                "a zero duration switches at once");
        rejects("return {windows={dim_inactive=1}}");
        rejects("return {windows={dim_inactive=-0.1}}");
        rejects("return {windows={dim_inactive='dark'}}");
        rejects("return {windows={dim_duration=5000}}");
        rejects("return {windows={dim_duration=1.5}}");
        // Peek.
        require(defaults.settings.effects.peek_opacity > 0.11F &&
                    defaults.settings.effects.peek_opacity < 0.13F &&
                    defaults.settings.effects.peek_duration == 150,
                "peek defaults");
        auto peek = paw::parse_config("return {peek={opacity=0,duration=0}}");
        require(peek.settings.effects.peek_opacity == 0 && peek.settings.effects.peek_duration == 0,
                "peek not parsed");
        rejects("return {peek={opacity=0.95}}");
        rejects("return {peek={opacity=-0.1}}");
        rejects("return {peek={size=3}}");
        auto bound = paw::parse_config(
            "return {bindings={{mods={},key='F9',action='peek'},{mods={'Super'},key='p',action='peek_toggle'}}}");
        require(!bound.bindings.empty(), "peek actions not accepted");
        // Night light.
        const auto &night = defaults.settings.effects;
        require(!night.night_light && night.day_kelvin == 6500 && night.night_kelvin == 3400 &&
                    night.sunrise == 420 && night.sunset == 1200 && !night.located &&
                    night.transition == 30,
                "night light defaults");
        auto timed = paw::parse_config(
            "return {night_light={enabled=true,day_temperature=6000,night_temperature=2500,"
            "sunrise='06:15',sunset='21:45',transition=0}}");
        const auto &t = timed.settings.effects;
        require(t.night_light && t.day_kelvin == 6000 && t.night_kelvin == 2500 &&
                    t.sunrise == 375 && t.sunset == 1305 && t.transition == 0 && !t.located,
                "night light times not parsed");
        auto located = paw::parse_config(
            "return {night_light={latitude=59.9,longitude=10.7}}").settings.effects;
        require(located.located && located.sunrise < 0 && located.sunset < 0 &&
                    located.latitude > 59.8F && located.longitude > 10.6F,
                "a location replaces the default times");
        auto both = paw::parse_config(
            "return {night_light={latitude=59.9,longitude=10.7,sunrise='05:00',sunset='22:00'}}")
                        .settings.effects;
        require(both.located && both.sunrise == 300 && both.sunset == 1320,
                "written-out times win over the location");
        rejects("return {night_light={latitude=91,longitude=0}}");
        rejects("return {night_light={latitude=0,longitude=181}}");
        rejects("return {night_light={sunrise='7am'}}");
        rejects("return {night_light={sunset='24:00'}}");
        rejects("return {night_light={sunset=20}}");
        rejects("return {night_light={day_temperature=999}}");
        rejects("return {night_light={night_temperature=3400.5}}");
        rejects("return {night_light={nights=1}}");
        require(!paw::parse_config("return {bindings={{mods={},key='F10',action='night_light_toggle'},"
                                      "{mods={},key='F11',action='night_light_on'},"
                                      "{mods={},key='F12',action='night_light_auto'}}}").bindings.empty(),
                "night light actions not accepted");
        // Hot corners.
        require(defaults.settings.effects.corner_mask == 0 &&
                    defaults.settings.effects.corner_size == 2 &&
                    defaults.settings.effects.corner_delay == 150,
                "hot corner defaults");
        auto corners = paw::parse_config(
            "return {layout={workspaces=4,workspace_names={'web'}},hot_corners={size=8,delay=0,"
            "top_left='toggle_overview',bottom_right='spawn foot -e htop',"
            "top_right='workspace web',bottom_left='none'}}");
        require(corners.settings.effects.corner_size == 8 && corners.settings.effects.corner_delay == 0,
                "hot corner size and delay");
        require(corners.settings.effects.corner_mask == 0b1011U, "hot corner mask");
        require(corners.hot_corners[0] == "toggle_overview" &&
                    corners.hot_corners[1] == "workspace web" &&
                    corners.hot_corners[3] == "spawn foot -e htop" && corners.hot_corners[2].empty(),
                "hot corner requests");
        rejects("return {hot_corners={top_left='no_such_action'}}");
        rejects("return {hot_corners={top_left='spawn'}}");
        rejects("return {hot_corners={top_left='workspace'}}");
        rejects("return {hot_corners={top_left='workspace 9'}}");
        rejects("return {hot_corners={top_left='   '}}");
        rejects("return {hot_corners={top_left=5}}");
        rejects("return {hot_corners={middle='peek'}}");
        // Zoom.
        const auto &z0 = defaults.settings.effects;
        require(z0.zoom_step > 1.24F && z0.zoom_step < 1.26F && z0.zoom_max == 8 &&
                    z0.zoom_duration == 150 && z0.zoom_scroll_modifier == 0,
                "zoom defaults");
        auto zoom = paw::parse_config(
            "return {zoom={step=1.5,max=16,duration=0,scroll_modifier='Super'}}").settings.effects;
        require(zoom.zoom_step > 1.49F && zoom.zoom_step < 1.51F && zoom.zoom_max == 16 &&
                    zoom.zoom_duration == 0 && zoom.zoom_scroll_modifier == SH_LOGO,
                "zoom not parsed");
        require(paw::parse_config("return {zoom={scroll_modifier=''}}")
                        .settings.effects.zoom_scroll_modifier == 0,
                "an empty modifier turns the wheel off");
        rejects("return {zoom={scroll_modifier='Hyper'}}");
        rejects("return {zoom={scroll_modifier=1}}");
        rejects("return {zoom={step=1}}");
        rejects("return {zoom={step=5}}");
        rejects("return {zoom={max=1}}");
        require(!paw::parse_config("return {bindings={{mods={'Super'},key='equal',action='zoom_in'},"
                                      "{mods={'Super'},key='minus',action='zoom_out'},"
                                      "{mods={'Super'},key='0',action='zoom_reset'}}}").bindings.empty(),
                "zoom actions not accepted");
        std::cout << "Effects configuration passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
