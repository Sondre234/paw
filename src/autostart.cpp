// SPDX-License-Identifier: GPL-3.0-or-later
// XDG autostart: which desktop entries of the autostart directories a login session starts, and
// how, after the Desktop Application Autostart and Desktop Entry Specifications.
#include "paw/autostart.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <unistd.h>

namespace paw {
namespace {
// A desktop file larger than this is not read: real ones are a few kilobytes.
constexpr std::uintmax_t max_file_size = 256 * 1024;

std::string lower(std::string text) {
    for (auto &c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
std::string trim(const std::string &text) {
    auto first = text.find_first_not_of(" \t");
    if (first == std::string::npos)
        return {};
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}
// A string value with its escapes undone: \s, \n, \t, \r and \\. Others stay as written.
std::string unescape(const std::string &value) {
    std::string result;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '\\' || i + 1 == value.size()) {
            result += value[i];
            continue;
        }
        switch (value[++i]) {
        case 's': result += ' '; break;
        case 'n': result += '\n'; break;
        case 't': result += '\t'; break;
        case 'r': result += '\r'; break;
        case '\\': result += '\\'; break;
        default:
            result += '\\';
            result += value[i];
        }
    }
    return result;
}
// A list value, its items separated by ';' (written \; inside an item).
std::vector<std::string> list(const std::string &value) {
    std::vector<std::string> items;
    std::string item;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 1 < value.size() && value[i + 1] == ';') {
            item += ';';
            ++i;
        } else if (value[i] == ';') {
            if (!item.empty())
                items.push_back(unescape(item));
            item.clear();
        } else {
            item += value[i];
        }
    }
    if (!item.empty())
        items.push_back(unescape(item));
    return items;
}
// The keys of the [Desktop Entry] group, without localized ones ("Name[de]"); nothing when the
// file cannot be read or has no such group.
std::optional<std::map<std::string, std::string>> read_desktop_group(
    const std::filesystem::path &path) {
    std::error_code error;
    auto size = std::filesystem::file_size(path, error);
    if (error || size > max_file_size)
        return std::nullopt;
    std::ifstream file(path);
    if (!file)
        return std::nullopt;
    std::map<std::string, std::string> keys;
    bool found = false, inside = false;
    for (std::string line; std::getline(file, line);) {
        auto text = trim(line);
        if (text.empty() || text[0] == '#')
            continue;
        if (text[0] == '[') {
            inside = text == "[Desktop Entry]";
            found = found || inside;
            continue;
        }
        auto equals = text.find('=');
        if (!inside || equals == std::string::npos)
            continue;
        auto key = trim(text.substr(0, equals));
        if (key.empty() || key.find('[') != std::string::npos)
            continue;
        keys.emplace(key, trim(text.substr(equals + 1))); // the first one counts
    }
    if (!found)
        return std::nullopt;
    return keys;
}
bool is_true(const std::string &value) {
    return value == "true" || value == "1";
}
bool is_false(const std::string &value) {
    return value == "false" || value == "0";
}
// Whether `program` can be run: an executable path, or a name found on PATH.
bool executable(const std::string &program) {
    auto runnable = [](const std::filesystem::path &path) {
        std::error_code error;
        return access(path.c_str(), X_OK) == 0 && !std::filesystem::is_directory(path, error);
    };
    if (program.empty())
        return false;
    if (program.find('/') != std::string::npos)
        return runnable(program);
    const char *path = std::getenv("PATH");
    std::istringstream directories(path ? path : "");
    for (std::string directory; std::getline(directories, directory, ':');)
        if (runnable(std::filesystem::path(directory.empty() ? "." : directory) / program))
            return true;
    return false;
}
// The name a program goes by: the last part of its path, in lower case.
std::string program_name(const std::string &program) {
    return lower(std::filesystem::path(program).filename().string());
}
} // namespace

std::vector<std::filesystem::path> autostart_directories() {
    std::vector<std::filesystem::path> directories;
    const char *config = std::getenv("XDG_CONFIG_HOME"), *home = std::getenv("HOME");
    if (config && *config == '/')
        directories.push_back(std::filesystem::path(config) / "autostart");
    else if (home && *home == '/')
        directories.push_back(std::filesystem::path(home) / ".config/autostart");
    const char *dirs = std::getenv("XDG_CONFIG_DIRS");
    std::istringstream list(dirs && *dirs ? dirs : "/etc/xdg");
    for (std::string directory; std::getline(list, directory, ':');)
        if (directory.starts_with('/'))
            directories.push_back(std::filesystem::path(directory) / "autostart");
    return directories;
}

std::vector<std::string> current_desktops() {
    std::vector<std::string> names;
    const char *desktop = std::getenv("XDG_CURRENT_DESKTOP");
    std::istringstream list(desktop ? desktop : "");
    for (std::string name; std::getline(list, name, ':');)
        if (!name.empty())
            names.push_back(name);
    return names;
}

