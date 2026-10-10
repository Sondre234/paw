/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* Sessions: `session save NAME` writes what every output and window is doing to a file (see
 * paw/session.h); `session restore NAME [launch]` puts matching windows back, and with
 * `launch` starts the applications that are missing, placing their windows as they open. A login
 * session saves itself as `last` as it ends and restores that as the next starts
 * (session.restore). */
#include "server.h"

static void session_read_command(struct sh_toplevel *toplevel, struct sh_session_window *window) {
    pid_t pid = toplevel_pid(toplevel);
    window->command[0] = '\0';
    if (pid <= 1 || pid == getpid())
        return;
    char path[64], cmdline[4096];
    snprintf(path, sizeof(path), "/proc/%d/cmdline", (int)pid);
    FILE *file = fopen(path, "r");
    if (!file)
        return;
    size_t length = fread(cmdline, 1, sizeof(cmdline), file);
    fclose(file);
    if (length < sizeof(cmdline))
        sh_session_set_command(window, cmdline, length);
}

static void session_capture(struct sh_server *server, struct sh_session *session) {
    const struct sh_settings *settings = server_settings(server);
    memset(session, 0, sizeof(*session));
    for (size_t i = 0; i < sizeof(server->output_workspaces) / sizeof(*server->output_workspaces) &&
                       session->output_count < SH_SESSION_MAX_OUTPUTS;
         ++i) {
        const char *name = server->output_workspaces[i].name;
        if (!name[0])
            continue;
        struct sh_session_output *o = &session->outputs[session->output_count++];
        snprintf(o->name, sizeof(o->name), "%s", name);
        o->workspace = server->output_workspaces[i].current;
        struct wlr_output *output = find_output(server, name);
        o->tiling = output ? output_default_tiling(server, output) : server->output_workspaces[i].tiling;
        for (int workspace = 0; workspace < settings->workspaces &&
                                session->layout_count < SH_SESSION_MAX_LAYOUTS;
             ++workspace) {
            enum sh_tile_layout layout = sh_tiling_layout(server->tiling, name, workspace);
            double ratio = sh_tiling_ratio(server->tiling, name, workspace);
            int count = sh_tiling_master_count(server->tiling, name, workspace);
            enum sh_tile_layout default_layout;
            double default_ratio;
            int default_count;
            sh_tiling_output_defaults(server->tiling, name, &default_layout, &default_ratio,
                                      &default_count);
            double widths[SH_SESSION_MAX_COLUMNS];
            int width_count = sh_tiling_scroll_widths(server->tiling, name, workspace, widths,
                                                      SH_SESSION_MAX_COLUMNS);
            if (layout == default_layout && fabs(ratio - default_ratio) < 0.0001 &&
                count == default_count && width_count == 0)
                continue;
            struct sh_session_layout *l = &session->layouts[session->layout_count++];
            memcpy(l->widths, widths, sizeof(double) * (size_t)width_count);
            l->width_count = width_count;
            snprintf(l->output, sizeof(l->output), "%s", name);
            l->workspace = workspace;
            l->layout = layout;
            l->ratio = ratio;
            l->master_count = count;
        }
    }
    // Oldest first, so that restoring in this order ends with the newest on top.
    struct sh_toplevel *toplevel;
    wl_list_for_each_reverse(toplevel, &server->toplevels, link) {
        if (toplevel->unmanaged || !toplevel_mapped(toplevel) || toplevel->group_hidden ||
            toplevel->swallowed || session->window_count >= SH_SESSION_MAX_WINDOWS)
            continue;
        struct sh_session_window *w = &session->windows[session->window_count++];
        const char *app_id = toplevel_app_id(toplevel), *title = toplevel_title(toplevel);
        snprintf(w->output, sizeof(w->output), "%s", toplevel->output);
        w->workspace = toplevel->workspace;
        w->flags = (toplevel->tiled ? SH_SESSION_TILED : 0) |
                   (toplevel->floating ? SH_SESSION_FLOATING : 0) |
                   (toplevel->minimized && !toplevel->scratchpad ? SH_SESSION_MINIMIZED : 0) |
                   (toplevel->sticky ? SH_SESSION_STICKY : 0) |
                   (toplevel->fullscreen ? SH_SESSION_FULLSCREEN : 0) |
                   (toplevel->arranged && toplevel->arrangement == SH_MAXIMIZE && !toplevel->tiled
                        ? SH_SESSION_MAXIMIZED
                        : 0) |
                   (toplevel->scratchpad ? SH_SESSION_SCRATCHPAD : 0) |
                   (server->focused_toplevel == toplevel ? SH_SESSION_FOCUSED : 0) |
                   (toplevel->above ? SH_SESSION_ABOVE : 0);
        // A tile is saved with the place it floats at; a window that has none floats on its tile.
        struct wlr_box box = toplevel_box(toplevel);
        if (toplevel->tiled && toplevel->restore_box.width > 0 && toplevel->restore_box.height > 0)
            box = toplevel->restore_box;
        else if (toplevel->fullscreen && toplevel->fullscreen_restore.width > 0)
            box = toplevel->fullscreen_restore;
        else if (toplevel->arranged && toplevel->restore_box.width > 0 &&
                 toplevel->restore_box.height > 0 && !toplevel->tiled)
            box = toplevel->restore_box;
        w->x = box.x, w->y = box.y, w->width = box.width, w->height = box.height;
        snprintf(w->app_id, sizeof(w->app_id), "%s", app_id ? app_id : "");
        snprintf(w->title, sizeof(w->title), "%s", title ? title : "");
        session_read_command(toplevel, w);
        int row = 0, column = toplevel->tiled ? sh_tiling_scroll_column(server->tiling, toplevel, &row) : -1;
        if (column >= 0) {
            w->scroll_column = column + 1;
            w->scroll_row = row + 1;
        }
    }
}

