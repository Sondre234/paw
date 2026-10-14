// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* What `paw wrapped` reports on: the counters the compositor keeps of a day at the desktop
 * (src/compositor/wrapped.c), as plain text in $XDG_STATE_HOME/paw/wrapped/YYYY-MM-DD, a file
 * per day. Each line is tab-separated: `KEY VALUE` for a counter, `app ID SECONDS` for the time
 * an application had the focus, and `streak APP SECONDS START` for the longest the focus stayed
 * with one application without a break (START in seconds since 1970). Each session adds what
 * it counted to the day's file, so that the file holds the whole day. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The counters, named in the file by sh_wrapped_counter_name. Distances are in layout pixels
 * and in millimetres on the monitor, times in seconds. */
enum sh_wrapped_counter {
    SH_WRAPPED_SESSIONS, /* sessions that started that day */
    SH_WRAPPED_STARTED,  /* when the first of them started; the earliest is kept, not a sum */
    SH_WRAPPED_ACTIVE,   /* time at the desktop: input in the last two minutes, or a video */
    SH_WRAPPED_IDLE,     /* the rest of the time the session ran: away, locked, monitors off */
    SH_WRAPPED_POINTER_PX,
    SH_WRAPPED_POINTER_MM,
    SH_WRAPPED_CLICKS_LEFT,
    SH_WRAPPED_CLICKS_RIGHT,
    SH_WRAPPED_CLICKS_MIDDLE,
    SH_WRAPPED_CLICKS_OTHER,
    SH_WRAPPED_SCROLL_PX, /* scrolling, in the units a wheel's notch is 15 of */
    SH_WRAPPED_SCROLL_MM,
    SH_WRAPPED_KEYS, /* key presses, never which keys */
    SH_WRAPPED_GESTURES, /* touchpad swipes and pinches */
    SH_WRAPPED_WINDOWS_OPENED,
    SH_WRAPPED_WINDOWS_CLOSED,
    SH_WRAPPED_MOVES,   /* windows moved with the pointer */
    SH_WRAPPED_RESIZES, /* windows resized with the pointer */
    SH_WRAPPED_SNAPS,   /* windows snapped to a half or a quarter, by pointer or key */
    SH_WRAPPED_WORKSPACE_SWITCHES,
    SH_WRAPPED_OVERVIEW_OPENS,
    SH_WRAPPED_SWITCHER_OPENS,
    SH_WRAPPED_LAUNCHES, /* programs the spawn and terminal actions started */
    SH_WRAPPED_COUNTERS
};

enum { SH_WRAPPED_NAME_MAX = 128 };

struct sh_wrapped_app {
    char id[SH_WRAPPED_NAME_MAX];
    int64_t seconds;
};

/* A focus streak; 0 seconds for none. */
struct sh_wrapped_streak {
    char app[SH_WRAPPED_NAME_MAX];
    int64_t seconds, start;
};

/* A day's counters, or what a session counted since it last saved. Zeroed, it is an empty day;
 * sh_wrapped_day_clear frees what adding applications allocated. */
struct sh_wrapped_day {
    int64_t counters[SH_WRAPPED_COUNTERS];
    struct sh_wrapped_app *apps;
    size_t app_count, app_capacity;
    struct sh_wrapped_streak streak;
};

/* The counter's key in the file ("keys", "pointer_mm", ...). */
const char *sh_wrapped_counter_name(enum sh_wrapped_counter counter);
/* Frees the applications and empties the day. */
void sh_wrapped_day_clear(struct sh_wrapped_day *day);
/* Adds `value` to the counter, or for SH_WRAPPED_STARTED keeps the earlier time (0 is none). */
void sh_wrapped_day_add(struct sh_wrapped_day *day, enum sh_wrapped_counter counter,
                        int64_t value);
/* Adds `seconds` to the application's focused time; "" stands for a window without an app ID,
 * and a tab or a line break in one becomes a space. False when out of memory. */
bool sh_wrapped_day_add_app(struct sh_wrapped_day *day, const char *id, int64_t seconds);
/* Keeps the streak when it is longer than the day's. */
void sh_wrapped_day_add_streak(struct sh_wrapped_day *day, const char *app, int64_t seconds,
                               int64_t start);
/* Adds every counter, application and the streak of `from` to `into`. */
bool sh_wrapped_day_merge(struct sh_wrapped_day *into, const struct sh_wrapped_day *from);
/* Orders the applications by focused time, the longest first. */
void sh_wrapped_day_sort(struct sh_wrapped_day *day);
/* Adds one line of a day file (without its line break) to the day. False for a line it does
 * not take, a comment, a key it does not know (a newer paw's) or a value that is not a number,
 * which it passes over. */
bool sh_wrapped_day_read_line(struct sh_wrapped_day *day, const char *line);
/* Adds every line of `file`. */
void sh_wrapped_day_read(struct sh_wrapped_day *day, FILE *file);
/* Writes the day's lines as its file holds them: every counter, the streak if any, and the
 * applications in the order they are in. */
bool sh_wrapped_day_write(const struct sh_wrapped_day *day, FILE *file);

/* $XDG_STATE_HOME/paw/wrapped, else ~/.local/state/paw/wrapped, and "/DATE" after it when
 * `date` is given, creating nothing. False when neither variable says where. */
bool sh_wrapped_path(const char *date, char *path, size_t size);
/* The local date `when` falls on, as YYYY-MM-DD. */
void sh_wrapped_date(time_t when, char date[11]);
/* When the local day `when` falls on ends: the next midnight. */
time_t sh_wrapped_day_end(time_t when);
/* Adds `day` to the file of `date`, creating it and the directories above it (for the user
 * alone): the file as it was and `day` are written to DATE.new, which then replaces it, so that
 * a crash meanwhile leaves the old file whole. False, with errno, when it could not. */
bool sh_wrapped_save(const char *date, const struct sh_wrapped_day *day);

/* How much of the time from `from` to `to` (milliseconds on a monotonic clock) counts as active:
 * the part up to `idle_after` after the last input, `last_input`; all of it while `held` (an
 * idle inhibitor, as a video playing holds); none of it while `away` (the session locked, every
 * monitor off, another VT in front). The rest is idle. */
int64_t sh_wrapped_active(int64_t from, int64_t to, int64_t last_input, int64_t idle_after,
                          bool away, bool held);

#ifdef __cplusplus
}
#endif
