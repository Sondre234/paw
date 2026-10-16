/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Borders in a gradient (windows.border_color or border_inactive_color one, or a window rule's):
 * small buffers painted by src/border.c, a strip along each side the scene stretches and a square
 * at each corner, rounded as the window is, in a tree of their own in place of the border's
 * rects. */
#include "server.h"

static void remove_gradient_border(struct sh_toplevel *toplevel) {
    if (toplevel->gradient)
        wlr_scene_node_destroy(&toplevel->gradient->node);
    toplevel->gradient = NULL;
    memset(toplevel->gradient_pieces, 0, sizeof(toplevel->gradient_pieces));
    memset(&toplevel->gradient_drawn, 0, sizeof(toplevel->gradient_drawn));
}

/* A rounded corner reaches into the window's geometry, where the pointer belongs to the window,
 * as it does through the hole of a rounded border's rect. */
static bool corner_accepts_input(struct wlr_scene_buffer *buffer, double *sx, double *sy) {
    struct sh_node *owner = scene_owner(&buffer->node);
    if (!owner || owner->kind != SH_NODE_TOPLEVEL)
        return true;
    struct sh_toplevel *toplevel = owner->owner;
    double x = buffer->node.x + *sx, y = buffer->node.y + *sy;
    return x < 0 || y < 0 || x >= toplevel->gradient_drawn.width ||
           y >= toplevel->gradient_drawn.height;
}

/* Draws the window's border `border` pixels wide around its geometry, with corners of `radius`,
 * in the inactive gradient `from` crossfading into the active one `to` by `mix`, at `opacity`; or
 * removes it without `on`. Pieces are painted again only when what they show changed. */
void refresh_gradient_border(struct sh_toplevel *toplevel, bool on, int border, int radius,
                             double mix, float opacity, const struct sh_gradient *from,
                             const struct sh_gradient *to) {
    struct sh_server *server = toplevel->server;
    struct wlr_box g = frame_geometry(toplevel);
    if (!on || border <= 0 || g.width < 1 || g.height < 1) {
        remove_gradient_border(toplevel);
        return;
    }
    if (!toplevel->gradient) {
        toplevel->gradient = wlr_scene_tree_create(toplevel->content);
        if (!toplevel->gradient)
            return;
        for (int i = 0; i < SH_BORDER_PIECES; ++i) {
            struct wlr_scene_buffer *piece = wlr_scene_buffer_create(toplevel->gradient, NULL);
            if (!piece) {
                remove_gradient_border(toplevel);
                return;
            }
            if (i >= 4) // the corners
                piece->point_accepts_input = corner_accepts_input;
            toplevel->gradient_pieces[i] = piece;
        }
    }
    for (int i = 0; i < SH_BORDER_PIECES; ++i)
        wlr_scene_buffer_set_opacity(toplevel->gradient_pieces[i], opacity);
    int scale = pixel_scale(server);
    struct sh_gradient_drawn drawn = {g.width, g.height, border, radius, scale, (float)mix,
                                      *from,   *to};
#ifdef PAW_ROUNDED_CORNERS
    // The window's rounded clip would take the border, which lies outside its geometry, so the
    // pieces get a clip of their own around the whole frame, as the shadow does; the corners
    // round themselves. It follows the window's lift whatever the pieces show.
    window_clip(toplevel, SH_CLIP_GRADIENT,
                &(struct wlr_box){-border, -border, g.width + 2 * border, g.height + 2 * border},
                0);
#endif
    if (!memcmp(&drawn, &toplevel->gradient_drawn, sizeof(drawn)))
        return;
    toplevel->gradient_drawn = drawn;
    struct sh_border_piece pieces[SH_BORDER_PIECES];
    sh_border_pieces(g.width, g.height, border, radius, scale, pieces);
    for (int i = 0; i < SH_BORDER_PIECES; ++i) {
        struct wlr_scene_buffer *node = toplevel->gradient_pieces[i];
        const struct sh_border_piece *piece = &pieces[i];
        wlr_scene_node_set_enabled(&node->node, piece->width > 0);
        if (piece->width <= 0)
            continue;
        uint32_t *pixels =
            malloc((size_t)piece->buffer_width * piece->buffer_height * sizeof(*pixels));
        if (!pixels)
            continue;
        sh_border_paint(pixels, piece, g.width, g.height, border, radius, scale, from, to, mix);
        struct wlr_buffer *buffer = sh_pixel_buffer(pixels, piece->buffer_width, piece->buffer_height);
        if (!buffer)
            continue; // it freed the pixels
        wlr_scene_buffer_set_buffer(node, buffer);
        wlr_buffer_drop(buffer); // the scene holds it now
        wlr_scene_buffer_set_dest_size(node, piece->width, piece->height);
        wlr_scene_node_set_position(&node->node, piece->x, piece->y);
    }
}
