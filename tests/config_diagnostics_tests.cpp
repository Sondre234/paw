// SPDX-License-Identifier: GPL-3.0-or-later
// Configuration diagnostics: unknown settings, wrong types, ranges, robustness, and the
// schema that docs/config-reference.md is generated from.
#include "paw/config.hpp"
#include "paw/config_schema.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "config_check.hpp"

namespace {
std::string literal(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%g", value);
    return buffer;
}
// "a.b.c" and a value as `return { a = { b = { c = VALUE } } }`; empty for list or map paths.
std::string wrap(const std::string &path, const std::string &value) {
    if (path.find_first_of("[<") != std::string::npos)
        return "";
    std::string open, close, rest = path;
    for (size_t dot; (dot = rest.find('.')) != std::string::npos; rest = rest.substr(dot + 1)) {
        open += rest.substr(0, dot) + "={";
        close += "}";
    }
    return "return {" + open + rest + "=" + value + close + "}";
}
bool accepted(const std::string &source) {
    try {
        (void)paw::parse_config(source);
        return true;
    } catch (const std::exception &) {
        return false;
    }
}
void unknown_settings() {
    rejects("return {shell={pannel_height=40}}", "unknown setting 'pannel_height' in shell");
    rejects("return {shell={pannel_height=40}}", "did you mean 'panel_height'?");
    rejects("return {layot={}}", "did you mean 'layout'?");
    rejects("return {mouse={focus_follow=true}}", "did you mean 'focus_follows'?");
    rejects("return {animations={durration=100}}", "did you mean 'duration'?");
    rejects("return {features={scratchpd=true}}", "unknown feature 'scratchpd'; did you mean "
                                                  "'scratchpad'?");
    rejects("return {bindings={{key='q',action='clsoe'}}}", "did you mean 'close'?");
    rejects("return {bindings={{mods={'Supr'},key='q',action='close'}}}", "did you mean 'Super'?");
    rejects("return {bindings={{button='sid',action='close'}}}", "did you mean 'side'?");
    rejects("return {bindings={{key='q',action='close',comand={'x'}}}}", "did you mean 'command'?");
    rejects("return {modes={resize={{key='Escape',action='mode',mode='default'}}},"
            "bindings={{key='r',action='mode',mode='resiz'}}}",
            "unknown mode 'resiz'; did you mean 'resize'?");
    rejects("return {modes={resize={{key='Escape',action='mode',mode='defualt'}}}}",
            "did you mean 'default'?");
    rejects("return {modes={resize={{key='Left',action='resize_left'}}}}",
            "mode 'resize' has no binding with action = \"mode\" to leave it");
    rejects("return {modes={resize={{button='side',action='mode',mode='default'}}}}",
            "a mode's bindings take a key, not a button");
    rejects("return {modes={resize={{switch='lid',state='close',action='mode',mode='default'}}}}",
            "a mode's bindings take a key, not a switch");
    rejects("return {bindings={{switch='lid',state='close',action='lock',locked=true}}}",
            "locked is only valid with a key");
    rejects("return {modes={default={{key='Escape',action='mode',mode='default'}}}}",
            "mode name 'default' must be");
    rejects("return {bindings={{key='r',action='mode'}}}",
            "the mode action needs mode = \"default\" or the name of one of modes");
    rejects("return {bindings={{key='r',action='close',mode='x'}}}",
            "mode is only valid with screenshot, mode and display_mode");
    rejects("return {windows={rules={{app_id='x',floatng=true}}}}", "did you mean 'floating'?");
    rejects("return {outputs={monitors={X={scal=2}}}}", "did you mean 'scale'?");
    rejects("return {shell={launchers={{name='a',command={'x'},icn='b'}}}}",
            "did you mean 'icon'?");
    rejects("return {shell={panel_margin={tp=1}}}", "did you mean 'top'?");
    // Nothing close: list what is valid.
    rejects("return {shell={zzzzzzzz=1}}", "expected one of: enabled, style, panel_height");
    require(error_of("return {shell={zzzzzzzz=1}}").find("did you mean") == std::string::npos,
            "a far-off name got a suggestion");
}
void locations() {
    auto at = [](const std::string &source, const std::string &fragment) {
        auto message = error_of(source);
        require(message.find("init.lua:" + fragment) != std::string::npos,
                "location " + fragment + " missing from: " + message);
    };
    at("return {\n  shell = {\n    enabled = true,\n    pannel_height = 4,\n  },\n}",
       "4: unknown setting");
    // The same name in another section, and one in a comment, are not mistaken for it.
    at("-- enabled = 1\nreturn {\n  animations = { enabled = true },\n  shell = {\n"
       "    enabled = 'yes',\n  },\n}",
       "5: shell.enabled must be a boolean");
    at("return {\n  layout = {\n    gap = 1,\n    workspaces = 'four',\n  },\n}",
       "4: layout.workspaces");
    at("return {\n  bindings = {\n    { key = 'q', action = 'close' },\n"
       "    { key = 'w', action = 'clsoe' },\n  },\n}",
       "4: unknown action");
    at("return {\n  outputs = {\n    monitors = {\n      [\"DP-1\"] = { scal = 2 },\n"
       "    },\n  },\n}",
       "4: unknown setting");
    at("return {\n  modes = {\n    resize = {\n"
       "      { key = 'Escape', action = 'mode', mode = 'default' },\n"
       "      { key = 'q', action = 'mode', mode = 'resise' },\n    },\n  },\n}",
       "5: unknown mode");
    at("return {\n  modes = {\n    launch = {\n      { key = 'x', action = 'close' },\n    },\n"
       "  },\n}",
       "3: mode 'launch' has no binding");
    // A setting of a nested section is looked for inside it, not at the same name elsewhere.
    at("return {\n  hot_corners = { delay = 100 },\n  shell = {\n    thumbnails = {\n"
       "      delay = 'soon',\n    },\n  },\n}",
       "5: shell.thumbnails.delay must be an integer");
    // A rule's setting is not taken for the one of the same name outside the rules, nor one
    // monitor's for another's.
    at("return {\n  windows = {\n    opacity = 1,\n    rules = {\n"
       "      { app_id = 'x', opacity = 7 },\n    },\n  },\n}",
       "5: windows.rules[1].opacity");
    at("return {\n  outputs = {\n    monitors = {\n      [\"HDMI-A-1\"] = { transform = 1 },\n"
       "      [\"DP-1\"] = { transform = 9 },\n    },\n  },\n}",
       "5: outputs.monitors[\"DP-1\"].transform");
    // Lua's own errors already carry file:line.
    at("return {\n  x = = 1,\n}", "2:");
    at("local a = nil\nreturn { shell = a.b }", "2:");
    // No location is invented for a setting that is not in the file.
    auto message = error_of("return {shell={['panel_' .. 'height']=1}}");
    require(message.find("init.lua:") == std::string::npos &&
                message.find("shell.panel_height") != std::string::npos,
            "unexpected location: " + message);
}
void wrong_types() {
    rejects("return {shell={panel_height='big'}}",
            "shell.panel_height must be an integer, not a string");
    rejects("return {shell={panel_height=1.5}}", "not a non-integer number");
    rejects("return {shell={enabled=1}}", "shell.enabled must be a boolean, not an integer");
    rejects("return {shell={font=3}}", "must be a string, not an integer");
    rejects("return {mouse={speed='fast'}}", "mouse.speed must be a number, not a string");
    rejects("return {shell=true}", "shell must be a table, not a boolean");
    rejects("return {shell={launchers=3}}", "must be a table");
    rejects("return 3", "configuration result must be a table, not an integer");
    rejects("return {features={sticky='no'}}", "features.sticky must be a boolean, not a string");
}
void ranges() {
    rejects("return {shell={panel_height=500}}",
            "shell.panel_height must be between 24 and 100, not 500");
    rejects("return {layout={gap=-3}}", "layout.gap must be between 0 and 100, not -3");
    rejects("return {windows={opacity=2}}", "windows.opacity must be between 0.05 and 1, not 2");
    rejects("return {keyboard={repeat_rate=101}}", "between 0 and 100");
    rejects("return {outputs={monitors={X={transform=9}}}}", "between 0 and 7");
    rejects("return {shell={widgets={network='taskbar'}}}",
            "shell.widgets.network must be true, false, \"bar\" or \"quick\", not \"taskbar\"");
    rejects("return {shell={widgets={volume=2}}}",
            "shell.widgets.volume must be true, false, \"bar\" or \"quick\", not an integer");
    // A table inside another names its own path in errors,
    rejects("return {windows={magnet={distance=999}}}",
            "windows.magnet.distance must be between 0 and 200, not 999");
    rejects("return {windows={shadow={blur=999}}}",
            "windows.shadow.blur must be between 0 and 100, not 999");
    rejects("return {windows={snap={distance=0}}}",
            "windows.snap.distance must be between 1 and 100, not 0");
    rejects("return {shell={panel_margin={top=999}}}",
            "shell.panel_margin.top must be between 0 and 200, not 999");
    // ... and what follows it goes back to naming the one around it.
    rejects("return {windows={magnet={distance=1},drag_strip=999}}",
            "windows.drag_strip must be between 0 and 100, not 999");
    // An entry of a keyed table names its key, one of a list its number.
    rejects("return {outputs={monitors={['DP-1']={transform=9}}}}",
            "outputs.monitors[\"DP-1\"].transform must be between 0 and 7, not 9");
    rejects("return {layout={outputs={['DP-1']={master_ratio=5}}}}",
            "layout.outputs[\"DP-1\"].master_ratio must be between 0.1 and 0.9, not 5");
    rejects("return {windows={rules={{app_id='a'},{app_id='x',opacity=7}}}}",
            "windows.rules[2].opacity must be between 0.05 and 1, not 7");
    rejects("return {bindings={{key='x',action='volume_up',amount=999}}}",
            "bindings[1].amount must be between 1 and 100, not 999");
    rejects("return {bindings={{key='r',action='mode',mode='resize'}},"
            "modes={resize={{key='Escape',action='mode',mode='default'},"
            "{key='Left',action='resize_left',amount=0}}}}",
            "modes.resize[2].amount must be between 1 and 4000, not 0");
}
void schema_matches_parser() {
    auto options = paw::config_options();
    size_t probed = 0, ranged = 0;
    for (const auto &option : options) {
        std::string path = option.path;
        // Every option lives inside one the schema lists (or at the top).
        auto dot = path.find_last_of('.');
        if (dot != std::string::npos) {
            auto parent = path.substr(0, dot);
            bool found = false;
            for (const auto &other : options) {
                std::string p = other.path;
                if (p == parent || p + "[]" == parent)
                    found = true;
            }
            require(found || parent.ends_with("[]") || parent.ends_with("<name>"),
                    path + " has no parent in the schema");
        }
        std::string example = option.example;
        if (example.empty())
            continue;
        auto source = wrap(path, example);
        if (source.empty())
            continue;
        ++probed;
        require(accepted(source), "the parser rejects the documented example for " + path + ": " +
                                      source);
        if (option.min != option.max) {
            ++ranged;
            require(accepted(wrap(path, literal(option.min))), path + " rejects its minimum");
            require(accepted(wrap(path, literal(option.max))), path + " rejects its maximum");
            require(!accepted(wrap(path, literal(option.min - 1))),
                    path + " accepts less than its minimum");
            require(!accepted(wrap(path, literal(option.max + 1))),
                    path + " accepts more than its maximum");
        }
        // A wrong-typed value is refused; a table for a scalar, and a string for a table.
        if (std::string(option.type) == "boolean")
            require(!accepted(wrap(path, "'x'")), path + " accepts a string as a boolean");
    }
    require(probed > 40 && ranged > 15, "the schema probes covered too little: " + std::to_string(probed) + "/" + std::to_string(ranged));
    // Each top-level key the parser knows is in the schema and the other way around.
    for (const char *name : {"version", "extends", "theme", "appearance", "keyboard", "mouse",
                             "touchpad", "layout", "outputs", "windows", "animations", "bindings",
                             "startup", "shell", "xwayland", "screenshots", "features", "overview", "peek", "night_light", "hot_corners", "zoom", "notifications", "osd", "profile", "profiles", "auto_reload", "power", "terminal", "autostart", "session", "gestures", "touch", "tablet", "modes", "idle"}) {
        bool found = false;
        for (const auto *child : paw::config_children(""))
            found = found || std::string(child->path) == name;
        require(found, std::string(name) + " missing from the schema");
    }
    require(paw::config_children("").size() == 36, "the schema has an unknown top-level key");
    // A key that is in the schema is accepted by keys(), however deeply nested.
    require(accepted("return {windows={rules={{app_id='x',sticky=true,focus=false}}}}"),
            "rule keys rejected");
    for (const auto &name : paw::config_action_names())
        (void)paw::parse_action(name);
}
void robustness() {
    auto throws = [](const std::string &source) {
        try {
            (void)paw::parse_config(source, "@bad.lua");
        } catch (const std::exception &) {
            return;
        }
        throw std::runtime_error("accepted: " + source.substr(0, 40));
    };
    throws("return {");
    throws("this is not lua");
    throws("error('boom')");
    throws("error({})");
    throws("error(nil)");
    throws("return nil");
    throws("return 'text'");
    throws("local function f() return f() + 1 end return f()");
    throws("return {shell={panel_height=" + std::string(400, '9') + "}}");
    throws("return {startup={{" + std::string(5000, 'x') + "}}}");
    throws("return {[1]=1}");
    throws("return {bindings={[2]={}}}");
    throws("return {windows={rules={{app_id='(((((('}}}}");
    throws("return {keyboard={layout='nonexistent-layout-zz'}}");
    throws("return {\"a\\0b\"}");
    std::string deep = "return ";
    for (int i = 0; i < 300; ++i)
        deep += "{a=";
    throws(deep + "1" + std::string(300, '}'));
    // A file that is missing or unreadable is an error, not a crash.
    try {
        (void)paw::load_config("/nonexistent/dir/init.lua");
        throw std::runtime_error("a missing file was accepted");
    } catch (const std::runtime_error &error) {
        require(std::string(error.what()).find("cannot open") != std::string::npos,
                std::string("missing file: ") + error.what());
    }
    // After any failure the next parse works: no state survives a rejected configuration.
    require(paw::parse_config("return {layout={gap=3}}").settings.gap_inner == 3,
            "parsing after a failure broke");
}
void reference_in_sync(const std::string &path) {
    auto generated = paw::config_reference_markdown();
    if (std::getenv("PAW_UPDATE_DOCS")) {
        std::ofstream(path, std::ios::binary | std::ios::trunc) << generated;
        std::cout << "wrote " << path << '\n';
        return;
    }
    std::ifstream file(path, std::ios::binary);
    require(bool(file), "cannot read " + path);
    std::stringstream text;
    text << file.rdbuf();
    require(text.str() == generated,
            path + " is out of date; regenerate it with PAW_UPDATE_DOCS=1 ctest -R config");
    for (const auto &option : paw::config_options())
        require(generated.find(std::string("`") + option.path + "`") != std::string::npos,
                std::string("reference lacks ") + option.path);
    for (const auto &name : paw::config_action_names())
        require(generated.find("`" + name + "`") != std::string::npos, "reference lacks " + name);
}
} // namespace

int main(int argc, char **argv) {
    try {
        require(argc == 3, "usage: config_diagnostics_tests EXAMPLE DOCS");
        setenv("PAW_DEFAULT_CONFIG", argv[1], 1); // for extends = "default"
        unknown_settings();
        locations();
        wrong_types();
        ranges();
        schema_matches_parser();
        robustness();
        reference_in_sync(argv[2]);
        std::cout << "Configuration diagnostics and reference passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
