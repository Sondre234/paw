/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The exploded view (paw xray): the focused output comes apart into the layers paw draws, bottom
 * to top (the background colour and the background layer, the bottom layer, the windows, the
 * floating ones, those kept above, fullscreen ones, the panels, X11's menus, the overlays), each
 * a sheet fanned out from the next like a deck of cards seen from the side, so that what lies
 * over what shows. A sheet is a live copy of its layer's surfaces on that output (overview_scene.h),
 * scaled down and cut to the output, over a card in its layer's colour, with a shadow and an
 * outline; a window has a sheet of its own while few show. src/xray_layout.c says where each one
 * goes. The pointer turns the stack and the wheel spreads it; the sheet under the pointer is
 * lifted, those over it fade, and the shell names it from the lines xray_announce sends. It
 * takes the keyboard and the pointer while open, as the overview does, and changes nothing but
 * the focus, to a window clicked; closed, nothing of it is left in the scene and nothing runs. */
#include "server.h"

/* The shell's surface with the view's text, which no sheet shows. */
#define XRAY_NAMESPACE "paw-xray"
/* Windows shown each on a sheet of its own at most; more share their layer's. */
enum { XRAY_APART = 6, XRAY_ENTRIES = 256 };

enum xray_layer {
    XRAY_BACKGROUND,
    XRAY_BOTTOM,
    XRAY_WINDOWS,
    XRAY_FLOATING,
    XRAY_ABOVE,
    XRAY_FULLSCREEN,
    XRAY_PEEK,
    XRAY_TOP,
    XRAY_COVER,
    XRAY_X11,
    XRAY_OVERLAY,
    XRAY_LAYERS
};
/* Their names, as `get xray` and the shell give them, and the colour of each one's sheets. */
static const struct {
    const char *name;
    uint32_t colour; /* 0xRRGGBB */
} xray_layers[XRAY_LAYERS] = {
    [XRAY_BACKGROUND] = {"background", 0x5b8def}, [XRAY_BOTTOM] = {"bottom", 0x2fb5a8},
    [XRAY_WINDOWS] = {"windows", 0x8cc152},       [XRAY_FLOATING] = {"floating", 0xf0b429},
    [XRAY_ABOVE] = {"above", 0xf47c3c},           [XRAY_FULLSCREEN] = {"fullscreen", 0xe8505b},
    [XRAY_PEEK] = {"peek", 0xc678dd},             [XRAY_TOP] = {"top", 0xa98bff},
    [XRAY_COVER] = {"fullscreen_cover", 0xff6b81}, [XRAY_X11] = {"x11", 0x9aa5b1},
    [XRAY_OVERLAY] = {"overlay", 0xff79c6},
};

enum xray_kind { XRAY_COLOUR, XRAY_SURFACE, XRAY_WINDOW, XRAY_MENU };
static const char *const xray_kinds[] = {"colour", "surface", "window", "x11"};

/* Something drawn on an output, as one sheet shows it. */
struct xray_entry {
    enum xray_layer layer;
    enum xray_kind kind;
    struct wlr_scene_tree *tree; /* what its sheet copies; NULL for the colour */
    struct sh_toplevel *window;  /* a window's or an X11 menu's, else NULL */
    struct sh_layer *surface;    /* a layer surface's, else NULL */
    struct wlr_box box;          /* where it is, in layout coordinates */
    int sheet;                   /* the sheet showing it, -1 for none */
};

/* The sheets' shadow: soft, falling a little down, as a window's does. */
static const struct sh_shadow xray_shadow = {
    .radius = 0, .blur = 36, .offset_x = 0, .offset_y = 14, .color = {0, 0, 0, 0.5f}};

static double clamp01(double value) {
    return value < 0 ? 0 : value > 1 ? 1 : value;
}

static uint64_t xray_mix(uint64_t hash, uint64_t value) {
    return hash ^ (value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2));
}

static uint64_t xray_bits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

/* A colour of 0xRRGGBB at alpha `a`, premultiplied as scene rectangles take it; never quite
 * opaque, since the scene hides what is under an opaque node and its surfaces then get no frame
 * callbacks, which would stop the very copies the sheets show. */
static void xray_colour(float out[4], uint32_t rgb, double a) {
    a = fmin(a, 0.9999);
    out[0] = (float)(((rgb >> 16) & 0xff) / 255.0 * a);
    out[1] = (float)(((rgb >> 8) & 0xff) / 255.0 * a);
    out[2] = (float)((rgb & 0xff) / 255.0 * a);
    out[3] = (float)a;
}

static bool no_input(struct wlr_scene_buffer *buffer, double *sx, double *sy) {
    return false;
}

/* The layer surface `node` draws, when it is shown on `output` and is not the shell's text. */
static struct sh_layer *xray_surface(struct wlr_scene_node *node, struct wlr_output *output) {
    struct sh_node *owner = node->data;
    if (!node->enabled || !owner || owner->kind != SH_NODE_LAYER)
        return NULL;
    struct sh_layer *layer = owner->owner;
    struct wlr_layer_surface_v1 *surface = layer->surface;
    if (surface->output != output || !surface->surface->mapped ||
        (surface->namespace && !strcmp(surface->namespace, XRAY_NAMESPACE)))
        return NULL;
    return layer;
}

/* The window `node` draws, when it is shown and reaches into `screen`; its box in `box`. Closing
 * copies and the place a peeked window keeps are nobody's. */
static struct sh_toplevel *xray_window(struct wlr_scene_node *node, struct wlr_box screen,
                                       struct wlr_box *box) {
    struct sh_node *owner = node->data;
    if (!node->enabled || !owner || owner->kind != SH_NODE_TOPLEVEL)
        return NULL;
    struct sh_toplevel *toplevel = owner->owner;
    if (!toplevel->scene_tree || &toplevel->scene_tree->node != node || !toplevel_mapped(toplevel))
        return NULL;
    *box = toplevel_box(toplevel);
    struct wlr_box shown;
    return wlr_box_intersection(&shown, box, &screen) ? toplevel : NULL;
}

