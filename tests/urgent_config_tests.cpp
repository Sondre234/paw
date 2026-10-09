// SPDX-License-Identifier: GPL-3.0-or-later
// Configuration of urgent windows: windows.activation and windows.urgent_color.
#include "paw/config.hpp"
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
static void rejects(const std::string &source, const char *fragment) {
    try {
        (void)paw::parse_config(source);
    } catch (const std::exception &error) {
        require(std::string(error.what()).find(fragment) != std::string::npos,
                "the error does not say what is wrong");
        return;
    }
    throw std::runtime_error("invalid configuration was accepted: " + source);
}

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
