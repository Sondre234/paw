// SPDX-License-Identifier: GPL-3.0-or-later
/* `paw fetch`: a paw print in the colours of the appearance in use, and beside it what only paw
 * knows of the session (its profile, uptime, frames, windows, monitors, the application launched
 * most), asked of the running compositor, or read from the configuration and paw's state files
 * outside a session. */
#include "paw/fetch.hpp"
#include "paw/cli.hpp"
#include "paw/config.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <numbers>
#include <set>
#include <sstream>
#include <stdexcept>
#include <sys/ioctl.h>
#include <unistd.h>

// The build paw fetch is part of; the compositor's build gives version.h and wlroots' headers.
#if __has_include("version.h")
#include "version.h"
#else
#define PAW_VERSION "unknown"
#endif
#if __has_include(<wlr/version.h>)
#include <wlr/version.h>
#endif

namespace paw::fetch {
namespace {

std::optional<long long> integer(const std::string &text) {
    long long value = 0;
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc() || end != text.data() + text.size())
        return std::nullopt;
    return value;
}

std::optional<double> real(const std::string &text) {
    char *end = nullptr;
    double value = std::strtod(text.c_str(), &end);
    if (text.empty() || *end)
        return std::nullopt;
    return value;
}

Rgb mix(Rgb a, Rgb b, double t) {
    auto channel = [t](int x, int y) { return static_cast<int>(std::lround(x + (y - x) * t)); };
    return {channel(a.r, b.r), channel(a.g, b.g), channel(a.b, b.b)};
}

Rgb straight(const float (&color)[4]) {
    auto channel = [&](int i) {
        float value = color[3] > 0 ? color[i] / color[3] : 0;
        return static_cast<int>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255));
    };
    return {channel(0), channel(1), channel(2)};
}

