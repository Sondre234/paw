// SPDX-License-Identifier: GPL-3.0-or-later
#include "paw/login1.h"

#include <stdio.h>

static const char *const method_names[SH_LOGIN1_METHODS] = {"PowerOff", "Reboot", "Suspend",
                                                            "Hibernate"};

const char *sh_login1_method_name(enum sh_login1_method method) {
    return (unsigned)method < SH_LOGIN1_METHODS ? method_names[method] : "?";
}

#if PAW_SDBUS_SYSTEMD
#include <systemd/sd-bus.h>
#elif PAW_SDBUS_ELOGIND
#include <elogind/sd-bus.h>
#elif PAW_SDBUS_BASU
#include <basu/sd-bus.h>
#endif

#if PAW_SDBUS_SYSTEMD || PAW_SDBUS_ELOGIND || PAW_SDBUS_BASU
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define LOGIN1 "org.freedesktop.login1"
#define LOGIN1_PATH "/org/freedesktop/login1"
#define LOGIN1_MANAGER "org.freedesktop.login1.Manager"
/* A power action may wait for a password; logind answers once it has been given. */
#define CALL_TIMEOUT_USEC (10ULL * 60 * 1000 * 1000)

/* One call in flight, and what its reply is for. */
struct pending {
    struct sh_login1 *login1;
    enum sh_login1_method method;
    sd_bus_slot *slot;
};

struct sh_login1 {
    sd_bus *bus;
    struct sh_login1_handler handler;
    struct pending asks[SH_LOGIN1_METHODS], calls[SH_LOGIN1_METHODS], inhibit, lid;
    sd_bus_slot *sleep_match, *lid_match;
};

/* The reason a reply carries, or NULL when it is not an error. */
static const char *reply_error(sd_bus_message *reply) {
    const sd_bus_error *error = sd_bus_message_get_error(reply);
    if (!error)
        return NULL;
    return error->message ? error->message : error->name ? error->name : "unknown error";
}

static int asked(sd_bus_message *reply, void *data, sd_bus_error *unused) {
    struct pending *pending = data;
    pending->slot = sd_bus_slot_unref(pending->slot);
    struct sh_login1 *login1 = pending->login1;
    const char *error = reply_error(reply), *answer = NULL;
    int r = error ? 0 : sd_bus_message_read(reply, "s", &answer);
    if (r < 0)
        error = strerror(-r);
    if (login1->handler.answer)
        login1->handler.answer(login1->handler.data, pending->method, error ? NULL : answer,
                               error);
    return 0;
}

static int called(sd_bus_message *reply, void *data, sd_bus_error *unused) {
    struct pending *pending = data;
    pending->slot = sd_bus_slot_unref(pending->slot);
    struct sh_login1 *login1 = pending->login1;
    if (login1->handler.done)
        login1->handler.done(login1->handler.data, pending->method, reply_error(reply));
    return 0;
}

static int inhibited(sd_bus_message *reply, void *data, sd_bus_error *unused) {
    struct pending *pending = data;
    pending->slot = sd_bus_slot_unref(pending->slot);
    struct sh_login1 *login1 = pending->login1;
    const char *error = reply_error(reply);
    int held = -1, fd = -1;
    int r = error ? 0 : sd_bus_message_read(reply, "h", &held);
    if (r < 0)
        error = strerror(-r);
    // The reply owns `held`; keep a copy of our own, clear of the standard descriptors.
    else if (!error && (fd = fcntl(held, F_DUPFD_CLOEXEC, 3)) < 0)
        error = strerror(errno);
    if (login1->handler.inhibited)
        login1->handler.inhibited(login1->handler.data, fd, error);
    else if (fd >= 0)
        close(fd);
    return 0;
}

static int prepare_for_sleep(sd_bus_message *message, void *data, sd_bus_error *unused) {
    struct sh_login1 *login1 = data;
    int before = 0;
    if (sd_bus_message_read(message, "b", &before) >= 0 && login1->handler.sleep)
        login1->handler.sleep(login1->handler.data, before);
    return 0;
}

/* Hands LidClosed's value, a variant holding a boolean, to the handler. */
static void report_lid(struct sh_login1 *login1, sd_bus_message *message) {
    int closed = 0;
    if (sd_bus_message_read(message, "v", "b", &closed) >= 0 && login1->handler.lid)
        login1->handler.lid(login1->handler.data, closed);
}

