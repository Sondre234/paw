/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* The control socket (`paw msg`): one request per connection, answered with "ok" and any
 * output, or with "error: ...". Subscribers stay connected and get the state after each change,
 * and events for the shell. */
#include "server.h"

#include <ctype.h>
#include <stdarg.h>

/* Control socket: one newline-terminated request per connection, answered with
 * "ok\n" plus any output, or "error: ...\n". Lives in the private runtime dir. */
struct sh_control_client {
    struct sh_server *server;
    int fd;
    struct wl_event_source *source;
    bool subscribed; // "subscribe": stays open and receives the state after each change
    bool shell;      // "subscribe shell": the desktop shell, which carries out what it is asked
    struct wl_list link;
    size_t length;
    char request[4096]; // `monitors apply` names every monitor's settings
};

void control_reply(int fd, const char *text) {
    size_t length = strlen(text);
    while (length > 0) {
        ssize_t written = send(fd, text, length, MSG_NOSIGNAL);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return;
        text += written;
        length -= (size_t)written;
    }
}

/* Answers "ok" for a request carried out, else "error: " and why. */
static void reply_done(int fd, bool done, const char *error) {
    char reply[PATH_MAX + 80];
    snprintf(reply, sizeof(reply), done ? "ok\n" : "error: %s\n", error);
    control_reply(fd, reply);
}

/* "session list"'s order and filter for scandir: the saved sessions by name. */
static int session_name_compare(const struct dirent **a, const struct dirent **b) {
    return strcmp((*a)->d_name, (*b)->d_name);
}
static int session_name_filter(const struct dirent *entry) {
    return sh_session_valid_name(entry->d_name);
}

static void control_session(struct sh_server *server, int fd, const char *arguments) {
    char verb[16] = "", name[SH_SESSION_NAME_MAX + 8] = "", option[16] = "", extra[8] = "";
    int count = sscanf(arguments, " %15s %71s %15s %7s", verb, name, option, extra);
    char error[300] = "", reply[512];
    if (!strcmp(verb, "list") && count == 1) {
        char directory[PATH_MAX];
        control_reply(fd, "ok\n");
        struct dirent **entries = NULL;
        int found = sh_session_path(NULL, directory, sizeof(directory))
                        ? scandir(directory, &entries, session_name_filter, session_name_compare)
                        : -1;
        for (int i = 0; i < found; ++i) {
            char path[PATH_MAX];
            struct stat info;
            sh_session_path(entries[i]->d_name, path, sizeof(path));
            if (stat(path, &info) == 0 && S_ISREG(info.st_mode)) {
                int windows = 0;
                FILE *file = fopen(path, "r");
                char line[8192];
                while (file && fgets(line, sizeof(line), file))
                    windows += !strncmp(line, "window\t", 7);
                if (file)
                    fclose(file);
                snprintf(reply, sizeof(reply), "%s\t%d\t%lld\n", entries[i]->d_name, windows,
                         (long long)info.st_mtime);
                control_reply(fd, reply);
            }
            free(entries[i]);
        }
        free(entries);
        return;
    }
    if (!strcmp(verb, "save") && count == 2) {
        int windows = 0;
        if (session_save(server, name, &windows, error, sizeof(error)))
            snprintf(reply, sizeof(reply), "ok\nsaved %s: %d windows\n", name, windows);
        else
            snprintf(reply, sizeof(reply), "error: %s\n", error);
        control_reply(fd, reply);
        return;
    }
    if (!strcmp(verb, "restore") && (count == 2 || (count == 3 && !strcmp(option, "launch")))) {
        int restored, launched, missing;
        if (session_restore(server, name, count == 3, &restored, &launched, &missing, error,
                            sizeof(error)))
            snprintf(reply, sizeof(reply), "ok\nrestored %d, launched %d, not found %d\n",
                     restored, launched, missing);
        else
            snprintf(reply, sizeof(reply), "error: %s\n", error);
        control_reply(fd, reply);
        return;
    }
    if (!strcmp(verb, "delete") && count == 2) {
        char path[PATH_MAX];
        if (!sh_session_path(name, path, sizeof(path)))
            snprintf(reply, sizeof(reply), "error: a session name is letters, digits, '.', '_' and '-'\n");
        else if (unlink(path) != 0)
            snprintf(reply, sizeof(reply), "error: no session named %s\n", name);
        else
            snprintf(reply, sizeof(reply), "ok\n");
        control_reply(fd, reply);
        return;
    }
    control_reply(fd, "error: usage: session save NAME | restore NAME [launch] | list | delete NAME\n");
}

