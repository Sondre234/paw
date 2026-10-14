/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Counting for `paw wrapped` (wrapped.enabled): pointer travel, clicks, scrolling, key presses
 * (how many, never which), gestures, what happens to windows and workspaces, the time each
 * application has the focus, and the time active and idle. What is counted is added to the day's
 * file (src/wrapped_day.c) every few minutes, as the session goes idle, locks or sleeps, and as it
 * ends; `get wrapped` says what is not saved yet. A hook elsewhere is one call here, and nothing
 * is allocated per event. */
#include "server.h"

/* Without input for this long the session is idle, unless an idle inhibitor (a video) holds
 * it. */
#define IDLE_AFTER_MS (2 * 60 * 1000)
/* How often what was counted is saved while the session runs. */
#define SAVE_MS (5 * 60 * 1000)
/* Millimetres in a logical pixel of a monitor that does not give its size: 96 dots an inch. */
#define DEFAULT_MM_PER_PX (25.4 / 96)

struct sh_wrapped {
    struct sh_server *server;
    struct wl_event_source *timer;
    int save_ms, idle_after_ms;
    /* Seconds added to the wall clock, for a test's midnight (PAW_TEST_WRAPPED_MIDNIGHT). */
    time_t clock_offset;
    /* The day counted for, and when it ends by the wall clock. */
    char date[11];
    time_t day_end;
    /* What was counted since the last save: whole units in `day`, and what is less than a whole
     * one so far of the distances (pixels and millimetres) and the times (milliseconds). */
    struct sh_wrapped_day day;
    double pointer_px, pointer_mm, scroll_px, scroll_mm;
    int64_t active_ms, idle_ms;
    /* The time is counted up to `settled` (now_ms()), with what held since the last change: the
     * last input, whether the session was away (locked, every monitor off, another VT in front)
     * or held active by an idle inhibitor, and the focused window's app ID ("" for none). */
    int64_t settled, last_input;
    bool away, held;
    char app[SH_WRAPPED_NAME_MAX];
    /* The application that had the focus last, `app` but while nothing has it: its focused time
     * not yet in `day`, and its streak, how long the focus has stayed with it while active, and
     * since when by the wall clock. The focus leaving a window to come straight back, as a click
     * on it does, changes neither. */
    char last_app[SH_WRAPPED_NAME_MAX];
    int64_t focus_ms, streak_ms;
    time_t streak_start;
    bool dirty; // something other than idle time was counted since the last save
    unsigned saves;
    time_t saved_at;
    bool failed_logged;
};

static time_t wall_time(const struct sh_wrapped *wrapped) {
    return time(NULL) + wrapped->clock_offset;
}

static void start_day(struct sh_wrapped *wrapped, time_t now) {
    sh_wrapped_date(now, wrapped->date);
    wrapped->day_end = sh_wrapped_day_end(now);
}

/* The streak under way ends: the focus moved to another application, or the session went idle
 * or was active without a window focused. */
static void end_streak(struct sh_wrapped *wrapped) {
    if (wrapped->streak_ms >= 1000)
        sh_wrapped_day_add_streak(&wrapped->day, wrapped->last_app, wrapped->streak_ms / 1000,
                                  wrapped->streak_start);
    wrapped->streak_ms = 0;
}

/* Counts the time from `settled` to `to` as what held then had it: active or idle, the active
 * part the focused application's and its streak's, which ends with idle time. */
static void count_time(struct sh_wrapped *wrapped, int64_t to) {
    if (to <= wrapped->settled)
        return;
    int64_t active = sh_wrapped_active(wrapped->settled, to, wrapped->last_input,
                                       wrapped->idle_after_ms, wrapped->away, wrapped->held);
    wrapped->active_ms += active;
    wrapped->idle_ms += to - wrapped->settled - active;
    if (active > 0) {
        wrapped->dirty = true;
        if (wrapped->app[0]) {
            if (!wrapped->streak_ms)
                wrapped->streak_start =
                    wall_time(wrapped) - (time_t)((now_ms() - wrapped->settled) / 1000);
            wrapped->focus_ms += active;
            wrapped->streak_ms += active;
        } else {
            end_streak(wrapped);
        }
    }
    if (to - wrapped->settled > active)
        end_streak(wrapped);
    wrapped->settled = to;
}

