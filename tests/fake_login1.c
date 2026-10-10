// SPDX-License-Identifier: GPL-3.0-or-later
/* A stand-in for logind's org.freedesktop.login1 Manager on a private bus, so the power actions
 * can be tested without a machine powering off: it answers CanPowerOff, CanReboot, CanSuspend
 * and CanHibernate, records PowerOff, Reboot, Suspend, Hibernate and Inhibit calls, hands out
 * inhibitors, and goes through PrepareForSleep as logind does around a suspend.
 *
 *     fake_login1 ADDRESS LOG ANSWERS
 *
 * ADDRESS is the bus to serve on. Each event is a line appended to LOG: "ready", "PowerOff
 * true" (a call and its interactive flag), "Inhibit sleep delay", "prepare" (PrepareForSleep
 * true sent), "release sleep delay" (an inhibitor closed), "sleep" (when the machine would
 * sleep: every delay inhibitor released, or 5 seconds passed), and "wake" (PrepareForSleep
 * false sent). ANSWERS is read at every call: "CanSuspend na" sets an answer (unlisted ones
 * are "yes"), and "fail Hibernate" makes that call fail as if logind had refused it. SIGUSR1
 * starts a suspend as if another program had asked for one. The LidClosed property is true while
 * ANSWERS has the line "LidClosed yes"; SIGUSR2 says it changed ("lid" in LOG). */
#define _GNU_SOURCE // pipe2
#if PAW_SDBUS_SYSTEMD
#include <systemd/sd-bus.h>
#elif PAW_SDBUS_ELOGIND
#include <elogind/sd-bus.h>
#elif PAW_SDBUS_BASU
#include <basu/sd-bus.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
#include <time.h>
#include <unistd.h>

#define MAX_INHIBITORS 16
#define DELAY_MAX_MS 5000

static const char *log_path, *answers_path;
static sd_bus *bus;
/* The read ends of the inhibitors handed out; a hang-up means the holder let go. */
static struct {
    int fd;
    char what[32], mode[16];
} inhibitors[MAX_INHIBITORS];
static int inhibitor_count;
static long long sleep_started = -1; /* ms since PrepareForSleep(true), -1 when awake */

static long long now_ms(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000LL + now.tv_nsec / 1000000;
}

static void record(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void record(const char *format, ...) {
    char line[256];
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(line, sizeof(line) - 1, format, arguments);
    va_end(arguments);
    if (length < 0)
        return;
    if (length > (int)sizeof(line) - 2)
        length = (int)sizeof(line) - 2;
    line[length++] = '\n';
    // One write per line with O_APPEND, so lines from other writers (the lock probe) interleave
    // whole.
    int fd = open(log_path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0 || write(fd, line, (size_t)length) != length) {
        perror("fake login1: cannot write the log");
        exit(1);
    }
    close(fd);
}

/* The value of the line "KEY VALUE" in the answers file, or NULL. */
static const char *answer_for(const char *key, char *value, size_t size) {
    FILE *file = fopen(answers_path, "r");
    if (!file)
        return NULL;
    char line[128];
    const char *found = NULL;
    size_t length = strlen(key);
    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = '\0';
        if (!strncmp(line, key, length) && line[length] == ' ') {
            snprintf(value, size, "%s", line + length + 1);
            found = value;
        }
    }
    fclose(file);
    return found;
}

/* Whether the answers file has the line "fail MEMBER". */
static bool fails(const char *member) {
    FILE *file = fopen(answers_path, "r");
    if (!file)
        return false;
    char line[128], value[64];
    bool result = false;
    while (fgets(line, sizeof(line), file))
        if (sscanf(line, "fail %63s", value) == 1 && !strcmp(value, member))
            result = true;
    fclose(file);
    return result;
}

static int can(sd_bus_message *message, void *data, sd_bus_error *error) {
    char value[32];
    const char *answer = answer_for(sd_bus_message_get_member(message), value, sizeof(value));
    return sd_bus_reply_method_return(message, "s", answer ? answer : "yes");
}

static void prepare_for_sleep(void) {
    if (sleep_started >= 0)
        return;
    // Written first, so that it comes before whatever the compositor does about it (the
    // locker it starts writes to the same log).
    record("prepare");
    sd_bus_emit_signal(bus, "/org/freedesktop/login1", "org.freedesktop.login1.Manager",
                       "PrepareForSleep", "b", 1);
    sleep_started = now_ms();
}

static bool delayed(void) {
    for (int i = 0; i < inhibitor_count; ++i)
        if (!strcmp(inhibitors[i].mode, "delay") && strstr(inhibitors[i].what, "sleep"))
            return true;
    return false;
}

/* Once nothing delays it any more (or the delay has run out), the machine sleeps and wakes. */
static void maybe_sleep(void) {
    if (sleep_started < 0 || (delayed() && now_ms() - sleep_started < DELAY_MAX_MS))
        return;
    record(delayed() ? "sleep (delay ran out)" : "sleep");
    sleep_started = -1;
    sd_bus_emit_signal(bus, "/org/freedesktop/login1", "org.freedesktop.login1.Manager",
                       "PrepareForSleep", "b", 0);
    record("wake");
}

static int act(sd_bus_message *message, void *data, sd_bus_error *error) {
    const char *member = sd_bus_message_get_member(message);
    int interactive = 0;
    int r = sd_bus_message_read(message, "b", &interactive);
    if (r < 0)
        return r;
    record("%s %s", member, interactive ? "true" : "false");
    if (fails(member))
        return sd_bus_error_set(error, "org.freedesktop.DBus.Error.AccessDenied",
                                "Access denied by the fake logind");
    r = sd_bus_reply_method_return(message, "");
    if (r >= 0 && (!strcmp(member, "Suspend") || !strcmp(member, "Hibernate")))
        prepare_for_sleep();
    return r;
}

