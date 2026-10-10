// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace paw {
// XDG autostart, after the Desktop Application Autostart Specification: the desktop entries in
// $XDG_CONFIG_HOME/autostart and each of $XDG_CONFIG_DIRS' autostart directories, which a login
// session starts. An entry in a more important directory hides one of the same file name in a
// less important one, so a user turns a system-wide entry off with a file of their own that says
// Hidden=true.
struct AutostartEntry {
    std::string name;           // the file's name, "foo.desktop"
    std::filesystem::path path; // the file that counts, from the most important directory
    std::string program_name;   // Name=
    // Exec, split into arguments with its field codes expanded or dropped.
    std::vector<std::string> command;
    std::string directory; // Path=: the working directory to start it in; "" for none
    std::string wm_class;  // StartupWMClass=: what its windows are called; "" for nothing said
    // Why the entry is not started, as `paw msg get autostart` says; "" when it is.
    std::string skip;
};

// What decides which entries are started.
struct AutostartOptions {
    std::vector<std::string> exclude;  // autostart.exclude: file names never started
    std::vector<std::string> desktops; // $XDG_CURRENT_DESKTOP's names, for OnlyShowIn/NotShownIn
    // What the shell provides itself this session; an entry for another of the same is skipped.
    bool notifications = false; // the notification daemon
    bool tray = false;          // the tray's StatusNotifierWatcher
    bool polkit = false;        // the polkit authentication agent
    // A sound server already runs (see sound_server_running); an entry starting one is skipped.
    bool sound = false;
};

// $XDG_CONFIG_HOME/autostart (else ~/.config/autostart), then the autostart directory of each of
// $XDG_CONFIG_DIRS (else /etc/xdg), most important first; relative paths are ignored.
std::vector<std::filesystem::path> autostart_directories();
// $XDG_CURRENT_DESKTOP's names.
std::vector<std::string> current_desktops();
// Whether PipeWire's or PulseAudio's socket is in $XDG_RUNTIME_DIR: a sound server runs already,
// as when the script that started the session started one.
bool sound_server_running();
// Every *.desktop file in `directories` by file name, the first directory's that has it, sorted by
// name, each with `skip` saying why it is not started.
std::vector<AutostartEntry> autostart_entries(const std::vector<std::filesystem::path> &directories,
                                              const AutostartOptions &options);
// The entry in `path` named `name`, as autostart_entries reads each one.
AutostartEntry read_autostart_entry(const std::filesystem::path &path, const std::string &name,
                                    const AutostartOptions &options);
// An Exec value (as written in the file, before its escapes are undone) split into arguments:
// %f, %F, %u, %U and the deprecated codes are dropped, %i becomes "--icon ICON" when there is an
// icon, %c the name, %k the file's path and %% a %. Nothing when its quoting is broken or it
// names no program.
std::optional<std::vector<std::string>> parse_exec(const std::string &value,
                                                   const std::string &name = {},
                                                   const std::string &icon = {},
                                                   const std::string &path = {});
// What the entry is that the shell provides itself: "notifications", "tray", "polkit", or "" for
// none; or "sound" for a sound server (PipeWire, PulseAudio, or a script that starts them). Told by the program it runs (after an `env` and its assignments), its TryExec, or its
// file's name.
std::string autostart_role(const std::string &name, const std::vector<std::string> &command,
                           const std::string &try_exec = {});
// The names the programs a command starts may go by, in lower case, for knowing their windows
// later: the last part of the path of each word that is no option (-x) or setting (A=b), a
// shell's command line split into its words ("sh -c 'sleep 2; foot'" gives sh, sleep, 2 and
// foot).
std::vector<std::string> program_names(const std::vector<std::string> &command);
// Whether a window with `app_id`, whose process ran `program` (its command line's first word),
// belongs to a program going by one of `names`: either is one of them, without regard to case
// and the program's directory.
bool started_by(const std::set<std::string> &names, const std::string &app_id,
                const std::string &program);
// Whether `program` can be run: an executable file at a path with a slash, or a name found on
// PATH.
bool executable(const std::string &program);
} // namespace paw
