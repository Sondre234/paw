// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* `paw top` (src/cli/top.cpp): what it samples from the running session, and the pure functions
 * that turn samples into what it draws (the arithmetic, the graphs, the columns), which the
 * unit test calls. */
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace paw::top {

// A monitor in one sample: its mode, refresh rate (Hz, 0 when unknown), the frames it has drawn,
// and the time each of its last frames took to draw (us, oldest first), from `get frame_times
// NAME`; `timed` is false when the compositor does not answer that.
struct Monitor {
    std::string name, mode;
    double refresh = 0;
    uint64_t frames = 0;
    std::vector<uint32_t> spent;
    bool timed = false;
};

// A window or layer surface in one sample, a line of `get commits`.
struct Client {
    bool layer = false;
    uint32_t id = 0;
    int pid = 0;
    uint64_t commits = 0, commit_ns = 0;
    std::string output;
    int workspace = 0;       // from 1; 0 for a layer surface
    std::string name, title; // app ID or namespace, and title
};

// What one look at the session found, `time` seconds into the run. `stats` are the fields of
// `get stats`; `listed` is false when the compositor does not answer `get commits`.
struct Sample {
    double time = 0;
    std::vector<uint64_t> stats;
    std::vector<Monitor> monitors;
    std::vector<Client> clients;
    bool listed = false;
};

// The clients of a reply to `get commits`; throws on a line it cannot read.
std::vector<Client> parse_clients(const std::vector<std::vector<std::string>> &rows);
// The refresh rate in a mode of `get outputs` ("2560x1440@143.912"); 0 without one.
double refresh_of(const std::string &mode);

// How a frame's time compares with its monitor's frame budget, the refresh interval: under half
// of it, up to all of it, or over it.
enum class Grade { good, tight, over };
Grade grade(double us, double budget_us);

// The value `share` (0 to 1) of `values` lie at or under, by nearest rank; 0 for none.
double percentile(std::vector<uint32_t> values, double share);

struct Summary {
    size_t count = 0;
    double average = 0, p99 = 0, worst = 0;
};
Summary summarize(const std::vector<uint32_t> &values);

// Events per second between two readings of a counter `seconds` apart; 0 when the counter went
// back (another compositor) or no time passed.
double per_second(uint64_t earlier, uint64_t later, double seconds);

// The sample to measure rates from: the oldest of `history` (oldest first) at most `window`
// seconds older than the newest, and older than it when there is one.
const Sample &reference(const std::vector<Sample> &history, double window);

// A client's rates over a window: commits and nanoseconds of handling per second.
struct Row {
    Client client;
    double commits = 0, ns = 0;
};
enum class Sort { time, commits, name, workspace };
Sort next(Sort sort);
const char *sort_name(Sort sort);
// The newest sample's clients with their rates since `reference` (or since the first sample
// after it that has them, for one that opened since), sorted.
std::vector<Row> rows(const std::vector<Sample> &history, double window, Sort sort);

// A bar graph of the last `width` of `values` (the newest on the right), `height` text rows of
// block elements top row first; a value of `top` or more fills its column, and any value shows at
// least a sliver. Columns left of the oldest value are blank.
std::vector<std::string> graph(const std::vector<uint32_t> &values, size_t width, size_t height,
                               double top);
// The block element a column for `value` has in text row `row` (0 at the top) of such a graph.
const char *glyph(double value, size_t row, size_t height, double top);
// The top of a graph's scale: a round number of microseconds at or over `worst`, and over a
// quarter of `budget` so that small frames look small.
double scale(double worst, double budget);
// How many text rows each monitor's graph gets on a terminal `rows` high (0 for a printout) where
// the rest takes `used` of them: what is left, between one and four.
size_t graph_height(size_t rows, size_t used, size_t monitors);

// How many columns `text` (UTF-8) takes on a terminal.
size_t width_of(const std::string &text);
// `text` safe to print, its control characters made '?', cut to `width` columns with "…" where it
// was cut, and padded with spaces to `width`.
std::string fit(const std::string &text, size_t width);
// The same, right-aligned.
std::string fit_right(const std::string &text, size_t width);
// The same, not padded.
std::string cut(const std::string &text, size_t width);
// `cells` joined by `separator`, as many to a line as fit in `width` (a longer cell alone).
std::vector<std::string> wrap(const std::vector<std::string> &cells, size_t width,
                              const std::string &separator);

// "850µs", "1.24ms", "16.7ms", "1.20s".
std::string duration(double us);
// Per second: "0", "0.4", "12.5", "144", "1.2k".
std::string rate(double value);
// "0:04:09", "26:13:05", "3d 4h".
std::string uptime(double seconds);
// The start time in clock ticks after boot in the text of /proc/PID/stat; 0 if it has none.
uint64_t start_ticks(const std::string &stat);

// The columns of the table of clients a terminal `width` wide has room for: the app ID's and
// title's widths (the title 0 when it has none) and whether each commit's time and the workspace
// show.
struct Columns {
    size_t name = 0, title = 0;
    bool each = false, workspace = false;
};
Columns columns(size_t width);

// What the drawing needs besides the samples.
struct View {
    size_t width = 80, height = 0; // height 0: a printout, as long as it takes
    bool color = false, paused = false;
    Sort sort = Sort::time;
    double window = 1; // seconds rates are measured over
    std::string version, uptime, profile, error;
};
// The lines of the screen, without newlines, each `view.width` columns at most (with colour,
// its escape sequences besides).
std::vector<std::string> draw(const std::vector<Sample> &history, const View &view);

} // namespace paw::top
