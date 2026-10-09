/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* What the compositor draws on and around a window: its border, opacity, rounded corners and
 * shadow, the window controls (and the xdg-decoration requests that ask for them), and a
 * group's tab strip. */
#include "server.h"

static void fade_update(void *data);

/* Windows are decorated by the server (no title bar, just the window controls) unless they ask to draw
 * their own frame, as frameless Electron apps like Discord do. */
enum wlr_xdg_toplevel_decoration_v1_mode
decoration_mode(struct wlr_xdg_toplevel_decoration_v1 *decoration) {
    return decoration->requested_mode == WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
               ? WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
               : WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
}

bool wants_decoration(struct sh_toplevel *toplevel) {
    if (toplevel->decoration)
        return toplevel->decoration->current.mode ==
               WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
#if WLR_HAS_XWAYLAND
    // X11 windows that leave decorations to the window manager (Spotify, for one).
    return toplevel->xsurface && !toplevel->unmanaged &&
           toplevel->xsurface->decorations == WLR_XWAYLAND_SURFACE_DECORATIONS_ALL;
#else
    return false;
#endif
}

/* Pixels per logical pixel of what the compositor draws itself: enough for the densest output. */
int pixel_scale(struct sh_server *server) {
    float scale = 1;
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        if (output->wlr_output->scale > scale)
            scale = output->wlr_output->scale;
    }
    return (int)ceilf(scale);
}

/* The style of window controls windows.controls asks for. */
enum sh_deco_style deco_style(struct sh_server *server) {
    return server_settings(server)->window_controls == SH_CONTROLS_TRAFFIC_LIGHTS
               ? SH_DECO_TRAFFIC_LIGHTS
               : SH_DECO_FLAT;
}

/* Whether the window draws around its geometry, as a client-side frame draws its shadow: its
 * surface reaches past the geometry it gives. */
static bool draws_own_shadow(struct sh_toplevel *toplevel) {
    if (!toplevel->xdg_toplevel || wants_decoration(toplevel))
        return false;
    struct wlr_xdg_surface *base = toplevel->xdg_toplevel->base;
    struct wlr_box g = base->geometry;
    return g.x > 0 || g.y > 0 || g.x + g.width < base->surface->current.width ||
           g.y + g.height < base->surface->current.height;
}

/* The shared buffer for a look of the controls, drawn when first needed and again once an
 * output's scale asks for more pixels. */
static struct wlr_buffer *deco_buffer(struct sh_server *server, struct sh_deco_look look) {
    int scale = pixel_scale(server);
    if (scale != server->deco_scale) {
        for (size_t i = 0; i < sizeof(server->deco_buffers) / sizeof(*server->deco_buffers); ++i) {
            wlr_buffer_drop(server->deco_buffers[i]); // windows showing one keep it till replaced
            server->deco_buffers[i] = NULL;
        }
        server->deco_scale = scale;
    }
    if (look.style == SH_DECO_FLAT) // the strip shows neither focus nor a press
        look.focused = true, look.pressed = SH_DECO_NONE;
    enum { PARTS = SH_DECO_FULLSCREEN + 1 };
    size_t index = (((size_t)look.style * 2 + look.focused) * PARTS + look.hovered) * PARTS +
                   look.pressed;
    if (!server->deco_buffers[index])
        server->deco_buffers[index] = sh_decoration_render(scale, &look);
    return server->deco_buffers[index];
}

/* The controls' place inside the window's scene tree: the flat strip at its top-right corner,
 * the traffic lights at its top-left. */
static void deco_position(struct sh_toplevel *toplevel, enum sh_deco_style style, int *x,
                          int *y) {
    struct wlr_box geometry = toplevel_geometry(toplevel);
    if (style == SH_DECO_TRAFFIC_LIGHTS) {
        *x = geometry.x;
        *y = geometry.y;
        return;
    }
    *x = geometry.x + geometry.width - SH_DECO_MARGIN - SH_DECO_WIDTH;
    if (*x < geometry.x + SH_DECO_MARGIN)
        *x = geometry.x + SH_DECO_MARGIN;
    *y = geometry.y + SH_DECO_MARGIN;
}

