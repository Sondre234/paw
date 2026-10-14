// SPDX-License-Identifier: GPL-3.0-or-later
/* paw doctor's report, and how it reads what other programs say: wpctl's status,
 * xdg-desktop-portal's configuration files, an update script's log, /proc and the session log.
 * Pure functions; src/cli/doctor.cpp gathers what they read. */
#include "paw/doctor.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace paw::doctor {
namespace {
std::string trim(const std::string &text) {
    auto first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos)
        return {};
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}
std::string lower(std::string text) {
    for (auto &c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
std::vector<std::string> lines(const std::string &text) {
    std::vector<std::string> result;
    std::istringstream stream(text);
    for (std::string line; std::getline(stream, line);)
        result.push_back(line);
    return result;
}
std::vector<std::string> split(const std::string &text, char separator) {
    std::vector<std::string> parts;
    std::string part;
    std::istringstream stream(text);
    while (std::getline(stream, part, separator))
        parts.push_back(part);
    return parts;
}
// One line for --plain: tabs and line breaks would split its fields.
std::string flat(const std::string &text, const char *line_break) {
    std::string result;
    for (char c : text) {
        if (c == '\n')
            result += line_break;
        else
            result += c == '\t' ? ' ' : c;
    }
    return result;
}
const char *symbol(Status status) {
    return status == Status::ok ? "✓" : status == Status::warning ? "!" : "✗";
}
const char *colour(Status status) {
    return status == Status::ok ? "\033[32m" : status == Status::warning ? "\033[33m" : "\033[31m";
}
std::string count(int number, const char *what) {
    return std::to_string(number) + " " + what + (number == 1 ? "" : "s");
}
// The keys of an .ini file's `group`.
std::map<std::string, std::string> ini_group(const std::string &text, const std::string &group) {
    std::map<std::string, std::string> keys;
    bool inside = false;
    for (const auto &raw : lines(text)) {
        auto line = trim(raw);
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;
        if (line.front() == '[') {
            inside = line == "[" + group + "]";
            continue;
        }
        auto equals = line.find('=');
        if (inside && equals != std::string::npos)
            keys[trim(line.substr(0, equals))] = trim(line.substr(equals + 1));
    }
    return keys;
}
std::vector<std::string> list(const std::string &value) {
    std::vector<std::string> items;
    for (const auto &item : split(value, ';'))
        if (auto word = trim(item); !word.empty())
            items.push_back(word);
    return items;
}
bool hex(const std::string &text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) {
        return std::isxdigit(static_cast<unsigned char>(c));
    });
}
} // namespace

std::string report(const std::vector<Result> &results, bool color) {
    std::vector<std::string> sections;
    for (const auto &result : results)
        if (std::find(sections.begin(), sections.end(), result.section) == sections.end())
            sections.push_back(result.section);
    const std::string bold = color ? "\033[1m" : "", plain = color ? "\033[0m" : "",
                      cyan = color ? "\033[36m" : "";
    std::string text;
    int errors = 0, warnings = 0;
    for (const auto &section : sections) {
        text += (text.empty() ? "" : "\n") + bold + section + plain + '\n';
        for (const auto &result : results) {
            if (result.section != section)
                continue;
            errors += result.status == Status::error;
            warnings += result.status == Status::warning;
            text += "  " + (color ? colour(result.status) : std::string()) +
                    symbol(result.status) + plain + ' ' + result.finding + '\n';
            if (result.status != Status::ok)
                for (const auto &step : split(result.fix, '\n'))
                    text += "      " + cyan + step + plain + '\n';
        }
    }
    text += '\n';
    if (!errors && !warnings)
        text += "Nothing wrong found.\n";
    else if (!errors)
        text += count(warnings, "warning") + ", nothing broken.\n";
    else
        text += count(errors, "problem") + (warnings ? " and " + count(warnings, "warning") : "") +
                ".\n";
    return text;
}

std::string plain_report(const std::vector<Result> &results) {
    std::string text;
    for (const auto &result : results)
        text += std::string(result.status == Status::ok        ? "ok"
                            : result.status == Status::warning ? "warning"
                                                               : "error") +
                '\t' + result.name + '\t' + flat(result.finding, " ") + '\t' +
                flat(result.status == Status::ok ? "" : result.fix, "; ") + '\n';
    return text;
}

