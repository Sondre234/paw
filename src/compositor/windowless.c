/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Processes left running without a window: those whose last window closed while they went on,
 * as an application hidden to a tray that shows no item, or one whose window died with its
 * connection while the process did not. Each is watched through a pidfd until it ends, listed by
 * `get windowless` and told to the shell with the state ("windowless PID APP_ID"), whose launchers
 * offer to kill it, which `windowless kill PID` does. A login session hands them, and the
 * processes of the windows still open, to the next one through $XDG_RUNTIME_DIR/paw-windowless:
 * an application that outlives a restart of the compositor without its window is known there
 * too. */
#include "server.h"
#include <sys/syscall.h>

/* The most a login session hands to the next. */
#define HANDED_MAX 256

static int open_pidfd(pid_t pid) {
    return (int)syscall(SYS_pidfd_open, pid, 0);
}

static int signal_pidfd(int pidfd, int signal) {
    return (int)syscall(SYS_pidfd_send_signal, pidfd, signal, NULL, 0);
}

/* When process `pid` started, in clock ticks after boot (the 22nd field of /proc/PID/stat), which
 * tells it from a later process given its PID; 0 when it cannot be read. */
static unsigned long long process_start(pid_t pid) {
    char path[64], text[1024];
    snprintf(path, sizeof(path), "/proc/%d/stat", (int)pid);
    FILE *file = fopen(path, "r");
    if (!file)
        return 0;
    size_t length = fread(text, 1, sizeof(text) - 1, file);
    fclose(file);
    text[length] = '\0';
    // The second field, the command's name, is in parentheses and may hold anything.
    const char *after = strrchr(text, ')');
    unsigned long long started = 0;
    if (!after || sscanf(after + 1,
                         " %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s "
                         "%*s %*s %llu",
                         &started) != 1)
        return 0;
    return started;
}

static struct sh_windowless *find_windowless(struct sh_server *server, pid_t pid) {
    struct sh_windowless *process;
    wl_list_for_each(process, &server->windowless, link) {
        if (process->pid == pid)
            return process;
    }
    return NULL;
}

static void forget(struct sh_windowless *process) {
    wl_list_remove(&process->link);
    wl_event_source_remove(process->ended);
    close(process->pidfd);
    free(process);
}

static int process_ended(int fd, uint32_t mask, void *data) {
    struct sh_windowless *process = data;
    struct sh_server *server = process->server;
    forget(process);
    notify_subscribers(server);
    return 0;
}

/* Watches process `pid`, whose last window had `app_id`, until it ends: one of the user's own,
 * which may be killed, and with `started` given, only the process that started then. Returns
 * whether it is watched. */
static bool watch(struct sh_server *server, pid_t pid, unsigned long long started,
                  const char *app_id) {
    if (pid <= 1 || pid == getpid() || find_windowless(server, pid))
        return false;
    int pidfd = open_pidfd(pid);
    if (pidfd < 0)
        return false; // it has ended
    unsigned long long now = process_start(pid);
    if (signal_pidfd(pidfd, 0) != 0 || !now || (started && started != now)) {
        close(pidfd);
        return false;
    }
    struct sh_windowless *process = calloc(1, sizeof(*process));
    if (!process) {
        close(pidfd);
        return false;
    }
    process->server = server;
    process->pid = pid;
    process->started = now;
    process->pidfd = pidfd;
    copy_field(process->app_id, sizeof(process->app_id), app_id);
    // A pidfd turns readable as its process ends.
    process->ended = wl_event_loop_add_fd(wl_display_get_event_loop(server->wl_display), pidfd,
                                          WL_EVENT_READABLE, process_ended, process);
    if (!process->ended) {
        close(pidfd);
        free(process);
        return false;
    }
    wl_list_insert(server->windowless.prev, &process->link);
    return true;
}

void windowless_window_opened(struct sh_toplevel *toplevel) {
    struct sh_windowless *process = find_windowless(toplevel->server, toplevel_pid(toplevel));
    if (process)
        forget(process);
}

void windowless_window_closed(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    // As the compositor ends, every window closes with it; the processes are not left.
    if (!server->running)
        return;
    pid_t pid = toplevel_pid(toplevel);
    struct sh_toplevel *other;
    wl_list_for_each(other, &server->toplevels, link) {
        if (toplevel_pid(other) == pid)
            return;
    }
    if (watch(server, pid, 0, toplevel_app_id(toplevel)))
        wlr_log(WLR_INFO, "Process %d (%s) runs on without a window", (int)pid,
                toplevel_app_id(toplevel) ? toplevel_app_id(toplevel) : "no app ID");
}