/* The controls sit over the window's content, so they hide until the pointer nears its corner. */
bool in_deco_corner(struct sh_toplevel *toplevel, double x, double y) {
    enum sh_deco_style style = deco_style(toplevel->server);
    int dx, dy, width, height;
    deco_position(toplevel, style, &dx, &dy);
    sh_decoration_size(style, &width, &height);
    double left = toplevel->scene_tree->node.x + dx - SH_DECO_MARGIN;
    double top = toplevel->scene_tree->node.y + dy - SH_DECO_MARGIN;
    return x >= left && y >= top && x < left + 2 * SH_DECO_MARGIN + width &&
           y < top + 2 * SH_DECO_MARGIN + height;
}

/* Traffic lights take the pointer only on and around their circles; between and below them it
 * reaches the window. */
static bool lights_accept_input(struct wlr_scene_buffer *buffer, double *sx, double *sy) {
    return sh_decoration_part_at(SH_DECO_TRAFFIC_LIGHTS, *sx, *sy) != SH_DECO_NONE;
}

/* Keeps the controls and the border above the window's surfaces. A new node starts on top and
 * the surfaces sit in one subtree, so a frame node is out of place only when something else
 * was stacked over all of them; raising one that is already fine would still make the scene
 * recompute the window's whole tree on every commit. */
static void raise_frame_node(struct sh_toplevel *toplevel, struct wlr_scene_node *node) {
    struct wl_list *children = &toplevel->content->children;
    struct wlr_scene_node *top = wl_container_of(children->prev, top, link);
    if (top == node || (toplevel->deco && top == &toplevel->deco->node) ||
        (toplevel->gradient && top == &toplevel->gradient->node))
        return;
    for (int i = 0; i < 4; ++i)
        if (toplevel->border[i] && top == &toplevel->border[i]->node)
            return;
    wlr_scene_node_raise_to_top(node);
}

/* Adds, removes, or updates a window's controls to match what it asks for and its state. */
void refresh_decoration(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    if (!toplevel->scene_tree)
        return;
    if (!toplevel_mapped(toplevel) || !wants_decoration(toplevel)) {
        if (toplevel->deco)
            wlr_scene_node_destroy(&toplevel->deco->node);
        toplevel->deco = NULL;
        return;
    }
    struct sh_deco_look look = {
        .style = deco_style(server),
        .focused = server->focused_toplevel == toplevel,
        .hovered = server->deco_hovered == toplevel ? server->deco_hovered_part : SH_DECO_NONE,
    };
    // A held button looks pressed while the pointer stays on it.
    if (server->deco_pressed == toplevel && look.hovered == server->deco_pressed_part)
        look.pressed = server->deco_pressed_part;
    struct wlr_buffer *buffer = deco_buffer(server, look);
    if (!buffer)
        return;
    if (!toplevel->deco) {
        toplevel->deco = wlr_scene_buffer_create(toplevel->content, buffer);
        if (!toplevel->deco)
            return;
    } else if (toplevel->deco->buffer != buffer) {
        wlr_scene_buffer_set_buffer(toplevel->deco, buffer);
    }
    int width, height;
    sh_decoration_size(look.style, &width, &height);
    wlr_scene_buffer_set_dest_size(toplevel->deco, width, height);
    toplevel->deco->point_accepts_input =
        look.style == SH_DECO_TRAFFIC_LIGHTS ? lights_accept_input : NULL;
    int x, y;
    deco_position(toplevel, look.style, &x, &y);
    wlr_scene_node_set_position(&toplevel->deco->node, x, y);
    raise_frame_node(toplevel, &toplevel->deco->node);
    wlr_scene_node_set_enabled(&toplevel->deco->node, server->deco_revealed == toplevel);
}

static bool no_input(struct wlr_scene_buffer *buffer, double *sx, double *sy) { return false; }

