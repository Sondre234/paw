/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The cursor trail (appearance.cursor_trail, the cursor_trail action): little sparkles, sakura
 * petals or hearts falling off the pointer as it moves and fading within a second, in a tree of
 * scene buffers over the windows and the panels and under the drag icons, the lock and the cursor
 * (which outputs draw over the scene), taking no input. src/cursor_trail.c spawns them, moves them
 * and paints their pictures, at the outputs' largest scale, each family's side by side in a strip
 * that the nodes of its places keep, showing the picture of the moment by their source box: the
 * renderer makes a texture for each node's buffer anew whenever it changes, which on a GPU is an
 * upload, and a petal turning would otherwise change its buffer every few frames. A timer steps
 * them every TICK_MS while any are left and stops with the last, so nothing runs while the
 * pointer rests. None come, and
 * those there go at once, while the session is locked or on another VT, while the cursor is hidden
 * (typing, resting, or a screenshot being taken, so that grim leaves them out as it leaves the
 * cursor out), and over a monitor showing a fullscreen window. Each step changes the nodes with
 * their tree off and shows it again once, as the ambient particles do. */
#include "server.h"
#include "paw/cursor_trail.h"

/* A step every this many milliseconds while particles are left. */
enum { TICK_MS = 16 };
/* A motion this far at once is a jump (a warp, the pointer put on another monitor), along which
 * nothing comes off it. */
static const double JUMP = 320;
/* Transparent pixels between the pictures of a strip, which smooth scaling may reach into. */
enum { GUTTER = 2 };
/* Colours for a configuration that gave none: light blue and sakura pink. */
static const float FALLBACK[2][3] = {{0.56f, 0.76f, 0.93f}, {0.95f, 0.63f, 0.75f}};

struct sh_cursor_trail {
    struct sh_server *server;
    struct wlr_scene_tree *tree; // over the overlay layer, under the drag icons
    struct wl_event_source *timer;
    bool ticking; // the timer is set
    int64_t ticked;
    enum sh_trail_kind kind;       // what falls now
    enum sh_trail_kind configured; // appearance.cursor_trail.kind as last read
    enum sh_trail_kind last;       // the last kind other than none, for toggling back on
    double amount;
    float colors[SH_TRAIL_TINTS][3];
    int tints;
    struct sh_trail trail;
    double x, y; // where the pointer was at its last motion
    bool placed; // x and y are set
    struct wlr_scene_buffer *nodes[SH_TRAIL_MAX]; // each place's, made as first needed
    int held[SH_TRAIL_MAX];  // the family whose strip each node holds, -1 for none
    int drawn[SH_TRAIL_MAX]; // the picture each node shows, -1 for none
    // Each family's pictures side by side at `scale`, painted as first needed, and where each
    // picture of the kind is in its family's strip, in buffer pixels.
    struct wlr_buffer **strips;
    int strip_count;
    struct wlr_fbox *cells;
    float scale;
};

static bool no_input(struct wlr_scene_buffer *buffer, double *sx, double *sy) { return false; }

/* The buffer transform that turns a picture `quarters` quarter turns clockwise on the screen. */
static enum wl_output_transform quarter_transform(int quarters) {
    static const enum wl_output_transform transforms[4] = {
        WL_OUTPUT_TRANSFORM_NORMAL, WL_OUTPUT_TRANSFORM_90, WL_OUTPUT_TRANSFORM_180,
        WL_OUTPUT_TRANSFORM_270};
    return transforms[quarters & 3];
}

static bool shows_fullscreen(struct sh_server *server, struct wlr_output *wlr_output) {
    struct sh_output *output = wlr_output ? sh_output_for(server, wlr_output) : NULL;
    if (!output)
        return false;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (!toplevel->unmanaged && toplevel->fullscreen && toplevel_mapped(toplevel) &&
            toplevel_visible(toplevel) && output_named(output, toplevel->output))
            return true;
    }
    return false;
}

/* Why nothing comes off the pointer now, whatever the kind: NULL if it may. */
static const char *paused_why(struct sh_cursor_trail *trail) {
    struct sh_server *server = trail->server;
    if (server->locked)
        return "locked";
#if WLR_HAS_SESSION
    if (server->session && !server->session->active)
        return "away"; // another VT is on the screen
#endif
    if (server->cursor_hidden & SH_HIDDEN_SCREENSHOT)
        return "screenshot";
    if (server->cursor_hidden)
        return "hidden";
    if (shows_fullscreen(server, pointer_output(server)))
        return "fullscreen";
    return NULL;
}

