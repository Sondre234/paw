/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Minimizing into the taskbar and restoring out of it: where clients show each window on each
 * output (wlr-foreign-toplevel's set_rectangle, the window control's), the box a window shrinks
 * into or grows out of, and the animations that take it there and back. */
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

/* The box the window shrinks into from `window` (its box in layout coordinates) on `output`, in
 * layout coordinates: its button there, as a taskbar last said; else, without one, the middle of
 * the strip a panel keeps along an edge of the output, where a taskbar would be; else a third of
 * its own size a little below its middle, shrinking where it is. `from` names which. */
static struct wlr_box fold_target(struct sh_toplevel *toplevel, struct wlr_output *output,
                                  struct wlr_box window, const char **from) {
    struct sh_server *server = toplevel->server;
    struct wlr_box full;
    wlr_output_layout_get_box(server->output_layout, output, &full);
    for (struct sh_window_rect *rect = toplevel->rects; rect; rect = rect->next) {
        if (strcmp(rect->output, output->name))
            continue;
        struct wlr_box box = {full.x + rect->box.x, full.y + rect->box.y, rect->box.width,
                              rect->box.height},
                       shared;
        // A rectangle off the output is no button of it.
        if (!wlr_box_intersection(&shared, &box, &full))
            break;
        *from = "button";
        return box;
    }
    struct sh_rect usable = usable_area(server, output);
    int strips[4] = {usable.y - full.y, full.y + full.height - (usable.y + usable.height),
                     usable.x - full.x, full.x + full.width - (usable.x + usable.width)};
    int edge = 0;
    for (int i = 1; i < 4; ++i)
        if (strips[i] > strips[edge])
            edge = i;
    int strip = strips[edge];
    if (strip > 0 && strip < full.width / 2 && strip < full.height / 2) {
        int side = strip < 48 ? strip : 48;
        int mid_x = full.x + full.width / 2, mid_y = full.y + full.height / 2;
        int x = edge == 2 ? full.x + strip / 2 : edge == 3 ? full.x + full.width - strip / 2 : mid_x;
        int y = edge == 0 ? full.y + strip / 2 : edge == 1 ? full.y + full.height - strip / 2 : mid_y;
        *from = "panel";
        return (struct wlr_box){x - side / 2, y - side / 2, side, side};
    }
    *from = "place";
    int width = window.width / 3 > 1 ? window.width / 3 : 1;
    int height = window.height / 3 > 1 ? window.height / 3 : 1;
    return (struct wlr_box){window.x + (window.width - width) / 2,
                            window.y + (window.height - height) / 2 + window.height / 6, width,
                            height};
}

/* What rounds itself and follows the fold without being one of its nodes: the rounded clip of a
 * window's surfaces and the frame around it, both at rest without a fold. */
struct fold_rounding {
    struct wlr_scene_tree *clipped;
    struct wlr_box clip;
    int radius;
    struct wlr_scene_rect *frame;
    int frame_radius, hole;
};

static void round_fold(const struct sh_anim *anim, const struct fold_rounding *rounding) {
#ifdef PAW_ROUNDED_CORNERS
    if (rounding->clipped && rounding->radius > 0) {
        struct wlr_box box = rounding->clip;
        int radius = rounding->radius;
        sh_anim_fold_map(anim, &box.x, &box.y, &box.width, &box.height, &radius);
        wlr_scene_tree_set_rounded_clip(rounding->clipped, &box, radius);
    }
    if (rounding->frame && rounding->frame_radius > 0) {
        // The frame's corners and width shrink with the window's, a pixel wide at least.
        int x = 0, y = 0, width = 1, height = 1, radius = rounding->frame_radius;
        sh_anim_fold_map(anim, &x, &y, &width, &height, &radius);
        int hole = rounding->hole;
        sh_anim_fold_map(anim, &x, &y, &width, &height, &hole);
        wlr_scene_rect_set_rounding(rounding->frame, radius, hole > 0 || !rounding->hole ? hole : 1);
    }
#else
    (void)anim, (void)rounding;
#endif
}

