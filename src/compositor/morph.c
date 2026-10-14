/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Windows gliding to a new place and size together (sh_anim_morph): maximized, snapped,
 * restored, made fullscreen, moved and resized by the keyboard, dragged off a snap. The client
 * draws the new size only once it has the configure, so its surfaces are stretched from what
 * it drew to the size drawn meanwhile, and what it drew before crossfades into its answer; the
 * frame (frame.c) is drawn around the size drawn. Input and `paw msg` see the final place at
 * once. */
#include "server.h"

/* The size the client's surfaces are drawn for now: an xdg-shell window's geometry, an X11
 * window's surface (its configured size is taken at once, its buffer later). */
static void live_size(struct sh_toplevel *toplevel, int *width, int *height) {
    struct wlr_box geometry = toplevel_geometry(toplevel);
    *width = geometry.width;
    *height = geometry.height;
#if WLR_HAS_XWAYLAND
    struct wlr_surface *surface = toplevel->xsurface ? toplevel->xsurface->surface : NULL;
    if (surface && surface->current.width > 0 && surface->current.height > 0) {
        *width = surface->current.width;
        *height = surface->current.height;
    }
#endif
}

/* Where the window is drawn and at what size, to glide from (toplevel_morph) once it is placed
 * again. */
struct wlr_box morph_from(struct sh_toplevel *toplevel) {
    struct wlr_box box = {toplevel->scene_tree->node.x + toplevel->content->node.x,
                          toplevel->scene_tree->node.y + toplevel->content->node.y, 0, 0};
    if (!sh_anim_drawn_size(&toplevel->anim, &box.width, &box.height))
        live_size(toplevel, &box.width, &box.height);
    return box;
}

/* The configure the client is to answer before it is drawn at its new size; 0 for none. */
static uint32_t pending_configure(struct sh_toplevel *toplevel) {
    if (!toplevel->xdg_toplevel)
        return 0;
    struct wlr_xdg_surface *base = toplevel->xdg_toplevel->base;
    bool pending = base->configure_idle || !wl_list_empty(&base->configure_list);
    return pending ? base->scheduled_serial : 0;
}

/* Whether what the client drew at `width` by `height` answers the size it was given. */
static bool answered(struct sh_toplevel *toplevel, int width, int height) {
#if WLR_HAS_XWAYLAND
    if (toplevel->xsurface)
        return width == toplevel->xsurface->width && height == toplevel->xsurface->height;
#endif
    uint32_t serial = toplevel->morph_serial;
    return !serial ||
           (int32_t)(toplevel->xdg_toplevel->base->current.configure_serial - serial) >= 0;
}

/* The size drawn changed: the frame follows it. */
static void morph_resized(void *data) {
    struct sh_toplevel *toplevel = data;
    if (toplevel->scene_tree)
        refresh_frame(toplevel);
}

/* After the window was placed again (configured), it glides there from `from` (morph_from's,
 * taken before) with the duration and curve of `kind`, its size too when that changed: from
 * the size it was drawn at to the one it was given, the client's picture stretched until it
 * draws that. A window not on the screen just takes its place. */
void toplevel_morph(struct sh_toplevel *toplevel, struct wlr_box from, enum sh_anim_kind kind) {
    if (!toplevel->scene_tree || !toplevel->shown || !toplevel_mapped(toplevel) ||
        !toplevel_visible(toplevel))
        return;
    struct wlr_scene_node *node = &toplevel->scene_tree->node;
    struct wlr_box to = toplevel_box(toplevel);
    // It starts where it was drawn: what it is drawn off where it rests now already (a placement
    // within this one may have started a glide) does not count twice.
    int dx = from.x - node->x - toplevel->content->node.x;
    int dy = from.y - node->y - toplevel->content->node.y;
    struct sh_anim *anim = &toplevel->anim;
    bool sized = from.width != to.width || from.height != to.height;
    // Placed where it is going already, as a reflow places it again and again.
    if (dx == 0 && dy == 0 &&
        (anim->morph ? anim->rest_width == to.width && anim->rest_height == to.height : !sized))
        return;
    int live_width, live_height;
    live_size(toplevel, &live_width, &live_height);
    uint32_t serial = pending_configure(toplevel);
    if (!anim->morph && !sized && !serial) {
        sh_anim_glide_kind(toplevel->server->animator, anim, toplevel->content, dx, dy, kind);
        return;
    }
    toplevel->morph_serial = serial;
    struct sh_morph morph = {
        .surfaces = toplevel->surfaces,
        .dx = dx,
        .dy = dy,
        .from_width = from.width,
        .from_height = from.height,
        .width = to.width,
        .height = to.height,
        .live_width = live_width,
        .live_height = live_height,
        .answered = answered(toplevel, live_width, live_height),
        .kind = kind,
        .resized = morph_resized,
        .data = toplevel,
    };
    sh_anim_morph(toplevel->server->animator, anim, toplevel->content, &morph);
}

/* The client commits: the morph hears what it drew, before the scene takes it in, and a toy's
 * stretch reads it afresh (toys.c). */
void morph_commit(struct sh_toplevel *toplevel) {
    sh_anim_pose_commit(&toplevel->anim, toplevel->surfaces);
    if (!toplevel->anim.morph)
        return;
    int width, height;
    live_size(toplevel, &width, &height);
    sh_anim_morph_commit(toplevel->server->animator, &toplevel->anim, width, height,
                         answered(toplevel, width, height));
}

/* The window's geometry as the frame is drawn around it: at the size a morph draws it. */
struct wlr_box frame_geometry(struct sh_toplevel *toplevel) {
    struct wlr_box box = toplevel_geometry(toplevel);
    sh_anim_drawn_size(&toplevel->anim, &box.width, &box.height);
    return box;
}

/* For `get drawn`: "rest", or a morph "moving" there, "waiting" for the client to draw the new
 * size, or "fading" what it drew before out. */
const char *morph_state(struct sh_toplevel *toplevel) {
    const struct sh_anim *anim = &toplevel->anim;
    if (!anim->animator || !anim->morph)
        return "rest";
    return anim->glide ? "moving" : !anim->answered ? "waiting" : "fading";
}