/* What `output` draws, bottom to top, into `entries`; how many there are. */
static int xray_collect(struct sh_server *server, struct sh_output *output,
                        struct xray_entry *entries, int size) {
    struct wlr_box screen;
    wlr_output_layout_get_box(server->output_layout, output->wlr_output, &screen);
    int count = 0;
    if (output->background && size > 0)
        entries[count++] = (struct xray_entry){XRAY_BACKGROUND, XRAY_COLOUR, .box = screen,
                                               .sheet = -1};
    const struct {
        enum xray_layer layer;
        struct wlr_scene_tree *tree;
    } trees[] = {
        {XRAY_BACKGROUND, server->layer_trees[0]}, {XRAY_BOTTOM, server->layer_trees[1]},
        {XRAY_WINDOWS, server->windows},           {XRAY_FLOATING, server->floating_windows},
        {XRAY_ABOVE, server->above_windows},       {XRAY_FULLSCREEN, server->fullscreen},
        {XRAY_PEEK, server->peek_layer},           {XRAY_TOP, server->layer_trees[2]},
        {XRAY_COVER, server->fullscreen_cover},    {XRAY_X11, server->unmanaged},
        {XRAY_OVERLAY, server->layer_trees[3]},
    };
    for (size_t i = 0; i < sizeof(trees) / sizeof(*trees); ++i) {
        struct wlr_scene_node *node;
        wl_list_for_each(node, &trees[i].tree->children, link) {
            if (count >= size)
                return count;
            struct xray_entry entry = {trees[i].layer, XRAY_SURFACE, .sheet = -1};
            struct sh_layer *layer = xray_surface(node, output->wlr_output);
            if (layer) {
                entry.surface = layer;
                entry.tree = layer->scene->tree;
                wlr_scene_node_coords(node, &entry.box.x, &entry.box.y);
                entry.box.width = layer->surface->surface->current.width;
                entry.box.height = layer->surface->surface->current.height;
            } else if ((entry.window = xray_window(node, screen, &entry.box))) {
                entry.kind = entry.window->unmanaged ? XRAY_MENU : XRAY_WINDOW;
                entry.tree = entry.window->scene_tree;
            } else {
                continue;
            }
            entries[count++] = entry;
        }
    }
    return count;
}

/* Puts the entries on sheets, bottom to top, into `keys` (layer and window): a sheet for each
 * layer with anything on it, and one for each window while no more than XRAY_APART show and
 * they all fit. Returns how many sheets. */
static int xray_plan(struct xray_entry *entries, int count, struct sh_xray_sheet *keys) {
    int windows = 0, layers = 0, last = -1;
    for (int i = 0; i < count; ++i) {
        windows += entries[i].kind == XRAY_WINDOW;
        layers += (int)entries[i].layer != last;
        last = (int)entries[i].layer;
    }
    bool apart = windows <= XRAY_APART && layers + windows <= XRAY_SHEETS;
    int sheets = 0;
    for (int i = 0; i < count; ++i) {
        struct xray_entry *entry = &entries[i];
        bool alone = apart && entry->kind == XRAY_WINDOW;
        bool joins = sheets > 0 && !alone && !keys[sheets - 1].window &&
                     keys[sheets - 1].layer == (int)entry->layer;
        if (!joins && sheets < XRAY_SHEETS) {
            keys[sheets].layer = (int)entry->layer;
            keys[sheets].window = alone ? entry->window : NULL;
            ++sheets;
        }
        entry->sheet = sheets - 1; // past the last sheet, the top one takes the rest
    }
    return sheets;
}

/* The sheet of those there are that shows `entry`, as they close: its window's, else its
 * layer's; -1 when it has none, as a window that came since. */
static int xray_kept_sheet(struct sh_xray *xray, const struct xray_entry *entry) {
    for (int i = 0; i < xray->count; ++i) {
        const struct sh_xray_sheet *sheet = &xray->sheets[i];
        if (sheet->window ? sheet->window == entry->window
                          : sheet->layer == (int)entry->layer && entry->kind != XRAY_WINDOW)
            return i;
    }
    // A window whose sheet of its own is gone shows on its layer's, if there is one.
    for (int i = 0; i < xray->count; ++i) {
        if (!xray->sheets[i].window && xray->sheets[i].layer == (int)entry->layer)
            return i;
    }
    return -1;
}

static struct sh_output *xray_output(struct sh_server *server) {
    struct wlr_output *output = find_output(server, server->xray.output);
    return output ? sh_output_for(server, output) : NULL;
}

/* The colour paw draws under every surface, as 0xRRGGBB. */
static uint32_t xray_background(const struct sh_settings *settings) {
    const float *bg = settings->background; // premultiplied
    float a = bg[3] > 0 ? bg[3] : 1;
    return (uint32_t)lroundf(fminf(1, bg[0] / a) * 255) << 16 |
           (uint32_t)lroundf(fminf(1, bg[1] / a) * 255) << 8 |
           (uint32_t)lroundf(fminf(1, bg[2] / a) * 255);
}

/* The name a sheet goes by in the shell: the app ids and namespaces of what it shows, and a
 * detail: a window's title, a surface's place, or how many it holds. */
