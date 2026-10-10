// SPDX-License-Identifier: GPL-3.0-or-later
// Configuration of the power controls.
#include "paw/config.hpp"
#include <iostream>
#include <stdexcept>

#include "config_check.hpp"

int main() {
    try {
        auto defaults = paw::parse_config("return {}");
        const auto &d = defaults.settings;
        require(defaults.power.lock_command == paw::Command{"swaylock", "-f"} &&
                    d.lock_before_sleep && d.close_windows && d.close_timeout == 5000 &&
                    !d.close_force,
                "power defaults");
        auto custom = paw::parse_config(
            "return {power={lock_command={'gtklock','--daemonize'},lock_before_sleep=false,"
            "close_windows=false,close_timeout=500,force=true}}");
        const auto &c = custom.settings;
        require(custom.power.lock_command == paw::Command{"gtklock", "--daemonize"} &&
                    !c.lock_before_sleep && !c.close_windows && c.close_timeout == 500 &&
                    c.close_force,
                "power not parsed");
        require(paw::parse_config("return {power={lock_command={}}}").power.lock_command.empty(),
                "an empty lock_command is not none");
        rejects("return {power={lock_command='swaylock -f'}}");
        rejects("return {power={lock_command={''}}}");
        rejects("return {power={lock_command={'swaylock',1}}}");
        rejects("return {power={lock_command={[2]='swaylock'}}}");
        rejects("return {power={locker={'swaylock'}}}");
        require(defaults.shell.widgets.power &&
                    !paw::parse_config("return {shell={widgets={power=false}}}")
                         .shell.widgets.power,
                "shell.widgets.power not parsed");
        rejects("return {power=true}");
        rejects("return {power={close_windows=1}}");
        rejects("return {power={close_timeout=1.5}}");
        require(defaults.power.countdown == 10 &&
                    paw::parse_config("return {power={countdown=0}}").power.countdown == 0 &&
                    paw::parse_config("return {power={countdown=300}}").power.countdown ==
                        300,
                "power.countdown not parsed");
        rejects("return {power={countdown='10'}}");
        std::cout << "power configuration passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
