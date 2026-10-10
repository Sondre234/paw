/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The taskbar's window menu and pictures: paw-window-control-v1 names a window by its
 * wlr-foreign-toplevel handle, tells the shell which output and workspace each is on, how it is
 * placed, its number and its own icon, moves one to another workspace or output, makes it sticky,
 * floats it or keeps it above, gives its capture source for a picture of it, and peeks at it. */
#include "server.h"
#include <sys/mman.h>

/* One paw_window_v1: the window it names, NULL once that is gone, and what it last sent
 * (the icon as the window's icon_serial then). */
struct sh_window_object {
    struct wl_resource *resource;
    struct sh_server *server;
    struct sh_toplevel *toplevel;
    struct wl_list link; // sh_server.window_objects
    bool sent;
    char output[64];
    uint32_t workspace, state, pid;
    unsigned icon_serial;
};

/* The window a taskbar handle the client holds stands for, or NULL when it is gone: the handles
 * wlroots made for a window are the resources of its foreign-toplevel handle. */
static struct sh_toplevel *handle_toplevel(struct sh_server *server, struct wl_resource *handle) {
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (!toplevel->foreign)
            continue;
        struct wl_resource *resource;
        wl_resource_for_each(resource, &toplevel->foreign->resources) {
            if (resource == handle)
                return toplevel;
        }
    }
    return NULL;
}

/* The state bits the object's version knows: from version 6, whether it is kept above. */
static uint32_t window_state(struct sh_toplevel *toplevel, struct wl_resource *resource) {
    struct wlr_output *output = find_output(toplevel->server, toplevel->output);
    bool above = toplevel->above &&
                 wl_resource_get_version(resource) >= PAW_WINDOW_V1_STATE_ABOVE_SINCE_VERSION;
    return (toplevel->sticky ? PAW_WINDOW_V1_STATE_STICKY : 0) |
           (toplevel->floating || toplevel->sticky ? PAW_WINDOW_V1_STATE_FLOATING : 0) |
           (toplevel->tiled ? PAW_WINDOW_V1_STATE_TILED : 0) |
           (tiles_for(toplevel, output) ? PAW_WINDOW_V1_STATE_TILING : 0) |
           (above ? PAW_WINDOW_V1_STATE_ABOVE : 0);
}

/* A file holding the icon's pixels for icon_image, sealed so that no client changes what
 * another reads from it; -1 when it cannot be made. */
static int icon_file(const struct sh_icon *icon) {
    int fd = memfd_create("paw-window-icon", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0)
        return -1;
    const char *data = (const char *)icon->pixels;
    size_t size = (size_t)icon->width * icon->height * 4;
    while (size) {
        ssize_t written = write(fd, data, size);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0) {
            close(fd);
            return -1;
        }
        data += written;
        size -= (size_t)written;
    }
    fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL);
    return fd;
}

/* The window's icon: its name (null for none, as for an icon dropped), then its pixels when it
 * has them, in a file of the client's own, which libwayland passes a copy of, so the
 * compositor's goes at once. */
static void send_icon(struct wl_resource *resource, const struct sh_icon *icon) {
    paw_window_v1_send_icon(resource, icon->name);
    if (!icon->pixels)
        return;
    int fd = icon_file(icon);
    if (fd < 0) {
        wlr_log_errno(WLR_ERROR, "Cannot pass a window's icon to a client");
        return;
    }
    paw_window_v1_send_icon_image(resource, fd, (uint32_t)icon->width,
                                       (uint32_t)icon->height);
    close(fd);
}

/* Sends what changed since the object last heard, then done; everything the first time. */
static void send_window(struct sh_window_object *object) {
    struct sh_toplevel *toplevel = object->toplevel;
    if (!toplevel)
        return;
    bool changed = !object->sent;
    if (!object->sent || strcmp(object->output, toplevel->output)) {
        snprintf(object->output, sizeof(object->output), "%s", toplevel->output);
        paw_window_v1_send_output(object->resource, object->output);
        changed = true;
    }
    uint32_t workspace = (uint32_t)toplevel->workspace + 1;
    if (!object->sent || object->workspace != workspace) {
        object->workspace = workspace;
        paw_window_v1_send_workspace(object->resource, workspace);
        changed = true;
    }
    uint32_t state = window_state(toplevel, object->resource);
    if (!object->sent || object->state != state) {
        object->state = state;
        paw_window_v1_send_state(object->resource, state);
        changed = true;
    }
    // From version 3, for the taskbar to match the window with the sound its process plays.
    pid_t process = toplevel_pid(toplevel);
    uint32_t pid = process > 0 ? (uint32_t)process : 0;
    if (wl_resource_get_version(object->resource) >= PAW_WINDOW_V1_PID_SINCE_VERSION &&
        (!object->sent || object->pid != pid)) {
        object->pid = pid;
        paw_window_v1_send_pid(object->resource, pid);
        changed = true;
    }
    // From version 4, once, as it never changes: the switcher's lines name the window by it.
    if (wl_resource_get_version(object->resource) >= PAW_WINDOW_V1_ID_SINCE_VERSION &&
        !object->sent)
        paw_window_v1_send_id(object->resource, toplevel->id);
    // From version 5, as it changes: the icon the window supplies itself, which goes with the
    // first state only when the window has one, there being nothing yet for the client to drop.
    if (wl_resource_get_version(object->resource) >= PAW_WINDOW_V1_ICON_SINCE_VERSION &&
        object->icon_serial != toplevel->icon_serial) {
        object->icon_serial = toplevel->icon_serial;
        if (object->sent || toplevel->icon.name || toplevel->icon.pixels) {
            send_icon(object->resource, &toplevel->icon);
            changed = true;
        }
    }
    if (changed)
        paw_window_v1_send_done(object->resource);
    object->sent = true;
}

