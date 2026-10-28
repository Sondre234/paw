// SPDX-License-Identifier: GPL-3.0-or-later
/* paw top's arithmetic and drawing (src/cli/top.cpp): percentiles, rates, the frame graphs, the
 * columns that fit a terminal, and the screen they make. */
#include "check.h"
#include "paw/top.hpp"

#include <stdexcept>
#include <string>
#include <vector>

using namespace paw::top;

namespace {

bool contains(const std::vector<std::string> &lines, const std::string &text) {
    for (const auto &line : lines)
        if (line.find(text) != std::string::npos)
            return true;
    return false;
}

Client window(uint32_t id, const std::string &name, uint64_t commits, uint64_t ns,
              int workspace = 1) {
    Client client;
    client.id = id;
    client.pid = 100 + static_cast<int>(id);
    client.commits = commits;
    client.commit_ns = ns;
    client.output = "DP-1";
    client.workspace = workspace;
    client.name = name;
    client.title = name + " window";
    return client;
}

// Two looks a second apart at a 60 Hz monitor and three windows: a video drawing 60 frames a
// second, an editor that drew twice, and a panel.
std::vector<Sample> history() {
    Sample before, after;
    after.time = 1;
    before.stats = {1000, 2000000, 9000, 500, 1000000, 3, 7, 40, 80000, 2, 400000};
    after.stats = {1060, 3000000, 9000, 562, 1500000, 3, 7, 140, 120000, 3, 900000};
    Monitor monitor;
    monitor.name = "DP-1";
    monitor.mode = "2560x1440@59.951";
    monitor.refresh = 59.951;
    monitor.frames = 1000;
    monitor.timed = true;
    before.monitors = {monitor};
    monitor.frames = 1060;
    for (int i = 0; i < 60; ++i)
        monitor.spent.push_back(i == 30 ? 20000 : 500 + static_cast<uint32_t>(i));
    after.monitors = {monitor};
    Client panel = window(9, "bar", 10, 5000);
    panel.layer = true;
    panel.workspace = 0;
    panel.title = "";
    before.clients = {window(1, "mpv", 1000, 4000000), window(2, "Editor", 20, 100000), panel};
    after.clients = {window(1, "mpv", 1060, 4600000), window(2, "Editor", 22, 900000), panel};
    before.listed = after.listed = true;
    return {before, after};
}

} // namespace

