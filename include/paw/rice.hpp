// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* Rices: a look to share and to try on (`paw rice`). A rice is a directory holding rice.lua and
 * the pictures and other files it names. rice.lua gives the rice's name, author and description
 * and the settings of an appearance profile; it is data, never run as Lua: a parser of Lua's
 * table syntax alone reads it (strings, numbers, booleans and tables, no names, calls or
 * operators), and it may set only how the desktop looks (`rice_setting_allowed`) and name only
 * files inside the rice. Installed rices live in $XDG_STATE_HOME/paw/rices/NAME, and the
 * configuration offers each as a profile named NAME. */
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace paw {

// A value of rice.lua: a boolean, an integer, another number, a string, or a table of values in
// order (`items`) and by name (`fields`, in the order given).
struct RiceData {
    enum class Type { Boolean, Integer, Number, String, Table };
    Type type = Type::Table;
    bool boolean = false;
    long long integer = 0;
    double number = 0;
    std::string string;
    std::vector<RiceData> items;
    std::vector<std::pair<std::string, RiceData>> fields;

    static RiceData string_of(std::string text);
    static RiceData integer_of(long long value);
    static RiceData boolean_of(bool value);
    // The field `name` of a table, or nullptr.
    const RiceData *field(std::string_view name) const;
    RiceData *field(std::string_view name);
    bool is_table() const { return type == Type::Table; }
    bool is_string() const { return type == Type::String; }
};
// Reads `source` as a table constructor in Lua's syntax, after an optional `return`, holding
// strings, numbers, booleans and tables alone; `name` names the file in errors ("rice.lua:3:
// ..."). Throws std::runtime_error.
RiceData parse_rice_data(const std::string &source, const std::string &name = "rice.lua");
// `data` in that syntax, a table over several lines indented by `indent` levels of 4 spaces
// after the first. Control characters, and bytes that are not UTF-8, are escaped, so the text
// is safe to print on a terminal. Throws for a number Lua's syntax cannot write (infinity).
std::string write_rice_data(const RiceData &data, int indent = 0);

// The most a rice holds: rice.lua's size, each file's, and the number of extra files.
constexpr size_t max_rice_source = 256 * 1024;
constexpr size_t max_rice_file = 50 * 1024 * 1024;
constexpr size_t max_rice_extras = 16;

struct Rice {
    std::string name;        // what it calls itself; installed, the directory may differ
    std::string author;      // "" when it names none
    std::string description; // "" when it has none
    // The profile's settings: a table with `appearance`, `windows` and `shell` tables.
    RiceData profile;
    // The pictures its settings name (shell.wallpaper, shell.start_icon), and the extra files
    // it brings for other programs, each relative to its directory.
    std::vector<std::string> pictures;
    std::vector<std::string> extras;
    std::filesystem::path directory; // where it was read from; empty for one read from text
};
// Whether a rice may set `setting` ({"shell", "accent"}): how the desktop looks, and nothing
// that runs a program, names a file outside the rice or changes how the desktop behaves.
bool rice_setting_allowed(const std::vector<std::string> &setting);
// Whether `path` names a file inside a rice: relative, 1 to 4 parts of letters, digits, '.',
// '-' and '_', none starting with '.'.
bool rice_path_valid(const std::string &path);
// rice.lua's text as a rice: its shape, its settings (allowed ones alone, the values still
// to be checked by the configuration) and its paths. Throws std::runtime_error saying what
// is wrong.
Rice parse_rice(const std::string &source);
// The rice in `directory`: rice.lua parsed, and every file it names a file there, not a link
// and not through one, at most max_rice_file bytes. Throws std::runtime_error.
Rice read_rice(const std::filesystem::path &directory);
// rice.lua and every file the rice in `directory` names, by their paths, read as read_rice
// checks them.
std::map<std::string, std::string> rice_files(const std::filesystem::path &directory);
// `files` (rice.lua and the files it names, by path) written into a new directory in `parent`
// named `.NAME-XXXXXX`, which the configuration passes over; returns it. Only the files
// rice.lua names are written. Throws std::runtime_error, leaving nothing behind.
std::filesystem::path stage_rice(const std::map<std::string, std::string> &files,
                                 const std::filesystem::path &parent);
// The profile's settings with the pictures' paths made absolute in the rice's directory.
RiceData rice_profile(const Rice &rice);
// The settings `a` and `b` give differently, as "shell.accent" with each one's value written as
// write_rice_data writes it ("" where one gives none), in the order of `b`, then those `a`
// alone gives.
struct RiceChange {
    std::string setting, before, after;
};
std::vector<RiceChange> rice_changes(const RiceData &a, const RiceData &b);
// `profile` with only the settings a rice may hold; the others' names, as "windows.rules", go
// to `left_out`.
RiceData rice_look(const RiceData &profile, std::vector<std::string> *left_out = nullptr);

// $XDG_STATE_HOME/paw/rices, else ~/.local/state/paw/rices; empty without either.
std::filesystem::path rices_directory();
// The installed rices, by name (their directories' names), each read by read_rice; why one
// could not be read goes to `problems`, as "NAME: WHY".
std::vector<Rice> installed_rices(std::vector<std::string> *problems = nullptr);
// A free name for a rice called `name` beside the profiles `taken`: `name`, else `name-2`,
// `name-3`, ...
std::string rice_install_name(const std::string &name, const std::vector<std::string> &taken);

// A rice on trial (`paw rice try`), recorded in $XDG_STATE_HOME/paw/rice-trial before the
// session switches to it, so that a session ending meanwhile comes back in the previous look.
struct RiceTrial {
    std::string rice;                    // its name, the profile on trial
    std::optional<std::string> previous; // the saved profile before it; none when none was
    bool replaced = false;               // an installed rice of that name waits set aside
    long pid = 0;                        // the compositor that took it on trial; 0 before
};
std::filesystem::path rice_trial_path();
std::optional<RiceTrial> load_rice_trial();
void save_rice_trial(const RiceTrial &trial);
// Where an installed rice waits during a trial of another of its name: rices/.NAME.previous.
std::filesystem::path rice_set_aside_path(const std::string &name);
// Starts a trial of the rice staged in `staged` (stage_rice) as `name`: records the trial,
// sets an installed rice of that name aside, moves the staged one into its place and saves
// `name` as the profile to use. Throws std::runtime_error, with nothing changed, when a trial
// is recorded already.
void begin_rice_trial(const std::filesystem::path &staged, const std::string &name);
// Ends the trial recorded, if any: keeping the rice, and dropping the one set aside; or going
// back, the saved profile as it was, the rice gone and the one set aside in its place. Each
// step can be taken again, so that a trial ended halfway ends again. Return the trial ended.
std::optional<RiceTrial> keep_rice_trial();
std::optional<RiceTrial> revert_rice_trial();
// At a compositor's start: a trial whose compositor is not running any more goes back.
// Returns it, or nothing when there was none or its compositor runs.
std::optional<RiceTrial> recover_rice_trial();

} // namespace paw
