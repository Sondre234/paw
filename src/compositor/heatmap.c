/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The pointer heatmap: how long the pointer rested and how often it clicked in each part of each
 * output, counted in a grid of cells over it (src/heatmap.c) for as long as the compositor runs,
 * in memory alone; and the overlay the heatmap action shows over every output, the grid blurred
 * and coloured from cold to hot with a legend in a corner, drawn again twice a second while it
 * shows and fading in and out as the animations do. */
#include "server.h"
#include "paw/heatmap.h"

/* How long a pointer at rest keeps counting: past this without moving or clicking, its user has
 * likely looked away, and an afternoon away would outweigh everything else. Under --headless,
 * PAW_TEST_HEATMAP_REST_MS sets it for tests. */
#define REST_MS 10000
/* How often the overlay is drawn again while it shows, and how long it fades in or out before
 * animations.speed. */
#define REFRESH_MS 500
#define FADE_MS 200
/* The overlay's opacity at the hot end, which leaves the desktop showing through. */
#define OPACITY 0.65
/* How far the legend stands off the corner of its output's usable area. */
#define LEGEND_MARGIN 16

/* An output's grid, its place in the layout as the grid was sized for it (empty while it is out
 * of the layout), and its part of the overlay while that shows: the picture of its grid and the
 * legend, with what the legend was drawn for and the corner it is in (enum corner). */
struct sh_heat_output {
    struct sh_heat_grid grid;
    struct wlr_box box;
    struct wlr_scene_buffer *picture, *legend;
    char legend_label[32];
    enum sh_heat_kind legend_kind;
    int legend_scale;
    int legend_corner;
};

/* The corners of an output's usable area a legend may stand in, the first preferred. */
enum corner { BOTTOM_RIGHT, BOTTOM_LEFT, TOP_RIGHT, TOP_LEFT, CORNERS };

struct sh_heatmap {
    /* Where the pointer was last seen (`seen`, at `x`, `y`): its output (NULL off every one)
     * and cell there, since when it has rested, and up to when its time there is counted. */
    bool seen;
    double x, y;
    struct sh_output *output;
    int column, row;
    int64_t rested_from, counted_to;
    int rest_ms;
    /* The overlay: whether it is wanted (else fading out, while `tree` is still there), what it
     * shows, its tree, under the drag icons and the lock, its fade, the timer that draws it
     * again, how many times it has been drawn, and what its legends say at the hot end. */
    bool shown;
    enum sh_heat_kind kind;
    struct wlr_scene_tree *tree;
    struct sh_fade fade;
    struct wl_event_source *timer;
    uint64_t drawn;
    char label[32];
    /* Every output's field in a row, and the blur's scratch, kept between drawings. */
    float *fields, *scratch;
    size_t fields_size, scratch_size;
};

static const char *const kind_names[SH_HEAT_KINDS] = {"motion", "clicks"};

static struct sh_heatmap *heatmap_of(struct sh_server *server) {
    if (server->heatmap)
        return server->heatmap;
    struct sh_heatmap *heat = calloc(1, sizeof(*heat));
    if (!heat)
        return NULL;
    const char *test = getenv("PAW_TEST_HEATMAP_REST_MS");
    int ms = test && headless_backend(server) ? atoi(test) : 0;
    heat->rest_ms = ms > 0 ? ms : REST_MS;
    sh_fade_init(&heat->fade, 0);
    return server->heatmap = heat;
}

/* Whether the pointer's rest and clicks count now: not while the session is locked. */
static bool counting(struct sh_server *server) {
    return !server->locked;
}

/* The time the pointer has rested in its cell and not yet counted, up to `now`: none past
 * `rest_ms` after it last moved. */
static uint32_t uncounted(const struct sh_heatmap *heat, int64_t now) {
    int64_t end = heat->rested_from + heat->rest_ms < now ? heat->rested_from + heat->rest_ms : now;
    return heat->output && end > heat->counted_to ? (uint32_t)(end - heat->counted_to) : 0;
}

/* Counts the pointer's rest up to `now` in its cell. */
static void count_rest(struct sh_server *server, int64_t now) {
    struct sh_heatmap *heat = server->heatmap;
    uint32_t rest = uncounted(heat, now);
    if (rest && counting(server))
        sh_heat_add(&heat->output->heat->grid, SH_HEAT_MOTION, heat->column, heat->row, rest);
    if (rest)
        heat->counted_to += rest;
}

