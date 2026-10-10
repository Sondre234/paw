// SPDX-License-Identifier: GPL-3.0-or-later
// Configuration of what a monitor shows: mirroring another one, the display_mode action, the bits
// per channel it is drawn in, HDR, and tearing.
#include "paw/config.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

#include "config_check.hpp"

static const sh_monitor &monitor(const paw::Config &config, const std::string &name) {
    for (int i = 0; i < config.settings.monitor_count; ++i)
        if (name == config.settings.monitors[i].name)
            return config.settings.monitors[i];
    throw std::runtime_error("no monitor " + name);
}

int main() {
    try {
        // A monitor mirrors another by connector or description; unset, it joins the layout.
        auto mirrored = paw::parse_config(
            "return {outputs={monitors={['HDMI-A-1']={mirror='eDP-1',mode='1920x1080'},"
            "['DP-2']={mirror='desc:Dell U27'},['DP-3']={scale=1.5}}}}");
        require(std::string(monitor(mirrored, "HDMI-A-1").mirror) == "eDP-1" &&
                    monitor(mirrored, "HDMI-A-1").width == 1920,
                "mirror by connector not parsed");
        require(std::string(monitor(mirrored, "DP-2").mirror) == "desc:Dell U27",
                "mirror by description not parsed");
        require(!monitor(mirrored, "DP-3").mirror[0], "a monitor mirrors nothing unless told");
        rejects("return {outputs={monitors={['HDMI-A-1']={mirror=''}}}}");
        rejects("return {outputs={monitors={['HDMI-A-1']={mirror='desc:'}}}}");
        rejects("return {outputs={monitors={['HDMI-A-1']={mirror='HDMI-A-1'}}}}");
        rejects("return {outputs={monitors={['HDMI-A-1']={mirror=1}}}}");
        rejects("return {outputs={monitors={['HDMI-A-1']={mirror={'eDP-1'}}}}}");

        // display_mode takes one of Win+P's choices, or none for the popup.
        auto modes = paw::parse_config(
            "return {bindings={{key='XF86Display',action='display_mode'},"
            "{mods={'Super'},key='F7',action='display_mode',mode='duplicate'},"
            "{switch='lid',state='open',action='display_mode',mode='extend'}}}");
        const auto &b = modes.bindings;
        require(b.size() == 3 && b[0].action == SH_DISPLAY_MODE &&
                    b[0].mode == SH_DISPLAY_MODE_STEP && b[1].mode == SH_DISPLAY_MODE_DUPLICATE &&
                    b[2].mode == SH_DISPLAY_MODE_EXTEND,
                "display_mode bindings not parsed");
        require(paw::parse_display_mode("internal") == SH_DISPLAY_MODE_INTERNAL &&
                    paw::parse_display_mode("external") == SH_DISPLAY_MODE_EXTERNAL,
                "display modes by name");
        require(paw::parse_action("display_mode") == SH_DISPLAY_MODE, "display_mode action");
        rejects("return {bindings={{key='p',action='display_mode',mode='mirror'}}}");
        rejects("return {bindings={{key='p',action='display_mode',mode=2}}}");
        rejects("return {bindings={{key='p',action='display_mode',output='eDP-1'}}}");

        // bit_depth is 8 unless 10 is asked for.
        auto deep = paw::parse_config(
            "return {outputs={monitors={['DP-1']={bit_depth=10},['DP-2']={bit_depth=8},"
            "['DP-3']={}}}}");
        require(monitor(deep, "DP-1").bit_depth == 10 && monitor(deep, "DP-2").bit_depth == 8 &&
                    monitor(deep, "DP-3").bit_depth == 8,
                "bit_depth not parsed");
        rejects("return {outputs={monitors={X={bit_depth=9}}}}");
        rejects("return {outputs={monitors={X={bit_depth=12}}}}");
        rejects("return {outputs={monitors={X={bit_depth='10'}}}}");
        rejects("return {outputs={monitors={X={bit_depth=10.5}}}}");

        // HDR is off unless a monitor asks for it.
        auto hdr = paw::parse_config(
            "return {outputs={monitors={['DP-1']={hdr=true},['DP-2']={hdr=false},['DP-3']={}}}}");
        require(monitor(hdr, "DP-1").hdr && !monitor(hdr, "DP-2").hdr && !monitor(hdr, "DP-3").hdr,
                "hdr not parsed");
        rejects("return {outputs={monitors={X={hdr='on'}}}}");
        rejects("return {outputs={monitors={X={hdr=1}}}}");

        // Tearing is off unless allowed, and a rule may let a window tear that does not ask.
        require(!paw::parse_config("return {}").settings.allow_tearing,
                "tearing allowed by default");
        auto tearing = paw::parse_config(
            "return {windows={allow_tearing=true,rules={{app_id='^cs2$',allow_tearing=true},"
            "{app_id='^mpv$',allow_tearing=false},{app_id='^foot$',floating=true}}}}");
        require(tearing.settings.allow_tearing, "windows.allow_tearing not parsed");
        require(tearing.window_actions("cs2", "").to_c().allow_tearing &&
                    !tearing.window_actions("mpv", "").to_c().allow_tearing &&
                    !tearing.window_actions("mpv", "").empty() &&
                    !tearing.window_actions("foot", "").to_c().allow_tearing,
                "allow_tearing rules not parsed");
        rejects("return {windows={allow_tearing='yes'}}");
        rejects("return {windows={rules={{app_id='x',allow_tearing=1}}}}");
    } catch (const std::exception &error) {
        std::cerr << "output_config: " << error.what() << '\n';
        return 1;
    }
    std::cout << "output configuration ok\n";
    return 0;
}
