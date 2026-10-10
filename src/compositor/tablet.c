/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* Drawing tablets (tablet-v2): a tool's position, pressure, tilt, distance, rotation, slider,
 * wheel, tip and buttons go to the surface under it, for Krita's and GIMP's pressure. Over a
 * surface that takes no tablet input, the compositor's controls or the desktop, the tool is the
 * pointer instead, its tip the left button and its buttons the right and middle ones. A tablet's
 * area is mapped to tablet.output, else to every monitor. Pads' buttons, rings and strips go to
 * the surface that has the keyboard. */
#include "server.h"

struct sh_tablet {
    struct wl_list link; // sh_server.tablet.tablets
    struct sh_server *server;
    struct wlr_tablet *wlr_tablet;
    struct wlr_tablet_v2_tablet *tablet_v2;
    struct wl_listener destroy;
};

struct sh_tablet_tool {
    struct wl_list link; // sh_server.tablet.tools
    struct sh_server *server;
    struct wlr_tablet_tool *wlr_tool;
    struct wlr_tablet_v2_tablet_tool *tool_v2;
    bool near;         /* in proximity of its tablet */
    bool pointer;      /* standing in for the pointer, over what takes no tablet input */
    uint32_t held[4];  /* the pointer's buttons its tip and buttons hold, 0 for none */
    /* Where the surface its tip is down on was in the layout, which keeps it while it is down. */
    double origin_x, origin_y;
    double tilt_x, tilt_y;
    struct wl_listener set_cursor, destroy;
};

struct sh_tablet_pad {
    struct wl_list link; // sh_server.tablet.pads
    struct sh_server *server;
    struct wlr_tablet_pad *wlr_pad;
    struct wlr_tablet_v2_tablet_pad *pad_v2;
    struct sh_tablet *tablet; // the tablet it belongs to, as far as it is known
    struct wl_listener button, ring, strip, attach, destroy;
};

/* The output tablets are mapped to, or NULL for the whole layout. */
static struct wlr_output *tablet_output(struct sh_server *server) {
    return find_output_key(server, server_settings(server)->tablet_output);
}

/* Maps every tablet again: the outputs or tablet.output changed. */
void map_tablets(struct sh_server *server) {
    struct sh_tablet *tablet;
    wl_list_for_each(tablet, &server->tablet.tablets, link)
        wlr_cursor_map_input_to_output(server->cursor, &tablet->wlr_tablet->base,
                                       tablet_output(server));
}

/* The tool presses or lets go of a pointer button, keeping count of those it holds. */
static void pointer_button(struct sh_tablet_tool *tool, uint32_t time, uint32_t button,
                           enum wl_pointer_button_state state) {
    for (int i = 0; i < 4; ++i) {
        if (state == WL_POINTER_BUTTON_STATE_PRESSED ? !tool->held[i] : tool->held[i] == button) {
            tool->held[i] = state == WL_POINTER_BUTTON_STATE_PRESSED ? button : 0;
            break;
        }
    }
    struct wlr_pointer_button_event event = {.time_msec = time, .button = button, .state = state};
    server_cursor_button(&tool->server->cursor_button, &event);
    wlr_seat_pointer_notify_frame(tool->server->seat);
}

static bool holds_pointer(const struct sh_tablet_tool *tool) {
    for (int i = 0; i < 4; ++i) {
        if (tool->held[i])
            return true;
    }
    return false;
}

static void release_pointer(struct sh_tablet_tool *tool, uint32_t time) {
    for (int i = 0; i < 4; ++i) {
        if (tool->held[i])
            pointer_button(tool, time, tool->held[i], WL_POINTER_BUTTON_STATE_RELEASED);
    }
}

static void tool_set_cursor(struct wl_listener *listener, void *data) {
    struct sh_tablet_tool *tool = wl_container_of(listener, tool, set_cursor);
    struct wlr_tablet_v2_event_cursor *event = data;
    struct wlr_surface *focus = tool->tool_v2->focused_surface;
    // Only the client the tool is over draws its cursor.
    if (tool->pointer || !focus || !event->seat_client ||
        event->seat_client->client != wl_resource_get_client(focus->resource))
        return;
    wlr_cursor_set_surface(tool->server->cursor, event->surface, event->hotspot_x,
                           event->hotspot_y);
}

