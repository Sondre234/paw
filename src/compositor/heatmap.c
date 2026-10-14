/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The pointer heatmap: how long the pointer rested and how often it clicked in each part of each
 * output, counted in a grid of cells over it (src/heatmap.c) for as long as the compositor runs,
 * in memory alone. */
#include "server.h"
#include "paw/heatmap.h"

/* How long a pointer at rest keeps counting: past this without moving or clicking, its user has
 * likely looked away, and an afternoon away would outweigh everything else. Under --headless,
 * PAW_TEST_HEATMAP_REST_MS sets it for tests. */
#define REST_MS 10000

/* An output's grid, and its place in the layout as the grid was sized for it (empty while it is
 * out of the layout). */
struct sh_heat_output {
    struct sh_heat_grid grid;
    struct wlr_box box;
};

struct sh_heatmap {
    /* Where the pointer was last seen (`seen`, at `x`, `y`): its output (NULL off every one)
     * and cell there, since when it has rested, and up to when its time there is counted. */
    bool seen;
    double x, y;
    struct sh_output *output;
    int column, row;
    int64_t rested_from, counted_to;
    int rest_ms;
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

/* The outputs were laid out: each in the layout gets a grid for its size, what it held resized
 * with it. */
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
    sh_heat_grid_free(&output->heat->grid);
    free(output->heat);
    output->heat = NULL;
}

/* As the compositor stops; the outputs' grids go with them. */
void heatmap_finish(struct sh_server *server) {
    free(server->heatmap);
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
