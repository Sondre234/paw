// SPDX-License-Identifier: GPL-3.0-or-later
/* `paw wrapped [--today|--week|--month|--year|--all|--since YYYY-MM-DD] [--plain|--json]`: a
 * report of time at the desktop in the manner of Spotify Wrapped, from the day files the
 * compositor saves (src/compositor/wrapped.c) and what the running session counted since it last
 * saved (`get wrapped`). */
#include "paw/wrapped.hpp"
#include "paw/cli.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <sys/ioctl.h>
#include <unistd.h>

namespace paw::wrapped {
namespace {

const char *const weekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char *const months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                              "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

std::chrono::year_month_day ymd(const std::string &date) {
    return std::chrono::year_month_day{std::chrono::year{std::stoi(date.substr(0, 4))},
                                       std::chrono::month{unsigned(std::stoi(date.substr(5, 2)))},
                                       std::chrono::day{unsigned(std::stoi(date.substr(8, 2)))}};
}

std::string format_date(std::chrono::year_month_day date) {
    char text[16];
    std::snprintf(text, sizeof(text), "%04d-%02u-%02u", int(date.year()), unsigned(date.month()),
                  unsigned(date.day()));
    return text;
}

// One decimal, without a trailing ".0".
std::string fixed1(double value) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f", value);
    std::string result = text;
    if (result.ends_with(".0"))
        result.resize(result.size() - 2);
    return result;
}

// `n` of something: "one football pitch", "2.5 London buses", "41 football pitches".
std::string amount(double n, const std::string &one, const std::string &many) {
    double rounded = n < 10 ? std::round(n * 10) / 10 : std::round(n);
    if (rounded == 1)
        return "one " + one;
    return (n < 10 ? fixed1(rounded) : number(static_cast<int64_t>(rounded))) + " " + many;
}

struct Reference {
    double size; // in the unit compared
    const char *one, *many, *after;
};

// The largest reference `value` is at least one of, as "N things", else "".
template <size_t N>
std::string compare(double value, const Reference (&references)[N], const std::string &before) {
    for (size_t i = N; i-- > 0;) {
        if (value >= references[i].size)
            return before +
                   amount(value / references[i].size, references[i].one, references[i].many) +
                   references[i].after;
    }
    return "";
}

std::string times(int64_t count) {
    return count == 1 ? "once" : count == 2 ? "twice" : number(count) + " times";
}

std::string plural(int64_t count, const std::string &one, const std::string &many) {
    return number(count) + " " + (count == 1 ? one : many);
}

// "a", "a and b", "a, b and c".
std::string listing(const std::vector<std::string> &items) {
    std::string result;
    for (size_t i = 0; i < items.size(); ++i)
        result += (i == 0 ? "" : i + 1 == items.size() ? " and " : ", ") + items[i];
    return result;
}

std::string capitalized(std::string text) {
    if (!text.empty() && text[0] >= 'a' && text[0] <= 'z')
        text[0] = static_cast<char>(text[0] - 'a' + 'A');
    return text;
}

std::string uppercase(std::string text) {
    for (auto &c : text)
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    return text;
}

int local_year(std::time_t when) {
    std::tm local{};
    localtime_r(&when, &local);
    return local.tm_year + 1900;
}

// Columns a UTF-8 text takes, a character each.
size_t columns(const std::string &text) {
    size_t count = 0;
    for (unsigned char c : text)
        count += (c & 0xC0) != 0x80;
    return count;
}

// The first `count` characters of a UTF-8 text.
std::string first_columns(const std::string &text, size_t count) {
    size_t i = 0;
    for (size_t seen = 0; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80 && seen++ == count)
            break;
    }
    return text.substr(0, i);
}

// `text` broken into lines of at most `width` columns, between words where it can.
std::vector<std::string> wrap(const std::string &text, size_t width) {
    std::vector<std::string> lines;
    std::string line;
    std::istringstream words(text);
    for (std::string word; words >> word;) {
        while (columns(word) > width) {
            if (!line.empty())
                lines.push_back(std::move(line)), line.clear();
            lines.push_back(first_columns(word, width));
            word = word.substr(lines.back().size());
        }
        if (line.empty())
            line = word;
        else if (columns(line) + 1 + columns(word) <= width)
            line += " " + word;
        else
            lines.push_back(std::move(line)), line = word;
    }
    if (!line.empty())
        lines.push_back(line);
    return lines;
}