static void send_windows(void *data) {
    struct sh_server *server = data;
    server->window_objects_idle = NULL;
    struct sh_window_object *object;
    wl_list_for_each(object, &server->window_objects, link) send_window(object);
}

/* Something about the windows may have changed: their objects hear of it once the change is
 * over, so that a window moving through several steps is described once, as it ends up. */
void window_objects_changed(struct sh_server *server) {
    if (!server->window_control || server->window_objects_idle ||
        wl_list_empty(&server->window_objects))
        return;
    server->window_objects_idle = wl_event_loop_add_idle(
        wl_display_get_event_loop(server->wl_display), send_windows, server);
}

/* The window is leaving the taskbar, its handles going inert: so do its objects, and a peek at
 * it ends. */
void window_objects_forget(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    if (!server->window_control)
        return;
    if (server->peek_window == toplevel)
        end_window_peek(server, true);
    struct sh_window_object *object;
    wl_list_for_each(object, &server->window_objects, link) {
        if (object->toplevel == toplevel)
            object->toplevel = NULL;
    }
}

/* The window a request is about, or NULL to ignore it: the window is gone, or the session is
 * locked. A hidden member of a window group is moved and placed with the window showing in its
 * slot. */
static struct sh_toplevel *requested(struct wl_resource *resource) {
    struct sh_window_object *object = wl_resource_get_user_data(resource);
    struct sh_toplevel *toplevel = object ? object->toplevel : NULL;
    if (!toplevel || toplevel->server->locked)
        return NULL;
    struct sh_toplevel *shown =
        toplevel->group_hidden ? group_shown(toplevel->server, toplevel->group) : NULL;
    return shown ? shown : toplevel;
}


static void window_move_to_workspace(struct wl_client *client, struct wl_resource *resource,
                                     uint32_t number) {
    struct sh_toplevel *toplevel = requested(resource);
    if (toplevel && number >= 1 && number <= (uint32_t)server_settings(toplevel->server)->workspaces)
        move_toplevel_to_workspace(toplevel->server, toplevel, (int)number - 1);
}

static void window_move_to_output(struct wl_client *client, struct wl_resource *resource,
                                  const char *name) {
    struct sh_toplevel *toplevel = requested(resource);
    struct wlr_output *output = toplevel ? find_output(toplevel->server, name) : NULL;
    if (output)
        move_toplevel_to_output(toplevel, output);
}

static void window_set_sticky(struct wl_client *client, struct wl_resource *resource) {
    struct sh_toplevel *toplevel = requested(resource);
    if (toplevel && server_settings(toplevel->server)->sticky)
        set_sticky(toplevel, true, true);
}

static void window_unset_sticky(struct wl_client *client, struct wl_resource *resource) {
    struct sh_toplevel *toplevel = requested(resource);
    if (toplevel)
        set_sticky(toplevel, false, true);
}

static void window_set_floating(struct wl_client *client, struct wl_resource *resource) {
    struct sh_toplevel *toplevel = requested(resource);
    if (toplevel)
        set_floating(toplevel, true, false);
}

static void window_unset_floating(struct wl_client *client, struct wl_resource *resource) {
    struct sh_toplevel *toplevel = requested(resource);
    if (toplevel)
        set_floating(toplevel, false, false);
}

static void window_set_above(struct wl_client *client, struct wl_resource *resource) {
    struct sh_toplevel *toplevel = requested(resource);
    if (toplevel)
        set_above(toplevel, true);
}

static void window_unset_above(struct wl_client *client, struct wl_resource *resource) {
    struct sh_toplevel *toplevel = requested(resource);
    if (toplevel)
        set_above(toplevel, false);
}

