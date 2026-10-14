// SPDX-License-Identifier: GPL-3.0-or-later
/* `paw heatmap`: the heatmap action in the running session (`paw heatmap [toggle|motion|clicks|
 * reset]`), and where the pointer has been, from `get heatmap`, in the terminal (`--term`): every
 * output in the layout at its place, on one scale. */
#include "paw/cli.hpp"
#include "paw/heatmap.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <sys/ioctl.h>
#include <unistd.h>

namespace paw::cli {
namespace {

constexpr const char *usage_text =
    "usage: paw heatmap [toggle|motion|clicks|reset] | paw heatmap --term [--clicks] "
    "[--output NAME]";

/* `words` with `separator` between them. */
std::string joined(const std::vector<std::string> &words, const char *separator) {
    std::string text;
    for (const auto &word : words)
        text += (text.empty() ? "" : separator) + word;
    return text;
}

/* A number of `get heatmap`'s reply; throws for anything else. */
int number(const std::string &text) {
    std::size_t used = 0;
    int value = 0;
    try {
        value = std::stoi(text, &used);
    } catch (const std::logic_error &) {
    }
    if (used != text.size() || text.empty())
        throw std::runtime_error("get heatmap: not a number: " + text);
    return value;
}

/* The colour of the scale at `level` laid over `under` (0 to 255 each), as the terminal shows
 * it. */
std::array<int, 3> over(double level, std::array<int, 3> under) {
    auto color = sh_heat_color(level);
    auto mix = [&](double channel, int below) {
        return static_cast<int>(std::lround(channel * 255 * color.a + below * (1 - color.a)));
    };
    return {mix(color.r, under[0]), mix(color.g, under[1]), mix(color.b, under[2])};
}

/* What the inside of an output shows where nothing was counted, in the terminal. */
constexpr std::array<int, 3> screen{30, 33, 40};

/* `field` and `inside` resampled to `columns` by `rows`. */
struct Resampled {
    std::vector<float> field, inside;
    float most = 0;
};
Resampled resample(const Heatmap &heatmap, int columns, int rows) {
    Resampled out{std::vector<float>(static_cast<std::size_t>(columns) * rows),
                  std::vector<float>(static_cast<std::size_t>(columns) * rows)};
    std::vector<float> scratch(static_cast<std::size_t>(columns) * heatmap.rows);
    sh_heat_resample(heatmap.field.data(), heatmap.columns, heatmap.rows, out.field.data(), columns,
                     rows, scratch.data());
    sh_heat_resample(heatmap.inside.data(), heatmap.columns, heatmap.rows, out.inside.data(),
                     columns, rows, scratch.data());
    out.most = sh_heat_peak(out.field.data(), out.field.size());
    return out;
}

/* The escape that sets the foreground (`layer` 38) or the background (48) to `color`. */
std::string sgr(int layer, std::array<int, 3> color) {
    return "\x1b[" + std::to_string(layer) + ";2;" + std::to_string(color[0]) + ';' +
           std::to_string(color[1]) + ';' + std::to_string(color[2]) + 'm';
}

/* A shade for `level` without colour, as the colours go: none where they are faint, light for
 * blue and cyan, medium for green, dark for yellow and red, full for red to white. */
std::string_view shade(double level) {
    static constexpr struct {
        double below;
        std::string_view shade;
    } shades[] = {{0.1, " "}, {0.45, "░"}, {0.65, "▒"}, {0.85, "▓"}};
    for (const auto &[below, shade] : shades) {
        if (!(level >= below))
            return shade;
    }
    return "█";
}

/* The line under the picture: the scale from cold to hot, the hottest cell's count at its end,
 * what the picture shows and of which outputs. */
std::string legend(const Heatmap &heatmap, bool colour) {
    std::string line = "0 ";
    for (int i = 0; i < 24; ++i) {
        double level = (i + 0.5) / 24;
        if (colour)
            line += sgr(48, over(level, screen)) + ' ';
        else
            line += shade(level);
    }
    char label[32];
    sh_heat_label(label, sizeof(label), heatmap.clicks ? SH_HEAT_CLICKS : SH_HEAT_MOTION,
                  heatmap.hottest);
    line += (colour ? "\x1b[0m " : " ") + std::string(label) + "  " +
            (heatmap.clicks ? "clicks" : "pointer rest") + " on " + joined(heatmap.outputs, ", ");
    return line + '\n';
}

/* The terminal's size in characters: its own, else $COLUMNS and $LINES, else 80 by 24. */
std::pair<int, int> terminal_size() {
    winsize size{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0 && size.ws_row > 0)
        return {size.ws_col, size.ws_row};
    auto from = [](const char *name, int fallback) {
        const char *value = std::getenv(name);
        int number = value ? std::atoi(value) : 0;
        return number > 0 ? number : fallback;
    };
    return {from("COLUMNS", 80), from("LINES", 24)};
}

/* Sends `line` and throws with the compositor's reason if it refuses. */
void send(const std::string &line) {
    auto reply = request(line);
    if (reply.ok)
        return;
    auto why = reply.body;
    while (!why.empty() && why.back() == '\n')
        why.pop_back();
    if (why.starts_with("error: "))
        why.erase(0, 7);
    throw std::runtime_error(why);
}

} // namespace

Heatmap heatmap_from(const std::vector<std::vector<std::string>> &rows) {
    struct Part {
        std::string name;
        int columns, rows, x, y, width, height;
        std::size_t first; // its first row of cells in `rows`
    };
    std::vector<Part> parts;
    Heatmap heatmap;
    for (std::size_t i = 0; i < rows.size();) {
        const auto &header = rows[i];
        if (header.size() != 9)
            throw std::runtime_error("get heatmap: unexpected line: " + joined(header, " "));
        Part part{header[0],         number(header[2]), number(header[3]), number(header[5]),
                  number(header[6]), number(header[7]), number(header[8]), i + 1};
        if (number(header[4]) != SH_HEAT_CELL || part.columns < 1 || part.rows < 1)
            throw std::runtime_error("get heatmap: unexpected grid for " + part.name);
        heatmap.clicks = header[1] == "clicks";
        if (i + 1 + part.rows > rows.size())
            throw std::runtime_error("get heatmap: " + part.name + "'s rows are cut short");
        parts.push_back(part);
        i += 1 + part.rows;
    }
    if (parts.empty())
        return heatmap;
    // Named from left to right, and top to bottom.
    std::sort(parts.begin(), parts.end(), [](const Part &a, const Part &b) {
        return std::pair(a.x, a.y) < std::pair(b.x, b.y);
    });
    for (const auto &part : parts)
        heatmap.outputs.push_back(part.name);
    int left = parts[0].x, top = parts[0].y, right = left, bottom = top;
    for (const auto &part : parts) {
        left = std::min(left, part.x);
        top = std::min(top, part.y);
        right = std::max(right, part.x + part.width);
        bottom = std::max(bottom, part.y + part.height);
    }
    heatmap.width = right - left;
    heatmap.height = bottom - top;
    heatmap.columns = sh_heat_cells(heatmap.width);
    heatmap.rows = sh_heat_cells(heatmap.height);
    auto cells = static_cast<std::size_t>(heatmap.columns) * heatmap.rows;
    heatmap.field.assign(cells, 0);
    heatmap.inside.assign(cells, 0);
    for (const auto &part : parts) {
        std::vector<uint32_t> counts(static_cast<std::size_t>(part.columns) * part.rows);
        for (int row = 0; row < part.rows; ++row) {
            const auto &values = rows[part.first + row];
            if (values.size() != static_cast<std::size_t>(part.columns))
                throw std::runtime_error("get heatmap: a row of " + part.name + " has " +
                                         std::to_string(values.size()) + " cells, not " +
                                         std::to_string(part.columns));
            for (int column = 0; column < part.columns; ++column) {
                auto count = std::strtoull(values[column].c_str(), nullptr, 10);
                counts[static_cast<std::size_t>(row) * part.columns + column] =
                    static_cast<uint32_t>(std::min<unsigned long long>(count, UINT32_MAX));
            }
        }
        sh_heat_grid grid{part.columns, part.rows, {}};
        auto kind = heatmap.clicks ? SH_HEAT_CLICKS : SH_HEAT_MOTION;
        grid.cells[kind] = counts.data();
        heatmap.hottest = std::max<double>(heatmap.hottest, sh_heat_most(&grid, kind));
        std::vector<float> field(counts.size()),
            scratch(static_cast<std::size_t>(std::max(part.columns, part.rows)));
        sh_heat_field(&grid, kind, field.data(), scratch.data());
        // Its place, to the nearest cell.
        int x0 = (part.x - left + SH_HEAT_CELL / 2) / SH_HEAT_CELL;
        int y0 = (part.y - top + SH_HEAT_CELL / 2) / SH_HEAT_CELL;
        for (int row = 0; row < part.rows && y0 + row < heatmap.rows; ++row) {
            for (int column = 0; column < part.columns && x0 + column < heatmap.columns; ++column) {
                auto at = static_cast<std::size_t>(y0 + row) * heatmap.columns + x0 + column;
                heatmap.field[at] = field[static_cast<std::size_t>(row) * part.columns + column];
                heatmap.inside[at] = 1;
            }
        }
    }
    return heatmap;
}

std::string heatmap_text(const Heatmap &heatmap, int columns, int lines, bool colour) {
    if (heatmap.columns < 1 || heatmap.rows < 1)
        return "No output in the layout\n";
    // A character is twice as tall as it is wide: half-blocks show two square pixels in one,
    // a shade one pixel as tall as the character. The legend and the prompt take two lines.
    double room = std::max(1, lines - 2) * 2.0;
    double scale = std::min(static_cast<double>(columns) / heatmap.columns, room / heatmap.rows);
    int width = std::max(1, static_cast<int>(std::lround(heatmap.columns * scale)));
    int height =
        std::max(1, static_cast<int>(std::lround(heatmap.rows * scale / (colour ? 1 : 2))));
    width = std::min(width, columns);
    height += colour && height % 2; // whole characters
    auto picture = resample(heatmap, width, height);
    std::string text;
    auto at = [&](int x, int y) { return static_cast<std::size_t>(y) * width + x; };
    if (!colour) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x)
                text += shade(sh_heat_level(picture.field[at(x, y)], picture.most));
            while (!text.empty() && text.back() == ' ')
                text.pop_back();
            text += '\n';
        }
        return text + legend(heatmap, false);
    }
    // A pixel's colour, or none outside every output, where the terminal's own shows.
    auto pixel = [&](int x, int y) -> std::optional<std::array<int, 3>> {
        if (y >= height || picture.inside[at(x, y)] < 0.5F)
            return std::nullopt;
        return over(sh_heat_level(picture.field[at(x, y)], picture.most), screen);
    };
    for (int y = 0; y < height; y += 2) {
        std::string foreground, background;
        for (int x = 0; x < width; ++x) {
            auto top = pixel(x, y), bottom = pixel(x, y + 1);
            // The upper half in the foreground and the lower in the background; with only one,
            // that half in the foreground over the terminal's background.
            std::string fg = top ? sgr(38, *top) : bottom ? sgr(38, *bottom) : "";
            std::string bg = top && bottom ? sgr(48, *bottom) : "\x1b[49m";
            if (bg != background)
                text += background = bg;
            if (!fg.empty() && fg != foreground)
                text += foreground = fg;
            text += top ? "▀" : bottom ? "▄" : " ";
        }
        text += "\x1b[0m\n";
    }
    return text + legend(heatmap, true);
}

int heatmap(int argc, char **argv) {
    std::string action, output;
    bool term = false, clicks = false;
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--term")
            term = true;
        else if (arg == "--clicks")
            clicks = true;
        else if (arg == "--output" && i + 1 < argc)
            output = argv[++i];
        else if (action.empty() &&
                 (arg == "toggle" || arg == "motion" || arg == "clicks" || arg == "reset"))
            action = arg;
        else
            throw std::runtime_error(usage_text);
    }
    if (term ? !action.empty() : clicks || !output.empty())
        throw std::runtime_error(usage_text);
    if (!term) {
        send("heatmap " + (action.empty() ? "toggle" : action));
        return 0;
    }
    auto heatmap = heatmap_from(
        query("heatmap" + (output.empty() ? "" : " " + output) + (clicks ? " clicks" : "")));
    auto [columns, lines] = terminal_size();
    const char *no_color = std::getenv("NO_COLOR");
    std::cout << heatmap_text(heatmap, columns, lines, !(no_color && *no_color));
    return 0;
}

} // namespace paw::cli