static void tool_destroy(struct wl_listener *listener, void *data) {
    struct sh_tablet_tool *tool = wl_container_of(listener, tool, destroy);
    release_pointer(tool, (uint32_t)now_ms());
    wl_list_remove(&tool->set_cursor.link);
    wl_list_remove(&tool->destroy.link);
    wl_list_remove(&tool->link);
    tool->wlr_tool->data = NULL;
    free(tool);
}

/* Our tool for a libinput tool, made the first time it comes near a tablet. */
static struct sh_tablet_tool *find_tool(struct sh_server *server, struct wlr_tablet_tool *wlr_tool) {
    if (wlr_tool->data)
        return wlr_tool->data;
    struct sh_tablet_tool *tool = calloc(1, sizeof(*tool));
    if (!tool)
        return NULL;
    tool->tool_v2 = wlr_tablet_tool_create(server->tablet.manager, server->seat, wlr_tool);
    if (!tool->tool_v2) {
        free(tool);
        return NULL;
    }
    tool->server = server;
    tool->wlr_tool = wlr_tool;
    wlr_tool->data = tool;
    add_listener(&tool->tool_v2->events.set_cursor, &tool->set_cursor, tool_set_cursor);
    add_listener(&wlr_tool->events.destroy, &tool->destroy, tool_destroy);
    wl_list_insert(server->tablet.tools.prev, &tool->link);
    return tool;
}

/* The tool is at (x, y) of its tablet's area, from 0 to 1: the cursor follows it, and what is
 * under it gets it, as a tablet tool or as the pointer. */
static void tool_moved(struct sh_server *server, struct sh_tablet *tablet,
                       struct sh_tablet_tool *tool, uint32_t time, double x, double y) {
    struct wlr_cursor *cursor = server->cursor;
    wlr_cursor_warp_absolute(cursor, &tablet->wlr_tablet->base, x, y);
    struct wlr_tablet_v2_tablet_tool *tool_v2 = tool->tool_v2;
    if (!tool->pointer && tool_v2->is_down) {
        // The surface its tip went down on keeps it, wherever it goes.
        wlr_tablet_v2_tablet_tool_notify_motion(tool_v2, cursor->x - tool->origin_x,
                                                cursor->y - tool->origin_y);
        return;
    }
    if (!holds_pointer(tool)) {
        double sx = 0, sy = 0;
        struct sh_node *owner = NULL;
        struct wlr_surface *surface =
            server->overview.open ? NULL
                                  : press_target_at(server, cursor->x, cursor->y, &sx, &sy, &owner);
        if (surface && wlr_surface_accepts_tablet_v2(surface, tablet->tablet_v2)) {
            if (tool->pointer)
                wlr_seat_pointer_notify_clear_focus(server->seat); // the tool's now, not the pointer's
            tool->pointer = false;
            wlr_tablet_v2_tablet_tool_notify_proximity_in(tool_v2, tablet->tablet_v2, surface);
            wlr_tablet_v2_tablet_tool_notify_motion(tool_v2, sx, sy);
            tool->origin_x = cursor->x - sx;
            tool->origin_y = cursor->y - sy;
            return;
        }
        if (tool_v2->focused_surface)
            wlr_tablet_v2_tablet_tool_notify_proximity_out(tool_v2);
        tool->pointer = true;
    }
    process_cursor_motion(server, time);
    wlr_seat_pointer_notify_frame(server->seat);
}

/* A tip touching a window or a panel focuses it, as a click does. */
static void tool_focus(struct sh_server *server) {
    double sx, sy, x = server->cursor->x, y = server->cursor->y;
    struct sh_node *owner = NULL;
    press_target_at(server, x, y, &sx, &sy, &owner);
    focus_pressed(server, x, y, owner);
}