/* Moves the whole units counted so far into `day`, the focused application's time too. */
static void fold(struct sh_wrapped *wrapped) {
    struct sh_wrapped_day *day = &wrapped->day;
    double *distances[] = {&wrapped->pointer_px, &wrapped->pointer_mm, &wrapped->scroll_px,
                           &wrapped->scroll_mm};
    enum sh_wrapped_counter counters[] = {SH_WRAPPED_POINTER_PX, SH_WRAPPED_POINTER_MM,
                                          SH_WRAPPED_SCROLL_PX, SH_WRAPPED_SCROLL_MM};
    for (size_t i = 0; i < sizeof(counters) / sizeof(*counters); ++i) {
        double whole = floor(*distances[i]);
        day->counters[counters[i]] += (int64_t)whole;
        *distances[i] -= whole;
    }
    day->counters[SH_WRAPPED_ACTIVE] += wrapped->active_ms / 1000;
    wrapped->active_ms %= 1000;
    day->counters[SH_WRAPPED_IDLE] += wrapped->idle_ms / 1000;
    wrapped->idle_ms %= 1000;
    if (wrapped->focus_ms >= 1000) {
        sh_wrapped_day_add_app(day, wrapped->last_app, wrapped->focus_ms / 1000);
        wrapped->focus_ms %= 1000;
    }
}

/* Adds what was counted to the day's file, and starts counting afresh; what is less than a whole
 * unit stays, as does the streak under way, which a later save may find longer. */
static void save(struct sh_wrapped *wrapped) {
    fold(wrapped);
    if (wrapped->streak_ms >= 1000)
        sh_wrapped_day_add_streak(&wrapped->day, wrapped->last_app, wrapped->streak_ms / 1000,
                                  wrapped->streak_start);
    if (!sh_wrapped_save(wrapped->date, &wrapped->day)) {
        if (!wrapped->failed_logged)
            wlr_log(WLR_ERROR, "Wrapped: cannot save %s: %s", wrapped->date, strerror(errno));
        wrapped->failed_logged = true;
        return; // kept, for the next save to try again
    }
    wrapped->failed_logged = false;
    sh_wrapped_day_clear(&wrapped->day);
    wrapped->dirty = false;
    ++wrapped->saves;
    wrapped->saved_at = wall_time(wrapped);
}

/* Counts the time up to now. Past midnight, what came before it is the old day's, saved before
 * the new day starts. */
static void settle(struct sh_wrapped *wrapped) {
    int64_t now = now_ms();
    time_t wall = wall_time(wrapped);
    if (wall >= wrapped->day_end) {
        int64_t after = (int64_t)(wall - wrapped->day_end) * 1000;
        int64_t span = now - wrapped->settled;
        count_time(wrapped, now - (after < span ? after : span));
        save(wrapped);
        start_day(wrapped, wall);
    }
    count_time(wrapped, now);
}

/* Whether every monitor in the layout is turned off; with none at all, as while the session
 * starts, nothing says the user is away. */
static bool screens_off(struct sh_server *server) {
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        if (!output->powered_off)
            return false;
    }
    return !wl_list_empty(&server->outputs);
}

void wrapped_follow(struct sh_server *server) {
    struct sh_wrapped *wrapped = server->wrapped;
    if (!wrapped)
        return;
    settle(wrapped);
    wrapped->away = server->locked || screens_off(server);
#if WLR_HAS_SESSION
    wrapped->away |= server->session && !server->session->active;
#endif
    wrapped->held = server->inhibitors > 0;
    const char *id = server->focused_toplevel ? toplevel_app_id(server->focused_toplevel) : NULL;
    snprintf(wrapped->app, sizeof(wrapped->app), "%s", id ? id : "");
    if (!wrapped->app[0] || !strcmp(wrapped->app, wrapped->last_app))
        return;
    if (wrapped->focus_ms > 0) // to the nearest second, as it is no longer added to
        sh_wrapped_day_add_app(&wrapped->day, wrapped->last_app, (wrapped->focus_ms + 500) / 1000);
    wrapped->focus_ms = 0;
    end_streak(wrapped);
    memcpy(wrapped->last_app, wrapped->app, sizeof(wrapped->app));
}

