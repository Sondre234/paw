// SPDX-License-Identifier: GPL-3.0-or-later
/* `paw top`: the compositor's frames, its work and the clients that make it, as htop shows a
 * machine's processes. It looks at the session a few times a second and redraws the terminal,
 * until q; with --once it prints one look. */
#include "paw/top.hpp"
#include "paw/cli.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <termios.h>
#include <thread>
#include <tuple>
#include <unistd.h>

// The version --version prints, where the build made one (a build with the compositor).
#if __has_include("version.h")
#include "version.h"
#else
#define PAW_VERSION ""
#endif

namespace paw::top {

namespace {

uint64_t number(const std::string &text) {
    char *end = nullptr;
    errno = 0;
    unsigned long long value = std::strtoull(text.c_str(), &end, 10);
    if (text.empty() || *end || errno || text[0] == '-')
        throw std::runtime_error("cannot read the compositor's reply: \"" + text + "\"");
    return value;
}

// The next code point of UTF-8 `text` from `at`, which it moves past it; U+FFFD for a byte that
// does not start one.
char32_t decode(const std::string &text, size_t &at) {
    auto byte = [&](size_t i) { return static_cast<unsigned char>(text[i]); };
    unsigned char first = byte(at);
    size_t length = first < 0x80 ? 1 : (first >> 5) == 6 ? 2 : (first >> 4) == 14 ? 3
                                     : (first >> 3) == 30 ? 4 : 0;
    if (!length || at + length > text.size()) {
        ++at;
        return 0xFFFD;
    }
    char32_t point = length == 1 ? first : first & (0x7F >> length);
    for (size_t i = 1; i < length; ++i) {
        if ((byte(at + i) & 0xC0) != 0x80) {
            ++at;
            return 0xFFFD;
        }
        point = point << 6 | (byte(at + i) & 0x3F);
    }
    at += length;
    return point;
}

// Columns a code point takes: none for a combining mark or a joiner, two for East Asian wide
// characters and emoji, one for the rest.
size_t columns_of(char32_t point) {
    static const char32_t none[][2] = {{0x0300, 0x036F}, {0x1AB0, 0x1AFF}, {0x1DC0, 0x1DFF},
                                       {0x200B, 0x200F}, {0x20D0, 0x20FF}, {0xFE00, 0xFE0F},
                                       {0xE0100, 0xE01EF}};
    static const char32_t wide[][2] = {
        {0x1100, 0x115F},   {0x2E80, 0x303E},   {0x3041, 0x33FF},   {0x3400, 0x4DBF},
        {0x4E00, 0x9FFF},   {0xA000, 0xA4CF},   {0xAC00, 0xD7A3},   {0xF900, 0xFAFF},
        {0xFE30, 0xFE4F},   {0xFF00, 0xFF60},   {0xFFE0, 0xFFE6},   {0x1F300, 0x1F64F},
        {0x1F900, 0x1F9FF}, {0x20000, 0x2FFFD}, {0x30000, 0x3FFFD}};
    for (const auto &range : none)
        if (point >= range[0] && point <= range[1])
            return 0;
    for (const auto &range : wide)
        if (point >= range[0] && point <= range[1])
            return 2;
    return 1;
}

std::string encode(char32_t point) {
    std::string text;
    if (point < 0x80) {
        text += static_cast<char>(point);
    } else if (point < 0x800) {
        text += static_cast<char>(0xC0 | point >> 6);
        text += static_cast<char>(0x80 | (point & 0x3F));
    } else if (point < 0x10000) {
        text += static_cast<char>(0xE0 | point >> 12);
        text += static_cast<char>(0x80 | (point >> 6 & 0x3F));
        text += static_cast<char>(0x80 | (point & 0x3F));
    } else {
        text += static_cast<char>(0xF0 | point >> 18);
        text += static_cast<char>(0x80 | (point >> 12 & 0x3F));
        text += static_cast<char>(0x80 | (point >> 6 & 0x3F));
        text += static_cast<char>(0x80 | (point & 0x3F));
    }
    return text;
}

enum class Align { left, right, none };

std::string fitted(const std::string &text, size_t width, Align align) {
    // Each code point as it will print, a control character (which could move the cursor or
    // start an escape sequence, from a window's title) as '?'.
    std::vector<std::pair<std::string, size_t>> pieces;
    size_t total = 0;
    for (size_t at = 0; at < text.size();) {
        char32_t point = decode(text, at);
        bool control = point < 0x20 || (point >= 0x7F && point < 0xA0) || point == 0xFFFD;
        pieces.emplace_back(control ? "?" : encode(point), control ? 1 : columns_of(point));
        total += pieces.back().second;
    }
    std::string result;
    size_t used = 0;
    if (total <= width) {
        for (const auto &piece : pieces)
            result += piece.first;
        used = total;
    } else if (width > 0) {
        for (const auto &piece : pieces) {
            if (used + piece.second > width - 1)
                break;
            result += piece.first;
            used += piece.second;
        }
        result += "…";
        ++used;
    }
    if (align == Align::left)
        result.append(width - used, ' ');
    else if (align == Align::right)
        result.insert(0, width - used, ' ');
    return result;
}

// Text in an SGR colour or style (1 bold, 2 dim, 7 reverse, 31 red, 32 green, 33 yellow), or
// as it is without colour.
struct Paint {
    bool on = false;
    std::string operator()(const char *code, const std::string &text) const {
        if (!on || text.empty())
            return text;
        return std::string("\x1b[") + code + "m" + text + "\x1b[0m";
    }
};

const char *grade_color(Grade grade) {
    switch (grade) {
    case Grade::good:
        return "32";
    case Grade::tight:
        return "33";
    case Grade::over:
        return "31";
    }
    return "0";
}

// "2560x1440@143.912" as "2560x1440 144Hz".
std::string mode_text(const Monitor &monitor) {
    std::string size = monitor.mode.substr(0, monitor.mode.find('@'));
    if (monitor.refresh <= 0)
        return size;
    char refresh[32];
    std::snprintf(refresh, sizeof(refresh), " %.0fHz", monitor.refresh);
    return size + refresh;
}

const Monitor *find_monitor(const Sample &sample, const std::string &name) {
    for (const auto &monitor : sample.monitors)
        if (monitor.name == name)
            return &monitor;
    return nullptr;
}

std::string lowered(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

} // namespace

std::vector<Client> parse_clients(const std::vector<std::vector<std::string>> &rows) {
    std::vector<Client> clients;
    for (const auto &row : rows) {
        if (row.size() < 9 || (row[0] != "window" && row[0] != "layer"))
            throw std::runtime_error("cannot read the compositor's reply to get commits");
        Client client;
        client.layer = row[0] == "layer";
        client.id = static_cast<uint32_t>(number(row[1]));
        client.pid = static_cast<int>(number(row[2]));
        client.commits = number(row[3]);
        client.commit_ns = number(row[4]);
        client.output = row[5];
        client.workspace = static_cast<int>(number(row[6]));
        client.name = row[7];
        client.title = row[8];
        for (size_t i = 9; i < row.size(); ++i) // a title with a tab the compositor kept
            client.title += " " + row[i];
        clients.push_back(std::move(client));
    }
    return clients;
}

double refresh_of(const std::string &mode) {
    auto at = mode.find('@');
    return at == std::string::npos ? 0 : std::strtod(mode.c_str() + at + 1, nullptr);
}

Grade grade(double us, double budget_us) {
    if (us > budget_us)
        return Grade::over;
    return us > budget_us / 2 ? Grade::tight : Grade::good;
}

double percentile(std::vector<uint32_t> values, double share) {
    if (values.empty())
        return 0;
    std::sort(values.begin(), values.end());
    auto rank = static_cast<size_t>(std::ceil(share * static_cast<double>(values.size())));
    return values[std::clamp<size_t>(rank, 1, values.size()) - 1];
}

Summary summarize(const std::vector<uint32_t> &values) {
    Summary summary;
    summary.count = values.size();
    if (values.empty())
        return summary;
    double total = 0;
    for (uint32_t value : values)
        total += value;
    summary.average = total / static_cast<double>(values.size());
    summary.p99 = percentile(values, 0.99);
    summary.worst = *std::max_element(values.begin(), values.end());
    return summary;
}

double per_second(uint64_t earlier, uint64_t later, double seconds) {
    if (seconds <= 0 || later < earlier)
        return 0;
    return static_cast<double>(later - earlier) / seconds;
}

const Sample &reference(const std::vector<Sample> &history, double window) {
    const Sample &newest = history.back();
    for (const auto &sample : history)
        if (&sample != &newest && newest.time - sample.time <= window + 1e-6)
            return sample;
    return history.size() > 1 ? history[history.size() - 2] : newest;
}

Sort next(Sort sort) {
    switch (sort) {
    case Sort::time:
        return Sort::commits;
    case Sort::commits:
        return Sort::name;
    case Sort::name:
        return Sort::workspace;
    case Sort::workspace:
        return Sort::time;
    }
    return Sort::time;
}

const char *sort_name(Sort sort) {
    switch (sort) {
    case Sort::time:
        return "time";
    case Sort::commits:
        return "commits";
    case Sort::name:
        return "app ID";
    case Sort::workspace:
        return "workspace";
    }
    return "";
}

std::vector<Row> rows(const std::vector<Sample> &history, double window, Sort sort) {
    std::vector<Row> result;
    if (history.empty())
        return result;
    const Sample &newest = history.back();
    size_t first = static_cast<size_t>(&reference(history, window) - history.data());
    for (const auto &client : newest.clients) {
        Row row{client};
        // Since the oldest look in the window that saw it: one that opened since has no
        // commits from before it opened to count.
        for (size_t i = first; i + 1 < history.size(); ++i) {
            auto same = std::find_if(history[i].clients.begin(), history[i].clients.end(),
                                     [&](const Client &other) {
                                         return other.layer == client.layer &&
                                                other.id == client.id && other.pid == client.pid;
                                     });
            if (same == history[i].clients.end())
                continue;
            double seconds = newest.time - history[i].time;
            row.commits = per_second(same->commits, client.commits, seconds);
            row.ns = per_second(same->commit_ns, client.commit_ns, seconds);
            break;
        }
        result.push_back(std::move(row));
    }
    auto named = [](const Row &row) {
        return std::make_tuple(lowered(row.client.name), lowered(row.client.title), row.client.id);
    };
    std::stable_sort(result.begin(), result.end(), [&](const Row &a, const Row &b) {
        switch (sort) {
        case Sort::time:
            if (a.ns != b.ns)
                return a.ns > b.ns;
            if (a.commits != b.commits)
                return a.commits > b.commits;
            break;
        case Sort::commits:
            if (a.commits != b.commits)
                return a.commits > b.commits;
            if (a.ns != b.ns)
                return a.ns > b.ns;
            break;
        case Sort::name:
            break;
        case Sort::workspace: {
            // Layer surfaces, on no workspace, after the windows.
            auto place = [](const Row &row) {
                return std::make_tuple(row.client.layer, row.client.output, row.client.workspace);
            };
            if (place(a) != place(b))
                return place(a) < place(b);
            break;
        }
        }
        return named(a) < named(b);
    });
    return result;
}

const char *glyph(double value, size_t row, size_t height, double top) {
    static const char *const blocks[] = {" ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    double eighths = top > 0 ? value / top * static_cast<double>(height * 8) : 0;
    auto level = std::clamp<long>(std::lround(eighths), 1, static_cast<long>(height * 8));
    long below = static_cast<long>(height - 1 - row) * 8; // eighths under this text row
    return blocks[std::clamp<long>(level - below, 0, 8)];
}

std::vector<std::string> graph(const std::vector<uint32_t> &values, size_t width, size_t height,
                               double top) {
    std::vector<std::string> lines(height);
    size_t shown = std::min(values.size(), width);
    for (size_t row = 0; row < height; ++row) {
        lines[row].assign(width - shown, ' ');
        for (size_t i = values.size() - shown; i < values.size(); ++i)
            lines[row] += glyph(values[i], row, height, top);
    }
    return lines;
}

double scale(double worst, double budget) {
    double wanted = std::max({worst, budget / 4, 1.0});
    double magnitude = std::pow(10, std::floor(std::log10(wanted)));
    for (double step : {1.0, 2.0, 2.5, 5.0})
        if (step * magnitude >= wanted * (1 - 1e-9))
            return step * magnitude;
    return 10 * magnitude;
}

size_t graph_height(size_t rows, size_t monitors) {
    if (rows == 0)
        return 3;
    // The header, two lines per monitor (a blank one and its numbers), two of activity, the
    // table's head and three of its rows, the keys, and the blank lines between.
    size_t fixed = 10 + 2 * monitors;
    if (monitors == 0 || rows <= fixed + monitors)
        return 1;
    return std::clamp<size_t>((rows - fixed) / monitors, 1, 4);
}

size_t width_of(const std::string &text) {
    size_t width = 0;
    for (size_t at = 0; at < text.size();) {
        if (text[at] == '\x1b' && at + 1 < text.size() && text[at + 1] == '[') {
            // An escape sequence takes no room: through its final byte.
            at += 2;
            while (at < text.size() && !(text[at] >= 0x40 && text[at] <= 0x7E))
                ++at;
            ++at;
            continue;
        }
        width += columns_of(decode(text, at));
    }
    return width;
}

std::string fit(const std::string &text, size_t width) { return fitted(text, width, Align::left); }

std::string fit_right(const std::string &text, size_t width) {
    return fitted(text, width, Align::right);
}

std::string cut(const std::string &text, size_t width) { return fitted(text, width, Align::none); }

std::vector<std::string> wrap(const std::vector<std::string> &cells, size_t width,
                              const std::string &separator) {
    std::vector<std::string> lines;
    std::string line;
    size_t used = 0, gap = width_of(separator);
    for (const auto &cell : cells) {
        size_t size = width_of(cell);
        if (!line.empty() && used + gap + size <= width) {
            line += separator + cell;
            used += gap + size;
            continue;
        }
        if (!line.empty())
            lines.push_back(line);
        line = cell;
        used = size;
    }
    if (!line.empty())
        lines.push_back(line);
    return lines;
}

std::string duration(double us) {
    char text[32];
    if (us < 999.5)
        std::snprintf(text, sizeof(text), "%.0fµs", us);
    else if (us < 9995)
        std::snprintf(text, sizeof(text), "%.2fms", us / 1e3);
    else if (us < 99950)
        std::snprintf(text, sizeof(text), "%.1fms", us / 1e3);
    else if (us < 999500)
        std::snprintf(text, sizeof(text), "%.0fms", us / 1e3);
    else
        std::snprintf(text, sizeof(text), "%.2fs", us / 1e6);
    return text;
}

std::string rate(double value) {
    char text[32];
    if (value < 0.05)
        return "0";
    if (value < 9.95)
        std::snprintf(text, sizeof(text), "%.1f", value);
    else if (value < 999.5)
        std::snprintf(text, sizeof(text), "%.0f", value);
    else if (value < 9950)
        std::snprintf(text, sizeof(text), "%.1fk", value / 1e3);
    else
        std::snprintf(text, sizeof(text), "%.0fk", value / 1e3);
    return text;
}

std::string uptime(double seconds) {
    auto whole = static_cast<long>(std::max(seconds, 0.0));
    char text[32];
    if (whole >= 4 * 86400)
        std::snprintf(text, sizeof(text), "%ldd %ldh", whole / 86400, whole / 3600 % 24);
    else
        std::snprintf(text, sizeof(text), "%ld:%02ld:%02ld", whole / 3600, whole / 60 % 60,
                      whole % 60);
    return text;
}

uint64_t start_ticks(const std::string &stat) {
    // pid (comm) state ppid ...: the 22nd field, the 20th after the name, which may hold
    // spaces and parentheses itself.
    auto close = stat.rfind(')');
    if (close == std::string::npos)
        return 0;
    std::istringstream fields(stat.substr(close + 1));
    std::string field;
    for (int i = 0; i < 20; ++i)
        if (!(fields >> field))
            return 0;
    char *end = nullptr;
    uint64_t ticks = std::strtoull(field.c_str(), &end, 10);
    return *end ? 0 : ticks;
}

Columns columns(size_t width) {
    Columns result;
    size_t used = 18; // commits and time each second
    if (width >= used + 4 + 1 + 12 + 6) {
        result.workspace = true;
        used += 4;
    }
    if (width >= used + 8 + 2 + 12 + 16) {
        result.each = true;
        used += 8;
    }
    size_t rest = width > used ? width - used : 0;
    if (rest >= 2 + 12 + 16) {
        result.name = std::clamp<size_t>((rest - 2) / 3, 12, 28);
        result.title = rest - 2 - result.name;
    } else {
        result.name = rest > 1 ? rest - 1 : 0;
    }
    return result;
}

std::vector<std::string> draw(const std::vector<Sample> &history, const View &view) {
    const size_t width = std::max<size_t>(view.width, 24);
    const Paint paint{view.color};
    std::vector<std::string> lines;
    auto add = [&](const std::vector<std::string> &cells, const std::string &separator) {
        for (auto &line : wrap(cells, width, separator))
            lines.push_back(std::move(line));
    };
    auto finish = [&]() {
        if (view.height == 0)
            return lines;
        // The keys on the last line, under whatever fits above them.
        lines.resize(view.height - 1);
        lines.push_back(paint("2", cut(std::string("q quit  p ") +
                                           (view.paused ? "resume" : "pause") + "  s sort: " +
                                           sort_name(view.sort),
                                       width)));
        return lines;
    };

    std::vector<std::string> header{
        paint("1", cut(view.version.empty() ? "paw" : "paw " + view.version, width))};
    if (!view.uptime.empty())
        header.push_back(cut("up " + view.uptime, width));
    if (!history.empty()) {
        size_t count = history.back().monitors.size();
        header.push_back(std::to_string(count) + (count == 1 ? " monitor" : " monitors"));
    }
    if (!view.profile.empty())
        header.push_back(cut("profile " + view.profile, width));
    if (view.paused)
        header.push_back(paint("7", "paused"));
    add(header, " · ");
    if (!view.error.empty())
        lines.push_back(paint("31", cut(view.error, width)));
    if (history.empty())
        return finish();
    const Sample &newest = history.back(), &from = reference(history, view.window);
    const double seconds = newest.time - from.time;

    // Each monitor: its frames each second, what drawing them took against its budget, and
    // a graph of its last frames.
    const size_t height = graph_height(view.height, newest.monitors.size());
    const size_t gutter = width >= 40 ? 8 : 0;
    for (const auto &monitor : newest.monitors) {
        lines.emplace_back();
        const double budget = 1e6 / (monitor.refresh > 0 ? monitor.refresh : 60);
        const Monitor *before = find_monitor(from, monitor.name);
        std::vector<std::string> cells{
            paint("1", cut(monitor.name, width)), cut(mode_text(monitor), width),
            rate(before ? per_second(before->frames, monitor.frames, seconds) : 0) + " fps"};
        if (!monitor.timed) {
            cells.push_back("its frame times need a newer compositor");
            add(cells, "  ");
            continue;
        }
        auto summary = summarize(monitor.spent);
        auto graded = [&](const char *label, double us) {
            return paint("2", label) + " " + paint(grade_color(grade(us, budget)), duration(us));
        };
        cells.push_back(graded("avg", summary.average));
        cells.push_back(graded("p99", summary.p99));
        cells.push_back(graded("worst", summary.worst));
        cells.push_back(paint("2", "budget " + duration(budget)));
        add(cells, "  ");
        const size_t span = width - gutter;
        const size_t shown = std::min(monitor.spent.size(), span);
        const auto newer = monitor.spent.end() - static_cast<long>(shown);
        const double top =
            scale(shown ? *std::max_element(newer, monitor.spent.end()) : 0, budget);
        for (size_t row = 0; row < height; ++row) {
            std::string line;
            if (gutter)
                line = row == 0 ? paint("2", fit_right(duration(top), gutter - 1)) + " "
                                : std::string(gutter, ' ');
            line.append(span - shown, ' ');
            const char *current = nullptr;
            for (auto value = newer; value != monitor.spent.end(); ++value) {
                const char *color = grade_color(grade(*value, budget));
                if (view.color && color != current) {
                    line += std::string("\x1b[") + color + "m";
                    current = color;
                }
                line += glyph(*value, row, height, top);
            }
            if (current)
                line += "\x1b[0m";
            lines.push_back(std::move(line));
        }
    }

    // What the compositor did each second, and how long it took.
    lines.emplace_back();
    auto each = [&](size_t field) {
        const auto &a = from.stats, &b = newest.stats;
        return field < a.size() && field < b.size() ? per_second(a[field], b[field], seconds) : 0;
    };
    auto counted = [&](const char *label, size_t field) {
        return paint("2", label) + " " + rate(each(field)) + "/s";
    };
    auto timed = [&](const char *label, size_t field, size_t ns) {
        return counted(label, field) + " " + duration(each(ns) / 1e3) + "/s";
    };
    add({paint("1", "Activity"), timed("frames", 0, 1), timed("commits", 3, 4),
         counted("configures", 5), timed("motions", 7, 8), timed("reflows", 9, 10),
         counted("rule matches", 6)},
        "  ");

    // Who makes the compositor work: each window's and panel's commits and the time handling
    // them took, each second.
    lines.emplace_back();
    if (!newest.listed) {
        lines.push_back(cut("Windows' commits need a newer compositor.", width));
        return finish();
    }
    const Columns layout = columns(width);
    auto line_of = [&](const std::string &commits, const std::string &time,
                       const std::string &each_one, const std::string &workspace,
                       const std::string &name, const std::string &title) {
        std::string line = fit_right(commits, 9) + " " + fit_right(time, 8);
        if (layout.each)
            line += " " + fit_right(each_one, 7);
        if (layout.workspace)
            line += " " + fit_right(workspace, 3);
        line += " " + fit(name, layout.name);
        if (layout.title)
            line += " " + fit(title, layout.title);
        return line;
    };
    lines.push_back(paint("7", line_of("COMMITS/s", "TIME/s", "EACH", "WS", "APP ID", "TITLE")));
    auto table = rows(history, view.window, view.sort);
    if (table.empty())
        lines.push_back(cut("No windows.", width));
    size_t room = table.size();
    if (view.height) {
        size_t left = view.height > lines.size() + 1 ? view.height - lines.size() - 1 : 0;
        room = table.size() > left ? (left ? left - 1 : 0) : table.size();
    }
    for (size_t i = 0; i < room; ++i) {
        const Row &row = table[i];
        const Client &client = row.client;
        auto line = line_of(rate(row.commits), row.ns > 0 ? duration(row.ns / 1e3) : "0",
                            row.commits > 0 ? duration(row.ns / row.commits / 1e3) : "-",
                            client.layer ? "-" : std::to_string(client.workspace), client.name,
                            client.layer ? "layer surface on " + client.output : client.title);
        lines.push_back(row.commits < 0.05 ? paint("2", line) : line);
    }
    if (room < table.size())
        lines.push_back(paint("2", cut("… " + std::to_string(table.size() - room) + " more",
                                       width)));
    return finish();
}

} // namespace paw::top

namespace paw::cli {

namespace {

using namespace paw::top;

volatile sig_atomic_t quitting = 0, resized = 0;
void on_quit(int) { quitting = 1; }
void on_resize(int) { resized = 1; }

// The running compositor's process: the peer of a connection to its control socket.
pid_t compositor_pid() {
    const char *path = std::getenv("PAW_SOCKET");
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (!path || !*path || std::strlen(path) >= sizeof(address.sun_path))
        return 0;
    std::strcpy(address.sun_path, path);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    ucred peer{};
    socklen_t size = sizeof(peer);
    if (fd < 0 || connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0 ||
        getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &size) < 0)
        peer.pid = 0;
    if (fd >= 0)
        close(fd);
    return peer.pid;
}

// Seconds since the machine booted, the clock /proc gives processes' start times on.
double since_boot() {
    timespec now{};
    clock_gettime(CLOCK_BOOTTIME, &now);
    return static_cast<double>(now.tv_sec) + static_cast<double>(now.tv_nsec) / 1e9;
}

// When the compositor started, in seconds after boot; negative if that cannot be told.
double compositor_started() {
    pid_t pid = compositor_pid();
    std::ifstream file("/proc/" + std::to_string(pid) + "/stat");
    uint64_t ticks = start_ticks({std::istreambuf_iterator<char>(file), {}});
    long hertz = sysconf(_SC_CLK_TCK);
    if (pid <= 0 || !ticks || hertz <= 0)
        return -1;
    return static_cast<double>(ticks) / static_cast<double>(hertz);
}

std::string session_uptime(double started) {
    return started < 0 ? "" : uptime(since_boot() - started);
}

// The appearance profile paw last saved, "" for none.
std::string saved_profile() {
    auto directory = state_directory();
    std::string name;
    if (!directory.empty()) {
        std::ifstream file(directory / "profile");
        std::getline(file, name);
    }
    return name;
}

// One look at the session. A query an older compositor does not answer leaves its part out;
// a session that cannot be reached throws.
Sample look(double time) {
    Sample sample;
    sample.time = time;
    for (const auto &row : query("stats"))
        for (const auto &field : row)
            sample.stats.push_back(number(field));
    auto drawn = query("output_frames");
    for (const auto &row : query("outputs")) {
        if (row.size() < 9 || row[1] != "1")
            continue;
        Monitor monitor;
        monitor.name = row[0];
        monitor.mode = row[8];
        monitor.refresh = refresh_of(row[8]);
        for (const auto &frames : drawn)
            if (frames.size() >= 2 && frames[0] == monitor.name)
                monitor.frames = number(frames[1]);
        auto reply = request("get frame_times " + monitor.name);
        if (reply.ok) {
            auto lines = rows(reply.body);
            monitor.timed = !lines.empty();
            for (size_t i = 1; i < lines.size(); ++i)
                monitor.spent.push_back(static_cast<uint32_t>(number(lines[i].at(0))));
        }
        sample.monitors.push_back(std::move(monitor));
    }
    auto reply = request("get commits");
    if (reply.ok) {
        sample.clients = parse_clients(rows(reply.body));
        sample.listed = true;
    }
    return sample;
}

void put(const std::string &text) {
    for (size_t sent = 0; sent < text.size();) {
        ssize_t written = write(STDOUT_FILENO, text.data() + sent, text.size() - sent);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            throw std::runtime_error("cannot write to the terminal");
        sent += static_cast<size_t>(written);
    }
}

// The terminal while top draws on it: keys read one at a time without echo, the screen of its
// own (the alternate one) without a cursor, put back as it was however top ends.
class Screen {
  public:
    Screen() {
        if (tcgetattr(STDIN_FILENO, &saved_) != 0)
            throw std::runtime_error("cannot read the terminal's settings");
        termios raw = saved_;
        raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO | ISIG | IEXTEN);
        raw.c_iflag &= ~static_cast<tcflag_t>(IXON | ICRNL);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0)
            throw std::runtime_error("cannot set up the terminal");
        put("\x1b[?1049h\x1b[?25l\x1b[?7l");
    }
    ~Screen() {
        try {
            put("\x1b[?7h\x1b[?25h\x1b[?1049l");
        } catch (const std::exception &) {
        }
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_);
    }
    Screen(const Screen &) = delete;
    Screen &operator=(const Screen &) = delete;
    void show(const std::vector<std::string> &lines) {
        std::string text = "\x1b[H";
        for (size_t i = 0; i < lines.size(); ++i)
            text += lines[i] + "\x1b[K" + (i + 1 < lines.size() ? "\r\n" : "");
        put(text + "\x1b[J");
    }

