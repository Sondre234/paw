// SPDX-License-Identifier: GPL-3.0-or-later
#include "paw/wrapped_day.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *const names[SH_WRAPPED_COUNTERS] = {
    "sessions",       "started",        "active",       "idle",          "pointer_px",
    "pointer_mm",     "clicks_left",    "clicks_right", "clicks_middle", "clicks_other",
    "scroll_px",      "scroll_mm",      "keys",         "gestures",      "windows_opened",
    "windows_closed", "moves",          "resizes",      "snaps",         "workspace_switches",
    "overview_opens", "switcher_opens", "launches",
};

const char *sh_wrapped_counter_name(enum sh_wrapped_counter counter) {
    return counter < SH_WRAPPED_COUNTERS ? names[counter] : "";
}

void sh_wrapped_day_clear(struct sh_wrapped_day *day) {
    free(day->apps);
    memset(day, 0, sizeof(*day));
}

void sh_wrapped_day_add(struct sh_wrapped_day *day, enum sh_wrapped_counter counter,
                        int64_t value) {
    if (counter >= SH_WRAPPED_COUNTERS)
        return;
    int64_t *slot = &day->counters[counter];
    if (counter == SH_WRAPPED_STARTED) {
        if (value > 0 && (!*slot || value < *slot))
            *slot = value;
    } else {
        *slot += value;
    }
}

/* `text` as a name in the file: at most SH_WRAPPED_NAME_MAX - 1 bytes, not cut inside a UTF-8
 * character, with no tab or line break; "unknown" for none. */
static void copy_name(char out[SH_WRAPPED_NAME_MAX], const char *text) {
    if (!text || !*text)
        text = "unknown";
    size_t length = strlen(text);
    if (length >= SH_WRAPPED_NAME_MAX) {
        length = SH_WRAPPED_NAME_MAX - 1;
        while (length && ((unsigned char)text[length] & 0xC0) == 0x80)
            --length;
    }
    for (size_t i = 0; i < length; ++i)
        out[i] = text[i] == '\t' || text[i] == '\n' || text[i] == '\r' ? ' ' : text[i];
    out[length] = '\0';
}

bool sh_wrapped_day_add_app(struct sh_wrapped_day *day, const char *id, int64_t seconds) {
    char name[SH_WRAPPED_NAME_MAX];
    copy_name(name, id);
    for (size_t i = 0; i < day->app_count; ++i) {
        if (!strcmp(day->apps[i].id, name)) {
            day->apps[i].seconds += seconds;
            return true;
        }
    }
    if (day->app_count == day->app_capacity) {
        size_t capacity = day->app_capacity ? day->app_capacity * 2 : 16;
        struct sh_wrapped_app *apps = realloc(day->apps, capacity * sizeof(*apps));
        if (!apps)
            return false;
        day->apps = apps;
        day->app_capacity = capacity;
    }
    struct sh_wrapped_app *app = &day->apps[day->app_count++];
    memcpy(app->id, name, sizeof(name));
    app->seconds = seconds;
    return true;
}

void sh_wrapped_day_add_streak(struct sh_wrapped_day *day, const char *app, int64_t seconds,
                               int64_t start) {
    if (seconds <= day->streak.seconds)
        return;
    copy_name(day->streak.app, app);
    day->streak.seconds = seconds;
    day->streak.start = start;
}

bool sh_wrapped_day_merge(struct sh_wrapped_day *into, const struct sh_wrapped_day *from) {
    for (int counter = 0; counter < SH_WRAPPED_COUNTERS; ++counter)
        sh_wrapped_day_add(into, counter, from->counters[counter]);
    bool done = true;
    for (size_t i = 0; i < from->app_count; ++i)
        done &= sh_wrapped_day_add_app(into, from->apps[i].id, from->apps[i].seconds);
    if (from->streak.seconds > 0)
        sh_wrapped_day_add_streak(into, from->streak.app, from->streak.seconds, from->streak.start);
    return done;
}

static int longer_first(const void *a, const void *b) {
    const struct sh_wrapped_app *x = a, *y = b;
    if (x->seconds != y->seconds)
        return x->seconds > y->seconds ? -1 : 1;
    return strcmp(x->id, y->id);
}

void sh_wrapped_day_sort(struct sh_wrapped_day *day) {
    if (day->app_count > 1)
        qsort(day->apps, day->app_count, sizeof(*day->apps), longer_first);
}

/* A whole decimal number, perhaps negative, and nothing else. */
static bool parse_number(const char *text, int64_t *value) {
    if (!*text)
        return false;
    char *end;
    errno = 0;
    long long number = strtoll(text, &end, 10);
    if (*end || errno)
        return false;
    *value = number;
    return true;
}

bool sh_wrapped_day_read_line(struct sh_wrapped_day *day, const char *line) {
    char copy[512];
    if (strlen(line) >= sizeof(copy))
        return false;
    strcpy(copy, line);
    char *fields[5];
    int count = 0;
    for (char *field = copy; count < 5;) {
        fields[count++] = field;
        char *tab = strchr(field, '\t');
        if (!tab)
            break;
        *tab = '\0';
        field = tab + 1;
    }
    int64_t value, start;
    if (!strcmp(fields[0], "app") && count == 3 && parse_number(fields[2], &value))
        return sh_wrapped_day_add_app(day, fields[1], value);
    if (!strcmp(fields[0], "streak") && count == 4 && parse_number(fields[2], &value) &&
        parse_number(fields[3], &start)) {
        sh_wrapped_day_add_streak(day, fields[1], value, start);
        return true;
    }
    if (count != 2 || !parse_number(fields[1], &value))
        return false;
    for (int counter = 0; counter < SH_WRAPPED_COUNTERS; ++counter) {
        if (!strcmp(fields[0], names[counter])) {
            sh_wrapped_day_add(day, counter, value);
            return true;
        }
    }
    return false;
}

