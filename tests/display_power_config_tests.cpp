// SPDX-License-Identifier: GPL-3.0-or-later
// Configuration of monitors turned off in the layout: the display actions, the idle steps, and
// the lid with the bindings of switches.
#include "paw/config.hpp"
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
static bool steps_are(const sh_idle_steps &steps, int dim, int display_off, int lock, int suspend) {
    return steps.dim == dim && steps.display_off == display_off && steps.lock == lock &&
           steps.suspend == suspend;
}
static void rejects(const std::string &source) {
    try {
        (void)paw::parse_config(source);
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("invalid configuration was accepted: " + source);
}

int main() {
    try {
        auto bound = paw::parse_config(
            "return {bindings={{mods={'Super'},key='F1',action='display_off'},"
            "{mods={'Super'},key='F2',action='display_on',output='HDMI-A-1'},"
            "{mods={'Super'},key='F3',action='display_toggle',output='desc:Dell U27'}}}");
        const auto &b = bound.bindings;
        require(b.size() == 3 && b[0].action == SH_DISPLAY_OFF && b[0].output.empty() &&
                    b[1].action == SH_DISPLAY_ON && b[1].output == "HDMI-A-1" &&
                    b[2].action == SH_DISPLAY_TOGGLE && b[2].output == "desc:Dell U27",
                "display bindings not parsed");
        require(paw::action_takes_display(SH_DISPLAY_OFF) &&
                    !paw::action_takes_display(SH_LOCK) &&
                    !paw::action_takes_output(SH_DISPLAY_TOGGLE),
                "display actions' targets");
        rejects("return {bindings={{key='l',action='display_off',output=''}}}");
        rejects("return {bindings={{key='l',action='display_off',output='desc:'}}}");
        rejects("return {bindings={{key='l',action='display_on',output=1}}}");
        rejects("return {bindings={{key='l',action='lock',output='HDMI-A-1'}}}");
        // The idle steps, in seconds, kept in milliseconds. Unset, the screens dim 30 seconds
        // before the monitors go off after ten minutes, and nothing locks or suspends.
        auto defaults = paw::parse_config("return {}").settings;
        require(steps_are(defaults.idle, 570000, 600000, 0, 0) &&
                    steps_are(defaults.idle_battery, 570000, 600000, 0, 0),
                "idle defaults");
        auto idle = [](const char *table) {
            return paw::parse_config(std::string("return {idle=") + table + "}").settings;
        };
        require(steps_are(idle("{display_off=300}").idle, 270000, 300000, 0, 0),
                "dimming follows display_off");
        require(steps_are(idle("{display_off=40}").idle, 20000, 40000, 0, 0),
                "dimming halfway under a minute");
        require(steps_are(idle("{display_off=0}").idle, 0, 0, 0, 0) &&
                    steps_are(idle("{display_off=0}").idle_battery, 0, 0, 0, 0),
                "display_off = 0 turns dimming off too");
        require(steps_are(idle("{dim=0}").idle, 0, 600000, 0, 0), "dim = 0");
        require(steps_are(idle("{dim=700,display_off=600}").idle, 700000, 600000, 0, 0),
                "dim after display_off is kept as given");
        auto both = idle("{dim=100,display_off=300,lock=600,suspend=1800,"
                         "battery={display_off=120,suspend=600}}");
        require(steps_are(both.idle, 100000, 300000, 600000, 1800000) &&
                    steps_are(both.idle_battery, 90000, 120000, 600000, 600000),
                "battery steps not parsed");
        require(steps_are(idle("{lock=60,battery={dim=10}}").idle_battery, 10000, 600000, 60000, 0),
                "battery dim not parsed");
        rejects("return {idle=true}");
        rejects("return {idle={dim=-1}}");
        rejects("return {idle={display_off=1.5}}");
        rejects("return {idle={lock='5'}}");
        rejects("return {idle={suspend=86401}}");
        rejects("return {idle={sleep=60}}");
        rejects("return {idle={battery=300}}");
        rejects("return {idle={battery={hibernate=60}}}");
        // The lid: clamshell unless set, and bindings on switches.
        require(defaults.lid == SH_LID_CLAMSHELL &&
                    paw::parse_config("return {outputs={lid='ignore'}}").settings.lid ==
                        SH_LID_IGNORE,
                "outputs.lid not parsed");
        rejects("return {outputs={lid='suspend'}}");
        rejects("return {outputs={lid=true}}");
        auto switches = paw::parse_config(
            "return {bindings={{switch='lid',state='close',action='lock'},"
            "{switch='lid',state='open',action='display_on',output='eDP-1'},"
            "{switch='tablet',state='on',action='spawn',command={'onboard'}},"
            "{mods={'Super'},key='l',action='close'}}}");
        auto *closing = switches.switch_binding(SH_SWITCH_LID, true);
        auto *opening = switches.switch_binding(SH_SWITCH_LID, false);
        auto *tablet = switches.switch_binding(SH_SWITCH_TABLET, true);
        require(closing && closing->action == SH_LOCK && opening &&
                    opening->action == SH_DISPLAY_ON && opening->output == "eDP-1" && tablet &&
                    tablet->action == SH_SPAWN && tablet->command == paw::Command{"onboard"} &&
                    !switches.switch_binding(SH_SWITCH_TABLET, false),
                "switch bindings not parsed");
        require(!switches.binding(0, 0) && switches.binding(SH_LOGO, 'l') &&
                    switches.binding(SH_LOGO, 'l')->action == SH_CLOSE,
                "a switch binding taken for a key");
        rejects("return {bindings={{switch='lid',action='lock'}}}");
        rejects("return {bindings={{switch='lid',state='closed',action='lock'}}}");
        rejects("return {bindings={{switch='tablet',state='close',action='lock'}}}");
        rejects("return {bindings={{switch='lidd',state='close',action='lock'}}}");
        rejects("return {bindings={{switch='lid',state='close',mods={'Super'},action='lock'}}}");
        rejects("return {bindings={{switch='lid',state='close',key='l',action='lock'}}}");
        rejects("return {bindings={{key='l',state='close',action='lock'}}}");
        rejects("return {bindings={{switch='lid',state='close',action='resize_left',repeats=true}}}");
        rejects("return {bindings={{switch='lid',state='close',action='lock'},"
                "{switch='lid',state='close',action='suspend'}}}");
        std::cout << "display power configuration passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