std::string repeat(const std::string &piece, size_t count) {
    std::string result;
    for (size_t i = 0; i < count; ++i)
        result += piece;
    return result;
}

std::string json_string(const std::string &text) {
    std::string result = "\"";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') {
            result += '\\';
            result += static_cast<char>(c);
        } else if (c < 0x20) {
            char escape[8];
            std::snprintf(escape, sizeof(escape), "\\u%04x", c);
            result += escape;
        } else {
            result += static_cast<char>(c);
        }
    }
    return result + "\"";
}

} // namespace

void add_day(Totals &totals, const std::string &date, const std::string &text) {
    sh_wrapped_day day{};
    std::istringstream lines(text);
    for (std::string line; std::getline(lines, line);)
        sh_wrapped_day_read_line(&day, line.c_str());
    for (int counter = 0; counter < SH_WRAPPED_COUNTERS; ++counter) {
        int64_t value = day.counters[counter], &total = totals.counters[counter];
        if (counter != SH_WRAPPED_STARTED)
            total += value;
        else if (value > 0 && (!total || value < total))
            total = value;
    }
    for (size_t i = 0; i < day.app_count; ++i)
        totals.apps[day.apps[i].id] += day.apps[i].seconds;
    if (day.streak.seconds > totals.streak.seconds)
        totals.streak = day.streak;
    totals.daily[date] += day.counters[SH_WRAPPED_ACTIVE];
    sh_wrapped_day_clear(&day);
}

bool counted(const Totals &totals) {
    for (int counter = 0; counter < SH_WRAPPED_COUNTERS; ++counter)
        if (counter != SH_WRAPPED_STARTED && counter != SH_WRAPPED_SESSIONS &&
            totals.counters[counter] > 0)
            return true;
    return !totals.apps.empty();
}

int64_t clicks(const Totals &totals) {
    const auto &c = totals.counters;
    return c[SH_WRAPPED_CLICKS_LEFT] + c[SH_WRAPPED_CLICKS_RIGHT] + c[SH_WRAPPED_CLICKS_MIDDLE] +
           c[SH_WRAPPED_CLICKS_OTHER];
}

std::string busiest_day(const Totals &totals) {
    std::string best;
    int64_t most = 0;
    for (const auto &[date, active] : totals.daily)
        if (active > most)
            best = date, most = active;
    return best;
}

bool is_date(const std::string &text) {
    if (text.size() != 10 || text[4] != '-' || text[7] != '-')
        return false;
    for (size_t i = 0; i < text.size(); ++i)
        if (i != 4 && i != 7 && (text[i] < '0' || text[i] > '9'))
            return false;
    return ymd(text).ok();
}

std::string add_days(const std::string &date, int days) {
    return format_date(std::chrono::sys_days{ymd(date)} + std::chrono::days{days});
}

std::string day_name(const std::string &date, int this_year) {
    auto day = ymd(date);
    std::chrono::weekday weekday{std::chrono::sys_days{day}};
    std::string name = std::string(weekdays[weekday.c_encoding()]) + " " +
                       std::to_string(unsigned(day.day())) + " " +
                       months[unsigned(day.month()) - 1];
    if (int(day.year()) != this_year)
        name += " " + std::to_string(int(day.year()));
    return name;
}

Period period(const std::string &option, const std::string &today, const std::string &since) {
    if (!is_date(today))
        throw std::runtime_error("not a date: " + today);
    if (option == "today")
        return {today, "", option};
    if (option == "week")
        return {add_days(today, -6), "", option};
    if (option == "month")
        return {add_days(today, -29), "", option};
    if (option == "year")
        return {add_days(today, -364), "", option};
    if (option == "all")
        return {"", "", option};
    if (option == "since") {
        if (!is_date(since))
            throw std::runtime_error("--since takes a date as YYYY-MM-DD, not '" + since + "'");
        return {since, "", option};
    }
    throw std::runtime_error("no such period: " + option);
}

bool in_period(const Period &period, const std::string &date) {
    return (period.from.empty() || date >= period.from) && (period.to.empty() || date <= period.to);
}

std::string number(int64_t value) {
    std::string digits = std::to_string(value < 0 ? -value : value), result;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i && (digits.size() - i) % 3 == 0)
            result += ',';
        result += digits[i];
    }
    return (value < 0 ? "-" : "") + result;
}

