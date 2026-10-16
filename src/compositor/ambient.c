/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Ambient particles (appearance.ambient, the ambient action): sakura petals, snow or fireflies
 * drifting over the wallpaper, under the bottom layer's panels and the windows, in a tree of
 * scene buffers for each output that takes no input. src/ambient.c moves them and paints their
 * pictures, made for each output at its scale as they are first needed. A timer steps them every
 * TICK_MS while an output shows them. An output pauses, its tree off, while the session is locked
 * or on another VT, while it is off or shows a fullscreen window, and while the screens are
 * dimmed; after a while without input they fade out until input comes. Each step changes the
 * nodes out of the scene's sight, their tree off, and shows the tree again once, as a party's
 * confetti does (toys.c). */
#include "server.h"
#include "paw/ambient.h"

/* A step every this many milliseconds; fading in or out takes FADE_MS. */
enum { TICK_MS = 33, FADE_MS = 1200 };
/* Without input this long, the particles fade out until there is some (PAW_TEST_AMBIENT_IDLE_MS
 * in place of it). */
static const int64_t IDLE_MS = 5 * 60 * 1000;

/* An output's particles. */
struct sh_ambient_sky {
    struct sh_output *output;
    struct wlr_scene_tree *tree;
    struct sh_ambient_field field; // the kind drawn and the area, the output's logical size
    struct sh_ambient_particle *particles;
    struct wlr_scene_buffer **nodes;
    int *drawn; // the picture each node shows, -1 for none
    int count;
    struct wlr_buffer **sprites; // the kind's pictures at `scale`, each painted as first needed
    float scale;
    const char *paused; // why it is not drawn (its tree off), NULL while it is
    double presence;    // how far it has faded in, 0 to 1
    bool leaving;       // fading out: toward none or another kind, or for want of input
};

struct sh_ambient {
    struct sh_server *server;
    struct wlr_scene_tree *tree; // over the background layer, under the bottom one
    struct wl_event_source *timer;
    bool ticking; // the timer is set
    bool asleep;  // an output paused for want of input, which wakes it
    enum sh_ambient_kind kind;       // what drifts now
    enum sh_ambient_kind configured; // appearance.ambient.kind as last read
    enum sh_ambient_kind last;       // the last kind other than none, for toggling back on
    int count; // appearance.ambient.count, 0 for each kind's own
    double speed;
    int64_t ticked, idle_ms;
};

static bool no_input(struct wlr_scene_buffer *buffer, double *sx, double *sy) { return false; }

/* The buffer transform that turns a picture `quarters` quarter turns clockwise on the screen. */
static enum wl_output_transform quarter_transform(int quarters) {
    static const enum wl_output_transform transforms[4] = {
        WL_OUTPUT_TRANSFORM_NORMAL, WL_OUTPUT_TRANSFORM_90, WL_OUTPUT_TRANSFORM_180,
        WL_OUTPUT_TRANSFORM_270};
    return transforms[quarters & 3];
}

static bool shows_fullscreen(struct sh_output *output) {
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &output->server->toplevels, link) {
        if (!toplevel->unmanaged && toplevel->fullscreen && toplevel_mapped(toplevel) &&
            toplevel_visible(toplevel) && output_named(output, toplevel->output))
            return true;
    }
    return false;
}

/* Why `output` shows no particles now, whatever the kind: NULL if it may. */
static const char *paused_why(struct sh_ambient *ambient, struct sh_output *output) {
    struct sh_server *server = ambient->server;
    if (server->locked)
        return "locked";
#if WLR_HAS_SESSION
    if (server->session && !server->session->active)
        return "away"; // another VT is on the screen
#endif
    struct wlr_box box;
    wlr_output_layout_get_box(server->output_layout, output->wlr_output, &box);
    if (output->powered_off || output->idle_off || !output->wlr_output->enabled ||
        wlr_box_empty(&box))
        return "display_off";
    if (shows_fullscreen(output))
        return "fullscreen";
    if (server->idle.done & (1u << SH_IDLE_DIM))
        return "dimmed";
    return NULL;
}

static bool idle(struct sh_ambient *ambient, int64_t now) {
    return now - ambient->server->idle.last_input >= ambient->idle_ms;
}

