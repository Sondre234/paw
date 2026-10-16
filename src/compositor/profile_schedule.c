/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The day and night schedule of appearance profiles (`profile_schedule`): at each sunrise the day
 * profile is picked and at each sunset the night one, as `paw msg profile NAME` picks one (saved,
 * then a reload, which crossfades). The last sunrise or sunset applied is kept with the profile it
 * brought in $XDG_STATE_HOME/paw/profile-schedule, which a profile picked by hand leaves alone:
 * none is applied twice, so a pick by hand stays until the next one, and one that passed while
 * paw was not running is applied as it starts. `profile_auto` hands the choice back to the
 * schedule at once. Which sunrise or sunset came last is src/profile_schedule.c's arithmetic. */
#include "server.h"
#include "paw/profile_schedule.h"
#include <limits.h>

/* How often the clock is looked at: a switch comes within a minute of its time. */
#define PROFILE_SCHEDULE_TICK_MS 60000
/* The days looked back through for the last sunrise or sunset; in a polar day or night longer
 * than that the profile stays as it is. */
#define LOOKBACK_DAYS 3

/* What the schedule wants now. */
struct due {
    bool found;        /* a sunrise or sunset within LOOKBACK_DAYS */
    bool sunrise;      /* the last was a sunrise, or the sun is up all day */
    int64_t time, now; /* its time and now, in seconds since the epoch */
    char profile[40];
};

static bool state_path(char *path, size_t size) {
    const char *state = getenv("XDG_STATE_HOME"), *home = getenv("HOME");
    int length;
    if (state && state[0] == '/')
        length = snprintf(path, size, "%s/paw/profile-schedule", state);
    else if (home && home[0] == '/')
        length = snprintf(path, size, "%s/.local/state/paw/profile-schedule", home);
    else
        return false;
    return length > 0 && (size_t)length < size;
}

/* "TIME sunrise|sunset PROFILE": the last sunrise or sunset applied, and the profile it brought. */
static void read_state(struct sh_server *server) {
    server->profile_schedule.applied = -1;
    server->profile_schedule.brought[0] = '\0';
    char path[PATH_MAX], line[128], kind[16], name[40] = "";
    long long time = -1;
    FILE *file = state_path(path, sizeof(path)) ? fopen(path, "r") : NULL;
    if (!file)
        return;
    if (fgets(line, sizeof(line), file) && sscanf(line, "%lld %15s %39s", &time, kind, name) >= 1) {
        server->profile_schedule.applied = time;
        snprintf(server->profile_schedule.brought, sizeof(server->profile_schedule.brought), "%s",
                 name);
    }
    fclose(file);
}

static void write_state(struct sh_server *server, bool sunrise) {
    char path[PATH_MAX], directory[PATH_MAX], temporary[PATH_MAX + 8];
    if (!state_path(path, sizeof(path)))
        return;
    snprintf(directory, sizeof(directory), "%s", path);
    *strrchr(directory, '/') = '\0';
    snprintf(temporary, sizeof(temporary), "%s.new", path);
    FILE *file = make_directories(directory) ? fopen(temporary, "w") : NULL;
    if (file) {
        fprintf(file, "%lld %s %s\n", (long long)server->profile_schedule.applied,
                sunrise ? "sunrise" : "sunset", server->profile_schedule.brought);
        if (fclose(file) == 0 && rename(temporary, path) == 0)
            return;
    }
    wlr_log_errno(WLR_ERROR, "Cannot write %s", path);
}

/* The time `minute` minutes after the local midnight `days_back` days before `today`. */
static int64_t local_time(const struct tm *today, int days_back, double minute) {
    struct tm at = *today;
    at.tm_mday -= days_back;
    at.tm_hour = at.tm_min = 0;
    at.tm_sec = (int)lround(minute * 60);
    at.tm_isdst = -1;
    return (int64_t)mktime(&at);
}

/* The sunrise and sunset `days_back` days before `today`: the schedule's own, else night light's
 * times, or its location's (as night_light_target has them). */