static void xray_label(const struct xray_entry *entries, int count, int sheet, char *name,
                       size_t name_size, char *detail, size_t detail_size,
                       const struct sh_settings *settings) {
    name[0] = detail[0] = '\0';
    size_t length = 0;
    int shown = 0, windows = 0;
    const struct xray_entry *only = NULL;
    for (int i = 0; i < count; ++i) {
        const struct xray_entry *entry = &entries[i];
        if (entry->sheet != sheet || entry->kind == XRAY_COLOUR)
            continue;
        ++shown;
        windows += entry->kind != XRAY_SURFACE;
        only = entry;
        char field[128];
        copy_field(field, sizeof(field),
                   entry->surface ? entry->surface->surface->namespace
                                  : toplevel_app_id(entry->window));
        if (!field[0] || strstr(name, field) || length + 2 >= name_size)
            continue; // each name once
        length += (size_t)snprintf(name + length, name_size - length, "%s%s",
                                   length ? ", " : "", field);
        if (length >= name_size)
            length = name_size - 1;
    }
    if (!shown) {
        // The colour alone: what paw draws under every surface.
        snprintf(name, name_size, "paw");
        snprintf(detail, detail_size, "#%06x", (unsigned)xray_background(settings));
    } else if (shown == 1 && only->window) {
        copy_field(detail, detail_size, toplevel_title(only->window));
    } else if (shown == 1) {
        snprintf(detail, detail_size, "%d\xc3\x97%d at %d,%d", only->box.width, only->box.height,
                 only->box.x, only->box.y);
    } else {
        snprintf(detail, detail_size, "%d %s", shown, windows == shown ? "windows" : "surfaces");
    }
}

/* Tells the shell what to name the sheets, in coordinates of the output:
 *   xray OUTPUT COUNT HOVERED X Y WIDTH HEIGHT LEGEND
 *     (X Y WIDTH HEIGHT: the area the stack keeps to; LEGEND: the width of the column left of it
 *     for a legend, 0 for none), then COUNT lines, bottom first:
 *   xray-sheet LAYER\tCOLOUR\tNAME\tDETAIL (COLOUR as #rrggbb)
 * and "xray-hover N" as the sheet under the pointer changes (-1 for none), "xray-close" as it
 * closes. Sent again whenever the sheets or their names change. */
static size_t xray_describe(struct sh_server *server, const struct xray_entry *entries,
                            int count, char *text, size_t size) {
    struct sh_xray *xray = &server->xray;
    size_t length = (size_t)snprintf(
        text, size, "xray %s %d %d %ld %ld %ld %ld %ld\n", xray->output, xray->count,
        xray->hovered, lround(xray->area.x), lround(xray->area.y), lround(xray->area.width),
        lround(xray->area.height), lround(xray->legend));
    for (int i = 0; i < xray->count && length < size; ++i) {
        char name[256], detail[192];
        xray_label(entries, count, i, name, sizeof(name), detail, sizeof(detail),
                   server_settings(server));
        const struct sh_xray_sheet *sheet = &xray->sheets[i];
        length += (size_t)snprintf(text + length, size - length, "xray-sheet %s\t#%06x\t%s\t%s\n",
                                   xray_layers[sheet->layer].name,
                                   (unsigned)xray_layers[sheet->layer].colour, name, detail);
    }
    return length < size ? length : size - 1;
}

/* Sends the sheets' names when they changed since the shell last heard them. */
static void xray_announce(struct sh_server *server, const struct xray_entry *entries, int count) {
    struct sh_xray *xray = &server->xray;
    size_t size = 512 + (size_t)xray->count * 512;
    char *text = malloc(size);
    if (!text)
        return;
    size_t length = xray_describe(server, entries, count, text, size);
    // The hover is not counted: "xray-hover" lines tell of it.
    char *sheets = memchr(text, '\n', length);
    uint64_t print = xray_mix(xray_mix(xray_bits(xray->area.width), xray_bits(xray->area.height)),
                              (uint64_t)xray->count);
    for (const char *c = xray->output; *c; ++c)
        print = (print ^ (unsigned char)*c) * 1099511628211ULL;
    for (const char *c = sheets ? sheets : text; c < text + length; ++c)
        print = (print ^ (unsigned char)*c) * 1099511628211ULL;
    if (print != xray->announced) {
        xray->announced = print;
        send_event(server, text, length);
    }
    free(text);
}

static void xray_hover(struct sh_server *server, int index) {
    struct sh_xray *xray = &server->xray;
    if (index < -1 || index >= xray->count)
        index = -1;
    if (index == xray->hovered)
        return;
    xray->hovered = index;
    char line[32];
    int length = snprintf(line, sizeof(line), "xray-hover %d\n", index);
    send_event(server, line, (size_t)length);
}

/* Lays the shadow's slices around a sheet `width` by `height`, at `alpha`. */
static void xray_shadow_place(struct sh_server *server, struct sh_xray_sheet *sheet, int width,
                              int height, float alpha) {
    struct sh_shadow_layout layout;
    sh_shadow_layout(&xray_shadow, &layout);
    int image_width, image_height;
    sh_shadow_image_size(&layout, width, height, &image_width, &image_height);
    struct sh_shadow_image *image = shadow_image(server, &xray_shadow, image_width, image_height);
    wlr_scene_node_set_enabled(&sheet->shadow->node, image && alpha > 0.001f);
    if (!image || alpha <= 0.001f)
        return;
    struct sh_shadow_slice slices[SH_SHADOW_SLICES];
    sh_shadow_slices(&layout, image_width, image_height, width, height, slices);
    for (int i = 0; i < SH_SHADOW_SLICES; ++i) {
        if (!sheet->slices[i]) {
            sheet->slices[i] = wlr_scene_buffer_create(sheet->shadow, NULL);
            if (!sheet->slices[i])
                return;
            sheet->slices[i]->point_accepts_input = no_input;
        }
        struct wlr_scene_buffer *slice = sheet->slices[i];
        const struct sh_shadow_slice *s = &slices[i];
        wlr_scene_node_set_enabled(&slice->node, s->width > 0 && s->to_width > 0);
        if (s->width <= 0 || s->to_width <= 0)
            continue;
        if (slice->buffer != image->buffer)
            wlr_scene_buffer_set_buffer(slice, image->buffer);
        int scale = image->scale;
        wlr_scene_buffer_set_source_box(slice, &(struct wlr_fbox){s->x * scale, s->y * scale,
                                                                  s->width * scale,
                                                                  s->height * scale});
        wlr_scene_buffer_set_dest_size(slice, s->to_width, s->to_height);
        wlr_scene_node_set_position(&slice->node, s->to_x, s->to_y);
        wlr_scene_buffer_set_opacity(slice, alpha);
    }
}