/* The file a login session hands its processes to the next one in, in the runtime directory:
 * a PID means nothing after the machine starts again. */
static bool handed_path(char *path, size_t size) {
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    return runtime && *runtime && snprintf(path, size, "%s/paw-windowless", runtime) < (int)size;
}

void windowless_init(struct sh_server *server) {
    char path[PATH_MAX];
    if (!server->login_session || !handed_path(path, sizeof(path)))
        return;
    FILE *file = fopen(path, "r");
    if (!file)
        return;
    char line[256];
    int handed = 0, watched = 0;
    while (handed < HANDED_MAX && fgets(line, sizeof(line), file)) {
        int pid;
        unsigned long long started;
        char app_id[128] = "";
        line[strcspn(line, "\n")] = '\0';
        if (sscanf(line, "%d\t%llu\t%127[^\n]", &pid, &started, app_id) < 2 || !started)
            continue;
        ++handed;
        watched += watch(server, pid, started, app_id);
    }
    fclose(file);
    if (!watched)
        return;
    wlr_log(WLR_INFO, "%d of the %d processes the last session handed over run on without a window",
            watched, handed);
    // Outputs may have been told of already, without these.
    notify_subscribers(server);
}

/* Writes "PID<tab>START<tab>APP_ID" for `pid` unless it is written already (`written`, `count`
 * of them). */
static void hand(FILE *file, pid_t pid, unsigned long long started, const char *app_id,
                 pid_t *written, int *count) {
    if (pid <= 1 || pid == getpid() || !started || *count >= HANDED_MAX)
        return;
    for (int i = 0; i < *count; ++i)
        if (written[i] == pid)
            return;
    written[(*count)++] = pid;
    char field[128];
    copy_field(field, sizeof(field), app_id);
    fprintf(file, "%d\t%llu\t%s\n", (int)pid, started, field);
}

void windowless_save(struct sh_server *server) {
    char path[PATH_MAX], temporary[PATH_MAX + 8];
    if (!server->login_session || !handed_path(path, sizeof(path)))
        return;
    snprintf(temporary, sizeof(temporary), "%s.new", path);
    int fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    FILE *file = fd >= 0 ? fdopen(fd, "w") : NULL;
    if (fd >= 0 && !file)
        close(fd);
    if (!file) {
        wlr_log_errno(WLR_ERROR, "Cannot write %s", temporary);
        return;
    }
    pid_t written[HANDED_MAX];
    int count = 0;
    struct sh_windowless *process;
    wl_list_for_each(process, &server->windowless, link)
        hand(file, process->pid, process->started, process->app_id, written, &count);
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        pid_t pid = toplevel_pid(toplevel);
        hand(file, pid, process_start(pid), toplevel_app_id(toplevel), written, &count);
    }
    if (fclose(file) != 0 || rename(temporary, path) != 0) {
        wlr_log_errno(WLR_ERROR, "Cannot write %s", path);
        unlink(temporary);
    }
}

void windowless_finish(struct sh_server *server) {
    struct sh_windowless *process, *next;
    wl_list_for_each_safe(process, next, &server->windowless, link)
        forget(process);
}

void describe_windowless(struct sh_server *server, int fd, const char *arguments) {
    control_reply(fd, "ok\n");
    struct sh_windowless *process;
    wl_list_for_each(process, &server->windowless, link) {
        char line[160];
        snprintf(line, sizeof(line), "%d\t%s\n", (int)process->pid,
                 process->app_id[0] ? process->app_id : "-");
        control_reply(fd, line);
    }
}

void control_windowless(struct sh_server *server, int fd, const char *arguments) {
    int pid;
    char extra[2];
    if (sscanf(arguments, " kill %d %1s", &pid, extra) != 1) {
        control_reply(fd, "error: usage: windowless kill PID\n");
        return;
    }
    struct sh_windowless *process = find_windowless(server, pid);
    if (!process) {
        control_reply(fd, "error: no process without a window has that PID\n");
        return;
    }
    // Through its pidfd, never a later process given its PID.
    if (signal_pidfd(process->pidfd, SIGKILL) != 0) {
        char reply[128];
        snprintf(reply, sizeof(reply), "error: %s\n", strerror(errno));
        control_reply(fd, reply);
        return;
    }
    wlr_log(WLR_INFO, "Killed process %d (%s), which had no window", pid,
            process->app_id[0] ? process->app_id : "no app ID");
    control_reply(fd, "ok\n");
}