/* Lets go of the pictures, which are painted again as they are next needed. */
static void drop_sprites(struct sh_ambient_sky *sky) {
    int sprites = sh_ambient_sprite_count(sky->field.kind);
    for (int i = 0; i < sky->count; ++i) {
        if (sky->drawn[i] >= 0)
            wlr_scene_buffer_set_buffer(sky->nodes[i], NULL);
        sky->drawn[i] = -1;
    }
    for (int i = 0; sky->sprites && i < sprites; ++i)
        wlr_buffer_drop(sky->sprites[i]);
    free(sky->sprites);
    sky->sprites = NULL;
}

static struct wlr_buffer *sprite(struct sh_ambient_sky *sky, int index) {
    enum sh_ambient_kind kind = sky->field.kind;
    if (index < 0 || index >= sh_ambient_sprite_count(kind))
        return NULL;
    if (!sky->sprites && !(sky->sprites = calloc(sh_ambient_sprite_count(kind),
                                                 sizeof(*sky->sprites))))
        return NULL;
    if (!sky->sprites[index]) {
        int side = (int)lround(sh_ambient_sprite_side(kind, index) * sky->scale);
        side = side < 1 ? 1 : side;
        uint32_t *pixels = malloc((size_t)side * side * sizeof(*pixels));
        if (!pixels)
            return NULL;
        sh_ambient_paint(kind, index, pixels, side);
        sky->sprites[index] = sh_pixel_buffer(pixels, side, side);
    }
    return sky->sprites[index];
}

/* Makes the sky hold `count` particles: new ones turn up anywhere, fading in. */
static bool fit_count(struct sh_ambient_sky *sky, int count) {
    if (count == sky->count)
        return true;
    for (int i = count; i < sky->count; ++i)
        wlr_scene_node_destroy(&sky->nodes[i]->node);
    if (count < sky->count) {
        sky->count = count;
        return true;
    }
    struct sh_ambient_particle *particles = realloc(sky->particles, count * sizeof(*particles));
    if (particles)
        sky->particles = particles;
    struct wlr_scene_buffer **nodes = realloc(sky->nodes, count * sizeof(*nodes));
    if (nodes)
        sky->nodes = nodes;
    int *drawn = realloc(sky->drawn, count * sizeof(*drawn));
    if (drawn)
        sky->drawn = drawn;
    if (!particles || !nodes || !drawn)
        return false;
    for (; sky->count < count; ++sky->count) {
        struct wlr_scene_buffer *node = wlr_scene_buffer_create(sky->tree, NULL);
        if (!node)
            return false;
        node->point_accepts_input = no_input;
        sky->nodes[sky->count] = node;
        sky->drawn[sky->count] = -1;
        sh_ambient_spawn(&sky->field, &sky->particles[sky->count], true);
    }
    return true;
}

/* How many particles of `kind` each output has. */
static int count_of(struct sh_ambient *ambient, enum sh_ambient_kind kind) {
    return kind == SH_AMBIENT_NONE ? 0
           : ambient->count > 0    ? ambient->count
                                   : sh_ambient_default_count(kind);
}

/* Empties the sky and fills it with particles of `kind` (none for none), faded out. */
static void reseed(struct sh_ambient *ambient, struct sh_ambient_sky *sky,
                   enum sh_ambient_kind kind) {
    drop_sprites(sky);
    fit_count(sky, 0);
    sky->field.kind = kind;
    sky->field.time = 0;
    sky->presence = 0;
    if (kind != SH_AMBIENT_NONE && !fit_count(sky, count_of(ambient, kind)))
        wlr_log(WLR_ERROR, "Out of memory for ambient particles on %s",
                sky->output->wlr_output->name);
}

static void free_sky(struct sh_ambient_sky *sky) {
    drop_sprites(sky);
    wlr_scene_node_destroy(&sky->tree->node);
    free(sky->particles);
    free(sky->nodes);
    free(sky->drawn);
    sky->output->ambient = NULL;
    free(sky);
}

/* Follows the output's place, size and scale: the particles keep their share of its width and
 * height, and the pictures are painted again at a new scale. */