/* Four bars of `thickness` around a box of `width` by `height` at the tree's origin, outside
 * it. */
static void xray_outline(struct wlr_scene_rect *bars[4], int width, int height, int thickness,
                         const float colour[4]) {
    struct wlr_box sides[4] = {
        {-thickness, -thickness, width + 2 * thickness, thickness},
        {-thickness, height, width + 2 * thickness, thickness},
        {-thickness, 0, thickness, height},
        {width, 0, thickness, height},
    };
    for (int i = 0; i < 4; ++i) {
        if (!bars[i])
            continue;
        wlr_scene_rect_set_size(bars[i], sides[i].width, sides[i].height);
        wlr_scene_rect_set_color(bars[i], colour);
        wlr_scene_node_set_position(&bars[i]->node, sides[i].x, sides[i].y);
    }
}

/* The light down the top of a card, as on glass: white fading out from a twelfth, a pixel wide,
 * which the scene stretches over the card. */
static struct wlr_buffer *xray_sheen(void) {
    enum { HEIGHT = 64 };
    uint32_t *pixels = malloc(HEIGHT * sizeof(*pixels));
    if (!pixels)
        return NULL;
    for (int y = 0; y < HEIGHT; ++y) {
        double left = 1 - (double)y / (HEIGHT - 1);
        uint32_t a = (uint32_t)lround(255 * 0.085 * left * left);
        pixels[y] = a << 24 | a << 16 | a << 8 | a; // premultiplied white
    }
    return sh_pixel_buffer(pixels, 1, HEIGHT);
}

static bool xray_sheet_make(struct sh_xray *xray, struct sh_xray_sheet *sheet) {
    if (sheet->tree)
        return true;
    float clear[4] = {0, 0, 0, 0};
    sheet->tree = wlr_scene_tree_create(xray->sheets_tree);
    if (!sheet->tree)
        return false;
    sheet->shadow = wlr_scene_tree_create(sheet->tree);
    sheet->card = wlr_scene_rect_create(sheet->tree, 1, 1, clear);
    sheet->sheen = wlr_scene_buffer_create(sheet->tree, xray->sheen);
    if (sheet->sheen)
        sheet->sheen->point_accepts_input = no_input;
    sheet->content = wlr_scene_tree_create(sheet->tree);
    for (int i = 0; i < 4; ++i)
        sheet->outline[i] = wlr_scene_rect_create(sheet->tree, 1, 1, clear);
    sheet->print = 0;
    if (!sheet->shadow || !sheet->card || !sheet->content) {
        wlr_scene_node_destroy(&sheet->tree->node);
        memset(sheet, 0, sizeof(*sheet));
        return false;
    }
    return true;
}

static void xray_sheet_drop(struct sh_xray_sheet *sheet) {
    if (sheet->tree)
        wlr_scene_node_destroy(&sheet->tree->node);
    int layer = sheet->layer;
    struct sh_toplevel *window = sheet->window;
    memset(sheet, 0, sizeof(*sheet));
    sheet->layer = layer;
    sheet->window = window;
}

/* Draws sheet `index` where it is drawn now: its copy (made again only when what it copies, its
 * scale or its opacity changed), card, outline and shadow. */
static void xray_sheet_draw(struct sh_server *server, const struct xray_entry *entries, int count,
                            int index) {
    struct sh_xray *xray = &server->xray;
    struct sh_xray_sheet *sheet = &xray->sheets[index];
    if (!xray_sheet_make(xray, sheet))
        return;
    const struct sh_settings *settings = server_settings(server);
    struct wlr_box screen = xray->screen;
    struct sh_xray_box box = sheet->drawn;
    double shown = clamp01(xray->progress);
    int width = (int)lround(box.width), height = (int)lround(box.height);
    width = width < 1 ? 1 : width;
    height = height < 1 ? 1 : height;
    wlr_scene_node_set_position(&sheet->tree->node, screen.x + (int)lround(box.x),
                                screen.y + (int)lround(box.y));
    double scale_x = box.width / (screen.width > 0 ? screen.width : 1);
    double scale_y = box.height / (screen.height > 0 ? screen.height : 1);
    // Over the sheet the pointer is on, the others fade to let it show.
    double fade = 1 - 0.8 * sheet->veil;
    float opacity = (float)fmin(0.9999, fade);
    uint64_t print = xray_mix(xray_mix(xray_bits(scale_x), xray_bits(scale_y)), xray_bits(opacity));
    for (int i = 0; i < count; ++i) {
        if (entries[i].sheet != index || !entries[i].tree)
            continue;
        int x, y;
        wlr_scene_node_coords(&entries[i].tree->node, &x, &y);
        print = xray_mix(print, (uintptr_t)entries[i].tree);
        print = xray_mix(print, (uint64_t)(uint32_t)x << 32 | (uint32_t)y);
        print = xray_mix(print, sh_thumb_fingerprint(entries[i].tree, NULL));
    }
    if (print != sheet->print) {
        sh_thumb_clear(sheet->content);
        struct wlr_box cut = {0, 0, screen.width, screen.height};
        for (int i = 0; i < count; ++i) {
            if (entries[i].sheet != index || !entries[i].tree)
                continue;
            int x, y;
            wlr_scene_node_coords(&entries[i].tree->node, &x, &y);
            sh_thumb_clone_cut(sheet->content, entries[i].tree, x - screen.x, y - screen.y, &cut,
                               scale_x, scale_y, opacity, NULL);
        }
        sheet->print = print;
    }
    // The bottom sheet's card is the colour paw draws under everything; the others are tinted
    // glass in their layer's colour, which shows only as the stack comes apart.
    float colour[4];
    uint32_t tint = xray_layers[sheet->layer].colour;
    if (sheet->layer == XRAY_BACKGROUND) {
        xray_colour(colour, xray_background(settings), fade);
    } else {
        float dark[4];
        overview_tint(dark, settings, 0.05f, 1);
        uint32_t r = (uint32_t)lround(dark[0] * 255 * 0.88 + ((tint >> 16) & 0xff) * 0.12);
        uint32_t g = (uint32_t)lround(dark[1] * 255 * 0.88 + ((tint >> 8) & 0xff) * 0.12);
        uint32_t b = (uint32_t)lround(dark[2] * 255 * 0.88 + (tint & 0xff) * 0.12);
        xray_colour(colour, r << 16 | g << 8 | b, 0.4 * shown * fade);
    }
    wlr_scene_rect_set_size(sheet->card, width, height);
    wlr_scene_rect_set_color(sheet->card, colour);
    if (sheet->sheen) {
        wlr_scene_buffer_set_dest_size(sheet->sheen, width, height / 2 > 1 ? height / 2 : 1);
        wlr_scene_buffer_set_opacity(sheet->sheen, (float)fmin(0.9999, shown * fade));
        wlr_scene_node_set_enabled(&sheet->sheen->node, sheet->layer != XRAY_BACKGROUND);
    }
    bool hovered = index == xray->hovered;
    xray_colour(colour, tint, (hovered ? 1 : 0.7) * shown * (1 - 0.6 * sheet->veil));
    xray_outline(sheet->outline, width, height, hovered ? 2 : 1, colour);
    xray_shadow_place(server, sheet, width, height, (float)(0.9 * shown * fade));
}