void wrapped_input(struct sh_server *server) {
    struct sh_wrapped *wrapped = server->wrapped;
    if (!wrapped)
        return;
    settle(wrapped);
    wrapped->last_input = wrapped->settled;
}

void wrapped_count(struct sh_server *server, enum sh_wrapped_counter counter) {
    struct sh_wrapped *wrapped = server->wrapped;
    if (!wrapped)
        return;
    ++wrapped->day.counters[counter];
    wrapped->dirty = true;
}

/* Millimetres in a logical pixel of the monitor at a point of the layout, from the size its
 * EDID gives, else at 96 dots an inch; a size that makes no sense (a projector's 0, an aspect
 * ratio given as centimetres) is passed over. */
static double mm_per_px(struct sh_server *server, double x, double y) {
    struct wlr_output *output = wlr_output_layout_output_at(server->output_layout, x, y);
    if (!output || output->phys_width <= 0 || output->width <= 0)
        return DEFAULT_MM_PER_PX;
    double physical = (double)output->phys_width / output->width; // mm per buffer pixel
    if (physical < 0.05 || physical > 2)
        return DEFAULT_MM_PER_PX;
    return physical * output->scale;
}

static void add_motion(struct sh_server *server, double distance) {
    struct sh_wrapped *wrapped = server->wrapped;
    if (!(distance > 0))
        return;
    wrapped->pointer_px += distance;
    wrapped->pointer_mm += distance * mm_per_px(server, server->cursor->x, server->cursor->y);
    wrapped->dirty = true;
}

void wrapped_motion(struct sh_server *server, double dx, double dy) {
    if (server->wrapped)
        add_motion(server, hypot(dx, dy));
}

void wrapped_motion_absolute(struct sh_server *server,
                             const struct wlr_pointer_motion_absolute_event *event) {
    if (!server->wrapped)
        return;
    double x, y;
    wlr_cursor_absolute_to_layout_coords(server->cursor, &event->pointer->base, event->x, event->y,
                                         &x, &y);
    add_motion(server, hypot(x - server->cursor->x, y - server->cursor->y));
}

void wrapped_button(struct sh_server *server, const struct wlr_pointer_button_event *event) {
    if (!server->wrapped || event->state != WL_POINTER_BUTTON_STATE_PRESSED)
        return;
    wrapped_count(server, event->button == BTN_LEFT     ? SH_WRAPPED_CLICKS_LEFT
                          : event->button == BTN_RIGHT  ? SH_WRAPPED_CLICKS_RIGHT
                          : event->button == BTN_MIDDLE ? SH_WRAPPED_CLICKS_MIDDLE
                                                        : SH_WRAPPED_CLICKS_OTHER);
}

void wrapped_axis(struct sh_server *server, const struct wlr_pointer_axis_event *event) {
    struct sh_wrapped *wrapped = server->wrapped;
    if (!wrapped)
        return;
    double distance = fabs(event->delta);
    wrapped->scroll_px += distance;
    wrapped->scroll_mm += distance * mm_per_px(server, server->cursor->x, server->cursor->y);
    wrapped->dirty = true;
}

void wrapped_grab_done(struct sh_server *server) {
    struct sh_toplevel *toplevel = server->grabbed_toplevel;
    if (!server->wrapped || !toplevel ||
        (server->cursor_mode != SH_CURSOR_MOVE && server->cursor_mode != SH_CURSOR_RESIZE))
        return;
    // Where the pointer was pressed: lift_press keeps it for a move, the grab's offset from the
    // edges taken gives it for a resize, and a fullscreen window's move keeps it as it is.
    double x = server->grab_x, y = server->grab_y;
    if (server->cursor_mode == SH_CURSOR_MOVE && !server->grab_fullscreen) {
        x = server->lift.press_x;
        y = server->lift.press_y;
    } else if (server->cursor_mode == SH_CURSOR_RESIZE) {
        struct wlr_box box = server->grab_geobox;
        x += box.x + (server->resize_edges & WLR_EDGE_RIGHT ? box.width : 0);
        y += box.y + (server->resize_edges & WLR_EDGE_BOTTOM ? box.height : 0);
    }
    // A press on a title bar that let go in place moved nothing.
    if (hypot(server->cursor->x - x, server->cursor->y - y) >= SH_DRAG_THRESHOLD)
        wrapped_count(server, server->cursor_mode == SH_CURSOR_MOVE ? SH_WRAPPED_MOVES
                                                                    : SH_WRAPPED_RESIZES);
}