/* An output, enabled or not, by connector name. */
static struct sh_output *sh_output_for_name(struct sh_server *server, const char *name) {
    struct sh_output *output;
    for_each_connected_output(output, server) {
        if (output_named(output, name))
            return output;
    }
    return NULL;
}

static void find_headless(struct wlr_backend *backend, void *data) {
    struct wlr_backend **found = data;
    if (wlr_backend_is_headless(backend))
        *found = backend;
}

/* The headless backend, where tests plug in outputs and keyboards; NULL without --headless. */
struct wlr_backend *headless_backend(struct sh_server *server) {
    struct wlr_backend *headless = NULL;
    if (wlr_backend_is_headless(server->backend))
        headless = server->backend;
    else if (wlr_backend_is_multi(server->backend))
        wlr_multi_for_each_backend(server->backend, find_headless, &headless);
    return headless;
}

/* "headless_output add [NAME] [WIDTHxHEIGHT]" plugs in a virtual output, and "headless_output
 * remove NAME" unplugs one, so tests can exercise hotplug without a display; "headless_output
 * capture NAME PATH" writes a mirror's picture to a file. Only under --headless. */
static void control_headless_output(struct sh_server *server, int fd, const char *args) {
    struct wlr_backend *headless = headless_backend(server);
    if (!headless) {
        control_reply(fd, "error: headless_output needs --headless\n");
        return;
    }
    char verb[16] = "", first[64] = "", second[64] = "";
    int fields = sscanf(args, "%15s %63s %63s", verb, first, second);
    if (!strcmp(verb, "add") && fields >= 1 && fields <= 3) {
        unsigned width = 1280, height = 720;
        char name[64] = "";
        for (int i = 1; i < fields; ++i) {
            const char *token = i == 1 ? first : second;
            char extra;
            if (sscanf(token, "%ux%u%c", &width, &height, &extra) == 2)
                continue;
            if (name[0]) {
                control_reply(fd, "error: usage: headless_output add [NAME] [WIDTHxHEIGHT]\n");
                return;
            }
            snprintf(name, sizeof(name), "%s", token);
        }
        if (!width || !height || width > 16384 || height > 16384) {
            control_reply(fd, "error: bad size\n");
            return;
        }
        if (name[0] && sh_output_for_name(server, name)) {
            control_reply(fd, "error: an output with that name exists\n");
            return;
        }
        snprintf(server->pending_output_name, sizeof(server->pending_output_name), "%s", name);
        struct wlr_output *added = wlr_headless_add_output(headless, width, height);
        server->pending_output_name[0] = '\0';
        if (!added) {
            control_reply(fd, "error: cannot add an output\n");
            return;
        }
        char reply[96];
        snprintf(reply, sizeof(reply), "ok\n%s\n", added->name);
        control_reply(fd, reply);
        return;
    }
    if (!strcmp(verb, "remove") && fields == 2) {
        struct sh_output *output = sh_output_for_name(server, first);
        if (!output) {
            control_reply(fd, "error: no such output\n");
            return;
        }
        wlr_output_destroy(output->wlr_output);
        control_reply(fd, "ok\n");
        return;
    }
    // A mirror's picture, which no screenshot tool can name (mirror.c).
    int path_at = 0;
    sscanf(args, "%*15s %*63s %n", &path_at);
    if (!strcmp(verb, "capture") && fields == 3 && path_at > 0) {
        struct sh_output *output = sh_output_for_name(server, first);
        char error[PATH_MAX + 64];
        if (!output)
            snprintf(error, sizeof(error), "no such output");
        reply_done(fd, output && mirror_capture(output, args + path_at, error, sizeof(error)),
                   error);
        return;
    }
    control_reply(fd, "error: usage: headless_output add [NAME] [WIDTHxHEIGHT] | remove NAME | "
                      "capture NAME PATH\n");
}