/* Lets go of the pictures, which are painted again as they are next needed. */
static void drop_strips(struct sh_cursor_trail *trail) {
    for (int i = 0; i < SH_TRAIL_MAX; ++i) {
        if (trail->nodes[i] && trail->held[i] >= 0)
            wlr_scene_buffer_set_buffer(trail->nodes[i], NULL);
        trail->held[i] = trail->drawn[i] = -1;
    }
    for (int i = 0; trail->strips && i < trail->strip_count; ++i)
        wlr_buffer_drop(trail->strips[i]);
    free(trail->strips);
    free(trail->cells);
    trail->strips = NULL;
    trail->cells = NULL;
    trail->strip_count = 0;
}

/* Family `family`'s pictures side by side, GUTTER apart, each at the top of its own cell. */
static struct wlr_buffer *strip(struct sh_cursor_trail *trail, int family) {
    enum sh_trail_kind kind = trail->trail.kind;
    int families = sh_trail_family_count(kind, trail->tints);
    int size = sh_trail_family_size(kind), first = sh_trail_family_first(kind, family);
    if (family < 0 || family >= families || size < 1 || size > 8)
        return NULL;
    if (!trail->strips) {
        trail->strips = calloc(families, sizeof(*trail->strips));
        trail->cells = calloc(sh_trail_sprite_count(kind, trail->tints), sizeof(*trail->cells));
        if (!trail->strips || !trail->cells) {
            free(trail->strips);
            free(trail->cells);
            trail->strips = NULL;
            trail->cells = NULL;
            return NULL;
        }
        trail->strip_count = families;
    }
    if (trail->strips[family])
        return trail->strips[family];
    int sides[8], width = 0, height = 1;
    for (int k = 0; k < size; ++k) {
        sides[k] = (int)lround(sh_trail_sprite_side(kind, first + k) * trail->scale);
        sides[k] = sides[k] < 1 ? 1 : sides[k];
        width += sides[k] + GUTTER;
        height = sides[k] > height ? sides[k] : height;
    }
    uint32_t *pixels = calloc((size_t)width * height, sizeof(*pixels));
    uint32_t *picture = malloc((size_t)height * height * sizeof(*picture));
    if (!pixels || !picture) {
        free(pixels);
        free(picture);
        return NULL;
    }
    for (int k = 0, x = 0; k < size; x += sides[k] + GUTTER, ++k) {
        sh_trail_paint(kind, first + k, (const float(*)[3])trail->colors, trail->tints, picture,
                       sides[k]);
        for (int row = 0; row < sides[k]; ++row)
            memcpy(&pixels[(size_t)row * width + x], &picture[(size_t)row * sides[k]],
                   sides[k] * sizeof(*picture));
        trail->cells[first + k] = (struct wlr_fbox){x, 0, sides[k], sides[k]};
    }
    free(picture);
    return trail->strips[family] = sh_pixel_buffer(pixels, width, height);
}

static void set_ticking(struct sh_cursor_trail *trail, int ms) {
    trail->ticking = ms > 0;
    wl_event_source_timer_update(trail->timer, ms);
}

/* Takes every particle away at once, and stops stepping. */
static void clear(struct sh_cursor_trail *trail) {
    sh_trail_clear(&trail->trail);
    wlr_scene_node_set_enabled(&trail->tree->node, false);
    trail->ticked = 0;
    if (trail->ticking)
        set_ticking(trail, 0);
}

/* Empties the trail for `kind`, with its pictures painted afresh as they are needed. */
static void reset(struct sh_cursor_trail *trail, enum sh_trail_kind kind) {
    clear(trail);
    drop_strips(trail);
    // Off meanwhile, the pointer went on unseen: nothing falls along the way from where it was.
    trail->placed = false;
    sh_trail_reset(&trail->trail, kind, trail->tints, trail->amount);
}

static float largest_scale(struct sh_server *server) {
    float scale = 1;
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        if (output->wlr_output->enabled && output->wlr_output->scale > scale)
            scale = output->wlr_output->scale;
    }
    return scale;
}

