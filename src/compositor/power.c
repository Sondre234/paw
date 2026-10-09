/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Power: suspend, hibernate, reboot and power off through logind, and what it allows of them,
 * over a connection that never blocks the compositor (src/login1.c); locking the screen with
 * power.lock_command, before the machine sleeps too; logging out; and closing every window
 * before power off, reboot and log out. */
#include "server.h"

#include <stdarg.h>

/* How long a locker gets to lock the screen before a suspend gives up. */
#define LOCK_TIMEOUT_MS 5000

/* Each power action, in the order menus list them: its name in the action table and in
 * messages, and the logind method that carries it out (SH_LOGIN1_METHODS for none). */
static const struct {
    enum sh_action action;
    const char *name, *label;
    enum sh_login1_method method;
} actions[] = {
    {SH_LOCK, "lock", "lock the screen", SH_LOGIN1_METHODS},
    {SH_SUSPEND, "suspend", "suspend", SH_LOGIN1_SUSPEND},
    {SH_HIBERNATE, "hibernate", "hibernate", SH_LOGIN1_HIBERNATE},
    {SH_REBOOT, "reboot", "reboot", SH_LOGIN1_REBOOT},
    {SH_POWER_OFF, "poweroff", "power off", SH_LOGIN1_POWER_OFF},
    {SH_LOGOUT, "logout", "log out", SH_LOGIN1_METHODS},
};

static int action_index(enum sh_action action) {
    for (size_t i = 0; i < sizeof(actions) / sizeof(*actions); ++i)
        if (actions[i].action == action)
            return (int)i;
    return -1;
}

bool power_action(enum sh_action action) {
    return action_index(action) >= 0;
}

const char *power_action_name(enum sh_action action) {
    int index = action_index(action);
    return index >= 0 ? actions[index].name : "";
}

static const char *action_label(enum sh_action action) {
    int index = action_index(action);
    return index >= 0 ? actions[index].label : "";
}

/* Tells the user what went wrong with a power action they no longer wait on: in the log, and
 * across the panel. */