/* "osd TEXT [PERCENT]": shows the shell's on-screen display on the focused output. A last word
 * that is a whole number from 0 to 100, with an optional %, is the level; the shell hears
 * "osd OUTPUT PERCENT TEXT", the percent -1 for none. */
static void control_osd(struct sh_server *server, int fd, const char *arguments) {
    arguments += *arguments == ' ';
    char text[512];
    copy_field(text, sizeof(text), arguments);
    size_t length = strlen(text);
    while (length && text[length - 1] == ' ')
        text[--length] = '\0';
    char *start = text;
    while (*start == ' ')
        ++start;
    if (!*start) {
        control_reply(fd, "error: usage: osd TEXT [PERCENT]\n");
        return;
    }
    int percent = -1;
    char *last = strrchr(start, ' ');
    if (last) {
        char *end = NULL;
        long value = strtol(last + 1, &end, 10);
        if (end != last + 1 && (!*end || (!strcmp(end, "%"))) && value >= 0 && value <= 100) {
            percent = (int)value;
            while (last > start && last[-1] == ' ')
                --last;
            *last = '\0';
        }
    }
    struct wlr_output *output = focused_output(server);
    char line[640];
    snprintf(line, sizeof(line), "osd %s %d %s\n", output ? output->name : "-", percent, start);
    send_shell_line(server, line);
    control_reply(fd, "ok\n");
}

/* What follows `name` in a request that starts with it as a word, from the space after it; NULL
 * for another request. */
static const char *arguments_of(const char *request, const char *name) {
    size_t length = strlen(name);
    if (strncmp(request, name, length) || (request[length] && request[length] != ' '))
        return NULL;
    return request + length;
}

/* "dnd [on|off|toggle]": the shell's notification daemon stops or resumes its cards. */
static void control_dnd(struct sh_server *server, int fd, const char *arguments) {
    const char *verb = *arguments ? arguments + 1 : "toggle";
    if (strcmp(verb, "on") && strcmp(verb, "off") && strcmp(verb, "toggle")) {
        control_reply(fd, "error: usage: dnd [on|off|toggle]\n");
        return;
    }
    char line[32];
    snprintf(line, sizeof(line), "dnd %s\n", verb);
    send_shell_line(server, line);
    control_reply(fd, "ok\n");
}

/* "type TEXT": types TEXT into what has the keyboard (type.c). */
static void control_type(struct sh_server *server, int fd, const char *arguments) {
    char error[128];
    reply_done(fd, type_text(server, arguments + (*arguments == ' '), error, sizeof(error)), error);
}

/* "overview filter [TEXT]", "overview select N" and "overview view N" (from 1) drive the open
 * overview, as typing, arrows and the strip do. */
static void control_overview(struct sh_server *server, int fd, const char *arguments) {
    const char *verb = arguments + (*arguments == ' ');
    const char *filter = arguments_of(verb, "filter"), *selection = arguments_of(verb, "select");
    const char *view = arguments_of(verb, "view");
    char *end = NULL;
    long number = strtol(selection ? selection : view ? view : "", &end, 10);
    bool whole = !*end && number >= 1;
    if (!server->overview.open) {
        control_reply(fd, "error: the overview is not open\n");
    } else if (filter) {
        overview_set_filter(server, filter + (*filter == ' '));
        control_reply(fd, "ok\n");
    } else if (selection && whole && number <= server->overview.count) {
        overview_select(server, (int)number - 1);
        control_reply(fd, "ok\n");
    } else if (view && whole && number <= server->overview.workspaces) {
        overview_view(server, (int)number - 1);
        control_reply(fd, "ok\n");
    } else {
        control_reply(fd, "error: usage: overview filter [TEXT] | select N | view N\n");
    }
}

/* The commands that are not actions, by name. Each hears the rest of the request from the space
 * after its name ("" for the name alone), which sscanf and split_words skip. */
static const struct {
    const char *name;
    void (*run)(struct sh_server *server, int fd, const char *arguments);
} commands[] = {
    {"headless_output", control_headless_output},
    {"headless_pointer", control_headless_pointer},
    {"headless_touch", control_headless_touch},
    {"headless_tablet", control_headless_tablet},
    {"headless_switch", control_headless_switch},
    {"session", control_session},
    {"dnd", control_dnd},
    {"monitors", control_monitors},
    {"osd", control_osd},
    {"type", control_type},
    {"overview", control_overview},
};

