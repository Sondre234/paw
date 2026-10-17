// SPDX-License-Identifier: GPL-3.0-or-later
/* What of `paw tour` needs no session: reading its table of steps, which of them can run, their
 * timing, and what their captions and windows say. */
#include "paw/tour.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace paw::tour {
namespace {

std::string trim(std::string_view text) {
    auto first = text.find_first_not_of(" \t"), last = text.find_last_not_of(" \t");
    return first == std::string_view::npos ? std::string()
                                           : std::string(text.substr(first, last - first + 1));
}

std::vector<std::string> split(std::string_view text, char separator) {
    std::vector<std::string> parts;
    for (size_t start = 0;;) {
        auto end = text.find(separator, start);
        parts.push_back(trim(text.substr(start, end - start)));
        if (end == std::string_view::npos)
            return parts;
        start = end + 1;
    }
}

std::vector<std::string> words(std::string_view text) {
    std::istringstream stream{std::string(text)};
    std::vector<std::string> result;
    for (std::string word; stream >> word;)
        result.push_back(word);
    return result;
}

std::string first_word(const std::string &request) {
    return request.substr(0, request.find(' '));
}

// A number of seconds from 0 to 60, or nothing.
bool parse_seconds(const std::string &text, double &seconds) {
    const char *end = text.data() + text.size();
    auto [last, error] = std::from_chars(text.data(), end, seconds);
    return error == std::errc() && last == end && seconds >= 0 && seconds <= 60;
}

// The {NAME}s a request may hold.
constexpr std::string_view placeholders[] = {"spare", "profile", "other_profile", "night_light"};

// Checks one of the tour's own requests; throws with what is wrong.
void check_request(const std::string &request) {
    auto parts = words(request);
    if (parts.empty())
        throw std::runtime_error("an empty request");
    double seconds = 0;
    if (parts[0] == "wait" && (parts.size() != 2 || !parse_seconds(parts[1], seconds)))
        throw std::runtime_error("wait takes seconds, from 0 to 60");
    if (parts[0] == "open" && (parts.size() != 2 || parts[1] < "1" || parts[1] > "4" ||
                               parts[1].size() != 1))
        throw std::runtime_error("open takes a number of windows, from 1 to 4");
    if (parts[0] == "tiling" && (parts.size() != 2 || (parts[1] != "on" && parts[1] != "off")))
        throw std::runtime_error("tiling takes on or off");
    if (parts[0] == "focus" && (parts.size() != 2 || parts[1] != "other"))
        throw std::runtime_error("focus takes other");
    // "close" alone is the action, for the focused window.
    if (parts[0] == "close" && parts.size() > 1 && (parts.size() != 2 || parts[1] != "windows"))
        throw std::runtime_error("close takes windows, or nothing for the focused one");
    for (size_t at = request.find('{'); at != std::string::npos; at = request.find('{', at + 1)) {
        auto end = request.find('}', at);
        auto name = std::string_view(request).substr(at + 1, end - at - 1);
        if (end == std::string::npos ||
            std::find(std::begin(placeholders), std::end(placeholders), name) ==
                std::end(placeholders))
            throw std::runtime_error("no such placeholder in '" + request + "'");
    }
}

} // namespace

std::vector<Step> parse_steps(const std::string &table) {
    std::vector<Step> steps;
    std::istringstream lines(table);
    int number = 0;
    for (std::string line; std::getline(lines, line);) {
        ++number;
        if (trim(line).empty() || trim(line).starts_with('#'))
            continue;
        try {
            auto fields = split(line, '|');
            if (fields.size() < 5 || fields.size() > 6)
                throw std::runtime_error("a step has five fields, and a sixth for what it needs");
            Step step;
            step.title = fields[0];
            step.body = fields[1];
            if (step.title.empty() || step.body.empty())
                throw std::runtime_error("a step needs a title and a sentence");
            if (fields[2] != "-")
                step.keys = words(fields[2]);
            if (fields[3] != "-") {
                for (auto &request : split(fields[3], ';')) {
                    check_request(request);
                    step.requests.push_back(request);
                }
            }
            if (!parse_seconds(fields[4], step.hold))
                throw std::runtime_error("how long a step stays is seconds, from 0 to 60");
            if (fields.size() == 6) {
                auto needs = words(fields[5]);
                if (needs.size() < 2 || needs[0] != "needs")
                    throw std::runtime_error("the sixth field is \"needs ACTION ...\"");
                step.needs.assign(needs.begin() + 1, needs.end());
            }
            steps.push_back(std::move(step));
        } catch (const std::runtime_error &error) {
            throw std::runtime_error("tour step on line " + std::to_string(number) + ": " +
                                     error.what());
        }
    }
    return steps;
}

