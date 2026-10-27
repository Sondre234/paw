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

        // profile_schedule: two of the profiles, at night light's times unless it has its own.
        auto unscheduled = paw::parse_config("return {" + profiles + "}");
        require(!unscheduled.settings.profile_schedule.day[0] &&
                    !unscheduled.settings.profile_schedule.night[0],
                "a schedule without profile_schedule");
        auto scheduled = paw::parse_config(
            "return {profile_schedule={day='light',night='high-contrast'}," + profiles + "}");
        const auto &schedule = scheduled.settings.profile_schedule;
        require(std::string(schedule.day) == "light" &&
                    std::string(schedule.night) == "high-contrast" && !schedule.own_times,
                "profile_schedule not read");
        auto timed = paw::parse_config("return {profile_schedule={day='light',night='light',"
                                       "sunrise='06:30',sunset='21:15'}," +
                                       profiles + "}");
        require(timed.settings.profile_schedule.own_times &&
                    timed.settings.profile_schedule.sunrise == 390 &&
                    timed.settings.profile_schedule.sunset == 1275,
                "profile_schedule's own times not read");
        // A profile picked by hand changes nothing of the schedule.
        auto picked = paw::parse_config(
            "return {profile_schedule={day='light',night='high-contrast'}," + profiles + "}",
            "config", {}, "high-contrast");
        require(picked.profile == "high-contrast" &&
                    std::string(picked.settings.profile_schedule.day) == "light",
                "the schedule changed with the profile");
        expect("return {profile_schedule={day='light'}," + profiles + "}",
               "needs a day and a night profile");
        expect("return {profile_schedule={day='light',night='hight-contrast'}," + profiles + "}",
               "unknown profile 'hight-contrast' in profile_schedule.night; did you mean "
               "'high-contrast'?");
        expect("return {profile_schedule={day='light',night='dark'}}", "needs a profiles table");
        expect("return {profile_schedule={day='light',night='light',sunrise='07:00'}," +
                   profiles + "}",
               "sunrise and sunset go together");
        expect("return {profile_schedule={day='light',night='light',sunrise='7',sunset='20:00'}," +
                   profiles + "}",
               "profile_schedule.sunrise must be a time");
        expect("return {profile_schedule={day='light',night='light',sunrise='08:00',"
               "sunset='08:00'}," +
                   profiles + "}",
               "must differ");
        expect("return {profile_schedule={day='light',night='light',dusk='20:00'}," + profiles +
                   "}",
               "unknown setting 'dusk' in profile_schedule");
        expect("return {\n profile_schedule = {\n  day = 'light',\n  night = 'nite',\n },\n" +
                   profiles + "}",
               "init.lua:4: unknown profile 'nite'");

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
        // Wallpapers and start buttons the default configuration names relative to itself stay
        // beside it.
        auto shipped = directory / "shipped";
        std::filesystem::create_directories(shipped);
        std::ofstream(shipped / "init.lua")
            << "return {shell={wallpaper='plain.png'}, profile='night', profiles={"
               "night={shell={wallpaper='rices/night/wallpaper.png',"
               "start_icon='rices/night/cat.svg',bar_icons='rices/night/bar',"
               "wallpaper_cycle={folder='rices/night/sky'},"
               "monitor_wallpapers={['DP-1']='rices/night/left.png',['DP-2']='/srv/right.png'},"
               "tiling_bar={workspace_icons={'rices/night/ws1.png','/srv/ws2.png'}}}},"
               "home={shell={wallpaper_cycle={folder='~/Pictures/sky'}}},"
               "fixed={shell={wallpaper='/srv/fixed.png'}}, bare={}}}";
        setenv("PAW_DEFAULT_CONFIG", (shipped / "init.lua").c_str(), 1);
        paw::save_profile("night");
        std::ofstream(extending) << "return {extends='default'}";
        require(paw::load_config(extending).shell.wallpaper ==
                    (shipped / "rices/night/wallpaper.png").string(),
                "a shipped profile's wallpaper not found beside the default configuration");
        require(paw::load_config(extending).shell.start_icon ==
                    (shipped / "rices/night/cat.svg").string(),
                "a shipped profile's start button not found beside the default configuration");
        require(paw::load_config(extending).shell.bar_icons == (shipped / "rices/night/bar").string(),
                "a shipped profile's bar icons not found beside the default configuration");
        require(paw::load_config(extending).shell.wallpaper_cycle.folder ==
                    (shipped / "rices/night/sky").string(),
                "a shipped profile's slideshow folder not found beside the default configuration");
        require(paw::load_config(extending).shell.monitor_wallpapers ==
                    std::map<std::string, std::string>{
                        {"DP-1", (shipped / "rices/night/left.png").string()},
                        {"DP-2", "/srv/right.png"}},
                "a shipped profile's monitor wallpapers not found beside the default configuration");
        require(paw::load_config(extending).shell.tiling_bar.workspace_icons ==
                    std::vector<std::string>{(shipped / "rices/night/ws1.png").string(),
                                             "/srv/ws2.png"},
                "a shipped profile's workspace pictures not found beside the default configuration");
        paw::save_profile("home");
        require(paw::load_config(extending).shell.wallpaper_cycle.folder == "~/Pictures/sky",
                "a slideshow folder in the home folder moved");
        paw::save_profile("fixed");
        require(paw::load_config(extending).shell.wallpaper == "/srv/fixed.png",
                "an absolute wallpaper moved");
        paw::save_profile("bare");
        require(paw::load_config(extending).shell.wallpaper == (shipped / "plain.png").string(),
                "the default configuration's own wallpaper not found beside it");
        std::ofstream(extending)
            << "return {extends='default', shell={wallpaper='mine.png', start_icon='mine.svg'}}";
        require(paw::load_config(extending).shell.wallpaper == "mine.png" &&
                    paw::load_config(extending).shell.start_icon == "mine.svg",
                "the extending configuration's own wallpaper or start button moved");
        std::filesystem::remove_all(directory);
        std::cout << "Profile configuration tests passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
