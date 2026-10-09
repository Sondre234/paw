// SPDX-License-Identifier: GPL-3.0-or-later
/* X11 client exercising XWayland mapping, focus, fullscreen, and close. */
#include <poll.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <xcb/xcb.h>

struct probe {
    xcb_connection_t *connection;
    xcb_window_t root, window;
    xcb_atom_t protocols, delete_window, state, fullscreen, attention, hints, icon;
    int width, height;
    bool mapped, closed;
};

static void die(const char *message) {
    fprintf(stderr, "x11 probe: %s\n", message);
    exit(1);
}

static xcb_atom_t atom(xcb_connection_t *connection, const char *name) {
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(
        connection, xcb_intern_atom(connection, false, strlen(name), name), NULL);
    if (!reply)
        die("cannot intern atom");
    xcb_atom_t result = reply->atom;
    free(reply);
    return result;
}

static double now(void) {
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return time.tv_sec + time.tv_nsec / 1e9;
}

static void handle(struct probe *probe, xcb_generic_event_t *event) {
    switch (event->response_type & ~0x80) {
    case XCB_MAP_NOTIFY:
        probe->mapped = true;
        break;
    case XCB_CONFIGURE_NOTIFY: {
        xcb_configure_notify_event_t *configure = (xcb_configure_notify_event_t *)event;
        if (configure->window == probe->window) {
            probe->width = configure->width;
            probe->height = configure->height;
        }
        break;
    }
    case XCB_KEY_PRESS:
    case XCB_KEY_RELEASE: {
        // Only while it grabs the keyboard: the window selects no key events.
        xcb_key_press_event_t *key = (xcb_key_press_event_t *)event;
        printf("key %u %s\n", key->detail - 8u,
               (event->response_type & ~0x80) == XCB_KEY_PRESS ? "pressed" : "released");
        fflush(stdout);
        break;
    }
    case XCB_CLIENT_MESSAGE: {
        xcb_client_message_event_t *message = (xcb_client_message_event_t *)event;
        if (message->type == probe->protocols && message->data.data32[0] == probe->delete_window)
            probe->closed = true;
        break;
    }
    }
}

/* Dispatch events until the condition holds, failing after a few seconds. */
#define WAIT_FOR(probe, condition, message)                                                        \
    do {                                                                                           \
        double deadline = now() + 5;                                                               \
        while (!(condition)) {                                                                     \
            xcb_generic_event_t *event;                                                            \
            while (!(condition) && (event = xcb_poll_for_event((probe)->connection))) {            \
                handle(probe, event);                                                              \
                free(event);                                                                       \
            }                                                                                      \
            if (condition)                                                                         \
                break;                                                                             \
            if (xcb_connection_has_error((probe)->connection))                                     \
                die("X connection failed while waiting: " message);                                \
            if (now() > deadline)                                                                  \
                die("timed out: " message);                                                        \
            struct pollfd fd = {xcb_get_file_descriptor((probe)->connection), POLLIN, 0};          \
            poll(&fd, 1, 50);                                                                      \
        }                                                                                          \
    } while (0)

/* Sets _NET_WM_ICON from SPEC's words, each "SIZE:AARRGGBB" or "WIDTHxHEIGHT:AARRGGBB": an
 * image of that size in that colour, its alpha beside a colour not premultiplied by it, as X11
 * gives them. */
static void set_icon(struct probe *probe, const char *spec) {
    char words[256];
    snprintf(words, sizeof(words), "%s", spec);
    uint32_t *data = NULL;
    size_t length = 0;
    for (char *word = strtok(words, " "); word; word = strtok(NULL, " ")) {
        unsigned width, height, colour;
        if (sscanf(word, "%ux%u:%x", &width, &height, &colour) != 3) {
            if (sscanf(word, "%u:%x", &width, &colour) != 2)
                die("an icon takes words \"SIZE:AARRGGBB\" or \"WIDTHxHEIGHT:AARRGGBB\"");
            height = width;
        }
        if (!width || !height || width > 512 || height > 512)
            die("an icon's image is 1 to 512 pixels wide and tall");
        data = realloc(data, (length + 2 + (size_t)width * height) * sizeof(*data));
        if (!data)
            die("out of memory");
        data[length++] = width;
        data[length++] = height;
        for (size_t i = 0; i < (size_t)width * height; ++i)
            data[length++] = colour;
    }
    xcb_change_property(probe->connection, XCB_PROP_MODE_REPLACE, probe->window, probe->icon,
                        XCB_ATOM_CARDINAL, 32, (uint32_t)length, data);
    free(data);
}

