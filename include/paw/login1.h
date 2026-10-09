// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* logind (org.freedesktop.login1, from systemd-logind or elogind) for the power actions: which
 * of them it allows, the actions themselves, and a delay inhibitor that holds off sleep until
 * the screen is locked; and whether the lid is closed. Nothing blocks: each call returns at
 * once, and its answer reaches the handler from sh_login1_dispatch, which the caller runs when
 * the descriptor is ready. */
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum sh_login1_method {
    SH_LOGIN1_POWER_OFF,
    SH_LOGIN1_REBOOT,
    SH_LOGIN1_SUSPEND,
    SH_LOGIN1_HIBERNATE,
    SH_LOGIN1_METHODS,
};

struct sh_login1_handler {
    void *data;
    /* Can<Method> answered "yes", "no", "challenge" (allowed after authenticating) or "na"; or
     * failed, with `answer` NULL and the reason in `error`. */
    void (*answer)(void *data, enum sh_login1_method method, const char *answer,
                   const char *error);
    /* <Method> returned: `error` is NULL when logind accepted it. */
    void (*done)(void *data, enum sh_login1_method method, const char *error);
    /* An inhibitor was granted: `fd` holds it until closed, and the handler owns it. -1 with
     * the reason in `error` when it was refused. */
    void (*inhibited)(void *data, int fd, const char *error);
    /* PrepareForSleep: `before` just before the machine sleeps, false once it has woken. */
    void (*sleep)(void *data, bool before);
    /* LidClosed, as logind reads it from the lid switch: when asked, and as it changes. */
    void (*lid)(void *data, bool closed);
};

struct sh_login1;

/* Connects to the system bus, or to the bus at `address` when it is not NULL. NULL, with the
 * reason in `error`, when that fails or paw was built without sd-bus. */
struct sh_login1 *sh_login1_connect(const char *address, const struct sh_login1_handler *handler,
                                    char *error, size_t error_size);
/* Closes the connection; no handler runs afterwards. */
void sh_login1_destroy(struct sh_login1 *login1);
/* The descriptor to watch: always for reading, and for writing too while sh_login1_wants_write
 * says so; sh_login1_dispatch must also run once sh_login1_timeout milliseconds have passed
 * (-1: no deadline). Each can change after any call, so ask again after one. */
int sh_login1_fd(struct sh_login1 *login1);
bool sh_login1_wants_write(struct sh_login1 *login1);
int sh_login1_timeout(struct sh_login1 *login1);
/* Handles what has arrived, running the handler for it. False once the connection is lost. */
bool sh_login1_dispatch(struct sh_login1 *login1);
/* Asks Can<Method>; the answer goes to the handler. False when it could not be sent. */
bool sh_login1_ask(struct sh_login1 *login1, enum sh_login1_method method);
/* Calls <Method>, letting logind ask for a password (interactive); the handler hears when it
 * returns. False when it could not be sent. */
bool sh_login1_call(struct sh_login1 *login1, enum sh_login1_method method);
/* Asks for a "sleep" delay inhibitor, given `why`; the handler gets its descriptor. */
bool sh_login1_inhibit_sleep(struct sh_login1 *login1, const char *why);
/* Asks whether the lid is closed (logind's LidClosed); the answer goes to the handler, and so
 * does every change from then on. */
bool sh_login1_ask_lid(struct sh_login1 *login1);
/* "PowerOff", "Reboot", "Suspend" or "Hibernate". */
const char *sh_login1_method_name(enum sh_login1_method method);

#ifdef __cplusplus
}
#endif