/* The clip of `tree`, a subtree of a window's content that keeps what it holds from the
 * window's own clip, where the fold has what it holds: `box`, in the content's coordinates. */
static void clip_fold(const struct sh_anim *anim, struct wlr_scene_tree *tree, struct wlr_box box) {
#ifdef PAW_ROUNDED_CORNERS
    sh_anim_fold_map(anim, &box.x, &box.y, &box.width, &box.height, NULL);
    box.x -= tree->node.x;
    box.y -= tree->node.y;
    wlr_scene_tree_set_rounded_clip(tree, &box, 0);
#else
    (void)anim, (void)tree, (void)box;
#endif
}

/* A window's own fold, as it restores: its clip and frame, its shadow's and its gradient
 * border's clips, as frame.c last set them. */
static void window_folded(const struct sh_anim *anim, void *data) {
    struct sh_toplevel *toplevel = data;
    if (!toplevel->content)
        return;
    struct wlr_box g = toplevel_geometry(toplevel);
    struct fold_rounding rounding = {
        .clipped = toplevel->content,
        .clip = {0, 0, g.width, g.height},
        .radius = toplevel->corner_radius,
        .frame = toplevel->frame_radius > 0 ? toplevel->border[0] : NULL,
        .frame_radius = toplevel->frame_radius,
        .hole = toplevel->frame_hole,
    };
    round_fold(anim, &rounding);
    if (toplevel->shadow)
        clip_fold(anim, toplevel->shadow, toplevel->shadow_box);
    const struct sh_gradient_drawn *drawn = &toplevel->gradient_drawn;
    if (toplevel->gradient)
        clip_fold(anim, toplevel->gradient,
                  (struct wlr_box){toplevel->gradient->node.x - drawn->border,
                                   toplevel->gradient->node.y - drawn->border,
                                   drawn->width + 2 * drawn->border,
                                   drawn->height + 2 * drawn->border});
}

/* A copy of a window minimizing, and what of it rounds itself; freed with the copy's tree. */
struct fold_copy {
    struct fold_rounding rounding;
    struct wl_listener destroy;
};

static void copy_folded(const struct sh_anim *anim, void *data) {
    struct fold_copy *copy = data;
    round_fold(anim, &copy->rounding);
}

static void copy_destroyed(struct wl_listener *listener, void *data) {
    struct fold_copy *copy = wl_container_of(listener, copy, destroy);
    wl_list_remove(&copy->destroy.link);
    free(copy);
}

/* Copies of the nodes below `source` into a tree of their own in `parent`, where `source` is. */
static void copy_subtree(struct wlr_scene_tree *parent, struct wlr_scene_tree *source) {
    if (!source || !source->node.enabled)
        return;
    struct wlr_scene_tree *tree = wlr_scene_tree_create(parent);
    if (!tree)
        return;
    wlr_scene_node_set_position(&tree->node, source->node.x, source->node.y);
    sh_thumb_clone(tree, source, 1, 1, NULL);
}

/* A copy of what the window shows, as it is drawn now, at full size, in a tree just above it
 * whose origin is the window's (its content's): the shadow, then the window and what is drawn on
 * it inside a clip that rounds its corners as the window's own does, then the borders, which
 * round themselves outside that clip. The clip and the frame then follow the fold. NULL when
 * there is nothing to copy. */