static void power_report(struct sh_server *server, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
static void power_report(struct sh_server *server, const char *format, ...) {
    char text[320];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    report_failure(server, "power-error", text);
}

/* Whether power.lock_command can lock the screen; with `start`, starts it too. */
static bool locker(struct sh_server *server, bool start, char *error, size_t error_size) {
    const struct sh_callbacks *callbacks = server->callbacks;
    if (!callbacks->lock) {
        snprintf(error, error_size, "no screen locker");
        return false;
    }
    return callbacks->lock(callbacks->userdata, start, error, error_size);
}

/* Starts the screen locker, unless one holds the screen already. One that has crashed left the
 * screen covered and locked; another takes its place. */
static bool power_lock(struct sh_server *server, char *error, size_t error_size) {
    if (server->lock)
        return true;
    if (!locker(server, true, error, error_size))
        return false;
    wlr_log(WLR_INFO, "Started the screen locker");
    return true;
}

/* Starts the locker for a sleep, unless one started for that is still on its way. */
static bool lock_for_sleep(struct sh_server *server, char *error, size_t error_size) {
    struct sh_power *power = &server->power;
    if (power->locker_started && now_ms() - power->locker_started < LOCK_TIMEOUT_MS)
        return true;
    if (!power_lock(server, error, error_size))
        return false;
    power->locker_started = now_ms();
    return true;
}

/* Whether the screen locks before the machine sleeps: power.lock_before_sleep, and a locker to
 * lock it with. */
static bool lock_wanted(struct sh_server *server) {
    char ignored[256];
    return server_settings(server)->lock_before_sleep &&
           locker(server, false, ignored, sizeof(ignored));
}

static bool lock_holds(struct sh_server *server) {
    return server->lock && server->lock->locked_sent;
}

/* Watches the connection for what sd-bus waits for next. */
static void power_watch(struct sh_server *server) {
    struct sh_power *power = &server->power;
    if (!power->login1)
        return;
    wl_event_source_fd_update(power->bus, WL_EVENT_READABLE | (sh_login1_wants_write(power->login1)
                                                                    ? WL_EVENT_WRITABLE
                                                                    : 0));
    int timeout = sh_login1_timeout(power->login1);
    wl_event_source_timer_update(power->bus_timer, timeout < 0 ? 0 : timeout > 0 ? timeout : 1);
}

/* Lets the machine sleep: closes the delay inhibitor. */
static void release_sleep(struct sh_server *server) {
    struct sh_power *power = &server->power;
    if (power->sleep_delay < 0)
        return;
    close(power->sleep_delay);
    power->sleep_delay = -1;
}

/* Holds a logind delay inhibitor while the screen has to lock before sleeping, so that a sleep
 * anything asks for (the lid, an idle daemon, another program) waits for the lock; lets go of
 * it otherwise. */
static void hold_sleep(struct sh_server *server) {
    struct sh_power *power = &server->power;
    if (!power->login1 || !lock_wanted(server)) {
        release_sleep(server);
        return;
    }
    if (power->sleep_delay >= 0 || power->inhibiting || power->before_sleep)
        return;
    power->inhibiting = sh_login1_inhibit_sleep(power->login1, "Locking the screen first");
    power_watch(server);
}

static void inhibited(void *data, int fd, const char *error) {
    struct sh_server *server = data;
    struct sh_power *power = &server->power;
    power->inhibiting = false;
    if (fd < 0) {
        wlr_log(WLR_ERROR, "logind will not wait for the screen to lock before sleeping: %s",
                error);
        return;
    }
    release_sleep(server);
    power->sleep_delay = fd;
    // Wanted no longer, or late for a sleep that has begun with the lock holding.
    if (!lock_wanted(server) || (power->before_sleep && lock_holds(server)))
        release_sleep(server);
}

static void prepare_for_sleep(void *data, bool before) {
    struct sh_server *server = data;
    struct sh_power *power = &server->power;
    power->before_sleep = before;
    if (!before) {
        wlr_log(WLR_INFO, "The machine woke up");
        hold_sleep(server);
        idle_activity(server); // as input would: the monitors come on, the steps start again
        return;
    }
    // A suspend of ours still waiting for the lock is not needed any more: the machine sleeps.
    if (power->step == SH_POWER_LOCKING) {
        power->step = SH_POWER_IDLE;
        wl_event_source_timer_update(power->timer, 0);
    }
    if (power->sleep_delay < 0)
        return;
    char error[256];
    if (lock_holds(server)) {
        release_sleep(server);
    } else if (!lock_for_sleep(server, error, sizeof(error))) {
        wlr_log(WLR_ERROR, "Sleeping without locking: %s", error);
        release_sleep(server);
    }
}

/* logind's LidClosed, which also tells of a lid closed before paw started (libinput says so
 * only of a lid it knows to be reliable). */
static void lid_answered(void *data, bool closed) {
    lid_from_logind(data, closed);
}

static void power_disconnect(struct sh_server *server) {
    struct sh_power *power = &server->power;
    if (!power->login1)
        return;
    wl_event_source_remove(power->bus);
    wl_event_source_remove(power->bus_timer);
    power->bus = power->bus_timer = NULL;
    sh_login1_destroy(power->login1);
    power->login1 = NULL;
    memset(power->answers, 0, sizeof(power->answers));
    release_sleep(server);
    lid_from_logind(server, false); // only the switches tell of the lid now
    power->inhibiting = power->before_sleep = false;
    if (power->step == SH_POWER_CALLING) {
        power->step = SH_POWER_IDLE;
        power_report(server, "%s: lost the connection to logind", action_label(power->action));
    }
}

static int bus_ready(int fd, uint32_t mask, void *data) {
    struct sh_server *server = data;
    if (!sh_login1_dispatch(server->power.login1)) {
        wlr_log(WLR_ERROR, "Lost the connection to logind");
        power_disconnect(server);
        notify_subscribers(server);
        return 0;
    }
    power_watch(server);
    return 0;
}

static int bus_timeout(void *data) {
    return bus_ready(-1, 0, data);
}

/* Asks logind afresh which actions it allows; the answers come to `answered`. */
static void power_ask(struct sh_server *server) {
    struct sh_power *power = &server->power;
    if (!power->login1)
        return;
    for (int i = 0; i < SH_LOGIN1_METHODS; ++i)
        sh_login1_ask(power->login1, (enum sh_login1_method)i);
    power_watch(server);
}

static void answered(void *data, enum sh_login1_method method, const char *answer,
                     const char *error) {
    struct sh_server *server = data;
    if (!answer)
        wlr_log(WLR_ERROR, "logind did not say whether it allows %s: %s",
                sh_login1_method_name(method), error);
    snprintf(server->power.answers[method], sizeof(server->power.answers[method]), "%s",
             answer ? answer : "");
    notify_subscribers(server);
}

static void done(void *data, enum sh_login1_method method, const char *error) {
    struct sh_server *server = data;
    struct sh_power *power = &server->power;
    if (power->step != SH_POWER_CALLING)
        return;
    power->step = SH_POWER_IDLE;
    if (error)
        power_report(server, "%s failed: %s", action_label(power->action), error);
    else
        wlr_log(WLR_INFO, "logind accepted %s", sh_login1_method_name(method));
}

/* Connects to logind unless already connected: on the system bus, or on the bus
 * $PAW_LOGIN1_BUS names. A headless compositor (the tests) uses only the latter, so
 * nothing it is asked to do can reach the machine's own logind. */
static bool power_connect(struct sh_server *server, char *error, size_t error_size) {
    struct sh_power *power = &server->power;
    if (power->login1)
        return true;
    const char *address = getenv("PAW_LOGIN1_BUS");
    if (address && !*address)
        address = NULL;
    if (!address && !power->system_bus) {
        snprintf(error, error_size,
                 "a headless session uses only the logind on the bus PAW_LOGIN1_BUS names");
        return false;
    }
    const struct sh_login1_handler handler = {.data = server,
                                              .answer = answered,
                                              .done = done,
                                              .inhibited = inhibited,
                                              .sleep = prepare_for_sleep,
                                              .lid = lid_answered};
    power->login1 = sh_login1_connect(address, &handler, error, error_size);
    if (!power->login1)
        return false;
    struct wl_event_loop *loop = wl_display_get_event_loop(server->wl_display);
    power->bus = wl_event_loop_add_fd(loop, sh_login1_fd(power->login1), WL_EVENT_READABLE,
                                      bus_ready, server);
    power->bus_timer = wl_event_loop_add_timer(loop, bus_timeout, server);
    if (!power->bus || !power->bus_timer) {
        snprintf(error, error_size, "cannot watch the connection to logind");
        if (power->bus)
            wl_event_source_remove(power->bus);
        if (power->bus_timer)
            wl_event_source_remove(power->bus_timer);
        sh_login1_destroy(power->login1);
        power->login1 = NULL;
        return false;
    }
    power_ask(server);
    hold_sleep(server);
    sh_login1_ask_lid(power->login1);
    power_watch(server);
    return true;
}

/* Carries out the action under way: hands it to logind, or ends the session. */
static bool power_call(struct sh_server *server, char *error, size_t error_size) {
    struct sh_power *power = &server->power;
    enum sh_login1_method method = actions[action_index(power->action)].method;
    if (power->action == SH_LOGOUT) {
        wlr_log(WLR_INFO, "Logging out");
        power->step = SH_POWER_IDLE;
        wl_display_terminate(server->wl_display);
        return true;
    }
    if (!power->login1 || !sh_login1_call(power->login1, method)) {
        snprintf(error, error_size, "cannot reach logind to %s", action_label(power->action));
        power->step = SH_POWER_IDLE;
        return false;
    }
    wlr_log(WLR_INFO, "Asking logind to %s", action_label(power->action));
    power->step = SH_POWER_CALLING;
    power_watch(server);
    return true;
}

static bool clients_left(struct sh_server *server) {
    for (size_t i = 0; i < sizeof(server->power.clients) / sizeof(*server->power.clients); ++i)
        if (server->power.clients[i].client)
            return true;
    return false;
}

static void forget_clients(struct sh_server *server) {
    for (size_t i = 0; i < sizeof(server->power.clients) / sizeof(*server->power.clients); ++i) {
        struct sh_power_client *slot = &server->power.clients[i];
        if (slot->client)
            wl_list_remove(&slot->destroy.link);
        slot->client = NULL;
    }
}

/* Everything that had to happen first has: the action goes ahead. */
static void power_proceed(struct sh_server *server) {
    wl_event_source_timer_update(server->power.timer, 0);
    forget_clients(server);
    char error[300];
    if (!power_call(server, error, sizeof(error)))
        power_report(server, "%s", error);
}

static void client_gone(struct wl_listener *listener, void *data) {
    struct sh_power_client *slot = wl_container_of(listener, slot, destroy);
    struct sh_server *server = slot->server;
    wl_list_remove(&slot->destroy.link);
    slot->client = NULL;
    if (server->power.step == SH_POWER_LEAVING && !clients_left(server))
        power_proceed(server);
}

/* Follows the clients of the windows asked to close for a log out: an application that saves
 * as it quits keeps its connection until it is done. X11 windows all belong to Xwayland, which
 * stays. */
static void follow_clients(struct sh_server *server) {
    size_t count = sizeof(server->power.clients) / sizeof(*server->power.clients);
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
#if WLR_HAS_XWAYLAND
        if (toplevel->xsurface)
            continue;
#endif
        struct wl_client *client = wl_resource_get_client(toplevel->xdg_toplevel->resource);
        struct sh_power_client *free_slot = NULL;
        bool known = false;
        for (size_t i = 0; i < count && !known; ++i) {
            known = server->power.clients[i].client == client;
            if (!server->power.clients[i].client && !free_slot)
                free_slot = &server->power.clients[i];
        }
        if (known || !free_slot)
            continue;
        free_slot->server = server;
        free_slot->client = client;
        free_slot->destroy.notify = client_gone;
        wl_client_add_destroy_listener(client, &free_slot->destroy);
    }
}

