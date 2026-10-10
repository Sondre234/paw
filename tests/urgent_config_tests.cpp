// SPDX-License-Identifier: GPL-3.0-or-later
// Configuration of urgent windows: windows.activation and windows.urgent_color.
#include "paw/config.hpp"
#include <iostream>
#include <stdexcept>

#include "config_check.hpp"

int main() {
    try {
        // Nothing steals focus by default.
        auto defaults = paw::parse_config("return {}");
        require(defaults.settings.activation == SH_ACTIVATION_URGENT, "activation default");
        require(defaults.settings.urgent_color[3] == 1.0F && defaults.settings.urgent_color[0] > 0.99F,
                "urgent color default");
        for (auto [name, value] : {std::pair{"focus", SH_ACTIVATION_FOCUS},
                                   {"urgent", SH_ACTIVATION_URGENT},
                                   {"ignore", SH_ACTIVATION_IGNORE}}) {
            auto config = paw::parse_config(std::string("return {windows={activation='") + name + "'}}");
            require(config.settings.activation == value, "activation not parsed");
        }
        auto color = paw::parse_config("return {windows={urgent_color='#00ff0080'}}");
        require(color.settings.urgent_color[1] > 0.49F && color.settings.urgent_color[1] < 0.51F &&
                    color.settings.urgent_color[3] > 0.49F && color.settings.urgent_color[3] < 0.51F,
                "urgent color is premultiplied RGBA");
        rejects("return {windows={activation='steal'}}", "windows.activation");
        rejects("return {windows={activation=true}}", "activation");
        rejects("return {windows={urgent_color='orange'}}", "urgent_color");
        rejects("return {windows={urgent_color=5}}", "urgent_color");
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "urgent configuration tests passed\n";
}
