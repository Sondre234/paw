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

// `paw heatmap` (src/cli/heatmap.cpp): the heatmap action, or where the pointer has been in the
// terminal.
int heatmap(int argc, char **argv);
// The pointer heatmap `get heatmap` rows describe, laid out as the outputs are: a field of cells
// SH_HEAT_CELL logical pixels square over their bounding box, `width` by `height` logical
// pixels, each output's counts blurred, and whether a cell is on an output (1) or between them
// (0). `hottest` is the hottest cell's count before the blur.
struct Heatmap {
    int columns = 0, rows = 0, width = 0, height = 0;
    std::vector<float> field, inside;
    bool clicks = false;
    double hottest = 0;
    std::vector<std::string> outputs;
};
Heatmap heatmap_from(const std::vector<std::vector<std::string>> &rows);
// The heatmap fitted into `columns` by `lines` of a terminal, the last two left for its legend
// and the prompt: half blocks in 24-bit colour, or shades without `colour`.
std::string heatmap_text(const Heatmap &heatmap, int columns, int lines, bool colour);

} // namespace paw::cli
