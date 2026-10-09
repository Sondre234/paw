/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* Touchscreens: each finger goes to the surface under it through wl_touch, several at once, on
 * the monitor the screen is mapped to (touch.output, else the one the device names, else a
 * built-in panel, else every monitor). A finger on a client that never bound wl_touch, on the
 * window controls, a drag strip or the bare desktop stands in for the pointer and its left
 * button instead, one finger at a time, as on sway. */
#include "server.h"

struct sh_touch_device {
    struct wl_list link; // sh_server.touch.devices
    struct sh_server *server;
    struct wlr_touch *touch;
    struct wl_listener destroy;
};

/* A built-in panel: a laptop's or a tablet's own screen. */
static bool built_in(const struct wlr_output *output) {
    return !strncmp(output->name, "eDP", 3) || !strncmp(output->name, "LVDS", 4) ||
           !strncmp(output->name, "DSI", 3);
}

/* The output a touchscreen is mapped to, or NULL for the whole layout. A touch.output that is
 * not plugged in leaves the choice to the rest. */
static struct wlr_output *touch_output(struct sh_server *server, struct wlr_touch *touch) {
    const char *setting = server_settings(server)->touch_output;
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        if (setting[0] && output_key_matches(setting, output->wlr_output))
            return output->wlr_output;
    }
    struct wlr_output *named = touch->output_name ? find_output(server, touch->output_name) : NULL;
    if (named)
        return named;
    wl_list_for_each(output, &server->outputs, link) {
        if (built_in(output->wlr_output))
            return output->wlr_output;
    }
    return NULL;
}

/* Maps every touchscreen again: the outputs or touch.output changed. */
void map_touchscreens(struct sh_server *server) {
    struct sh_touch_device *device;
    wl_list_for_each(device, &server->touch.devices, link)
        wlr_cursor_map_input_to_output(server->cursor, &device->touch->base,
                                       touch_output(server, device->touch));
}

/* The output a touchscreen is mapped to, for `get touch`; NULL for the whole layout. */
static struct wlr_output *mapped_output(struct sh_server *server, struct wlr_touch *touch) {
    struct wlr_output *output = touch_output(server, touch);
    return output && wlr_output_layout_get(server->output_layout, output) ? output : NULL;
}

/* Clients see a touch capability only while a touchscreen is plugged in, so that one without
 * any binds no wl_touch. */
static void update_capabilities(struct sh_server *server) {
    uint32_t capabilities = WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD;
    if (!wl_list_empty(&server->touch.devices))
        capabilities |= WL_SEAT_CAPABILITY_TOUCH;
    wlr_seat_set_capabilities(server->seat, capabilities);
}

static struct sh_touch_point *find_point(struct sh_server *server, int32_t id) {
    for (size_t i = 0; i < sizeof(server->touch.points) / sizeof(*server->touch.points); ++i) {
        if (server->touch.points[i].used && server->touch.points[i].id == id)
            return &server->touch.points[i];
    }
    return NULL;
}

/* Forgets the points wlroots no longer has, as after a cancel. */
static void prune_points(struct sh_server *server) {
    for (size_t i = 0; i < sizeof(server->touch.points) / sizeof(*server->touch.points); ++i) {
        struct sh_touch_point *point = &server->touch.points[i];
        if (point->used && !wlr_seat_touch_get_point(server->seat, point->id))
            point->used = false;
    }
}

/* The finger standing in for the pointer presses or lets go of its left button, which reaches
 * whatever a click there would: a window control, a drag strip that moves the window as the
 * finger moves, the desktop, the overview. */
static void pointer_button(struct sh_server *server, uint32_t time,
                           enum wl_pointer_button_state state) {
    struct wlr_pointer_button_event event = {
        .time_msec = time, .button = BTN_LEFT, .state = state};
    server_cursor_button(&server->cursor_button, &event);
    wlr_seat_pointer_notify_frame(server->seat);
}