/* A window has gone: a power off waiting for the windows to close goes ahead after the last
 * (a log out once their applications have disconnected too). */
void power_window_closed(struct sh_server *server) {
    struct sh_power *power = &server->power;
    if (power->step != SH_POWER_CLOSING || !wl_list_empty(&server->toplevels))
        return;
    wlr_log(WLR_INFO, "Every window has closed");
    if (power->action == SH_LOGOUT && clients_left(server)) {
        power->step = SH_POWER_LEAVING; // within what is left of close_timeout
        return;
    }
    power_proceed(server);
}

/* The step under way ran out of time. */
static int power_timeout(void *data) {
    struct sh_server *server = data;
    struct sh_power *power = &server->power;
    if (power->step == SH_POWER_LOCKING) {
        power->step = SH_POWER_IDLE;
        power_report(server, "%s cancelled: the screen did not lock within %d seconds",
                     action_label(power->action), LOCK_TIMEOUT_MS / 1000);
    } else if (power->step == SH_POWER_CLOSING) {
        // The windows left, named by their applications.
        int count = 0;
        char names[160] = "";
        size_t used = 0;
        struct sh_toplevel *toplevel;
        wl_list_for_each(toplevel, &server->toplevels, link) {
            const char *app_id = toplevel_app_id(toplevel), *title = toplevel_title(toplevel);
            const char *name = app_id && *app_id ? app_id : title && *title ? title : "untitled";
            if (used < sizeof(names) && !strstr(names, name))
                used += (size_t)snprintf(names + used, sizeof(names) - used, "%s%s",
                                         used ? ", " : "", name);
            ++count;
        }
        const char *noun = count == 1 ? "window is" : "windows are";
        if (server_settings(server)->close_force) {
            wlr_log(WLR_INFO, "%d %s still open (%s); going ahead", count, noun, names);
            power_proceed(server);
        } else {
            power->step = SH_POWER_IDLE;
            forget_clients(server);
            power_report(server, "%s cancelled: %d %s still open (%s)",
                         action_label(power->action), count, noun, names);
        }
    } else if (power->step == SH_POWER_LEAVING) {
        // The windows have closed; an application still running goes with the session.
        power_proceed(server);
    }
    return 0;
}

