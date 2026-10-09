// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>
#include <string>
#include <vector>

namespace paw {
// One configuration setting. The schema is the single list of what a configuration may
// contain: the parser takes its accepted keys from it, docs/config-reference.md is generated
// from it, and tests probe every entry against the parser.
//
// Paths join nested names with dots. "[]" marks the elements of a list ("bindings[].key")
// and "<name>" a table keyed by an arbitrary name ("outputs.monitors.<name>.mode").
struct Option {
    const char *path;
    const char *type;        // boolean, integer, number, string, color, enum, list, table, ...
    const char *default_value; // as documented, e.g. "52" or "unset"
    const char *example;     // a Lua literal the parser accepts; "" for tables and lists
    double min, max;         // an inclusive range for integer and number; both 0 otherwise
    const char *description;
};

std::span<const Option> config_options();
// The settings directly inside `parent` ("" for the top level), in schema order.
std::vector<const Option *> config_children(const std::string &parent);
// Every action name a binding may use, and the modifier and button names.
std::vector<std::string> config_action_names();
std::vector<std::string> config_modifier_names();
std::vector<std::string> config_button_names();
// The closest of `candidates` to `word` when it is plausibly a typo of one; else empty.
std::string closest_match(const std::string &word, const std::vector<std::string> &candidates);
// docs/config-reference.md, generated from the schema.
std::string config_reference_markdown();
} // namespace paw