static void day_times(const struct sh_settings *settings, const struct tm *today, int days_back,
                      struct sh_day_times *times, bool *polar_day) {
    const struct sh_profile_schedule *schedule = &settings->profile_schedule;
    const struct sh_effect_settings *fx = &settings->effects;
    *times = (struct sh_day_times){fx->sunrise, fx->sunset, false};
    if (schedule->own_times) {
        *times = (struct sh_day_times){schedule->sunrise, schedule->sunset, false};
        return;
    }
    if (fx->sunrise >= 0)
        return;
    struct tm day = *today;
    day.tm_mday -= days_back;
    day.tm_hour = 12;
    day.tm_min = day.tm_sec = 0;
    day.tm_isdst = -1;
    mktime(&day); // the date, and that day's offset from UTC
    bool up = false;
    if (!sh_solar_times(fx->latitude, fx->longitude, day.tm_year + 1900, day.tm_mon + 1,
                        day.tm_mday, day.tm_gmtoff / 3600.0, &times->sunrise, &times->sunset,
                        &up)) {
        times->polar = true;
        if (polar_day)
            *polar_day = up;
    }
}

/* False without a schedule. The clock is night light's: fixed by PAW_NIGHT_LIGHT_TIME for tests
 * at that time of today. */
static bool schedule_due(struct sh_server *server, struct due *due) {
    const struct sh_settings *settings = server_settings(server);
    const struct sh_profile_schedule *schedule = &settings->profile_schedule;
    if (!schedule->day[0] || !schedule->night[0])
        return false;
    time_t wall = time(NULL);
    struct tm today;
    localtime_r(&wall, &today);
    double minute = server->night_clock >= 0
                        ? server->night_clock
                        : today.tm_hour * 60 + today.tm_min + today.tm_sec / 60.0;
    *due = (struct due){.now = server->night_clock >= 0 ? local_time(&today, 0, minute)
                                                        : (int64_t)wall};
    struct sh_day_times days[LOOKBACK_DAYS];
    bool polar_day = false;
    for (int back = 0; back < LOOKBACK_DAYS; ++back)
        day_times(settings, &today, back, &days[back], back == 0 ? &polar_day : NULL);
    struct sh_boundary last;
    due->found = sh_last_boundary(days, LOOKBACK_DAYS, minute, &last);
    if (due->found) {
        due->time = local_time(&today, last.days_back, last.minute);
        due->sunrise = last.sunrise;
    } else {
        due->sunrise = polar_day;
    }
    snprintf(due->profile, sizeof(due->profile), "%s",
             due->sunrise ? schedule->day : schedule->night);
    return true;
}

/* Picks `profile` as `paw msg profile NAME` does, unless it is in use: saved, then the
 * configuration read again, which crossfades to it. Before the compositor runs (as it starts)
 * only the configuration is read again, nothing being set up yet that it changes. */
static void pick(struct sh_server *server, const char *profile) {
    if (!strcmp(server_settings(server)->profile, profile))
        return;
    char request[64], error[256] = "";
    snprintf(request, sizeof(request), "profile %s", profile);
    int argument = 0;
    const struct sh_callbacks *callbacks = server->callbacks;
    enum sh_action action =
        callbacks->command(callbacks->userdata, request, &argument, error, sizeof(error));
    if (action != SH_RELOAD) {
        wlr_log(WLR_ERROR, "Profile schedule: %s: %s", request, error[0] ? error : "refused");
        return;
    }
    if (server->running)
        reload_config(server);
    else if (!callbacks->reload(callbacks->userdata, error, sizeof(error)) && error[0])
        wlr_log(WLR_ERROR, "Profile schedule: %s", error);
}

static void apply(struct sh_server *server, const struct due *due) {
    server->profile_schedule.applied = due->time;
    snprintf(server->profile_schedule.brought, sizeof(server->profile_schedule.brought), "%s",
             due->profile);
    write_state(server, due->sunrise);
    time_t when = (time_t)due->time;
    struct tm at;
    localtime_r(&when, &at);
    wlr_log(WLR_INFO, "Profile schedule: the %s of %04d-%02d-%02d %02d:%02d brings %s",
            due->sunrise ? "sunrise" : "sunset", at.tm_year + 1900, at.tm_mon + 1, at.tm_mday,
            at.tm_hour, at.tm_min, due->profile);
    pick(server, due->profile);
}

