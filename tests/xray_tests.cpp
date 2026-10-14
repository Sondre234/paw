// SPDX-License-Identifier: GPL-3.0-or-later
/* `paw xray --list`: get xray's rows printed output by output from the top down, lined up, with
 * long names cut and the view's state at the end. */
#include "check.h"
#include "paw/cli.hpp"

#include <string>

namespace {

std::vector<std::string> row(const std::string &output, const std::string &layer,
                             const std::string &kind, const std::string &name,
                             const std::string &title, const std::string &place) {
    // place: "x y width height"
    std::vector<std::string> fields{output, layer, "#5b8def", kind, name, title};
    size_t start = 0;
    for (int i = 0; i < 4; ++i) {
        size_t end = place.find(' ', start);
        fields.push_back(place.substr(start, end - start));
        start = end + 1;
    }
    for (const char *rest : {"-1", "0", "0", "0", "0"})
        fields.emplace_back(rest);
    return fields;
}

} // namespace

int main() {
    std::vector<std::vector<std::string>> rows{
        {"closed 0 - -1"},
        row("DP-1", "background", "colour", "paw", "#1f2430", "0 0 1920 1080"),
        row("DP-1", "background", "surface", "paw-desktop", "", "0 0 1920 1080"),
        row("DP-1", "windows", "window", "kitty", "~/dev", "100 80 800 600"),
        row("DP-1", "top", "surface", "paw-panel", "", "0 1040 1920 40"),
        row("HDMI-A-1", "background", "colour", "paw", "#1f2430", "1920 0 1280 1024"),
    };
    auto text = paw::cli::xray_list(rows, false);
    const std::string want = "DP-1, top first:\n"
                             "  top         surface  paw-panel      1920\u00d740 at 0,1040\n"
                             "  windows     window   kitty \u2014 ~/dev  800\u00d7600 at 100,80\n"
                             "  background  surface  paw-desktop    1920\u00d71080 at 0,0\n"
                             "  background  colour   paw \u2014 #1f2430  1920\u00d71080 at 0,0\n"
                             "\n"
                             "HDMI-A-1, top first:\n"
                             "  background  colour   paw \u2014 #1f2430  1280\u00d71024 at 1920,0\n";
    CHECK(text == want, "got:\n%s", text.c_str());

    // A long title is cut, a character at a time, and the view's state follows when it is up.
    rows = {{"open 1000 DP-1 2"},
            row("DP-1", "windows", "window", "firefox",
                std::string(30, 'x') + "é" + std::string(60, 'y'), "0 0 10 10")};
    text = paw::cli::xray_list(rows, false);
    CHECK(text.find("firefox — " + std::string(30, 'x') + "é" + std::string(22, 'y') +
                    "…  10×10 at 0,0") != std::string::npos,
          "got:\n%s", text.c_str());
    CHECK(text.find("\nThe x-ray view is open on DP-1.\n") != std::string::npos, "got:\n%s",
          text.c_str());

    // In colour, a square of the layer's before each line.
    text = paw::cli::xray_list(rows, true);
    CHECK(text.find("  \033[38;2;91;141;239m■\033[0m windows") != std::string::npos,
          "got:\n%s", text.c_str());

    // Rows too short, as from a newer compositor's other lines, are passed over.
    CHECK(paw::cli::xray_list({{"closed 0 - -1"}, {"x", "y"}}, false).empty());
    return failures != 0;
}