/* The capture source ext-foreign-toplevel-list's handle for the window gives, the same object;
 * an inert one when the window is gone or the session is locked, never none, which the client's
 * next request would find missing. */
static void window_get_capture_source(struct wl_client *client, struct wl_resource *resource,
                                      uint32_t id) {
    struct sh_window_object *object = wl_resource_get_user_data(resource);
    struct sh_toplevel *toplevel = object ? object->toplevel : NULL;
    wlr_ext_image_capture_source_v1_create_resource(
        toplevel ? toplevel_capture_source(toplevel) : NULL, client, id);
}

/* A capture source of its own for a small picture of the window (scaled_capture.c), inert as
 * get_capture_source's is. */
static void window_get_scaled_capture_source(struct wl_client *client,
                                             struct wl_resource *resource, uint32_t id,
                                             uint32_t width, uint32_t height) {
    if (!width || !height) {
        wl_resource_post_error(resource, PAW_WINDOW_V1_ERROR_INVALID_SIZE,
                               "a picture of %ux%u pixels", width, height);
        return;
    }
    struct sh_window_object *object = wl_resource_get_user_data(resource);
    create_scaled_capture_source(client, id, object ? object->toplevel : NULL, width, height);
}

/* Peeks at the window itself, a hidden member of a window group too (effects.c), until this
 * object ends it or goes, or something else does: another peek, the window closing or being
 * focused, the session locking, the peek action. */
static void window_set_peek(struct wl_client *client, struct wl_resource *resource) {
    struct sh_window_object *object = wl_resource_get_user_data(resource);
    struct sh_toplevel *toplevel = object ? object->toplevel : NULL;
    if (!toplevel || toplevel->server->locked || !toplevel_mapped(toplevel))
        return;
    peek_at_window(toplevel);
    toplevel->server->peek_object = object;
}

static void window_unset_peek(struct wl_client *client, struct wl_resource *resource) {
    struct sh_window_object *object = wl_resource_get_user_data(resource);
    if (object && object->server->peek_object == object)
        end_window_peek(object->server, true);
}

static const struct paw_window_v1_interface window_implementation = {
    .destroy = destroy_resource,
    .move_to_workspace = window_move_to_workspace,
    .move_to_output = window_move_to_output,
    .set_sticky = window_set_sticky,
    .unset_sticky = window_unset_sticky,
    .set_floating = window_set_floating,
    .unset_floating = window_unset_floating,
    .get_capture_source = window_get_capture_source,
    .get_scaled_capture_source = window_get_scaled_capture_source,
    .set_peek = window_set_peek,
    .unset_peek = window_unset_peek,
    .set_above = window_set_above,
    .unset_above = window_unset_above,
};

/* Destroyed by its client, or as the client disconnects: a peek it asked for ends. */
static void window_resource_destroy(struct wl_resource *resource) {
    struct sh_window_object *object = wl_resource_get_user_data(resource);
    if (object->server->peek_object == object)
        end_window_peek(object->server, true);
    wl_list_remove(&object->link);
    free(object);
}

static void control_get_window(struct wl_client *client, struct wl_resource *resource,
                               uint32_t id, struct wl_resource *handle) {
    struct sh_server *server = wl_resource_get_user_data(resource);
    struct sh_window_object *object = calloc(1, sizeof(*object));
    if (!object) {
        wl_client_post_no_memory(client);
        return;
    }
    object->resource = wl_resource_create(client, &paw_window_v1_interface,
                                          wl_resource_get_version(resource), id);
    if (!object->resource) {
        free(object);
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(object->resource, &window_implementation, object,
                                   window_resource_destroy);
    object->server = server;
    object->toplevel = handle_toplevel(server, handle);
    wl_list_insert(&server->window_objects, &object->link);
    send_window(object);
}


static const struct paw_window_control_v1_interface control_implementation = {
    .destroy = destroy_resource,
    .get_window = control_get_window,
};

static void control_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
    struct wl_resource *resource =
        wl_resource_create(client, &paw_window_control_v1_interface, version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &control_implementation, data, NULL);
}

/* Offered to every client, as wlr-foreign-toplevel-management is: it does nothing to a window a
 * taskbar could not already do, and shows nothing of one that ext-foreign-toplevel-list's
 * capture sources do not, but for which process made it, the number the switcher calls it and
 * the icon it gives for taskbars to show; a peek shows the window for a moment and changes
 * nothing about it. */
void window_control_init(struct sh_server *server) {
    wl_list_init(&server->window_objects);
    server->window_control = wl_global_create(server->wl_display,
                                              &paw_window_control_v1_interface, 6, server,
                                              control_bind);
    if (!server->window_control)
        wlr_log(WLR_ERROR, "Cannot offer paw-window-control-v1");
}
