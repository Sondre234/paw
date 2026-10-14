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

// `paw xray [--list]`: the exploded view of the focused output's layers, or (--list) what each
// output draws, layer by layer.
int xray(int argc, char **argv);
// `get xray`'s rows as `paw xray --list` prints them: each output's surfaces and windows from the
// top down, with their layer, kind, name and place; `colour` puts a square of each layer's
// colour before it.
std::string xray_list(const std::vector<std::vector<std::string>> &rows, bool colour);

} // namespace paw::cli
