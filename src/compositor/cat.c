/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The desktop cat (Lua `cat`, the cat action): a pixel-art cat drawn over the windows, under
 * fullscreen windows, the panels and the lock, that lives on the focused window's top edge.
 * src/cat.c decides what it does; this shows it the windows and monitors, steps it and draws it.
 * It takes no input. It is stepped on a timer of its own, every frame while it moves and seldom
 * while it sits or sleeps; each output's frame looks whether what it stands on moved (whatever
 * moves a window draws a frame), and pointer motion and other input reach it as they come. While
 * it is hidden (the session locked, a fullscreen window, its monitor off) nothing runs at all. */
#include "server.h"
#include "paw/cat.h"

enum { CAT_AREAS = 16, CAT_WINDOWS = 128, CAT_LEDGES = 512 };

/* What the cat sees, made afresh for each step. */
struct sight {
    struct sh_cat_area areas[CAT_AREAS];
    struct sh_output *outputs[CAT_AREAS];
    int area_count;
    struct sh_cat_window windows[CAT_WINDOWS];
    int window_count;
    struct sh_cat_ledge tops[CAT_LEDGES], ledges[CAT_LEDGES];
    struct sh_cat_world world;
};

/* What an output's frame compares with how things stood at the last step: why the cat is hidden,
 * the urgent window, and the box of what it stands on. */
struct glance {
    const char *hidden;
    uint32_t urgent;
    struct wlr_box held;
};

struct sh_cat_view {
    struct sh_server *server;
    bool shown;      // turned on
    bool configured; // cat.enabled as last read
    bool placed;     // `cat` has turned up somewhere since it was shown
    struct sh_cat cat;
    struct wl_event_source *timer;
    int64_t due; // when the timer fires, 0 while it is not set
    struct glance last;
    struct sight sight;
    struct wlr_scene_tree *tree;
    struct wlr_scene_buffer *sprite, *z;
    struct wlr_buffer *frames[SH_CAT_FRAMES], *z_buffer;
    int coat, scale;    // what the buffers are painted in, and the size of a cell
    int drawn_frame;    // the frame the sprite shows, -1 for none
    int64_t sleep_ms;   // without input this long, it sleeps
};

static const char *const coat_names[SH_CAT_COATS] = {"ginger", "black", "cream", "grey"};

static bool no_input(struct wlr_scene_buffer *buffer, double *sx, double *sy) { return false; }

/* The coat cat.coat names, or the one appearance.color_scheme suits: a black cat on a light
 * desktop, a cream one on a dark one. */
static int wanted_coat(struct sh_server *server) {
    const struct sh_settings *settings = server_settings(server);
    if (settings->cat_coat >= 0 && settings->cat_coat < SH_CAT_COATS)
        return settings->cat_coat;
    return settings->color_scheme == SH_COLOR_SCHEME_LIGHT  ? SH_CAT_COAT_INK
           : settings->color_scheme == SH_COLOR_SCHEME_DARK ? SH_CAT_COAT_CREAM
                                                            : SH_CAT_COAT_GINGER;
}

static void drop_buffers(struct sh_cat_view *view) {
    for (int i = 0; i < SH_CAT_FRAMES; ++i) {
        wlr_buffer_drop(view->frames[i]);
        view->frames[i] = NULL;
    }
    wlr_buffer_drop(view->z_buffer);
    view->z_buffer = NULL;
    view->drawn_frame = -1;
}

static struct wlr_buffer *frame_buffer(struct sh_cat_view *view, enum sh_cat_frame frame) {
    if (!view->frames[frame]) {
        uint32_t *pixels = malloc(SH_CAT_ART_WIDTH * SH_CAT_ART_HEIGHT * sizeof(*pixels));
        if (!pixels)
            return NULL;
        sh_cat_paint(pixels, frame, view->coat);
        view->frames[frame] = sh_pixel_buffer(pixels, SH_CAT_ART_WIDTH, SH_CAT_ART_HEIGHT);
    }
    return view->frames[frame];
}

/* The box a window is drawn in, its border included, in layout coordinates. */
static struct wlr_box window_box(struct sh_toplevel *toplevel) {
    struct wlr_box geometry = frame_geometry(toplevel);
    struct wlr_box box = {toplevel->scene_tree->node.x + toplevel->content->node.x,
                          toplevel->scene_tree->node.y + toplevel->content->node.y,
                          geometry.width, geometry.height};
    int border = server_settings(toplevel->server)->border_width;
    if (border > 0 && !frameless(toplevel, toplevel_output(toplevel))) {
        box.x -= border;
        box.y -= border;
        box.width += 2 * border;
        box.height += 2 * border;
    }
    return box;
}

