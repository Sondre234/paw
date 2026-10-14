// SPDX-License-Identifier: GPL-3.0-or-later
/* paw wrapped: the day files the compositor keeps its counters in (src/wrapped_day.c), and the
 * report `paw wrapped` makes of them (src/cli/wrapped.cpp). */
#include "check.h"
#include "paw/wrapped.hpp"
#include "paw/wrapped_day.h"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace {

std::string written(const sh_wrapped_day &day) {
    char *text = nullptr;
    size_t size = 0;
    FILE *file = open_memstream(&text, &size);
    sh_wrapped_day_write(&day, file);
    fclose(file);
    std::string result(text, size);
    free(text);
    return result;
}

sh_wrapped_day read(const std::string &text) {
    sh_wrapped_day day{};
    FILE *file = fmemopen(const_cast<char *>(text.data()), text.size(), "r");
    sh_wrapped_day_read(&day, file);
    fclose(file);
    return day;
}

void day_files() {
    sh_wrapped_day day{};
    sh_wrapped_day_add(&day, SH_WRAPPED_KEYS, 40);
    sh_wrapped_day_add(&day, SH_WRAPPED_KEYS, 2);
    sh_wrapped_day_add(&day, SH_WRAPPED_STARTED, 2000);
    sh_wrapped_day_add(&day, SH_WRAPPED_STARTED, 1000);
    sh_wrapped_day_add(&day, SH_WRAPPED_STARTED, 0);
    sh_wrapped_day_add(&day, SH_WRAPPED_STARTED, 3000);
    CHECK(day.counters[SH_WRAPPED_KEYS] == 42);
    CHECK(day.counters[SH_WRAPPED_STARTED] == 1000, "the earliest start is kept, 0 is none");

    CHECK(sh_wrapped_day_add_app(&day, "kitty", 10));
    CHECK(sh_wrapped_day_add_app(&day, "firefox", 30));
    CHECK(sh_wrapped_day_add_app(&day, "kitty", 25));
    CHECK(sh_wrapped_day_add_app(&day, "a\tb\nc", 1));
    CHECK(sh_wrapped_day_add_app(&day, "", 2));
    CHECK(day.app_count == 4, "%zu", day.app_count);
    CHECK(!strcmp(day.apps[2].id, "a b c"), "a tab or line break in an ID is a space: %s",
          day.apps[2].id);
    CHECK(!strcmp(day.apps[3].id, "unknown"), "%s", day.apps[3].id);
    std::string longer(300, 'x');
    longer.replace(126, 2, "\xc3\xa9"); // é across the limit is left out whole
    CHECK(sh_wrapped_day_add_app(&day, longer.c_str(), 1));
    CHECK(strlen(day.apps[4].id) == 126, "%zu", strlen(day.apps[4].id));

    sh_wrapped_day_add_streak(&day, "kitty", 20, 500);
    sh_wrapped_day_add_streak(&day, "firefox", 15, 600);
    CHECK(!strcmp(day.streak.app, "kitty") && day.streak.seconds == 20 && day.streak.start == 500,
          "the longer streak stays");
    sh_wrapped_day_add_streak(&day, "firefox", 21, 700);
    CHECK(!strcmp(day.streak.app, "firefox") && day.streak.start == 700);

    sh_wrapped_day_sort(&day);
    CHECK(!strcmp(day.apps[0].id, "kitty") && day.apps[0].seconds == 35);
    CHECK(!strcmp(day.apps[1].id, "firefox"));

    // What is written reads back the same, and reading it twice adds it up.
    auto text = written(day);
    CHECK(text.find("keys\t42\n") != std::string::npos, "%s", text.c_str());
    CHECK(text.find("app\tkitty\t35\n") != std::string::npos);
    CHECK(text.find("streak\tfirefox\t21\t700\n") != std::string::npos);
    auto again = read(text + text);
    CHECK(again.counters[SH_WRAPPED_KEYS] == 84);
    CHECK(again.counters[SH_WRAPPED_STARTED] == 1000, "a start is not added up");
    CHECK(again.app_count == 5 && again.apps[0].seconds == 70);
    CHECK(again.streak.seconds == 21, "a streak is the longest, not a sum");
    sh_wrapped_day_clear(&again);

    // Lines a newer paw or a hand might add are passed over; the rest of the file still counts.
    std::string odd = "# a comment\nkeys\t5\nnew_counter\t7\nkeys\tmany\nclicks_left\t-\n"
                      "app\tfoo\nstreak\tfoo\t3\nmoves\t1\t2\n" +
                      std::string(1000, 'y') + "\nsnaps\t2\nwindows_opened\t3";
    auto passed = read(odd);
    CHECK(passed.counters[SH_WRAPPED_KEYS] == 5);
    CHECK(passed.counters[SH_WRAPPED_MOVES] == 0 && passed.app_count == 0 &&
          passed.streak.seconds == 0);
    CHECK(passed.counters[SH_WRAPPED_SNAPS] == 2, "a line too long for any key is passed over");
    CHECK(passed.counters[SH_WRAPPED_WINDOWS_OPENED] == 3, "a last line without a line break");
    sh_wrapped_day_clear(&passed);

    sh_wrapped_day merged{};
    sh_wrapped_day_add(&merged, SH_WRAPPED_KEYS, 1);
    CHECK(sh_wrapped_day_merge(&merged, &day));
    CHECK(merged.counters[SH_WRAPPED_KEYS] == 43 && merged.app_count == 5 &&
          merged.streak.seconds == 21);
    sh_wrapped_day_clear(&merged);
    sh_wrapped_day_clear(&day);
    CHECK(!day.apps && !day.app_count && !day.counters[SH_WRAPPED_KEYS]);
    for (int counter = 0; counter < SH_WRAPPED_COUNTERS; ++counter)
        CHECK(*sh_wrapped_counter_name(static_cast<sh_wrapped_counter>(counter)), "%d", counter);
}