void wrapped_save(struct sh_server *server) {
    struct sh_wrapped *wrapped = server->wrapped;
    if (!wrapped)
        return;
    wrapped_follow(server);
    save(wrapped);
}

static int save_timer(void *data) {
    struct sh_wrapped *wrapped = data;
    settle(wrapped);
    if (wrapped->dirty)
        save(wrapped);
    wl_event_source_timer_update(wrapped->timer, wrapped->save_ms);
    return 0;
}

/* A test's setting of `name`, in milliseconds, else `fallback`. */
static int test_ms(const char *name, int fallback) {
    const char *value = getenv(name);
    int ms = value && *value ? atoi(value) : 0;
    return ms > 0 ? ms : fallback;
}

/* Why the compositor counts nothing, or NULL when it counts: wrapped.enabled is off, or it is not
 * a session at the machine. A nested paw's input is the desktop it runs in's, counted there if at
 * all, and under --headless (the tests, the benchmark) it is nobody's, unless a test asks for it
 * with PAW_TEST_WRAPPED. */
static const char *not_counted(struct sh_server *server) {
    if (!server_settings(server)->wrapped)
        return "wrapped.enabled is false";
    const char *test = getenv("PAW_TEST_WRAPPED");
    if (test && *test)
        return NULL;
#if WLR_HAS_SESSION
    if (server->session)
        return NULL;
#endif
    return "only a standalone session (paw --session) counts";
}

/* Starts counting, as a session starts (`session`) or wrapped.enabled is turned on. */
static void start(struct sh_server *server, bool session) {
    if (server->wrapped || not_counted(server))
        return;
    struct sh_wrapped *wrapped = calloc(1, sizeof(*wrapped));
    if (!wrapped)
        return;
    wrapped->server = server;
    wrapped->save_ms = test_ms("PAW_TEST_WRAPPED_SAVE_MS", SAVE_MS);
    wrapped->idle_after_ms = test_ms("PAW_TEST_WRAPPED_IDLE_MS", IDLE_AFTER_MS);
    // PAW_TEST_WRAPPED_MIDNIGHT=SECONDS: the day ends that long after the session starts.
    const char *midnight = getenv("PAW_TEST_WRAPPED_MIDNIGHT");
    if (midnight && *midnight) {
        time_t now = time(NULL);
        wrapped->clock_offset = sh_wrapped_day_end(now) - now - atoi(midnight);
    }
    time_t now = wall_time(wrapped);
    start_day(wrapped, now);
    if (session) {
        sh_wrapped_day_add(&wrapped->day, SH_WRAPPED_SESSIONS, 1);
        sh_wrapped_day_add(&wrapped->day, SH_WRAPPED_STARTED, now);
    }
    wrapped->dirty = true;
    wrapped->settled = wrapped->last_input = now_ms();
    wrapped->timer =
        wl_event_loop_add_timer(wl_display_get_event_loop(server->wl_display), save_timer, wrapped);
    if (wrapped->timer)
        wl_event_source_timer_update(wrapped->timer, wrapped->save_ms);
    server->wrapped = wrapped;
    wrapped_follow(server);
    char path[PATH_MAX];
    if (sh_wrapped_path(NULL, path, sizeof(path)))
        wlr_log(WLR_INFO, "Wrapped: counting into %s", path);
}

void wrapped_init(struct sh_server *server) { start(server, true); }

void wrapped_finish(struct sh_server *server) {
    struct sh_wrapped *wrapped = server->wrapped;
    if (!wrapped)
        return;
    wrapped_follow(server);
    save(wrapped);
    if (wrapped->timer)
        wl_event_source_remove(wrapped->timer);
    sh_wrapped_day_clear(&wrapped->day);
    free(wrapped);
    server->wrapped = NULL;
}