static void pointer_move(struct sh_server *server, struct wlr_touch *touch, uint32_t time,
                         double x, double y) {
    wlr_cursor_warp_absolute(server->cursor, &touch->base, x, y);
    process_cursor_motion(server, time);
    wlr_seat_pointer_notify_frame(server->seat);
}

/* Lets go of the finger standing in for the pointer, if one does. */
static void release_pointer(struct sh_server *server, uint32_t time) {
    if (server->touch.pointer_id < 0)
        return;
    server->touch.pointer_id = -1;
    pointer_button(server, time, WL_POINTER_BUTTON_STATE_RELEASED);
}

/* A finger on a window or a panel focuses it, as a click does. */
static void touch_focus(struct sh_server *server, double x, double y, struct sh_node *owner) {
    struct wlr_output *output = wlr_output_layout_output_at(server->output_layout, x, y);
    if (output)
        set_active_output(server, output->name);
    if (server->locked || !owner)
        return;
    if (owner->kind == SH_NODE_TOPLEVEL)
        focus_toplevel(owner->owner);
    else
        focus_layer(owner->owner);
}

static void touch_down(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, touch_down);
    struct wlr_touch_down_event *event = data;
    wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
    double x, y, sx = 0, sy = 0;
    wlr_cursor_absolute_to_layout_coords(server->cursor, &event->touch->base, event->x, event->y,
                                         &x, &y);
    // The overview, and what the compositor draws itself, take the pointer.
    struct sh_node *owner = NULL;
    struct wlr_surface *surface =
        server->overview.open ? NULL : press_target_at(server, x, y, &sx, &sy, &owner);
    if (surface && wlr_surface_accepts_touch(surface, server->seat)) {
        struct sh_touch_point *point = find_point(server, event->touch_id);
        for (size_t i = 0; !point && i < sizeof(server->touch.points) / sizeof(*point); ++i) {
            if (!server->touch.points[i].used)
                point = &server->touch.points[i];
        }
        if (!point)
            return; // more fingers than any screen has
        touch_focus(server, x, y, owner);
        if (!wlr_seat_touch_notify_down(server->seat, surface, event->time_msec, event->touch_id,
                                        sx, sy))
            return;
        *point = (struct sh_touch_point){
            .id = event->touch_id, .used = true, .origin_x = x - sx, .origin_y = y - sy};
        return;
    }
    if (server->touch.pointer_id >= 0)
        return; // one finger stands in for the pointer at a time
    server->touch.pointer_id = event->touch_id;
    pointer_move(server, event->touch, event->time_msec, event->x, event->y);
    pointer_button(server, event->time_msec, WL_POINTER_BUTTON_STATE_PRESSED);
}

static void touch_motion(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, touch_motion);
    struct wlr_touch_motion_event *event = data;
    wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
    if (event->touch_id == server->touch.pointer_id) {
        pointer_move(server, event->touch, event->time_msec, event->x, event->y);
        return;
    }
    struct sh_touch_point *point = find_point(server, event->touch_id);
    if (!point)
        return;
    // The surface the finger came down on keeps it, wherever it goes, in its own coordinates.
    double x, y;
    wlr_cursor_absolute_to_layout_coords(server->cursor, &event->touch->base, event->x, event->y,
                                         &x, &y);
    wlr_seat_touch_notify_motion(server->seat, event->time_msec, event->touch_id,
                                 x - point->origin_x, y - point->origin_y);
}

static void touch_up(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, touch_up);
    struct wlr_touch_up_event *event = data;
    wlr_idle_notifier_v1_notify_activity(server->idle_notifier, server->seat);
    if (event->touch_id == server->touch.pointer_id) {
        release_pointer(server, event->time_msec);
        return;
    }
    struct sh_touch_point *point = find_point(server, event->touch_id);
    if (!point)
        return;
    point->used = false;
    wlr_seat_touch_notify_up(server->seat, event->time_msec, event->touch_id);
}

/* libinput gives a touch up (a palm, the screen turning off): the client forgets every finger
 * it had, as wl_touch.cancel says. */