std::string skip_reason(const Step &step, const Offer &offer) {
    for (const auto &action : step.needs)
        if (!offer.actions.contains(action))
            return "the compositor has no " + action;
    for (const auto &request : step.requests) {
        if (!offer.spare_workspace && request.find("{spare}") != std::string::npos)
            return "no second empty workspace";
        if (!offer.other_profile && request.find("{other_profile}") != std::string::npos)
            return "no other appearance profile";
        auto action = first_word(request);
        if (!offer.scratchpad_free && (action == "move_to_scratchpad" || action == "scratchpad_show"))
            return "windows of yours are in the scratchpad";
    }
    return "";
}

int scaled_ms(double seconds, double speed) {
    return seconds <= 0 || speed <= 0 ? 0 : static_cast<int>(std::lround(seconds * 1000 / speed));
}

double step_seconds(const Step &step) {
    double seconds = step.hold;
    for (const auto &request : step.requests) {
        auto parts = words(request);
        double wait = 0;
        if (parts.size() == 2 && parts[0] == "wait" && parse_seconds(parts[1], wait))
            seconds += wait;
    }
    return seconds;
}

std::string key_label(const std::string &keys) {
    static const std::map<std::string, std::string, std::less<>> names{
        {"minus", "-"},        {"equal", "="},         {"plus", "+"},
        {"grave", "`"},        {"bracketleft", "["},   {"bracketright", "]"},
        {"comma", ","},        {"period", "."},        {"semicolon", ";"},
        {"apostrophe", "'"},   {"slash", "/"},         {"backslash", "\\"},
        {"space", "Space"},    {"Return", "Enter"},    {"Escape", "Esc"},
        {"BackSpace", "Backspace"}, {"Prior", "Page Up"}, {"Next", "Page Down"}};
    std::string label;
    size_t start = 0;
    for (;;) {
        auto end = keys.find(" + ", start);
        auto key = keys.substr(start, end == std::string::npos ? end : end - start);
        if (auto name = names.find(key); name != names.end())
            key = name->second;
        else if (key.starts_with("XF86") && key.size() > 4)
            key = key.substr(4);
        label += key;
        if (end == std::string::npos)
            return label;
        label += " + ";
        start = end + 3;
    }
}

std::string caption_keys(const std::vector<std::string> &actions,
                         const std::map<std::string, std::string> &bindings) {
    std::string keys;
    for (const auto &action : actions) {
        auto bound = bindings.find(action);
        if (bound == bindings.end() || bound->second.empty() || bound->second == "-")
            continue;
        keys += (keys.empty() ? "" : " / ") + key_label(bound->second);
    }
    if (keys.empty() && !actions.empty())
        keys = "paw msg " + actions.front();
    return keys;
}

std::string other_profile(const std::vector<std::string> &profiles, const std::string &current) {
    auto at = std::find(profiles.begin(), profiles.end(), current);
    if (current.empty() || at == profiles.end() || profiles.size() < 2)
        return "";
    // A light profile's dark twin and the other way round, as the shipped ones pair up.
    static const std::pair<std::string_view, std::string_view> twins[] = {
        {"light", "dark"}, {"dark", "light"}, {"washi", "sumi"}, {"sumi", "washi"},
        {"day", "night"},  {"night", "day"}};
    for (auto [from, to] : twins) {
        auto found = current.find(from);
        if (found == std::string::npos)
            continue;
        auto twin = current;
        twin.replace(found, from.size(), to);
        if (std::find(profiles.begin(), profiles.end(), twin) != profiles.end())
            return twin;
    }
    return ++at == profiles.end() ? profiles.front() : *at;
}

std::string shell_quote(const std::string &text) {
    std::string quoted = "'";
    for (char c : text)
        quoted += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return quoted + "'";
}

