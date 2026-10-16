// SPDX-License-Identifier: GPL-3.0-or-later
// power.lock_style: the stock swaylock given the appearance profile's look.
#include "paw/lock_style.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>
#include <unistd.h>

namespace paw {
namespace {
struct Rgba {
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

int hex_digit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// "#RRGGBB" or "#RRGGBBAA", as the configuration checked it; black for anything else.
Rgba parse(std::string_view text) {
    if (text.starts_with('#'))
        text.remove_prefix(1);
    if (text.size() != 6 && text.size() != 8)
        return {};
    std::array<uint8_t, 4> channels{0, 0, 0, 255};
    for (size_t i = 0; i < text.size() / 2; ++i) {
        int high = hex_digit(text[2 * i]), low = hex_digit(text[2 * i + 1]);
        if (high < 0 || low < 0)
            return {};
        channels[i] = static_cast<uint8_t>(high * 16 + low);
    }
    return {channels[0], channels[1], channels[2], channels[3]};
}

uint8_t byte(float value) {
    return static_cast<uint8_t>(std::clamp(value, 0.0F, 1.0F) * 255 + 0.5F);
}

// A premultiplied colour of the compositor's settings, straight.
Rgba unpremultiplied(const float color[4]) {
    if (color[3] <= 0)
        return {0, 0, 0, 0};
    return {byte(color[0] / color[3]), byte(color[1] / color[3]), byte(color[2] / color[3]),
            byte(color[3])};
}

// rrggbbaa, as swaylock takes a colour.
std::string hex(Rgba color) {
    char text[9];
    std::snprintf(text, sizeof(text), "%02x%02x%02x%02x", color.r, color.g, color.b, color.a);
    return text;
}

// How far apart two colours look, roughly: the sum of their channels' differences.
int distance(Rgba a, Rgba b) {
    return std::abs(a.r - b.r) + std::abs(a.g - b.g) + std::abs(a.b - b.b);
}

// Below this distance a key press's highlight would hardly show on the ring.
constexpr int distinct = 64;
// The indicator's inside is the panel's colour, no more see-through than the shell's glass lets
// a surface be (Theme.glassFloor), so that its text reads over a busy wallpaper.
constexpr uint8_t inside_floor = 191;
} // namespace

const Command &stock_lock_command() {
    static const Command command{"swaylock", "-f"};
    return command;
}

std::filesystem::path lock_wallpaper(const Config &config, const std::filesystem::path &config_path,
                                     std::string_view picked_wallpapers) {
    const std::string &configured = config.shell.wallpaper;
    // As the shell reads the file: the last line for the profile counts, and only while the
    // profile configures what it did when the wallpaper was picked.
    std::optional<std::string> picked;
    while (!picked_wallpapers.empty()) {
        auto end = picked_wallpapers.find('\n');
        auto line = picked_wallpapers.substr(0, end);
        picked_wallpapers.remove_prefix(end == std::string_view::npos ? picked_wallpapers.size()
                                                                      : end + 1);
        auto first = line.find('\t');
        if (first == std::string_view::npos)
            continue;
        auto second = line.find('\t', first + 1);
        if (second == std::string_view::npos || line.find('\t', second + 1) != line.npos ||
            second + 1 == line.size())
            continue;
        if (line.substr(0, first) != config.profile)
            continue;
        if (line.substr(first + 1, second - first - 1) == configured)
            picked = std::string(line.substr(second + 1));
        else
            picked.reset();
    }
    if (picked)
        return *picked;
    if (configured.empty())
        return {};
    std::filesystem::path file(configured);
    if (file.is_relative()) {
        std::error_code error;
        auto directory = std::filesystem::absolute(config_path, error).parent_path();
        file = directory / file;
    }
    return file;
}

Command lock_style_arguments(const Config &config, const LockStyleContext &context) {
    const auto &shell = config.shell;
    const auto &settings = config.settings;
    Rgba inside = parse(shell.panel_color);
    inside.a = std::max(inside.a, inside_floor);
    const Rgba ring = parse(shell.accent), text = parse(shell.text_color);
    const Rgba urgent = unpremultiplied(settings.urgent_color);
    const Rgba none{0, 0, 0, 0};
    // A key press lights a piece of the ring in the border's colour: the stop of its gradient
    // that stands out most from the ring (the accent is often the first stop too), or the text's
    // colour when none does.
    Rgba key = text;
    int best = -1;
    const auto &gradient = settings.border_active_gradient;
    for (int i = 0; i < std::clamp(gradient.count, 1, int(SH_GRADIENT_STOPS)); ++i) {
        Rgba stop = unpremultiplied(gradient.stops[i]);
        if (distance(stop, ring) > best) {
            best = distance(stop, ring);
            key = stop;
        }
    }
    if (best < distinct)
        key = text;
    Command arguments;
    auto add = [&](std::initializer_list<const char *> options, Rgba color) {
        for (const char *option : options)
            arguments.insert(arguments.end(), {option, hex(color)});
    };
    auto wallpaper = lock_wallpaper(config, context.config_path, context.picked_wallpapers);
    if (!wallpaper.empty() && context.readable && context.readable(wallpaper))
        // ":" first: no output named, and a colon in the path stays in it.
        if (auto escaped = wordexp_escape(wallpaper.string()))
            arguments.insert(arguments.end(), {"--image", ":" + *escaped, "--scaling", "fill"});
    Rgba background{byte(settings.background[0]), byte(settings.background[1]),
                    byte(settings.background[2]), 255};
    add({"--color"}, background);
    if (!shell.font.empty())
        arguments.insert(arguments.end(), {"--font", shell.font});
    arguments.insert(arguments.end(), {"--indicator-idle-visible", "--show-failed-attempts"});
    add({"--inside-color", "--inside-clear-color", "--inside-caps-lock-color",
         "--inside-ver-color", "--inside-wrong-color", "--layout-bg-color"},
        inside);
    add({"--ring-color", "--ring-clear-color", "--ring-caps-lock-color", "--ring-ver-color"},
        ring);
    add({"--ring-wrong-color", "--bs-hl-color", "--caps-lock-bs-hl-color"}, urgent);
    add({"--key-hl-color", "--caps-lock-key-hl-color"}, key);
    add({"--text-color", "--text-clear-color", "--text-caps-lock-color", "--text-ver-color",
         "--text-wrong-color", "--layout-text-color"},
        text);
    // No outlines: the ring alone, as the shell draws its controls.
    add({"--line-color", "--line-clear-color", "--line-caps-lock-color", "--line-ver-color",
         "--line-wrong-color", "--separator-color", "--layout-border-color"},
        none);
    return arguments;
}

Command lock_command(const Config &config, const LockStyleContext &context) {
    Command command = config.power.lock_command;
    if (!config.power.lock_style || command != stock_lock_command() || context.swaylock_config)
        return command;
    auto arguments = lock_style_arguments(config, context);
    command.insert(command.end(), arguments.begin(), arguments.end());
    return command;
}

LockStyleContext lock_style_context(const std::filesystem::path &config_path) {
    LockStyleContext context;
    context.config_path = config_path;
    if (auto state = profile_state_path(); !state.empty()) {
        std::ifstream file(state.parent_path() / "wallpapers");
        context.picked_wallpapers.assign(std::istreambuf_iterator<char>(file), {});
    }
    // Where swaylock looks, in its order.
    const char *home = std::getenv("HOME"), *config_home = std::getenv("XDG_CONFIG_HOME");
    std::string home_directory = home ? home : "";
    std::string swaylock_home = config_home && *config_home ? std::string(config_home)
                                                            : home_directory + "/.config";
    for (const auto &path : {home_directory + "/.swaylock/config",
                             swaylock_home + "/swaylock/config",
                             std::string("/etc/swaylock/config")})
        if (access(path.c_str(), R_OK) == 0)
            context.swaylock_config = true;
    context.readable = [](const std::filesystem::path &path) {
        std::error_code error;
        return access(path.c_str(), R_OK) == 0 && std::filesystem::is_regular_file(path, error);
    };
    return context;
}

std::optional<std::string> wordexp_escape(std::string_view path) {
    std::string escaped;
    escaped.reserve(path.size() * 2);
    for (char c : path) {
        if (c == '\n' || c == '\0')
            return std::nullopt;
        auto u = static_cast<unsigned char>(c);
        // Letters, digits and what wordexp takes as it is pass; so do bytes past ASCII, parts of
        // UTF-8 characters. Anything else a backslash keeps from being expanded or split.
        bool plain = u >= 0x80 || (u >= '0' && u <= '9') || (u >= 'a' && u <= 'z') ||
                     (u >= 'A' && u <= 'Z') || std::strchr("/._-+,:@%=", c);
        if (!plain)
            escaped += '\\';
        escaped += c;
    }
    return escaped;
}
} // namespace paw