void sh_wrapped_day_read(struct sh_wrapped_day *day, FILE *file) {
    char line[512];
    while (fgets(line, sizeof(line), file)) {
        size_t length = strcspn(line, "\n");
        if (!line[length] && !feof(file)) { // longer than any line paw writes: pass it over
            int c;
            while ((c = fgetc(file)) != EOF && c != '\n') {
            }
            continue;
        }
        line[length] = '\0';
        sh_wrapped_day_read_line(day, line);
    }
}

bool sh_wrapped_day_write(const struct sh_wrapped_day *day, FILE *file) {
    for (int counter = 0; counter < SH_WRAPPED_COUNTERS; ++counter)
        fprintf(file, "%s\t%" PRId64 "\n", names[counter], day->counters[counter]);
    if (day->streak.seconds > 0)
        fprintf(file, "streak\t%s\t%" PRId64 "\t%" PRId64 "\n", day->streak.app,
                day->streak.seconds, day->streak.start);
    for (size_t i = 0; i < day->app_count; ++i)
        fprintf(file, "app\t%s\t%" PRId64 "\n", day->apps[i].id, day->apps[i].seconds);
    return !ferror(file);
}

bool sh_wrapped_path(const char *date, char *path, size_t size) {
    const char *state = getenv("XDG_STATE_HOME"), *home = getenv("HOME");
    int length;
    if (state && state[0] == '/')
        length =
            snprintf(path, size, "%s/paw/wrapped%s%s", state, date ? "/" : "", date ? date : "");
    else if (home && home[0] == '/')
        length = snprintf(path, size, "%s/.local/state/paw/wrapped%s%s", home, date ? "/" : "",
                          date ? date : "");
    else
        return false;
    return length > 0 && (size_t)length < size;
}

void sh_wrapped_date(time_t when, char date[11]) {
    struct tm local;
    if (!localtime_r(&when, &local) || !strftime(date, 11, "%Y-%m-%d", &local))
        strcpy(date, "1970-01-01");
}

time_t sh_wrapped_day_end(time_t when) {
    struct tm local;
    if (!localtime_r(&when, &local))
        return when + 24 * 60 * 60;
    local.tm_mday += 1;
    local.tm_hour = local.tm_min = local.tm_sec = 0;
    local.tm_isdst = -1;
    time_t end = mktime(&local);
    return end > when ? end : when + 24 * 60 * 60;
}

/* Creates `path` and the directories above it, for the user alone, as `mkdir -p`. */
static bool make_directories(char *path) {
    for (char *c = path + 1; *c; ++c) {
        if (*c != '/')
            continue;
        *c = '\0';
        int result = mkdir(path, 0700);
        *c = '/';
        if (result < 0 && errno != EEXIST)
            return false;
    }
    return mkdir(path, 0700) == 0 || errno == EEXIST;
}

bool sh_wrapped_save(const char *date, const struct sh_wrapped_day *day) {
    char directory[PATH_MAX], path[PATH_MAX], temporary[PATH_MAX + 4];
    if (!sh_wrapped_path(NULL, directory, sizeof(directory)) ||
        !sh_wrapped_path(date, path, sizeof(path)) ||
        snprintf(temporary, sizeof(temporary), "%s.new", path) >= (int)sizeof(temporary)) {
        errno = ENAMETOOLONG;
        return false;
    }
    if (!make_directories(directory))
        return false;
    struct sh_wrapped_day merged = {0};
    FILE *old = fopen(path, "re");
    if (old) {
        sh_wrapped_day_read(&merged, old);
        fclose(old);
    } else if (errno != ENOENT) {
        return false;
    }
    if (!sh_wrapped_day_merge(&merged, day)) {
        sh_wrapped_day_clear(&merged);
        errno = ENOMEM;
        return false;
    }
    sh_wrapped_day_sort(&merged);
    int fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    FILE *file = fd >= 0 ? fdopen(fd, "w") : NULL;
    if (!file) {
        int error = errno;
        if (fd >= 0)
            close(fd);
        sh_wrapped_day_clear(&merged);
        errno = error;
        return false;
    }
    fputs("# paw wrapped: what the desktop counted this day; `paw wrapped` reads it\n", file);
    bool written =
        sh_wrapped_day_write(&merged, file) && fflush(file) == 0 && fsync(fileno(file)) == 0;
    int error = errno;
    if (fclose(file) != 0 && written) {
        written = false;
        error = errno;
    }
    sh_wrapped_day_clear(&merged);
    if (written && rename(temporary, path) < 0) {
        written = false;
        error = errno;
    }
    if (!written) {
        unlink(temporary);
        errno = error;
    }
    return written;
}

int64_t sh_wrapped_active(int64_t from, int64_t to, int64_t last_input, int64_t idle_after,
                          bool away, bool held) {
    if (to <= from || away)
        return 0;
    if (held)
        return to - from;
    int64_t until = last_input + idle_after;
    if (until <= from)
        return 0;
    return (until < to ? until : to) - from;
}