/* Creates `path` and the directories above it (for the user alone), as `mkdir -p`. */
bool make_directories(char *path) {
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

bool session_save(struct sh_server *server, const char *name, int *windows, char *error,
                  size_t error_size) {
    char path[PATH_MAX], directory[PATH_MAX], temporary[PATH_MAX + 8];
    if (!sh_session_valid_name(name)) {
        snprintf(error, error_size, "a session name is letters, digits, '.', '_' and '-'");
        return false;
    }
    if (!sh_session_path(NULL, directory, sizeof(directory)) ||
        !sh_session_path(name, path, sizeof(path)) || !make_directories(directory)) {
        snprintf(error, error_size, "cannot create the sessions directory");
        return false;
    }
    struct sh_session *session = malloc(sizeof(*session));
    if (!session) {
        snprintf(error, error_size, "out of memory");
        return false;
    }
    session_capture(server, session);
    snprintf(temporary, sizeof(temporary), "%s.new", path);
    FILE *file = fopen(temporary, "w");
    bool ok = file && sh_session_write(session, file);
    if (file && fclose(file) != 0)
        ok = false;
    if (ok && rename(temporary, path) != 0)
        ok = false;
    if (!ok) {
        unlink(temporary);
        snprintf(error, error_size, "cannot write %s: %s", path, strerror(errno));
    }
    *windows = session->window_count;
    free(session);
    return ok;
}

/* Runs a command with an ordinary signal mask (the compositor blocks signals for its event
 * loop); the child is reaped with the others. */
static bool session_spawn(char *const *argv) {
    posix_spawnattr_t attributes;
    if (posix_spawnattr_init(&attributes) != 0)
        return false;
    sigset_t mask;
    sigemptyset(&mask);
    posix_spawnattr_setsigmask(&attributes, &mask);
    posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETSIGMASK);
    pid_t pid;
    extern char **environ;
    int error = posix_spawnp(&pid, argv[0], NULL, &attributes, argv, environ);
    posix_spawnattr_destroy(&attributes);
    if (error)
        wlr_log(WLR_ERROR, "Cannot launch %s: %s", argv[0], strerror(error));
    return error == 0;
}

