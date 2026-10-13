/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Minimizing into the taskbar and restoring out of it: where clients show each window on each
 * output (wlr-foreign-toplevel's set_rectangle, the window control's). */
#include "server.h"

/* Where a client shows a window on one output, in the output's logical coordinates: its button
 * on a taskbar, or a picture of it. It lasts until the resource it came through goes. */
struct sh_window_rect {
    struct sh_window_rect *next;
    struct sh_toplevel *toplevel;
    char output[64];
    struct wlr_box box;
    struct wl_resource *owner; // the taskbar's surface, or its window control object
    struct wl_listener owner_destroy;
};

static void free_rect(struct sh_window_rect *rect) {
    struct sh_window_rect **link = &rect->toplevel->rects;
    while (*link && *link != rect)
        link = &(*link)->next;
    if (*link)
        *link = rect->next;
    wl_list_remove(&rect->owner_destroy.link);
    free(rect);
}

static void rect_owner_destroyed(struct wl_listener *listener, void *data) {
    struct sh_window_rect *rect = wl_container_of(listener, rect, owner_destroy);
    free_rect(rect);
}

/* The owner's rectangle for the window on `output`, or on any output when it is NULL. */
static struct sh_window_rect *owned_rect(struct sh_toplevel *toplevel, const char *output,
                                         struct wl_resource *owner) {
    for (struct sh_window_rect *rect = toplevel->rects; rect; rect = rect->next)
        if (rect->owner == owner && (!output || !strcmp(rect->output, output)))
            return rect;
    return NULL;
}

/* Where `owner` shows the window on `output`, which goes first among the window's rectangles: of
 * several clients' rectangles on one output, the last one set counts. An empty box takes the
 * owner's rectangle away, on every output when `output` is NULL. */
void set_window_rect(struct sh_toplevel *toplevel, const char *output, struct wlr_box box,
                     struct wl_resource *owner) {
    struct sh_window_rect *rect = owned_rect(toplevel, output, owner);
    if (box.width <= 0 || box.height <= 0) {
        for (; rect; rect = owned_rect(toplevel, output, owner))
            free_rect(rect);
        return;
    }
    if (!output || !output[0])
        return;
    if (rect) {
        free_rect(rect);
        rect = NULL;
    }
    rect = calloc(1, sizeof(*rect));
    if (!rect)
        return;
    rect->toplevel = toplevel;
    snprintf(rect->output, sizeof(rect->output), "%s", output);
    rect->box = box;
    rect->owner = owner;
    rect->owner_destroy.notify = rect_owner_destroyed;
    wl_resource_add_destroy_listener(owner, &rect->owner_destroy);
    rect->next = toplevel->rects;
    toplevel->rects = rect;
}

/* The window leaves the taskbars, whose handles for it close: what they said of it goes. */
void forget_window_rects(struct sh_toplevel *toplevel) {
    while (toplevel->rects)
        free_rect(toplevel->rects);
}

/* Where a client's surface is on its output, in the output's logical coordinates: a layer
 * surface, a taskbar's, or one of its subsurfaces. False for any other. */
static bool surface_place(struct sh_server *server, struct wlr_surface *surface,
                          struct wlr_output **output, int *x, int *y) {
    int dx = 0, dy = 0;
    struct wlr_subsurface *subsurface;
    while ((subsurface = wlr_subsurface_try_from_wlr_surface(surface))) {
        dx += subsurface->current.x;
        dy += subsurface->current.y;
        surface = subsurface->parent;
    }
    struct wlr_layer_surface_v1 *layer_surface = wlr_layer_surface_v1_try_from_wlr_surface(surface);
    struct sh_layer *layer = layer_surface ? layer_surface->data : NULL;
    if (!layer || !layer->scene || !layer_surface->output)
        return false;
    int lx, ly;
    wlr_scene_node_coords(&layer->scene->tree->node, &lx, &ly);
    struct wlr_box box;
    wlr_output_layout_get_box(server->output_layout, layer_surface->output, &box);
    *output = layer_surface->output;
    *x = lx - box.x + dx;
    *y = ly - box.y + dy;
    return true;
}

/* zwlr_foreign_toplevel_handle_v1.set_rectangle: where a taskbar shows the window, in its
 * surface's coordinates. Only the last one a surface gives counts, and a rectangle of no size
 * takes it away. */
void foreign_set_rectangle(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, foreign_rectangle);
    struct wlr_foreign_toplevel_handle_v1_set_rectangle_event *event = data;
    if (!event->surface)
        return;
    struct wl_resource *owner = event->surface->resource;
    set_window_rect(toplevel, NULL, (struct wlr_box){0}, owner);
    struct wlr_output *output;
    int x, y;
    if (event->width > 0 && event->height > 0 &&
        surface_place(toplevel->server, event->surface, &output, &x, &y))
        set_window_rect(toplevel, output->name,
                        (struct wlr_box){x + event->x, y + event->y, event->width, event->height},
                        owner);
}

/* `get minimize`: "rect ID OUTPUT X Y WIDTH HEIGHT" for where clients show each window, the
 * one that counts first, in its output's coordinates. */
void describe_minimize(struct sh_server *server, int fd, const char *arguments) {
    control_reply(fd, "ok\n");
    struct sh_toplevel *toplevel;
    char line[256];
    wl_list_for_each(toplevel, &server->toplevels, link) {
        for (struct sh_window_rect *rect = toplevel->rects; rect; rect = rect->next) {
            char output[64];
            copy_field(output, sizeof(output), rect->output);
            snprintf(line, sizeof(line), "rect\t%u\t%s\t%d\t%d\t%d\t%d\n", toplevel->id,
                     output, rect->box.x, rect->box.y, rect->box.width, rect->box.height);
            control_reply(fd, line);
        }
    }
}