static void draw(struct sh_cursor_trail *trail) {
    float scale = largest_scale(trail->server);
    if (scale != trail->scale) {
        drop_strips(trail);
        trail->scale = scale;
    }
    // The scene works out what shows after each change to a node it shows: a few for each
    // particle. They change out of its sight instead, and come back together.
    wlr_scene_node_set_enabled(&trail->tree->node, false);
    for (int i = 0; i < SH_TRAIL_MAX; ++i) {
        const struct sh_trail_particle *particle = &trail->trail.particles[i];
        struct wlr_scene_buffer *node = trail->nodes[i];
        if (particle->life <= 0) {
            if (node)
                wlr_scene_node_set_enabled(&node->node, false);
            continue;
        }
        if (!node) {
            if (!(node = trail->nodes[i] = wlr_scene_buffer_create(trail->tree, NULL)))
                continue;
            node->point_accepts_input = no_input;
        }
        struct sh_trail_look look;
        sh_trail_look(&trail->trail, particle, &look);
        int width = (int)lround(look.width), height = (int)lround(look.height);
        int family = sh_trail_family_at(&trail->trail, i);
        int first = sh_trail_family_first(trail->trail.kind, family);
        bool shown = look.opacity > 0.004 && width > 0 && height > 0 && look.sprite >= first &&
                     look.sprite < first + sh_trail_family_size(trail->trail.kind);
        struct wlr_buffer *buffer = shown ? strip(trail, family) : NULL;
        wlr_scene_node_set_enabled(&node->node, buffer != NULL);
        if (!buffer)
            continue;
        if (trail->held[i] != family) {
            wlr_scene_buffer_set_buffer(node, buffer);
            trail->held[i] = family;
        }
        if (trail->drawn[i] != look.sprite) {
            wlr_scene_buffer_set_source_box(node, &trail->cells[look.sprite]);
            trail->drawn[i] = look.sprite;
        }
        wlr_scene_buffer_set_transform(node, quarter_transform(look.quarters));
        wlr_scene_buffer_set_dest_size(node, width, height);
        wlr_scene_node_set_position(&node->node, (int)lround(look.x), (int)lround(look.y));
        wlr_scene_buffer_set_opacity(node, (float)(look.opacity > 1 ? 1 : look.opacity));
    }
    wlr_scene_node_set_enabled(&trail->tree->node, true);
}

/* Moves the particles on and draws them, stepping again while any are left. */
static int trail_timer(void *data) {
    struct sh_cursor_trail *trail = data;
    trail->ticking = false;
    if (paused_why(trail)) {
        clear(trail);
        return 0;
    }
    int64_t now = now_ms();
    // After a stall, as if a few steps went by.
    double dt = trail->ticked ? (double)(now - trail->ticked) : 0;
    dt = dt < 0 ? 0 : dt > 4 * TICK_MS ? 4 * TICK_MS : dt;
    trail->ticked = now;
    if (!sh_trail_step(&trail->trail, dt)) {
        clear(trail);
        return 0;
    }
    draw(trail);
    set_ticking(trail, TICK_MS);
    return 0;
}

void cursor_trail_moved(struct sh_server *server) {
    struct sh_cursor_trail *trail = server->cursor_trail;
    if (!trail || trail->kind == SH_TRAIL_NONE)
        return;
    double x = server->cursor->x, y = server->cursor->y;
    double from_x = trail->x, from_y = trail->y;
    bool placed = trail->placed;
    trail->x = x;
    trail->y = y;
    trail->placed = true;
    if (paused_why(trail)) {
        if (trail->trail.count)
            clear(trail);
        return;
    }
    if (!placed || hypot(x - from_x, y - from_y) > JUMP)
        return;
    if (sh_trail_moved(&trail->trail, from_x, from_y, x, y) > 0 && !trail->ticking) {
        // Drawn at once, and stepped from now.
        if (!trail->ticked)
            trail->ticked = now_ms();
        set_ticking(trail, 1);
    }
}

void cursor_trail_clear(struct sh_server *server) {
    struct sh_cursor_trail *trail = server->cursor_trail;
    if (trail && (trail->trail.count || trail->ticking))
        clear(trail);
}

static void set_kind(struct sh_cursor_trail *trail, enum sh_trail_kind kind) {
    if (kind < SH_TRAIL_NONE || kind >= SH_TRAIL_KINDS)
        kind = SH_TRAIL_NONE;
    if (kind != SH_TRAIL_NONE)
        trail->last = kind;
    if (kind == trail->kind)
        return;
    wlr_log(WLR_INFO, "Cursor trail: %s", sh_trail_kind_name(kind));
    trail->kind = kind;
    reset(trail, kind);
}

void cursor_trail_switch(struct sh_server *server, int choice) {
    struct sh_cursor_trail *trail = server->cursor_trail;
    if (!trail)
        return;
    enum sh_trail_kind kind;
    switch (choice) {
    case SH_CURSOR_TRAIL_TOGGLE:
        kind = trail->kind != SH_TRAIL_NONE ? SH_TRAIL_NONE : trail->last;
        break;
    case SH_CURSOR_TRAIL_NEXT:
        kind = (trail->kind + 1) % SH_TRAIL_KINDS;
        break;
    default:
        kind = (enum sh_trail_kind)(choice - SH_CURSOR_TRAIL_OFF);
        break;
    }
    set_kind(trail, kind);
}