/* Puts a live window where the session had its match. */
static void session_place(struct sh_server *server, struct sh_toplevel *toplevel,
                          const struct sh_session_window *saved) {
    const struct sh_settings *settings = server_settings(server);
    struct wlr_output *output = find_output(server, saved->output);
    if (!output)
        output = home_output(toplevel);
    if (!output)
        return;
    if (toplevel->fullscreen)
        set_fullscreen(toplevel, false);
    if (toplevel->sticky)
        set_sticky(toplevel, false, false);
    toplevel->scratchpad = false;
    untile_toplevel(toplevel, false);
    if (toplevel->arranged)
        unarrange_in_place(toplevel);
    toplevel->minimized = false;
    set_toplevel_output(toplevel, output);
    toplevel->workspace = saved->workspace < settings->workspaces ? saved->workspace
                                                                   : settings->workspaces - 1;
    toplevel->floating = (saved->flags & SH_SESSION_FLOATING) || toplevel_is_dialog(toplevel);
    toplevel->placed = false;
    if ((saved->flags & SH_SESSION_TILED) && !(saved->flags & SH_SESSION_FLOATING) &&
        wants_tiling(toplevel, output)) {
        // The place it floats at, if it is ever floated, is the saved one.
        if (saved->width > 0 && saved->height > 0)
            toplevel->restore_box = (struct wlr_box){saved->x, saved->y, saved->width, saved->height};
        tile_toplevel(toplevel, output, NULL, false);
    } else if (saved->width > 0 && saved->height > 0) {
        struct wlr_box box = rebase_box(
            server, (struct wlr_box){saved->x, saved->y, saved->width, saved->height}, output);
        toplevel_set_states(toplevel, false, 0);
        toplevel_configure_box(toplevel, box);
    }
    if (saved->flags & SH_SESSION_MAXIMIZED)
        place_by_hand(toplevel, SH_MAXIMIZE);
    if (saved->flags & SH_SESSION_STICKY && settings->sticky)
        set_sticky(toplevel, true, false);
    set_above(toplevel, saved->flags & SH_SESSION_ABOVE);
    wlr_scene_node_set_enabled(&toplevel->scene_tree->node, toplevel_visible(toplevel));
    if (saved->flags & SH_SESSION_SCRATCHPAD)
        hide_in_scratchpad(toplevel);
    else if (saved->flags & SH_SESSION_MINIMIZED)
        minimize_toplevel(toplevel);
    else if (saved->flags & SH_SESSION_FULLSCREEN)
        set_fullscreen(toplevel, true);
}

/* Puts the tiles of the scrolling layout back into the columns they sat in, with the saved
 * widths. Only windows found now are placed; the ones launched by the restore arrive later and
 * open as usual, right of the focused column. */
static void session_restore_columns(struct sh_server *server, const struct sh_session *session,
                                    struct sh_toplevel *const *live, const int *assignment) {
    const struct sh_settings *settings = server_settings(server);
    for (int i = 0; i < session->layout_count; ++i) {
        const struct sh_session_layout *l = &session->layouts[i];
        struct wlr_output *output = find_output(server, l->output);
        if (!output || l->workspace >= settings->workspaces || l->layout != SH_LAYOUT_SCROLL)
            continue;
        void *windows[SH_SESSION_MAX_WINDOWS];
        int columns[SH_SESSION_MAX_WINDOWS], rows[SH_SESSION_MAX_WINDOWS], count = 0;
        for (int j = 0; j < session->window_count; ++j) {
            const struct sh_session_window *saved = &session->windows[j];
            if (assignment[j] < 0 || saved->scroll_column < 1 || saved->workspace != l->workspace ||
                strcmp(saved->output, l->output) != 0)
                continue;
            struct sh_toplevel *toplevel = live[assignment[j]];
            if (!toplevel->tiled || strcmp(toplevel->output, output->name) != 0)
                continue;
            windows[count] = toplevel;
            columns[count] = saved->scroll_column - 1;
            rows[count++] = saved->scroll_row - 1;
        }
        if (count && sh_tiling_scroll_restore(server->tiling, output->name, l->workspace, windows,
                                              columns, rows, count, l->widths, l->width_count))
            reflow_output(server, output);
    }
}

/* How long the windows of a restore that are still to open have to open: those launched by hand,
 * and those a login session's startup, autostart or restore starts, which may take a while
 * longer as everything starts at once. */
#define PENDING_MS 30000
#define LOGIN_PENDING_MS 60000

/* session_restore; as a login session starts (`login`), a missing window that startup or autostart
 * started a program for waits for it (`waiting`) rather than being launched again. */