bool sound_server_running() {
    const char *runtime = std::getenv("XDG_RUNTIME_DIR");
    if (!runtime || *runtime != '/')
        return false;
    std::error_code error;
    return std::filesystem::exists(std::filesystem::path(runtime) / "pipewire-0", error) ||
           std::filesystem::exists(std::filesystem::path(runtime) / "pulse/native", error);
}

std::optional<std::vector<std::string>> parse_exec(const std::string &value,
                                                   const std::string &name,
                                                   const std::string &icon,
                                                   const std::string &path) {
    // Split at unquoted blanks. The specification allows only double quotes, in which \ escapes
    // ", `, $ and \; single quotes and a backslash outside quotes are taken as a shell would, as
    // GLib and KDE do, since files written by hand use them.
    auto text = unescape(value);
    std::vector<std::string> words;
    std::string word;
    bool in_word = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == ' ' || c == '\t' || c == '\n') {
            if (in_word)
                words.push_back(word);
            word.clear();
            in_word = false;
            continue;
        }
        in_word = true;
        if (c == '"') {
            for (++i;; ++i) {
                if (i == text.size())
                    return std::nullopt;
                if (text[i] == '"')
                    break;
                if (text[i] == '\\' && i + 1 < text.size() &&
                    std::string_view("\"`$\\").find(text[i + 1]) != std::string_view::npos)
                    ++i;
                word += text[i];
            }
        } else if (c == '\'') {
            auto end = text.find('\'', i + 1);
            if (end == std::string::npos)
                return std::nullopt;
            word += text.substr(i + 1, end - i - 1);
            i = end;
        } else if (c == '\\' && i + 1 < text.size()) {
            word += text[++i];
        } else {
            word += c;
        }
    }
    if (in_word)
        words.push_back(word);
    // Field codes. A session starts the program with no files or URLs to open.
    std::vector<std::string> command;
    for (const auto &raw : words) {
        if (raw == "%i") {
            if (!icon.empty()) {
                command.emplace_back("--icon");
                command.push_back(icon);
            }
            continue;
        }
        std::string argument;
        bool whole_code = raw.size() == 2 && raw[0] == '%' && raw[1] != '%';
        for (std::size_t i = 0; i < raw.size(); ++i) {
            if (raw[i] != '%' || i + 1 == raw.size()) {
                argument += raw[i];
                continue;
            }
            switch (raw[++i]) {
            case '%': argument += '%'; break;
            case 'c': argument += name; break;
            case 'k': argument += path; break;
            case 'i': argument += icon.empty() ? "" : "--icon " + icon; break;
            default: break; // %f %F %u %U %d %D %n %N %v %m, and unknown ones
            }
        }
        // A code standing alone that gave nothing leaves no empty argument behind.
        if (!whole_code || !argument.empty())
            command.push_back(argument);
    }
    if (command.empty() || command.front().empty())
        return std::nullopt;
    return command;
}

std::string autostart_role(const std::string &name, const std::vector<std::string> &command,
                           const std::string &try_exec) {
    std::vector<std::string> names;
    // The program `env` runs, past its options (-u and -C take the next argument) and
    // assignments.
    std::size_t first = 0;
    if (!command.empty() && program_name(command[0]) == "env")
        for (first = 1; first < command.size() && (command[first].starts_with('-') ||
                                                   command[first].find('=') != std::string::npos);
             ++first) {
            const auto &option = command[first];
            if (option == "-u" || option == "--unset" || option == "-C" || option == "--chdir")
                ++first;
        }
    if (first < command.size())
        names.push_back(program_name(command[first]));
    if (!try_exec.empty())
        names.push_back(program_name(try_exec));
    auto stem = lower(name);
    if (stem.ends_with(".desktop"))
        stem.resize(stem.size() - 8);
    names.push_back(stem);
    auto any = [&](auto &&predicate) { return std::any_of(names.begin(), names.end(), predicate); };
    auto contains = [](const std::string &text, std::initializer_list<const char *> parts) {
        return std::any_of(parts.begin(), parts.end(), [&](const char *part) {
            return text.find(part) != std::string::npos;
        });
    };
    static const std::set<std::string> notification_daemons{
        "dunst", "mako", "swaync", "fnott", "notify-osd", "wired", "notifyd"};
    // xfce4-notifyd, lxqt-notificationd, mate-notification-daemon, notification-daemon,
    // deadd-notification-center, sway-notification-center, linux_notification_center.
    if (any([&](const std::string &n) {
            return notification_daemons.count(n) ||
                   contains(n, {"notifyd", "notificationd", "notification-daemon",
                                "notification-center", "notification_center"});
        }))
        return "notifications";
    // Programs that serve org.kde.StatusNotifierWatcher; xembedsniproxy, which shows X11 tray
    // icons through it, is not one of them.
    static const std::set<std::string> tray_watchers{
        "snixembed", "status-notifier-watcher", "statusnotifierwatcher",
        "indicator-application-service", "ayatana-indicator-application-service"};
    if (any([&](const std::string &n) { return tray_watchers.count(n) > 0; }))
        return "tray";
    // polkit-gnome-authentication-agent-1, polkit-kde-authentication-agent-1, lxpolkit,
    // lxqt-policykit-agent, mate-polkit, xfce-polkit, hyprpolkitagent, pantheon-agent-polkit,
    // and soteria.
    if (any([&](const std::string &n) {
            return n == "soteria" || contains(n, {"polkit", "policykit"});
        }))
        return "polkit";
    // pipewire, pipewire-pulse, wireplumber, gentoo-pipewire-launcher, pulseaudio and
    // start-pulseaudio-x11: another start of these restarts the sound under running programs.
    if (any([&](const std::string &n) {
            return n == "wireplumber" || contains(n, {"pipewire", "pulseaudio"});
        }))
        return "sound";
    return {};
}