static void request_fullscreen(struct probe *probe, bool enable) {
    xcb_client_message_event_t message = {
        .response_type = XCB_CLIENT_MESSAGE,
        .format = 32,
        .window = probe->window,
        .type = probe->state,
        .data.data32 = {enable ? 1 : 0, probe->fullscreen, 0, 1, 0}};
    xcb_send_event(probe->connection, false, probe->root,
                   XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
                   (const char *)&message);
    xcb_flush(probe->connection);
}

int main(int argc, char **argv) {
    const char *command = argc == 2 ? argv[1] : "full";
    struct probe probe = {0};
    probe.connection = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(probe.connection))
        die("cannot connect to X server");
    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(probe.connection)).data;
    probe.root = screen->root;
    probe.protocols = atom(probe.connection, "WM_PROTOCOLS");
    probe.delete_window = atom(probe.connection, "WM_DELETE_WINDOW");
    probe.state = atom(probe.connection, "_NET_WM_STATE");
    probe.fullscreen = atom(probe.connection, "_NET_WM_STATE_FULLSCREEN");
    probe.attention = atom(probe.connection, "_NET_WM_STATE_DEMANDS_ATTENTION");
    probe.hints = atom(probe.connection, "WM_HINTS");
    probe.icon = atom(probe.connection, "_NET_WM_ICON");

    probe.window = xcb_generate_id(probe.connection);
    uint32_t values[] = {screen->white_pixel, XCB_EVENT_MASK_STRUCTURE_NOTIFY};
    xcb_create_window(probe.connection, XCB_COPY_FROM_PARENT, probe.window, probe.root, 0, 0, 300,
                      200, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
                      XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, values);
    // Tests telling several windows apart, or matching a window rule, name them.
    const char *env_title = getenv("PAW_PROBE_TITLE");
    const char *title = env_title && *env_title ? env_title : "paw X11 probe";
    const char class[] = "paw-x11-probe\0paw-x11-probe";
    xcb_change_property(probe.connection, XCB_PROP_MODE_REPLACE, probe.window, XCB_ATOM_WM_NAME,
                        XCB_ATOM_STRING, 8, strlen(title), title);
    xcb_change_property(probe.connection, XCB_PROP_MODE_REPLACE, probe.window, XCB_ATOM_WM_CLASS,
                        XCB_ATOM_STRING, 8, sizeof(class), class);
    xcb_change_property(probe.connection, XCB_PROP_MODE_REPLACE, probe.window, probe.protocols,
                        XCB_ATOM_ATOM, 32, 1, &probe.delete_window);
    // Real toolkits say which process owns a window; the compositor reads it for swallowing.
    uint32_t pid = (uint32_t)getpid();
    xcb_change_property(probe.connection, XCB_PROP_MODE_REPLACE, probe.window,
                        atom(probe.connection, "_NET_WM_PID"), XCB_ATOM_CARDINAL, 32, 1, &pid);
    if (getenv("PAW_PROBE_URGENT_ON_MAP")) {
        // Asks for attention before mapping, as an application started in the background does.
        uint32_t hints[9] = {256};
        xcb_change_property(probe.connection, XCB_PROP_MODE_REPLACE, probe.window, probe.hints,
                            probe.hints, 32, 9, hints);
    }
    // PAW_PROBE_ICON=SPEC gives the window an icon before it maps (see set_icon).
    if (getenv("PAW_PROBE_ICON"))
        set_icon(&probe, getenv("PAW_PROBE_ICON"));
    xcb_map_window(probe.connection, probe.window);
    xcb_flush(probe.connection);
    WAIT_FOR(&probe, probe.mapped, "window mapping");

    // The compositor gives newly mapped windows keyboard focus, unless a rule says otherwise
    // (the window that asked for attention before mapping is opened that way).
    double deadline = now() + 5;
    for (; !getenv("PAW_PROBE_URGENT_ON_MAP");) {
        xcb_get_input_focus_reply_t *focus = xcb_get_input_focus_reply(
            probe.connection, xcb_get_input_focus(probe.connection), NULL);
        bool focused = focus && focus->focus == probe.window;
        free(focus);
        if (focused)
            break;
        if (now() > deadline)
            die("mapped window did not receive input focus");
        struct timespec pause = {0, 20 * 1000 * 1000};
        nanosleep(&pause, NULL);
    }
    puts(getenv("PAW_PROBE_URGENT_ON_MAP") ? "X11 window mapped and focused (not waited for)"
                                              : "X11 window mapped and focused");

    if (!strcmp(command, "wait-close")) {
        // The harness closes this window through the compositor.
        puts("waiting for close");
        fflush(stdout);
        WAIT_FOR(&probe, probe.closed, "compositor close request");
        puts("X11 close request received");
        xcb_disconnect(probe.connection);
        return 0;
    }

    if (!strcmp(command, "commands")) {
        // Lines on standard input ask for attention the two ways X11 clients do: "demand" and
        // "undemand" add and remove _NET_WM_STATE_DEMANDS_ATTENTION, "hint" and "unhint" set
        // and clear the urgency flag of WM_HINTS. "icon SPEC" sets _NET_WM_ICON (see set_icon)
        // and "unicon" deletes it. "grab" takes an active grab of the keyboard, as a virtual
        // machine's window does, saying "grab taken" (or "grab refused"), after which the keys
        // print as "key CODE pressed|released" (evdev's codes); "ungrab" ends it. Closing the
        // window ends it.
        puts("waiting for commands");
        fflush(stdout);
        char pending[128];
        size_t length = 0;
        bool input = true;
        while (!probe.closed) {
            xcb_generic_event_t *event;
            while ((event = xcb_poll_for_event(probe.connection))) {
                handle(&probe, event);
                free(event);
            }
            if (xcb_connection_has_error(probe.connection))
                die("X connection failed");
            struct pollfd fds[2] = {{xcb_get_file_descriptor(probe.connection), POLLIN, 0},
                                    {input ? 0 : -1, POLLIN, 0}};
            poll(fds, 2, 50);
            if (!(fds[1].revents & (POLLIN | POLLHUP)))
                continue;
            ssize_t count = read(0, pending + length, sizeof(pending) - 1 - length);
            if (count <= 0) {
                input = false;
                continue;
            }
            length += (size_t)count;
            pending[length] = '\0';
            char *newline;
            while ((newline = strchr(pending, '\n'))) {
                *newline = '\0';
                if (!strcmp(pending, "demand") || !strcmp(pending, "undemand")) {
                    xcb_client_message_event_t message = {
                        .response_type = XCB_CLIENT_MESSAGE,
                        .format = 32,
                        .window = probe.window,
                        .type = probe.state,
                        .data.data32 = {pending[0] == 'd' ? 1 : 0, probe.attention, 0, 1, 0}};
                    xcb_send_event(probe.connection, false, probe.root,
                                   XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
                                       XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT,
                                   (const char *)&message);
                } else if (!strcmp(pending, "hint") || !strcmp(pending, "unhint")) {
                    // flags, input, initial state, icon pixmap, icon window, icon x, icon y,
                    // icon mask, window group; bit 8 of the flags is urgency.
                    uint32_t hints[9] = {pending[0] == 'h' ? 256 : 0};
                    xcb_change_property(probe.connection, XCB_PROP_MODE_REPLACE, probe.window,
                                        probe.hints, probe.hints, 32, 9, hints);
                } else if (!strncmp(pending, "icon ", 5)) {
                    set_icon(&probe, pending + 5);
                } else if (!strcmp(pending, "unicon")) {
                    xcb_delete_property(probe.connection, probe.window, probe.icon);
                } else if (!strcmp(pending, "grab")) {
                    xcb_grab_keyboard_reply_t *grab = xcb_grab_keyboard_reply(
                        probe.connection,
                        xcb_grab_keyboard(probe.connection, true, probe.window, XCB_CURRENT_TIME,
                                          XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC),
                        NULL);
                    printf("grab %s\n",
                           grab && grab->status == XCB_GRAB_STATUS_SUCCESS ? "taken" : "refused");
                    fflush(stdout);
                    free(grab);
                } else if (!strcmp(pending, "ungrab")) {
                    xcb_ungrab_keyboard(probe.connection, XCB_CURRENT_TIME);
                } else
                    die("unknown command");
                xcb_flush(probe.connection);
                length -= (size_t)(newline + 1 - pending);
                memmove(pending, newline + 1, length + 1);
            }
        }
        puts("X11 close request received");
        xcb_disconnect(probe.connection);
        return 0;
    }

    xcb_get_geometry_reply_t *root = xcb_get_geometry_reply(
        probe.connection, xcb_get_geometry(probe.connection, probe.root), NULL);
    if (!root)
        die("cannot query root geometry");
    int screen_width = root->width, screen_height = root->height;
    free(root);

    request_fullscreen(&probe, true);
    WAIT_FOR(&probe, probe.width == screen_width && probe.height == screen_height,
             "fullscreen configure");
    puts("X11 fullscreen covered the screen");
    request_fullscreen(&probe, false);
    WAIT_FOR(&probe, probe.width == 300 && probe.height == 200, "fullscreen exit");
    puts("X11 fullscreen exit restored size");

    xcb_destroy_window(probe.connection, probe.window);
    xcb_flush(probe.connection);
    xcb_disconnect(probe.connection);
    return 0;
}