static void control_handle(struct sh_server *server, int fd, const char *request) {
    if (run_query(server, fd, request))
        return;
    // A test's keyboard types on the lock screen too, as any keyboard does.
    const char *arguments = arguments_of(request, "headless_keyboard");
    if (arguments) {
        control_headless_keyboard(server, fd, arguments);
        return;
    }
    if (server->locked) {
        control_reply(fd, "error: the session is locked\n");
        return;
    }
    for (size_t i = 0; i < sizeof(commands) / sizeof(*commands); ++i) {
        arguments = arguments_of(request, commands[i].name);
        if (arguments) {
            commands[i].run(server, fd, arguments);
            return;
        }
    }
    // "output NAME ACTION": workspace actions switch that output instead of the focused one.
    struct wlr_output *target = NULL;
    if (!strncmp(request, "output ", 7)) {
        char name[64];
        const char *action = strchr(request + 7, ' ');
        int length = action ? (int)(action - request - 7) : 0;
        snprintf(name, sizeof(name), "%.*s", length, request + 7);
        target = action ? find_output(server, name) : NULL;
        if (!target) {
            reply_done(fd, false, action ? "no such output" : "output needs a name and an action");
            return;
        }
        request = action + 1;
    }
    char error[256] = "";
    int argument = 0;
    enum sh_action action = server->callbacks->command(server->callbacks->userdata, request,
                                                       &argument, error, sizeof(error));
    xkb_layout_index_t layouts = server->keymap ? xkb_keymap_num_layouts(server->keymap) : 0;
    // The caller hears why an action did not start where it can be told: a program that did not
    // start, as the panel does, a screenshot without grim, a power action logind does not allow,
    // a display mode that cannot be had (one monitor alone) or a monitor that is not there.
    bool done = false;
    if (action == SH_NONE) {
        if (!error[0])
            snprintf(error, sizeof(error), "unknown request");
    } else if (action == SH_SWITCH_LAYOUT && argument > 0 &&
               (xkb_layout_index_t)argument > layouts) {
        snprintf(error, sizeof(error), "the keymap has %u layout%s", layouts,
                 layouts == 1 ? "" : "s");
    } else if (action == SH_SPAWN || action == SH_TERMINAL) {
        done = launch_program(server, action, error, sizeof(error));
    } else if (action == SH_SCREENSHOT) {
        done = take_screenshot(server, (enum sh_screenshot_mode)argument, error, sizeof(error));
    } else if (power_action(action)) {
        done = power_start(server, action, error, sizeof(error));
    } else if (action == SH_DISPLAY_MODE) {
        done = display_mode_choose(server, argument, error, sizeof(error));
    } else {
        server->target_output = target;
        if (display_action(action)) {
            done = display_power(server, action, error, sizeof(error));
        } else {
            run_action(server, action, argument);
            done = true;
        }
        server->target_output = NULL;
    }
    reply_done(fd, done, error);
}

static void control_client_close(struct sh_control_client *client) {
    if (client->subscribed)
        wl_list_remove(&client->link);
    wl_event_source_remove(client->source);
    close(client->fd);
    free(client);
}

/* Removes a multi-byte character cut short at the end of `text`, as snprintf leaves one. */
void drop_partial_utf8(char *text) {
    size_t length = strlen(text), start = length;
    while (start > 0 && ((unsigned char)text[start - 1] & 0xC0) == 0x80)
        --start;
    if (start == 0)
        return;
    unsigned char lead = (unsigned char)text[start - 1];
    if (lead < 0xC0)
        return; // ASCII, or stray continuation bytes: nothing was cut
    size_t needed = lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2 : lead >= 0xC0 ? 1 : 0;
    if (length - start < needed)
        text[start - 1] = '\0';
}

/* Makes the tabs and line breaks in `text` spaces, so that it stays one column of one line. */
void flatten_field(char *text) {
    for (char *c = text; *c; ++c)
        if (*c == '\t' || *c == '\n' || *c == '\r')
            *c = ' ';
}

/* Copies `text` (none for NULL) into `out` as flatten_field leaves it. */
void copy_field(char *out, size_t size, const char *text) {
    snprintf(out, size, "%s", text ? text : "");
    flatten_field(out);
}