std::string distance(double millimetres) {
    if (millimetres < 10)
        return number(std::llround(millimetres)) + " mm";
    if (millimetres < 1000)
        return number(std::llround(millimetres / 10)) + " cm";
    double metres = millimetres / 1000;
    if (metres < 10)
        return fixed1(std::round(metres * 10) / 10) + " m";
    if (metres < 1000)
        return number(std::llround(metres)) + " m";
    double kilometres = metres / 1000;
    if (kilometres < 100)
        return fixed1(std::round(kilometres * 10) / 10) + " km";
    return number(std::llround(kilometres)) + " km";
}

std::string duration(int64_t seconds) {
    if (seconds < 60)
        return number(seconds) + " s";
    if (seconds < 3600)
        return number(seconds / 60) + " m";
    int64_t hours = seconds / 3600, minutes = seconds % 3600 / 60;
    if (hours < 10 && minutes)
        return number(hours) + " h " + number(minutes) + " m";
    return number(hours) + " h";
}

std::string travel_comparison(double millimetres) {
    static const Reference references[] = {
        {85.6, "credit card", "credit cards", " laid end to end"},
        {297, "A4 page", "A4 pages", " laid end to end"},
        {11200, "London bus", "London buses", " nose to tail"},
        {105000, "football pitch", "football pitches", ""},
        {42195000, "marathon", "marathons", ""},
        {40075000000, "trip around the Earth", "trips around the Earth", ""},
    };
    return compare(millimetres, references, "about ");
}

std::string height_comparison(double millimetres) {
    static const Reference references[] = {
        {1750, "person", "people", " standing on each other's heads"},
        {93000, "Statue of Liberty", "Statues of Liberty", ""},
        {330000, "Eiffel Tower", "Eiffel Towers", ""},
        {8849000, "Mount Everest", "Mount Everests", ""},
    };
    return compare(millimetres, references, "as tall as ");
}

std::string typing_comparison(int64_t keys) {
    // A tweet is 280 characters at most, a page of a novel about 1,800 and a novel about 500,000.
    static const Reference references[] = {
        {280, "tweet", "tweets", ""},
        {1800, "page of a novel", "pages of a novel", ""},
        {500000, "novel's worth of typing", "novels' worth of typing", ""},
    };
    return compare(static_cast<double>(keys), references, "about ");
}

std::string time_comparison(int64_t seconds) {
    static const Reference references[] = {
        {22 * 60, "sitcom episode", "sitcom episodes", ""},
        {2 * 3600, "feature film", "feature films", " back to back"},
        {40 * 3600, "working week", "working weeks", ""},
    };
    return compare(static_cast<double>(seconds), references, "that's ");
}