Sinks parse_wpctl_status(const std::string &text) {
    Sinks sinks;
    std::string top, sub;
    for (const auto &line : lines(text)) {
        if (line.empty())
            continue;
        // A heading of its own ("Audio", "PipeWire 'pipewire-0' [...]") starts at the margin.
        if (line[0] != ' ' && line[0] != '\t' && static_cast<unsigned char>(line[0]) < 0x80) {
            top = trim(line);
            sub.clear();
            continue;
        }
        // "├─ Sinks:", "└─ Clients:"
        if (auto bar = line.find("─ "); bar != std::string::npos && trim(line).back() == ':') {
            sub = trim(line.substr(bar + std::string("─ ").size()));
            sub.pop_back();
            if (top == "Audio" && sub == "Sinks")
                sinks.listed = true;
            continue;
        }
        // "│  *   33. Name [vol: 1.00]": past the tree's lines, a mark for the default, the
        // object's number and its name.
        std::size_t at = 0;
        while (at < line.size()) {
            if (line[at] == ' ' || line[at] == '\t')
                ++at;
            else if (line.compare(at, 3, "│") == 0 || line.compare(at, 3, "├") == 0 ||
                     line.compare(at, 3, "└") == 0 || line.compare(at, 3, "─") == 0)
                at += 3;
            else
                break;
        }
        bool marked = at < line.size() && line[at] == '*';
        if (marked)
            at = line.find_first_not_of(' ', at + 1);
        if (at == std::string::npos || at >= line.size() ||
            !std::isdigit(static_cast<unsigned char>(line[at])))
            continue;
        auto dot = line.find(". ", at);
        if (dot == std::string::npos ||
            line.find_first_not_of("0123456789", at) != dot)
            continue;
        auto name = trim(line.substr(dot + 2));
        std::string detail;
        if (auto open = name.rfind(" ["); open != std::string::npos && name.back() == ']') {
            detail = name.substr(open + 2, name.size() - open - 3);
            name = trim(name.substr(0, open));
        }
        if (top == "Audio" && sub == "Sinks") {
            sinks.names.push_back(name);
            if (marked)
                sinks.default_name = name;
        } else if (top.starts_with("PipeWire") && sub == "Clients") {
            Client client{name};
            if (auto pid = detail.find("pid:"); pid != std::string::npos)
                client.pid = std::atoi(detail.c_str() + pid + 4);
            sinks.clients.push_back(client);
        }
    }
    return sinks;
}

bool no_sound_output(const Sinks &sinks) {
    if (sinks.names.empty())
        return true;
    auto name = lower(sinks.default_name);
    return name == "dummy output" || name.find("auto_null") != std::string::npos;
}

Environment parse_environ(const std::string &text) {
    Environment env;
    for (const auto &entry : split_nul(text))
        if (auto equals = entry.find('='); equals != std::string::npos && equals > 0)
            env.emplace(entry.substr(0, equals), entry.substr(equals + 1));
    return env;
}

std::vector<std::string> desktop_names(const std::string &value) {
    std::vector<std::string> names;
    for (const auto &name : split(value, ':'))
        if (!name.empty())
            names.push_back(lower(name));
    return names;
}

std::vector<std::filesystem::path> portal_config_directories(const Environment &env) {
    auto get = [&](const char *name) {
        auto found = env.find(name);
        return found == env.end() ? std::string() : found->second;
    };
    auto home = get("HOME");
    // A variable that is unset, empty or relative counts as unset, as the specification says.
    auto own = [&](const char *name, const char *fallback) -> std::vector<std::string> {
        auto value = get(name);
        if (!value.empty() && value[0] == '/')
            return {value};
        if (home.empty())
            return {};
        return {home + fallback};
    };
    auto shared = [&](const char *name, const char *fallback) {
        auto value = get(name);
        std::vector<std::string> directories;
        for (const auto &directory : split(value.empty() ? fallback : value, ':'))
            if (!directory.empty() && directory[0] == '/')
                directories.push_back(directory);
        return directories;
    };
    std::vector<std::filesystem::path> directories;
    auto add = [&](const std::vector<std::string> &bases) {
        for (const auto &base : bases) {
            auto directory = std::filesystem::path(base) / "xdg-desktop-portal";
            if (std::find(directories.begin(), directories.end(), directory) == directories.end())
                directories.push_back(directory);
        }
    };
    add(own("XDG_CONFIG_HOME", "/.config"));
    add(shared("XDG_CONFIG_DIRS", "/etc/xdg"));
    add({"/etc"});
    add(own("XDG_DATA_HOME", "/.local/share"));
    add(shared("XDG_DATA_DIRS", "/usr/local/share:/usr/share"));
    add({"/usr/share"});
    return directories;
}

