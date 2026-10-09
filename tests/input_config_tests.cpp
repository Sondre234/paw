// SPDX-License-Identifier: GPL-3.0-or-later
// Configuration of touch input: the touchpad swipes the compositor takes (`gestures`) and the
// outputs touchscreens and drawing tablets are mapped to (`touch`, `tablet`).
#include "paw/config.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

static void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}
static void rejects(const std::string &source, const char *fragment) {
    try {
        (void)paw::parse_config(source);
    } catch (const std::exception &error) {
        require(std::string(error.what()).find(fragment) != std::string::npos,
                std::string("the error does not say what is wrong: ") + error.what());
        return;
    }
    throw std::runtime_error("invalid configuration was accepted: " + source);
}
static const sh_swipe_binding *find(const sh_gesture_settings &gestures, int fingers,
                                    sh_swipe_direction direction) {
    for (int i = 0; i < gestures.swipe_count; ++i)
        if (gestures.swipes[i].fingers == fingers && gestures.swipes[i].direction == direction)
            return &gestures.swipes[i];
    return nullptr;
}

static void gestures() {
    // On by default: three fingers sideways switch workspaces, up and down the overview.
    auto defaults = paw::parse_config("return {}").settings.gestures;
    require(defaults.enabled && defaults.distance == 300 && !defaults.invert, "gesture defaults");
    require(defaults.swipe_count == 4, "four default swipes");
    struct {
        sh_swipe_direction direction;
        sh_action action;
        const char *request;
    } expected[] = {{SH_SWIPE_LEFT, SH_WORKSPACE_NEXT, "workspace_next"},
                    {SH_SWIPE_RIGHT, SH_WORKSPACE_PREV, "workspace_prev"},
                    {SH_SWIPE_UP, SH_OVERVIEW_TOGGLE, "toggle_overview"},
                    {SH_SWIPE_DOWN, SH_OVERVIEW_CANCEL, "overview_cancel"}};
    for (const auto &e : expected) {
        const auto *swipe = find(defaults, 3, e.direction);
        require(swipe && swipe->action == e.action && !std::strcmp(swipe->request, e.request),
                std::string("default swipe ") + e.request);
    }
    // Settings change, and the shipped configuration keeps the defaults.
    auto set = paw::parse_config(
                   "return {gestures={enabled=false, distance=120, invert=true}}")
                   .settings.gestures;
    require(!set.enabled && set.distance == 120 && set.invert && set.swipe_count == 4,
            "gesture settings");

    // A list replaces the defaults; requests take arguments as hot corners' do.
    auto own = paw::parse_config(
                   "return {layout={workspaces=6, workspace_names={'one','two'}}, gestures={swipes={"
                   "{fingers=4, direction='left', action='workspace_next'},"
                   "{direction='up', action='spawn foot --server'},"
                   "{fingers=5, direction='down', action='workspace two'},"
                   "{fingers=4, direction='right', action='none'}}}}")
                   .settings.gestures;
    require(own.swipe_count == 4, "own swipes");
    require(!find(own, 3, SH_SWIPE_LEFT), "a list replaces the default swipes");
    require(find(own, 4, SH_SWIPE_LEFT) && find(own, 4, SH_SWIPE_LEFT)->action == SH_WORKSPACE_NEXT,
            "four fingers left");
    const auto *spawn = find(own, 3, SH_SWIPE_UP); // fingers default to 3
    require(spawn && spawn->action == SH_SPAWN && !std::strcmp(spawn->request, "spawn foot --server"),
            "a swipe that spawns");
    require(find(own, 5, SH_SWIPE_DOWN) && find(own, 5, SH_SWIPE_DOWN)->action == SH_WORKSPACE,
            "a named workspace");
    require(find(own, 4, SH_SWIPE_RIGHT) && find(own, 4, SH_SWIPE_RIGHT)->action == SH_NONE,
            "a swipe taken for nothing");
    require(paw::parse_config("return {gestures={swipes={}}}").settings.gestures.swipe_count ==
                0,
            "an empty list takes no swipe");

    rejects("return {gestures={distance=10}}", "gestures.distance must be between 50 and 2000");
    rejects("return {gestures={invert='yes'}}", "gestures.invert must be a boolean");
    rejects("return {gestures={swipes={{fingers=2, direction='left', action='close'}}}}",
            "fingers must be between 3 and 5");
    rejects("return {gestures={swipes={{direction='sideways', action='close'}}}}",
            "unknown direction 'sideways'");
    rejects("return {gestures={swipes={{direction='lfet', action='close'}}}}", "did you mean 'left'");
    rejects("return {gestures={swipes={{direction='left'}}}}", "action must be a string");
    rejects("return {gestures={swipes={{direction='left', action='clsoe'}}}}",
            "did you mean 'close'");
    rejects("return {gestures={swipes={{direction='left', action='  '}}}}", "action is blank");
    rejects("return {gestures={swipes={{direction='left', action='spawn'}}}}",
            "needs a program after spawn");
    rejects("return {gestures={swipes={{direction='left', action='workspace 9'}}}}",
            "needs a workspace number or name");
    rejects("return {gestures={swipes={{direction='left', action='close'},"
            "{fingers=3, direction='left', action='workspace_next'}}}}",
            "two swipes of 3 fingers left");
    rejects("return {gestures={swipes={{direction='left', action='close', finger=3}}}}",
            "did you mean 'fingers'");
    std::string many = "return {gestures={swipes={";
    for (int i = 0; i < 17; ++i)
        many += "{direction='left', action='close'},";
    rejects(many + "}}}", "list is too long");
}

static void touch() {
    require(paw::parse_config("return {}").settings.touch_output[0] == '\0',
            "touchscreens follow their device or the built-in panel by default");
    require(!std::strcmp(paw::parse_config("return {touch={output='eDP-1'}}")
                             .settings.touch_output,
                         "eDP-1"),
            "touch.output by connector");
    require(!std::strcmp(paw::parse_config("return {touch={output='desc:Dell U2720Q'}}")
                             .settings.touch_output,
                         "desc:Dell U2720Q"),
            "touch.output by description");
    rejects("return {touch={output=1}}", "output must be a string");
    rejects("return {touch={output='desc:'}}", "touch.output needs a description");
    rejects("return {touch={output='" + std::string(200, 'x') + "'}}", "output is too long");
    rejects("return {touch={ouptut='eDP-1'}}", "did you mean 'output'");
}

static void tablet() {
    require(paw::parse_config("return {}").settings.tablet_output[0] == '\0',
            "tablets span every output by default");
    require(!std::strcmp(paw::parse_config("return {tablet={output='DP-2'}}")
                             .settings.tablet_output,
                         "DP-2"),
            "tablet.output");
    rejects("return {tablet={output=true}}", "output must be a string");
    rejects("return {tablet={output='desc:'}}", "tablet.output needs a description");
    rejects("return {tablet={mapping='DP-2'}}", "unknown setting 'mapping' in tablet");
}

int main() {
    try {
        gestures();
        touch();
        tablet();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "input configuration tests passed\n";
}