static bool same_look(const struct sh_shadow *a, const struct sh_shadow *b) {
    return a->radius == b->radius && a->blur == b->blur && a->offset_x == b->offset_x &&
           a->offset_y == b->offset_y && !memcmp(a->color, b->color, sizeof(a->color));
}

/* The shadow image of a look for windows of `width` by `height` (sh_shadow_image_size's) at
 * the scale the outputs ask for, painted when first needed. */
static struct sh_shadow_image *shadow_image(struct sh_server *server, const struct sh_shadow *look,
                                            int width, int height) {
    int scale = pixel_scale(server);
    struct sh_shadow_image *images = server->shadow_images, *oldest = &images[0];
    size_t count = sizeof(server->shadow_images) / sizeof(*server->shadow_images);
    for (size_t i = 0; i < count; ++i) {
        struct sh_shadow_image *image = &images[i];
        if (image->buffer && image->scale == scale && image->width == width &&
            image->height == height && same_look(&image->look, look)) {
            image->used = ++server->shadow_uses;
            return image;
        }
        if (oldest->buffer && (!image->buffer || image->used < oldest->used))
            oldest = image;
    }
    struct sh_shadow_layout layout;
    sh_shadow_layout(look, &layout);
    int pixel_width = (layout.left + width + layout.right) * scale;
    int pixel_height = (layout.top + height + layout.bottom) * scale;
    uint32_t *pixels = malloc((size_t)pixel_width * pixel_height * sizeof(*pixels));
    if (!pixels || !sh_shadow_paint(pixels, look, width, height, scale)) {
        free(pixels);
        return NULL;
    }
    struct wlr_buffer *buffer = sh_pixel_buffer(pixels, pixel_width, pixel_height);
    if (!buffer)
        return NULL; // it freed the pixels
    wlr_buffer_drop(oldest->buffer); // windows showing it keep it until they change
    *oldest = (struct sh_shadow_image){*look, width, height, scale, buffer, ++server->shadow_uses};
    return oldest;
}

static void remove_shadow(struct sh_toplevel *toplevel) {
    if (toplevel->shadow)
        wlr_scene_node_destroy(&toplevel->shadow->node);
    toplevel->shadow = NULL;
    memset(toplevel->shadow_slices, 0, sizeof(toplevel->shadow_slices));
    toplevel->shadow_image = NULL;
}

/* The shadow under a window whose frame reaches `outset` past its geometry (its border) with
 * corners of `radius`: slices of a shared image laid out around the frame, in a tree at the
 * bottom of the window's. It takes no input, and nothing that places or measures the window
 * counts it. The tree has a clip of its own as large as the shadow, since the one rounding the
 * window would hide it (only the nearest clip applies). Laid out again only when the frame's
 * size or the image changes, so a running animation keeps what it set. */