static void tool_proximity(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, tablet.tool_proximity);
    struct wlr_tablet_tool_proximity_event *event = data;
    struct sh_tablet *tablet = event->tablet->data;
    struct sh_tablet_tool *tool = find_tool(server, event->tool);
    if (!tablet || !tool)
        return;
    wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
    if (event->state == WLR_TABLET_TOOL_PROXIMITY_OUT) {
        release_pointer(tool, event->time_msec);
        wlr_tablet_v2_tablet_tool_notify_proximity_out(tool->tool_v2);
        tool->near = tool->pointer = false;
        return;
    }
    tool->near = true;
    tool_moved(server, tablet, tool, event->time_msec, event->x, event->y);
}

static void tool_axis(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, tablet.tool_axis);
    struct wlr_tablet_tool_axis_event *event = data;
    struct sh_tablet *tablet = event->tablet->data;
    struct sh_tablet_tool *tool = find_tool(server, event->tool);
    if (!tablet || !tool)
        return;
    wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
    uint32_t axes = event->updated_axes;
    if (axes & (WLR_TABLET_TOOL_AXIS_X | WLR_TABLET_TOOL_AXIS_Y))
        tool_moved(server, tablet, tool, event->time_msec, event->x, event->y);
    if (axes & WLR_TABLET_TOOL_AXIS_TILT_X)
        tool->tilt_x = event->tilt_x;
    if (axes & WLR_TABLET_TOOL_AXIS_TILT_Y)
        tool->tilt_y = event->tilt_y;
    if (tool->pointer || !tool->tool_v2->focused_surface)
        return; // the pointer has no pressure
    struct wlr_tablet_v2_tablet_tool *tool_v2 = tool->tool_v2;
    if (axes & WLR_TABLET_TOOL_AXIS_PRESSURE)
        wlr_tablet_v2_tablet_tool_notify_pressure(tool_v2, event->pressure);
    if (axes & WLR_TABLET_TOOL_AXIS_DISTANCE)
        wlr_tablet_v2_tablet_tool_notify_distance(tool_v2, event->distance);
    if (axes & (WLR_TABLET_TOOL_AXIS_TILT_X | WLR_TABLET_TOOL_AXIS_TILT_Y))
        wlr_tablet_v2_tablet_tool_notify_tilt(tool_v2, tool->tilt_x, tool->tilt_y);
    if (axes & WLR_TABLET_TOOL_AXIS_ROTATION)
        wlr_tablet_v2_tablet_tool_notify_rotation(tool_v2, event->rotation);
    if (axes & WLR_TABLET_TOOL_AXIS_SLIDER)
        wlr_tablet_v2_tablet_tool_notify_slider(tool_v2, event->slider);
    if (axes & WLR_TABLET_TOOL_AXIS_WHEEL)
        wlr_tablet_v2_tablet_tool_notify_wheel(tool_v2, event->wheel_delta, 0);
}

static void tool_tip(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, tablet.tool_tip);
    struct wlr_tablet_tool_tip_event *event = data;
    struct sh_tablet *tablet = event->tablet->data;
    struct sh_tablet_tool *tool = find_tool(server, event->tool);
    if (!tablet || !tool)
        return;
    wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
    struct wlr_tablet_v2_tablet_tool *tool_v2 = tool->tool_v2;
    if (event->state == WLR_TABLET_TOOL_TIP_DOWN) {
        tool_moved(server, tablet, tool, event->time_msec, event->x, event->y);
        if (!tool->pointer && tool_v2->focused_surface) {
            tool_focus(server);
            wlr_tablet_v2_tablet_tool_notify_down(tool_v2);
            wlr_tablet_tool_v2_start_implicit_grab(tool_v2);
        } else {
            pointer_button(tool, event->time_msec, BTN_LEFT, WL_POINTER_BUTTON_STATE_PRESSED);
        }
        return;
    }
    bool held = false;
    for (int i = 0; i < 4; ++i)
        held = held || tool->held[i] == BTN_LEFT;
    if (held)
        pointer_button(tool, event->time_msec, BTN_LEFT, WL_POINTER_BUTTON_STATE_RELEASED);
    else
        wlr_tablet_v2_tablet_tool_notify_up(tool_v2);
    // Lifted, the tool goes to what is under it again.
    tool_moved(server, tablet, tool, event->time_msec, event->x, event->y);
}