void saving() {
    char directory[] = "/tmp/paw-wrapped-XXXXXX";
    CHECK(mkdtemp(directory));
    setenv("XDG_STATE_HOME", directory, 1);
    char path[4096];
    CHECK(sh_wrapped_path("2026-10-14", path, sizeof(path)));
    CHECK(std::string(path) == std::string(directory) + "/paw/wrapped/2026-10-14", "%s", path);

    sh_wrapped_day day{};
    sh_wrapped_day_add(&day, SH_WRAPPED_KEYS, 10);
    sh_wrapped_day_add_app(&day, "kitty", 60);
    CHECK(sh_wrapped_save("2026-10-14", &day), "%s", strerror(errno));
    CHECK(sh_wrapped_save("2026-10-14", &day), "%s", strerror(errno));
    std::ifstream file(path);
    std::stringstream text;
    text << file.rdbuf();
    CHECK(text.str().starts_with("# "), "a comment says what the file is");
    auto saved = read(text.str());
    CHECK(saved.counters[SH_WRAPPED_KEYS] == 20, "a save adds to the day's file");
    CHECK(saved.app_count == 1 && saved.apps[0].seconds == 120);
    CHECK(!std::filesystem::exists(std::string(path) + ".new"));
    auto mode = std::filesystem::status(path).permissions();
    CHECK((mode & std::filesystem::perms::others_read) == std::filesystem::perms::none,
          "the file is the user's alone");
    sh_wrapped_day_clear(&saved);
    sh_wrapped_day_clear(&day);
    std::filesystem::remove_all(directory);

    unsetenv("XDG_STATE_HOME");
    setenv("HOME", "/home/someone", 1);
    CHECK(sh_wrapped_path(nullptr, path, sizeof(path)));
    CHECK(std::string(path) == "/home/someone/.local/state/paw/wrapped", "%s", path);
    unsetenv("HOME");
    CHECK(!sh_wrapped_path(nullptr, path, sizeof(path)));
}