std::vector<std::string> terminal_command(const std::vector<std::string> &terminal,
                                          const std::string &title, const std::string &script) {
    if (terminal.empty())
        return {};
    auto name = std::filesystem::path(terminal.front()).filename().string();
    std::vector<std::string> command = terminal;
    auto add = [&](std::initializer_list<std::string> words) {
        command.insert(command.end(), words);
    };
    // Each terminal's way to a title and a command; the script sets the title too, for one that
    // ignores it.
    if (name == "kitty" || name == "foot")
        add({"--title", title});
    else if (name == "alacritty" || name == "xterm")
        add({name == "xterm" ? "-T" : "--title", title, "-e"});
    else if (name == "ghostty")
        add({"--title=" + title, "-e"});
    else if (name == "wezterm")
        add({"start", "--always-new-process", "--"});
    else if (name == "gnome-terminal")
        add({"--wait", "--title", title, "--"}); // which would otherwise hand it to a server
    else
        add({"-e"}); // konsole's and most others'
    add({"sh", "-c", script});
    return command;
}

std::string client_command(const std::string &command, const std::string &title) {
    std::string result;
    size_t start = 0;
    for (size_t at; (at = command.find("{title}", start)) != std::string::npos; start = at + 7)
        result += command.substr(start, at - start) + shell_quote(title);
    return result + command.substr(start);
}

bool acts_on_window(const std::string &request) {
    static const std::set<std::string, std::less<>> actions{
        "close", "maximize", "restore", "toggle_maximize", "minimize", "fullscreen",
        "toggle_floating", "toggle_sticky", "toggle_above", "move_to_scratchpad",
        "move_to_workspace", "tile", "promote", "swap_next", "swap_prev", "consume_left",
        "consume_right", "expel", "column_widen", "column_narrow", "column_cycle_width",
        "center_column", "group_toggle", "group_next", "group_prev", "ungroup"};
    auto action = first_word(request);
    return actions.contains(action) || action.starts_with("snap_") ||
           action.starts_with("move_") || action.starts_with("resize_") ||
           action.starts_with("group_merge_");
}

bool acts_on_output(const std::string &request) {
    static const std::set<std::string, std::less<>> actions{
        "toggle_tiling", "toggle_overview", "switcher", "switcher_prev", "switcher_app",
        "switcher_app_prev", "scroll_left", "scroll_right"};
    auto action = first_word(request);
    return actions.contains(action) || action.starts_with("workspace") ||
           action.starts_with("layout_") || action.starts_with("master_");
}

std::string expand(const std::string &request, const std::map<std::string, std::string> &values) {
    std::string result;
    size_t start = 0;
    for (size_t at; (at = request.find('{', start)) != std::string::npos;) {
        auto end = request.find('}', at);
        if (end == std::string::npos)
            break;
        auto value = values.find(request.substr(at + 1, end - at - 1));
        if (value == values.end())
            throw std::runtime_error("nothing for " + request.substr(at, end - at + 1));
        result += request.substr(start, at - start) + value->second;
        start = end + 1;
    }
    return result + request.substr(start);
}

Options parse_options(int argc, char **argv) {
    const std::string usage = "usage: paw tour [--list] [--step] [--from N] [--speed FACTOR] "
                              "[--no-restore] [--client COMMAND]";
    Options options;
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc)
                throw std::runtime_error(arg + " needs a value; " + usage);
            return argv[++i];
        };
        if (arg == "--list") {
            options.list = true;
        } else if (arg == "--step") {
            options.step = true;
        } else if (arg == "--no-restore") {
            options.restore = false;
        } else if (arg == "--from") {
            auto text = value();
            auto [last, error] = std::from_chars(text.data(), text.data() + text.size(),
                                                 options.from);
            if (error != std::errc() || last != text.data() + text.size() || options.from < 1)
                throw std::runtime_error("--from takes a step's number, from 1");
        } else if (arg == "--speed") {
            auto text = value();
            auto [last, error] = std::from_chars(text.data(), text.data() + text.size(),
                                                 options.speed);
            if (error != std::errc() || last != text.data() + text.size() ||
                !(options.speed >= 0.25 && options.speed <= 100))
                throw std::runtime_error("--speed takes a factor from 0.25 to 100");
        } else if (arg == "--client") {
            options.client = value();
            if (trim(options.client).empty())
                throw std::runtime_error("--client takes a command");
        } else {
            throw std::runtime_error(usage);
        }
    }
    return options;
}

} // namespace paw::tour
