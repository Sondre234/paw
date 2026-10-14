// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* `paw doctor` (src/cli/doctor.cpp): a check's result and the report of them, and how it reads
 * what other programs say (src/cli/doctor_parse.cpp): wpctl, xdg-desktop-portal's configuration,
 * an update script's log, /proc and the session's log. These are pure functions, for the unit
 * test. */
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace paw::doctor {

enum class Status { ok, warning, error };

// What one check found: a line saying so and, unless it is fine, the command or step that fixes
// it (steps on lines of their own).
struct Result {
    std::string section; // the heading it is printed under, as "Sound"
    std::string name;    // what --plain calls it, as "output"
    Status status = Status::ok;
    std::string finding{};
    std::string fix{};
};

// The report on a terminal: each section's results under its heading, ✓, ! or ✗ before each
// finding and its fix indented below, then a line counting the problems; in ANSI colours with
// `color`.
std::string report(const std::vector<Result> &results, bool color);
// The report for scripts: a line per result of "ok", "warning" or "error", its name, finding and
// fix separated by tabs, a fix's steps joined by "; ".
std::string plain_report(const std::vector<Result> &results);

// What `wpctl status` lists: the programs connected to PipeWire, and the sound outputs (sinks)
// under Audio, the default one marked with *.
struct Client {
    std::string name;
    int pid = 0;
};
struct Sinks {
    bool listed = false; // the output had an Audio section with its sinks
    std::vector<Client> clients;
    std::vector<std::string> names;
    std::string default_name; // "" when none is marked
};
Sinks parse_wpctl_status(const std::string &text);
// Whether there is nothing to play sound on: no output, or the default one PipeWire's stand-in
// for none ("Dummy Output", auto_null).
bool no_sound_output(const Sinks &sinks);

// An environment, as in /proc/PID/environ (NAME=VALUE entries separated by NULs).
using Environment = std::map<std::string, std::string>;
Environment parse_environ(const std::string &text);
// The names XDG_CURRENT_DESKTOP lists, in lower case.
std::vector<std::string> desktop_names(const std::string &value);
// Where xdg-desktop-portal looks for its configuration with `env`, the most important directory
// first (portals.conf(5)): XDG_CONFIG_HOME, XDG_CONFIG_DIRS, /etc, XDG_DATA_HOME, XDG_DATA_DIRS
// and /usr/share, each with /xdg-desktop-portal.
std::vector<std::filesystem::path> portal_config_directories(const Environment &env);
// The configuration file it reads: in the first of `directories` that has one, DESKTOP-portals.conf
// for the first of `desktops` there is one for, else portals.conf; empty for none. `exists` says
// whether a file is there (a link to nothing is not).
std::filesystem::path portal_config(const std::vector<std::filesystem::path> &directories,
                                    const std::vector<std::string> &desktops,
                                    const std::function<bool(const std::filesystem::path &)> &exists);
// The backends a portals.conf's [preferred] group names for `interface`, else for every one
// (default); "none" turns it off, and "*" takes any.
std::vector<std::string> portal_backends(const std::string &conf, const std::string &interface);
// Whether a backend's NAME.portal file lists `interface` among its Interfaces.
bool portal_implements(const std::string &portal_file, const std::string &interface);

// What an update script's log says, one that writes "update-paw: DATE TIME building COMMIT" as it
// starts, "update-paw: DATE TIME installed COMMIT" once it has, and cmake --install's lines between.
struct UpdateLog {
    std::string building, building_time;   // the commit it built last, and when it started
    std::string installed, installed_time; // the commit it installed last, and when
    std::string binary;                    // where it installed paw, "" when it did not say
};
UpdateLog parse_update_log(const std::string &text);

// What /proc/PID/stat says of a process.
struct ProcessStat {
    char state = '?'; // 'Z' for a zombie, which has ended
    int ppid = 0;
    unsigned long long start = 0; // when it started, in clock ticks after boot
};
std::optional<ProcessStat> parse_stat(const std::string &text);
// The program a /proc/PID/exe link names, without the " (deleted)" a replaced file gets.
std::string program_path(const std::string &link);
// Whether the program a /proc/PID/exe link names was deleted or replaced since it started.
bool program_replaced(const std::string &link);
// A NUL-separated list, as /proc/PID/cmdline.
std::vector<std::string> split_nul(const std::string &text);
// The commit `paw --version` names: 922e2b0 of "paw 0.1.1 (git 922e2b0)" or of
// "(git v0.1.1-3-g922e2b0)"; "" for none.
std::string version_commit(const std::string &version);

// The part of a session log after the compositor last started in it ("Running Wayland
// compositor"); nothing when it did not.
std::optional<std::string> since_last_start(const std::string &log);
// What follows `prefix` on the last line of `text` that starts with it; "" for none.
std::string last_line(const std::string &text, const std::string &prefix);

// `text` as a single word for sh: as it is when it is plain, else in single quotes.
std::string shell_word(const std::string &text);

} // namespace paw::doctor