/* Adds to `text`, `size` bytes of which `*length` are used, as snprintf would; `*length` counts
 * what did not fit too, so that nothing more is added once one part did not. */
static void append(char *text, size_t size, size_t *length, const char *format, ...)
    __attribute__((format(printf, 4, 5)));
static void append(char *text, size_t size, size_t *length, const char *format, ...) {
    if (*length >= size)
        return;
    va_list arguments;
    va_start(arguments, format);
    int added = vsnprintf(text + *length, size - *length, format, arguments);
    va_end(arguments);
    if (added > 0)
        *length += (size_t)added;
}

/* The state subscribers get: "tiling on|off", "workspace N" and "focused NAME" for the focused
 * output, and "output NAME N USED TILING" for each output, with its current workspace, those
 * holding windows ("1,3", or "-"), and whether it tiles ("on" or "off"); then the urgent
 * windows, the keyboard layout and night light, as described below, and the power actions
 * that may run. */
static void describe_state(struct sh_server *server, char *state, size_t size) {
    struct wlr_output *focused = focused_output(server);
    size_t length = 0;
    append(state, size, &length, "tiling %s\nworkspace %d\nfocused %s\n",
           output_tiles(server, focused) ? "on" : "off", focused_workspace(server),
           focused ? focused->name : "-");
    struct sh_output *output;
    wl_list_for_each_reverse(output, &server->outputs, link) {
        char used[128];
        occupied_workspaces(server, output->wlr_output, used, sizeof(used));
        append(state, size, &length, "output %s %d %s %s\n", output->wlr_output->name,
               *output_workspace(server, output->wlr_output->name) + 1, used,
               output_tiles(server, output->wlr_output) ? "on" : "off");
    }
    // "urgent COUNT", then "urgent-output NAME 2,3" for each output with urgent windows, the
    // workspaces they are on, and "urgent-window OUTPUT WORKSPACE APP_ID TITLE" (tab separated
    // after the name) for each, the one that has waited longest first.
    unsigned count = 0;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) count += toplevel->urgent;
    append(state, size, &length, "urgent %u\n", count);
    wl_list_for_each_reverse(output, &server->outputs, link) {
        unsigned used = 0;
        wl_list_for_each(toplevel, &server->toplevels, link) {
            if (toplevel->urgent && toplevel->workspace < 32 &&
                !strcmp(toplevel->output, output->wlr_output->name))
                used |= 1u << toplevel->workspace;
        }
        if (!used)
            continue;
        append(state, size, &length, "urgent-output %s", output->wlr_output->name);
        for (int i = 0, first = 1; i < 32; ++i) {
            if (used & 1u << i) {
                append(state, size, &length, "%s%d", first ? " " : ",", i + 1);
                first = 0;
            }
        }
        append(state, size, &length, "\n");
    }
    unsigned last = 0;
    for (unsigned listed = 0; listed < count && listed < 16 && length < size; ++listed) {
        struct sh_toplevel *next = NULL;
        wl_list_for_each(toplevel, &server->toplevels, link) {
            if (toplevel->urgent && toplevel->urgent_order > last &&
                (!next || toplevel->urgent_order < next->urgent_order))
                next = toplevel;
        }
        if (!next)
            break;
        last = next->urgent_order;
        // The title as the taskbar has it (the shell finds the window by it), cut short at a
        // character boundary.
        char app_id[64], title[256];
        const char *raw_title = toplevel_title(next);
        copy_field(app_id, sizeof(app_id), toplevel_app_id(next));
        copy_field(title, sizeof(title), raw_title ? raw_title : "Untitled");
        drop_partial_utf8(title);
        append(state, size, &length, "urgent-window %s\t%d\t%s\t%s\n", next->output,
               next->workspace + 1, app_id, title);
    }
    // "keyboard-layout N COUNT SHORT NAME": the active keyboard layout (from 1) of how many,
    // its short name ("us") and its name ("English (US)").
    if (server->keymap) {
        char code[32], name[256];
        layout_short_name(server, server->keyboard_layout, code, sizeof(code));
        const char *full = xkb_keymap_layout_get_name(server->keymap, server->keyboard_layout);
        copy_field(name, sizeof(name), full);
        drop_partial_utf8(name);
        append(state, size, &length, "keyboard-layout %u %u %s %s\n", server->keyboard_layout + 1,
               xkb_keymap_num_layouts(server->keymap), code, name);
    }
    // "mode NAME": the binding mode in use, "default" outside any.
    append(state, size, &length, "mode %s\n", binding_mode(server));
    // "night-light ACTIVE MODE": whether the screen is warmed now ("on" or "off"), and whether
    // the schedule decides ("auto") or an override holds it "on" or "off".
    append(state, size, &length, "night-light %s %s\n",
           server->night_kelvin < SH_KELVIN_NEUTRAL ? "on" : "off",
           server->night_mode == SH_NIGHT_ON    ? "on"
           : server->night_mode == SH_NIGHT_OFF ? "off"
                                                : "auto");
    // "locked on|off": whether the session is locked, for the shell to record nothing then.
    append(state, size, &length, "locked %s\n", server->locked ? "on" : "off");
    // "power ACTIONS": the power actions that may run, as "lock,suspend,poweroff", or "-".
    char actions[128];
    power_available(server, actions, sizeof(actions));
    append(state, size, &length, "power %s\n", actions);
}