AutostartEntry read_autostart_entry(const std::filesystem::path &path, const std::string &name,
                                    const AutostartOptions &options) {
    AutostartEntry entry{name, path, {}, {}, {}, {}, {}};
    auto keys = read_desktop_group(path);
    if (!keys) {
        entry.skip = "not a desktop entry: no [Desktop Entry] group, or it cannot be read";
        return entry;
    }
    auto get = [&](const char *key) {
        auto found = keys->find(key);
        return found == keys->end() ? std::string() : found->second;
    };
    entry.program_name = unescape(get("Name"));
    entry.wm_class = unescape(get("StartupWMClass"));
    entry.directory = unescape(get("Path"));
    auto try_exec = unescape(get("TryExec"));
    auto exec = get("Exec");
    auto command = parse_exec(exec, entry.program_name, unescape(get("Icon")), path.string());
    if (command)
        entry.command = *command;
    auto shown = [&](const std::string &key) {
        for (const auto &desktop : list(get(key.c_str())))
            for (const auto &current : options.desktops)
                if (lower(desktop) == lower(current))
                    return true;
        return false;
    };
    auto type = get("Type");
    if (std::find(options.exclude.begin(), options.exclude.end(), name) != options.exclude.end())
        entry.skip = "excluded by autostart.exclude";
    else if (is_true(get("Hidden")))
        entry.skip = "Hidden=true";
    else if (!type.empty() && type != "Application")
        entry.skip = "Type=" + type + ", not Application";
    else if (keys->count("OnlyShowIn") && !shown("OnlyShowIn"))
        entry.skip = "OnlyShowIn=" + get("OnlyShowIn");
    else if (shown("NotShownIn"))
        entry.skip = "NotShownIn=" + get("NotShownIn");
    else if (is_false(get("X-GNOME-Autostart-enabled")))
        entry.skip = "X-GNOME-Autostart-enabled=false";
    else if (!try_exec.empty() && !executable(try_exec))
        entry.skip = "TryExec " + try_exec + " is not installed";
    else if (exec.empty())
        entry.skip = "no Exec";
    else if (!command)
        entry.skip = "Exec cannot be split into arguments: " + exec;
    else if (is_true(get("Terminal")))
        entry.skip = "Terminal=true: paw does not open a terminal for it";
    else if (auto role = autostart_role(name, entry.command, try_exec);
             role == "notifications" && options.notifications)
        entry.skip = "the shell is the notification daemon";
    else if (role == "tray" && options.tray)
        entry.skip = "the shell serves the tray";
    else if (role == "polkit" && options.polkit)
        entry.skip = "the shell is the polkit agent";
    else if (role == "sound" && options.sound)
        entry.skip = "a sound server is already running";
    return entry;
}

std::vector<std::string> program_names(const std::vector<std::string> &command) {
    std::vector<std::string> names;
    std::string word;
    auto flush = [&] {
        if (!word.empty() && word.front() != '-' && word.find('=') == std::string::npos)
            names.push_back(program_name(word));
        word.clear();
    };
    for (const auto &argument : command) {
        for (char c : argument) {
            if (std::isspace(static_cast<unsigned char>(c)) ||
                std::string_view(";&|()<>'\"`").find(c) != std::string_view::npos)
                flush();
            else
                word += c;
        }
        flush();
    }
    return names;
}

bool started_by(const std::set<std::string> &names, const std::string &app_id,
                const std::string &program) {
    return (!app_id.empty() && names.count(lower(app_id))) ||
           (!program.empty() && names.count(program_name(program)));
}

std::vector<AutostartEntry> autostart_entries(const std::vector<std::filesystem::path> &directories,
                                              const AutostartOptions &options) {
    std::map<std::string, std::filesystem::path> files; // by name, sorted
    for (const auto &directory : directories) {
        std::error_code error;
        for (std::filesystem::directory_iterator it(directory, error), end; !error && it != end;
             it.increment(error)) {
            auto name = it->path().filename().string();
            std::error_code ignored;
            if (name.ends_with(".desktop") && name.size() > 8 && !name.starts_with('.') &&
                it->is_regular_file(ignored))
                files.emplace(name, it->path()); // the more important directory came first
        }
    }
    std::vector<AutostartEntry> entries;
    for (const auto &[name, path] : files)
        entries.push_back(read_autostart_entry(path, name, options));
    return entries;
}
} // namespace paw