static void place(struct sh_ambient_sky *sky) {
    struct sh_server *server = sky->output->server;
    struct wlr_box box;
    wlr_output_layout_get_box(server->output_layout, sky->output->wlr_output, &box);
    if (wlr_box_empty(&box))
        return;
    wlr_scene_node_set_position(&sky->tree->node, box.x, box.y);
    struct sh_ambient_field *field = &sky->field;
    if (field->width != box.width || field->height != box.height) {
        // Particles put in before the output had a size start again anywhere in it.
        bool sized = field->width > 0 && field->height > 0;
        for (int i = 0; i < sky->count && sized; ++i) {
            sky->particles[i].x *= box.width / field->width;
            sky->particles[i].y *= box.height / field->height;
        }
        field->width = box.width;
        field->height = box.height;
        for (int i = 0; i < sky->count && !sized; ++i)
            sh_ambient_spawn(field, &sky->particles[i], true);
    }
    if (sky->scale != sky->output->wlr_output->scale) {
        drop_sprites(sky);
        sky->scale = sky->output->wlr_output->scale;
    }
}

static struct sh_ambient_sky *make_sky(struct sh_ambient *ambient, struct sh_output *output) {
    struct sh_ambient_sky *sky = calloc(1, sizeof(*sky));
    if (!sky)
        return NULL;
    sky->output = output;
    sky->tree = wlr_scene_tree_create(ambient->tree);
    if (!sky->tree) {
        free(sky);
        return NULL;
    }
    wlr_scene_node_set_enabled(&sky->tree->node, false);
    sky->paused = "off";
    sky->scale = output->wlr_output->scale;
    // A sky of its own on each output, the same however many there are.
    sky->field.seed = ((uint32_t)now_ns() ^ (uint32_t)(uintptr_t)output) | 1;
    output->ambient = sky;
    place(sky);
    return sky;
}

static void draw(struct sh_ambient_sky *sky) {
    // The scene works out what shows after each change to a node it shows: a few for each
    // particle. They change out of its sight instead, and come back together.
    wlr_scene_node_set_enabled(&sky->tree->node, false);
    double presence = sky->presence * sky->presence * (3 - 2 * sky->presence);
    for (int i = 0; i < sky->count; ++i) {
        struct sh_ambient_look look;
        sh_ambient_look(&sky->field, &sky->particles[i], &look);
        struct wlr_scene_buffer *node = sky->nodes[i];
        float opacity = (float)(look.opacity * presence);
        struct wlr_buffer *buffer = opacity > 0.004f ? sprite(sky, look.sprite) : NULL;
        wlr_scene_node_set_enabled(&node->node, buffer != NULL);
        if (!buffer)
            continue;
        if (sky->drawn[i] != look.sprite) {
            wlr_scene_buffer_set_buffer(node, buffer);
            sky->drawn[i] = look.sprite;
        }
        wlr_scene_buffer_set_transform(node, quarter_transform(look.quarters));
        int width = (int)lround(look.width), height = (int)lround(look.height);
        wlr_scene_buffer_set_dest_size(node, width < 1 ? 1 : width, height < 1 ? 1 : height);
        wlr_scene_node_set_position(&node->node, (int)lround(look.x), (int)lround(look.y));
        wlr_scene_buffer_set_opacity(node, opacity > 1 ? 1 : opacity);
    }
    wlr_scene_node_set_enabled(&sky->tree->node, true);
}

/* Moves an output's particles on by `dt`; whether it wants another step. */
static bool step_sky(struct sh_ambient *ambient, struct sh_ambient_sky *sky, double dt,
                     bool resting) {
    const char *paused = paused_why(ambient, sky->output);
    if (paused) {
        wlr_scene_node_set_enabled(&sky->tree->node, false);
        sky->paused = paused;
        // Unseen, it takes up a new kind at once, which fades in as it shows again.
        if (sky->field.kind != ambient->kind)
            reseed(ambient, sky, ambient->kind);
        return false;
    }
    place(sky);
    // Fading out, toward a new kind, none or for want of input, and in again.
    bool wanted = ambient->kind != SH_AMBIENT_NONE && sky->field.kind == ambient->kind &&
                  !resting;
    sky->leaving = !wanted;
    sky->presence += (wanted ? dt : -dt) / FADE_MS;
    if (sky->presence > 1)
        sky->presence = 1;
    if (sky->presence <= 0 && !wanted) {
        sky->presence = 0;
        if (sky->field.kind != ambient->kind)
            reseed(ambient, sky, ambient->kind);
        if (ambient->kind == SH_AMBIENT_NONE || resting) {
            wlr_scene_node_set_enabled(&sky->tree->node, false);
            sky->paused = resting ? "idle" : "off";
            return false;
        }
    }
    if (sky->presence < 0)
        sky->presence = 0;
    sky->paused = NULL;
    if (sky->count != count_of(ambient, sky->field.kind))
        fit_count(sky, count_of(ambient, sky->field.kind));
    sh_ambient_step(&sky->field, sky->particles, sky->count, dt * ambient->speed);
    draw(sky);
    return true;
}