/* The output in the layout at `x`, `y`, trying `last` first. */
static struct sh_output *output_at(struct sh_server *server, struct sh_output *last, double x,
                                   double y) {
    if (last && last->heat && wlr_box_contains_point(&last->heat->box, x, y))
        return last;
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        if (output->heat && wlr_box_contains_point(&output->heat->box, x, y))
            return output;
    }
    return NULL;
}

/* The pointer's rest so far counts in the cell it rested in, and where it moved to, a new rest
 * starts at `now`. */
static void follow_pointer(struct sh_server *server, int64_t now) {
    struct sh_heatmap *heat = server->heatmap;
    count_rest(server, now);
    double x = server->cursor->x, y = server->cursor->y;
    if (heat->seen && x == heat->x && y == heat->y)
        return;
    heat->seen = true;
    heat->x = x;
    heat->y = y;
    heat->output = output_at(server, heat->output, x, y);
    if (heat->output) {
        const struct wlr_box *box = &heat->output->heat->box;
        heat->column = (int)((x - box->x) / SH_HEAT_CELL);
        heat->row = (int)((y - box->y) / SH_HEAT_CELL);
    }
    heat->rested_from = heat->counted_to = now;
}

/* The pointer moved, or a focus change had its motion processed where it is, which goes on
 * resting. No allocation, no system call. */
void heatmap_motion(struct sh_server *server) {
    if (server->heatmap)
        follow_pointer(server, now_ms());
}

/* A button of the pointer went down or up: a press counts as a click where the pointer is, and
 * starts its rest there again. */
void heatmap_button(struct sh_server *server, const struct wlr_pointer_button_event *event) {
    struct sh_heatmap *heat = server->heatmap;
    if (!heat || event->state != WL_POINTER_BUTTON_STATE_PRESSED)
        return;
    int64_t now = now_ms();
    follow_pointer(server, now);
    heat->rested_from = heat->counted_to = now;
    if (heat->output && counting(server))
        sh_heat_add(&heat->output->heat->grid, SH_HEAT_CLICKS, heat->column, heat->row, 1);
}

static bool no_input(struct wlr_scene_buffer *buffer, double *sx, double *sy) {
    return false;
}

/* Takes an output's part of the overlay away. */
static void forget_overlay(struct sh_heat_output *heat) {
    if (heat->picture)
        wlr_scene_node_destroy(&heat->picture->node);
    if (heat->legend)
        wlr_scene_node_destroy(&heat->legend->node);
    heat->picture = heat->legend = NULL;
    heat->legend_label[0] = '\0';
}

/* A scene buffer in the overlay's tree that takes no input. */
static struct wlr_scene_buffer *overlay_buffer(struct sh_heatmap *heat) {
    struct wlr_scene_buffer *buffer = wlr_scene_buffer_create(heat->tree, NULL);
    if (buffer) {
        buffer->point_accepts_input = no_input;
        wlr_scene_buffer_set_opacity(buffer, (float)sh_fade_value(&heat->fade, now_ms()));
    }
    return buffer;
}

/* How much of `field` (the output's) lies under `box`, in the output's logical coordinates. */
static double heat_under(const struct sh_heat_grid *grid, const float *field, struct wlr_box box) {
    int left = box.x / SH_HEAT_CELL, top = box.y / SH_HEAT_CELL;
    int right = (box.x + box.width - 1) / SH_HEAT_CELL;
    int bottom = (box.y + box.height - 1) / SH_HEAT_CELL;
    double sum = 0;
    for (int row = top < 0 ? 0 : top; row <= bottom && row < grid->rows; ++row) {
        for (int column = left < 0 ? 0 : left; column <= right && column < grid->columns; ++column)
            sum += field[(size_t)row * grid->columns + column];
    }
    return sum;
}

/* Where the legend stands in `corner` of `output`'s usable area, in layout coordinates. */
static struct wlr_box legend_box(struct sh_output *output, int corner) {
    struct sh_rect usable = usable_area(output->server, output->wlr_output);
    bool right = corner == BOTTOM_RIGHT || corner == TOP_RIGHT;
    bool bottom = corner == BOTTOM_RIGHT || corner == BOTTOM_LEFT;
    return (struct wlr_box){
        right ? usable.x + usable.width - SH_HEAT_LEGEND_WIDTH - LEGEND_MARGIN
              : usable.x + LEGEND_MARGIN,
        bottom ? usable.y + usable.height - SH_HEAT_LEGEND_HEIGHT - LEGEND_MARGIN
               : usable.y + LEGEND_MARGIN,
        SH_HEAT_LEGEND_WIDTH, SH_HEAT_LEGEND_HEIGHT};
}