static void refresh_shadow(struct sh_toplevel *toplevel, bool on, int outset, int radius) {
    struct sh_server *server = toplevel->server;
    const struct sh_settings *settings = server_settings(server);
    struct wlr_box g = toplevel_geometry(toplevel);
    int width = g.width + 2 * outset, height = g.height + 2 * outset;
    if (!on || width < 1 || height < 1) {
        remove_shadow(toplevel);
        return;
    }
    struct sh_shadow look = {
        .radius = radius > 0 ? radius + outset : 0,
        .blur = settings->shadow_blur,
        .offset_x = settings->shadow_x,
        .offset_y = settings->shadow_y,
    };
    bool focused = server->focused_toplevel == toplevel;
    memcpy(look.color, focused ? settings->shadow_color : settings->shadow_inactive_color,
           sizeof(look.color));
    struct sh_shadow_layout layout;
    sh_shadow_layout(&look, &layout);
    int image_width, image_height;
    sh_shadow_image_size(&layout, width, height, &image_width, &image_height);
    struct sh_shadow_image *image = shadow_image(server, &look, image_width, image_height);
    if (!image)
        return;
    if (!toplevel->shadow) {
        toplevel->shadow = wlr_scene_tree_create(toplevel->content);
        if (!toplevel->shadow)
            return;
        wlr_scene_node_lower_to_bottom(&toplevel->shadow->node);
        for (int i = 0; i < SH_SHADOW_SLICES; ++i) {
            struct wlr_scene_buffer *slice = wlr_scene_buffer_create(toplevel->shadow, NULL);
            if (!slice) {
                remove_shadow(toplevel);
                return;
            }
            slice->point_accepts_input = no_input;
            wlr_scene_buffer_set_opacity(slice, toplevel->opacity);
            toplevel->shadow_slices[i] = slice;
        }
    }
    wlr_scene_node_set_position(&toplevel->shadow->node, -outset, -outset);
    toplevel->shadow_box = (struct wlr_box){-outset - layout.left, -outset - layout.top,
                                            layout.left + width + layout.right,
                                            layout.top + height + layout.bottom};
    toplevel->shadow_alpha = look.color[3];
#ifdef PAW_ROUNDED_CORNERS
    wlr_scene_tree_set_rounded_clip(toplevel->shadow,
                                    &(struct wlr_box){-layout.left, -layout.top,
                                                      toplevel->shadow_box.width,
                                                      toplevel->shadow_box.height},
                                    0);
#endif
    if (toplevel->shadow_image == image->buffer && toplevel->shadow_width == width &&
        toplevel->shadow_height == height)
        return;
    toplevel->shadow_image = image->buffer;
    toplevel->shadow_width = width;
    toplevel->shadow_height = height;
    struct sh_shadow_slice slices[SH_SHADOW_SLICES];
    sh_shadow_slices(&layout, image_width, image_height, width, height, slices);
    for (int i = 0; i < SH_SHADOW_SLICES; ++i) {
        struct wlr_scene_buffer *slice = toplevel->shadow_slices[i];
        const struct sh_shadow_slice *s = &slices[i];
        wlr_scene_node_set_enabled(&slice->node, s->width > 0);
        if (s->width <= 0)
            continue;
        if (slice->buffer != image->buffer)
            wlr_scene_buffer_set_buffer(slice, image->buffer);
        int scale = image->scale;
        wlr_scene_buffer_set_source_box(slice, &(struct wlr_fbox){s->x * scale, s->y * scale,
                                                                  s->width * scale,
                                                                  s->height * scale});
        wlr_scene_buffer_set_dest_size(slice, s->to_width, s->to_height);
        wlr_scene_node_set_position(&slice->node, s->to_x, s->to_y);
    }
}

/* The tab strip of a group's shown window: one segment per member, the shown one lit. */
void refresh_tabs(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    if (!toplevel->scene_tree)
        return;
    if (!toplevel->group || toplevel->group_hidden || !toplevel_mapped(toplevel) ||
        toplevel->fullscreen || !groups_enabled(server)) {
        if (toplevel->tabs)
            wlr_scene_node_destroy(&toplevel->tabs->node);
        toplevel->tabs = NULL;
        return;
    }
    struct wlr_box g = toplevel_geometry(toplevel);
    int count = group_size(server, toplevel->group), active = group_index(toplevel);
    int hover = server->tabs_hovered == toplevel ? server->tabs_hovered_index : -1;
    int scale = pixel_scale(server);
    if (g.width < 1)
        return;
    if (!toplevel->tabs || toplevel->tabs_width != g.width || toplevel->tabs_count != count ||
        toplevel->tabs_active != active || toplevel->tabs_hover != hover ||
        toplevel->tabs_scale != scale) {
        size_t pixels = (size_t)g.width * scale * SH_TABS_HEIGHT * scale;
        uint32_t *data = calloc(pixels, sizeof(*data));
        if (!data)
            return;
        sh_tabs_paint(data, g.width, scale, count, active, hover);
        struct wlr_buffer *buffer =
            sh_pixel_buffer(data, g.width * scale, SH_TABS_HEIGHT * scale);
        if (!buffer)
            return; // it freed the pixels
        if (!toplevel->tabs)
            toplevel->tabs = wlr_scene_buffer_create(toplevel->content, buffer);
        else
            wlr_scene_buffer_set_buffer(toplevel->tabs, buffer);
        wlr_buffer_drop(buffer); // the scene holds it now
        if (!toplevel->tabs)
            return;
        toplevel->tabs_width = g.width;
        toplevel->tabs_count = count;
        toplevel->tabs_active = active;
        toplevel->tabs_hover = hover;
        toplevel->tabs_scale = scale;
        wlr_scene_buffer_set_dest_size(toplevel->tabs, g.width, SH_TABS_HEIGHT);
    }
    wlr_scene_node_set_position(&toplevel->tabs->node, g.x, g.y);
    wlr_scene_node_raise_to_top(&toplevel->tabs->node);
}