static void set_ticking(struct sh_ambient *ambient, int ms) {
    ambient->ticking = ms > 0;
    wl_event_source_timer_update(ambient->timer, ms);
}

/* Steps every output's particles, making a sky for an output without one, and lets go of the
 * skies of an output faded out to none. */
static void tick(struct sh_ambient *ambient) {
    struct sh_server *server = ambient->server;
    int64_t now = now_ms();
    // After a stall (another VT, a pause), as if a few steps went by.
    double dt = ambient->ticked ? (double)(now - ambient->ticked) : 0;
    dt = dt < 0 ? 0 : dt > 4 * TICK_MS ? 4 * TICK_MS : dt;
    ambient->ticked = now;
    bool resting = idle(ambient, now), more = false;
    ambient->asleep = false;
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        struct sh_ambient_sky *sky = output->ambient;
        if (output->mirror) { // out of the layout, it shows another output's frames
            if (sky)
                free_sky(sky);
            continue;
        }
        if (!sky && ambient->kind != SH_AMBIENT_NONE)
            sky = make_sky(ambient, output);
        if (!sky)
            continue;
        more |= step_sky(ambient, sky, dt, resting);
        if (sky->paused && (!strcmp(sky->paused, "idle") || !strcmp(sky->paused, "dimmed")))
            ambient->asleep = true;
        if (ambient->kind == SH_AMBIENT_NONE && sky->field.kind == SH_AMBIENT_NONE)
            free_sky(sky);
    }
    if (!more)
        ambient->ticked = 0;
    set_ticking(ambient, more ? TICK_MS : 0);
}

static int ambient_timer(void *data) {
    tick(data);
    return 0;
}

/* Steps soon. */
static void poke(struct sh_ambient *ambient) {
    if (!ambient->ticking)
        set_ticking(ambient, 1);
}

static void set_kind(struct sh_ambient *ambient, enum sh_ambient_kind kind) {
    if (kind < SH_AMBIENT_NONE || kind >= SH_AMBIENT_KINDS)
        kind = SH_AMBIENT_NONE;
    if (kind != SH_AMBIENT_NONE)
        ambient->last = kind;
    if (kind == ambient->kind)
        return;
    wlr_log(WLR_INFO, "Ambient particles: %s", sh_ambient_kind_name(kind));
    ambient->kind = kind;
    poke(ambient);
}

void ambient_switch(struct sh_server *server, int choice) {
    struct sh_ambient *ambient = server->ambient;
    if (!ambient)
        return;
    enum sh_ambient_kind kind;
    switch (choice) {
    case SH_AMBIENT_TOGGLE:
        kind = ambient->kind != SH_AMBIENT_NONE ? SH_AMBIENT_NONE : ambient->last;
        break;
    case SH_AMBIENT_NEXT:
        kind = (ambient->kind + 1) % SH_AMBIENT_KINDS;
        break;
    default:
        kind = (enum sh_ambient_kind)(choice - SH_AMBIENT_OFF);
        break;
    }
    set_kind(ambient, kind);
}

void ambient_frame(struct sh_output *output) {
    struct sh_ambient *ambient = output->server->ambient;
    if (!ambient || ambient->ticking || ambient->kind == SH_AMBIENT_NONE)
        return;
    // Whatever paused it here (the lock, a fullscreen window, the monitor off) drew a frame as
    // it went; one that comes along without a sky yet gets one.
    struct sh_ambient_sky *sky = output->ambient;
    if ((!sky || (sky->paused && strcmp(sky->paused, "idle"))) && !paused_why(ambient, output))
        poke(ambient);
}

void ambient_input(struct sh_server *server) {
    struct sh_ambient *ambient = server->ambient;
    if (ambient && ambient->asleep && !ambient->ticking)
        poke(ambient);
}

void ambient_forget_output(struct sh_output *output) {
    if (output->ambient)
        free_sky(output->ambient);
}