/* Where each sheet rests at the tilt and spread it has now (for the pointer, the stack fanned
 * out whatever the progress) and is drawn. */
static void xray_layout(struct sh_xray *xray) {
    struct sh_xray_view view = {xray->screen.width, xray->screen.height, xray->area, xray->count,
                                xray->tilt_x, xray->tilt_y, xray->spread, 1};
    struct sh_xray_box boxes[XRAY_SHEETS];
    double lift[XRAY_SHEETS];
    sh_xray_layout(&view, NULL, boxes);
    for (int i = 0; i < xray->count; ++i) {
        xray->sheets[i].rest = boxes[i];
        lift[i] = xray->sheets[i].lift;
    }
    view.progress = xray->progress;
    sh_xray_layout(&view, lift, boxes);
    for (int i = 0; i < xray->count; ++i)
        xray->sheets[i].drawn = boxes[i];
}

/* The sheet under the pointer, where the sheets rest. */
static int xray_sheet_at_pointer(struct sh_server *server) {
    struct sh_xray *xray = &server->xray;
    struct sh_xray_box boxes[XRAY_SHEETS];
    for (int i = 0; i < xray->count; ++i)
        boxes[i] = xray->sheets[i].rest;
    return sh_xray_sheet_at(boxes, xray->count, server->cursor->x - xray->screen.x,
                            server->cursor->y - xray->screen.y);
}

static void xray_hide(struct sh_server *server) {
    struct sh_xray *xray = &server->xray;
    xray->visible = xray->open = xray->closing = false;
    xray->hovered = -1;
    xray->count = 0;
    // The copies hold the clients' buffers: nothing of the view stays in the scene.
    for (int i = 0; i < XRAY_SHEETS; ++i)
        xray_sheet_drop(&xray->sheets[i]);
    if (xray->tree)
        wlr_scene_node_destroy(&xray->tree->node);
    xray->tree = xray->sheets_tree = NULL;
    xray->backdrop = NULL;
}

/* Brings the view up to date: what the output draws, the sheets for it (kept as they are while
 * they close), their names, the easing toward where they head, and the drawing. True while
 * anything still moves. */
