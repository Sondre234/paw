// SPDX-License-Identifier: GPL-3.0-or-later
/* Dragging the taskbar to another edge of the output, or one of its edges to resize it
 * (shell.panel_draggable). The shell, seeing a press on its bar's empty space move, asks with
 * "panel_drag OUTPUT" while the left button is held, or with "panel_drag resize EDGE" for a press
 * on one of the bar's edges, EDGE the output's edge the drag is told from; the compositor takes the
 * pointer as it does to move a window: no surface hears it until the button is let go. A surface
 * the pointer went on to would not know the button was held (wlroots forgets the pressed buttons
 * as the focus changes) and would never hear it let go, and a bar's edge dragged outwards leaves
 * the bar. Meanwhile the shell hears "panel-drag OUTPUT EDGE" whenever the edge of OUTPUT (the
 * bar's monitor; without one, the monitor the pointer is over) nearest the pointer changes: past
 * the monitor's edge, beside another, that is the edge it crossed. Then "panel-dropped OUTPUT
 * EDGE" as the button is let go; for a resize, "panel-resize OUTPUT DISTANCE" whenever the pointer's distance from EDGE of the output
 * the resize began on changes, in whole logical pixels, and "panel-resized OUTPUT DISTANCE" as the
 * button is let go. Either ends with "panel-drag-cancelled" after "panel_drag cancel" (Escape in
 * the shell) or a grab that ends otherwise (the session locking). The shell moves and sizes the
 * bar itself, writing the configuration. */
#include "server.h"

static const char *const edge_names[] = {"top", "bottom", "left", "right"};

/* The pointer's distances from the top, bottom, left and right edges of `box`, in edge_names'
 * order. */
static void distances(struct sh_server *server, const struct wlr_box *box, double away[4]) {
    const double x = server->cursor->x - box->x, y = server->cursor->y - box->y;
    away[0] = y, away[1] = box->height - y, away[2] = x, away[3] = box->width - x;
}

/* The edge of `output` nearest the pointer, as an index into edge_names; beyond an edge, the
 * pointer is nearest it (a negative distance). */
static int nearest_edge(struct sh_server *server, struct wlr_output *output) {
    struct wlr_box box;
    wlr_output_layout_get_box(server->output_layout, output, &box);
    double away[4];
    distances(server, &box, away);
    int nearest = 1;
    for (int i = 0; i < 4; ++i)
        if (away[i] < away[nearest])
            nearest = i;
    return nearest;
}

/* Tells the shell `event` with the bar's output (the one under the pointer while the drag names
 * none, or the one it named is gone) and the edge of it nearest the pointer: always, or only when
 * either changed since it was told last. */
static void report_edge(struct sh_server *server, const char *event, bool always) {
    struct wlr_output *output =
        server->panel_drag.bar[0] ? find_output(server, server->panel_drag.bar) : NULL;
    if (!output)
        output = pointer_output(server);
    if (!output)
        return;
    const int edge = nearest_edge(server, output);
    if (!always && edge == server->panel_drag.edge &&
        !strcmp(output->name, server->panel_drag.output))
        return;
    server->panel_drag.edge = edge;
    snprintf(server->panel_drag.output, sizeof(server->panel_drag.output), "%s", output->name);
    char line[128];
    snprintf(line, sizeof(line), "%s %s %s\n", event, output->name, edge_names[edge]);
    send_shell_line(server, line);
}

/* Tells the shell `event` with the output a resize began on and the pointer's distance from the
 * bar's edge of it (none past it): always, or only when it changed since it was told last. */
static void report_distance(struct sh_server *server, const char *event, bool always) {
    double away[4];
    distances(server, &server->panel_drag.box, away);
    const double along = away[server->panel_drag.resize];
    const int distance = along > 0 ? (int)lround(along) : 0;
    if (!always && distance == server->panel_drag.distance)
        return;
    server->panel_drag.distance = distance;
    char line[128];
    snprintf(line, sizeof(line), "%s %s %d\n", event, server->panel_drag.output, distance);
    send_shell_line(server, line);
}

/* Takes the pointer while the left button is held and nothing else has it: to move the bar of
 * `bar` ("" for whichever output the pointer is over), or with `resize` (an index into
 * edge_names, else -1) to resize the bar along that edge of the output under the pointer. */
static bool panel_drag_begin(struct sh_server *server, int resize, const char *bar) {
    struct wlr_output *output = pointer_output(server);
    if (server->cursor_mode != SH_CURSOR_PASSTHROUGH || server->grab_button ||
        server->overview.open || !(server->held_buttons & 1u << (BTN_LEFT - BTN_MOUSE)) ||
        (resize >= 0 && !output))
        return false;
    server->cursor_mode = SH_CURSOR_PANEL;
    // Let go of as the left button is (server_cursor_button), which no client hears.
    server->grab_button = BTN_LEFT;
    server->panel_drag.resize = resize;
    snprintf(server->panel_drag.bar, sizeof(server->panel_drag.bar), "%s", bar);
    wlr_seat_pointer_notify_clear_focus(server->seat);
    if (resize < 0) {
        show_xcursor(server, "grabbing");
        report_edge(server, "panel-drag", true);
        return true;
    }
    wlr_output_layout_get_box(server->output_layout, output, &server->panel_drag.box);
    snprintf(server->panel_drag.output, sizeof(server->panel_drag.output), "%s", output->name);
    show_xcursor(server, resize < 2 ? "ns-resize" : "ew-resize");
    report_distance(server, "panel-resize", true);
    return true;
}

void panel_drag_motion(struct sh_server *server) {
    if (server->panel_drag.resize >= 0)
        report_distance(server, "panel-resize", false);
    else
        report_edge(server, "panel-drag", false);
}

bool panel_drag_drop(struct sh_server *server) {
    if (server->cursor_mode != SH_CURSOR_PANEL)
        return false;
    if (server->panel_drag.resize >= 0)
        report_distance(server, "panel-resized", true);
    else
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

/* "panel_drag [OUTPUT]" takes the pointer for a drag of OUTPUT's taskbar, while the left button
 * is held; "panel_drag resize EDGE" for a drag of one of its edges, told from EDGE of the output
 * under the pointer; "panel_drag cancel" ends either, leaving the bar as it is. */
void control_panel_drag(struct sh_server *server, int fd, const char *arguments) {
    const char *verb = arguments + (*arguments == ' ');
    int resize = -1;
    if (!strncmp(verb, "resize ", 7)) {
        for (int i = 0; i < 4; ++i)
            if (!strcmp(verb + 7, edge_names[i]))
                resize = i;
    }
    if (!strcmp(verb, "cancel")) {
        panel_drag_cancel(server);
        control_reply(fd, "ok\n");
    } else if (resize < 0 && *verb && (strchr(verb, ' ') || !find_output(server, verb))) {
        control_reply(fd, "error: usage: panel_drag [OUTPUT | resize top|bottom|left|right | "
                          "cancel], OUTPUT a monitor's name\n");
    } else {
        control_reply(fd, panel_drag_begin(server, resize, resize < 0 ? verb : "")
                              ? "ok\n"
                              : "error: the pointer is busy, or its left button is not held\n");
    }
}
