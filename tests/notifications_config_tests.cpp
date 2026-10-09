// SPDX-License-Identifier: GPL-3.0-or-later
// Configuration of the notification daemon and the on-screen display.
#include "paw/config.hpp"
#include <iostream>
#include <stdexcept>

static void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
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
        using paw::Corner;
        auto defaults = paw::parse_config("return {}");
        const auto &n = defaults.notifications;
        require(n.enabled && n.position == Corner::TopRight && n.timeout == 6000 &&
                    n.max_visible == 4 && !n.dnd && n.width == 360 && n.history == 100,
                "notification defaults");
        const auto &o = defaults.osd;
        require(o.enabled && !o.top && o.timeout == 1500 && o.volume && o.brightness,
                "osd defaults");
        auto custom = paw::parse_config(
            "return {notifications={enabled=false,position='bottom-left',timeout=0,max_visible=2,"
            "dnd=true,width=500,history=0},osd={enabled=false,position='top',timeout=800,"
            "volume=false,brightness=false}}");
        require(!custom.notifications.enabled && custom.notifications.position == Corner::BottomLeft &&
                    custom.notifications.timeout == 0 && custom.notifications.max_visible == 2 &&
                    custom.notifications.dnd && custom.notifications.width == 500 &&
                    custom.notifications.history == 0,
                "notifications not parsed");
        require(!custom.osd.enabled && custom.osd.top && custom.osd.timeout == 800 &&
                    !custom.osd.volume && !custom.osd.brightness,
                "osd not parsed");
        for (auto [name, corner] : {std::pair{"top-left", Corner::TopLeft},
                                    std::pair{"bottom-right", Corner::BottomRight},
                                    std::pair{"top-right", Corner::TopRight}})
            require(paw::parse_config(std::string("return {notifications={position='") + name +
                                         "'}}")
                            .notifications.position == corner,
                    "position not parsed");
        rejects("return {notifications={position='middle'}}");
        rejects("return {notifications={position=1}}");
        rejects("return {notifications={timeout=1.5}}");
        rejects("return {notifications={width=100}}");
        rejects("return {notifications={sound=true}}");
        rejects("return {notifications=3}");
        rejects("return {osd={position='left'}}");
        rejects("return {osd={timeout=100}}");
        rejects("return {osd={timeout=20000}}");
        rejects("return {osd={volume=1}}");
        rejects("return {osd={color='red'}}");
        std::cout << "notification and osd configuration passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
