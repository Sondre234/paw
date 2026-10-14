// SPDX-License-Identifier: GPL-3.0-or-later
/* paw wrapped: the day files the compositor keeps its counters in (src/wrapped_day.c). */
#include "check.h"
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

} // namespace

int main() {
    day_files();
    saving();
    dates();
    active_time();
    return failures != 0;
}