static bool restore_session(struct sh_server *server, const char *name, bool launch, bool login,
                            int *restored, int *launched, int *waiting, int *missing,
                            char *error, size_t error_size) {
    char path[PATH_MAX];
    if (!sh_session_path(name, path, sizeof(path))) {
        snprintf(error, error_size, "a session name is letters, digits, '.', '_' and '-'");
        return false;
    }
    FILE *file = fopen(path, "r");
    if (!file) {
        snprintf(error, error_size, "no session named %s", name);
        return false;
    }
    struct sh_session *session = malloc(sizeof(*session));
    bool ok = session && sh_session_read(session, file, error, error_size);
    fclose(file);
    if (!session) {
        snprintf(error, error_size, "out of memory");
        return false;
    }
    if (!ok) {
        free(session);
        return false;
    }
    const struct sh_settings *settings = server_settings(server);
    for (int i = 0; i < session->output_count; ++i) {
        struct wlr_output *output = find_output(server, session->outputs[i].name);
        if (output && session->outputs[i].tiling >= 0)
            set_output_tiling(server, output, session->outputs[i].tiling == 1);
    }
    for (int i = 0; i < session->layout_count; ++i) {
        const struct sh_session_layout *l = &session->layouts[i];
        struct wlr_output *output = find_output(server, l->output);
        if (!output || l->workspace >= settings->workspaces || l->layout >= SH_LAYOUT_COUNT)
            continue;
        sh_tiling_set_layout(server->tiling, output->name, l->workspace,
                             (enum sh_tile_layout)l->layout);
        sh_tiling_adjust(server->tiling, output->name, l->workspace,
                         l->ratio - sh_tiling_ratio(server->tiling, output->name, l->workspace),
                         l->master_count -
                             sh_tiling_master_count(server->tiling, output->name, l->workspace));
        reflow_output(server, output);
    }
    struct sh_toplevel *live[SH_SESSION_MAX_WINDOWS * 2];
    const char *live_app_ids[SH_SESSION_MAX_WINDOWS * 2], *live_titles[SH_SESSION_MAX_WINDOWS * 2];
    int live_count = 0;
    struct sh_toplevel *toplevel;
    wl_list_for_each_reverse(toplevel, &server->toplevels, link) {
        if (toplevel->unmanaged || !toplevel_mapped(toplevel) ||
            live_count >= SH_SESSION_MAX_WINDOWS * 2)
            continue;
        const char *app_id = toplevel_app_id(toplevel), *title = toplevel_title(toplevel);
        live[live_count] = toplevel;
        live_app_ids[live_count] = app_id ? app_id : "";
        live_titles[live_count++] = title ? title : "";
    }
    int assignment[SH_SESSION_MAX_WINDOWS];
    sh_session_match(session->windows, session->window_count, live_app_ids, live_titles,
                     live_count, assignment);
    *restored = *launched = *waiting = *missing = 0;
    struct sh_toplevel *focus = NULL;
    for (int i = 0; i < session->window_count; ++i) {
        const struct sh_session_window *saved = &session->windows[i];
        if (assignment[i] >= 0) {
            session_place(server, live[assignment[i]], saved);
            if (saved->flags & SH_SESSION_FOCUSED)
                focus = live[assignment[i]];
            ++*restored;
            continue;
        }
        char **argv = sh_session_argv(saved->command);
        const struct sh_callbacks *callbacks = server->callbacks;
        bool started = login && callbacks->started &&
                       callbacks->started(callbacks->userdata, saved->app_id, argv ? argv[0] : "");
        size_t slot = 0, slots = sizeof(server->session_pending) / sizeof(*server->session_pending);
        int64_t now = now_ms();
        while (slot < slots && server->session_pending[slot].used &&
               server->session_pending[slot].deadline >= now)
            ++slot;
        if (slot < slots && (started || (launch && argv && session_spawn(argv)))) {
            server->session_pending[slot].window = *saved;
            server->session_pending[slot].deadline = now + (login ? LOGIN_PENDING_MS : PENDING_MS);
            server->session_pending[slot].used = true;
            ++*(started ? waiting : launched);
        } else {
            ++*missing;
        }
        free(argv);
    }
    session_restore_columns(server, session, live, assignment);
    for (int i = 0; i < session->output_count; ++i) {
        struct wlr_output *output = find_output(server, session->outputs[i].name);
        if (output && session->outputs[i].workspace < settings->workspaces)
            switch_workspace(server, output, session->outputs[i].workspace);
    }
    if (focus && toplevel_visible(focus))
        focus_toplevel(focus);
    notify_subscribers(server);
    free(session);
    return true;
}