int main() {
    // Percentiles by nearest rank, and a summary of frame times.
    std::vector<uint32_t> hundred;
    for (uint32_t i = 1; i <= 100; ++i)
        hundred.push_back(101 - i);
    CHECK(percentile(hundred, 0.99) == 99);
    CHECK(percentile(hundred, 0.5) == 50);
    CHECK(percentile(hundred, 1) == 100);
    CHECK(percentile({7}, 0.99) == 7);
    CHECK(percentile({}, 0.99) == 0);
    auto summary = summarize({100, 300, 200});
    CHECK(summary.count == 3 && summary.worst == 300 && summary.p99 == 300);
    NEAR(summary.average, 200, 1e-9);
    CHECK(summarize({}).count == 0 && summarize({}).worst == 0);

    // Frames against a 60 Hz budget.
    CHECK(grade(8000, 16667) == Grade::good);
    CHECK(grade(9000, 16667) == Grade::tight);
    CHECK(grade(16667, 16667) == Grade::tight);
    CHECK(grade(17000, 16667) == Grade::over);

    // Rates between two readings.
    NEAR(per_second(100, 160, 0.5), 120, 1e-9);
    CHECK(per_second(160, 100, 1) == 0, "a counter that went back is another compositor");
    CHECK(per_second(100, 160, 0) == 0);

    // The look rates are measured from: the oldest within the window.
    std::vector<Sample> looks(6);
    for (size_t i = 0; i < looks.size(); ++i)
        looks[i].time = 0.25 * static_cast<double>(i);
    CHECK(reference(looks, 1).time == 0.25);
    CHECK(reference(looks, 0.1).time == 1, "the one before the newest when none is that recent");
    CHECK(reference({looks[0]}, 1).time == 0, "the newest alone");

    // Clients with their rates, sorted.
    auto table = rows(history(), 1, Sort::time);
    CHECK(table.size() == 3);
    CHECK(table[0].client.name == "Editor", "the most time first");
    NEAR(table[0].commits, 2, 1e-9);
    NEAR(table[0].ns, 800000, 1e-9);
    CHECK(table[1].client.name == "mpv" && table[2].client.name == "bar");
    NEAR(table[2].commits, 0, 1e-9);
    table = rows(history(), 1, Sort::commits);
    CHECK(table[0].client.name == "mpv", "the most commits first");
    table = rows(history(), 1, Sort::name);
    CHECK(table[0].client.name == "bar" && table[1].client.name == "Editor",
          "by app ID, whatever its case");
    auto later = history();
    later[1].clients[1].workspace = 3;
    table = rows(later, 1, Sort::workspace);
    CHECK(table[0].client.name == "mpv" && table[1].client.name == "Editor" &&
              table[2].client.name == "bar",
          "by workspace, layer surfaces last");
    CHECK(next(Sort::time) == Sort::commits && next(Sort::workspace) == Sort::time);
    // A window that opened since the oldest look counts from the first look that saw it.
    auto opened = history();
    Sample third = opened[1];
    third.time = 1.5;
    third.clients.push_back(window(5, "new", 30, 3000));
    opened[1].clients.push_back(window(5, "new", 10, 1000));
    opened.push_back(third);
    table = rows(opened, 2, Sort::commits);
    CHECK(table[1].client.name == "new");
    NEAR(table[1].commits, 40, 1e-9);
    // A reused number under another process is another client.
    auto reused = history();
    reused[1].clients[0].pid = 999;
    table = rows(reused, 1, Sort::commits);
    CHECK(table[0].client.name == "Editor", "%s", table[0].client.name.c_str());

    // What get commits and get outputs say.
    auto clients = parse_clients(
        {{"window", "3", "42", "120", "560000", "DP-1", "2", "firefox", "Mozilla Firefox"},
         {"layer", "1", "40", "8", "900", "DP-1", "0", "paw-shell", ""}});
    CHECK(clients.size() == 2);
    CHECK(!clients[0].layer && clients[0].id == 3 && clients[0].pid == 42);
    CHECK(clients[0].commits == 120 && clients[0].commit_ns == 560000);
    CHECK(clients[0].workspace == 2 && clients[0].title == "Mozilla Firefox");
    CHECK(clients[1].layer && clients[1].name == "paw-shell" && clients[1].title.empty());
    bool refused = false;
    try {
        parse_clients({{"window", "x", "1", "1", "1", "DP-1", "1", "a", "b"}});
    } catch (const std::runtime_error &) {
        refused = true;
    }
    CHECK(refused, "a field that is not a number");
    NEAR(refresh_of("2560x1440@143.912"), 143.912, 1e-9);
    CHECK(refresh_of("1280x720") == 0);

    // Graphs: eighths of a column, at least a sliver.
    CHECK(std::string(glyph(50, 0, 1, 100)) == "▄" && std::string(glyph(200, 0, 1, 100)) == "█");
    CHECK(std::string(glyph(75, 0, 2, 100)) == "▄" && std::string(glyph(75, 1, 2, 100)) == "█",
          "over two rows");
    CHECK(std::string(glyph(30, 0, 2, 100)) == " " && std::string(glyph(30, 1, 2, 100)) == "▅");
    CHECK(std::string(glyph(0, 0, 3, 100)) == " " && std::string(glyph(0, 2, 3, 100)) == "▁");
    CHECK(scale(1000, 16667) == 5000, "a quarter of the budget at least, made round");
    CHECK(scale(20001, 16667) == 25000);
    CHECK(scale(0, 0) == 1);
    CHECK(graph_height(0, 10, 2) == 3, "a printout");
    CHECK(graph_height(50, 12, 1) == 4);
    CHECK(graph_height(24, 18, 1) == 4);
    CHECK(graph_height(24, 18, 2) == 3);
    CHECK(graph_height(24, 18, 3) == 2);
    CHECK(graph_height(24, 22, 3) == 1);
    CHECK(graph_height(10, 30, 3) == 1, "one row each even where there is no room");

    // Text that fits.
    CHECK(width_of("abc") == 3 && width_of("µs") == 2 && width_of("日本") == 4);
    CHECK(width_of("é") == 1, "a combining accent takes no room");
    CHECK(width_of("\x1b[1mbold\x1b[0m") == 4, "escape sequences take no room");
    CHECK(fit("firefox", 10) == "firefox   ");
    CHECK(fit("firefox", 5) == "fire…");
    CHECK(fit_right("12", 4) == "  12");
    CHECK(cut("firefox", 4) == "fir…" && cut("ok", 4) == "ok");
    CHECK(fit("日本語", 4) == "日… ", "a wide character that does not fit is left out");
    CHECK(fit("a\x1b]0;evil\a\tb", 20) == "a?]0;evil??b        ",
          "a title cannot send the terminal escape sequences");
    CHECK(fit("\xff\xfe", 2) == "??", "bytes that are not UTF-8");
    CHECK(fit("x", 0).empty());
    CHECK(wrap({"aa", "bb", "cc"}, 6, " ") == (std::vector<std::string>{"aa bb", "cc"}));
    CHECK(wrap({"a long cell", "b"}, 4, " ") == (std::vector<std::string>{"a long cell", "b"}),
          "a cell longer than a line alone");

    // Numbers.
    CHECK(duration(850) == "850µs" && duration(1240) == "1.24ms" && duration(16667) == "16.7ms");
    CHECK(duration(250000) == "250ms" && duration(1.2e6) == "1.20s");
    CHECK(rate(0) == "0" && rate(0.42) == "0.4" && rate(12.5) == "12" && rate(143.9) == "144");
    CHECK(rate(1234) == "1.2k" && rate(25000) == "25k");
    CHECK(uptime(249) == "0:04:09" && uptime(26 * 3600 + 13 * 60 + 5) == "26:13:05");
    CHECK(uptime(4 * 86400 + 5 * 3600) == "4d 5h");
    CHECK(start_ticks("1234 (paw) S 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 4242 99 0") ==
          4242);
    CHECK(start_ticks("7 (a) b (c) S 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 77 1") == 77,
          "a process name with parentheses and spaces");
    CHECK(start_ticks("garbage") == 0 && start_ticks("1 (x) S 1 2") == 0);
    CHECK(cpu_ticks("1234 (paw) S 1 2 3 4 5 6 7 8 9 10 300 45 13 14 15 16 17 18 4242 99 0") == 345,
          "user and system time");

    // The table's columns at a few widths: every line as wide as the terminal.
    for (size_t width : {24, 30, 44, 60, 80, 120, 200}) {
        auto layout = columns(width);
        size_t used = 18 + (layout.workspace ? 4 : 0) + (layout.each ? 8 : 0) + 1 + layout.name +
                      (layout.title ? 1 + layout.title : 0);
        CHECK(used == width, "%zu columns fill %zu", used, width);
    }
    CHECK(columns(80).title > 0 && columns(80).each && columns(80).workspace);
    CHECK(columns(44).title == 0 && columns(44).workspace && !columns(44).each);
    CHECK(columns(120).name == 28);

    // The screen: a printout, then a terminal 24 rows high with colour.
    View view;
    view.version = "0.1.1";
    view.uptime = "1:00:00";
    view.profile = "washi";
    auto lines = draw(history(), view);
    CHECK(lines[0] == "paw 0.1.1 · up 1:00:00 · 1 monitor · profile washi", "%s", lines[0].c_str());
    auto busy = history();
    busy[0].cpu = 10;
    busy[1].cpu = 10.075;
    CHECK(draw(busy, view)[0] == "paw 0.1.1 · up 1:00:00 · cpu 7.5% · 1 monitor · profile washi",
          "%s", draw(busy, view)[0].c_str());
    CHECK(contains(lines, "DP-1 2560x1440 60Hz  60 fps  avg 854µs  p99 20.0ms  worst 20.0ms"));
    CHECK(contains(lines, "worst 20.0ms") && contains(lines, "budget 16.7ms"));
    CHECK(contains(lines, "commits 62/s 500µs/s") && contains(lines, "motions 100/s 40µs/s"));
    CHECK(contains(lines, "reflows 1.0/s 500µs/s"));
    CHECK(contains(lines, "COMMITS/s   TIME/s    EACH  WS APP ID"));
    CHECK(contains(lines, "      2.0    800µs   400µs   1 Editor"));
    CHECK(contains(lines, "layer surface on DP-1"));
    for (const auto &line : lines) {
        CHECK(width_of(line) <= 80, "%s", line.c_str());
        CHECK(line.find('\x1b') == std::string::npos, "no colour in a printout: %s", line.c_str());
    }
    auto renamed = history();
    renamed[1].monitors[0].name = renamed[0].monitors[0].name = "HDMI-A-1-long";
    CHECK(contains(draw(renamed, view), "HDMI-A-1-long 60Hz  60 fps"),
          "the refresh rate alone where the size would not fit on the line");
    view.width = 60;
    view.height = 24;
    view.color = true;
    view.paused = true;
    view.sort = Sort::commits;
    lines = draw(history(), view);
    CHECK(lines.size() == 24);
    CHECK(lines.back().find("p resume  s sort: commits") != std::string::npos, "%s",
          lines.back().c_str());
    CHECK(contains(lines, "paused") && contains(lines, "\x1b[31m"), "the slow frame in red");
    for (const auto &line : lines)
        CHECK(width_of(line) <= 60, "%s", line.c_str());
    view.height = 12; // too short for the windows
    lines = draw(history(), view);
    CHECK(lines.size() == 12);
    view.height = 14; // room for one, and a line saying how many more there are
    lines = draw(history(), view);
    CHECK(lines.size() == 14 && lines[12].find("… 2 more") != std::string::npos, "%s",
          lines[12].c_str());
    view.height = 24;
    view.error = "cannot connect to /run/paw.sock";
    lines = draw({}, view);
    CHECK(lines.size() == 24 && contains(lines, "cannot connect"));
    auto old = history();
    old[1].listed = false;
    old[1].monitors[0].timed = false;
    view.error.clear();
    lines = draw(old, view);
    CHECK(contains(lines, "need a newer compositor"));
    return failures != 0;
}