static int inhibit(sd_bus_message *message, void *data, sd_bus_error *error) {
    const char *what, *who, *why, *mode;
    int r = sd_bus_message_read(message, "ssss", &what, &who, &why, &mode);
    if (r < 0)
        return r;
    record("Inhibit %s %s", what, mode);
    if (inhibitor_count == MAX_INHIBITORS)
        return sd_bus_error_set(error, "org.freedesktop.DBus.Error.LimitsExceeded",
                                "Too many inhibitors");
    int ends[2];
    if (pipe2(ends, O_CLOEXEC) < 0)
        return -errno;
    inhibitors[inhibitor_count].fd = ends[0];
    snprintf(inhibitors[inhibitor_count].what, sizeof(inhibitors[0].what), "%s", what);
    snprintf(inhibitors[inhibitor_count].mode, sizeof(inhibitors[0].mode), "%s", mode);
    ++inhibitor_count;
    r = sd_bus_reply_method_return(message, "h", ends[1]);
    close(ends[1]); // the reply holds its own copy until it is sent
    return r;
}

static int lid_closed(sd_bus *unused, const char *path, const char *interface,
                      const char *property, sd_bus_message *reply, void *data,
                      sd_bus_error *error) {
    char value[16];
    const char *answer = answer_for("LidClosed", value, sizeof(value));
    return sd_bus_message_append(reply, "b", answer && !strcmp(answer, "yes"));
}

static const sd_bus_vtable manager[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_PROPERTY("LidClosed", "b", lid_closed, 0, SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
    SD_BUS_METHOD("CanPowerOff", "", "s", can, 0),
    SD_BUS_METHOD("CanReboot", "", "s", can, 0),
    SD_BUS_METHOD("CanSuspend", "", "s", can, 0),
    SD_BUS_METHOD("CanHibernate", "", "s", can, 0),
    SD_BUS_METHOD("PowerOff", "b", "", act, 0),
    SD_BUS_METHOD("Reboot", "b", "", act, 0),
    SD_BUS_METHOD("Suspend", "b", "", act, 0),
    SD_BUS_METHOD("Hibernate", "b", "", act, 0),
    SD_BUS_METHOD("Inhibit", "ssss", "h", inhibit, 0),
    SD_BUS_SIGNAL("PrepareForSleep", "b", 0),
    SD_BUS_VTABLE_END,
};

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: fake_login1 ADDRESS LOG ANSWERS\n");
        return 2;
    }
    log_path = argv[2];
    answers_path = argv[3];
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGUSR1);
    sigaddset(&signals, SIGUSR2);
    sigaddset(&signals, SIGTERM);
    sigaddset(&signals, SIGINT);
    sigprocmask(SIG_BLOCK, &signals, NULL);
    int signal_fd = signalfd(-1, &signals, SFD_CLOEXEC);
    int r = sd_bus_new(&bus);
    if (r >= 0)
        r = sd_bus_set_address(bus, argv[1]);
    if (r >= 0)
        r = sd_bus_set_bus_client(bus, 1);
    if (r >= 0)
        r = sd_bus_start(bus);
    if (r >= 0)
        r = sd_bus_add_object_vtable(bus, NULL, "/org/freedesktop/login1",
                                     "org.freedesktop.login1.Manager", manager, NULL);
    if (r >= 0)
        r = sd_bus_request_name(bus, "org.freedesktop.login1", 0);
    if (r < 0 || signal_fd < 0) {
        fprintf(stderr, "fake login1: cannot serve on %s: %s\n", argv[1], strerror(-r));
        return 1;
    }
    record("ready");
    for (;;) {
        while ((r = sd_bus_process(bus, NULL)) > 0)
            ;
        if (r < 0) {
            fprintf(stderr, "fake login1: bus lost: %s\n", strerror(-r));
            return 1;
        }
        maybe_sleep();
        struct pollfd fds[2 + MAX_INHIBITORS] = {
            {sd_bus_get_fd(bus), (short)sd_bus_get_events(bus), 0},
            {signal_fd, POLLIN, 0},
        };
        for (int i = 0; i < inhibitor_count; ++i)
            fds[2 + i] = (struct pollfd){inhibitors[i].fd, POLLIN, 0};
        int timeout = sleep_started >= 0 ? 50 : -1;
        if (poll(fds, (nfds_t)(2 + inhibitor_count), timeout) < 0 && errno != EINTR)
            return 1;
        if (fds[1].revents & POLLIN) {
            struct signalfd_siginfo info;
            if (read(signal_fd, &info, sizeof(info)) == sizeof(info)) {
                if (info.ssi_signo == SIGUSR2) {
                    sd_bus_emit_properties_changed(bus, "/org/freedesktop/login1",
                                                   "org.freedesktop.login1.Manager", "LidClosed",
                                                   NULL);
                    record("lid");
                    continue;
                }
                if (info.ssi_signo != SIGUSR1)
                    break;
                record("external suspend");
                prepare_for_sleep();
            }
        }
        for (int i = inhibitor_count - 1; i >= 0; --i) {
            if (!(fds[2 + i].revents & (POLLHUP | POLLIN | POLLERR)))
                continue;
            record("release %s %s", inhibitors[i].what, inhibitors[i].mode);
            close(inhibitors[i].fd);
            inhibitors[i] = inhibitors[--inhibitor_count];
        }
    }
    sd_bus_flush_close_unref(bus);
    return 0;
}