std::vector<Card> cards(const Totals &totals, const Period &period, std::time_t now) {
    std::vector<Card> result;
    if (!counted(totals))
        return result;
    const auto &c = totals.counters;
    int year = local_year(now);
    auto days = static_cast<int64_t>(totals.daily.size());
    const std::string &first = totals.daily.begin()->first, &last = totals.daily.rbegin()->first;

    Card intro{
        period.name == "year" ? "paw wrapped " + std::to_string(year) : "paw wrapped", "", {}, {}};
    if (period.name == "today")
        intro.headline = "Your day at the desktop";
    else if (period.name == "week")
        intro.headline = "Your week at the desktop";
    else if (period.name == "month")
        intro.headline = "Your month at the desktop";
    else if (period.name == "year")
        intro.headline =
            first > period.from ? "Your time at the desktop so far" : "Your year at the desktop";
    else if (period.name == "since")
        intro.headline = "Your time at the desktop since " + day_name(period.from, year);
    else
        intro.headline = "All your time at the desktop";
    std::string sessions =
        c[SH_WRAPPED_SESSIONS] > 0 ? plural(c[SH_WRAPPED_SESSIONS], "session", "sessions") : "";
    if (days == 1)
        intro.lines.push_back(capitalized(day_name(first, year)) +
                              (sessions.empty() ? "" : ", " + sessions) + ".");
    else
        intro.lines.push_back(plural(days, "day", "days") +
                              (sessions.empty() ? "" : " and " + sessions) + ", from " +
                              day_name(first, year) + " to " + day_name(last, year) + ".");
    result.push_back(intro);

    if (int64_t active = c[SH_WRAPPED_ACTIVE]; active > 0) {
        Card card{"Time", "You spent " + duration(active) + " at the desktop.", {}, {}};
        if (auto comparison = time_comparison(active); !comparison.empty())
            card.lines.push_back(capitalized(comparison) + ".");
        if (c[SH_WRAPPED_IDLE] >= 60)
            card.lines.push_back("paw ran " + duration(c[SH_WRAPPED_IDLE]) +
                                 " more while you were away or idle.");
        result.push_back(card);
    }

    if (!totals.apps.empty()) {
        std::vector<std::pair<std::string, int64_t>> apps(totals.apps.begin(), totals.apps.end());
        std::stable_sort(apps.begin(), apps.end(),
                         [](const auto &a, const auto &b) { return a.second > b.second; });
        apps.resize(std::min<size_t>(apps.size(), 5));
        Card card{"Top apps",
                  "Your top app was " + apps[0].first + ": " + duration(apps[0].second) + ".",
                  {},
                  {}};
        std::vector<std::string> others;
        for (size_t i = 1; i < apps.size(); ++i)
            others.push_back(apps[i].first);
        if (!others.empty())
            card.lines.push_back("Then " + listing(others) + ".");
        for (const auto &[app, seconds] : apps)
            card.bars.push_back(
                {app, duration(seconds), apps[0].second ? double(seconds) / apps[0].second : 0});
        result.push_back(card);
    }

    if (totals.streak.seconds >= 60) {
        std::time_t start = totals.streak.start;
        std::tm local{};
        localtime_r(&start, &local);
        char date[16], clock[8];
        std::strftime(date, sizeof(date), "%Y-%m-%d", &local);
        std::strftime(clock, sizeof(clock), "%H:%M", &local);
        result.push_back({"Longest focus",
                          "Longest focus streak: " + duration(totals.streak.seconds) + " in " +
                              totals.streak.app + ".",
                          {"From " + day_name(date, year) + " at " + clock + ", without a break."},
                          {}});
    }

    if (double mm = double(c[SH_WRAPPED_POINTER_MM]); mm > 0) {
        auto comparison = travel_comparison(mm);
        result.push_back({"Mouse miles",
                          "You moved your mouse " + distance(mm) +
                              (comparison.empty() ? "." : " — " + comparison + "."),
                          {"That's " + number(c[SH_WRAPPED_POINTER_PX]) + " pixels."},
                          {}});
    }

    if (int64_t total = clicks(totals); total > 0) {
        Card card{"Clicks", "You clicked " + times(total) + ".", {}, {}};
        std::vector<std::string> buttons;
        const std::pair<int, const char *> kinds[] = {{SH_WRAPPED_CLICKS_LEFT, "left"},
                                                      {SH_WRAPPED_CLICKS_RIGHT, "right"},
                                                      {SH_WRAPPED_CLICKS_MIDDLE, "middle"},
                                                      {SH_WRAPPED_CLICKS_OTHER, "other buttons"}};
        for (const auto &[counter, name] : kinds)
            if (c[counter] > 0)
                buttons.push_back(number(c[counter]) + " " + name);
        if (buttons.size() > 1)
            card.lines.push_back(capitalized(listing(buttons)) + ".");
        if (int64_t active = c[SH_WRAPPED_ACTIVE]; active > 0)
            card.lines.push_back(total > active ? "More than one a second at the desktop."
                                                : "One every " + duration(active / total) +
                                                      " at the desktop.");
        result.push_back(card);
    }

    if (double mm = double(c[SH_WRAPPED_SCROLL_MM]); mm > 0) {
        auto comparison = height_comparison(mm);
        result.push_back(
            {"Scrolling",
             "You scrolled " + distance(mm) + (comparison.empty() ? "." : " — " + comparison + "."),
             {},
             {}});
    }

    if (int64_t keys = c[SH_WRAPPED_KEYS]; keys > 0) {
        auto comparison = typing_comparison(keys);
        result.push_back({"Keyboard",
                          "You pressed " + plural(keys, "key", "keys") +
                              (comparison.empty() ? "." : " — " + comparison + "."),
                          {"paw counted them, never which ones."},
                          {}});
    }

    if (c[SH_WRAPPED_WINDOWS_OPENED] || c[SH_WRAPPED_MOVES] || c[SH_WRAPPED_RESIZES] ||
        c[SH_WRAPPED_SNAPS] || c[SH_WRAPPED_LAUNCHES]) {
        Card card{"Windows", "", {}, {}};
        if (c[SH_WRAPPED_WINDOWS_OPENED])
            card.headline = "You opened " +
                            plural(c[SH_WRAPPED_WINDOWS_OPENED], "window", "windows") +
                            " and closed " + number(c[SH_WRAPPED_WINDOWS_CLOSED]) + ".";
        std::vector<std::string> handled;
        if (c[SH_WRAPPED_MOVES])
            handled.push_back("dragged windows " + times(c[SH_WRAPPED_MOVES]));
        if (c[SH_WRAPPED_RESIZES])
            handled.push_back("resized them " + times(c[SH_WRAPPED_RESIZES]));
        if (c[SH_WRAPPED_SNAPS])
            handled.push_back("snapped them into place " + times(c[SH_WRAPPED_SNAPS]));
        if (!handled.empty())
            card.lines.push_back("You " + listing(handled) + ".");
        if (c[SH_WRAPPED_LAUNCHES])
            card.lines.push_back("You started " +
                                 plural(c[SH_WRAPPED_LAUNCHES], "program", "programs") +
                                 " from a key.");
        if (card.headline.empty()) {
            card.headline = card.lines.front();
            card.lines.erase(card.lines.begin());
        }
        result.push_back(card);
    }

    if (c[SH_WRAPPED_WORKSPACE_SWITCHES] || c[SH_WRAPPED_OVERVIEW_OPENS] ||
        c[SH_WRAPPED_SWITCHER_OPENS] || c[SH_WRAPPED_GESTURES]) {
        Card card{"Getting around", "", {}, {}};
        if (int64_t switches = c[SH_WRAPPED_WORKSPACE_SWITCHES]; switches) {
            card.lines.push_back("You switched workspaces " + times(switches) + ".");
            if (days > 1 && switches / days > 0)
                card.lines.push_back("About " + number(switches / days) + " a day.");
        }
        std::vector<std::string> opened;
        if (c[SH_WRAPPED_OVERVIEW_OPENS])
            opened.push_back("the overview " + times(c[SH_WRAPPED_OVERVIEW_OPENS]));
        if (c[SH_WRAPPED_SWITCHER_OPENS])
            opened.push_back("the window switcher " + times(c[SH_WRAPPED_SWITCHER_OPENS]));
        if (!opened.empty())
            card.lines.push_back("You opened " + listing(opened) + ".");
        if (c[SH_WRAPPED_GESTURES])
            card.lines.push_back(
                "You made " +
                plural(c[SH_WRAPPED_GESTURES], "touchpad gesture", "touchpad gestures") + ".");
        card.headline = card.lines.front();
        card.lines.erase(card.lines.begin());
        result.push_back(card);
    }

    if (auto busiest = busiest_day(totals); days > 1 && !busiest.empty())
        result.push_back({"Busiest day",
                          "Your busiest day was " + day_name(busiest, year) + ".",
                          {duration(totals.daily.at(busiest)) + " at the desktop."},
                          {}});
    return result;
}