static void touch_cancel(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, touch_cancel);
    struct wlr_touch_cancel_event *event = data;
    if (event->touch_id == server->touch.pointer_id) {
        release_pointer(server, event->time_msec);
        return;
    }
    struct wlr_touch_point *point = wlr_seat_touch_get_point(server->seat, event->touch_id);
    if (point && point->client)
        wlr_seat_touch_notify_cancel(server->seat, point->client);
    prune_points(server);
}

static void touch_frame(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, touch_frame);
    wlr_seat_touch_notify_frame(server->seat);
}

static void touch_device_destroy(struct wl_listener *listener, void *data) {
    struct sh_touch_device *device = wl_container_of(listener, device, destroy);
    struct sh_server *server = device->server;
    wl_list_remove(&device->destroy.link);
    wl_list_remove(&device->link);
    free(device);
    if (wl_list_empty(&server->touch.devices)) {
        // The last one went with fingers down: they lift.
        release_pointer(server, (uint32_t)now_ms());
        struct wlr_seat_client *clients[16];
        int count = 0;
        struct wlr_touch_point *point;
        wl_list_for_each(point, &server->seat->touch_state.touch_points, link) {
            bool listed = !point->client;
            for (int i = 0; i < count && !listed; ++i)
                listed = clients[i] == point->client;
            if (!listed && count < 16)
                clients[count++] = point->client;
        }
        for (int i = 0; i < count; ++i)
            wlr_seat_touch_notify_cancel(server->seat, clients[i]);
        prune_points(server);
    }
    if (server->running)
        update_capabilities(server);
}

void server_new_touch(struct sh_server *server, struct wlr_input_device *input) {
    struct sh_touch_device *device = calloc(1, sizeof(*device));
    if (!device)
        return;
    device->server = server;
    device->touch = wlr_touch_from_input_device(input);
    wlr_cursor_attach_input_device(server->cursor, input);
    add_listener(&input->events.destroy, &device->destroy, touch_device_destroy);
    wl_list_insert(server->touch.devices.prev, &device->link);
    wlr_cursor_map_input_to_output(server->cursor, input, touch_output(server, device->touch));
    update_capabilities(server);
    wlr_log(WLR_INFO, "Touchscreen %s", input->name ? input->name : "");
}

/* `get touch`: each touchscreen (`touchscreen`, its name, and the output it is mapped to, "-"
 * for every one), then the finger standing in for the pointer (`pointer`, its id), if one does. */
void describe_touch(struct sh_server *server, int fd) {
    struct sh_touch_device *device;
    wl_list_for_each(device, &server->touch.devices, link) {
        struct wlr_output *output = mapped_output(server, device->touch);
        char line[256];
        snprintf(line, sizeof(line), "touchscreen\t%s\t%s\n",
                 device->touch->base.name ? device->touch->base.name : "",
                 output ? output->name : "-");
        control_reply(fd, line);
    }
    if (server->touch.pointer_id >= 0) {
        char line[64];
        snprintf(line, sizeof(line), "pointer\t%d\n", server->touch.pointer_id);
        control_reply(fd, line);
    }
}

void touch_init(struct sh_server *server) {
    wl_list_init(&server->touch.devices);
    server->touch.pointer_id = -1;
    struct wlr_cursor *cursor = server->cursor;
    add_listener(&cursor->events.touch_down, &server->touch_down, touch_down);
    add_listener(&cursor->events.touch_motion, &server->touch_motion, touch_motion);
    add_listener(&cursor->events.touch_up, &server->touch_up, touch_up);
    add_listener(&cursor->events.touch_cancel, &server->touch_cancel, touch_cancel);
    add_listener(&cursor->events.touch_frame, &server->touch_frame, touch_frame);
}

void touch_finish(struct sh_server *server) {
    wl_list_remove(&server->touch_down.link);
    wl_list_remove(&server->touch_motion.link);
    wl_list_remove(&server->touch_up.link);
    wl_list_remove(&server->touch_cancel.link);
    wl_list_remove(&server->touch_frame.link);
}