static void set_buffer_opacity(struct wlr_scene_buffer *buffer, int sx, int sy, void *data) {
    struct sh_toplevel *toplevel = data;
    // The border keeps its own colours, as its rects do.
    if (buffer != toplevel->deco && buffer != toplevel->dim && buffer != toplevel->tabs &&
        (!toplevel->gradient || buffer->node.parent != toplevel->gradient))
        wlr_scene_buffer_set_opacity(buffer, toplevel->opacity);
}

static float rule_opacity(struct sh_toplevel *toplevel, const char *app_id, const char *title,
                          bool active) {
    struct sh_server *server = toplevel->server;
    app_id = app_id ? app_id : "";
    title = title ? title : "";
    struct sh_opacity_rule *cache = &toplevel->opacity_rule;
    if (cache->generation == server->config_generation && cache->active == active &&
        cache->app_id && cache->title && !strcmp(cache->app_id, app_id) &&
        !strcmp(cache->title, title))
        return cache->value;
    char *app_id_copy = strdup(app_id), *title_copy = strdup(title);
    ++server->stats.opacity_rules;
    cache->value = server->callbacks->opacity(server->callbacks->userdata, app_id, title, active);
    if (!app_id_copy || !title_copy) { // out of memory: leave the cache unusable
        free(app_id_copy);
        free(title_copy);
        app_id_copy = title_copy = NULL;
    }
    free(cache->app_id);
    free(cache->title);
    cache->app_id = app_id_copy;
    cache->title = title_copy;
    cache->active = active;
    cache->generation = server->config_generation;
    return cache->value;
}

/* The border around the window's geometry and its opacity, both following focus. Called on
 * every commit, since the geometry and the set of surfaces can change with any of them. */
