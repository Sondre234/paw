// SPDX-License-Identifier: GPL-3.0-or-later
// Writing a setting back into the configuration's Lua source: edit_setting and save_setting.
#include "paw/config_edit.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

#include "config_check.hpp"

namespace {
const std::vector<std::string> position = {"shell", "panel_position"};

// `source` with shell.panel_position set to `value`, "top" or "bottom", where an edit takes when
// the configuration it gives has the panel there with the profile `chosen` in use.
std::string set_position(const std::string &source, const std::string &value,
                         const std::string &chosen = "") {
    return paw::edit_setting(source, position, '"' + value + '"', [&](const std::string &text) {
        try {
            return (paw::parse_config(text, "config", {}, chosen).shell.panel_edge ==
                    paw::PanelEdge::Top) == (value == "top");
        } catch (const std::exception &) {
            return false;
        }
    });
}
void expect(const std::string &source, const std::string &value, const std::string &edited,
            const std::string &chosen = "") {
    auto result = set_position(source, value, chosen);
    require(result == edited, "edited as\n" + result + "\ninstead of\n" + edited);
}
// The message of the error edit_setting throws, which fails the test when it does not.
std::string refusal(const std::string &source, const std::vector<std::string> &trail,
                    const std::function<bool(const std::string &)> &takes) {
    try {
        paw::edit_setting(source, trail, "\"top\"", takes);
    } catch (const std::runtime_error &error) {
        return error.what();
    }
    throw std::runtime_error("an edit took that should not have: " + source);
}
std::string read(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}
void write(const std::filesystem::path &path, const std::string &text) {
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
}
bool on_top(const paw::Config &config) { return config.shell.panel_edge == paw::PanelEdge::Top; }
} // namespace

