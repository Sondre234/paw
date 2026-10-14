// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* The subcommands `paw NAME ...` runs besides starting a compositor (src/cli/), and what they
 * share: a request to the running session's control socket, its reply split into rows, and
 * where paw keeps its state. Each subcommand is `int NAME(int argc, char **argv)`, argv[1]
 * being its own name; it returns the exit status and throws std::runtime_error for a usage
 * error or a failure, which main prints as "paw: WHAT". */
#include <filesystem>
#include <string>
#include <vector>

namespace paw::cli {

struct Reply {
    bool ok = false;
    std::string body; // what follows the "ok" or "error" line
};

// Sends one request line (no newline) to the control socket $PAW_SOCKET names and returns the
// reply. Throws if the session cannot be reached.
Reply request(const std::string &line);
// The tab-separated fields of each line of a reply's body.
std::vector<std::vector<std::string>> rows(const std::string &body);
// `get NAME [ARGUMENTS]` as rows; throws with the compositor's error if it refuses.
std::vector<std::vector<std::string>> query(const std::string &name);
// $XDG_STATE_HOME/paw, else ~/.local/state/paw; empty without either.
std::filesystem::path state_directory();

// `paw doctor`: looks the session over and prints what is wrong with it and how to fix that.
int doctor(int argc, char **argv);
// `paw fetch`: a paw print and what paw knows of the session (src/cli/fetch.cpp).
int fetch(int argc, char **argv);
// The configuration paw starts with when given none: $XDG_CONFIG_HOME/paw/init.lua (or
// ~/.config's) where it exists, else the shipped one.
std::filesystem::path configuration_path();
// `paw showcase`: a screenshot with paw fetch open in a terminal (src/cli/showcase.cpp).
int showcase(int argc, char **argv);
// `paw top`: the compositor's frames, its work and the windows that make it, live or once.
int top(int argc, char **argv);
// `paw shake [WINDOW]`, `paw gravity` and `paw party` (src/cli/toys.cpp), and the request line
// each sends.
int toy(int argc, char **argv);
std::string toy_request(int argc, char **argv);

} // namespace paw::cli