/* Whether the cat may stand on the window: shown, an ordinary window with its number, and not
 * fullscreen (which hides the cat on its monitor). */
static bool standable(struct sh_toplevel *toplevel) {
    return toplevel->id && !toplevel->unmanaged && !toplevel->fullscreen &&
           toplevel_mapped(toplevel) && toplevel_visible(toplevel) && toplevel->content &&
           toplevel->scene_tree->node.enabled;
}

static struct sh_toplevel *window_numbered(struct sh_server *server, uint32_t id) {
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel->id == id)
            return toplevel;
    }
    return NULL;
}

static bool shows_fullscreen(struct sh_server *server, struct wlr_output *output) {
    if (!output)
        return false;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (!toplevel->unmanaged && toplevel->fullscreen && toplevel_mapped(toplevel) &&
            toplevel_visible(toplevel) && !strcmp(toplevel->output, output->name))
            return true;
    }
    return false;
}

/* The window that has been urgent the longest and stands where the cat could go, else 0. */
static uint32_t urgent_window(struct sh_server *server) {
    struct sh_toplevel *toplevel, *oldest = NULL;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel->urgent && standable(toplevel) &&
            (!oldest || toplevel->urgent_order < oldest->urgent_order))
            oldest = toplevel;
    }
    return oldest ? oldest->id : 0;
}

/* The output the cat's feet are on, else the nearest; NULL without one. */
static struct sh_output *cat_output(struct sh_cat_view *view) {
    struct sh_server *server = view->server;
    struct sh_output *output, *nearest = NULL;
    double best = INFINITY;
    wl_list_for_each(output, &server->outputs, link) {
        struct wlr_box box;
        wlr_output_layout_get_box(server->output_layout, output->wlr_output, &box);
        double x = view->cat.x, y = view->cat.y - 0.5;
        double dx = x < box.x ? box.x - x : x >= box.x + box.width ? x - box.x - box.width + 1 : 0;
        double dy = y < box.y ? box.y - y : y >= box.y + box.height ? y - box.y - box.height + 1 : 0;
        if (dx + dy < best) {
            best = dx + dy;
            nearest = output;
        }
    }
    return nearest;
}

/* The box of what the cat stands on: the window's, or the free area of its monitor for a floor. */
static struct wlr_box held_box(struct sh_cat_view *view) {
    struct sh_cat *cat = &view->cat;
    if (!view->placed || !cat->grounded)
        return (struct wlr_box){0};
    if (cat->on) {
        struct sh_toplevel *toplevel = window_numbered(view->server, cat->on);
        return toplevel && standable(toplevel) ? window_box(toplevel) : (struct wlr_box){0};
    }
    struct sh_output *output = cat_output(view);
    if (!output)
        return (struct wlr_box){0};
    struct sh_rect usable = usable_area(view->server, output->wlr_output);
    return (struct wlr_box){usable.x, usable.y, usable.width, usable.height};
}

/* Why the cat is not drawn now, or NULL. */
static const char *hidden_why(struct sh_cat_view *view) {
    struct sh_server *server = view->server;
    if (!view->shown)
        return "off";
    if (server->locked)
        return "locked";
    if (wl_list_empty(&server->outputs))
        return "no_output";
    if (!view->placed)
        return NULL;
    struct sh_output *output = cat_output(view);
    if (!output || output->powered_off || !output->wlr_output->enabled)
        return "display_off";
    if (shows_fullscreen(server, output->wlr_output) ||
        shows_fullscreen(server, focused_output(server)))
        return "fullscreen";
    return NULL;
}

static void glance(struct sh_cat_view *view, struct glance *seen) {
    seen->hidden = hidden_why(view);
    seen->urgent = seen->hidden ? 0 : urgent_window(view->server);
    seen->held = seen->hidden ? (struct wlr_box){0} : held_box(view);
}