std::filesystem::path portal_config(const std::vector<std::filesystem::path> &directories,
                                    const std::vector<std::string> &desktops,
                                    const std::function<bool(const std::filesystem::path &)> &exists) {
    for (const auto &directory : directories) {
        for (const auto &desktop : desktops)
            if (auto file = directory / (desktop + "-portals.conf"); exists(file))
                return file;
        if (auto file = directory / "portals.conf"; exists(file))
            return file;
    }
    return {};
}

std::vector<std::string> portal_backends(const std::string &conf, const std::string &interface) {
    auto keys = ini_group(conf, "preferred");
    if (auto own = keys.find(interface); own != keys.end())
        return list(own->second);
    if (auto fallback = keys.find("default"); fallback != keys.end())
        return list(fallback->second);
    return {};
}

bool portal_implements(const std::string &portal_file, const std::string &interface) {
    auto interfaces = list(ini_group(portal_file, "portal")["Interfaces"]);
    return std::find(interfaces.begin(), interfaces.end(), interface) != interfaces.end();
}

UpdateLog parse_update_log(const std::string &text) {
    UpdateLog log;
    for (const auto &line : lines(text)) {
        std::istringstream words(line);
        std::string first, date, time, verb, commit;
        words >> first;
        if (first == "update-paw:" && words >> date >> time >> verb >> commit) {
            if (verb == "building") {
                log.building = commit;
                log.building_time = date + ' ' + time;
            } else if (verb == "installed") {
                log.installed = commit;
                log.installed_time = date + ' ' + time;
            }
        } else if (first == "--") {
            // "-- Installing: PATH" or "-- Up-to-date: PATH"
            std::string action, path;
            if (words >> action >> path && (action == "Installing:" || action == "Up-to-date:") &&
                std::filesystem::path(path).filename() == "paw")
                log.binary = path;
        }
    }
    return log;
}

std::optional<ProcessStat> parse_stat(const std::string &text) {
    // "PID (COMM) STATE PPID ...": COMM may hold spaces and parentheses itself.
    auto close = text.rfind(')');
    if (close == std::string::npos)
        return std::nullopt;
    std::istringstream fields(text.substr(close + 1));
    std::vector<std::string> words;
    for (std::string word; fields >> word;)
        words.push_back(word);
    // starttime is the 22nd field, the 20th after COMM.
    if (words.size() < 20 || words[0].size() != 1)
        return std::nullopt;
    ProcessStat stat;
    stat.state = words[0][0];
    try {
        stat.ppid = std::stoi(words[1]);
        stat.start = std::stoull(words[19]);
    } catch (const std::exception &) {
        return std::nullopt;
    }
    return stat;
}

std::string program_path(const std::string &link) {
    return program_replaced(link) ? link.substr(0, link.size() - std::string(" (deleted)").size())
                                  : link;
}

bool program_replaced(const std::string &link) { return link.ends_with(" (deleted)"); }

std::vector<std::string> split_nul(const std::string &text) {
    std::vector<std::string> parts;
    for (std::size_t start = 0; start < text.size();) {
        auto end = text.find('\0', start);
        if (end == std::string::npos)
            end = text.size();
        parts.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}

std::string version_commit(const std::string &version) {
    auto open = version.find("(git ");
    if (open == std::string::npos)
        return {};
    auto close = version.find(')', open);
    if (close == std::string::npos)
        return {};
    auto describe = version.substr(open + 5, close - open - 5);
    if (describe.ends_with("-dirty"))
        describe.resize(describe.size() - 6);
    if (auto g = describe.rfind("-g"); g != std::string::npos && hex(describe.substr(g + 2)))
        return describe.substr(g + 2);
    return hex(describe) ? describe : std::string();
}

std::optional<std::string> since_last_start(const std::string &log) {
    auto start = log.rfind("Running Wayland compositor");
    if (start == std::string::npos)
        return std::nullopt;
    auto line = log.rfind('\n', start);
    return log.substr(line == std::string::npos ? 0 : line + 1);
}

std::string last_line(const std::string &text, const std::string &prefix) {
    std::string found;
    for (const auto &line : lines(text))
        if (line.starts_with(prefix))
            found = line.substr(prefix.size());
    return found;
}

std::string shell_word(const std::string &text) {
    bool plain = !text.empty() && std::all_of(text.begin(), text.end(), [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) ||
               std::string_view("_./:@%+=,-").find(c) != std::string_view::npos;
    });
    if (plain)
        return text;
    std::string word = "'";
    for (char c : text)
        word += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return word + "'";
}

} // namespace paw::doctor