/* The lock holds on every output: a suspend waiting for it goes ahead, and so does a sleep. */
void power_locked(struct sh_server *server) {
    struct sh_power *power = &server->power;
    power->locker_started = 0;
    if (power->before_sleep)
        release_sleep(server);
    if (power->step == SH_POWER_LOCKING)
        power_proceed(server);
}

bool power_start(struct sh_server *server, enum sh_action action, char *error,
                 size_t error_size) {
    struct sh_power *power = &server->power;
    int index = action_index(action);
    if (index < 0) {
        snprintf(error, error_size, "not a power action");
        return false;
    }
    if (action == SH_LOCK)
        return power_lock(server, error, error_size);
    if (power->step != SH_POWER_IDLE) {
        snprintf(error, error_size, "%s is already under way", action_label(power->action));
        return false;
    }
    enum sh_login1_method method = actions[index].method;
    char reason[256];
    if (method != SH_LOGIN1_METHODS && !power_connect(server, reason, sizeof(reason))) {
        snprintf(error, error_size, "logind is out of reach: %s", reason);
        return false;
    }
    if (method != SH_LOGIN1_METHODS) {
        // A refusal stands on the last answer; asking again keeps the next one current.
        const char *answer = power->answers[method];
        bool refused = !strcmp(answer, "no") || !strcmp(answer, "na");
        if (refused)
            snprintf(error, error_size, "logind does not allow %s here (Can%s: %s)",
                     actions[index].label, sh_login1_method_name(method), answer);
        power_ask(server);
        if (refused)
            return false;
    }
    power->action = action;
    if ((action == SH_SUSPEND || action == SH_HIBERNATE) && lock_wanted(server) &&
        !lock_holds(server)) {
        if (!lock_for_sleep(server, error, error_size))
            return false;
        power->step = SH_POWER_LOCKING;
        wl_event_source_timer_update(power->timer, LOCK_TIMEOUT_MS);
        return true;
    }
    // The session as it is, before its windows go, for the next login to start from.
    if (action == SH_POWER_OFF || action == SH_REBOOT || action == SH_LOGOUT)
        session_save_last(server);
    const struct sh_settings *settings = server_settings(server);
    if ((action == SH_POWER_OFF || action == SH_REBOOT || action == SH_LOGOUT) &&
        settings->close_windows && !wl_list_empty(&server->toplevels)) {
        // Every window is asked to close, as by its close button; the last to go lets it on.
        wlr_log(WLR_INFO, "Closing every window to %s", action_label(action));
        struct sh_toplevel *toplevel;
        wl_list_for_each(toplevel, &server->toplevels, link) toplevel_close(toplevel);
        if (action == SH_LOGOUT)
            follow_clients(server);
        power->step = SH_POWER_CLOSING;
        wl_event_source_timer_update(power->timer, settings->close_timeout);
        return true;
    }
    return power_call(server, error, error_size);
}