/* Builds what the cat sees into view->sight. */
static void look(struct sh_cat_view *view) {
    struct sh_server *server = view->server;
    struct sight *sight = &view->sight;
    sight->area_count = 0;
    struct wlr_output *focused = focused_output(server);
    int focused_area = -1;
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        if (sight->area_count == CAT_AREAS)
            break;
        struct wlr_box box;
        wlr_output_layout_get_box(server->output_layout, output->wlr_output, &box);
        if (wlr_box_empty(&box))
            continue;
        struct sh_rect usable = usable_area(server, output->wlr_output);
        if (output->wlr_output == focused)
            focused_area = sight->area_count;
        sight->outputs[sight->area_count] = output;
        sight->areas[sight->area_count++] =
            (struct sh_cat_area){box.x, box.y, box.width, box.height, usable.y,
                                 usable.y + usable.height};
    }
    // The windows front to back, as they are drawn.
    sight->window_count = 0;
    struct wlr_scene_tree *trees[WINDOW_LAYERS];
    window_layers(server, trees);
    for (int i = 0; i < WINDOW_LAYERS; ++i) {
        struct wlr_scene_node *node;
        wl_list_for_each_reverse(node, &trees[i]->children, link) {
            struct sh_node *owner = node->data;
            if (!owner || owner->kind != SH_NODE_TOPLEVEL || sight->window_count == CAT_WINDOWS)
                continue;
            struct sh_toplevel *toplevel = owner->owner;
            if (&toplevel->scene_tree->node != node || !node->enabled || !standable(toplevel))
                continue;
            struct wlr_box box = window_box(toplevel);
            sight->windows[sight->window_count++] =
                (struct sh_cat_window){toplevel->id, box.x, box.y, box.width, box.height};
        }
    }
    int tops = 0, ledges = 0;
    sh_cat_ledges(sight->windows, sight->window_count, sight->areas, sight->area_count,
                  view->cat.height, sight->tops, &tops, sight->ledges, &ledges, CAT_LEDGES);
    struct sh_toplevel *current = server->focused_toplevel;
    const struct sh_idle *idle = &server->idle;
    sight->world = (struct sh_cat_world){
        .areas = sight->areas,
        .area_count = sight->area_count,
        .focused_area = focused_area,
        .tops = sight->tops,
        .ledges = sight->ledges,
        .top_count = tops,
        .ledge_count = ledges,
        .focused = current && standable(current) ? current->id : 0,
        .urgent = urgent_window(server),
        .idle = now_ms() - idle->last_input >= view->sleep_ms ||
                (idle->done & (1u << SH_IDLE_DIM)),
    };
}

/* Sets the timer to step the cat in `ms` milliseconds (at least 1). */
static void step_in(struct sh_cat_view *view, int ms) {
    if (ms < 1)
        ms = 1;
    view->due = now_ms() + ms;
    wl_event_source_timer_update(view->timer, ms);
}

static void stop(struct sh_cat_view *view) {
    view->due = 0;
    wl_event_source_timer_update(view->timer, 0);
}

static void draw(struct sh_cat_view *view, int64_t now) {
    struct sh_cat *cat = &view->cat;
    struct sh_cat_pose pose;
    sh_cat_pose(cat, now, &pose);
    if ((int)pose.frame != view->drawn_frame) {
        struct wlr_buffer *buffer = frame_buffer(view, pose.frame);
        if (buffer) {
            wlr_scene_buffer_set_buffer(view->sprite, buffer);
            view->drawn_frame = pose.frame;
        }
    }
    wlr_scene_buffer_set_transform(view->sprite, pose.left ? WL_OUTPUT_TRANSFORM_FLIPPED
                                                           : WL_OUTPUT_TRANSFORM_NORMAL);
    // Squashed about its feet, as it lands.
    int width = (int)lround(SH_CAT_ART_WIDTH * view->scale * (1 + pose.squash));
    int height = (int)lround(SH_CAT_ART_HEIGHT * view->scale * (1 - pose.squash));
    wlr_scene_buffer_set_dest_size(view->sprite, width, height);
    wlr_scene_node_set_position(&view->sprite->node, (int)lround(cat->x - width / 2.0),
                                (int)lround(cat->y) - height);
    // The "z" rises from its head, which lies forward as it sleeps.
    wlr_scene_node_set_enabled(&view->z->node, pose.z);
    if (pose.z) {
        if (!view->z_buffer) {
            uint32_t *pixels = malloc(SH_CAT_Z_SIZE * SH_CAT_Z_SIZE * sizeof(*pixels));
            if (pixels) {
                sh_cat_paint_z(pixels, view->coat);
                view->z_buffer = sh_pixel_buffer(pixels, SH_CAT_Z_SIZE, SH_CAT_Z_SIZE);
            }
        }
        if (view->z->buffer != view->z_buffer)
            wlr_scene_buffer_set_buffer(view->z, view->z_buffer);
        int size = SH_CAT_Z_SIZE * view->scale;
        wlr_scene_buffer_set_dest_size(view->z, size, size);
        int forward = (pose.left ? -1 : 1) * (4 + 2 * pose.z_step) * view->scale;
        wlr_scene_node_set_position(&view->z->node, (int)lround(cat->x) + forward - size / 2,
                                    (int)lround(cat->y) - (12 + 3 * pose.z_step) * view->scale -
                                        size);
    }
}