static bool xray_refresh(struct sh_server *server) {
    struct sh_xray *xray = &server->xray;
    if (!xray->visible)
        return false;
    struct sh_output *output = xray_output(server);
    if (!output || server->locked) {
        xray_dismiss(server);
        return false;
    }
    const struct sh_settings *settings = server_settings(server);
    wlr_output_layout_get_box(server->output_layout, output->wlr_output, &xray->screen);
    xray->area = sh_xray_area(xray->screen.width, xray->screen.height, &xray->legend);
    struct xray_entry *entries = calloc(XRAY_ENTRIES, sizeof(*entries));
    if (!entries)
        return false;
    int count = xray_collect(server, output, entries, XRAY_ENTRIES);
    if (xray->closing) {
        for (int i = 0; i < count; ++i)
            entries[i].sheet = xray_kept_sheet(xray, &entries[i]);
    } else {
        struct sh_xray_sheet keys[XRAY_SHEETS] = {0};
        int sheets = xray_plan(entries, count, keys);
        bool same = sheets == xray->count;
        for (int i = 0; same && i < sheets; ++i)
            same = keys[i].layer == xray->sheets[i].layer &&
                   keys[i].window == xray->sheets[i].window;
        if (!same) {
            // The sheets change: each slot copies its own anew, keeping how far it was lifted.
            for (int i = sheets; i < XRAY_SHEETS; ++i)
                xray_sheet_drop(&xray->sheets[i]);
            for (int i = 0; i < sheets; ++i) {
                xray->sheets[i].layer = keys[i].layer;
                xray->sheets[i].window = keys[i].window;
                xray->sheets[i].print = 0;
            }
            xray->count = sheets;
            if (xray->hovered >= sheets)
                xray_hover(server, -1);
        }
        xray_announce(server, entries, count);
    }

    // Easing: the progress on the clock and its curve, the rest toward where it heads.
    int64_t now = now_ms();
    // A step after a rest eases one frame's worth, not the whole rest's.
    double elapsed = fmin(32, (double)(now - xray->ticked));
    xray->ticked = now;
    if (xray->progress != xray->to) {
        int64_t gone = now - xray->started;
        if (xray->span <= 0 || gone >= xray->span) {
            xray->progress = xray->to;
        } else {
            const struct sh_curve *curve = &settings->animation_styles[SH_ANIM_MINIMIZE].curve;
            xray->progress = xray->from + (xray->to - xray->from) *
                                              sh_curve_eval(curve, (double)gone / xray->span);
        }
    }
    double speed = settings->animation_speed > 0 ? settings->animation_speed : 1;
    double settle = settings->animations ? 180 / speed : 0;
    bool moving = xray->progress != xray->to;
    double *eased[] = {&xray->tilt_x, &xray->tilt_y, &xray->spread};
    double aims[] = {xray->aim_x, xray->aim_y, xray->spread_aim};
    for (size_t i = 0; i < sizeof(eased) / sizeof(*eased); ++i) {
        *eased[i] = sh_xray_ease(*eased[i], aims[i], elapsed, settle, 1e-3);
        moving |= *eased[i] != aims[i];
    }
    for (int i = 0; i < xray->count; ++i) {
        struct sh_xray_sheet *sheet = &xray->sheets[i];
        double lift = i == xray->hovered ? 1 : 0;
        double veil = xray->hovered >= 0 && i > xray->hovered ? 1 : 0;
        sheet->lift = sh_xray_ease(sheet->lift, lift, elapsed, settle, 1e-3);
        sheet->veil = sh_xray_ease(sheet->veil, veil, elapsed, settle, 1e-3);
        moving |= sheet->lift != lift || sheet->veil != veil;
    }
    // Closed back into the screen, it goes, whatever still eases.
    if (xray->closing && xray->progress == xray->to)
        moving = false;
    xray_layout(xray);
    if (xray->pointing && xray->open)
        xray_hover(server, xray_sheet_at_pointer(server));

    float colour[4];
    overview_tint(colour, settings, 0.06f, (float)(0.97 * clamp01(xray->progress)));
    if (xray->backdrop) {
        wlr_scene_rect_set_size(xray->backdrop, xray->screen.width, xray->screen.height);
        wlr_scene_rect_set_color(xray->backdrop, colour);
        wlr_scene_node_set_position(&xray->backdrop->node, xray->screen.x, xray->screen.y);
    }
    for (int i = 0; i < xray->count; ++i)
        xray_sheet_draw(server, entries, count, i);
    free(entries);
    return moving;
}

static int xray_step(void *data) {
    struct sh_server *server = data;
    struct sh_xray *xray = &server->xray;
    xray->armed = false;
    bool moving = xray_refresh(server);
    if (!xray->visible)
        return 0;
    if (moving) {
        xray->armed = true;
        wl_event_source_timer_update(xray->timer, 16);
    } else if (xray->closing) {
        xray_hide(server);
    }
    return 0;
}

/* Asks for a look at the view soon, as something drawn may have changed; many asks make one. */
void xray_touch(struct sh_server *server) {
    struct sh_xray *xray = &server->xray;
    if (!xray->visible || xray->armed || !xray->timer)
        return;
    xray->armed = true;
    wl_event_source_timer_update(xray->timer, 8);
}

static int glide_span(const struct sh_settings *settings, double factor) {
    if (!settings->animations)
        return 0;
    double speed = settings->animation_speed > 0 ? settings->animation_speed : 1;
    return (int)(settings->animation_styles[SH_ANIM_MINIMIZE].duration * factor / speed);
}

void xray_open(struct sh_server *server) {
    struct sh_xray *xray = &server->xray;
    if (xray->open || server->locked)
        return;
    struct wlr_output *output = focused_output(server);
    if (!output)
        return;
    if (xray->visible) // still closing elsewhere, or here
        xray_hide(server);
    switcher_close(server, -1);
    overview_dismiss(server);
    if (!xray->timer)
        xray->timer = wl_event_loop_add_timer(wl_display_get_event_loop(server->wl_display),
                                              xray_step, server);
    // Over the windows, the panels and X11's menus, under the overlays, where the shell's text
    // goes, and the drag icons and the lock.
    xray->tree = wlr_scene_tree_create(&server->scene->tree);
    if (!xray->timer || !xray->tree) {
        xray_hide(server);
        return;
    }
    wlr_scene_node_place_above(&xray->tree->node, &server->overview_layer->node);
    float clear[4] = {0, 0, 0, 0};
    if (!xray->sheen)
        xray->sheen = xray_sheen();
    xray->backdrop = wlr_scene_rect_create(xray->tree, 1, 1, clear);
    xray->sheets_tree = wlr_scene_tree_create(xray->tree);
    if (!xray->backdrop || !xray->sheets_tree) {
        xray_hide(server);
        return;
    }
    snprintf(xray->output, sizeof(xray->output), "%s", output->name);
    xray->origin_x = server->cursor->x;
    xray->origin_y = server->cursor->y;
    xray->tilt_x = xray->tilt_y = xray->aim_x = xray->aim_y = 0;
    xray->spread = xray->spread_aim = 1;
    xray->scroll = 0;
    xray->hovered = -1;
    xray->pointing = false;
    xray->count = 0;
    xray->announced = 0;
    const struct sh_settings *settings = server_settings(server);
    xray->from = xray->progress = 0;
    xray->to = 1;
    xray->started = xray->ticked = now_ms();
    xray->span = glide_span(settings, 1.5);
    if (xray->span <= 0)
        xray->progress = 1;
    xray->open = xray->visible = true;
    xray->closing = false;
    // The pointer belongs to the view, not to the window under it.
    wlr_seat_pointer_clear_focus(server->seat);
    set_default_cursor(server);
    if (xray_refresh(server))
        xray_touch(server);
    wlr_log(WLR_INFO, "X-ray opened on %s with %d sheets", output->name, xray->count);
}