static int got_lid(sd_bus_message *reply, void *data, sd_bus_error *unused) {
    struct pending *pending = data;
    pending->slot = sd_bus_slot_unref(pending->slot);
    if (!reply_error(reply))
        report_lid(pending->login1, reply);
    return 0;
}

/* PropertiesChanged of the manager: LidClosed's new value, or only its name when it was left out,
 * which asks for it again. */
static int properties_changed(sd_bus_message *message, void *data, sd_bus_error *unused) {
    struct sh_login1 *login1 = data;
    const char *interface = NULL, *name = NULL;
    if (sd_bus_message_read(message, "s", &interface) < 0 || strcmp(interface, LOGIN1_MANAGER) ||
        sd_bus_message_enter_container(message, 'a', "{sv}") < 0)
        return 0;
    while (sd_bus_message_enter_container(message, 'e', "sv") > 0) {
        if (sd_bus_message_read(message, "s", &name) < 0)
            return 0;
        if (!strcmp(name, "LidClosed"))
            report_lid(login1, message);
        else if (sd_bus_message_skip(message, "v") < 0)
            return 0;
        if (sd_bus_message_exit_container(message) < 0)
            return 0;
    }
    if (sd_bus_message_exit_container(message) < 0 ||
        sd_bus_message_enter_container(message, 'a', "s") < 0)
        return 0;
    while (sd_bus_message_read(message, "s", &name) > 0)
        if (!strcmp(name, "LidClosed"))
            sh_login1_ask_lid(login1);
    return 0;
}

struct sh_login1 *sh_login1_connect(const char *address, const struct sh_login1_handler *handler,
                                    char *error, size_t error_size) {
    struct sh_login1 *login1 = calloc(1, sizeof(*login1));
    if (!login1) {
        snprintf(error, error_size, "out of memory");
        return NULL;
    }
    login1->handler = *handler;
    int r;
    if (address) {
        r = sd_bus_new(&login1->bus);
        if (r >= 0)
            r = sd_bus_set_address(login1->bus, address);
        if (r >= 0)
            r = sd_bus_set_bus_client(login1->bus, 1);
        if (r >= 0)
            r = sd_bus_start(login1->bus);
    } else {
        r = sd_bus_open_system(&login1->bus);
    }
    // polkit may ask for a password before allowing an action (the "challenge" answer).
    if (r >= 0)
        r = sd_bus_set_allow_interactive_authorization(login1->bus, 1);
    if (r >= 0)
        r = sd_bus_match_signal(login1->bus, &login1->sleep_match, LOGIN1, LOGIN1_PATH,
                                LOGIN1_MANAGER, "PrepareForSleep", prepare_for_sleep, login1);
    if (r >= 0)
        r = sd_bus_match_signal(login1->bus, &login1->lid_match, LOGIN1, LOGIN1_PATH,
                                "org.freedesktop.DBus.Properties", "PropertiesChanged",
                                properties_changed, login1);
    if (r < 0) {
        snprintf(error, error_size, "cannot connect to %s: %s",
                 address ? address : "the system bus", strerror(-r));
        sd_bus_slot_unref(login1->sleep_match);
        sd_bus_slot_unref(login1->lid_match);
        sd_bus_flush_close_unref(login1->bus);
        free(login1);
        return NULL;
    }
    for (int i = 0; i < SH_LOGIN1_METHODS; ++i) {
        login1->asks[i] = (struct pending){login1, (enum sh_login1_method)i, NULL};
        login1->calls[i] = (struct pending){login1, (enum sh_login1_method)i, NULL};
    }
    login1->inhibit.login1 = login1;
    login1->lid.login1 = login1;
    return login1;
}

void sh_login1_destroy(struct sh_login1 *login1) {
    if (!login1)
        return;
    // Dropping the slots first means no reply runs a handler for a caller that has gone.
    for (int i = 0; i < SH_LOGIN1_METHODS; ++i) {
        sd_bus_slot_unref(login1->asks[i].slot);
        sd_bus_slot_unref(login1->calls[i].slot);
    }
    sd_bus_slot_unref(login1->inhibit.slot);
    sd_bus_slot_unref(login1->lid.slot);
    sd_bus_slot_unref(login1->sleep_match);
    sd_bus_slot_unref(login1->lid_match);
    sd_bus_flush_close_unref(login1->bus);
    free(login1);
}

int sh_login1_fd(struct sh_login1 *login1) {
    return sd_bus_get_fd(login1->bus);
}