/* Takes up cat.scale and the coat; true when either changed. */
static bool restyle(struct sh_cat_view *view) {
    int coat = wanted_coat(view->server), scale = server_settings(view->server)->cat_scale;
    if (scale < 1)
        scale = 1;
    if (coat == view->coat && scale == view->scale)
        return false;
    if (coat != view->coat)
        drop_buffers(view);
    view->coat = coat;
    view->scale = scale;
    view->cat.width = SH_CAT_ART_WIDTH * scale;
    view->cat.height = SH_CAT_ART_HEIGHT * scale;
    return true;
}

static void step(struct sh_cat_view *view) {
    struct sh_server *server = view->server;
    view->due = 0;
    const char *hidden = hidden_why(view);
    if (hidden) {
        wlr_scene_node_set_enabled(&view->tree->node, false);
        glance(view, &view->last);
        return; // an output's frame shows it again once it may be
    }
    look(view);
    int64_t now = now_ms();
    if (!view->placed) {
        sh_cat_init(&view->cat, SH_CAT_ART_WIDTH * view->scale, SH_CAT_ART_HEIGHT * view->scale,
                    NULL, 0, (uint32_t)now_ns() | 1, now);
        sh_cat_arrive(&view->cat, &view->sight.world, now);
        view->placed = true;
        if (hidden_why(view)) { // it turned up on a monitor showing a fullscreen window
            step(view);
            return;
        }
    }
    int next = sh_cat_step(&view->cat, &view->sight.world, now);
    if (!view->sight.world.idle) {
        // Be there as it gets sleepy.
        int64_t sleepy = server->idle.last_input + view->sleep_ms - now;
        if (sleepy < next)
            next = (int)(sleepy < 1 ? 1 : sleepy);
    }
    draw(view, now);
    wlr_scene_node_set_enabled(&view->tree->node, true);
    glance(view, &view->last);
    step_in(view, next);
}

static int cat_timer(void *data) {
    step(data);
    return 0;
}

/* Steps it soon unless it is stepped every frame anyway. */
static void poke(struct sh_cat_view *view) {
    if (!view->due || view->due - now_ms() > 1)
        step_in(view, 1);
}

static void set_shown(struct sh_cat_view *view, bool shown) {
    if (view->shown == shown)
        return;
    view->shown = shown;
    view->placed = false;
    if (shown) {
        step_in(view, 1);
    } else {
        stop(view);
        wlr_scene_node_set_enabled(&view->tree->node, false);
        glance(view, &view->last);
    }
    wlr_log(WLR_INFO, "The cat is %s", shown ? "out" : "away");
}

void cat_switch(struct sh_server *server, int choice) {
    struct sh_cat_view *view = server->cat;
    if (view)
        set_shown(view, choice == SH_CAT_ON || (choice == SH_CAT_TOGGLE && !view->shown));
}

void cat_frame(struct sh_server *server) {
    struct sh_cat_view *view = server->cat;
    if (!view || !view->shown || (view->due && view->due - now_ms() <= 20))
        return;
    struct glance now;
    glance(view, &now);
    if (now.hidden != view->last.hidden || now.urgent != view->last.urgent ||
        !wlr_box_equal(&now.held, &view->last.held))
        poke(view);
}

void cat_input(struct sh_server *server) {
    struct sh_cat_view *view = server->cat;
    if (view && view->shown && view->placed && sh_cat_wake(&view->cat, now_ms()))
        poke(view);
}