static struct wlr_scene_tree *copy_window(struct sh_toplevel *toplevel, struct fold_copy **made) {
    struct wlr_scene_node *window = &toplevel->scene_tree->node;
    struct wlr_scene_tree *content = toplevel->content;
    struct fold_copy *copy = calloc(1, sizeof(*copy));
    struct wlr_scene_tree *tree = copy ? wlr_scene_tree_create(window->parent) : NULL;
    if (!tree) {
        free(copy);
        return NULL;
    }
    wlr_scene_node_place_above(&tree->node, window);
    wlr_scene_node_set_position(&tree->node, window->x + content->node.x,
                                window->y + content->node.y);
    copy->destroy.notify = copy_destroyed;
    wl_signal_add(&tree->node.events.destroy, &copy->destroy);
    copy_subtree(tree, toplevel->shadow);
    // What rounds itself, and what lies outside the clip, is copied apart from the rest.
    bool rounded = toplevel->corner_radius > 0;
    struct wlr_scene_node *apart[2 + 4] = {toplevel->shadow ? &toplevel->shadow->node : NULL,
                                           rounded && toplevel->gradient
                                               ? &toplevel->gradient->node
                                               : NULL};
    for (int i = 0; i < 4; ++i)
        apart[2 + i] = rounded && toplevel->border[i] ? &toplevel->border[i]->node : NULL;
    bool shown[6] = {false};
    for (int i = 0; i < 6; ++i) {
        shown[i] = apart[i] && apart[i]->enabled;
        if (shown[i])
            wlr_scene_node_set_enabled(apart[i], false);
    }
    struct wlr_scene_tree *body = wlr_scene_tree_create(tree);
    int made_nodes = body ? sh_thumb_clone(body, content, 1, 1, NULL) : 0;
    for (int i = 0; i < 6; ++i)
        if (shown[i])
            wlr_scene_node_set_enabled(apart[i], true);
    if (!made_nodes) {
        wlr_scene_node_destroy(&tree->node);
        return NULL;
    }
    struct wlr_box g = toplevel_geometry(toplevel);
    copy->rounding = (struct fold_rounding){.clipped = body,
                                            .clip = {0, 0, g.width, g.height},
                                            .radius = toplevel->corner_radius};
    if (rounded)
        copy_subtree(tree, toplevel->gradient);
    struct wlr_scene_rect *frame = toplevel->border[0];
    if (rounded && frame && frame->node.enabled && toplevel->frame_radius > 0) {
        struct wlr_scene_rect *ring = wlr_scene_rect_create(tree, frame->width, frame->height,
                                                            frame->color);
        if (ring) {
            wlr_scene_node_set_position(&ring->node, frame->node.x, frame->node.y);
            copy->rounding.frame = ring;
            copy->rounding.frame_radius = toplevel->frame_radius;
            copy->rounding.hole = toplevel->frame_hole;
        }
    }
    *made = copy;
    return tree;
}

/* Whether the window minimizes and restores with a fold: shown on its output, not in the
 * scratchpad, which has its own way in and out, nor peeked at (by the taskbar's picture or the
 * switcher), which shows it where it is already. */
static bool folds(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    return !server->locked && server->running && toplevel->shown && toplevel->scene_tree &&
           toplevel->scene_tree->node.enabled && toplevel_visible(toplevel) &&
           !toplevel->scratchpad && server->peek_window != toplevel && toplevel_mapped(toplevel);
}

/* The window is about to be minimized and hidden: a copy of it shrinks into its button, from
 * where it is drawn, or, if it was on its way out of the button, back in from there. */
void minimize_animate(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    if (toplevel->minimizing) {
        toplevel->minimizing->ref = NULL; // an earlier copy goes on by itself
        toplevel->minimizing = NULL;
    }
    if (!folds(toplevel) || !server_settings(server)->animations)
        return;
    struct sh_fold restoring;
    double from = sh_anim_folded(&toplevel->anim, &restoring);
    // Drawn at rest, though gliding still, for the copy.
    sh_anim_settle(&toplevel->anim);
    struct fold_copy *made = NULL;
    struct wlr_scene_tree *copy = copy_window(toplevel, &made);
    if (!copy)
        return;
    int x, y, rest_x, rest_y;
    wlr_scene_node_coords(&copy->node, &x, &y);
    wlr_scene_node_coords(&toplevel->scene_tree->node, &rest_x, &rest_y);
    struct wlr_box g = toplevel_geometry(toplevel), into;
    if (from >= 0) {
        into = (struct wlr_box){rest_x + restoring.into_x, rest_y + restoring.into_y,
                                restoring.into_width, restoring.into_height};
    } else {
        from = 0;
        struct wlr_output *output = toplevel_output(toplevel);
        if (!output) {
            wlr_scene_node_destroy(&copy->node);
            return;
        }
        into = fold_target(toplevel, output, (struct wlr_box){x, y, g.width, g.height},
                           &toplevel->fold_target);
    }
    toplevel->fold_into = into;
    struct sh_fold fold = {0, 0, g.width, g.height, into.x - x, into.y - y,
                           into.width, into.height, copy_folded, made};
    sh_anim_fold_away(server->animator, copy, &fold, from, &toplevel->minimizing);
}