void dates() {
    setenv("TZ", "UTC", 1);
    tzset();
    char date[11];
    sh_wrapped_date(1760400000, date); // 2025-10-14 00:00 UTC
    CHECK(!strcmp(date, "2025-10-14"), "%s", date);
    sh_wrapped_date(1760399999, date);
    CHECK(!strcmp(date, "2025-10-13"), "%s", date);
    CHECK(sh_wrapped_day_end(1760399999) == 1760400000);
    CHECK(sh_wrapped_day_end(1760400000) == 1760400000 + 86400, "midnight starts a day");
    // The day summer time starts on is 23 hours long: 29 March 2026 in Central Europe, which
    // begins at 1774738800.
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    sh_wrapped_date(1774738800, date);
    CHECK(!strcmp(date, "2026-03-29"), "%s", date);
    CHECK(sh_wrapped_day_end(1774738800 + 3600) == 1774738800 + 23 * 3600, "%lld",
          static_cast<long long>(sh_wrapped_day_end(1774738800 + 3600)));
    unsetenv("TZ");
    tzset();
}

void active_time() {
    const int64_t idle_after = 120000;
    // Input at 1000: active until two minutes later, idle after.
    CHECK(sh_wrapped_active(1000, 61000, 1000, idle_after, false, false) == 60000);
    CHECK(sh_wrapped_active(1000, 301000, 1000, idle_after, false, false) == 120000);
    CHECK(sh_wrapped_active(200000, 301000, 1000, idle_after, false, false) == 0,
          "long after the last input");
    CHECK(sh_wrapped_active(100000, 301000, 1000, idle_after, false, false) == 21000);
    CHECK(sh_wrapped_active(1000, 301000, 1000, idle_after, false, true) == 300000,
          "an idle inhibitor holds the session active");
    CHECK(sh_wrapped_active(1000, 301000, 1000, idle_after, true, true) == 0,
          "locked, nothing is active");
    CHECK(sh_wrapped_active(5000, 1000, 1000, idle_after, false, false) == 0);
}

namespace w = paw::wrapped;

void words() {
    CHECK(w::number(0) == "0" && w::number(999) == "999" && w::number(1000) == "1,000");
    CHECK(w::number(1234567) == "1,234,567" && w::number(-1234) == "-1,234");
    CHECK(w::distance(8) == "8 mm" && w::distance(450) == "45 cm", "%s", w::distance(450).c_str());
    CHECK(w::distance(2400) == "2.4 m" && w::distance(312000) == "312 m");
    CHECK(w::distance(4100000) == "4.1 km" && w::distance(4000000) == "4 km");
    CHECK(w::distance(1234000000) == "1,234 km");
    CHECK(w::duration(45) == "45 s" && w::duration(840) == "14 m");
    CHECK(w::duration(8040) == "2 h 14 m" && w::duration(7200) == "2 h");
    CHECK(w::duration(219600) == "61 h", "%s", w::duration(219600).c_str());

    CHECK(w::travel_comparison(4100000) == "about 39 football pitches", "%s",
          w::travel_comparison(4100000).c_str());
    CHECK(w::travel_comparison(50).empty(), "nothing is that small");
    CHECK(w::travel_comparison(300) == "about one A4 page laid end to end");
    CHECK(w::travel_comparison(28000) == "about 2.5 London buses nose to tail");
    CHECK(w::travel_comparison(84390000) == "about 2 marathons");
    CHECK(w::height_comparison(330000) == "as tall as one Eiffel Tower");
    CHECK(w::height_comparison(1000).empty());
    CHECK(w::typing_comparison(1250000) == "about 2.5 novels' worth of typing");
    CHECK(w::typing_comparison(560) == "about 2 tweets" && w::typing_comparison(100).empty());
    CHECK(w::typing_comparison(90000) == "about 50 pages of a novel");
    CHECK(w::time_comparison(600).empty());
    CHECK(w::time_comparison(3 * 7200) == "that's 3 feature films back to back");
    CHECK(w::time_comparison(40 * 3600) == "that's one working week");
}

