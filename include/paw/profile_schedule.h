// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* The day and night schedule of appearance profiles (`profile_schedule`): which sunrise or sunset
 * came last before a moment, which decides the profile due then. The compositor's side, with the
 * clock, the state file and the switch, is src/compositor/profile_schedule.c. Nothing here reads
 * the clock. */
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One day's sunrise and sunset, in minutes after its local midnight, both in [0, 1440), in
 * either order; `polar` when there is neither, the sun up or down all day. */
struct sh_day_times {
    double sunrise, sunset;
    bool polar;
};

/* A sunrise or a sunset: on the day `days_back` days before, at `minute` after its midnight. */
struct sh_boundary {
    int days_back;
    double minute;
    bool sunrise; /* the day's profile's turn; else the night's */
};

/* The last sunrise or sunset at or before `minute` on days[0], looking back through days[1]
 * (the day before) to days[count - 1]. A sunrise and a sunset at the same minute count as a
 * sunset. False when none of those days has either, as in polar day or night. */
bool sh_last_boundary(const struct sh_day_times *days, int count, double minute,
                      struct sh_boundary *boundary);

#ifdef __cplusplus
}
#endif
