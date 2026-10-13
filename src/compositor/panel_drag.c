// SPDX-License-Identifier: GPL-3.0-or-later
/* Dragging the taskbar to another edge of the output (shell.panel_draggable). The shell, seeing a
 * press on its bar's empty space move, asks with "panel_drag" while the left button is held, and
 * the compositor takes the pointer as it does to move a window: no surface hears it until the
 * button is let go. A surface the pointer went on to would not know the button was held (wlroots
 * forgets the pressed buttons as the focus changes) and would never hear it let go. Meanwhile the
 * shell hears "panel-drag OUTPUT EDGE" whenever the edge nearest the pointer, on the output it is
 * over, changes; "panel-dropped OUTPUT EDGE" as the button is let go; or "panel-drag-cancelled"
 * after "panel_drag cancel" (Escape in the shell) or a grab that ends otherwise (the session
 * locking). The shell moves the bar itself, writing the configuration. */
#include "server.h"

static const char *const edge_names[] = {"top", "bottom", "left", "right"};

/* The edge of `output` nearest the pointer, as an index into edge_names. */
static int nearest_edge(struct sh_server *server, struct wlr_output *output) {
    struct wlr_box box;
    wlr_output_layout_get_box(server->output_layout, output, &box);
    const double x = server->cursor->x - box.x, y = server->cursor->y - box.y;
    const double away[] = {y, box.height - y, x, box.width - x};
    int nearest = 1;
    for (int i = 0; i < 4; ++i)
        if (away[i] < away[nearest])
            nearest = i;
    return nearest;
}

/* Tells the shell `event` with the output under the pointer and the edge nearest it: always, or
 * only when either changed since it was told last. */
static void report_edge(struct sh_server *server, const char *event, bool always) {
    struct wlr_output *output = pointer_output(server);
    if (!output)
        return;
    const int edge = nearest_edge(server, output);
    if (!always && edge == server->panel_drag.edge && !strcmp(output->name, server->panel_drag.output))
        return;
    server->panel_drag.edge = edge;
    snprintf(server->panel_drag.output, sizeof(server->panel_drag.output), "%s", output->name);
    char line[128];
    snprintf(line, sizeof(line), "%s %s %s\n", event, output->name, edge_names[edge]);
    send_shell_line(server, line);
}

/* Takes the pointer while the left button is held and nothing else has it. */
static bool panel_drag_begin(struct sh_server *server) {
    if (server->cursor_mode != SH_CURSOR_PASSTHROUGH || server->grab_button ||
        server->overview.open || !(server->held_buttons & 1u << (BTN_LEFT - BTN_MOUSE)))
        return false;
    server->cursor_mode = SH_CURSOR_PANEL;
    // Let go of as the left button is (server_cursor_button), which no client hears.
    server->grab_button = BTN_LEFT;
    wlr_seat_pointer_notify_clear_focus(server->seat);
    show_xcursor(server, "grabbing");
    report_edge(server, "panel-drag", true);
    return true;
}

void panel_drag_motion(struct sh_server *server) {
    report_edge(server, "panel-drag", false);
}

bool panel_drag_drop(struct sh_server *server) {
    if (server->cursor_mode != SH_CURSOR_PANEL)
        return false;
    report_edge(server, "panel-dropped", true);
    server->cursor_mode = SH_CURSOR_PASSTHROUGH;
    return true;
}

void panel_drag_cancel(struct sh_server *server) {
    if (server->cursor_mode != SH_CURSOR_PANEL)
        return;
    // The button still held is let go of unheard, as it would have been.
    server->cursor_mode = SH_CURSOR_PASSTHROUGH;
    send_shell_line(server, "panel-drag-cancelled\n");
}

/* "panel_drag" takes the pointer for a drag of the taskbar, while the left button is held;
 * "panel_drag cancel" ends one, leaving the bar where it is. */
void control_panel_drag(struct sh_server *server, int fd, const char *arguments) {
    const char *verb = arguments + (*arguments == ' ');
    if (!*verb) {
        control_reply(fd, panel_drag_begin(server)
                              ? "ok\n"
                              : "error: the pointer is busy, or its left button is not held\n");
    } else if (!strcmp(verb, "cancel")) {
        panel_drag_cancel(server);
        control_reply(fd, "ok\n");
    } else {
        control_reply(fd, "error: usage: panel_drag [cancel]\n");
    }
}
