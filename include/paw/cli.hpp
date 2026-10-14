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

} // namespace paw::cli