void power_run(struct sh_server *server, enum sh_action action) {
    char error[300];
    if (!power_start(server, action, error, sizeof(error)))
        power_report(server, "%s", error);
}

/* The `index`th power action, in menu order, and whether it may run: "yes" or "no" for those
 * of paw's own; for logind's, its answer, "unknown" until it gives one, or "unavailable"
 * without logind. False past the last. */
bool power_describe(struct sh_server *server, size_t index, const char **name,
                    const char **status) {
    if (index >= sizeof(actions) / sizeof(*actions))
        return false;
    struct sh_power *power = &server->power;
    enum sh_login1_method method = actions[index].method;
    char ignored[256];
    *name = actions[index].name;
    *status = actions[index].action == SH_LOCK ? (locker(server, false, ignored, sizeof(ignored))
                                                      ? "yes"
                                                      : "no")
              : method == SH_LOGIN1_METHODS ? "yes"
              : !power->login1             ? "unavailable"
              : power->answers[method][0]   ? power->answers[method]
                                            : "unknown";
    return true;
}

/* The actions that may run (logind allows them, possibly after a password), comma-separated
 * in menu order, or "-" for none. */
void power_available(struct sh_server *server, char *list, size_t size) {
    size_t used = 0;
    const char *name, *status;
    list[0] = '\0';
    for (size_t i = 0; power_describe(server, i, &name, &status); ++i)
        if ((!strcmp(status, "yes") || !strcmp(status, "challenge")) && used < size)
            used += (size_t)snprintf(list + used, size - used, "%s%s", used ? "," : "", name);
    if (!used)
        snprintf(list, size, "-");
}

/* What `get power` says of the action under way: its name and step, or "-". */
const char *power_pending(struct sh_server *server, char *text, size_t size) {
    static const char *const steps[] = {"idle", "closing", "leaving", "locking", "calling"};
    struct sh_power *power = &server->power;
    if (power->step == SH_POWER_IDLE)
        return "-";
    snprintf(text, size, "%s %s", power_action_name(power->action), steps[power->step]);
    return text;
}

void power_reload(struct sh_server *server) {
    power_ask(server);
    hold_sleep(server);
    notify_subscribers(server); // a locker may have come or gone
}

void power_init(struct sh_server *server) {
    server->power.timer =
        wl_event_loop_add_timer(wl_display_get_event_loop(server->wl_display), power_timeout,
                                server);
    char error[256];
    if (!power_connect(server, error, sizeof(error)))
        wlr_log(WLR_INFO, "Suspend, hibernate, reboot and power off are unavailable: %s", error);
}

void power_finish(struct sh_server *server) {
    server->power.step = SH_POWER_IDLE;
    forget_clients(server);
    if (server->power.timer)
        wl_event_source_remove(server->power.timer);
    server->power.timer = NULL;
    power_disconnect(server);
}