/* The window minimized gave its focus to another, which came to the front: the copy goes on
 * over the windows it was over. */
void minimize_raise(struct sh_toplevel *toplevel) {
    if (toplevel->minimizing)
        wlr_scene_node_raise_to_top(&toplevel->minimizing->tree->node);
}

/* The window was just restored and shown where it goes: it grows out of its button, or, if it
 * was on its way in and is back in the same place, out from where it is drawn. */
void restore_animate(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    // One the peek showed while minimized, still fading out of it, is where it is already.
    if (!folds(toplevel) || toplevel->peek_lent || !server_settings(server)->animations)
        return;
    int x, y;
    wlr_scene_node_coords(&toplevel->scene_tree->node, &x, &y);
    struct wlr_box g = toplevel_geometry(toplevel), into;
    struct sh_fold minimizing;
    struct sh_anim *copy = toplevel->minimizing;
    double from = copy ? sh_anim_folded(copy, &minimizing) : -1;
    int copy_x = 0, copy_y = 0;
    if (copy)
        wlr_scene_node_coords(&copy->tree->node, &copy_x, &copy_y);
    if (from >= 0 && copy_x == x && copy_y == y && minimizing.rest_width == g.width &&
        minimizing.rest_height == g.height) {
        into = (struct wlr_box){copy_x + minimizing.into_x, copy_y + minimizing.into_y,
                                minimizing.into_width, minimizing.into_height};
        sh_anim_finish(copy);
    } else {
        // Anywhere else, the copy goes on in, and the window comes out of its button.
        from = 1;
        struct wlr_output *output = toplevel_output(toplevel);
        if (!output)
            return;
        into = fold_target(toplevel, output, (struct wlr_box){x, y, g.width, g.height},
                           &toplevel->fold_target);
    }
    toplevel->fold_into = into;
    // From its place: a glide that would bring it from where it was minimized lands first.
    sh_anim_finish(&toplevel->anim);
    struct sh_fold fold = {0, 0, g.width, g.height, into.x - x, into.y - y,
                           into.width, into.height, window_folded, toplevel};
    sh_anim_unfold(server->animator, &toplevel->anim, toplevel->content, &fold, from);
}

/* The window goes: a copy still minimizing goes on by itself. */
void minimize_forget(struct sh_toplevel *toplevel) {
    if (toplevel->minimizing)
        toplevel->minimizing->ref = NULL;
    toplevel->minimizing = NULL;
    forget_window_rects(toplevel);
}

/* `get minimize`: "rect ID OUTPUT X Y WIDTH HEIGHT" for where clients show each window, the
 * one that counts first, in its output's coordinates; then "fold ID minimize|restore AT X Y
 * WIDTH HEIGHT TARGET" for each window on its way in or out: AT in thousandths of the way into
 * the box, the box in layout coordinates, TARGET "button", "panel" or "place". */
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
    wl_list_for_each(toplevel, &server->toplevels, link) {
        double minimizing = toplevel->minimizing ? sh_anim_folded(toplevel->minimizing, NULL) : -1;
        double restoring = sh_anim_folded(&toplevel->anim, NULL);
        struct wlr_box into = toplevel->fold_into;
        for (int restore = 0; restore < 2; ++restore) {
            double at = restore ? restoring : minimizing;
            if (at < 0)
                continue;
            snprintf(line, sizeof(line), "fold\t%u\t%s\t%ld\t%d\t%d\t%d\t%d\t%s\n",
                     toplevel->id, restore ? "restore" : "minimize", lround(1000 * at), into.x,
                     into.y, into.width, into.height,
                     toplevel->fold_target ? toplevel->fold_target : "-");
            control_reply(fd, line);
        }
    }
}