void cat_pointer_moved(struct sh_server *server) {
    struct sh_cat_view *view = server->cat;
    if (view && view->shown && view->placed && !view->last.hidden &&
        sh_cat_pointer(&view->cat, server->cursor->x, server->cursor->y, now_ms()))
        poke(view);
}

void cat_reload(struct sh_server *server) {
    struct sh_cat_view *view = server->cat;
    if (!view)
        return;
    const char *sleep = getenv("PAW_TEST_CAT_SLEEP_MS");
    view->sleep_ms = sleep && *sleep ? atoll(sleep) : server_settings(server)->cat_sleep * 1000LL;
    bool restyled = restyle(view);
    bool configured = server_settings(server)->cat;
    if (configured != view->configured) {
        view->configured = configured;
        set_shown(view, configured);
    } else if (restyled && view->shown) {
        view->drawn_frame = -1;
        poke(view);
    }
}

void cat_init(struct sh_server *server) {
    struct sh_cat_view *view = calloc(1, sizeof(*view));
    if (!view)
        return;
    view->server = server;
    view->coat = -1;
    view->drawn_frame = -1;
    view->timer = wl_event_loop_add_timer(wl_display_get_event_loop(server->wl_display),
                                          cat_timer, view);
    // Over every window but fullscreen ones, under the panels, the overlays and the lock.
    view->tree = wlr_scene_tree_create(&server->scene->tree);
    view->sprite = view->tree ? wlr_scene_buffer_create(view->tree, NULL) : NULL;
    view->z = view->tree ? wlr_scene_buffer_create(view->tree, NULL) : NULL;
    if (!view->timer || !view->sprite || !view->z) {
        if (view->timer)
            wl_event_source_remove(view->timer);
        if (view->tree)
            wlr_scene_node_destroy(&view->tree->node);
        free(view);
        return;
    }
    wlr_scene_node_place_above(&view->tree->node, &server->above_windows->node);
    wlr_scene_node_set_enabled(&view->tree->node, false);
    struct wlr_scene_buffer *buffers[] = {view->sprite, view->z};
    for (size_t i = 0; i < 2; ++i) {
        buffers[i]->point_accepts_input = no_input;
        wlr_scene_buffer_set_filter_mode(buffers[i], WLR_SCALE_FILTER_NEAREST);
    }
    server->cat = view;
    view->last.hidden = "off";
    cat_reload(server);
}

void cat_finish(struct sh_server *server) {
    struct sh_cat_view *view = server->cat;
    if (!view)
        return;
    wl_event_source_remove(view->timer);
    wlr_scene_node_destroy(&view->tree->node);
    drop_buffers(view);
    free(view);
    server->cat = NULL;
}

void describe_cat(struct sh_server *server, int fd, const char *arguments) {
    // Whether it is turned on; "shown", or why it is not drawn (off, locked, fullscreen,
    // display_off, no_output); the output it is on, where its feet are, what it does, the frame
    // it shows, its coat, and what it stands on: the window's title, "floor", or "-" in the air.
    struct sh_cat_view *view = server->cat;
    if (!view) {
        control_reply(fd, "error: there is no cat\n");
        return;
    }
    const struct sh_cat *cat = &view->cat;
    const char *hidden = hidden_why(view);
    struct sh_output *output = view->placed ? cat_output(view) : NULL;
    char title[256] = "-";
    if (view->placed && cat->grounded && !cat->on) {
        snprintf(title, sizeof(title), "floor");
    } else if (view->placed && cat->grounded) {
        struct sh_toplevel *toplevel = window_numbered(server, cat->on);
        copy_field(title, sizeof(title), toplevel ? toplevel_title(toplevel) : "-");
    }
    struct sh_cat_pose pose;
    sh_cat_pose(cat, now_ms(), &pose);
    char line[512];
    snprintf(line, sizeof(line), "ok\n%s\t%s\t%s\t%ld\t%ld\t%s\t%s\t%s\t%s\n",
             view->shown ? "on" : "off", hidden ? hidden : view->placed ? "shown" : "waiting",
             output ? output->wlr_output->name : "-", view->placed ? lround(cat->x) : 0,
             view->placed ? lround(cat->y) : 0,
             view->placed ? sh_cat_state_name(cat->state) : "-",
             view->placed ? sh_cat_frame_name(pose.frame) : "-",
             view->coat >= 0 ? coat_names[view->coat] : "-", title);
    control_reply(fd, line);
}