/* Subscribers get the state after each change, and "launcher OUTPUT" or "palette OUTPUT" when a
 * binding asks the shell for its application menu or command palette. */
void notify_subscribers(struct sh_server *server) {
    overview_touch(server, true); // a change of windows or workspaces, when it is open
    window_objects_changed(server);
    char state[sizeof(server->sent_state)];
    describe_state(server, state, sizeof(state));
    if (!strcmp(state, server->sent_state))
        return;
    strcpy(server->sent_state, state);
    send_event(server, state, strlen(state));
}

/* Sends every subscriber an event, such as a request for the shell. A subscriber that cannot keep
 * up is dropped rather than blocking the compositor. */
void send_event(struct sh_server *server, const char *text, size_t length) {
    struct sh_control_client *client, *temporary;
    wl_list_for_each_safe(client, temporary, &server->subscribers, link) {
        if (send(client->fd, text, length, MSG_NOSIGNAL | MSG_DONTWAIT) != (ssize_t)length)
            control_client_close(client);
    }
}

/* Sends the shell "WHAT OUTPUT". */
static void request_shell_on(struct sh_server *server, const char *what,
                             struct wlr_output *output) {
    if (!output)
        return;
    char line[128];
    int length = snprintf(line, sizeof(line), "%s %s\n", what, output->name);
    if (length < 0 || (size_t)length >= sizeof(line))
        return;
    send_event(server, line, (size_t)length);
}

/* Asks the shell to open something (`what`: "launcher", "palette", "notifications",
 * "power-menu", "clipboard", "emoji" or "display-settings") on the output under the pointer. */
void request_shell(struct sh_server *server, const char *what) {
    struct wlr_cursor *cursor = server->cursor;
    request_shell_on(server, what,
                     wlr_output_layout_output_at(server->output_layout, cursor->x, cursor->y));
}

void send_shell_line(struct sh_server *server, const char *line) {
    send_event(server, line, strlen(line));
}

/* Whether the desktop shell is subscribed ("subscribe shell"), to carry out what is asked of it
 * that another subscriber would only hear of, such as the volume keys. */
bool shell_listening(struct sh_server *server) {
    struct sh_control_client *client;
    wl_list_for_each(client, &server->subscribers, link) {
        if (client->shell)
            return true;
    }
    return false;
}

/* Tells the user what went wrong with something they no longer wait on: in the log, and across
 * the panel, which hears "EVENT TEXT" (such as "power-error Suspend failed: ..."). */
void report_failure(struct sh_server *server, const char *event, const char *text) {
    char line[384];
    int start = snprintf(line, sizeof(line), "%s ", event);
    if (start < 0 || (size_t)start + 2 > sizeof(line))
        return;
    copy_field(line + start, sizeof(line) - (size_t)start - 1, text); // room for "\n"
    drop_partial_utf8(line);
    line[start] = (char)toupper((unsigned char)line[start]);
    wlr_log(WLR_ERROR, "%s", line + start);
    strcat(line, "\n");
    send_shell_line(server, line);
}