static void tool_button(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, tablet.tool_button);
    struct wlr_tablet_tool_button_event *event = data;
    struct sh_tablet_tool *tool = find_tool(server, event->tool);
    if (!tool)
        return;
    wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
    if (!tool->pointer && tool->tool_v2->focused_surface) {
        wlr_tablet_v2_tablet_tool_notify_button(tool->tool_v2, event->button,
                                                (enum zwp_tablet_pad_v2_button_state)event->state);
        return;
    }
    // As the pointer, a pen's lower button is the right one and its upper the middle one.
    uint32_t button = event->button == BTN_STYLUS    ? BTN_RIGHT
                      : event->button == BTN_STYLUS2 ? BTN_MIDDLE
                                                     : event->button;
    pointer_button(tool, event->time_msec, button,
                   event->state == WLR_BUTTON_PRESSED ? WL_POINTER_BUTTON_STATE_PRESSED
                                                      : WL_POINTER_BUTTON_STATE_RELEASED);
}

static void tablet_destroy(struct wl_listener *listener, void *data) {
    struct sh_tablet *tablet = wl_container_of(listener, tablet, destroy);
    struct sh_tablet_pad *pad;
    wl_list_for_each(pad, &tablet->server->tablet.pads, link) {
        if (pad->tablet == tablet)
            pad->tablet = NULL;
    }
    wl_list_remove(&tablet->destroy.link);
    wl_list_remove(&tablet->link);
    free(tablet);
}

/* The pad of `tablet` gives its buttons to the surface with the keyboard. */
static void pad_enter(struct sh_tablet_pad *pad, struct wlr_surface *surface) {
    if (pad->tablet && surface && wlr_surface_accepts_tablet_v2(surface, pad->tablet->tablet_v2))
        wlr_tablet_v2_tablet_pad_notify_enter(pad->pad_v2, pad->tablet->tablet_v2, surface);
}

void server_new_tablet(struct sh_server *server, struct wlr_input_device *device) {
    struct sh_tablet *tablet = calloc(1, sizeof(*tablet));
    if (!tablet)
        return;
    tablet->server = server;
    tablet->wlr_tablet = wlr_tablet_from_input_device(device);
    tablet->tablet_v2 = wlr_tablet_create(server->tablet.manager, server->seat, device);
    if (!tablet->tablet_v2) {
        free(tablet);
        return;
    }
    tablet->wlr_tablet->data = tablet;
    add_listener(&device->events.destroy, &tablet->destroy, tablet_destroy);
    wl_list_insert(server->tablet.tablets.prev, &tablet->link);
    wlr_cursor_attach_input_device(server->cursor, device);
    wlr_cursor_map_input_to_output(server->cursor, device, tablet_output(server));
    // A pad that came first, or alone, belongs to it.
    struct sh_tablet_pad *pad;
    wl_list_for_each(pad, &server->tablet.pads, link) {
        if (!pad->tablet) {
            pad->tablet = tablet;
            pad_enter(pad, server->seat->keyboard_state.focused_surface);
        }
    }
    wlr_log(WLR_INFO, "Drawing tablet %s", device->name ? device->name : "");
}

/* Whether the surface with the keyboard takes the pad: wlroots would otherwise go on sending
 * its buttons to the last one that did. */
static bool pad_focused(struct sh_tablet_pad *pad) {
    struct wlr_surface *surface = pad->server->seat->keyboard_state.focused_surface;
    return pad->tablet && surface && wlr_surface_accepts_tablet_v2(surface, pad->tablet->tablet_v2);
}

