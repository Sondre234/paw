/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A rice on trial (`paw rice try`): `rice try NAME` switches to it, and unless `rice keep` keeps
 * it within 15 seconds it goes back by itself, as display settings do; the shell asks whether to
 * keep it. The files (the rice, the profile in use before it, the record of the trial that
 * brings the previous look back after a crash) are sh_callbacks.rice's, in src/rice.cpp; the
 * switch is a reload, which crossfades to the profile. */
#include "server.h"

/* How long a trial lasts unless it is kept. Under --headless, PAW_TEST_RICE_TRIAL_MS sets it for
 * tests. */
#define RICE_TRIAL_MS 15000

static int rice_trial_ms(struct sh_server *server) {
    const char *test = getenv("PAW_TEST_RICE_TRIAL_MS");
    int ms = test && headless_backend(server) ? atoi(test) : 0;
    return ms > 0 ? ms : RICE_TRIAL_MS;
}

static void forget_trial(struct sh_server *server) {
    server->rice.trial = false;
    if (server->rice.timer)
        wl_event_source_timer_update(server->rice.timer, 0);
}

static bool rice_files(struct sh_server *server, const char *request, char *error,
                       size_t error_size) {
    return server->callbacks->rice(server->callbacks->userdata, request, error, error_size);
}

/* Ends the trial, the previous look back, and tells the shell why: "rice-reverted REASON". */
static void revert_trial(struct sh_server *server, const char *reason) {
    if (!server->rice.trial)
        return;
    forget_trial(server);
    char error[256] = "";
    if (!rice_files(server, "revert", error, sizeof(error)))
        wlr_log(WLR_ERROR, "Cannot take the rice %s back: %s", server->rice.name, error);
    reload_config(server);
    wlr_log(WLR_INFO, "The rice %s went back: %s", server->rice.name, reason);
    char line[96];
    snprintf(line, sizeof(line), "rice-reverted %s\n", reason);
    send_shell_line(server, line);
}

static int trial_over(void *data) {
    revert_trial(data, "timeout");
    return 0;
}

/* Puts the rice `name`, recorded and installed by `paw rice try`, on trial: the configuration is
 * read again with it as the profile in use, and back at once where it is not. */
static bool start_trial(struct sh_server *server, const char *name, char *error,
                        size_t error_size) {
    if (server->rice.trial) {
        snprintf(error, error_size, "%s is on trial; keep it or revert it first",
                 server->rice.name);
        return false;
    }
    if (!server->rice.timer)
        server->rice.timer = wl_event_loop_add_timer(
            wl_display_get_event_loop(server->wl_display), trial_over, server);
    if (!server->rice.timer) {
        snprintf(error, error_size, "no timer to take the rice back; nothing changed");
        return false;
    }
    char request[96];
    snprintf(request, sizeof(request), "try %s", name);
    if (!rice_files(server, request, error, error_size))
        return false;
    snprintf(server->rice.name, sizeof(server->rice.name), "%s", name);
    server->rice.trial = true;
    reload_config(server);
    if (strcmp(server_settings(server)->profile, name) != 0) {
        snprintf(error, error_size, "the configuration did not take %s; the previous look is back",
                 name);
        revert_trial(server, "refused");
        return false;
    }
    int ms = rice_trial_ms(server);
    wl_event_source_timer_update(server->rice.timer, ms);
    server->rice.ends = now_ms() + ms;
    wlr_log(WLR_INFO, "Trying the rice %s for %d ms", name, ms);
    char line[128];
    snprintf(line, sizeof(line), "rice-trial %d %s\n", ms, name);
    send_shell_line(server, line);
    return true;
}

/* `rice try NAME` puts a rice on trial, replying with the milliseconds before it goes back;
 * `rice keep` keeps it and `rice revert` takes it back at once, each replying with its name. */
void control_rice(struct sh_server *server, int fd, const char *arguments) {
    char verb[16] = "", name[64] = "", extra[2] = "", error[320] = "", reply[400];
    int words = sscanf(arguments, " %15s %63s %1s", verb, name, extra);
    if (!strcmp(verb, "try") && words == 2) {
        if (start_trial(server, name, error, sizeof(error))) {
            snprintf(reply, sizeof(reply), "ok\n%d\n", rice_trial_ms(server));
            control_reply(fd, reply);
            return;
        }
    } else if ((!strcmp(verb, "keep") || !strcmp(verb, "revert")) && words == 1) {
        if (!server->rice.trial) {
            snprintf(error, sizeof(error), "no rice is on trial");
        } else if (!strcmp(verb, "revert")) {
            revert_trial(server, "asked");
            snprintf(reply, sizeof(reply), "ok\n%s\n", server->rice.name);
            control_reply(fd, reply);
            return;
        } else {
            forget_trial(server);
            // Kept for this session at least, whether or not its files can be.
            char line[96];
            snprintf(line, sizeof(line), "rice-kept %s\n", server->rice.name);
            send_shell_line(server, line);
            if (rice_files(server, "keep", error, sizeof(error))) {
                snprintf(reply, sizeof(reply), "ok\n%s\n", server->rice.name);
                control_reply(fd, reply);
                return;
            }
        }
    } else {
        snprintf(error, sizeof(error), "usage: rice try NAME | keep | revert");
    }
    snprintf(reply, sizeof(reply), "error: %s\n", error);
    control_reply(fd, reply);
}

/* `get rice_trial`: the rice on trial and the milliseconds left before it goes back, by a tab,
 * or "-". */
void describe_rice_trial(struct sh_server *server, int fd, const char *arguments) {
    char reply[128] = "ok\n-\n";
    if (server->rice.trial) {
        int64_t left = server->rice.ends - now_ms();
        snprintf(reply, sizeof(reply), "ok\n%s\t%lld\n", server->rice.name,
                 (long long)(left > 0 ? left : 0));
    }
    control_reply(fd, reply);
}

/* As the compositor ends: a rice still on trial goes back, so that the next session starts in
 * the previous look. */
void rice_finish(struct sh_server *server) {
    if (server->rice.trial) {
        forget_trial(server);
        char error[256] = "";
        if (!rice_files(server, "revert", error, sizeof(error)))
            wlr_log(WLR_ERROR, "Cannot take the rice %s back: %s", server->rice.name, error);
    }
    if (server->rice.timer)
        wl_event_source_remove(server->rice.timer);
    server->rice.timer = NULL;
}