bool session_restore(struct sh_server *server, const char *name, bool launch,
                     int *restored, int *launched, int *missing, char *error,
                     size_t error_size) {
    int waiting;
    return restore_session(server, name, launch, false, restored, launched, &waiting, missing,
                           error, error_size);
}

/* The session a login session ends with and the next one starts from. */
static const char last_session[] = "last";

/* Whether this session keeps the last one: a login session, with session.restore on. */
static bool keeps_last(struct sh_server *server) {
    return server->login_session &&
           server_settings(server)->session_restore != SH_SESSION_RESTORE_OFF;
}

void session_save_last(struct sh_server *server) {
    if (!keeps_last(server))
        return;
    int windows = 0;
    char error[320];
    if (session_save(server, last_session, &windows, error, sizeof(error)))
        wlr_log(WLR_INFO, "Saved the session as %s: %d windows", last_session, windows);
    else
        wlr_log(WLR_ERROR, "Cannot save the session as %s: %s", last_session, error);
}

void session_restore_last(struct sh_server *server) {
    char path[PATH_MAX];
    if (!keeps_last(server) || !sh_session_path(last_session, path, sizeof(path)) ||
        access(path, F_OK) != 0)
        return;
    bool launch = server_settings(server)->session_restore == SH_SESSION_RESTORE_LAUNCH;
    int restored, launched, waiting, missing;
    char error[320];
    if (restore_session(server, last_session, launch, true, &restored, &launched, &waiting,
                        &missing, error, sizeof(error)))
        wlr_log(WLR_INFO,
                "Restored the last session: %d windows placed, %d waited for from startup and "
                "autostart, %d launched, %d not found",
                restored, waiting, launched, missing);
    else
        wlr_log(WLR_ERROR, "Cannot restore the last session: %s", error);
}

/* A window a session restore launched takes over the saved window's place as its rule: the
 * output, workspace, floating place and state it had. Returns whether one matched. */
bool session_claim(struct sh_server *server, struct sh_toplevel *toplevel,
                   struct sh_window_rule *rule, bool ruled) {
    const char *app_id = toplevel_app_id(toplevel);
    int64_t now = now_ms();
    for (size_t i = 0; i < sizeof(server->session_pending) / sizeof(*server->session_pending);
         ++i) {
        if (server->session_pending[i].used && server->session_pending[i].deadline < now)
            server->session_pending[i].used = false;
    }
    for (size_t i = 0; i < sizeof(server->session_pending) / sizeof(*server->session_pending);
         ++i) {
        if (!server->session_pending[i].used ||
            strcmp(server->session_pending[i].window.app_id, app_id ? app_id : ""))
            continue;
        const struct sh_session_window *saved = &server->session_pending[i].window;
        server->session_pending[i].used = false;
        if (!ruled)
            memset(rule, 0, sizeof(*rule)), rule->floating = -1;
        snprintf(rule->output, sizeof(rule->output), "%s", saved->output);
        rule->workspace = saved->workspace + 1;
        rule->floating = saved->flags & SH_SESSION_TILED ? 0 : saved->flags & SH_SESSION_FLOATING ? 1 : -1;
        rule->fullscreen = saved->flags & SH_SESSION_FULLSCREEN;
        rule->maximize = saved->flags & SH_SESSION_MAXIMIZED;
        rule->sticky = saved->flags & SH_SESSION_STICKY;
        rule->above = saved->flags & SH_SESSION_ABOVE;
        rule->no_focus = !(saved->flags & SH_SESSION_FOCUSED);
        rule->width = rule->height = 0;
        rule->position = SH_RULE_POSITION_UNSET;
        struct wlr_output *output = find_output(server, saved->output);
        if (output && saved->width > 0 && saved->height > 0) {
            struct sh_rect area = usable_area(server, output);
            rule->width = saved->width;
            rule->height = saved->height;
            rule->position = SH_RULE_POSITION_AT;
            rule->x = saved->x - area.x;
            rule->y = saved->y - area.y;
        }
        return true;
    }
    return ruled;
}