void refresh_frame(struct sh_toplevel *toplevel) {
    if (!toplevel->scene_tree)
        return;
    overview_touch(toplevel->server, false); // a thumbnail of it may need copying again
#if WLR_HAS_XWAYLAND
    if (toplevel->unmanaged)
        return;
#endif
    struct sh_server *server = toplevel->server;
    const struct sh_settings *settings = server_settings(server);
    bool mapped = toplevel_mapped(toplevel);
    bool active = server->focused_toplevel == toplevel;
    const char *app_id = toplevel_app_id(toplevel), *title = toplevel_title(toplevel);
    float opacity =
        toplevel->fullscreen || !mapped ? 1 : rule_opacity(toplevel, app_id, title, active);
    // Opacity and border color fade when focus changes; a window that is not yet shown, or
    // was hidden, takes them at once.
    int b = settings->border_width;
    // An urgent window is framed in its color even without a border: inside its edges, so the
    // layout does not move.
    bool urgent = toplevel->urgent && !active;
    bool inset = urgent && b == 0;
    bool shown = (b > 0 || inset) && mapped && !frameless(toplevel, toplevel_output(toplevel));
    const float *color = urgent ? settings->urgent_color
                                : active ? settings->border_active : settings->border_inactive;
    // A border in gradients eases from the inactive one to the active one rather than between
    // colours; going from one kind to the other, it takes the new one at once.
    bool gradient = shown && !urgent && gradient_borders(server);
    float target[SH_TWEEN_VALUES] = {opacity}, fading[SH_TWEEN_VALUES];
    if (gradient)
        target[1] = active;
    else if (shown)
        memcpy(target + 1, color, 4 * sizeof(float));
    sh_tween_track(server->animator, &toplevel->fade, SH_ANIM_FOCUS,
                   mapped && toplevel->shown && toplevel_visible(toplevel) &&
                       toplevel->border_gradient == gradient,
                   target, fading, fade_update, toplevel);
    toplevel->border_gradient = gradient;
    double mix = fading[1];
    // Peeking scales the opacity, fullscreen windows included, but for a window peeked at. One
    // on the screen only for a peek fades in and out whole, its controls, tabs and border too.
    float peeked = peek_scale(toplevel, now_ms());
    float whole = shown_for_peek(toplevel) ? peeked : 1;
    opacity = fading[0] * peeked;
    color = fading + 1;
    float pulsing[4];
    if (urgent || whole < 1) {
        // The border pulses over its fade to the urgent color; premultiplied, scaling all four
        // channels dims it.
        float pulse = urgent ? urgent_pulse(toplevel, now_ms()) : 1;
        for (int i = 0; i < 4; ++i)
            pulsing[i] = color[i] * pulse * whole;
        color = pulsing;
    }
    // New subsurfaces start opaque, so a translucent window is revisited on every commit.
    if (opacity != toplevel->opacity || opacity < 1) {
        toplevel->opacity = opacity;
        wlr_scene_node_for_each_buffer(&toplevel->scene_tree->node, set_buffer_opacity, toplevel);
    }
    update_dim(toplevel);
    refresh_decoration(toplevel); // the controls follow the window's width
    refresh_tabs(toplevel);
    if (toplevel->deco)
        wlr_scene_buffer_set_opacity(toplevel->deco, whole);
    if (toplevel->tabs)
        wlr_scene_buffer_set_opacity(toplevel->tabs, whole);

    // Windows on an output that tiles get rounded corners, floating ones too, and with
    // windows.round = "always" so does every other window but one that draws a shadow of its
    // own, which has corners of its own too (and would lose that shadow to the clip). The clip
    // takes in everything drawn for them but the border, which rounds itself to match.
    struct wlr_box g = toplevel_geometry(toplevel);
    struct wlr_output *output = toplevel_output(toplevel);
    bool rounded = mapped && !frameless(toplevel, output) &&
                   (tiles_for(toplevel, output) ||
                    (settings->round_always && !draws_own_shadow(toplevel)));
    int radius = rounded ? settings->corner_radius : 0;
#ifdef PAW_ROUNDED_CORNERS
    wlr_scene_tree_set_rounded_clip(
        toplevel->content, radius > 0 ? &(struct wlr_box){0, 0, g.width, g.height} : NULL, radius);
#else
    radius = 0;
#endif
    toplevel->corner_radius = radius;
    // A shadow under every window that has none of its own, or whose own the clip cut away;
    // none under a fullscreen or maximized one.
    refresh_shadow(toplevel,
                   settings->shadow && mapped && !frameless(toplevel, output) &&
                       (radius > 0 || !draws_own_shadow(toplevel)),
                   shown && !inset ? b : 0, radius);

    // xdg-shell windows keep their scene tree while unmapped; the border must not. One in
    // gradients is drawn in pieces of its own instead.
    refresh_gradient_border(toplevel, gradient, b, radius, mix, whole);
    if (toplevel->gradient)
        raise_frame_node(toplevel, &toplevel->gradient->node);
    for (int i = 0; i < 4; ++i) {
        if (!shown || gradient) {
            if (toplevel->border[i])
                wlr_scene_node_destroy(&toplevel->border[i]->node);
            toplevel->border[i] = NULL;
            toplevel->frame_hole = 0;
            continue;
        }
        if (!toplevel->border[i])
            toplevel->border[i] = wlr_scene_rect_create(toplevel->content, 1, 1, color);
        if (!toplevel->border[i])
            return;
        wlr_scene_rect_set_color(toplevel->border[i], color);
    }
    if (!shown || gradient)
        return;
    // The scene tree's origin is the top-left corner of the window geometry.
    if (inset)
        b = g.width < 8 || g.height < 8 ? 1 : 2;
    const struct wlr_box outside[4] = {{-b, -b, g.width + 2 * b, b},
                                       {-b, g.height, g.width + 2 * b, b},
                                       {-b, 0, b, g.height},
                                       {g.width, 0, b, g.height}};
    const struct wlr_box inside[4] = {{0, 0, g.width, b},
                                      {0, g.height - b, g.width, b},
                                      {0, b, b, g.height - 2 * b},
                                      {g.width - b, b, b, g.height - 2 * b}};
    const struct wlr_box *sides = inset ? inside : outside;
    // Around rounded corners the first side is the whole frame, a hollow rounded rect whose
    // inner edge follows the window's corners; the other three are not needed.
    const struct wlr_box frame[4] = {
        inset ? (struct wlr_box){0, 0, g.width, g.height}
              : (struct wlr_box){-b, -b, g.width + 2 * b, g.height + 2 * b}};
    if (radius > 0)
        sides = frame;
    toplevel->frame_hole = radius > 0 ? b : 0;
    for (int i = 0; i < 4; ++i) {
        wlr_scene_node_set_position(&toplevel->border[i]->node, sides[i].x, sides[i].y);
        wlr_scene_rect_set_size(toplevel->border[i], sides[i].width, sides[i].height);
#ifdef PAW_ROUNDED_CORNERS
        wlr_scene_rect_set_rounding(toplevel->border[i],
                                    i == 0 && radius > 0 ? radius + (inset ? 0 : b) : 0,
                                    i == 0 && radius > 0 ? b : 0);
#endif
        raise_frame_node(toplevel, &toplevel->border[i]->node);
    }
}