static void pad_button(struct wl_listener *listener, void *data) {
    struct sh_tablet_pad *pad = wl_container_of(listener, pad, button);
    struct wlr_tablet_pad_button_event *event = data;
    wlr_idle_notifier_v1_notify_activity(pad->server->idle_notifier, pad->server->seat);
    if (!pad_focused(pad))
        return;
    wlr_tablet_v2_tablet_pad_notify_button(pad->pad_v2, event->button, event->time_msec,
                                           (enum zwp_tablet_pad_v2_button_state)event->state);
}

static void pad_ring(struct wl_listener *listener, void *data) {
    struct sh_tablet_pad *pad = wl_container_of(listener, pad, ring);
    struct wlr_tablet_pad_ring_event *event = data;
    wlr_idle_notifier_v1_notify_activity(pad->server->idle_notifier, pad->server->seat);
    if (!pad_focused(pad))
        return;
    wlr_tablet_v2_tablet_pad_notify_ring(pad->pad_v2, event->ring, event->position,
                                         event->source == WLR_TABLET_PAD_RING_SOURCE_FINGER,
                                         event->time_msec);
}

static void pad_strip(struct wl_listener *listener, void *data) {
    struct sh_tablet_pad *pad = wl_container_of(listener, pad, strip);
    struct wlr_tablet_pad_strip_event *event = data;
    wlr_idle_notifier_v1_notify_activity(pad->server->idle_notifier, pad->server->seat);
    if (!pad_focused(pad))
        return;
    wlr_tablet_v2_tablet_pad_notify_strip(pad->pad_v2, event->strip, event->position,
                                          event->source == WLR_TABLET_PAD_STRIP_SOURCE_FINGER,
                                          event->time_msec);
}

/* libinput tells which tablet a pad belongs to once both are there. */
static void pad_attach(struct wl_listener *listener, void *data) {
    struct sh_tablet_pad *pad = wl_container_of(listener, pad, attach);
    struct wlr_tablet *wlr_tablet = data;
    struct sh_tablet *tablet;
    wl_list_for_each(tablet, &pad->server->tablet.tablets, link) {
        if (tablet->wlr_tablet == wlr_tablet && pad->tablet != tablet) {
            pad->tablet = tablet;
            pad_enter(pad, pad->server->seat->keyboard_state.focused_surface);
        }
    }
}

static void pad_destroy(struct wl_listener *listener, void *data) {
    struct sh_tablet_pad *pad = wl_container_of(listener, pad, destroy);
    wl_list_remove(&pad->button.link);
    wl_list_remove(&pad->ring.link);
    wl_list_remove(&pad->strip.link);
    wl_list_remove(&pad->attach.link);
    wl_list_remove(&pad->destroy.link);
    wl_list_remove(&pad->link);
    free(pad);
}

void server_new_tablet_pad(struct sh_server *server, struct wlr_input_device *device) {
    struct sh_tablet_pad *pad = calloc(1, sizeof(*pad));
    if (!pad)
        return;
    pad->server = server;
    pad->wlr_pad = wlr_tablet_pad_from_input_device(device);
    pad->pad_v2 = wlr_tablet_pad_create(server->tablet.manager, server->seat, device);
    if (!pad->pad_v2) {
        free(pad);
        return;
    }
    add_listener(&pad->wlr_pad->events.button, &pad->button, pad_button);
    add_listener(&pad->wlr_pad->events.ring, &pad->ring, pad_ring);
    add_listener(&pad->wlr_pad->events.strip, &pad->strip, pad_strip);
    add_listener(&pad->wlr_pad->events.attach_tablet, &pad->attach, pad_attach);
    add_listener(&device->events.destroy, &pad->destroy, pad_destroy);
    wl_list_insert(server->tablet.pads.prev, &pad->link);
    // Until libinput says otherwise, it belongs to the tablet plugged in last.
    if (!wl_list_empty(&server->tablet.tablets))
        pad->tablet = wl_container_of(server->tablet.tablets.prev, pad->tablet, link);
    pad_enter(pad, server->seat->keyboard_state.focused_surface);
}