void periods() {
    CHECK(w::is_date("2026-10-14") && !w::is_date("2026-02-30") && !w::is_date("2026-1-14"));
    CHECK(!w::is_date("2026-10-14.new") && !w::is_date("abcd-ef-gh"));
    CHECK(w::add_days("2026-01-01", -1) == "2025-12-31");
    CHECK(w::add_days("2028-02-28", 1) == "2028-02-29", "a leap year");
    CHECK(w::day_name("2026-10-03", 2026) == "Sat 3 Oct");
    CHECK(w::day_name("2026-10-03", 2025) == "Sat 3 Oct 2026", "another year says which");

    auto today = w::period("today", "2026-10-14");
    CHECK(today.from == "2026-10-14" && w::in_period(today, "2026-10-14") &&
          !w::in_period(today, "2026-10-13"));
    CHECK(w::period("week", "2026-10-14").from == "2026-10-08", "the last seven days");
    CHECK(w::period("month", "2026-10-14").from == "2026-09-15");
    CHECK(w::period("year", "2026-10-14").from == "2025-10-15");
    CHECK(w::in_period(w::period("all", "2026-10-14"), "1999-01-01"));
    CHECK(w::period("since", "2026-10-14", "2026-03-01").from == "2026-03-01");
    for (auto [option, since] : {std::pair{"since", "March"}, {"since", ""}, {"fortnight", ""}}) {
        bool refused = false;
        try {
            w::period(option, "2026-10-14", since);
        } catch (const std::runtime_error &) {
            refused = true;
        }
        CHECK(refused, "%s %s", option, since);
    }
}

// Two days and the running session's lines, as `get wrapped` gives them.
w::Totals example() {
    w::Totals totals;
    w::add_day(totals, "2026-10-03",
               "# a comment\nsessions\t2\nstarted\t1791000000\nactive\t33120\nidle\t8000\n"
               "pointer_px\t9000000\npointer_mm\t2400000\nclicks_left\t12000\n"
               "clicks_right\t900\nscroll_mm\t105000\nkeys\t180000\nwindows_opened\t300\n"
               "windows_closed\t290\nmoves\t120\nsnaps\t12\nworkspace_switches\t800\n"
               "streak\tkitty\t8040\t1791034920\napp\tfirefox\t15000\napp\tkitty\t12000\n"
               "app\tdiscord\t4000\n");
    w::add_day(totals, "2026-10-12",
               "sessions\t1\nstarted\t1791700000\nactive\t20000\npointer_mm\t1700000\n"
               "pointer_px\t6000000\nkeys\t90000\napp\tfirefox\t12000\n");
    w::add_day(totals, "2026-10-12",
               "enabled\tyes\nday\t2026-10-12\nsaves\t3\nsaved\t1791790000\nstate\tactive\n"
               "focus\tfirefox\t70\nsessions\t0\nstarted\t0\nactive\t50\nkeys\t10\n"
               "streak\tfirefox\t70\t1791790100\napp\tfirefox\t50\n");
    return totals;
}

void totals() {
    auto totals = example();
    CHECK(totals.counters[SH_WRAPPED_SESSIONS] == 3);
    CHECK(totals.counters[SH_WRAPPED_STARTED] == 1791000000, "the first start");
    CHECK(totals.counters[SH_WRAPPED_KEYS] == 270010, "get wrapped's own lines are passed over");
    CHECK(totals.apps.at("firefox") == 27050 && totals.apps.at("kitty") == 12000);
    CHECK(!strcmp(totals.streak.app, "kitty") && totals.streak.seconds == 8040);
    CHECK(totals.daily.size() == 2 && totals.daily.at("2026-10-12") == 20050);
    CHECK(w::busiest_day(totals) == "2026-10-03");
    CHECK(w::clicks(totals) == 12900);
    CHECK(w::counted(totals));
    w::Totals sessions_only;
    w::add_day(sessions_only, "2026-10-14", "sessions\t1\nstarted\t1791900000\n");
    CHECK(!w::counted(sessions_only), "a session that did nothing is nothing to report");
    CHECK(!w::counted(w::Totals{}));
}