std::string render(const std::vector<Card> &cards, int width, bool colour) {
    // Spotify's green, then pink, yellow, purple, orange, cyan, blue and coral (256 colours).
    static const int palette[] = {41, 205, 220, 141, 208, 51, 75, 203};
    size_t inner = static_cast<size_t>(std::max(width, 24)) - 4;
    std::string out;
    for (size_t index = 0; index < cards.size(); ++index) {
        const Card &card = cards[index];
        std::string tint = "\x1b[38;5;" + std::to_string(palette[index % 8]) + "m";
        auto style = [&](const std::string &codes, const std::string &text) {
            return colour && !text.empty() ? codes + text + "\x1b[0m" : text;
        };
        std::string border = colour ? tint : "", reset = colour ? "\x1b[0m" : "";
        // A row of the card: `text` styled, padded by what it takes without the styling.
        auto row = [&](const std::string &styled, size_t taken) {
            out += border + "│" + reset + " " + styled +
                   std::string(inner > taken ? inner - taken : 0, ' ') + " " + border + "│" +
                   reset + "\n";
        };
        out += border + "╭" + repeat("─", inner + 2) + "╮" + reset + "\n";
        std::string title = " " + uppercase(card.title) + " ";
        row(style("\x1b[1;7m" + tint, title), columns(title));
        row("", 0);
        for (const auto &line : wrap(card.headline, inner))
            row(style("\x1b[1m" + tint, line), columns(line));
        for (const auto &text : card.lines)
            for (const auto &line : wrap(text, inner))
                row(line, columns(line));
        if (!card.bars.empty()) {
            row("", 0);
            size_t label = 0, value = 0;
            for (const auto &bar : card.bars) {
                label = std::max(label, columns(bar.label));
                value = std::max(value, columns(bar.value));
            }
            label = std::min(label, inner / 3);
            size_t length = inner > label + value + 2 ? inner - label - value - 2 : 1;
            for (const auto &bar : card.bars) {
                std::string name = first_columns(bar.label, label);
                auto filled = static_cast<size_t>(std::lround(bar.share * double(length)));
                filled = std::clamp<size_t>(filled, 1, length);
                std::string text = name + std::string(label - columns(name) + 1, ' ') +
                                   style(tint, repeat("█", filled)) +
                                   style("\x1b[2m", repeat("░", length - filled)) + " " +
                                   std::string(value - columns(bar.value), ' ') + bar.value;
                row(text, label + 1 + length + 1 + value);
            }
        }
        out += border + "╰" + repeat("─", inner + 2) + "╯" + reset + "\n";
        if (index + 1 < cards.size())
            out += "\n";
    }
    return out;
}

