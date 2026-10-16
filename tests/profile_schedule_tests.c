// SPDX-License-Identifier: GPL-3.0-or-later
/* The day and night schedule of appearance profiles: the last sunrise or sunset before a moment,
 * on that day or the days before, through times in either order and polar days. */
#include "paw/profile_schedule.h"
#include <stdio.h>
#include <string.h>
#include "check.h"

/* The boundary before `minute` on days[0] as "D:MINUTE:rise" or "D:MINUTE:set", "none" for none. */
static const char *last(const struct sh_day_times *days, int count, double minute) {
    static char text[32];
    struct sh_boundary b;
    if (!sh_last_boundary(days, count, minute, &b))
        return "none";
    snprintf(text, sizeof(text), "%d:%g:%s", b.days_back, b.minute, b.sunrise ? "rise" : "set");
    return text;
}

#define LAST(days, minute, expected)                                                               \
    do {                                                                                           \
        const char *got = last(days, (int)(sizeof(days) / sizeof(*(days))), minute);              \
        CHECK(!strcmp(got, expected), "at %g: %s, not %s", (double)(minute), got, expected);      \
    } while (0)

int main(void) {
    // 07:00 and 20:00 every day.
    struct sh_day_times usual[] = {{420, 1200, false}, {420, 1200, false}, {420, 1200, false}};
    LAST(usual, 720, "0:420:rise");    // noon: this morning's sunrise
    LAST(usual, 420, "0:420:rise");    // the very minute counts
    LAST(usual, 419.5, "1:1200:set");  // just before: last evening's sunset
    LAST(usual, 0, "1:1200:set");      // midnight
    LAST(usual, 1200, "0:1200:set");   // sunset
    LAST(usual, 1439.9, "0:1200:set"); // late evening

    // The day running through midnight: up at 20:00, down at 07:00.
    struct sh_day_times shift[] = {{1200, 420, false}, {1200, 420, false}};
    LAST(shift, 720, "0:420:set");
    LAST(shift, 1300, "0:1200:rise");
    LAST(shift, 300, "1:1200:rise");

    // A location's times shift from day to day; each day has its own.
    struct sh_day_times spring[] = {{400, 1230, false}, {403, 1227, false}};
    LAST(spring, 399, "1:1227:set");
    LAST(spring, 401, "0:400:rise");

    // Both at once count as a sunset: no day at all.
    struct sh_day_times none[] = {{600, 600, false}, {600, 600, false}};
    LAST(none, 700, "0:600:set");
    LAST(none, 500, "1:600:set");

    // Polar day or night: the last boundary is the one before it began, if near enough.
    struct sh_day_times polar[] = {{0, 0, true}, {0, 0, true}, {540, 840, false}};
    LAST(polar, 720, "2:840:set");
    struct sh_day_times dark[] = {{0, 0, true}, {0, 0, true}};
    LAST(dark, 720, "none");
    // The first polar day still comes after yesterday's sunset.
    struct sh_day_times first[] = {{0, 0, true}, {500, 900, false}};
    LAST(first, 100, "1:900:set");
    // Leaving a polar night: the first sunrise ends it.
    struct sh_day_times leaving[] = {{700, 800, false}, {0, 0, true}};
    LAST(leaving, 750, "0:700:rise");
    LAST(leaving, 600, "none");

    // No days at all.
    CHECK(!sh_last_boundary(usual, 0, 720, &(struct sh_boundary){0}));

    if (failures)
        return 1;
    puts("profile_schedule passed");
    return 0;
}