void ambient_reload(struct sh_server *server) {
    struct sh_ambient *ambient = server->ambient;
    if (!ambient)
        return;
    const struct sh_settings *settings = server_settings(server);
    const char *idle_ms = getenv("PAW_TEST_AMBIENT_IDLE_MS");
    ambient->idle_ms = idle_ms && *idle_ms ? atoll(idle_ms) : IDLE_MS;
    ambient->count = settings->ambient_count > 0 ? settings->ambient_count : 0;
    ambient->speed = settings->ambient_speed > 0 ? settings->ambient_speed : 1;
    // The action's choice stands until appearance.ambient.kind changes, as a profile does.
    enum sh_ambient_kind configured = (enum sh_ambient_kind)settings->ambient;
    if (configured != ambient->configured) {
        ambient->configured = configured;
        set_kind(ambient, configured);
    }
    if (ambient->kind != SH_AMBIENT_NONE)
        poke(ambient); // the count or the speed may have changed
}

void ambient_init(struct sh_server *server) {
    struct sh_ambient *ambient = calloc(1, sizeof(*ambient));
    if (!ambient)
        return;
    ambient->server = server;
    ambient->last = SH_AMBIENT_SAKURA;
    ambient->timer = wl_event_loop_add_timer(wl_display_get_event_loop(server->wl_display),
                                             ambient_timer, ambient);
    // Over the wallpaper (the background layer), under the bottom layer and the windows.
    ambient->tree = wlr_scene_tree_create(&server->scene->tree);
    if (!ambient->timer || !ambient->tree) {
        if (ambient->timer)
            wl_event_source_remove(ambient->timer);
        if (ambient->tree)
            wlr_scene_node_destroy(&ambient->tree->node);
        free(ambient);
        return;
    }
    wlr_scene_node_place_above(&ambient->tree->node, &server->layer_trees[0]->node);
    server->ambient = ambient;
    ambient_reload(server);
}

void ambient_finish(struct sh_server *server) {
    struct sh_ambient *ambient = server->ambient;
    if (!ambient)
        return;
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        if (output->ambient)
            free_sky(output->ambient);
    }
    wl_event_source_remove(ambient->timer);
    wlr_scene_node_destroy(&ambient->tree->node);
    free(ambient);
    server->ambient = NULL;
}

/* Each particle of the output named `name` as it is drawn: its box in layout coordinates, its
 * opacity in thousandths, the picture it shows and its quarter turns; nothing for one not drawn. */
static void describe_particles(struct sh_server *server, int fd, const char *name) {
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        if (strcmp(output->wlr_output->name, name))
            continue;
        control_reply(fd, "ok\n");
        struct sh_ambient_sky *sky = output->ambient;
        for (int i = 0; sky && !sky->paused && i < sky->count; ++i) {
            struct wlr_scene_buffer *node = sky->nodes[i];
            if (!node->node.enabled || sky->drawn[i] < 0)
                continue;
            char line[128];
            snprintf(line, sizeof(line), "%d\t%d\t%d\t%d\t%ld\t%d\t%d\n",
                     sky->tree->node.x + node->node.x, sky->tree->node.y + node->node.y,
                     node->dst_width, node->dst_height, lround(node->opacity * 1000),
                     sky->drawn[i], (int)node->transform);
            control_reply(fd, line);
        }
        return;
    }
    control_reply(fd, "error: no output named that\n");
}

void describe_ambient(struct sh_server *server, int fd, const char *arguments) {
    // A line for each output in the layout: its name, the kind it draws, how many particles it
    // has, and "shown", "fading" (out, toward none or another kind, or for want of input), or why
    // it shows none (off, locked, away, display_off, fullscreen, dimmed, idle). With an output's
    // name, its particles instead (describe_particles).
    struct sh_ambient *ambient = server->ambient;
    if (!ambient) {
        control_reply(fd, "error: there are no ambient particles\n");
        return;
    }
    if (arguments && *arguments) {
        describe_particles(server, fd, arguments);
        return;
    }
    control_reply(fd, "ok\n");
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        if (output->mirror)
            continue;
        struct sh_ambient_sky *sky = output->ambient;
        const char *state = !sky          ? "off"
                            : sky->paused  ? sky->paused
                            : sky->leaving ? "fading"
                                           : "shown";
        char line[256];
        snprintf(line, sizeof(line), "%s\t%s\t%d\t%s\n", output->wlr_output->name,
                 sh_ambient_kind_name(sky ? sky->field.kind : SH_AMBIENT_NONE),
                 sky ? sky->count : 0, state);
        control_reply(fd, line);
    }
}
