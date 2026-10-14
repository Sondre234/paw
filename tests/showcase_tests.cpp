// SPDX-License-Identifier: GPL-3.0-or-later
/* paw showcase's options, the terminal's command line and where the picture goes
 * (src/cli/showcase.cpp), without a session. */
#include "check.h"
#include "paw/showcase.hpp"

#include <stdexcept>

using namespace paw::showcase;
using Words = std::vector<std::string>;

static Options parsed(Words words) {
    words.insert(words.begin(), {"paw", "showcase"});
    std::vector<char *> argv;
    for (auto &word : words)
        argv.push_back(word.data());
    return parse(static_cast<int>(argv.size()), argv.data());
}

static bool refused(const Words &words, const std::string &part) {
    try {
        parsed(words);
    } catch (const std::runtime_error &error) {
        return std::string(error.what()).find(part) != std::string::npos;
    }
    return false;
}

int main() {
    auto plain = parsed({});
    CHECK(plain.delay == 1 && !plain.keep && plain.output.empty() && plain.file.empty() &&
          plain.terminal.empty());
    auto all = parsed({"--delay", "2.5", "--keep", "--output", "DP-1", "-o", "shot.png",
                       "--terminal", "foot  --app-id showcase"});
    NEAR(all.delay, 2.5, 1e-9);
    CHECK(all.keep && all.output == "DP-1" && all.file == "shot.png");
    CHECK(all.terminal == (Words{"foot", "--app-id", "showcase"}), "the words of --terminal");
    CHECK(parsed({"--delay", "0"}).delay == 0);
    CHECK(refused({"--delay", "soon"}, "--delay takes seconds"));
    CHECK(refused({"--delay", "-1"}, "--delay takes seconds"));
    CHECK(refused({"--delay", "601"}, "--delay takes seconds"));
    CHECK(refused({"--delay"}, "usage: paw showcase"), "an option without its value");
    CHECK(refused({"--terminal", " "}, "--terminal takes a command"));
    CHECK(refused({"--bogus"}, "usage: paw showcase"));

    Words program = {"/bin/sh", "-c", "script", "paw", "pipe"};
    auto with = [&](Words terminal) { return terminal_command(terminal, program); };
    CHECK(with({"kitty"}) == (Words{"kitty", "/bin/sh", "-c", "script", "paw", "pipe"}));
    CHECK(with({"/usr/bin/foot", "--server"})[2] == "/bin/sh", "foot runs what follows it");
    CHECK(with({"footclient"})[1] == "/bin/sh");
    CHECK(with({"alacritty"}) ==
          (Words{"alacritty", "-e", "/bin/sh", "-c", "script", "paw", "pipe"}));
    CHECK(with({"ghostty"})[1] == "-e" && with({"xterm"})[1] == "-e" &&
          with({"konsole"})[1] == "-e");
    CHECK(with({"gnome-terminal"})[1] == "--" && with({"kgx"})[1] == "--");
    CHECK(with({"wezterm"}) ==
          (Words{"wezterm", "start", "--", "/bin/sh", "-c", "script", "paw", "pipe"}));
    CHECK(with({"wezterm", "start", "--always-new-process"})[3] == "--",
          "WezTerm's own subcommand is kept");
    CHECK(with({"/tmp/stand-in", "--probe", "x"})[3] == "-e", "an unknown terminal takes -e");

    CHECK(pictures_directory("/shots", "", "/home/me") == "/shots");
    std::string dirs = "# comment\nXDG_DESKTOP_DIR=\"$HOME/Desktop\"\n"
                       "XDG_PICTURES_DIR=\"$HOME/Bilder\"\n";
    CHECK(pictures_directory("relative", dirs, "/home/me") == "/home/me/Bilder",
          "a relative $XDG_PICTURES_DIR is ignored");
    CHECK(pictures_directory(nullptr, "XDG_PICTURES_DIR=\"/data/pics\"\n", "/home/me") ==
          "/data/pics");
    CHECK(pictures_directory(nullptr, "XDG_PICTURES_DIR=\"pics\"\n", "/home/me") ==
          "/home/me/Pictures");
    CHECK(pictures_directory(nullptr, "", "/home/me") == "/home/me/Pictures");

    std::tm time{};
    time.tm_year = 2026 - 1900;
    time.tm_mon = 9;
    time.tm_mday = 14;
    time.tm_hour = 15;
    time.tm_min = 3;
    time.tm_sec = 7;
    CHECK(file_name(time) == "paw-showcase-2026-10-14-150307.png", "%s", file_name(time).c_str());
    return failures != 0;
}
