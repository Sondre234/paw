// SPDX-License-Identifier: GPL-3.0-or-later
// Appearance profiles: `profiles`, `profile`, and the saved choice.
#include "paw/config.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

#include "config_check.hpp"

static void expect(const std::string &source, const std::string &part) {
    auto message = error_of(source);
    require(message.find(part) != std::string::npos,
            "expected '" + part + "' in: " + message);
}
static float red(const float (&color)[4]) { return color[0]; }

int main() {
    try {
        const std::string profiles = R"(
            shell = { accent = "#111111", panel_height = 40 },
            windows = { border_width = 2 },
            profiles = {
                light = {
                    appearance = { background = "#ffffff" },
                    shell = { accent = "#222222", panel_color = "#f0f0f0" },
                    windows = { border_color = "#000000" },
                },
                ["high-contrast"] = { shell = { text_color = "#ffffff" } },
            },
        )";
        // No profile: the file's own settings, and the names for a picker.
        auto plain = paw::parse_config("return {" + profiles + "}");
        require(plain.profile.empty(), "a profile was applied unasked");
        require(plain.profiles == std::vector<std::string>{"high-contrast", "light"},
                "profile names not sorted");
        require(plain.shell.accent == "#111111" && plain.shell.panel_color == "#151e2c",
                "settings changed without a profile");
        // `profile` starts with one; its settings replace the file's, the rest stays.
        auto light = paw::parse_config("return {profile='light'," + profiles + "}");
        require(light.profile == "light", "starting profile not used");
        require(light.shell.accent == "#222222" && light.shell.panel_color == "#f0f0f0" &&
                    light.shell.panel_height == 40,
                "profile shell settings not laid over the file's");
        require(red(light.settings.background) == 1.0F, "profile background not applied");
        require(light.settings.border_width == 2, "unset profile setting replaced the file's");
        // A chosen profile wins over `profile`; an unknown choice falls back to it.
        auto chosen = paw::parse_config("return {profile='light'," + profiles + "}", "config",
                                           {}, "high-contrast");
        require(chosen.profile == "high-contrast" && chosen.shell.text_color == "#ffffff" &&
                    chosen.shell.accent == "#111111",
                "chosen profile not applied");
        auto stale = paw::parse_config("return {profile='light'," + profiles + "}", "config",
                                          {}, "gone");
        require(stale.profile == "light", "unknown saved profile not ignored");
        // Mistakes, including in a profile not in use, are found as the file loads.
        expect("return {profile='dark'}", "needs a profiles table");
        expect("return {profile='dak', profiles={dark={}}}", "did you mean 'dark'");
        expect("return {profiles={dark={layout={gap=3}}}}", "unknown setting 'layout'");
        expect("return {profiles={['a b']={}}}", "must be 1 to 32 letters");
        expect("return {profiles={next={}}}", "not next or prev");
        expect("return {profiles={dark=3}}", "must be a table");
        expect("return {profiles={'x'}}", "keyed by name");
        expect("return {\n profiles = {\n  dark = {\n   shell = { accent = 'blue' },\n  },\n },\n}",
               "init.lua:4: profile 'dark'");
        expect("return {profiles={dark={shell={panel_height=500}}}}",
               "profile 'dark': shell.panel_height must be between");

        // load_config takes the saved choice from $XDG_STATE_HOME/paw/profile.
        auto directory = std::filesystem::temp_directory_path() /
                         ("paw-profiles-" + std::to_string(getpid()));
        std::filesystem::create_directories(directory);
        setenv("XDG_STATE_HOME", directory.c_str(), 1);
        auto config = directory / "init.lua";
        std::ofstream(config) << "return {profile='light'," << profiles << "}";
        require(paw::load_config(config).profile == "light", "starting profile lost");
        paw::save_profile("high-contrast");
        require(paw::profile_state_path() == directory / "paw/profile", "state path");
        require(paw::saved_profile() == "high-contrast", "saved profile not read back");
        require(paw::load_config(config).profile == "high-contrast",
                "saved profile not used");
        // A file that extends the default configuration gets its profiles, unless it has its own;
        // then it offers those alone.
        setenv("PAW_DEFAULT_CONFIG", config.c_str(), 1);
        auto extending = directory / "extending.lua";
        std::ofstream(extending) << "return {extends='default'}";
        auto extended = paw::load_config(extending);
        require(extended.profiles.size() == 2 && extended.profile == "high-contrast" &&
                    extended.shell.text_color == "#ffffff",
                "profiles of the default configuration not offered");
        std::ofstream(extending) << "return {extends='default', profile='mine', profiles={mine={}}}";
        auto own = paw::load_config(extending);
        require(own.profiles == std::vector<std::string>{"mine"} && own.profile == "mine",
                "the default configuration's profiles joined a file's own");
        std::ofstream(extending) << "return {extends='default', profiles={mine={}}}";
        require(paw::load_config(extending).profile.empty(),
                "the default configuration's profile applied to a file with its own profiles");
        std::filesystem::remove_all(directory);
        std::cout << "Profile configuration tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