/* The corner of `output` its legend hides the least of `field` in: the bottom-right one unless
 * another is clearly cooler, and the one it is in unless another is much cooler, so that it does
 * not hop about as the heat changes. */
static int legend_corner(struct sh_output *output, const float *field, bool placed) {
    const struct sh_heat_output *own = output->heat;
    double under[CORNERS];
    int coolest = BOTTOM_RIGHT;
    for (int corner = 0; corner < CORNERS; ++corner) {
        struct wlr_box box = legend_box(output, corner);
        box.x -= own->box.x;
        box.y -= own->box.y;
        under[corner] = heat_under(&own->grid, field, box);
        if (under[corner] < under[coolest] * 0.5)
            coolest = corner;
    }
    int current = placed ? own->legend_corner : BOTTOM_RIGHT;
    return under[coolest] < under[current] * 0.25 ? coolest : current;
}

/* Gives `output` its legend for `most` of the overlay's kind, drawn anew only when it would
 * look different, in the corner of its usable area that hides the least of its `field`. */
static void place_legend(struct sh_heatmap *heat, struct sh_output *output, const float *field,
                         double most) {
    struct sh_heat_output *own = output->heat;
    int scale = (int)ceil(output->wlr_output->scale);
    scale = scale < 1 ? 1 : scale;
    own->legend_corner = legend_corner(output, field, own->legend);
    if (!own->legend)
        own->legend = overlay_buffer(heat);
    if (!own->legend)
        return;
    if (strcmp(own->legend_label, heat->label) || own->legend_kind != heat->kind ||
        own->legend_scale != scale) {
        size_t size = (size_t)SH_HEAT_LEGEND_WIDTH * SH_HEAT_LEGEND_HEIGHT * scale * scale;
        uint32_t *pixels = malloc(size * sizeof(*pixels));
        if (!pixels)
            return;
        sh_heat_legend(pixels, scale, heat->kind, most);
        struct wlr_buffer *buffer = sh_pixel_buffer(pixels, SH_HEAT_LEGEND_WIDTH * scale,
                                                    SH_HEAT_LEGEND_HEIGHT * scale);
        if (!buffer)
            return;
        wlr_scene_buffer_set_buffer(own->legend, buffer);
        wlr_buffer_drop(buffer);
        wlr_scene_buffer_set_dest_size(own->legend, SH_HEAT_LEGEND_WIDTH, SH_HEAT_LEGEND_HEIGHT);
        snprintf(own->legend_label, sizeof(own->legend_label), "%s", heat->label);
        own->legend_kind = heat->kind;
        own->legend_scale = scale;
    }
    struct wlr_box box = legend_box(output, own->legend_corner);
    wlr_scene_node_set_position(&own->legend->node, box.x, box.y);
}

/* Draws the overlay again over every output in the layout from the grids as they are, the
 * pointer's rest so far counted: the outputs share one scale, up to the hottest place of all. */
