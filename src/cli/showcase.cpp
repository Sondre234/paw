// SPDX-License-Identifier: GPL-3.0-or-later
/* `paw showcase`: a screenshot to show the desktop off. It saves the arrangement of the windows as
 * a session, opens the terminal running `paw fetch`, waits for it to settle, takes the monitor
 * with grim, then closes the terminal again and restores the session; on a failure or Ctrl+C
 * too. */
#include "paw/showcase.hpp"

#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace paw::showcase {

namespace {

const char *const usage = "usage: paw showcase [--delay SECONDS] [--keep] [--output NAME] "
                          "[-o FILE] [--terminal COMMAND]";

} // namespace

Options parse(int argc, char **argv) {
    Options options;
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        bool more = i + 1 < argc;
        if (arg == "--keep")
            options.keep = true;
        else if (arg == "--delay" && more) {
            char *end = nullptr;
            options.delay = std::strtod(argv[++i], &end);
            if (end == argv[i] || *end || !(options.delay >= 0 && options.delay <= 600))
                throw std::runtime_error("--delay takes seconds from 0 to 600");
        } else if (arg == "--output" && more)
            options.output = argv[++i];
        else if (arg == "-o" && more)
            options.file = argv[++i];
        else if (arg == "--terminal" && more) {
            std::istringstream words(argv[++i]);
            options.terminal.clear();
            for (std::string word; words >> word;)
                options.terminal.push_back(word);
            if (options.terminal.empty())
                throw std::runtime_error("--terminal takes a command");
        } else
            throw std::runtime_error(usage);
    }
    return options;
}

std::vector<std::string> terminal_command(const std::vector<std::string> &terminal,
                                          const std::vector<std::string> &program) {
    auto command = terminal;
    auto name = terminal.empty() ? "" : std::filesystem::path(terminal[0]).filename().string();
    if (name == "kitty" || name == "foot" || name == "footclient") {
        // These run what follows their options.
    } else if (name == "wezterm") {
        if (terminal.size() == 1)
            command.push_back("start");
        command.push_back("--");
    } else if (name == "gnome-terminal" || name == "kgx" || name == "ptyxis" ||
               name == "gnome-console")
        command.push_back("--");
    else
        command.push_back("-e");
    command.insert(command.end(), program.begin(), program.end());
    return command;
}

std::filesystem::path pictures_directory(const char *environment, const std::string &user_dirs,
                                         const std::filesystem::path &home) {
    if (environment && *environment == '/')
        return environment;
    std::istringstream lines(user_dirs);
    for (std::string line; std::getline(lines, line);) {
        if (!line.starts_with("XDG_PICTURES_DIR=\"") || !line.ends_with('"'))
            continue;
        auto value = line.substr(18, line.size() - 19);
        if (value.starts_with("$HOME/"))
            return home / value.substr(6);
        if (value.starts_with('/'))
            return value;
    }
    return home / "Pictures";
}

std::string file_name(const std::tm &time) {
    char name[64];
    std::strftime(name, sizeof(name), "paw-showcase-%Y-%m-%d-%H%M%S.png", &time);
    return name;
}

} // namespace paw::showcase
