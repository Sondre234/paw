// SPDX-License-Identifier: GPL-3.0-or-later
#include "paw/profile_schedule.h"

bool sh_last_boundary(const struct sh_day_times *days, int count, double minute,
                      struct sh_boundary *boundary) {
    for (int back = 0; back < count; ++back) {
        const struct sh_day_times *day = &days[back];
        if (day->polar)
            continue;
        // On the day itself only what has come by `minute`; on the days before, all of it.
        double until = back == 0 ? minute : 1440;
        bool rose = day->sunrise <= until, set = day->sunset <= until;
        if (!rose && !set)
            continue;
        bool sunrise = rose && (!set || day->sunrise > day->sunset);
        *boundary = (struct sh_boundary){
            .days_back = back, .minute = sunrise ? day->sunrise : day->sunset, .sunrise = sunrise};
        return true;
    }
    return false;
}
