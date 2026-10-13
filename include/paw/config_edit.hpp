// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Writing a setting back into a Lua configuration file, as the shell does when the taskbar is
// dragged to another edge: the file is edited as text, so that everything else in it (comments,
// layout, the order of its tables) stays as the user wrote it.

#include "paw/config.hpp"
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace paw {
// `source` with the setting `trail` ({"shell", "panel_position"} for shell.panel_position) set
// to `literal`, Lua source of its value (`"left"`, quotes included). It tries, in turn, each
// place the setting could be given and returns the first edit `takes` accepts, so that the one
// that is in force changes (a profile's, rather than the file's own, while that profile is in
// use):
// - an assignment the file already has, a field of a table given to the setting's section
//   (`shell = { panel_position = "top" }`, a profile's too) or a statement
//   (`config.shell.panel_position = "top"`), whose value is a single string, number, boolean or
//   name; the fewest tables deep first, then in the order of the file;
// - a new first field of a table given to the section, the fewest tables deep first;
// - a new first field of the table after `return`, holding the section.
// Comments and strings are read as Lua reads them, so a setting in either is never taken for
// one. A source `takes` accepts already is returned as it is. Throws std::runtime_error when no
// edit takes.
std::string edit_setting(const std::string &source, const std::vector<std::string> &trail,
                         const std::string &literal,
                         const std::function<bool(const std::string &)> &takes);
// Sets `trail` to `literal` in the configuration file at `path`, as edit_setting does, where the
// edit takes when the edited file loads as load_config would load it (with the saved profile)
// and `takes` accepts the configuration it gives. The file is replaced at once, keeping its
// permissions, unless it gives the value already; a link's target is edited, and the link stays. Throws std::runtime_error, leaving
// the file as it was, when the file cannot be read or written, has an error, or no edit takes.
void save_setting(const std::filesystem::path &path, const std::vector<std::string> &trail,
                  const std::string &literal, const std::function<bool(const Config &)> &takes);
} // namespace paw
