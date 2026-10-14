// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* What `paw showcase` (src/cli/showcase.cpp) decides without a session: its options, the command
 * line that opens a terminal running a program, and where the picture goes. */
#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

namespace paw::showcase {

struct Options {
    double delay = 1;                  // seconds between the window settling and the picture
    bool keep = false;                 // leave the window and the arrangement as they are
    std::string output;                // the monitor to take; "" for the focused one
    std::filesystem::path file;        // where the picture goes; "" for the pictures directory
    std::vector<std::string> terminal; // the terminal to open; empty for the configured one
};

// The options in argv[2] on; throws std::runtime_error with the usage for anything else.
Options parse(int argc, char **argv);
// The command line that opens `terminal` running `program`: after -e for most terminals, as it is
// for kitty and foot, after -- for GNOME's, and after start -- for WezTerm.
std::vector<std::string> terminal_command(const std::vector<std::string> &terminal,
                                          const std::vector<std::string> &program);
// Where pictures go: `environment` ($XDG_PICTURES_DIR) when it is absolute, else the
// XDG_PICTURES_DIR that `user_dirs` (the text of user-dirs.dirs) names, else ~/Pictures.
std::filesystem::path pictures_directory(const char *environment, const std::string &user_dirs,
                                         const std::filesystem::path &home);
// The picture's name for a time: paw-showcase-2026-10-14-153000.png.
std::string file_name(const std::tm &time);

} // namespace paw::showcase