std::string plain(const std::vector<Card> &cards) {
    std::string out;
    for (const auto &card : cards) {
        out += (out.empty() ? "" : "\n") + card.title + ": " + card.headline + "\n";
        for (const auto &line : card.lines)
            out += "  " + line + "\n";
        for (const auto &bar : card.bars)
            out += "  " + bar.label + ": " + bar.value + "\n";
    }
    return out;
}

std::string json(const Totals &totals, const Period &period) {
    auto bound = [](const std::string &date) {
        return date.empty() ? std::string("null") : json_string(date);
    };
    std::string out = "{\n  \"from\": " + bound(period.from) + ",\n  \"to\": " + bound(period.to) +
                      ",\n  \"days\": " + std::to_string(totals.daily.size()) +
                      ",\n  \"counters\": {";
    for (int counter = 0; counter < SH_WRAPPED_COUNTERS; ++counter)
        out += std::string(counter ? "," : "") + "\n    " +
               json_string(sh_wrapped_counter_name(static_cast<sh_wrapped_counter>(counter))) +
               ": " + std::to_string(totals.counters[counter]);
    out += "\n  },\n  \"apps\": [";
    std::vector<std::pair<std::string, int64_t>> apps(totals.apps.begin(), totals.apps.end());
    std::stable_sort(apps.begin(), apps.end(),
                     [](const auto &a, const auto &b) { return a.second > b.second; });
    for (size_t i = 0; i < apps.size(); ++i)
        out += std::string(i ? "," : "") + "\n    {\"id\": " + json_string(apps[i].first) +
               ", \"seconds\": " + std::to_string(apps[i].second) + "}";
    out += std::string(apps.empty() ? "" : "\n  ") + "],\n  \"streak\": ";
    if (totals.streak.seconds > 0)
        out += "{\"app\": " + json_string(totals.streak.app) +
               ", \"seconds\": " + std::to_string(totals.streak.seconds) +
               ", \"start\": " + std::to_string(totals.streak.start) + "}";
    else
        out += "null";
    out += ",\n  \"daily\": {";
    size_t i = 0;
    for (const auto &[date, active] : totals.daily)
        out += std::string(i++ ? "," : "") + "\n    " + json_string(date) + ": " +
               std::to_string(active);
    out += std::string(totals.daily.empty() ? "" : "\n  ") + "},\n  \"busiest_day\": ";
    auto busiest = busiest_day(totals);
    out += (busiest.empty() ? std::string("null") : json_string(busiest)) + "\n}\n";
    return out;
}

} // namespace paw::wrapped