void cursor_trail_reload(struct sh_server *server) {
    struct sh_cursor_trail *trail = server->cursor_trail;
    if (!trail)
        return;
    const struct sh_settings *settings = server_settings(server);
    float colors[SH_TRAIL_TINTS][3];
    int tints = settings->cursor_trail_tints;
    if (tints < 1 || tints > SH_TRAIL_TINTS) {
        tints = 2;
        memcpy(colors, FALLBACK, sizeof(FALLBACK));
    } else {
        memcpy(colors, settings->cursor_trail_colors, sizeof(colors));
    }
    double amount = settings->cursor_trail_amount > 0 ? settings->cursor_trail_amount : 1;
    bool repaint = tints != trail->tints || memcmp(colors, trail->colors, tints * sizeof(*colors));
    memcpy(trail->colors, colors, sizeof(colors));
    trail->tints = tints;
    trail->amount = amount;
    // The action's choice stands until appearance.cursor_trail.kind changes, as a profile does.
    enum sh_trail_kind configured = (enum sh_trail_kind)settings->cursor_trail;
    if (configured != trail->configured) {
        trail->configured = configured;
        set_kind(trail, configured);
    }
    if (repaint || trail->trail.amount != amount)
        reset(trail, trail->kind);
}

void cursor_trail_init(struct sh_server *server) {
    struct sh_cursor_trail *trail = calloc(1, sizeof(*trail));
    if (!trail)
        return;
    trail->server = server;
    trail->last = SH_TRAIL_SPARKLES;
    trail->scale = 1;
    trail->trail.seed = ((uint32_t)now_ns() ^ (uint32_t)(uintptr_t)trail) | 1;
    for (int i = 0; i < SH_TRAIL_MAX; ++i)
        trail->held[i] = trail->drawn[i] = -1;
    trail->timer = wl_event_loop_add_timer(wl_display_get_event_loop(server->wl_display),
                                           trail_timer, trail);
    // Over the windows, the panels and the overlays; under the drag icons and the lock, and the
    // cursor, which outputs draw over the whole scene.
    trail->tree = wlr_scene_tree_create(&server->scene->tree);
    if (!trail->timer || !trail->tree) {
        if (trail->timer)
            wl_event_source_remove(trail->timer);
        if (trail->tree)
            wlr_scene_node_destroy(&trail->tree->node);
        free(trail);
        return;
    }
    wlr_scene_node_place_below(&trail->tree->node, &server->drag_icons->node);
    wlr_scene_node_set_enabled(&trail->tree->node, false);
    server->cursor_trail = trail;
    sh_trail_reset(&trail->trail, SH_TRAIL_NONE, 1, 1);
    cursor_trail_reload(server);
}

void cursor_trail_finish(struct sh_server *server) {
    struct sh_cursor_trail *trail = server->cursor_trail;
    if (!trail)
        return;
    drop_strips(trail);
    wl_event_source_remove(trail->timer);
    wlr_scene_node_destroy(&trail->tree->node);
    free(trail);
    server->cursor_trail = NULL;
}

void describe_cursor_trail(struct sh_server *server, int fd, const char *arguments) {
    // The kind, how many particles there are, and "shown", "still" (none, the pointer resting),
    // "off", or why none come (locked, away, screenshot, hidden, fullscreen). With "particles",
    // each particle as it is drawn instead: its box in layout coordinates, its opacity in
    // thousandths, its picture and its transform.
    struct sh_cursor_trail *trail = server->cursor_trail;
    if (!trail) {
        control_reply(fd, "error: there is no cursor trail\n");
        return;
    }
    if (arguments && *arguments) {
        if (strcmp(arguments, "particles")) {
            control_reply(fd, "error: cursor_trail takes nothing, or \"particles\"\n");
            return;
        }
        control_reply(fd, "ok\n");
        for (int i = 0; trail->tree->node.enabled && i < SH_TRAIL_MAX; ++i) {
            struct wlr_scene_buffer *node = trail->nodes[i];
            if (!node || !node->node.enabled || trail->drawn[i] < 0 ||
                trail->trail.particles[i].life <= 0)
                continue;
            char line[128];
            snprintf(line, sizeof(line), "%d\t%d\t%d\t%d\t%ld\t%d\t%d\n", node->node.x,
                     node->node.y, node->dst_width, node->dst_height,
                     lround(node->opacity * 1000), trail->drawn[i], (int)node->transform);
            control_reply(fd, line);
        }
        return;
    }
    const char *paused = paused_why(trail);
    const char *state = trail->kind == SH_TRAIL_NONE ? "off"
                        : paused                     ? paused
                        : trail->trail.count         ? "shown"
                                                     : "still";
    char line[128];
    snprintf(line, sizeof(line), "ok\n%s\t%d\t%s\n", sh_trail_kind_name(trail->kind),
             trail->trail.count, state);
    control_reply(fd, line);
}