  private:
    termios saved_{};
};

struct Options {
    bool once = false, color = true;
    int interval = 250; // ms
};

std::pair<size_t, size_t> terminal_size() {
    winsize size{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0 && size.ws_row > 0)
        return {size.ws_col, size.ws_row};
    return {80, 24};
}

int print_once(const Options &options) {
    View view;
    view.version = PAW_VERSION;
    view.color = options.color && isatty(STDOUT_FILENO);
    view.window = options.interval / 1e3;
    view.width = 100;
    if (isatty(STDOUT_FILENO))
        view.width = terminal_size().first;
    else if (const char *columns = std::getenv("COLUMNS"); columns && std::atoi(columns) > 0)
        view.width = static_cast<size_t>(std::atoi(columns));
    std::vector<Sample> history{look(0)};
    std::this_thread::sleep_for(std::chrono::milliseconds(options.interval));
    history.push_back(look(options.interval / 1e3));
    view.uptime = session_uptime(compositor_started());
    view.profile = saved_profile();
    std::string text;
    for (auto line : draw(history, view)) {
        while (!line.empty() && line.back() == ' ')
            line.pop_back();
        text += line + "\n";
    }
    put(text);
    return 0;
}

int run_live(const Options &options) {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
        throw std::runtime_error("paw top draws on a terminal; paw top --once prints one look");
    // Quitting and resizing arrive only while ppoll waits, so neither slips in between a check
    // and the wait.
    struct sigaction quit{}, resize{}, ignore{};
    quit.sa_handler = on_quit;
    resize.sa_handler = on_resize;
    ignore.sa_handler = SIG_IGN;
    sigset_t handled, waiting;
    sigemptyset(&handled);
    for (int signal : {SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGWINCH})
        sigaddset(&handled, signal);
    sigprocmask(SIG_BLOCK, &handled, &waiting);
    for (int signal : {SIGINT, SIGTERM, SIGHUP, SIGQUIT})
        sigaction(signal, &quit, nullptr);
    sigaction(SIGWINCH, &resize, nullptr);
    sigaction(SIGPIPE, &ignore, nullptr); // a compositor gone mid-request is an error, not death

    const auto start = std::chrono::steady_clock::now();
    auto now = [&] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    };
    const double interval = options.interval / 1e3;
    View view;
    view.version = PAW_VERSION;
    view.color = options.color;
    view.window = std::max(1.0, interval);
    std::vector<Sample> history;
    double started = -1;
    auto sample = [&] {
        try {
            history.push_back(look(now()));
            // Keep what rates are measured from: the newest look a window old, and later ones.
            while (history.size() > 2 && history.back().time - history[1].time >= view.window)
                history.erase(history.begin());
            if (started < 0 || !view.error.empty()) // first, or a compositor started since
                started = compositor_started();
            view.uptime = session_uptime(started);
            view.error.clear();
            view.profile = saved_profile();
        } catch (const std::runtime_error &error) {
            view.error = error.what();
        }
    };

    Screen screen;
    sample();
    double due = now() + interval;
    for (;;) {
        std::tie(view.width, view.height) = terminal_size();
        screen.show(draw(history, view));
        for (bool redraw = false; !redraw;) {
            if (quitting)
                return 0;
            if (resized) {
                resized = 0;
                break;
            }
            if (!view.paused && now() >= due) {
                sample();
                due = std::max(due + interval, now());
                break;
            }
            double wait = view.paused ? 0 : due - now();
            timespec timeout{static_cast<time_t>(wait),
                             static_cast<long>((wait - std::floor(wait)) * 1e9)};
            pollfd input{STDIN_FILENO, POLLIN, 0};
            int ready = ppoll(&input, 1, view.paused ? nullptr : &timeout, &waiting);
            if (ready < 0 && errno != EINTR)
                throw std::runtime_error(std::string("cannot wait for keys: ") +
                                         std::strerror(errno));
            if (ready <= 0)
                continue;
            if (input.revents & (POLLHUP | POLLERR))
                return 0;
            char keys[64];
            ssize_t count = read(STDIN_FILENO, keys, sizeof(keys));
            for (ssize_t i = 0; i < count; ++i) {
                char key = keys[i];
                if (key == '\x1b' && i + 1 < count && (keys[i + 1] == '[' || keys[i + 1] == 'O')) {
                    // An arrow or function key: through the sequence's final byte.
                    for (i += 2; i < count && !(keys[i] >= 0x40 && keys[i] <= 0x7E); ++i) {
                    }
                    continue;
                }
                if (key == 'q' || key == 'Q' || key == '\x1b' || key == '\x03')
                    return 0;
                if (key == 'p' || key == 'P' || key == ' ') {
                    view.paused = !view.paused;
                    due = now(); // a look at once on resuming
                    redraw = true;
                } else if (key == 's' || key == 'S') {
                    view.sort = next(view.sort);
                    redraw = true;
                }
            }
        }
    }
}

} // namespace

int top(int argc, char **argv) {
    const std::string usage = "usage: paw top [--once] [--interval MS] [--no-color]";
    Options options;
    if (const char *no_color = std::getenv("NO_COLOR"); no_color && *no_color)
        options.color = false;
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--once")
            options.once = true;
        else if (arg == "--no-color")
            options.color = false;
        else if (arg == "--interval" && i + 1 < argc) {
            char *end = nullptr;
            long interval = std::strtol(argv[++i], &end, 10);
            if (*end || interval < 50 || interval > 60000)
                throw std::runtime_error("--interval takes milliseconds from 50 to 60000");
            options.interval = static_cast<int>(interval);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << usage << "\n"
                         "Shows the running session's frames, what the compositor does each "
                         "second and the windows\nthat make it work. q, Escape or Ctrl+C quits, "
                         "p pauses, s changes the order of the windows.\n"
                         "--once prints one look and exits; --interval sets how often it looks "
                         "(250 ms).\n";
            return 0;
        } else
            throw std::runtime_error(usage);
    }
    return options.once ? print_once(options) : run_live(options);
}

} // namespace paw::cli