static void draw_overlay(struct sh_server *server) {
    struct sh_heatmap *heat = server->heatmap;
    count_rest(server, now_ms());
    size_t cells = 0, side = 0;
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        const struct sh_heat_grid *grid = output->heat ? &output->heat->grid : NULL;
        if (!grid || wlr_box_empty(&output->heat->box) || !grid->cells[0])
            continue;
        cells += (size_t)grid->columns * grid->rows;
        size_t longer = (size_t)(grid->columns > grid->rows ? grid->columns : grid->rows);
        side = longer > side ? longer : side;
    }
    if (cells > heat->fields_size) {
        float *fields = realloc(heat->fields, cells * sizeof(*fields));
        if (!fields)
            return;
        heat->fields = fields;
        heat->fields_size = cells;
    }
    if (side > heat->scratch_size) {
        float *scratch = realloc(heat->scratch, side * sizeof(*scratch));
        if (!scratch)
            return;
        heat->scratch = scratch;
        heat->scratch_size = side;
    }
    float most = 0;
    uint32_t hottest = 0;
    size_t at = 0;
    wl_list_for_each(output, &server->outputs, link) {
        const struct sh_heat_grid *grid = output->heat ? &output->heat->grid : NULL;
        if (!grid || wlr_box_empty(&output->heat->box) || !grid->cells[0])
            continue;
        size_t count = (size_t)grid->columns * grid->rows;
        sh_heat_field(grid, heat->kind, heat->fields + at, heat->scratch);
        float peak = sh_heat_peak(heat->fields + at, count);
        most = peak > most ? peak : most;
        uint32_t cell = sh_heat_most(grid, heat->kind);
        hottest = cell > hottest ? cell : hottest;
        at += count;
    }
    sh_heat_label(heat->label, sizeof(heat->label), heat->kind, hottest);
    at = 0;
    for_each_connected_output(output, server) {
        struct sh_heat_output *own = output->heat;
        if (!own)
            continue;
        const struct sh_heat_grid *grid = &own->grid;
        if (output->disabled || wlr_box_empty(&own->box) || !grid->cells[0]) {
            forget_overlay(own);
            continue;
        }
        size_t count = (size_t)grid->columns * grid->rows;
        uint32_t *pixels = malloc(count * sizeof(*pixels));
        if (!own->picture) {
            own->picture = overlay_buffer(heat);
            if (own->picture)
                wlr_scene_buffer_set_filter_mode(own->picture, WLR_SCALE_FILTER_BILINEAR);
        }
        if (pixels && own->picture) {
            sh_heat_paint(heat->fields + at, grid->columns, grid->rows, most, OPACITY, pixels);
            struct wlr_buffer *buffer = sh_pixel_buffer(pixels, grid->columns, grid->rows);
            if (buffer) {
                wlr_scene_buffer_set_buffer(own->picture, buffer);
                wlr_buffer_drop(buffer);
            }
            // The cells along the right and bottom edges reach past the output: only the part
            // over it is stretched over it.
            wlr_scene_buffer_set_source_box(
                own->picture, &(struct wlr_fbox){0, 0, (double)own->box.width / SH_HEAT_CELL,
                                                 (double)own->box.height / SH_HEAT_CELL});
            wlr_scene_buffer_set_dest_size(own->picture, own->box.width, own->box.height);
            wlr_scene_node_set_position(&own->picture->node, own->box.x, own->box.y);
        } else {
            free(pixels);
        }
        place_legend(heat, output, heat->fields + at, hottest);
        at += count;
    }
    ++heat->drawn;
}

static int refresh_overlay(void *data) {
    struct sh_server *server = data;
    struct sh_heatmap *heat = server->heatmap;
    if (heat && heat->shown) {
        // Under the lock nothing of it shows.
        if (!server->locked)
            draw_overlay(server);
        wl_event_source_timer_update(heat->timer, REFRESH_MS);
    }
    return 0;
}

static int fade_duration(struct sh_server *server) {
    const struct sh_settings *settings = server_settings(server);
    if (!settings->animations)
        return 0;
    double speed = settings->animation_speed > 0 ? settings->animation_speed : 1;
    return (int)lround(FADE_MS / speed);
}

/* Takes the overlay away at once. */
static void remove_overlay(struct sh_server *server) {
    struct sh_heatmap *heat = server->heatmap;
    if (!heat || !heat->tree)
        return;
    struct sh_output *output;
    for_each_connected_output(output, server) {
        if (output->heat)
            forget_overlay(output->heat);
    }
    wlr_scene_node_destroy(&heat->tree->node);
    heat->tree = NULL;
    heat->shown = false;
    if (heat->timer)
        wl_event_source_timer_update(heat->timer, 0);
    sh_fade_init(&heat->fade, 0);
}

static void show_overlay(struct sh_server *server, enum sh_heat_kind kind) {
    struct sh_heatmap *heat = server->heatmap;
    if (!heat->tree) {
        heat->tree = wlr_scene_tree_create(&server->scene->tree);
        if (!heat->tree)
            return;
        // Over the windows and panels, under what follows the pointer and the lock.
        wlr_scene_node_place_below(&heat->tree->node, &server->drag_icons->node);
        sh_fade_init(&heat->fade, 0);
    }
    if (!heat->timer)
        heat->timer = wl_event_loop_add_timer(wl_display_get_event_loop(server->wl_display),
                                              refresh_overlay, server);
    heat->shown = true;
    heat->kind = kind;
    sh_fade_to(&heat->fade, 1, now_ms(), fade_duration(server));
    draw_overlay(server);
    if (heat->timer)
        wl_event_source_timer_update(heat->timer, REFRESH_MS);
    schedule_frames(server);
}