/* Closes the view, focusing `chosen` if given: the sheets glide back into the screen. */
void xray_close(struct sh_server *server, struct sh_toplevel *chosen) {
    struct sh_xray *xray = &server->xray;
    if (!xray->open)
        return;
    xray->open = false;
    xray->closing = true;
    xray->pointing = false;
    xray_hover(server, -1);
    send_event(server, "xray-close\n", strlen("xray-close\n"));
    if (chosen && !server->locked) {
        focus_toplevel(chosen);
        struct wlr_box box = toplevel_box(chosen);
        if (!wlr_box_contains_point(&box, server->cursor->x, server->cursor->y))
            pointer_follow(chosen);
    }
    const struct sh_settings *settings = server_settings(server);
    xray->from = xray->progress;
    xray->to = 0;
    xray->started = now_ms();
    xray->span = glide_span(settings, clamp01(xray->progress));
    wlr_log(WLR_INFO, "X-ray closed");
    if (xray->span <= 0) {
        xray_hide(server);
        return;
    }
    xray_touch(server);
}

/* Closes at once, without the glide: the session locks, or the output goes. */
void xray_dismiss(struct sh_server *server) {
    struct sh_xray *xray = &server->xray;
    if (xray->open)
        xray_close(server, NULL);
    if (xray->visible)
        xray_hide(server);
}

void xray_toggle(struct sh_server *server) {
    if (server->xray.open)
        xray_close(server, NULL);
    else
        xray_open(server);
}

/* The window under the pointer on `index`'s sheet: the topmost of the windows it shows, where
 * they are on the output; NULL for none. */
static struct sh_toplevel *xray_window_at(struct sh_server *server, int index) {
    struct sh_xray *xray = &server->xray;
    struct sh_output *output = xray_output(server);
    if (!output || index < 0 || index >= xray->count)
        return NULL;
    struct sh_xray_box rest = xray->sheets[index].rest;
    if (rest.width <= 0 || rest.height <= 0)
        return NULL;
    // The pointer on the sheet, as a point of the output.
    double x = xray->screen.x + (server->cursor->x - xray->screen.x - rest.x) *
                                    xray->screen.width / rest.width;
    double y = xray->screen.y + (server->cursor->y - xray->screen.y - rest.y) *
                                    xray->screen.height / rest.height;
    struct xray_entry *entries = calloc(XRAY_ENTRIES, sizeof(*entries));
    if (!entries)
        return NULL;
    int count = xray_collect(server, output, entries, XRAY_ENTRIES);
    struct sh_xray_sheet keys[XRAY_SHEETS] = {0};
    xray_plan(entries, count, keys);
    struct sh_toplevel *found = NULL;
    for (int i = count - 1; i >= 0 && !found; --i) {
        if (entries[i].sheet == index && entries[i].kind == XRAY_WINDOW &&
            wlr_box_contains_point(&entries[i].box, x, y))
            found = entries[i].window;
    }
    free(entries);
    return found;
}

/* The topmost window of `index`'s sheet, for Return. */
static struct sh_toplevel *xray_top_window(struct sh_server *server, int index) {
    struct sh_output *output = xray_output(server);
    if (!output || index < 0)
        return NULL;
    struct xray_entry *entries = calloc(XRAY_ENTRIES, sizeof(*entries));
    if (!entries)
        return NULL;
    int count = xray_collect(server, output, entries, XRAY_ENTRIES);
    struct sh_xray_sheet keys[XRAY_SHEETS] = {0};
    xray_plan(entries, count, keys);
    struct sh_toplevel *found = NULL;
    for (int i = count - 1; i >= 0 && !found; --i) {
        if (entries[i].sheet == index && entries[i].kind == XRAY_WINDOW)
            found = entries[i].window;
    }
    free(entries);
    return found;
}

/* Keys while the view is open: Escape closes it, the arrows and Tab go up and down the stack,
 * Return goes to the selected sheet's window, + and - spread it, 0 sets it straight again. Every
 * key is kept from the windows. */
void xray_key(struct sh_server *server, uint32_t modifiers, xkb_keysym_t sym) {
    struct sh_xray *xray = &server->xray;
    int hovered = xray->hovered;
    switch (sym) {
    case XKB_KEY_Escape:
        xray_close(server, NULL);
        return;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        xray_close(server, xray_top_window(server, hovered));
        return;
    case XKB_KEY_Up:
    case XKB_KEY_Right:
    case XKB_KEY_Down:
    case XKB_KEY_Left:
    case XKB_KEY_Tab:
    case XKB_KEY_ISO_Left_Tab: {
        if (!xray->count)
            return;
        bool down = sym == XKB_KEY_Down || sym == XKB_KEY_Left || sym == XKB_KEY_ISO_Left_Tab ||
                    (sym == XKB_KEY_Tab && (modifiers & WLR_MODIFIER_SHIFT));
        int next = hovered < 0 ? (down ? xray->count - 1 : 0) : hovered + (down ? -1 : 1);
        xray->pointing = false;
        xray_hover(server, next < 0 ? 0 : next >= xray->count ? xray->count - 1 : next);
        break;
    }
    case XKB_KEY_Home:
        xray->pointing = false;
        xray_hover(server, 0);
        break;
    case XKB_KEY_End:
        xray->pointing = false;
        xray_hover(server, xray->count - 1);
        break;
    case XKB_KEY_plus:
    case XKB_KEY_equal:
    case XKB_KEY_KP_Add:
        xray->spread_aim = sh_xray_spread(xray->spread_aim, -1);
        break;
    case XKB_KEY_minus:
    case XKB_KEY_KP_Subtract:
        xray->spread_aim = sh_xray_spread(xray->spread_aim, 1);
        break;
    case XKB_KEY_0:
    case XKB_KEY_KP_0:
        xray->origin_x = server->cursor->x;
        xray->origin_y = server->cursor->y;
        xray->aim_x = xray->aim_y = 0;
        xray->spread_aim = 1;
        break;
    default:
        return;
    }
    xray_touch(server);
}

