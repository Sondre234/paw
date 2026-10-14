// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* What `paw fetch` (src/cli/fetch.cpp) draws, apart from asking the session and reading files:
 * the palette it draws in, the paw print, the fields' text made from the session's replies, and
 * all of it side by side. */
#include "paw/backend.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace paw::fetch {

struct Rgb {
    int r = 0, g = 0, b = 0;
    bool operator==(const Rgb &) const = default;
};

// The colours of the appearance in use that paw fetch draws in.
struct Palette {
    std::string scheme; // "light" or "dark", as appearance.color_scheme; "" for neither
    Rgb background;
    std::vector<Rgb> border; // the focused window's border: a colour, or a gradient's stops
    double angle = 0;        // the gradient's, as windows.border_color gives it
    Rgb inactive, urgent;
};

// A query's reply as paw::cli::rows splits it.
using Rows = std::vector<std::vector<std::string>>;

// #RRGGBB or #RRGGBBAA, its alpha left out.
std::optional<Rgb> parse_color(const std::string &text);
// The palette of a configuration's settings (premultiplied colours).
Palette palette_of(const sh_settings &settings);
// The palette `get palette` replied, with what it leaves out as in `fallback`.
Palette palette_of(const Rows &reply, Palette fallback);

// WCAG's contrast ratio between two colours, from 1 to 21.
double contrast(Rgb a, Rgb b);
// `color` taken toward black or white, whichever stands out more from `background`, just far
// enough for a contrast of `ratio`.
Rgb readable(Rgb color, Rgb background, double ratio);
// The border's gradient at (u, v) of a square from (0, 0) at the top left to (1, 1), laid along
// its angle as the border draws it: 0 runs from left to right, 90 from top to bottom.
Rgb gradient_at(const Palette &palette, double u, double v);

// 45s, 9m30s, 4h12m, 3d4h: a time in milliseconds in its largest two units.
std::string duration(int64_t milliseconds);
// 999, 1.5k, 158k, 2.4M.
std::string amount(uint64_t count);
// 0.44ms, 9.5ms, 25ms.
std::string milliseconds(double value);
// A version as `paw --version` names it, "0.1.1 (git 61f4e8d)", as "0.1.1 (61f4e8d)".
std::string version_text(const std::string &version);

// The fields, from the session's replies.
// "4h12m · 158k frames": the session's uptime and the frames drawn.
std::string session_text(int64_t uptime, uint64_t frames);
// "avg 0.44ms · p99 1.2ms · worst 9.5ms", from `get stats` and `get frame_times`; "" before the
// first frame.
std::string frames_text(const Rows &stats, const Rows &frame_times);
// "7 on 3 workspaces · dwindle", from `get windows` and the focused monitor's tiling and layout.
std::string windows_text(const Rows &windows, bool tiling, const std::string &layout);
// A line per monitor from `get outputs`: "DP-1 2560x1440@144Hz ×1.25 HDR".
std::vector<std::string> monitors_text(const Rows &outputs);
// "inhibited · dim 9m30s · lock 15m", from `get idle`.
std::string idle_text(const Rows &idle);
// "3400K" while night light warms the screens, "scheduled" while it waits; "" while it is off.
std::string night_text(const Rows &night_light);
// The saved sessions' names, "last, work", the first four of them.
std::string sessions_text(std::vector<std::string> names);

struct Launches {
    std::string id;
    int count = 0;
};
// The application launched most often in the shell's launches file (lines of desktop ID, count
// and last launch, tab-separated), the one launched last of a tie.
std::optional<Launches> top_app(const std::string &file);
// A desktop entry's Name, "" without one.
std::string desktop_name(const std::string &entry);
// A desktop ID as a name, where no entry gives one: "org.kde.dolphin.desktop" as "dolphin".
std::string app_label(const std::string &id);

struct Field {
    std::string label, value; // a label "" continues the field above
};
struct Style {
    bool color = true; // 24-bit colour escapes
    bool logo = true;
    int columns = 0; // the terminal's width, too narrow for the logo beside the fields; 0 unknown
};
// The paw print, a line per row of half blocks, all as wide: in the border's colours laid along
// its gradient, or plain.
std::vector<std::string> logo(const Palette &palette, bool color);
// Everything: the logo, and beside it `title` (its first word coloured), a rule, the fields and a
// strip of the palette's colours.
std::string render(const Palette &palette, const std::string &title,
                   const std::vector<Field> &fields, Style style);

} // namespace paw::fetch