namespace paw::cli {
namespace {

const char usage[] = "usage: paw wrapped [--today | --week | --month | --year | --all | "
                     "--since YYYY-MM-DD] [--plain | --json]";

// What the running session counted since it last saved, from `get wrapped`.
struct Live {
    bool enabled = false;
    std::string why; // it counts nothing
    std::string day, saves, text;
};

std::optional<Live> live() {
    const char *socket = std::getenv("PAW_SOCKET");
    if (!socket || !*socket)
        return std::nullopt;
    Reply reply;
    try {
        reply = request("get wrapped");
    } catch (const std::runtime_error &) {
        return std::nullopt; // not in a session after all
    }
    if (!reply.ok)
        return std::nullopt; // a paw that counts nothing
    Live result;
    result.text = reply.body;
    for (const auto &fields : rows(reply.body)) {
        if (fields.size() >= 2 && fields[0] == "enabled") {
            result.enabled = fields[1] == "yes";
            result.why = fields.size() > 2 ? fields[2] : "";
        } else if (fields.size() == 2 && fields[0] == "day")
            result.day = fields[1];
        else if (fields.size() == 2 && fields[0] == "saves")
            result.saves = fields[1];
    }
    return result;
}

wrapped::Totals saved(const std::filesystem::path &directory, const wrapped::Period &period) {
    wrapped::Totals totals;
    std::error_code error;
    for (const auto &entry : std::filesystem::directory_iterator(directory, error)) {
        auto name = entry.path().filename().string();
        if (!wrapped::is_date(name) || !wrapped::in_period(period, name))
            continue;
        std::ifstream file(entry.path());
        std::stringstream text;
        text << file.rdbuf();
        wrapped::add_day(totals, name, text.str());
    }
    return totals;
}

// `path` with the home directory as ~, as people know it.
std::string shown(const std::filesystem::path &path) {
    std::string text = path.string();
    const char *home = std::getenv("HOME");
    size_t length = home ? std::strlen(home) : 0;
    if (length > 1 && text.starts_with(home) && (text.size() == length || text[length] == '/'))
        return "~" + text.substr(length);
    return text;
}

int terminal_width() {
    winsize size{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0)
        return size.ws_col;
    return 80;
}

} // namespace

int wrapped(int argc, char **argv) {
    std::string option, since;
    bool plain = false, json = false;
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << usage << "\n";
            return 0;
        }
        if (arg == "--plain" || arg == "--json") {
            (arg == "--plain" ? plain : json) = true;
            continue;
        }
        if (!option.empty())
            throw std::runtime_error(usage);
        if (arg == "--today" || arg == "--week" || arg == "--month" || arg == "--year" ||
            arg == "--all")
            option = arg.substr(2);
        else if (arg == "--since" && i + 1 < argc)
            option = "since", since = argv[++i];
        else
            throw std::runtime_error(usage);
    }
    std::time_t now = std::time(nullptr);
    char today[11];
    sh_wrapped_date(now, today);
    auto period = wrapped::period(option.empty() ? "year" : option, today, since);
    auto directory = state_directory() / "wrapped";

    // The running session's count joins the files, read between two looks at it so that a save
    // meanwhile, which would count it twice or not at all, is noticed and the files read again.
    wrapped::Totals totals;
    std::optional<Live> session;
    for (int attempt = 0; attempt < 5; ++attempt) {
        auto before = live();
        totals = state_directory().empty() ? wrapped::Totals{} : saved(directory, period);
        session = live();
        if (!session || (before && before->saves == session->saves))
            break;
    }
    if (session && session->enabled && wrapped::in_period(period, session->day))
        wrapped::add_day(totals, session->day, session->text);

    if (json) {
        std::cout << wrapped::json(totals, period);
        return 0;
    }
    bool off = session && !session->enabled;
    if (!wrapped::counted(totals)) {
        std::cout << "Nothing counted "
                  << (option.empty() || option == "all" ? "yet" : "in that time")
                  << ". A standalone paw session counts what happens at the desktop while "
                     "wrapped.enabled is on, as it is unless the configuration turns it off, and "
                     "saves it every few minutes in "
                  << shown(directory) << ". Come back after a while at the desktop.\n";
        if (off)
            std::cout << "This session counts nothing: " << session->why << ".\n";
        return 0;
    }
    auto cards = wrapped::cards(totals, period, now);
    cards.push_back({"That's a wrap",
                     "See you next time.",
                     {"Counted on this machine and kept there, in " + shown(directory) + "."},
                     {}});
    if (off)
        cards.back().lines.push_back("This session counts nothing: " + session->why + ".");
    const char *no_colour = std::getenv("NO_COLOR");
    if (plain || !isatty(STDOUT_FILENO) || (no_colour && *no_colour))
        std::cout << wrapped::plain(cards);
    else
        std::cout << wrapped::render(cards, std::clamp(terminal_width(), 32, 64), true);
    return 0;
}

} // namespace paw::cli