/* Applies the last sunrise or sunset unless it was applied already, with the profile the
 * schedule names for it now; one applied later than now, the clock gone back, is applied anew.
 * Nothing changes while a rice is on trial, until it is kept or goes back. */
static void check(struct sh_server *server) {
    struct sh_profile_schedule_state *state = &server->profile_schedule;
    struct due due;
    bool scheduled = schedule_due(server, &due);
    if (scheduled && due.found && !server->rice.trial &&
        (due.time > state->applied || state->applied > due.now ||
         strcmp(due.profile, state->brought) != 0))
        apply(server, &due);
    // Whether the shell's Quick Settings say the schedule decides: the profile it wants is in use.
    bool following = scheduled && !strcmp(server_settings(server)->profile, due.profile);
    if (following != state->following) {
        state->following = following;
        if (server->running)
            notify_subscribers(server);
    }
    if (state->timer)
        wl_event_source_timer_update(
            state->timer, scheduled && server->night_clock < 0 ? PROFILE_SCHEDULE_TICK_MS : 0);
}

static int tick(void *data) {
    check(data);
    return 0;
}

void profile_schedule_start(struct sh_server *server) {
    read_state(server);
    server->profile_schedule.timer = wl_event_loop_add_timer(
        wl_display_get_event_loop(server->wl_display), tick, server);
    check(server);
}

void profile_schedule_reloaded(struct sh_server *server) {
    // Looked at once the reload is over: the configuration may have changed the schedule, and a
    // switch is a reload of its own.
    if (server->profile_schedule.timer)
        wl_event_source_timer_update(server->profile_schedule.timer, 1);
}

void profile_schedule_resume(struct sh_server *server) {
    struct due due;
    if (!schedule_due(server, &due)) {
        wlr_log(WLR_ERROR, "profile_auto: the configuration has no profile_schedule");
        return;
    }
    if (server->rice.trial) {
        wlr_log(WLR_ERROR, "profile_auto: the rice %s is on trial; keep it or revert it first",
                server->rice.name);
        return;
    }
    if (due.found) {
        server->profile_schedule.applied = due.time;
        snprintf(server->profile_schedule.brought, sizeof(server->profile_schedule.brought), "%s",
                 due.profile);
        write_state(server, due.sunrise);
    }
    pick(server, due.profile);
    check(server);
}

void profile_schedule_finish(struct sh_server *server) {
    if (server->profile_schedule.timer)
        wl_event_source_remove(server->profile_schedule.timer);
    server->profile_schedule.timer = NULL;
}

/* `get profile_schedule`: the profile the schedule wants now ("-" without one), the last sunrise
 * or sunset ("sunrise", "sunset", or "polar-day" or "polar-night" without one near) and its time
 * (-1 without one), the time of the last one applied (-1 for none), and whether the profile in
 * use is the one it wants (1 or 0), by tabs. */
void describe_profile_schedule(struct sh_server *server, int fd, const char *arguments) {
    struct due due;
    char reply[160];
    if (!schedule_due(server, &due)) {
        snprintf(reply, sizeof(reply), "ok\n-\t-\t-1\t%lld\t0\n",
                 (long long)server->profile_schedule.applied);
    } else {
        const char *kind = due.found ? (due.sunrise ? "sunrise" : "sunset")
                                     : (due.sunrise ? "polar-day" : "polar-night");
        snprintf(reply, sizeof(reply), "ok\n%s\t%s\t%lld\t%lld\t%d\n", due.profile, kind,
                 due.found ? (long long)due.time : -1LL,
                 (long long)server->profile_schedule.applied,
                 !strcmp(server_settings(server)->profile, due.profile));
    }
    control_reply(fd, reply);
}