int main(int argc, char **argv) {
    try {
        require(argc == 2, "example config path required");
        auto directory = std::filesystem::temp_directory_path() /
                         ("paw-config-edit-" + std::to_string(getpid()));
        std::filesystem::remove_all(directory);
        std::filesystem::create_directories(directory / "state");
        // Not the developer's saved profile.
        setenv("XDG_STATE_HOME", (directory / "state").c_str(), 1);

        // The value the file gives is replaced, and nothing else changes.
        expect("return {\n"
               "    shell = {\n"
               "        panel_height = 40,\n"
               "        panel_position = \"bottom\", -- or \"top\"\n"
               "    },\n"
               "}\n",
               "top",
               "return {\n"
               "    shell = {\n"
               "        panel_height = 40,\n"
               "        panel_position = \"top\", -- or \"top\"\n"
               "    },\n"
               "}\n");
        // While a profile is in use, its value is the one in force, and changes alone.
        const std::string profiles = "return {\n"
                                     "    profile = \"washi\",\n"
                                     "    shell = { panel_position = 'bottom' },\n"
                                     "    profiles = {\n"
                                     "        washi = { shell = { panel_height = 46, panel_position = 'bottom', panel_radius = 12 } },\n"
                                     "        sumi = { shell = { panel_position = 'bottom' } },\n"
                                     "    },\n"
                                     "}\n";
        expect(profiles, "top",
               "return {\n"
               "    profile = \"washi\",\n"
               "    shell = { panel_position = 'bottom' },\n"
               "    profiles = {\n"
               "        washi = { shell = { panel_height = 46, panel_position = \"top\", panel_radius = 12 } },\n"
               "        sumi = { shell = { panel_position = 'bottom' } },\n"
               "    },\n"
               "}\n");
        // The profile chosen from the shell wins over `profile`.
        expect(profiles, "top",
               "return {\n"
               "    profile = \"washi\",\n"
               "    shell = { panel_position = 'bottom' },\n"
               "    profiles = {\n"
               "        washi = { shell = { panel_height = 46, panel_position = 'bottom', panel_radius = 12 } },\n"
               "        sumi = { shell = { panel_position = \"top\" } },\n"
               "    },\n"
               "}\n",
               "sumi");
        // Already there, nothing changes: not even a profile not in use, which would take too.
        expect(profiles, "bottom", profiles);
        // A profile not in use is left alone: the file's own shell table gets the setting, on a
        // line of its own, indented as the field below it.
        expect("return {\n"
               "    shell = {\n"
               "        panel_height = 40,\n"
               "    },\n"
               "    profiles = { washi = { shell = { panel_position = 'bottom' } } },\n"
               "}\n",
               "top",
               "return {\n"
               "    shell = {\n"
               "        panel_position = \"top\",\n"
               "        panel_height = 40,\n"
               "    },\n"
               "    profiles = { washi = { shell = { panel_position = 'bottom' } } },\n"
               "}\n");
        // After a comment ending the brace's line, before one on a line of its own; tabs kept.
        expect("return {\n"
               "\tshell = { -- the panel\n"
               "\t\t-- how tall\n"
               "\t\tpanel_height = 40,\n"
               "\t},\n"
               "}\n",
               "top",
               "return {\n"
               "\tshell = { -- the panel\n"
               "\t\tpanel_position = \"top\",\n"
               "\t\t-- how tall\n"
               "\t\tpanel_height = 40,\n"
               "\t},\n"
               "}\n");
        // An empty table across lines: one step in from its brace.
        expect("return {\n  shell = {\n  },\n}\n", "top",
               "return {\n  shell = {\n      panel_position = \"top\",\n  },\n}\n");
        // A table on one line gets it beside its brace.
        expect("return { shell = { panel_height = 40 } }", "top",
               "return { shell = { panel_position = \"top\", panel_height = 40 } }");
        expect("return {shell={panel_height=40}}", "top",
               "return {shell={panel_position = \"top\", panel_height=40}}");
        expect("return { shell = {} }", "top", "return { shell = { panel_position = \"top\" } }");
        expect("return {shell={ }}", "top", "return {shell={ panel_position = \"top\" }}");
        // No shell table: one goes first in the table the file returns.
        expect("return {\n    layout = { gap = 4 },\n}\n", "top",
               "return {\n    shell = { panel_position = \"top\" },\n    layout = { gap = 4 },\n}\n");
        expect("return {}", "top", "return { shell = { panel_position = \"top\" } }");
        // Keys written as strings, and statements on a table returned by name.
        expect("return { [\"shell\"] = { ['panel_position'] = 'bottom' } }", "top",
               "return { [\"shell\"] = { ['panel_position'] = \"top\" } }");
        expect("local config = { shell = {} }\n"
               "config.shell.panel_position = \"bottom\"\n"
               "return config\n",
               "top",
               "local config = { shell = {} }\n"
               "config.shell.panel_position = \"top\"\n"
               "return config\n");
        // What comments and strings say is not a setting.
        expect("-- shell = { panel_position = \"bottom\" }\n"
               "--[==[\n"
               "return { shell = { panel_position = \"bottom\" } }\n"
               "]==]\n"
               "local note = \"shell = { panel_position = 'bottom' }\"\n"
               "local long = [[ shell = { panel_position = \"bottom\" } ]]\n"
               "return {\n"
               "    shell = { panel_position = \"bottom\" }, -- panel_position = \"bottom\"\n"
               "}\n",
               "top",
               "-- shell = { panel_position = \"bottom\" }\n"
               "--[==[\n"
               "return { shell = { panel_position = \"bottom\" } }\n"
               "]==]\n"
               "local note = \"shell = { panel_position = 'bottom' }\"\n"
               "local long = [[ shell = { panel_position = \"bottom\" } ]]\n"
               "return {\n"
               "    shell = { panel_position = \"top\" }, -- panel_position = \"bottom\"\n"
               "}\n");
        // Line ends stay as the file has them.
        expect("return {\r\n    shell = {\r\n        panel_height = 40,\r\n    },\r\n}\r\n", "top",
               "return {\r\n    shell = {\r\n        panel_position = \"top\",\r\n"
               "        panel_height = 40,\r\n    },\r\n}\r\n");

        // Other kinds of values, and settings in a section's own tables.
        auto set = [](const std::string &source, const std::vector<std::string> &trail,
                      const std::string &literal, auto check) {
            return paw::edit_setting(source, trail, literal, [&](const std::string &text) {
                try {
                    return check(paw::parse_config(text));
                } catch (const std::exception &) {
                    return false;
                }
            });
        };
        auto height = [](const paw::Config &config) { return config.shell.panel_height == 46; };
        require(set("return { shell = { panel_height = 40 } }", {"shell", "panel_height"}, "46",
                    height) == "return { shell = { panel_height = 46 } }",
                "a number not replaced");
        auto size = [](const paw::Config &config) { return config.shell.thumbnails.size == 300; };
        const std::vector<std::string> thumbnails = {"shell", "thumbnails", "size"};
        require(set("return { shell = { thumbnails = { delay = 100 } } }", thumbnails, "300", size) ==
                    "return { shell = { thumbnails = { size = 300, delay = 100 } } }",
                "a nested setting not added to its own table");
        require(set("return { shell = { panel_height = 40 } }", thumbnails, "300", size) ==
                    "return { shell = { thumbnails = { size = 300 }, panel_height = 40 } }",
                "a nested setting's table not added to its section");
        require(set("return {}", thumbnails, "300", size) ==
                    "return { shell = { thumbnails = { size = 300 } } }",
                "a nested setting's section not added");

        // A value worked out by an expression is not replaced, and when nothing takes the
        // setting is named.
        auto never = [](const std::string &) { return false; };
        auto message = refusal("return { shell = { panel_position = 'bot' .. 'tom' } }", position, never);
        require(message.find("shell.panel_position") != std::string::npos, "refusal: " + message);
        bool replaced = false;
        (void)paw::edit_setting("local p = 'bottom'\nreturn { shell = { panel_position = p .. '' } }",
                                position, "\"top\"", [&](const std::string &text) {
                                    replaced = replaced || text.find("\"top\" .. ''") != std::string::npos;
                                    return true;
                                });
        require(!replaced, "part of an expression was replaced");

        // A file extending the default configuration gets the setting of its own.
        auto shipped = directory / "shipped.lua";
        write(shipped, "return { shell = { panel_position = 'top' } }");
        setenv("PAW_DEFAULT_CONFIG", shipped.c_str(), 1);
        expect("return { extends = \"default\" }", "bottom",
               "return { shell = { panel_position = \"bottom\" }, extends = \"default\" }");

        // The example: washi sets its own; the light profile leaves it to the file's shell table.
        setenv("PAW_DEFAULT_CONFIG", argv[1], 1);
        const auto example = read(argv[1]);
        const std::string washi = "panel_height = 46, panel_position = \"top\"";
        auto edited = example;
        edited.replace(edited.find(washi), washi.size(), "panel_height = 46, panel_position = \"bottom\"");
        require(set_position(example, "bottom", "washi") == edited, "example's washi not edited");
        edited = example;
        const std::string base = "\n    shell = {\n";
        edited.insert(edited.find(base) + base.size(), "        panel_position = \"top\",\n");
        require(edited.find(base) == example.find(base) && example.find(base, example.find(base) + 1) ==
                    std::string::npos,
                "the example's shell table moved");
        require(set_position(example, "top", "light") == edited, "example's shell table not edited");

        // A monitor's own edge, under a key that is no Lua name, is written as ["DP-1"].
        const std::vector<std::string> monitor = {"shell", "panel_positions", "DP-1"};
        auto monitor_at = [&](const std::string &source, paw::PanelEdge edge, const char *literal) {
            return paw::edit_setting(source, monitor, literal, [&](const std::string &text) {
                try {
                    return paw::parse_config(text, "config", {}, "").shell.edge_of("DP-1") == edge;
                } catch (const std::exception &) {
                    return false;
                }
            });
        };
        const std::string shell_only = "return {\n    shell = {\n        panel_height = 46,\n    },\n}\n";
        require(monitor_at(shell_only, paw::PanelEdge::Left, "\"left\"") ==
                    "return {\n    shell = {\n        panel_positions = { [\"DP-1\"] = \"left\" },\n"
                    "        panel_height = 46,\n    },\n}\n",
                "a monitor's edge not added to the shell table");
        require(monitor_at("return { shell = { panel_positions = { [\"HDMI-A-1\"] = 'top' } } }",
                           paw::PanelEdge::Left, "\"left\"") ==
                    "return { shell = { panel_positions = { [\"DP-1\"] = \"left\", [\"HDMI-A-1\"] = 'top' } } }",
                "a monitor's edge not added beside another's");
        require(monitor_at("return { shell = { panel_positions = { ['DP-1'] = 'left' } } }",
                           paw::PanelEdge::Top, "\"top\"") ==
                    "return { shell = { panel_positions = { ['DP-1'] = \"top\" } } }",
                "a monitor's edge not changed in place");
        require(monitor_at("return {\n    layout = {},\n}\n", paw::PanelEdge::Left, "\"left\"") ==
                    "return {\n    shell = { panel_positions = { [\"DP-1\"] = \"left\" } },\n"
                    "    layout = {},\n}\n",
                "a monitor's edge not added with a shell table");

        // save_setting edits the file as load_config reads it, the saved profile in use.
        auto config = directory / "init.lua";
        write(config, profiles);
        paw::save_profile("sumi");
        paw::save_setting(config, position, "\"top\"", on_top);
        require(read(config) == set_position(profiles, "top", "sumi") && on_top(paw::load_config(config)),
                "save_setting did not edit the saved profile's setting");
        // The taskbar resized by its inner edge has its height written the same way: into the
        // profile in use when that sets it, else into the file's shell table, which gains it.
        const std::vector<std::string> panel_height = {"shell", "panel_height"};
        auto tall = [](const paw::Config &config) { return config.shell.panel_height == 64; };
        write(config, profiles);
        paw::save_profile("washi");
        paw::save_setting(config, panel_height, "64", tall);
        auto resized = profiles;
        resized.replace(resized.find("panel_height = 46"), 17, "panel_height = 64");
        require(read(config) == resized && tall(paw::load_config(config)),
                "a resized taskbar's height not written into the profile in use:\n" + read(config));
        write(config, profiles);
        paw::save_profile("sumi");
        paw::save_setting(config, panel_height, "64", tall);
        resized = profiles;
        resized.replace(resized.find("shell = { panel_position"), 9, "shell = { panel_height = 64,");
        require(read(config) == resized && tall(paw::load_config(config)),
                "a resized taskbar's height not added to the shell table:\n" + read(config));
        // A link's target is edited, keeping its permissions, and the link stays; no file is left
        // beside it.
        std::filesystem::create_directories(directory / "real");
        std::filesystem::create_directories(directory / "linked");
        auto target = directory / "real/init.lua", link = directory / "linked/init.lua";
        write(target, "return {\n    shell = { panel_position = 'bottom' },\n}\n");
        chmod(target.c_str(), 0640);
        std::filesystem::create_symlink("../real/init.lua", link);
        paw::save_setting(link, position, "\"top\"", on_top);
        struct stat kept {};
        stat(target.c_str(), &kept);
        require(std::filesystem::is_symlink(link) &&
                    read(target) == "return {\n    shell = { panel_position = \"top\" },\n}\n" &&
                    (kept.st_mode & 07777) == 0640,
                "a linked configuration not edited in place");
        size_t files = 0;
        for ([[maybe_unused]] const auto &entry : std::filesystem::directory_iterator(directory / "real"))
            ++files;
        require(files == 1, "a temporary file was left beside the configuration");
        // A file that gives the value already is not written.
        struct stat before {}, after {};
        stat(target.c_str(), &before);
        paw::save_setting(link, position, "\"top\"", on_top);
        stat(target.c_str(), &after);
        require(before.st_ino == after.st_ino, "a configuration giving the value already was rewritten");
        // Nothing taking, or an error in the file, leaves it as it was.
        const std::string fixed = "return { shell = { panel_position = 'bottom' } }";
        write(config, fixed);
        try {
            paw::save_setting(config, position, "\"top\"", [](const paw::Config &) { return false; });
            throw std::logic_error("save_setting saved an edit that did not take");
        } catch (const std::runtime_error &error) {
            require(std::string(error.what()).find("shell.panel_position") != std::string::npos &&
                        read(config) == fixed,
                    std::string("refused save: ") + error.what());
        }
        const std::string broken = "return { shell = { panel_height = 'tall' } }";
        write(config, broken);
        try {
            paw::save_setting(config, position, "\"top\"", on_top);
            throw std::logic_error("save_setting edited a configuration with an error");
        } catch (const std::runtime_error &error) {
            require(std::string(error.what()).find("has an error") != std::string::npos &&
                        read(config) == broken,
                    std::string("broken configuration: ") + error.what());
        }
        std::filesystem::remove_all(directory);
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "config_edit tests passed\n";
    return 0;
}
