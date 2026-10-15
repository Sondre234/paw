// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* The subcommands `paw NAME ...` runs besides starting a compositor (src/cli/), and what they
 * share: a request to the running session's control socket, its reply split into rows, and
 * where paw keeps its state. Each subcommand is `int NAME(int argc, char **argv)`, argv[1]
 * being its own name; it returns the exit status and throws std::runtime_error for a usage
 * error or a failure, which main prints as "paw: WHAT". */
#include <filesystem>
#include <map>
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

// $XDG_CONFIG_HOME/paw/init.lua, else ~/.config/paw/init.lua, whether or not it exists; empty
// without either variable (src/cli/config.cpp).
std::filesystem::path personal_configuration_path();
// The configuration paw starts with when given none: the personal one where it exists, else
// the shipped one.
std::filesystem::path configuration_path();
// `paw doctor`: looks the session over and prints what is wrong with it and how to fix that.
int doctor(int argc, char **argv);
// `paw fetch`: a paw print and what paw knows of the session (src/cli/fetch.cpp).
int fetch(int argc, char **argv);
// `paw showcase`: a screenshot with paw fetch open in a terminal (src/cli/showcase.cpp).
int showcase(int argc, char **argv);
// `paw top`: the compositor's frames, its work and the windows that make it, live or once.
int top(int argc, char **argv);
// `paw shake [WINDOW]`, `paw gravity` and `paw party` (src/cli/toys.cpp), and the request line
// each sends.
int toy(int argc, char **argv);
std::string toy_request(int argc, char **argv);
// `paw rice`: a profile's look packed into a file to share, and one tried on, going back by
// itself unless it is kept (src/cli/rice.cpp).
int rice(int argc, char **argv);
// The most an archive holding a rice may be, unpacked.
constexpr size_t max_rice_archive = 64 * 1024 * 1024;
// `files` (paths in a rice) as a tar archive, under the directory `name`.
std::string rice_archive(const std::string &name, const std::map<std::string, std::string> &files);
// The files of a tar archive holding a rice, by their paths in it: rice.lua at the top or in
// the one directory at the top. Throws for an archive with a link, a device, an absolute path or
// one leading out of it, or more than max_rice_archive bytes.
std::map<std::string, std::string> rice_archive_files(const std::string &archive);
// What `command` writes reading the file `input`, at most `most` bytes; throws when it fails or
// writes more.
std::string run_filter(const std::vector<std::string> &command, const std::filesystem::path &input,
                       size_t most);
// `paw cat [on|off|toggle]`: shows or hides the desktop cat, toggling without a word, and says
// what it is up to.
int cat(int argc, char **argv);
// That sentence, for the rows of `get cat`.
std::string cat_said(const std::vector<std::vector<std::string>> &rows);
// `paw wrapped`: a report of time at the desktop from what the compositor counts.
int wrapped(int argc, char **argv);
// `paw xray [--list]`: the exploded view of the focused output's layers, or (--list) what each
// output draws, layer by layer.
int xray(int argc, char **argv);
// `get xray`'s rows as `paw xray --list` prints them: each output's surfaces and windows from the
// top down, with their layer, kind, name and place; `colour` puts a square of each layer's
// colour before it.
std::string xray_list(const std::vector<std::vector<std::string>> &rows, bool colour);
// `paw heatmap` (src/cli/heatmap.cpp): the heatmap action, or where the pointer has been in the
// terminal or in an image.
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
// The heatmap as an image file, a pixel a logical pixel: "png", "pam" or "ppm".
std::string heatmap_image(const Heatmap &heatmap, const std::string &format);
// `paw tour [--list] [--step] [--from N] [--speed FACTOR] [--no-restore] [--client COMMAND]`: a
// narrated demo of paw that drives the running session, then puts it back as it was.
int tour(int argc, char **argv);

} // namespace paw::cli