void report() {
    setenv("TZ", "UTC", 1);
    tzset();
    auto totals = example();
    std::time_t now = 1791936000; // 2026-10-14 UTC
    auto cards = w::cards(totals, w::period("year", "2026-10-14"), now);
    std::string titles;
    for (const auto &card : cards)
        titles += card.title + "|";
    CHECK(titles == "paw wrapped 2026|Time|Top apps|Longest focus|Mouse miles|Clicks|Scrolling|"
                    "Keyboard|Windows|Getting around|Busiest day|",
          "%s", titles.c_str());
    CHECK(cards[0].headline == "Your time at the desktop so far", "%s", cards[0].headline.c_str());
    CHECK(cards[0].lines[0] == "2 days and 3 sessions, from Sat 3 Oct to Mon 12 Oct.", "%s",
          cards[0].lines[0].c_str());
    CHECK(cards[2].headline == "Your top app was firefox: 7 h 30 m.", "%s",
          cards[2].headline.c_str());
    CHECK(cards[2].lines[0] == "Then kitty and discord." && cards[2].bars.size() == 3);
    CHECK(cards[2].bars[0].share == 1 && cards[2].bars[1].value == "3 h 20 m");
    CHECK(cards[3].headline == "Longest focus streak: 2 h 14 m in kitty.");
    CHECK(cards[3].lines[0] == "From Sat 3 Oct at 13:42, without a break.", "%s",
          cards[3].lines[0].c_str());
    CHECK(cards[4].headline == "You moved your mouse 4.1 km — about 39 football pitches.", "%s",
          cards[4].headline.c_str());
    CHECK(cards[5].lines[0] == "12,000 left and 900 right.", "%s", cards[5].lines[0].c_str());
    CHECK(cards[8].lines[0] == "You dragged windows 120 times and snapped them into place 12 "
                               "times.",
          "%s", cards[8].lines[0].c_str());
    CHECK(cards[9].headline == "You switched workspaces 800 times." &&
          cards[9].lines[0] == "About 400 a day.");

    // A day with only key presses says only that.
    w::Totals keys;
    w::add_day(keys, "2026-10-14", "sessions\t1\nkeys\t42\n");
    auto few = w::cards(keys, w::period("today", "2026-10-14"), now);
    CHECK(few.size() == 2 && few[0].headline == "Your day at the desktop" &&
              few[0].lines[0] == "Wed 14 Oct, 1 session." &&
              few[1].headline == "You pressed 42 keys.",
          "%zu", few.size());
    CHECK(w::cards(w::Totals{}, w::period("all", "2026-10-14"), now).empty());

    // Boxes as wide as asked, every line of them, coloured or not.
    for (bool colour : {false, true}) {
        auto text = w::render(cards, 40, colour);
        CHECK((text.find("\x1b[") != std::string::npos) == colour);
        std::istringstream lines(text);
        int rows = 0;
        for (std::string line; std::getline(lines, line);) {
            std::string bare;
            for (size_t i = 0; i < line.size(); ++i) {
                if (line[i] == '\x1b')
                    i = line.find('m', i);
                else
                    bare += line[i];
            }
            size_t columns = 0;
            for (unsigned char c : bare)
                columns += (c & 0xC0) != 0x80;
            CHECK(bare.empty() || columns == 40, "%zu: %s", columns, bare.c_str());
            rows += !bare.empty();
        }
        CHECK(rows > 40);
    }
    auto text = w::plain(cards);
    CHECK(text.starts_with("paw wrapped 2026: Your time at the desktop so far\n  2 days"));
    CHECK(text.find("\nTop apps: Your top app was firefox: 7 h 30 m.\n  Then kitty and "
                    "discord.\n  firefox: 7 h 30 m\n") != std::string::npos,
          "%s", text.c_str());

    w::add_day(totals, "2026-10-13", "app\ta\"b\\c\t5\n");
    auto json = w::json(totals, w::period("week", "2026-10-14"));
    CHECK(json.find("\"from\": \"2026-10-08\",\n  \"to\": null") != std::string::npos, "%s",
          json.c_str());
    CHECK(json.find("\"keys\": 270010") != std::string::npos);
    CHECK(json.find("{\"id\": \"firefox\", \"seconds\": 27050}") != std::string::npos);
    CHECK(json.find("\"a\\\"b\\\\c\"") != std::string::npos, "quotes and backslashes escaped");
    CHECK(json.find("\"busiest_day\": \"2026-10-03\"") != std::string::npos);
    unsetenv("TZ");
    tzset();
}

} // namespace

int main() {
    day_files();
    saving();
    dates();
    active_time();
    words();
    periods();
    totals();
    report();
    return failures != 0;
}