void wrapped_reload(struct sh_server *server) {
    if (!not_counted(server))
        start(server, false);
    else if (server->wrapped) {
        wrapped_finish(server);
        wlr_log(WLR_INFO, "Wrapped: wrapped.enabled is false; counting nothing");
    }
}

/* `get wrapped`: whether counting is on (`enabled yes`, or `enabled no` and why), and while it is
 * the day counted for (`day YYYY-MM-DD`), how many times this session saved and when it last did
 * (seconds since 1970, 0 for never), what the time counts as now (`state active|idle|away`) and
 * the focused application with its streak so far (`focus APP SECONDS`, "-" for none); then what
 * was counted and not saved yet, as lines of a day file, the time up to now included. */
void describe_wrapped(struct sh_server *server, int fd, const char *arguments) {
    struct sh_wrapped *wrapped = server->wrapped;
    if (!wrapped) {
        const char *why = not_counted(server);
        char line[128];
        snprintf(line, sizeof(line), "ok\nenabled\tno\t%s\n", why ? why : "out of memory");
        control_reply(fd, line);
        return;
    }
    int64_t now = now_ms();
    int64_t active = sh_wrapped_active(wrapped->settled, now, wrapped->last_input,
                                       wrapped->idle_after_ms, wrapped->away, wrapped->held);
    int64_t idle = now - wrapped->settled - active;
    const char *state = wrapped->away ? "away"
                        : wrapped->held || now - wrapped->last_input < wrapped->idle_after_ms
                            ? "active"
                            : "idle";
    // The active time since the last count is the focused application's and its streak's, which
    // goes on unless that time was partly idle.
    int64_t focused = wrapped->focus_ms + (wrapped->app[0] ? active : 0);
    int64_t streak = wrapped->streak_ms + (wrapped->app[0] ? active : 0);
    bool streaking = wrapped->app[0] && !idle;
    time_t streak_start = wrapped->streak_ms
                              ? wrapped->streak_start
                              : wall_time(wrapped) - (time_t)((now - wrapped->settled) / 1000);
    char line[512];
    snprintf(line, sizeof(line),
             "ok\nenabled\tyes\nday\t%s\nsaves\t%u\nsaved\t%lld\nstate\t%s\nfocus\t%s\t%lld\n",
             wrapped->date, wrapped->saves, (long long)wrapped->saved_at, state,
             wrapped->app[0] ? wrapped->app : "-", (long long)(streaking ? streak / 1000 : 0));
    control_reply(fd, line);
    // A copy of the day with what is less than whole and the time since the last count added.
    struct sh_wrapped_day day = {0};
    sh_wrapped_day_merge(&day, &wrapped->day);
    sh_wrapped_day_add(&day, SH_WRAPPED_POINTER_PX, (int64_t)wrapped->pointer_px);
    sh_wrapped_day_add(&day, SH_WRAPPED_POINTER_MM, (int64_t)wrapped->pointer_mm);
    sh_wrapped_day_add(&day, SH_WRAPPED_SCROLL_PX, (int64_t)wrapped->scroll_px);
    sh_wrapped_day_add(&day, SH_WRAPPED_SCROLL_MM, (int64_t)wrapped->scroll_mm);
    sh_wrapped_day_add(&day, SH_WRAPPED_ACTIVE, (wrapped->active_ms + active) / 1000);
    sh_wrapped_day_add(&day, SH_WRAPPED_IDLE, (wrapped->idle_ms + idle) / 1000);
    if (focused >= 1000)
        sh_wrapped_day_add_app(&day, wrapped->last_app, focused / 1000);
    if (streak >= 1000)
        sh_wrapped_day_add_streak(&day, wrapped->last_app, streak / 1000, streak_start);
    sh_wrapped_day_sort(&day);
    char *text = NULL;
    size_t size = 0;
    FILE *file = open_memstream(&text, &size);
    if (file) {
        sh_wrapped_day_write(&day, file);
        fclose(file);
        control_reply(fd, text);
    }
    free(text);
    sh_wrapped_day_clear(&day);
}
