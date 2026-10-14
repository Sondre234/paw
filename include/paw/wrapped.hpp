// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* `paw wrapped` (src/cli/wrapped.cpp): the day files of src/wrapped_day.c added up over a period,
 * and the report made of them, a card per thing worth saying, with a playful comparison. Pure,
 * apart from the clock and the time zone, for tests/wrapped_tests.cpp. */
#include "paw/wrapped_day.h"

#include <array>
#include <cstdint>
#include <ctime>
#include <map>
#include <string>
#include <vector>

namespace paw::wrapped {

// What the days of a period add up to.
struct Totals {
    std::array<int64_t, SH_WRAPPED_COUNTERS> counters{};
    std::map<std::string, int64_t> apps;  // seconds each application had the focus
    std::map<std::string, int64_t> daily; // active seconds, by date
    sh_wrapped_streak streak{};           // the longest
};

// Adds a day's lines, a day file's or `get wrapped`'s, counted on `date` (YYYY-MM-DD).
void add_day(Totals &totals, const std::string &date, const std::string &text);
// Whether anything was counted at all.
bool counted(const Totals &totals);
int64_t clicks(const Totals &totals);
// The date with the most active time, or "" for none.
std::string busiest_day(const Totals &totals);

// The dates a report covers: `from` and `to` included, "" for no bound either way.
struct Period {
    std::string from, to;
    std::string name; // "today", "week", "month", "year", "all", "since"
};
// The period an option names, with `today` the local date: "today", "week" (the last 7 days),
// "month" (30), "year" (365), "all", or "since" with `since` its first date. Throws
// std::runtime_error for anything else, or a date that is not YYYY-MM-DD.
Period period(const std::string &option, const std::string &today, const std::string &since = "");
bool in_period(const Period &period, const std::string &date);
// Whether `text` is a date as the day files are named, YYYY-MM-DD.
bool is_date(const std::string &text);
// `date` moved by `days`.
std::string add_days(const std::string &date, int days);
// "Fri 3 Oct", and with the year when it is not `this_year`.
std::string day_name(const std::string &date, int this_year = 0);

// 1,234,567.
std::string number(int64_t value);
// "8 mm", "45 cm", "312 m", "4.1 km", "1,234 km".
std::string distance(double millimetres);
// "45 s", "14 m", "2 h 14 m", "61 h".
std::string duration(int64_t seconds);
// Comparisons, "" when there is nothing to compare with: the pointer's travel laid flat ("about
// 41 football pitches"), scrolling stood up ("as tall as 3 Eiffel Towers"), typing ("about 2.5
// novels' worth"), and time at the desktop ("that's 3 feature films back to back").
std::string travel_comparison(double millimetres);
std::string height_comparison(double millimetres);
std::string typing_comparison(int64_t keys);
std::string time_comparison(int64_t seconds);

struct Bar {
    std::string label, value;
    double share; // of the longest bar, 0 to 1
};
struct Card {
    std::string title, headline;
    std::vector<std::string> lines;
    std::vector<Bar> bars;
};
// The report's cards, each only when there is something to say: who it is for and what period,
// time, the top applications, the longest focus streak, the mouse, clicks, scrolling, keys,
// windows, getting around, and the busiest day. `now` dates the streak and says the year.
std::vector<Card> cards(const Totals &totals, const Period &period, std::time_t now);
// Cards drawn as boxes `width` columns wide, in colour (ANSI) or not.
std::string render(const std::vector<Card> &cards, int width, bool colour);
// Cards as plain lines, for a pipe or NO_COLOR.
std::string plain(const std::vector<Card> &cards);
// The totals as JSON.
std::string json(const Totals &totals, const Period &period);

} // namespace paw::wrapped