double luminance(Rgb color) {
    auto linear = [](int value) {
        double s = value / 255.0;
        return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * linear(color.r) + 0.7152 * linear(color.g) + 0.0722 * linear(color.b);
}

// A UTF-8 string's width in a terminal, each code point a column, as everything paw fetch writes
// is.
size_t width(const std::string &text) {
    return static_cast<size_t>(std::count_if(text.begin(), text.end(), [](char c) {
        return (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    }));
}

std::string foreground(Rgb color) {
    return "\x1b[38;2;" + std::to_string(color.r) + ";" + std::to_string(color.g) + ";" +
           std::to_string(color.b) + "m";
}

std::string background(Rgb color) {
    return "\x1b[48;2;" + std::to_string(color.r) + ";" + std::to_string(color.g) + ";" +
           std::to_string(color.b) + "m";
}

const char *const reset = "\x1b[0m";

// The paw print, two pixels to a character cell: '#' is the pad, '+' a highlight on it.
const char *const paw_print[] = {
    "......##......##......", //
    ".....####....####.....", //
    "....##++##..##++##....", //
    "....######..######....", //
    "....######..######....", //
    ".##..####....####..##.", //
    "#++#..##......##..#++#", //
    "####..............####", //
    "####......##......####", //
    ".##.....######.....##.", //
    "......###++#####......", //
    ".....############.....", //
    "....##############....", //
    "....##############....", //
    "....####.####.####....", //
    ".....##...##...##.....", //
};
constexpr int print_width = 22, print_height = sizeof(paw_print) / sizeof(*paw_print);

} // namespace

std::optional<Rgb> parse_color(const std::string &text) {
    if ((text.size() != 7 && text.size() != 9) || text[0] != '#' ||
        text.find_first_not_of("0123456789abcdefABCDEF", 1) != std::string::npos)
        return std::nullopt;
    auto channel = [&](size_t at) {
        return static_cast<int>(std::stoi(text.substr(at, 2), nullptr, 16));
    };
    return Rgb{channel(1), channel(3), channel(5)};
}

Palette palette_of(const sh_settings &settings) {
    Palette palette;
    palette.scheme = settings.color_scheme == SH_COLOR_SCHEME_LIGHT  ? "light"
                     : settings.color_scheme == SH_COLOR_SCHEME_DARK ? "dark"
                                                                     : "";
    palette.background = straight(settings.background);
    for (int i = 0; i < settings.border_active_gradient.count && i < SH_GRADIENT_STOPS; ++i)
        palette.border.push_back(straight(settings.border_active_gradient.stops[i]));
    if (palette.border.empty())
        palette.border.push_back(straight(settings.border_active));
    palette.angle = settings.border_active_gradient.angle;
    palette.inactive = straight(settings.border_inactive);
    palette.urgent = straight(settings.urgent_color);
    return palette;
}

Palette palette_of(const Rows &reply, Palette fallback) {
    auto palette = std::move(fallback);
    for (const auto &row : reply) {
        if (row.size() < 2)
            continue;
        const auto &name = row[0];
        if (name == "scheme")
            palette.scheme = row[1] == "-" ? "" : row[1];
        else if (name == "background" || name == "urgent") {
            if (auto color = parse_color(row[1]))
                (name == "background" ? palette.background : palette.urgent) = *color;
        } else if (name == "border" || name == "border_inactive") {
            std::vector<Rgb> stops;
            for (size_t i = 2; i < row.size(); ++i)
                if (auto color = parse_color(row[i]))
                    stops.push_back(*color);
            if (stops.empty())
                continue;
            if (name == "border_inactive")
                palette.inactive = stops.front();
            else {
                palette.border = std::move(stops);
                palette.angle = real(row[1]).value_or(0);
            }
        }
    }
    return palette;
}

double contrast(Rgb a, Rgb b) {
    double x = luminance(a), y = luminance(b);
    return (std::max(x, y) + 0.05) / (std::min(x, y) + 0.05);
}

Rgb readable(Rgb color, Rgb background, double ratio) {
    const Rgb black{0, 0, 0}, white{255, 255, 255};
    Rgb toward = contrast(black, background) > contrast(white, background) ? black : white;
    for (int step = 0; step <= 20; ++step) {
        Rgb candidate = mix(color, toward, step / 20.0);
        if (contrast(candidate, background) >= ratio)
            return candidate;
    }
    return toward;
}

Rgb gradient_at(const Palette &palette, double u, double v) {
    const auto &stops = palette.border;
    if (stops.empty())
        return {};
    if (stops.size() == 1)
        return stops.front();
    double radians = palette.angle * std::numbers::pi / 180, dx = std::cos(radians),
           dy = std::sin(radians);
    double reach = std::abs(dx) + std::abs(dy);
    double t = std::clamp(((u - 0.5) * dx + (v - 0.5) * dy) / reach + 0.5, 0.0, 1.0);
    double at = t * static_cast<double>(stops.size() - 1);
    size_t i = std::min(static_cast<size_t>(at), stops.size() - 2);
    return mix(stops[i], stops[i + 1], at - static_cast<double>(i));
}

std::string duration(int64_t milliseconds) {
    int64_t seconds = std::max<int64_t>(milliseconds, 0) / 1000;
    auto two = [](int64_t big, const char *big_unit, int64_t small, const char *small_unit) {
        return std::to_string(big) + big_unit + (small ? std::to_string(small) + small_unit : "");
    };
    if (seconds < 60)
        return std::to_string(seconds) + "s";
    if (seconds < 3600)
        return two(seconds / 60, "m", seconds % 60, "s");
    if (seconds < 86400)
        return two(seconds / 3600, "h", seconds / 60 % 60, "m");
    return two(seconds / 86400, "d", seconds / 3600 % 24, "h");
}

std::string amount(uint64_t count) {
    auto scaled = [](double value, const char *unit) {
        char text[32];
        if (value < 9.95)
            std::snprintf(text, sizeof(text), "%.1f", value);
        else
            std::snprintf(text, sizeof(text), "%.0f", value);
        std::string result = text;
        if (result.ends_with(".0"))
            result.resize(result.size() - 2);
        return result + unit;
    };
    if (count < 1000)
        return std::to_string(count);
    if (count < 999500)
        return scaled(static_cast<double>(count) / 1e3, "k");
    if (count < 999500000)
        return scaled(static_cast<double>(count) / 1e6, "M");
    return scaled(static_cast<double>(count) / 1e9, "G");
}

std::string milliseconds(double value) {
    char text[32];
    if (value < 0.995)
        std::snprintf(text, sizeof(text), "%.2fms", value);
    else if (value < 9.95)
        std::snprintf(text, sizeof(text), "%.1fms", value);
    else
        std::snprintf(text, sizeof(text), "%.0fms", value);
    return text;
}

std::string version_text(const std::string &version) {
    auto text = version;
    if (auto at = text.find("(git "); at != std::string::npos)
        text.erase(at + 1, 4);
    return text;
}

std::string session_text(int64_t uptime, uint64_t frames) {
    return duration(uptime) + " · " + amount(frames) + (frames == 1 ? " frame" : " frames");
}

std::string frames_text(const Rows &stats, const Rows &frame_times) {
    if (stats.empty() || stats[0].size() < 3)
        return "";
    auto frames = integer(stats[0][0]), spent = integer(stats[0][1]), worst = integer(stats[0][2]);
    if (!frames || !spent || !worst || *frames <= 0)
        return "";
    std::string text = "avg " + milliseconds(*spent / 1e6 / static_cast<double>(*frames));
    std::vector<long long> times;
    for (size_t i = 1; i < frame_times.size(); ++i)
        if (auto time = frame_times[i].size() == 2 ? integer(frame_times[i][0]) : std::nullopt)
            times.push_back(*time);
    if (times.size() >= 100) {
        // The nearest rank: the frame 99 in a hundred took no longer than.
        size_t rank = (times.size() * 99 + 99) / 100 - 1;
        std::nth_element(times.begin(), times.begin() + static_cast<long>(rank), times.end());
        text += " · p99 " + milliseconds(static_cast<double>(times[rank]) / 1e3);
    }
    return text + " · worst " + milliseconds(*worst / 1e6);
}

std::string windows_text(const Rows &windows, bool tiling, const std::string &layout) {
    int count = 0;
    std::set<std::pair<std::string, std::string>> workspaces;
    for (const auto &row : windows) {
        if (row.size() < 13)
            continue;
        if (row[12] == "1" && row[2] == "1")
            continue; // hidden in the scratchpad, on no workspace
        ++count;
        workspaces.emplace(row[10], row[0]);
    }
    std::string text = count == 0
                           ? "none"
                           : std::to_string(count) + " on " + std::to_string(workspaces.size()) +
                                 (workspaces.size() == 1 ? " workspace" : " workspaces");
    return text + " · " + (tiling ? layout : "floating");
}

std::vector<std::string> monitors_text(const Rows &outputs) {
    std::vector<std::string> lines;
    for (const auto &row : outputs) {
        if (row.size() < 14)
            continue;
        const auto &name = row[0];
        if (row[1] != "1") {
            if (row[11] != "-")
                lines.push_back(name + " mirrors " + row[11]);
            continue;
        }
        std::string text = name;
        const auto &mode = row[8];
        auto at = mode.find('@');
        text += " " + mode.substr(0, at);
        if (auto refresh = at == std::string::npos ? std::nullopt : real(mode.substr(at + 1));
            refresh && *refresh > 0)
            text += "@" + std::to_string(std::lround(*refresh)) + "Hz";
        if (auto scale = real(row[6]); scale && std::abs(*scale - 1) > 1e-3) {
            char number[32];
            std::snprintf(number, sizeof(number), "%g", *scale);
            text += std::string(" ×") + number;
        }
        if (row[13] == "hdr")
            text += " HDR";
        else if (row[12] == "10")
            text += " 10-bit";
        if (row[10] == "off")
            text += " (off)";
        lines.push_back(text);
    }
    return lines;
}

std::string idle_text(const Rows &idle) {
    std::vector<std::string> parts;
    static const std::map<std::string, std::string> names = {
        {"dim", "dim"}, {"display_off", "screens off"}, {"lock", "lock"}, {"suspend", "sleep"}};
    for (const auto &row : idle) {
        if (row.size() >= 5 && row[0] == "state") {
            if (row.size() >= 8 && row[7] == "1")
                parts.push_back("kept awake");
            else if (row[2] == "1")
                parts.push_back("inhibited");
            if (integer(row[4]).value_or(0) > 0)
                parts.push_back("dimmed");
            if (row[3] == "1")
                parts.push_back("on battery");
        } else if (row.size() == 3 && names.contains(row[0])) {
            if (auto timeout = integer(row[1]); timeout && *timeout > 0)
                parts.push_back(names.at(row[0]) + " " + duration(*timeout));
        }
    }
    if (parts.empty())
        return idle.empty() ? "" : "never sleeps";
    std::string text;
    for (const auto &part : parts)
        text += (text.empty() ? "" : " · ") + part;
    return text;
}

std::string night_text(const Rows &night_light) {
    if (night_light.empty() || night_light[0].size() < 3)
        return "";
    auto kelvin = integer(night_light[0][0]).value_or(6500);
    if (kelvin < 6500)
        return std::to_string(kelvin) + "K";
    return night_light[0][2] == "1" ? "scheduled" : "";
}

std::string sessions_text(std::vector<std::string> names) {
    std::sort(names.begin(), names.end());
    std::string text;
    for (size_t i = 0; i < names.size() && i < 4; ++i)
        text += (i ? ", " : "") + names[i];
    if (names.size() > 4)
        text += " +" + std::to_string(names.size() - 4);
    return text;
}

std::optional<Launches> top_app(const std::string &file) {
    std::optional<Launches> top;
    long long top_last = 0;
    std::istringstream lines(file);
    for (std::string line; std::getline(lines, line);) {
        auto fields = cli::rows(line);
        if (fields.size() != 1 || fields[0].size() != 3 || fields[0][0].empty())
            continue;
        auto count = integer(fields[0][1]), last = integer(fields[0][2]);
        if (!count || !last || *count <= 0)
            continue;
        if (!top || *count > top->count || (*count == top->count && *last > top_last)) {
            top = Launches{fields[0][0], static_cast<int>(*count)};
            top_last = *last;
        }
    }
    return top;
}

std::string desktop_name(const std::string &entry) {
    std::istringstream lines(entry);
    bool inside = false;
    for (std::string line; std::getline(lines, line);) {
        if (line.starts_with('['))
            inside = line.starts_with("[Desktop Entry]");
        else if (inside && line.starts_with("Name=")) {
            auto name = line.substr(5);
            while (!name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
                name.pop_back();
            return name;
        }
    }
    return "";
}

std::string app_label(const std::string &id) {
    auto name = id;
    if (name.ends_with(".desktop"))
        name.resize(name.size() - 8);
    if (auto dot = name.rfind('.'); dot != std::string::npos && dot + 1 < name.size())
        name = name.substr(dot + 1);
    return name;
}

std::vector<std::string> logo(const Palette &palette, bool color) {
    // A pixel's colour: the gradient at its cell's middle, so that a cell's two pixels share it
    // and draw as one block, a highlight lighter, both kept apart from the background the
    // terminal is likely to have, the profile's.
    auto pixel = [&](int x, int y) -> std::optional<Rgb> {
        char c = paw_print[y][x];
        if (c == '.')
            return std::nullopt;
        Rgb base =
            readable(gradient_at(palette, (x + 0.5) / print_width, ((y & ~1) + 1.0) / print_height),
                     palette.background, 1.5);
        return c == '+' ? readable(mix(base, {255, 255, 255}, 0.45), palette.background, 1.25)
                        : base;
    };
    std::vector<std::string> lines;
    for (int y = 0; y + 1 < print_height; y += 2) {
        std::string line;
        std::optional<Rgb> shown, under; // the colours the line's escapes left set
        auto paint = [&](std::optional<Rgb> fg, std::optional<Rgb> bg) {
            if ((shown && !fg) || (under && !bg)) {
                line += reset;
                shown = under = std::nullopt;
            }
            if (fg && fg != shown)
                line += foreground(*fg);
            if (bg && bg != under)
                line += background(*bg);
            shown = fg;
            under = bg;
        };
        for (int x = 0; x < print_width; ++x) {
            auto top = pixel(x, y), bottom = pixel(x, y + 1);
            const char *cell = top && bottom ? "█" : top ? "▀" : bottom ? "▄" : " ";
            if (color) {
                if (top && bottom && top != bottom) {
                    paint(top, bottom);
                    cell = "▀";
                } else
                    paint(top ? top : bottom, std::nullopt);
            }
            line += cell;
        }
        if (shown || under)
            line += reset;
        lines.push_back(std::move(line));
    }
    return lines;
}

std::string render(const Palette &palette, const std::string &title,
                   const std::vector<Field> &fields, Style style) {
    Rgb plain = palette.border.empty() ? Rgb{128, 128, 128} : palette.border.front();
    Rgb label = readable(plain, palette.background, 3);
    Rgb heading =
        readable(palette.border.empty() ? plain : palette.border.back(), palette.background, 3);
    Rgb rule = readable(palette.inactive, palette.background, 1.5);
    auto paint = [&](const std::string &text, Rgb color, bool bold) {
        if (!style.color || text.empty())
            return text;
        return (bold ? "\x1b[1m" : "") + foreground(color) + text + reset;
    };
    struct Line {
        std::string text;
        size_t width;
    };
    std::vector<Line> lines;
    auto space = title.find(' ');
    auto first = title.substr(0, space),
         rest = space == std::string::npos ? "" : title.substr(space);
    lines.push_back({paint(first, heading, true) + rest, width(title)});
    std::string ruler;
    for (size_t i = 0; i < width(title); ++i)
        ruler += "─";
    lines.push_back({paint(ruler, rule, false), width(title)});
    size_t widest = 0;
    for (const auto &field : fields)
        widest = std::max(widest, width(field.label));
    for (const auto &field : fields) {
        auto name = field.label + std::string(widest - width(field.label) + 2, ' ');
        lines.push_back(
            {paint(field.label, label, true) + name.substr(field.label.size()) + field.value,
             widest + 2 + width(field.value)});
    }
    if (style.color && style.logo) {
        std::string strip;
        std::vector<Rgb> colors = palette.border;
        colors.push_back(palette.inactive);
        colors.push_back(palette.urgent);
        std::vector<Rgb> shown;
        for (auto color : colors)
            if (std::find(shown.begin(), shown.end(), color) == shown.end()) {
                shown.push_back(color);
                strip += foreground(color) + "███";
            }
        lines.push_back({"", 0});
        lines.push_back({strip + reset, shown.size() * 3});
    }
    std::vector<std::string> art;
    if (style.logo)
        art = logo(palette, style.color);
    const size_t gap = 3;
    if (style.columns > 0 && !art.empty()) {
        size_t needed = 0;
        for (const auto &line : lines)
            needed = std::max(needed, print_width + gap + line.width);
        if (needed > static_cast<size_t>(style.columns))
            art.clear();
    }
    std::string text;
    for (size_t i = 0; i < std::max(art.size(), lines.size()); ++i) {
        std::string row;
        if (!art.empty())
            row = i < art.size() ? art[i] : std::string(print_width, ' ');
        if (i < lines.size())
            row += (art.empty() ? "" : std::string(gap, ' ')) + lines[i].text;
        while (!row.empty() && row.back() == ' ')
            row.pop_back();
        text += row + '\n';
    }
    return text;
}

} // namespace paw::fetch

namespace paw::cli {
namespace {

using fetch::Field;
using fetch::Rows;

// A query's rows, or nothing when the session refuses it (an older compositor without it).
std::optional<Rows> ask(const std::string &name) {
    auto reply = request("get " + name);
    if (!reply.ok)
        return std::nullopt;
    return rows(reply.body);
}

std::string value_of(const Rows &rows, const std::string &name) {
    for (const auto &row : rows)
        if (row.size() >= 2 && row[0] == name)
            return row[1];
    return "";
}

std::string read_text(const std::filesystem::path &path) {
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

// The name an application's desktop entry gives it, looked for where the XDG specification says.
std::string application_name(const std::string &id) {
    std::vector<std::filesystem::path> directories;
    if (const auto *home = std::getenv("XDG_DATA_HOME"); home && *home == '/')
        directories.emplace_back(home);
    else if (const auto *user = std::getenv("HOME"); user && *user)
        directories.push_back(std::filesystem::path(user) / ".local/share");
    std::string system = "/usr/local/share:/usr/share";
    if (const auto *dirs = std::getenv("XDG_DATA_DIRS"); dirs && *dirs)
        system = dirs;
    std::stringstream list(system);
    for (std::string directory; std::getline(list, directory, ':');)
        if (!directory.empty())
            directories.emplace_back(directory);
    for (const auto &directory : directories) {
        std::error_code failure;
        auto path = directory / "applications" / id;
        if (std::filesystem::is_regular_file(path, failure))
            if (auto name = fetch::desktop_name(read_text(path)); !name.empty())
                return name;
    }
    return fetch::app_label(id);
}

} // namespace

int fetch(int argc, char **argv) {
    fetch::Style style;
    std::filesystem::path chosen;
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--no-color" || arg == "--no-colour")
            style.color = false;
        else if (arg == "--no-logo")
            style.logo = false;
        else if (arg == "--config" && i + 1 < argc)
            chosen = argv[++i];
        else
            throw std::runtime_error("usage: paw fetch [--no-logo] [--no-color] [--config PATH]");
    }
    // https://no-color.org: any value but an empty one.
    if (const auto *no_color = std::getenv("NO_COLOR"); no_color && *no_color)
        style.color = false;
    if (!isatty(STDOUT_FILENO))
        style.color = false;
    else if (winsize size{}; ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0)
        style.columns = size.ws_col;

    std::string wlroots;
#ifdef WLR_VERSION_STR
    wlroots = WLR_VERSION_STR;
#endif
    std::string title = "paw " + fetch::version_text(PAW_VERSION);
    if (!wlroots.empty())
        title += " · wlroots " + wlroots;

    // The session answers for itself; outside one, or from a compositor that does not know the
    // queries, the configuration and the state files stand in.
    std::optional<Rows> about;
    bool session = true;
    try {
        if (auto reply = request("get about"); reply.ok)
            about = rows(reply.body);
    } catch (const SessionUnreachable &) {
        session = false;
    }
    std::string profile;
    auto defaults = fetch::palette_of(Config{}.settings);
    std::optional<fetch::Palette> palette;
    if (about)
        profile = value_of(*about, "profile");
    if (auto reply = session ? ask("palette") : std::nullopt)
        palette = fetch::palette_of(*reply, defaults);
    if (!palette) {
        // A configuration with an error gives paw's own colours, not those of another one.
        try {
            auto config = load_config(chosen.empty() ? configuration_path() : chosen);
            palette = fetch::palette_of(config.settings);
            if (!about)
                profile = config.profile;
        } catch (const std::exception &) {
            palette = defaults;
            if (!about)
                profile = saved_profile();
        }
    }

    std::vector<Field> fields;
    if (!profile.empty())
        fields.push_back(
            {"profile", profile + (palette->scheme.empty() ? "" : " · " + palette->scheme)});
    if (!session)
        fields.push_back({"session", "not in a paw session"});
    else {
        auto stats = ask("stats");
        uint64_t frames = 0;
        if (stats && !stats->empty() && !(*stats)[0].empty())
            frames = std::strtoull((*stats)[0][0].c_str(), nullptr, 10);
        if (auto uptime = about ? value_of(*about, "uptime") : ""; !uptime.empty())
            fields.push_back({"session", fetch::session_text(
                                             std::strtoll(uptime.c_str(), nullptr, 10), frames)});
        else if (stats)
            fields.push_back(
                {"session", fetch::amount(frames) + (frames == 1 ? " frame" : " frames")});
        if (auto times = ask("frame_times"); stats)
            if (auto text = fetch::frames_text(*stats, times.value_or(Rows{})); !text.empty())
                fields.push_back({"frames", text});
        if (auto windows = ask("windows")) {
            auto tiling = ask("tiling");
            auto layout = ask("layout");
            bool tiles = tiling && !tiling->empty() && (*tiling)[0][0] == "on";
            fields.push_back(
                {"windows",
                 fetch::windows_text(*windows, tiles,
                                     layout && !layout->empty() ? (*layout)[0][0] : "tiling")});
        }
        if (auto outputs = ask("outputs")) {
            auto monitors = fetch::monitors_text(*outputs);
            for (size_t i = 0; i < monitors.size(); ++i)
                fields.push_back({i                      ? ""
                                  : monitors.size() == 1 ? "monitor"
                                                         : "monitors",
                                  monitors[i]});
        }
        if (auto idle = ask("idle"))
            if (auto text = fetch::idle_text(*idle); !text.empty())
                fields.push_back({"idle", text});
        if (auto night = ask("night_light"))
            if (auto text = fetch::night_text(*night); !text.empty())
                fields.push_back({"night light", text});
        if (auto mode = ask("mode"); mode && !mode->empty() && (*mode)[0][0] != "default")
            fields.push_back({"mode", (*mode)[0][0]});
        if (!about)
            fields.push_back({"running", "a paw older than paw fetch"});
        else {
            auto version = value_of(*about, "version");
            auto library = value_of(*about, "wlroots");
            if ((!version.empty() && version != PAW_VERSION) ||
                (!library.empty() && !wlroots.empty() && library != wlroots))
                fields.push_back(
                    {"running", "paw " + fetch::version_text(version) + " · wlroots " + library});
        }
    }
    auto state = state_directory();
    if (!state.empty()) {
        if (auto top = fetch::top_app(read_text(state / "launches")))
            fields.push_back({"top app", application_name(top->id) + " · " +
                                             std::to_string(top->count) +
                                             (top->count == 1 ? " launch" : " launches")});
        std::vector<std::string> names;
        std::error_code failure;
        // Not paw showcase's own, saved while it runs.
        for (const auto &entry : std::filesystem::directory_iterator(state / "sessions", failure))
            if (auto name = entry.path().filename().string();
                entry.is_regular_file(failure) && !name.starts_with("showcase-"))
                names.push_back(name);
        if (!names.empty())
            fields.push_back({"sessions", fetch::sessions_text(names)});
    }
    std::cout << fetch::render(*palette, title, fields, style);
    return 0;
}

} // namespace paw::cli