/* The keyboard moved: the pads follow it. */
static void keyboard_focus_change(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, tablet.keyboard_focus_change);
    struct wlr_seat_keyboard_focus_change_event *event = data;
    struct sh_tablet_pad *pad;
    wl_list_for_each(pad, &server->tablet.pads, link) {
        if (event->old_surface)
            wlr_tablet_v2_tablet_pad_notify_leave(pad->pad_v2, event->old_surface);
        pad_enter(pad, event->new_surface);
    }
}

/* `get tablet`: each tablet (`tablet`, its name and the output it is mapped to, "-" for every
 * one), each pad (`pad`, its name and its tablet's, "-" for none), and each tool (`tool`, its
 * type, whether it is near, and what it is over: `tablet` and the surface as `get seat` names
 * it, `pointer`, or `-`). */
void describe_tablets(struct sh_server *server, int fd,
                      void (*surface)(struct sh_server *, int, const char *, struct wlr_surface *)) {
    char line[320];
    struct sh_tablet *tablet;
    struct wlr_output *output = tablet_output(server);
    wl_list_for_each(tablet, &server->tablet.tablets, link) {
        snprintf(line, sizeof(line), "tablet\t%s\t%s\n",
                 tablet->wlr_tablet->base.name ? tablet->wlr_tablet->base.name : "",
                 output ? output->name : "-");
        control_reply(fd, line);
    }
    struct sh_tablet_pad *pad;
    wl_list_for_each(pad, &server->tablet.pads, link) {
        const char *tablet_name = pad->tablet ? pad->tablet->wlr_tablet->base.name : NULL;
        snprintf(line, sizeof(line), "pad\t%s\t%s\n",
                 pad->wlr_pad->base.name ? pad->wlr_pad->base.name : "",
                 tablet_name ? tablet_name : "-");
        control_reply(fd, line);
    }
    static const char *types[] = {"-",     "pen",      "eraser", "brush", "pencil",
                                  "airbrush", "mouse", "lens",   "totem"};
    struct sh_tablet_tool *tool;
    wl_list_for_each(tool, &server->tablet.tools, link) {
        enum wlr_tablet_tool_type type = tool->wlr_tool->type;
        snprintf(line, sizeof(line), "tool\t%s\t%d",
                 type >= WLR_TABLET_TOOL_TYPE_PEN && type <= WLR_TABLET_TOOL_TYPE_TOTEM
                     ? types[type]
                     : "-",
                 tool->near);
        if (tool->pointer) {
            control_reply(fd, line);
            control_reply(fd, "\tpointer\t-\n");
        } else {
            surface(server, fd, line, tool->tool_v2->focused_surface);
        }
    }
}

void tablet_init(struct sh_server *server) {
    server->tablet.manager = wlr_tablet_v2_create(server->wl_display);
    wl_list_init(&server->tablet.tablets);
    wl_list_init(&server->tablet.pads);
    wl_list_init(&server->tablet.tools);
    struct wlr_cursor *cursor = server->cursor;
    add_listener(&cursor->events.tablet_tool_proximity, &server->tablet.tool_proximity,
                 tool_proximity);
    add_listener(&cursor->events.tablet_tool_axis, &server->tablet.tool_axis, tool_axis);
    add_listener(&cursor->events.tablet_tool_tip, &server->tablet.tool_tip, tool_tip);
    add_listener(&cursor->events.tablet_tool_button, &server->tablet.tool_button, tool_button);
    add_listener(&server->seat->keyboard_state.events.focus_change,
                 &server->tablet.keyboard_focus_change, keyboard_focus_change);
}

void tablet_finish(struct sh_server *server) {
    wl_list_remove(&server->tablet.tool_proximity.link);
    wl_list_remove(&server->tablet.tool_axis.link);
    wl_list_remove(&server->tablet.tool_tip.link);
    wl_list_remove(&server->tablet.tool_button.link);
    wl_list_remove(&server->tablet.keyboard_focus_change.link);
}