/* The overlay fades out, drawn no more meanwhile, and goes. */
static void hide_overlay(struct sh_server *server) {
    struct sh_heatmap *heat = server->heatmap;
    heat->shown = false;
    if (heat->timer)
        wl_event_source_timer_update(heat->timer, 0);
    sh_fade_to(&heat->fade, 0, now_ms(), fade_duration(server));
    schedule_frames(server);
}

/* The heatmap action (enum sh_heatmap_choice): toggling shows the overlay of the kind it showed
 * last, motion at first, or hides it; motion or clicks shows that kind, or hides it while it
 * shows; reset forgets everything counted. */
void heatmap_action(struct sh_server *server, int choice) {
    struct sh_heatmap *heat = heatmap_of(server);
    if (!heat)
        return;
    if (choice == SH_HEATMAP_RESET) {
        struct sh_output *output;
        for_each_connected_output(output, server) {
            if (output->heat)
                sh_heat_grid_clear(&output->heat->grid);
        }
        heat->rested_from = heat->counted_to = now_ms();
        if (heat->shown)
            draw_overlay(server);
        wlr_log(WLR_INFO, "Heatmap: reset");
        return;
    }
    enum sh_heat_kind kind = choice == SH_HEATMAP_CLICKS   ? SH_HEAT_CLICKS
                             : choice == SH_HEATMAP_MOTION ? SH_HEAT_MOTION
                                                           : heat->kind;
    if (heat->shown && (choice == SH_HEATMAP_TOGGLE || kind == heat->kind))
        hide_overlay(server);
    else
        show_overlay(server, kind);
}

/* Before an output's frame: the overlay at its opacity now, and gone once it has faded out. True
 * while it is still fading. */
bool heatmap_tick(struct sh_server *server) {
    struct sh_heatmap *heat = server->heatmap;
    if (!heat || !heat->tree)
        return false;
    int64_t now = now_ms();
    bool fading = sh_fade_active(&heat->fade, now);
    if (!fading && !heat->shown) {
        remove_overlay(server);
        return false;
    }
    float opacity = (float)sh_fade_value(&heat->fade, now);
    struct sh_output *output;
    for_each_connected_output(output, server) {
        struct sh_heat_output *own = output->heat;
        if (own && own->picture)
            wlr_scene_buffer_set_opacity(own->picture, opacity);
        if (own && own->legend)
            wlr_scene_buffer_set_opacity(own->legend, opacity);
    }
    return fading;
}

/* The outputs were laid out: each in the layout gets a grid for its size, what it held resized
 * with it, and the overlay follows. */
void heatmap_arrange(struct sh_server *server) {
    struct sh_heatmap *heat = heatmap_of(server);
    if (!heat)
        return;
    int64_t now = now_ms();
    count_rest(server, now); // in its cell as it was
    struct sh_output *output;
    for_each_connected_output(output, server) {
        struct wlr_box box = {0};
        if (!output->disabled)
            wlr_output_layout_get_box(server->output_layout, output->wlr_output, &box);
        if (!output->heat && !wlr_box_empty(&box))
            output->heat = calloc(1, sizeof(*output->heat));
        if (!output->heat)
            continue;
        if (!wlr_box_empty(&box) &&
            !sh_heat_grid_resize(&output->heat->grid, sh_heat_cells(box.width),
                                 sh_heat_cells(box.height)))
            box = (struct wlr_box){0};
        output->heat->box = box;
    }
    // The pointer's cell on the grids as they are now.
    if (heat->seen) {
        heat->seen = false;
        follow_pointer(server, now);
    }
    if (heat->shown)
        draw_overlay(server);
}

/* `output` goes away, and its grid with it. */
void heatmap_forget_output(struct sh_output *output) {
    struct sh_heatmap *heat = output->server->heatmap;
    if (heat && heat->output == output) {
        heat->output = NULL;
        heat->seen = false;
    }
    if (!output->heat)
        return;
    forget_overlay(output->heat);
    sh_heat_grid_free(&output->heat->grid);
    free(output->heat);
    output->heat = NULL;
}

/* As the compositor stops: the overlay goes; the outputs' grids go with them. */
void heatmap_finish(struct sh_server *server) {
    struct sh_heatmap *heat = server->heatmap;
    if (!heat)
        return;
    remove_overlay(server);
    if (heat->timer)
        wl_event_source_remove(heat->timer);
    free(heat->fields);
    free(heat->scratch);
    free(heat);
    server->heatmap = NULL;
}