bool sh_login1_wants_write(struct sh_login1 *login1) {
    int events = sd_bus_get_events(login1->bus);
    return events > 0 && (events & POLLOUT);
}

int sh_login1_timeout(struct sh_login1 *login1) {
    uint64_t deadline;
    if (sd_bus_get_timeout(login1->bus, &deadline) < 0 || deadline == UINT64_MAX)
        return -1;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t current = (uint64_t)now.tv_sec * 1000000 + (uint64_t)now.tv_nsec / 1000;
    if (deadline <= current)
        return 0;
    uint64_t ms = (deadline - current + 999) / 1000;
    return ms > INT32_MAX ? INT32_MAX : (int)ms;
}

bool sh_login1_dispatch(struct sh_login1 *login1) {
    int r;
    while ((r = sd_bus_process(login1->bus, NULL)) > 0)
        ;
    return r >= 0;
}

bool sh_login1_ask(struct sh_login1 *login1, enum sh_login1_method method) {
    struct pending *pending = &login1->asks[method];
    if (pending->slot) // the answer on its way will do
        return true;
    char member[32];
    snprintf(member, sizeof(member), "Can%s", method_names[method]);
    return sd_bus_call_method_async(login1->bus, &pending->slot, LOGIN1, LOGIN1_PATH,
                                    LOGIN1_MANAGER, member, asked, pending, "") >= 0;
}

bool sh_login1_call(struct sh_login1 *login1, enum sh_login1_method method) {
    struct pending *pending = &login1->calls[method];
    sd_bus_message *message = NULL;
    pending->slot = sd_bus_slot_unref(pending->slot);
    int r = sd_bus_message_new_method_call(login1->bus, &message, LOGIN1, LOGIN1_PATH,
                                           LOGIN1_MANAGER, method_names[method]);
    if (r >= 0)
        r = sd_bus_message_append(message, "b", 1);
    if (r >= 0)
        r = sd_bus_call_async(login1->bus, &pending->slot, message, called, pending,
                              CALL_TIMEOUT_USEC);
    sd_bus_message_unref(message);
    return r >= 0;
}

bool sh_login1_inhibit_sleep(struct sh_login1 *login1, const char *why) {
    struct pending *pending = &login1->inhibit;
    pending->slot = sd_bus_slot_unref(pending->slot);
    return sd_bus_call_method_async(login1->bus, &pending->slot, LOGIN1, LOGIN1_PATH,
                                    LOGIN1_MANAGER, "Inhibit", inhibited, pending, "ssss",
                                    "sleep", "paw", why, "delay") >= 0;
}

bool sh_login1_ask_lid(struct sh_login1 *login1) {
    struct pending *pending = &login1->lid;
    if (pending->slot) // the answer on its way will do
        return true;
    return sd_bus_call_method_async(login1->bus, &pending->slot, LOGIN1, LOGIN1_PATH,
                                    "org.freedesktop.DBus.Properties", "Get", got_lid, pending,
                                    "ss", LOGIN1_MANAGER, "LidClosed") >= 0;
}
#else
struct sh_login1 *sh_login1_connect(const char *address, const struct sh_login1_handler *handler,
                                    char *error, size_t error_size) {
    (void)address;
    (void)handler;
    snprintf(error, error_size, "paw was built without sd-bus");
    return NULL;
}
void sh_login1_destroy(struct sh_login1 *login1) {
    (void)login1;
}
int sh_login1_fd(struct sh_login1 *login1) {
    (void)login1;
    return -1;
}
bool sh_login1_wants_write(struct sh_login1 *login1) {
    (void)login1;
    return false;
}
int sh_login1_timeout(struct sh_login1 *login1) {
    (void)login1;
    return -1;
}
bool sh_login1_dispatch(struct sh_login1 *login1) {
    (void)login1;
    return false;
}
bool sh_login1_ask(struct sh_login1 *login1, enum sh_login1_method method) {
    (void)login1;
    (void)method;
    return false;
}
bool sh_login1_call(struct sh_login1 *login1, enum sh_login1_method method) {
    (void)login1;
    (void)method;
    return false;
}
bool sh_login1_inhibit_sleep(struct sh_login1 *login1, const char *why) {
    (void)login1;
    (void)why;
    return false;
}
bool sh_login1_ask_lid(struct sh_login1 *login1) {
    (void)login1;
    return false;
}
#endif