bool xray_motion(struct sh_server *server) {
    struct sh_xray *xray = &server->xray;
    if (!xray->open)
        return false;
    double x = server->cursor->x, y = server->cursor->y;
    if (!wlr_box_contains_point(&xray->screen, x, y)) {
        // On another output the pointer is the windows' there.
        xray->pointing = false;
        xray_hover(server, -1);
        return false;
    }
    sh_xray_tilt(x - xray->origin_x, y - xray->origin_y, xray->screen.width, xray->screen.height,
                 &xray->aim_x, &xray->aim_y);
    xray->pointing = true;
    xray_hover(server, xray_sheet_at_pointer(server));
    xray_touch(server);
    set_default_cursor(server);
    wlr_seat_pointer_clear_focus(server->seat);
    return true;
}

/* A press on a sheet showing a window goes to that window; one on the backdrop closes the view,
 * and one on another output closes it and goes on to what is there. */
bool xray_button(struct sh_server *server, const struct wlr_pointer_button_event *event) {
    struct sh_xray *xray = &server->xray;
    uint32_t bit = event->button >= BTN_MOUSE && event->button < BTN_MOUSE + 32
                       ? 1u << (event->button - BTN_MOUSE)
                       : 0;
    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        if (!(xray->pressed & bit))
            return false;
        xray->pressed &= ~bit;
        return true;
    }
    if (!xray->open)
        return false;
    if (!wlr_box_contains_point(&xray->screen, server->cursor->x, server->cursor->y)) {
        xray_close(server, NULL);
        process_cursor_motion(server, event->time_msec);
        return false;
    }
    xray->pressed |= bit;
    if (event->button != BTN_LEFT)
        return true;
    int sheet = xray_sheet_at_pointer(server);
    if (sheet < 0) {
        xray_close(server, NULL);
        return true;
    }
    struct sh_toplevel *window = xray_window_at(server, sheet);
    if (window)
        xray_close(server, window);
    return true;
}

/* The wheel spreads the stack apart (up) and together (down). */
bool xray_axis(struct sh_server *server, const struct wlr_pointer_axis_event *event) {
    struct sh_xray *xray = &server->xray;
    if (!xray->open)
        return false;
    if (event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        xray->spread_aim = sh_xray_spread(xray->spread_aim, event->delta / 15);
        xray_touch(server);
    }
    return true;
}

void xray_finish(struct sh_server *server) {
    xray_hide(server);
    wlr_buffer_drop(server->xray.sheen);
    server->xray.sheen = NULL;
    if (server->xray.timer)
        wl_event_source_remove(server->xray.timer);
    server->xray.timer = NULL;
}

/* `get xray`: the state (open, closing or closed), how far the sheets have come apart
 * (thousandths), the output (- while closed) and the sheet under the pointer (-1 for none),
 * then what every output draws, bottom to top: output, layer, its colour, kind (colour,
 * surface, window or x11), name (namespace or app id; paw for the colour), title (the colour
 * as #rrggbb for the colour), x, y, width and height in layout coordinates, and while the view
 * shows that output, the sheet showing it (-1 for none) and where that is drawn now. */
void describe_xray(struct sh_server *server, int fd, const char *arguments) {
    struct sh_xray *xray = &server->xray;
    struct xray_entry *entries = calloc(XRAY_ENTRIES, sizeof(*entries));
    if (!entries) {
        control_reply(fd, "error: out of memory\n");
        return;
    }
    control_reply(fd, "ok\n");
    char line[1024];
    snprintf(line, sizeof(line), "%s %ld %s %d\n",
             xray->open ? "open" : xray->visible ? "closing" : "closed",
             lround(1000 * xray->progress), xray->visible ? xray->output : "-",
             xray->visible ? xray->hovered : -1);
    control_reply(fd, line);
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        int count = xray_collect(server, output, entries, XRAY_ENTRIES);
        bool shown = xray->visible && !strcmp(xray->output, output->wlr_output->name);
        if (shown && !xray->closing) {
            struct sh_xray_sheet keys[XRAY_SHEETS] = {0};
            xray_plan(entries, count, keys);
        } else {
            for (int i = 0; i < count; ++i)
                entries[i].sheet = shown ? xray_kept_sheet(xray, &entries[i]) : -1;
        }
        for (int i = 0; i < count; ++i) {
            const struct xray_entry *entry = &entries[i];
            char name[128], title[256];
            if (entry->kind == XRAY_COLOUR) {
                snprintf(name, sizeof(name), "paw");
                snprintf(title, sizeof(title), "#%06x",
                         (unsigned)xray_background(server_settings(server)));
            } else {
                copy_field(name, sizeof(name),
                           entry->surface ? entry->surface->surface->namespace
                                          : toplevel_app_id(entry->window));
                copy_field(title, sizeof(title),
                           entry->window ? toplevel_title(entry->window) : "");
            }
            int sheet = shown && entry->sheet < xray->count ? entry->sheet : -1;
            struct sh_xray_box drawn = sheet >= 0 ? xray->sheets[sheet].drawn
                                                  : (struct sh_xray_box){0, 0, 0, 0};
            snprintf(line, sizeof(line),
                     "%s\t%s\t#%06x\t%s\t%s\t%s\t%d\t%d\t%d\t%d\t%d\t%ld\t%ld\t%ld\t%ld\n",
                     output->wlr_output->name, xray_layers[entry->layer].name,
                     (unsigned)xray_layers[entry->layer].colour, xray_kinds[entry->kind], name,
                     title, entry->box.x, entry->box.y, entry->box.width, entry->box.height, sheet,
                     sheet >= 0 ? lround(xray->screen.x + drawn.x) : 0,
                     sheet >= 0 ? lround(xray->screen.y + drawn.y) : 0, lround(drawn.width),
                     lround(drawn.height));
            control_reply(fd, line);
        }
    }
    free(entries);
}