/* `get heatmap [OUTPUT] [motion|clicks]`: for each output in the layout (or the one named), a
 * line with its name, the kind, the grid's columns and rows, the cell's size and the output's
 * place and size in the layout (logical pixels), then a line for each row of cells, the
 * milliseconds the pointer rested in each (with motion, the default) or the clicks there,
 * tab-separated. The rest under way counts as far as it has come. */
void describe_heatmap(struct sh_server *server, int fd, const char *arguments) {
    char first[64] = "", second[64] = "", extra[2] = "";
    int words = arguments ? sscanf(arguments, "%63s %63s %1s", first, second, extra) : 0;
    const char *name = NULL;
    enum sh_heat_kind kind = SH_HEAT_MOTION;
    bool usable = words <= 2;
    const char *word[] = {first, second};
    for (int i = 0; i < words && i < 2; ++i) {
        if (!strcmp(word[i], kind_names[SH_HEAT_MOTION]))
            kind = SH_HEAT_MOTION;
        else if (!strcmp(word[i], kind_names[SH_HEAT_CLICKS]))
            kind = SH_HEAT_CLICKS;
        else if (!name)
            name = word[i];
        else
            usable = false;
    }
    if (!usable) {
        control_reply(fd, "error: usage: get heatmap [OUTPUT] [motion|clicks]\n");
        return;
    }
    struct sh_output *output;
    bool found = !name;
    wl_list_for_each(output, &server->outputs, link) {
        found = found || (output_named(output, name) && output->heat &&
                          !wlr_box_empty(&output->heat->box));
    }
    if (!found) {
        control_reply(fd, "error: no such output in the layout\n");
        return;
    }
    control_reply(fd, "ok\n");
    struct sh_heatmap *heat = server->heatmap;
    uint32_t rest = heat && counting(server) ? uncounted(heat, now_ms()) : 0;
    wl_list_for_each(output, &server->outputs, link) {
        const struct sh_heat_output *own = output->heat;
        if (!own || wlr_box_empty(&own->box) || !own->grid.cells[kind] ||
            (name && !output_named(output, name)))
            continue;
        const struct sh_heat_grid *grid = &own->grid;
        char header[192];
        snprintf(header, sizeof(header), "%s\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
                 output->wlr_output->name, kind_names[kind], grid->columns, grid->rows,
                 SH_HEAT_CELL, own->box.x, own->box.y, own->box.width, own->box.height);
        control_reply(fd, header);
        // Ten digits and a tab a cell, at most.
        char *line = malloc((size_t)grid->columns * 11 + 2);
        if (!line)
            return;
        bool resting = kind == SH_HEAT_MOTION && rest && heat->output == output;
        for (int row = 0; row < grid->rows; ++row) {
            size_t length = 0;
            for (int column = 0; column < grid->columns; ++column) {
                unsigned long long value = grid->cells[kind][(size_t)row * grid->columns + column];
                if (resting && row == heat->row && column == heat->column)
                    value = value + rest > UINT32_MAX ? UINT32_MAX : value + rest;
                length += (size_t)sprintf(line + length, column ? "\t%llu" : "%llu", value);
            }
            line[length++] = '\n';
            line[length] = '\0';
            control_reply(fd, line);
        }
        free(line);
    }
}

/* `get heatmap_overlay`: while the overlay is there, a line for each output it covers: its name,
 * "shown" or "hiding" (fading out), the kind, the opacity in thousandths, what the legend says at
 * the hot end, and how many times the overlay has been drawn. */
void describe_heatmap_overlay(struct sh_server *server, int fd, const char *arguments) {
    control_reply(fd, "ok\n");
    struct sh_heatmap *heat = server->heatmap;
    if (!heat || !heat->tree)
        return;
    double opacity = sh_fade_value(&heat->fade, now_ms());
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        if (!output->heat || !output->heat->picture)
            continue;
        char line[256];
        snprintf(line, sizeof(line), "%s\t%s\t%s\t%ld\t%s\t%llu\n", output->wlr_output->name,
                 heat->shown ? "shown" : "hiding", kind_names[heat->kind],
                 lround(opacity * 1000), heat->label, (unsigned long long)heat->drawn);
        control_reply(fd, line);
    }
}