void forget_decoration(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    if (server->deco_hovered == toplevel)
        server->deco_hovered = NULL;
    if (server->deco_revealed == toplevel)
        server->deco_revealed = NULL;
    if (server->deco_pressed == toplevel)
        server->deco_pressed = NULL;
    if (server->tabs_hovered == toplevel)
        server->tabs_hovered = NULL;
    if (toplevel->xdg_toplevel && toplevel->tabs)
        wlr_scene_node_destroy(&toplevel->tabs->node);
    toplevel->tabs = NULL;
    // X11 windows lose it with their scene tree; xdg-shell ones keep theirs while unmapped.
    if (toplevel->xdg_toplevel && toplevel->deco)
        wlr_scene_node_destroy(&toplevel->deco->node);
    toplevel->deco = NULL;
}

static void fade_update(void *data) {
    refresh_frame(data);
}

/* The mode is sent with the first configure, or right away once the window has had one. */
static void decoration_set_mode(struct sh_toplevel *toplevel) {
    if (toplevel->xdg_toplevel->base->initialized)
        wlr_xdg_toplevel_decoration_v1_set_mode(toplevel->decoration,
                                                decoration_mode(toplevel->decoration));
}

static void decoration_request_mode(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, decoration_mode);
    decoration_set_mode(toplevel);
}

static void decoration_destroy(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, decoration_destroy);
    wl_list_remove(&toplevel->decoration_mode.link);
    wl_list_remove(&toplevel->decoration_destroy.link);
    toplevel->decoration = NULL;
    refresh_decoration(toplevel);
}

void server_new_decoration(struct wl_listener *listener, void *data) {
    struct wlr_xdg_toplevel_decoration_v1 *decoration = data;
    struct wlr_scene_tree *tree = decoration->toplevel->base->data;
    struct sh_node *node = tree ? tree->node.data : NULL;
    if (!node || node->kind != SH_NODE_TOPLEVEL)
        return;
    struct sh_toplevel *toplevel = node->owner;
    toplevel->decoration = decoration;
    add_listener(&decoration->events.request_mode, &toplevel->decoration_mode,
                 decoration_request_mode);
    add_listener(&decoration->events.destroy, &toplevel->decoration_destroy, decoration_destroy);
    decoration_set_mode(toplevel);
}
