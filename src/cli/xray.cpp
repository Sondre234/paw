// SPDX-License-Identifier: GPL-3.0-or-later
/* `paw xray` opens or closes the exploded view of the focused output's layers in the running
 * session, as the xray action does; `paw xray --list` prints what each output draws, layer by
 * layer from the top down, from `get xray`: which surface or window lies over which. */
#include "paw/cli.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

namespace paw::cli {

namespace {

// Characters, not bytes, for lining up columns of UTF-8 text.
size_t length(const std::string &text) {
    return static_cast<size_t>(
        std::count_if(text.begin(), text.end(), [](char c) { return (c & 0xC0) != 0x80; }));
}

// At most `most` characters of `text`, an ellipsis standing for the rest.
std::string shorten(const std::string &text, size_t most) {
    if (length(text) <= most)
        return text;
    size_t kept = 0, end = 0;
    while (end < text.size()) {
        size_t next = end + 1;
        while (next < text.size() && (text[next] & 0xC0) == 0x80)
            ++next;
        if (++kept > most - 1)
            break;
        end = next;
    }
    return text.substr(0, end) + "…";
}

std::string pad(const std::string &text, size_t width) {
    size_t have = length(text);
    return have >= width ? text : text + std::string(width - have, ' ');
}

// A square in the colour #rrggbb, for a terminal that shows 24-bit colour.
std::string swatch(const std::string &hex) {
    if (hex.size() != 7 || hex[0] != '#')
        return "  ";
    unsigned long rgb = std::stoul(hex.substr(1), nullptr, 16);
    return "\033[38;2;" + std::to_string(rgb >> 16 & 0xff) + ";" +
           std::to_string(rgb >> 8 & 0xff) + ";" + std::to_string(rgb & 0xff) + "m■\033[0m ";
}

} // namespace

std::string xray_list(const std::vector<std::vector<std::string>> &rows, bool colour) {
    struct Entry {
        std::string layer, colour, kind, what, place;
    };
    std::vector<std::pair<std::string, std::vector<Entry>>> outputs;
    size_t layer_width = 0, kind_width = 0, what_width = 0;
    // The first row is the view's state; each other one something an output draws, bottom up:
    // output, layer, colour, kind, name, title, x, y, width, height, then the sheet.
    for (size_t i = 1; i < rows.size(); ++i) {
        const auto &row = rows[i];
        if (row.size() < 10)
            continue;
        Entry entry{row[1], row[2], row[3], row[4].empty() ? "-" : row[4],
                    row[8] + "×" + row[9] + " at " + row[6] + "," + row[7]};
        if (!row[5].empty())
            entry.what += " — " + row[5];
        entry.what = shorten(entry.what, 64);
        layer_width = std::max(layer_width, length(entry.layer));
        kind_width = std::max(kind_width, length(entry.kind));
        what_width = std::max(what_width, length(entry.what));
        if (outputs.empty() || outputs.back().first != row[0])
            outputs.emplace_back(row[0], std::vector<Entry>{});
        outputs.back().second.push_back(std::move(entry));
    }
    std::string text;
    for (const auto &[output, entries] : outputs) {
        text += (text.empty() ? "" : "\n") + output + ", top first:\n";
        for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry)
            text += "  " + (colour ? swatch(entry->colour) : std::string()) +
                    pad(entry->layer, layer_width) + "  " + pad(entry->kind, kind_width) + "  " +
                    pad(entry->what, what_width) + "  " + entry->place + "\n";
    }
    if (!rows.empty() && !rows[0].empty() && rows[0][0].rfind("closed", 0) != 0) {
        // "open 1000 HEADLESS-1 2": the view is up on that output.
        auto state = rows[0][0];
        auto first = state.find(' '), second = state.find(' ', first + 1);
        auto third = state.find(' ', second + 1);
        if (third != std::string::npos)
            text += "\nThe x-ray view is " + state.substr(0, first) + " on " +
                    state.substr(second + 1, third - second - 1) + ".\n";
    }
    return text;
}

int xray(int argc, char **argv) {
    bool list = false;
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--list" || arg == "-l")
            list = true;
        else
            throw std::runtime_error("usage: paw xray [--list]");
    }
    if (list) {
        std::cout << xray_list(query("xray"), isatty(STDOUT_FILENO));
        return 0;
    }
    auto reply = request("xray");
    if (!reply.ok) {
        std::cerr << "paw: " << reply.body;
        return 1;
    }
    return 0;
}

} // namespace paw::cli