/* "taskbar OUTPUT": the panel on the focused output, the one the keyboard was typing on, takes
 * the keyboard to walk its buttons, or gives it back when it has it. */
void request_taskbar(struct sh_server *server) {
    request_shell_on(server, "taskbar", focused_output(server));
}

static int control_client_readable(int fd, uint32_t mask, void *data) {
    struct sh_control_client *client = data;
    if (client->subscribed) {
        char ignored[64];
        ssize_t count = read(fd, ignored, sizeof(ignored));
        if (count == 0 || (count < 0 && errno != EAGAIN && errno != EINTR))
            control_client_close(client);
        return 0;
    }
    ssize_t count =
        read(fd, client->request + client->length, sizeof(client->request) - 1 - client->length);
    if (count < 0 && (errno == EAGAIN || errno == EINTR))
        return 0;
    if (count <= 0) {
        control_client_close(client);
        return 0;
    }
    client->length += (size_t)count;
    client->request[client->length] = '\0';
    char *newline = strchr(client->request, '\n');
    if (!newline && client->length < sizeof(client->request) - 1)
        return 0;
    if (newline)
        *newline = '\0';
    if (newline && (!strcmp(client->request, "subscribe") ||
                    !strcmp(client->request, "subscribe shell"))) {
        client->subscribed = true;
        client->shell = client->request[9] != '\0';
        wl_list_insert(&client->server->subscribers, &client->link);
        char state[3 + sizeof(client->server->sent_state)] = "ok\n";
        describe_state(client->server, state + 3, sizeof(state) - 3);
        size_t length = strlen(state);
        if (send(fd, state, length, MSG_NOSIGNAL | MSG_DONTWAIT) != (ssize_t)length)
            control_client_close(client);
        return 0;
    }
    // Replies are small; a blocking write keeps the protocol simple.
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);
    if (newline)
        control_handle(client->server, fd, client->request);
    else
        control_reply(fd, "error: request too long\n");
    control_client_close(client);
    return 0;
}

static int control_accept(int fd, uint32_t mask, void *data) {
    struct sh_server *server = data;
    int client_fd = accept4(fd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (client_fd < 0)
        return 0;
    struct sh_control_client *client = calloc(1, sizeof(*client));
    if (!client) {
        close(client_fd);
        return 0;
    }
    client->server = server;
    client->fd = client_fd;
    client->source = wl_event_loop_add_fd(wl_display_get_event_loop(server->wl_display), client_fd,
                                          WL_EVENT_READABLE, control_client_readable, client);
    if (!client->source) {
        close(client_fd);
        free(client);
    }
    return 0;
}

void open_control_socket(struct sh_server *server, const char *wayland_socket) {
    server->control_fd = -1;
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    if (!runtime || !*runtime ||
        snprintf(server->control_path, sizeof(server->control_path), "%s/paw.%s.sock", runtime,
                 wayland_socket) >= (int)sizeof(server->control_path) ||
        strlen(server->control_path) >= sizeof(address.sun_path)) {
        wlr_log(WLR_ERROR, "No usable XDG_RUNTIME_DIR; control socket disabled");
        server->control_path[0] = '\0';
        return;
    }
    strcpy(address.sun_path, server->control_path);
    unlink(server->control_path); // A stale socket from a crashed session.
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0 || bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0 || listen(fd, SOMAXCONN) < 0) {
        wlr_log_errno(WLR_ERROR, "Cannot create control socket %s", server->control_path);
        if (fd >= 0)
            close(fd);
        server->control_path[0] = '\0';
        return;
    }
    server->control_fd = fd;
    server->control_source = wl_event_loop_add_fd(wl_display_get_event_loop(server->wl_display), fd,
                                                  WL_EVENT_READABLE, control_accept, server);
    setenv("PAW_SOCKET", server->control_path, true);
    wlr_log(WLR_INFO, "Control socket: %s", server->control_path);
}

void close_control_socket(struct sh_server *server) {
    struct sh_control_client *client, *temporary;
    wl_list_for_each_safe(client, temporary, &server->subscribers, link) {
        control_client_close(client);
    }
    if (server->control_source)
        wl_event_source_remove(server->control_source);
    if (server->control_fd >= 0)
        close(server->control_fd);
    if (server->control_path[0])
        unlink(server->control_path);
}
