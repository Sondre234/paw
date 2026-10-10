/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* The overview (Expose). Opening it lays every window of the focused output's workspace out as
 * a live thumbnail in a grid, with a strip of the output's workspaces above. It is drawn by the
 * compositor from scaled copies of the windows' scene nodes (overview_scene.h), so the
 * thumbnails move with the windows' contents at no cost while nothing changes. The shell
 * draws the text (titles, the filter) over it from the events sent by overview_announce. The
 * overview takes the keyboard and the pointer while open and changes nothing until a window
 * is picked, a workspace chosen, or a thumbnail dropped on the strip. Snap Assist is the same
 * overview in the free slot beside a window just snapped (overview_assist): it lists the
 * output's other windows there, puts the one picked into the slot, and leaves the pointer to
 * the rest of the screen. */
#include "server.h"

static void overview_colour(float out[4], float r, float g, float b, float a) {
    out[0] = r * a; // scene rectangles take premultiplied colours
    out[1] = g * a;
    out[2] = b * a;
    out[3] = a;
}

static void overview_thumb_clear(struct sh_thumb *thumb) {
    if (thumb->tree)
        wlr_scene_node_destroy(&thumb->tree->node);
    memset(thumb, 0, sizeof(*thumb));
}

/* Places `toplevel`'s thumbnail with its top-left corner at (x, y), copying the window again
 * only when it changed or the scale did. */
static void overview_thumb_place(struct sh_thumb *thumb, struct wlr_scene_tree *parent,
                                 struct sh_toplevel *toplevel, int x, int y, double scale) {
    if (thumb->tree && thumb->toplevel != toplevel) {
        sh_thumb_clear(thumb->tree);
        thumb->fingerprint = 0;
        thumb->scale = 0;
    }
    thumb->toplevel = toplevel;
    if (!thumb->tree) {
        thumb->tree = wlr_scene_tree_create(parent);
        if (!thumb->tree)
            return;
    }
    // The cards behind the thumbnails stand in for the windows' shadows.
    const struct wlr_scene_node *shadow = toplevel->shadow ? &toplevel->shadow->node : NULL;
    uint64_t print = sh_thumb_fingerprint(toplevel->scene_tree, shadow);
    if (print != thumb->fingerprint || fabs(scale - thumb->scale) > 1e-4) {
        sh_thumb_clear(thumb->tree);
        sh_thumb_clone(thumb->tree, toplevel->scene_tree, scale, 1.0f, shadow);
        thumb->fingerprint = print;
        thumb->scale = scale;
    }
    wlr_scene_node_set_position(&thumb->tree->node, x, y);
    wlr_scene_node_set_enabled(&thumb->tree->node, true);
}

static void overview_rect_set(struct wlr_scene_rect **rect, struct wlr_scene_tree *parent,
                              struct sh_rect box, const float colour[4]) {
    if (!*rect)
        *rect = wlr_scene_rect_create(parent, box.width, box.height, colour);
    if (!*rect)
        return;
    wlr_scene_rect_set_size(*rect, box.width < 1 ? 1 : box.width, box.height < 1 ? 1 : box.height);
    wlr_scene_rect_set_color(*rect, colour);
    wlr_scene_node_set_position(&(*rect)->node, box.x, box.y);
    wlr_scene_node_set_enabled(&(*rect)->node, true);
}

/* Four bars of `thickness` around `box`, outside it. */
static void overview_frame_set(struct wlr_scene_rect *bars[4], struct wlr_scene_tree *parent,
                               struct sh_rect box, int thickness, const float colour[4]) {
    struct sh_rect sides[4] = {
        {box.x - thickness, box.y - thickness, box.width + 2 * thickness, thickness},
        {box.x - thickness, box.y + box.height, box.width + 2 * thickness, thickness},
        {box.x - thickness, box.y, thickness, box.height},
        {box.x + box.width, box.y, thickness, box.height},
    };
    for (int i = 0; i < 4; ++i)
        overview_rect_set(&bars[i], parent, sides[i], colour);
}

static void overview_frame_hide(struct wlr_scene_rect *bars[4]) {
    for (int i = 0; i < 4; ++i) {
        if (bars[i])
            wlr_scene_node_set_enabled(&bars[i]->node, false);
    }
}

static bool overview_listable(struct sh_toplevel *toplevel) {
#if WLR_HAS_XWAYLAND
    if (toplevel->unmanaged)
        return false;
#endif
    return toplevel_mapped(toplevel) && !toplevel->swallowed;
}

/* Whether a window is on `workspace` of the overview's output, for the grid and the strip. */
static bool overview_on(struct sh_overview *overview, struct sh_toplevel *toplevel,
                        int workspace) {
    return overview_listable(toplevel) && !toplevel->minimized &&
           !strcmp(toplevel->output, overview->output) &&
           (toplevel->sticky || toplevel->workspace == workspace);
}

/* Whether Snap Assist offers a window: one of the output's on the workspace it shows, minimized
 * ones included, but for the window just snapped, any other snapped into a half or a quarter,
 * a fullscreen one and one hidden in the scratchpad or a group. */
static bool assist_lists(struct sh_overview *overview, struct sh_toplevel *toplevel) {
    return overview_listable(toplevel) && toplevel != overview->assist_from &&
           !toplevel->fullscreen && !toplevel->scratchpad && !toplevel->group_hidden &&
           !strcmp(toplevel->output, overview->output) &&
           (toplevel->sticky || toplevel->workspace == overview->viewed) &&
           !(toplevel->arranged && sh_snap_quarters(toplevel->arrangement));
}

/* Lists the windows the grid shows, keeping the selection on its window when it is still
 * there. Without a filter that is the viewed workspace's windows, most recently used first; a
 * filter lists every window that matches, minimized ones and other workspaces' included. */
static void overview_collect(struct sh_server *server) {
    struct sh_overview *overview = &server->overview;
    struct sh_toplevel *kept = overview->selected >= 0 && overview->selected < overview->count
                                   ? overview->windows[overview->selected]
                                   : NULL;
    struct sh_toplevel *old[OVERVIEW_MAX];
    struct sh_thumb old_thumbs[OVERVIEW_MAX];
    int old_count = overview->count;
    memcpy(old, overview->windows, sizeof(old));
    memcpy(old_thumbs, overview->thumbs, sizeof(old_thumbs));
    memset(overview->thumbs, 0, sizeof(overview->thumbs));
    overview->count = 0;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (overview->count >= OVERVIEW_MAX || !overview_listable(toplevel))
            continue;
        bool listed;
        if (overview->assist) {
            listed = assist_lists(overview, toplevel);
        } else if (overview->filter[0]) {
            const char *app_id = toplevel_app_id(toplevel), *title = toplevel_title(toplevel);
            char text[512];
            snprintf(text, sizeof(text), "%s %s", app_id ? app_id : "", title ? title : "");
            listed = sh_overview_matches(text, overview->filter);
        } else {
            listed = overview_on(overview, toplevel, overview->viewed);
        }
        if (listed)
            overview->windows[overview->count++] = toplevel;
    }
    // A window that stays keeps its thumbnail, so it is not copied again.
    for (int i = 0; i < overview->count; ++i) {
        for (int j = 0; j < old_count; ++j) {
            if (old[j] == overview->windows[i] && old_thumbs[j].tree) {
                overview->thumbs[i] = old_thumbs[j];
                memset(&old_thumbs[j], 0, sizeof(old_thumbs[j]));
                break;
            }
        }
    }
    for (int j = 0; j < old_count; ++j)
        overview_thumb_clear(&old_thumbs[j]);
    overview->selected = -1;
    for (int i = 0; i < overview->count; ++i) {
        if (overview->windows[i] == kept)
            overview->selected = i;
    }
    if (overview->selected < 0 && overview->count)
        overview->selected = 0;
}

/* Where each window's thumbnail rests, and the workspace strip. Only the geometry: nothing is
 * drawn here. */
static void overview_layout(struct sh_server *server) {
    struct sh_overview *overview = &server->overview;
    const struct sh_settings *settings = server_settings(server);
    int gap = settings->overview_gap;
    struct sh_rect area = {overview->area.x, overview->area.y, overview->area.width,
                           overview->area.height};
    // Snap Assist has no strip, nor room for a search above its slot.
    int top = area.y + (overview->assist ? gap : OVERVIEW_TOP);
    overview->strip_count = 0;
    if (settings->overview_strip && overview->workspaces > 1) {
        struct sh_rect band = {area.x + gap, top, area.width - 2 * gap,
                               area.height / 7 > 40 ? area.height / 7 : 40};
        double aspect = (double)overview->screen.width / (overview->screen.height ? overview->screen.height : 1);
        int strip_gap = gap / 2 < 8 ? 8 : gap / 2;
        if (sh_overview_strip(overview->workspaces, band, strip_gap, aspect, band.height,
                              overview->strip_cells)) {
            overview->strip_count = overview->workspaces;
            top = overview->strip_cells[0].y + overview->strip_cells[0].height + gap;
        }
    }
    struct sh_rect grid = {area.x + gap, top, area.width - 2 * gap, area.y + area.height - gap - top};
    for (int i = 0; i < overview->count; ++i) {
        struct wlr_box box = toplevel_box(overview->windows[i]);
        overview->sizes[i] = (struct sh_rect){0, 0, box.width > 0 ? box.width : 1,
                                              box.height > 0 ? box.height : 1};
        // Snap Assist's thumbnails show in their cells, as the windows stay where they are.
        bool shown = toplevel_visible(overview->windows[i]) && !overview->assist &&
                     !strcmp(overview->windows[i]->output, overview->output);
        overview->placed[i] = shown;
        overview->origins[i] = (struct sh_rect){box.x, box.y, overview->sizes[i].width,
                                                overview->sizes[i].height};
    }
    if (!overview->count || !sh_overview_grid(overview->sizes, overview->count, grid, gap, 1.0,
                                              overview->cells)) {
        for (int i = 0; i < overview->count; ++i)
            overview->cells[i] = (struct sh_rect){grid.x, grid.y, 1, 1};
    }
    for (int i = 0; i < overview->count; ++i) {
        if (!overview->placed[i])
            overview->origins[i] = overview->cells[i];
    }
}

static struct sh_rect overview_rect_between(struct sh_rect from, struct sh_rect to, double t) {
    return (struct sh_rect){(int)lround(from.x + (to.x - from.x) * t),
                            (int)lround(from.y + (to.y - from.y) * t),
                            (int)lround(from.width + (to.width - from.width) * t),
                            (int)lround(from.height + (to.height - from.height) * t)};
}

/* Draws the overview at the current progress: the backdrop, the strip with a copy of each
 * workspace's windows, the cards and thumbnails, and the selection. */
static void overview_render(struct sh_server *server) {
    struct sh_overview *overview = &server->overview;
    const struct sh_settings *settings = server_settings(server);
    if (!overview->tree)
        return;
    double t = overview->progress;
    bool settled = t >= 1;
    float colour[4];
    overview_colour(colour, 0.04f, 0.05f, 0.08f, (float)(settings->overview_dim * t));
    // Snap Assist darkens its slot alone, rounded as a window there would be.
    struct sh_rect backdrop = overview->assist ? overview->area
                                               : (struct sh_rect){overview->screen.x,
                                                                  overview->screen.y,
                                                                  overview->screen.width,
                                                                  overview->screen.height};
    overview_rect_set(&overview->backdrop, overview->tree, backdrop, colour);
#ifdef PAW_ROUNDED_CORNERS
    if (overview->backdrop)
        wlr_scene_rect_set_rounding(overview->backdrop,
                                    overview->assist ? overview->assist_radius : 0, 0);
#endif
    // The strip fades in with the backdrop by staying hidden until the grid has settled.
    for (int w = 0; w < OVERVIEW_WORKSPACES; ++w) {
        bool listed = settled && w < overview->strip_count;
        if (overview->strip_back[w])
            wlr_scene_node_set_enabled(&overview->strip_back[w]->node, listed);
        if (!listed) {
            overview_frame_hide(overview->strip_mark[w]);
            continue;
        }
        struct sh_rect cell = overview->strip_cells[w];
        bool viewed = w == overview->viewed, target = w == overview->drop;
        overview_colour(colour, viewed ? 0.22f : 0.12f, viewed ? 0.25f : 0.13f,
                        viewed ? 0.31f : 0.16f, 0.95f);
        overview_rect_set(&overview->strip_back[w], overview->strip, cell, colour);
        if (target || viewed || w == overview->current) {
            if (target)
                overview_colour(colour, 0.35f, 0.85f, 0.5f, 1);
            else if (viewed)
                overview_colour(colour, 0.36f, 0.6f, 1.0f, 1);
            else
                overview_colour(colour, 0.8f, 0.8f, 0.85f, 0.5f);
            overview_frame_set(overview->strip_mark[w], overview->strip, cell, target ? 3 : 2,
                               colour);
        } else {
            overview_frame_hide(overview->strip_mark[w]);
        }
    }
    // Copies of each workspace's windows in its strip cell, at the output's proportions.
    int minis = 0;
    if (settled) {
        for (int w = 0; w < overview->strip_count; ++w) {
            struct sh_rect cell = overview->strip_cells[w];
            double scale = (double)cell.width / (overview->screen.width ? overview->screen.width : 1);
            struct sh_toplevel *toplevel;
            wl_list_for_each_reverse(toplevel, &server->toplevels, link) {
                if (minis >= OVERVIEW_MINI_MAX)
                    break;
                if (!overview_on(overview, toplevel, w) || toplevel == overview->dragged)
                    continue;
                struct wlr_box box = toplevel_box(toplevel);
                overview_thumb_place(&overview->minis[minis++], overview->strip, toplevel,
                                     cell.x + (int)lround((box.x - overview->screen.x) * scale),
                                     cell.y + (int)lround((box.y - overview->screen.y) * scale),
                                     scale);
            }
        }
    }
    for (int i = minis; i < OVERVIEW_MINI_MAX; ++i)
        overview_thumb_clear(&overview->minis[i]);
    overview->mini_count = minis;

    for (int i = 0; i < OVERVIEW_MAX; ++i) {
        if (i >= overview->count || !overview->windows[i]) {
            if (overview->cards[i])
                wlr_scene_node_set_enabled(&overview->cards[i]->node, false);
            continue;
        }
        struct sh_rect rect = overview_rect_between(overview->origins[i], overview->cells[i], t);
        double scale = (double)rect.width / overview->sizes[i].width;
        bool dragged = overview->dragging && overview->press == i;
        if (dragged) {
            rect.x += (int)lround(server->cursor->x - overview->press_x);
            rect.y += (int)lround(server->cursor->y - overview->press_y);
        }
        struct sh_rect card = {rect.x - OVERVIEW_PAD, rect.y - OVERVIEW_PAD,
                               rect.width + 2 * OVERVIEW_PAD, rect.height + 2 * OVERVIEW_PAD};
        overview_colour(colour, 0.1f, 0.11f, 0.14f, 0.85f);
        if (settled && !dragged)
            overview_rect_set(&overview->cards[i], overview->cards_tree, card, colour);
        else if (overview->cards[i])
            wlr_scene_node_set_enabled(&overview->cards[i]->node, false);
        overview_thumb_place(&overview->thumbs[i], overview->grid, overview->windows[i], rect.x,
                             rect.y, scale);
        if (dragged && overview->thumbs[i].tree)
            wlr_scene_node_raise_to_top(&overview->thumbs[i].tree->node);
    }
    if (settled && overview->selected >= 0 && overview->selected < overview->count &&
        !overview->dragging) {
        struct sh_rect cell = overview->cells[overview->selected];
        overview_colour(colour, 0.36f, 0.6f, 1.0f, 1);
        overview_frame_set(overview->frame, overview->frames,
                           (struct sh_rect){cell.x - OVERVIEW_PAD, cell.y - OVERVIEW_PAD,
                                            cell.width + 2 * OVERVIEW_PAD,
                                            cell.height + 2 * OVERVIEW_PAD},
                           3, colour);
    } else {
        overview_frame_hide(overview->frame);
    }
}

/* Tells the shell what to draw its text over: the output, the filter, the selection, then a
 * line per thumbnail and per strip cell, in coordinates of the output.
 *   overview OUTPUT COUNT SELECTED VIEWED STRIP AREA_X AREA_Y AREA_WIDTH AREA_HEIGHT FILTER
 *     (AREA is what the panels leave, FILTER is "-" when empty)
 *   overview-window X Y WIDTH HEIGHT APP_ID\tTITLE\tWORKSPACE\tURGENT (URGENT is 0 or 1)
 *   overview-strip X Y WIDTH HEIGHT WORKSPACE\tWINDOWS
 * and "overview-select N" as the selection moves, "overview-close" when it closes. Snap Assist
 * begins with "overview-assist OUTPUT COUNT SELECTED X Y WIDTH HEIGHT" instead, its slot, and has
 * no strip. */
size_t overview_describe(struct sh_server *server, char *text, size_t size) {
    struct sh_overview *overview = &server->overview;
    size_t length = 0;
    if (overview->assist)
        length += snprintf(text + length, size - length, "overview-assist %s %d %d %d %d %d %d\n",
                           overview->output, overview->count, overview->selected,
                           overview->area.x - overview->screen.x,
                           overview->area.y - overview->screen.y, overview->area.width,
                           overview->area.height);
    else
        length += snprintf(text + length, size - length,
                           "overview %s %d %d %d %d %d %d %d %d %s\n", overview->output,
                           overview->count, overview->selected, overview->viewed + 1,
                           overview->strip_count, overview->area.x - overview->screen.x,
                           overview->area.y - overview->screen.y, overview->area.width,
                           overview->area.height, overview->filter[0] ? overview->filter : "-");
    for (int i = 0; i < overview->count && length < size; ++i) {
        struct sh_toplevel *toplevel = overview->windows[i];
        if (!toplevel)
            continue;
        // Neither can end the line or the column: a client sets both as it likes.
        char clean_title[160], clean_app_id[128];
        copy_field(clean_title, sizeof(clean_title), toplevel_title(toplevel));
        copy_field(clean_app_id, sizeof(clean_app_id), toplevel_app_id(toplevel));
        struct sh_rect cell = overview->cells[i];
        length += snprintf(text + length, size - length, "overview-window %d %d %d %d %s\t%s\t%d\t%d\n",
                           cell.x - overview->screen.x, cell.y - overview->screen.y, cell.width,
                           cell.height, clean_app_id, clean_title, toplevel->workspace + 1,
                           toplevel->urgent);
    }
    for (int w = 0; w < overview->strip_count && length < size; ++w) {
        struct sh_rect cell = overview->strip_cells[w];
        int windows = 0;
        struct sh_toplevel *toplevel;
        wl_list_for_each(toplevel, &server->toplevels, link) windows += overview_on(overview, toplevel, w);
        length += snprintf(text + length, size - length, "overview-strip %d %d %d %d %d\t%d\n",
                           cell.x - overview->screen.x, cell.y - overview->screen.y, cell.width,
                           cell.height, w + 1, windows);
    }
    return length < size ? length : size - 1;
}

static void overview_announce(struct sh_server *server) {
    struct sh_overview *overview = &server->overview;
    if (wl_list_empty(&server->subscribers))
        return;
    size_t size = 512 + (size_t)(overview->count + OVERVIEW_WORKSPACES) * 512;
    char *text = malloc(size);
    if (!text)
        return;
    size_t length = overview_describe(server, text, size);
    send_event(server, text, length);
    free(text);
}

void overview_select(struct sh_server *server, int index) {
    struct sh_overview *overview = &server->overview;
    if (!overview->count || index < 0 || index >= overview->count || index == overview->selected)
        return;
    overview->selected = index;
    char line[32];
    int length = snprintf(line, sizeof(line), "overview-select %d\n", index);
    send_event(server, line, (size_t)length);
    overview_render(server);
}

/* Lays out again if the windows or workspaces changed, then draws; synchronous, so what a
 * control request changed is in place when it is answered. */
static void overview_refresh(struct sh_server *server) {
    struct sh_overview *overview = &server->overview;
    if (!overview->visible)
        return;
    if (overview->open && overview->dirty) {
        overview->dirty = false;
        overview_collect(server);
        if (overview->assist && !overview->count) {
            overview_close(server, NULL, -1); // nothing left to offer
            return;
        }
        overview_layout(server);
        overview_announce(server);
    }
    overview_render(server);
}

static void overview_hide(struct sh_server *server) {
    struct sh_overview *overview = &server->overview;
    overview->visible = false;
    overview->open = false;
    overview->closing = false;
    overview->dragging = false;
    overview->press = -1;
    overview->dragged = NULL;
    overview->assist = false;
    overview->assist_from = NULL;
    for (int i = 0; i < OVERVIEW_MAX; ++i)
        overview_thumb_clear(&overview->thumbs[i]);
    for (int i = 0; i < OVERVIEW_MINI_MAX; ++i)
        overview_thumb_clear(&overview->minis[i]);
    overview->count = overview->mini_count = 0;
    if (overview->tree)
        wlr_scene_node_set_enabled(&overview->tree->node, false);
}

static int overview_step(void *data) {
    struct sh_server *server = data;
    struct sh_overview *overview = &server->overview;
    overview->armed = false;
    if (!overview->visible)
        return 0;
    if (overview->progress != overview->to) {
        int64_t elapsed = now_ms() - overview->started;
        if (overview->span <= 0 || elapsed >= overview->span) {
            overview->progress = overview->to;
        } else {
            struct sh_curve ease = {SH_CURVE_EASE_OUT, {0, 0, 0, 0}};
            overview->progress = overview->from + (overview->to - overview->from) *
                                                      sh_curve_eval(&ease, (double)elapsed /
                                                                               overview->span);
        }
    }
    overview_refresh(server);
    if (overview->progress != overview->to) {
        overview->armed = true;
        wl_event_source_timer_update(overview->timer, 16);
    } else if (overview->closing) {
        overview_hide(server);
    }
    return 0;
}

/* Asks for a redraw soon; many changes in a row make one. */
void overview_touch(struct sh_server *server, bool relayout) {
    struct sh_overview *overview = &server->overview;
    if (!overview->visible)
        return;
    if (relayout)
        overview->dirty = true;
    if (!overview->armed && overview->timer) {
        overview->armed = true;
        wl_event_source_timer_update(overview->timer, 8);
    }
}

/* A window that goes away leaves the overview at once; the next layout drops its place. */
void overview_forget(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    struct sh_overview *overview = &server->overview;
    if (!overview->visible)
        return;
    for (int i = 0; i < OVERVIEW_MAX; ++i) {
        if (overview->thumbs[i].toplevel == toplevel)
            overview_thumb_clear(&overview->thumbs[i]);
        if (i < overview->count && overview->windows[i] == toplevel)
            overview->windows[i] = NULL;
    }
    for (int i = 0; i < OVERVIEW_MINI_MAX; ++i) {
        if (overview->minis[i].toplevel == toplevel)
            overview_thumb_clear(&overview->minis[i]);
    }
    if (overview->dragged == toplevel) {
        overview->dragged = NULL;
        overview->dragging = false;
        overview->press = -1;
    }
    if (overview->assist && overview->assist_from == toplevel) {
        overview->assist_from = NULL;
        overview_close(server, NULL, -1); // the window it was beside went
        return;
    }
    overview_touch(server, true);
}

/* Fullscreen windows sit above the other windows; while the overview is open the
 * focused one goes down among the others, as it does when it loses focus. */
static void overview_lower_fullscreen(struct sh_server *server, bool lower) {
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (!toplevel->fullscreen || !toplevel->scene_tree)
            continue;
        wlr_scene_node_reparent(&toplevel->scene_tree->node,
                                lower ? window_layer(toplevel) : fullscreen_tree(toplevel));
    }
}

/* Opens the overview on `output`, or Snap Assist when `assist` is set (and `area` its slot);
 * false when there is nothing for Snap Assist to offer. */
static bool overview_begin(struct sh_server *server, struct wlr_output *output, bool assist,
                           struct sh_rect area) {
    struct sh_overview *overview = &server->overview;
    const struct sh_settings *settings = server_settings(server);
    if (overview->visible) // still gliding closed
        overview_hide(server);
    switcher_close(server, -1);
    overview->assist = assist;
    snprintf(overview->output, sizeof(overview->output), "%s", output->name);
    wlr_output_layout_get_box(server->output_layout, output, &overview->screen);
    struct sh_rect usable = usable_area(server, output);
    overview->area = assist ? area : (struct sh_rect){usable.x, usable.y, usable.width, usable.height};
    overview->workspaces = assist                                     ? 0
                           : settings->workspaces < OVERVIEW_WORKSPACES ? settings->workspaces
                                                                        : OVERVIEW_WORKSPACES;
    overview->current = overview->viewed = *output_workspace(server, output->name);
    overview->filter[0] = '\0';
    overview->count = 0;
    overview->selected = -1;
    overview->press = overview->drop = -1;
    overview->dragging = false;
    overview->dragged = NULL;
    overview->scroll = 0;
    if (!overview->tree) {
        overview->tree = wlr_scene_tree_create(server->overview_layer);
        if (!overview->tree) {
            overview->assist = false;
            return false;
        }
        float clear[4] = {0, 0, 0, 0};
        overview->backdrop = wlr_scene_rect_create(overview->tree, 1, 1, clear);
        overview->strip = wlr_scene_tree_create(overview->tree);
        overview->cards_tree = wlr_scene_tree_create(overview->tree);
        overview->grid = wlr_scene_tree_create(overview->tree);
        overview->frames = wlr_scene_tree_create(overview->tree);
        overview->timer = wl_event_loop_add_timer(wl_display_get_event_loop(server->wl_display),
                                                  overview_step, server);
    }
    overview_collect(server);
    if (assist && !overview->count) {
        overview->assist = false;
        overview->assist_from = NULL;
        return false;
    }
    wlr_scene_node_set_enabled(&overview->tree->node, true);
    if (!assist)
        overview_lower_fullscreen(server, true);
    overview->selected = -1;
    for (int i = 0; i < overview->count; ++i) {
        if (overview->windows[i] == server->focused_toplevel)
            overview->selected = i;
    }
    if (overview->selected < 0 && overview->count)
        overview->selected = 0;
    overview_layout(server);
    overview->open = overview->visible = true;
    overview->closing = false;
    overview->dirty = false;
    overview->from = overview->progress = 0;
    overview->to = 1;
    overview->span = settings->overview_animation && settings->animations
                         ? (int)(settings->overview_duration / (settings->animation_speed > 0 ? settings->animation_speed : 1))
                         : 0;
    overview->started = now_ms();
    if (overview->span <= 0)
        overview->progress = 1;
    // The pointer belongs to the overview, not to the window under it; Snap Assist takes it
    // only over its slot.
    if (!assist) {
        wlr_seat_pointer_clear_focus(server->seat);
        set_default_cursor(server);
    }
    overview_render(server);
    if (overview->progress != overview->to)
        overview_touch(server, false);
    overview_announce(server);
    wlr_log(WLR_INFO, "%s opened on %s", assist ? "Snap Assist" : "Overview", output->name);
    return true;
}

void overview_open(struct sh_server *server) {
    struct sh_overview *overview = &server->overview;
    const struct sh_settings *settings = server_settings(server);
    if (!settings->overview) {
        wlr_log(WLR_INFO, "Overview actions ignored: overview.enabled is false");
        return;
    }
    if (overview->open || server->locked || !server->overview_layer)
        return;
    struct wlr_output *output = focused_output(server);
    if (output)
        overview_begin(server, output, false, (struct sh_rect){0});
}

/* Snap Assist beside `from`, which was just snapped on `output`: the windows snap.c offers in
 * the free `area`, the slot `slot` (an arrangement), its backdrop rounded by `radius`. It
 * replaces one open already; the overview itself open keeps it from opening. */
void overview_assist(struct sh_server *server, struct sh_toplevel *from, struct wlr_output *output,
                     enum sh_action slot, struct sh_rect area, int radius) {
    struct sh_overview *overview = &server->overview;
    if ((overview->open && !overview->assist) || server->locked || !server->overview_layer)
        return;
    if (overview->open)
        overview_close(server, NULL, -1);
    overview->assist_from = from;
    overview->assist_slot = slot;
    overview->assist_radius = radius;
    overview_begin(server, output, true, area);
}

/* Closes the overview, focusing `chosen` if any; else, with `workspace` not below 0, showing
 * that workspace on the overview's output. The thumbnails glide back to the windows. */
void overview_close(struct sh_server *server, struct sh_toplevel *chosen, int workspace) {
    struct sh_overview *overview = &server->overview;
    if (!overview->open)
        return;
    if (overview->assist) {
        // Snap Assist goes at once, and the window picked, if any, takes the slot; snapped
        // there, it may bring Snap Assist back for the next free slot.
        enum sh_action slot = overview->assist_slot;
        struct wlr_output *output = find_output(server, overview->output);
        send_event(server, "overview-close\n", strlen("overview-close\n"));
        overview_hide(server);
        wlr_log(WLR_INFO, "Snap Assist closed");
        if (chosen && output && !server->locked) {
            place_by_hand_on(chosen, slot, output);
            focus_toplevel(chosen);
        }
        return;
    }
    overview->open = false;
    overview->dragging = false;
    overview->press = overview->drop = -1;
    overview->dragged = NULL;
    overview_lower_fullscreen(server, false);
    if (chosen && !server->locked) {
        focus_toplevel(chosen);
        struct wlr_box box = toplevel_box(chosen);
        if (!wlr_box_contains_point(&box, server->cursor->x, server->cursor->y))
            pointer_follow(chosen);
    } else if (workspace >= 0) {
        struct wlr_output *output = find_output(server, overview->output);
        if (output) {
            switch_workspace(server, output, workspace);
            focus_top_on(server, output);
        }
    }
    // Where the windows are now: the thumbnails glide there.
    for (int i = 0; i < overview->count; ++i) {
        if (!overview->windows[i])
            continue;
        bool shown = toplevel_visible(overview->windows[i]) && overview_listable(overview->windows[i]);
        struct wlr_box box = toplevel_box(overview->windows[i]);
        overview->origins[i] = shown ? (struct sh_rect){box.x, box.y, overview->sizes[i].width,
                                                         overview->sizes[i].height}
                                     : overview->cells[i];
    }
    const struct sh_settings *settings = server_settings(server);
    overview->closing = true;
    overview->from = overview->progress;
    overview->to = 0;
    overview->started = now_ms();
    overview->span = settings->overview_animation && settings->animations
                         ? (int)(settings->overview_duration * overview->progress / (settings->animation_speed > 0 ? settings->animation_speed : 1))
                         : 0;
    send_event(server, "overview-close\n", strlen("overview-close\n"));
    if (overview->span <= 0) {
        overview_hide(server);
    } else {
        overview_render(server);
        overview_touch(server, false);
    }
    wlr_log(WLR_INFO, "Overview closed");
}

/* A swipe holds the open overview `progress` of the way (0: the windows where they are, 1: the
 * grid), whatever the time, until it is held again or let go. */
void overview_hold(struct sh_server *server, double progress) {
    struct sh_overview *overview = &server->overview;
    if (!overview->open)
        return;
    progress = progress < 0 ? 0 : progress > 1 ? 1 : progress;
    overview->from = overview->to = overview->progress = progress;
    overview->span = 0;
    overview_refresh(server);
}

/* Lets the held overview go: it glides open the rest of the way, or closes from where it is. */
void overview_release(struct sh_server *server, bool open) {
    struct sh_overview *overview = &server->overview;
    if (!overview->open)
        return;
    if (!open) {
        overview_close(server, NULL, -1);
        return;
    }
    const struct sh_settings *settings = server_settings(server);
    overview->from = overview->progress;
    overview->to = 1;
    overview->started = now_ms();
    overview->span = settings->overview_animation && settings->animations
                         ? (int)(settings->overview_duration * (1 - overview->progress) /
                                 (settings->animation_speed > 0 ? settings->animation_speed : 1))
                         : 0;
    if (overview->span <= 0)
        overview->progress = 1;
    overview_render(server);
    if (overview->progress != overview->to)
        overview_touch(server, false);
}

/* Closes at once, without the glide: the session locks, or the output goes. */
void overview_dismiss(struct sh_server *server) {
    struct sh_overview *overview = &server->overview;
    if (overview->open)
        overview_close(server, NULL, -1);
    if (overview->visible)
        overview_hide(server);
}

void overview_confirm(struct sh_server *server, int index) {
    struct sh_overview *overview = &server->overview;
    if (!overview->open)
        return;
    if (index >= 0 && index < overview->count && overview->windows[index])
        overview_close(server, overview->windows[index], -1);
    else if (!overview->count)
        overview_close(server, NULL, overview->viewed); // an empty workspace: go there
}

/* Shows another workspace of the output in the grid, without switching to it. */
void overview_view(struct sh_server *server, int workspace) {
    struct sh_overview *overview = &server->overview;
    if (!overview->open || overview->assist || workspace < 0 || workspace >= overview->workspaces ||
        (workspace == overview->viewed && !overview->filter[0]))
        return;
    overview->viewed = workspace;
    overview->filter[0] = '\0';
    overview->selected = -1;
    overview->dirty = true;
    overview_refresh(server);
}

void overview_set_filter(struct sh_server *server, const char *text) {
    struct sh_overview *overview = &server->overview;
    if (!overview->open || overview->assist || !strcmp(overview->filter, text))
        return;
    snprintf(overview->filter, sizeof(overview->filter), "%s", text);
    overview->selected = -1; // the first match
    overview->dirty = true;
    overview_refresh(server);
}

static void overview_close_selected(struct sh_server *server) {
    struct sh_overview *overview = &server->overview;
    if (overview->selected >= 0 && overview->selected < overview->count &&
        overview->windows[overview->selected])
        toplevel_close(overview->windows[overview->selected]);
}

/* Index of the thumbnail (its card) under a point, else -1. */
static int overview_thumb_at(struct sh_overview *overview, double x, double y) {
    for (int i = overview->count - 1; i >= 0; --i) {
        struct sh_rect cell = overview->cells[i];
        if (x >= cell.x - OVERVIEW_PAD && x < cell.x + cell.width + OVERVIEW_PAD &&
            y >= cell.y - OVERVIEW_PAD && y < cell.y + cell.height + OVERVIEW_PAD)
            return i;
    }
    return -1;
}

static int overview_strip_at(struct sh_overview *overview, double x, double y) {
    for (int w = 0; w < overview->strip_count; ++w) {
        struct sh_rect cell = overview->strip_cells[w];
        if (x >= cell.x && x < cell.x + cell.width && y >= cell.y && y < cell.y + cell.height)
            return w;
    }
    return -1;
}

/* Keys while the overview is open. Text goes to the filter; the rest is navigation. Every key
 * is kept from the windows. */
void overview_key(struct sh_server *server, uint32_t modifiers, xkb_keysym_t sym) {
    struct sh_overview *overview = &server->overview;
    bool control = modifiers & WLR_MODIFIER_CTRL;
    int selected = overview->selected;
    switch (sym) {
    case XKB_KEY_Escape:
        if (overview->filter[0])
            overview_set_filter(server, "");
        else
            overview_close(server, NULL, -1);
        return;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        overview_confirm(server, selected);
        return;
    case XKB_KEY_Left:
    case XKB_KEY_Right:
        if (control && !overview->assist) {
            overview_view(server, overview->viewed + (sym == XKB_KEY_Left ? -1 : 1));
            return;
        }
        // fall through
    case XKB_KEY_Up:
    case XKB_KEY_Down: {
        if (selected < 0 || !overview->count)
            return;
        enum sh_overview_direction direction = sym == XKB_KEY_Left    ? SH_OVERVIEW_LEFT
                                               : sym == XKB_KEY_Right ? SH_OVERVIEW_RIGHT
                                               : sym == XKB_KEY_Up    ? SH_OVERVIEW_UP
                                                                      : SH_OVERVIEW_DOWN;
        overview_select(server, sh_overview_neighbour(overview->cells, overview->count, selected,
                                                      direction));
        return;
    }
    case XKB_KEY_Tab:
    case XKB_KEY_ISO_Left_Tab: {
        if (!overview->count)
            return;
        bool back = sym == XKB_KEY_ISO_Left_Tab || (modifiers & WLR_MODIFIER_SHIFT);
        int next = (selected + (back ? -1 : 1) + overview->count) % overview->count;
        overview_select(server, next);
        return;
    }
    case XKB_KEY_Home:
        overview_select(server, 0);
        return;
    case XKB_KEY_End:
        overview_select(server, overview->count - 1);
        return;
    case XKB_KEY_Page_Up:
        overview_view(server, overview->viewed - 1);
        return;
    case XKB_KEY_Page_Down:
        overview_view(server, overview->viewed + 1);
        return;
    case XKB_KEY_Delete:
        overview_close_selected(server);
        return;
    case XKB_KEY_BackSpace: {
        size_t length = strlen(overview->filter);
        if (!length)
            return;
        char text[sizeof(overview->filter)];
        memcpy(text, overview->filter, length);
        while (length > 0 && (text[length - 1] & 0xC0) == 0x80)
            --length; // step back over a multibyte character's tail
        if (length > 0)
            --length;
        text[length] = '\0';
        overview_set_filter(server, text);
        return;
    }
    }
    if (modifiers & (WLR_MODIFIER_CTRL | WLR_MODIFIER_ALT | WLR_MODIFIER_LOGO) || overview->assist)
        return; // Snap Assist has no search
    char utf8[8];
    int bytes = xkb_keysym_to_utf8(sym, utf8, sizeof(utf8));
    if (bytes <= 1 || (unsigned char)utf8[0] < 0x20 || utf8[0] == 0x7f)
        return; // bytes counts the terminator: 1 means no character
    char text[sizeof(overview->filter)];
    size_t length = strlen(overview->filter);
    if (length + (size_t)bytes >= sizeof(text))
        return;
    memcpy(text, overview->filter, length);
    memcpy(text + length, utf8, (size_t)bytes);
    overview_set_filter(server, text);
}

bool overview_button(struct sh_server *server, const struct wlr_pointer_button_event *event) {
    struct sh_overview *overview = &server->overview;
    double x = server->cursor->x, y = server->cursor->y;
    uint32_t bit = event->button >= BTN_MOUSE && event->button < BTN_MOUSE + 32
                       ? 1u << (event->button - BTN_MOUSE)
                       : 0;
    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        if (!(overview->pressed & bit))
            return false;
        overview->pressed &= ~bit;
        if (event->button != BTN_LEFT || overview->press < 0)
            return true;
        int press = overview->press;
        bool dragging = overview->dragging;
        int drop = overview->drop;
        struct sh_toplevel *toplevel = press < overview->count ? overview->windows[press] : NULL;
        overview->press = overview->drop = -1;
        overview->dragging = false;
        overview->dragged = NULL;
        if (!overview->open)
            return true;
        if (dragging) {
            if (toplevel && drop >= 0 && drop != toplevel->workspace &&
                !strcmp(toplevel->output, overview->output)) {
                move_toplevel_to_workspace(server, toplevel, drop);
                overview->dirty = true;
            }
            overview_refresh(server);
        } else if (overview_thumb_at(overview, x, y) == press) {
            overview_confirm(server, press);
        }
        return true;
    }
    if (!overview->open)
        return false;
    // A click on a panel, or on another monitor, closes the overview and goes on to what is
    // there: the taskbar's buttons still work.
    struct wlr_box area = {overview->area.x, overview->area.y, overview->area.width,
                           overview->area.height};
    if (!wlr_box_contains_point(&area, x, y)) {
        overview_close(server, NULL, -1);
        process_cursor_motion(server, event->time_msec);
        return false;
    }
    overview->pressed |= bit;
    if (event->button == BTN_LEFT) {
        int thumb = overview_thumb_at(overview, x, y), cell = overview_strip_at(overview, x, y);
        if (thumb >= 0) {
            overview_select(server, thumb);
            overview->press = thumb;
            overview->press_x = x;
            overview->press_y = y;
        } else if (cell >= 0) {
            if (cell == overview->viewed && !overview->filter[0])
                overview_close(server, NULL, cell); // a second click goes there
            else
                overview_view(server, cell);
        } else {
            overview_close(server, NULL, -1);
        }
    } else if (event->button == BTN_MIDDLE) {
        int thumb = overview_thumb_at(overview, x, y);
        if (thumb >= 0 && overview->windows[thumb])
            toplevel_close(overview->windows[thumb]);
    }
    return true;
}

bool overview_motion(struct sh_server *server) {
    struct sh_overview *overview = &server->overview;
    if (!overview->open)
        return false;
    double x = server->cursor->x, y = server->cursor->y;
    // Snap Assist has the pointer over its slot alone, and nowhere to drag a thumbnail to.
    struct wlr_box area = {overview->area.x, overview->area.y, overview->area.width,
                           overview->area.height};
    if (overview->assist && overview->press < 0 && !wlr_box_contains_point(&area, x, y))
        return false;
    if (overview->press >= 0 && !overview->dragging && !overview->assist &&
        fabs(x - overview->press_x) + fabs(y - overview->press_y) > 8 &&
        overview->press < overview->count) {
        overview->dragging = true;
        overview->dragged = overview->windows[overview->press];
    }
    if (overview->dragging) {
        overview->drop = overview_strip_at(overview, x, y);
        overview_render(server);
    } else if (overview->press < 0) {
        int thumb = overview_thumb_at(overview, x, y);
        if (thumb >= 0)
            overview_select(server, thumb);
    }
    set_default_cursor(server);
    wlr_seat_pointer_clear_focus(server->seat);
    return true;
}

/* The hot corner: entering the configured corner of an output opens the overview there. */
void overview_hot_corner(struct sh_server *server) {
    struct sh_overview *overview = &server->overview;
    const struct sh_settings *settings = server_settings(server);
    int corner = settings->overview_hot_corner;
    bool inside = false;
    struct wlr_output *output = NULL;
    if (settings->overview && corner > 0 && !server->locked &&
        server->cursor_mode == SH_CURSOR_PASSTHROUGH) {
        output = wlr_output_layout_output_at(server->output_layout, server->cursor->x,
                                             server->cursor->y);
        if (output) {
            struct wlr_box box;
            wlr_output_layout_get_box(server->output_layout, output, &box);
            double x = corner == 2 || corner == 4 ? box.x + box.width - 1 : box.x;
            double y = corner == 3 || corner == 4 ? box.y + box.height - 1 : box.y;
            inside = fabs(server->cursor->x - x) < 2 && fabs(server->cursor->y - y) < 2;
        }
    }
    if (inside && !overview->in_corner && !overview->open && !server->switcher.open) {
        server->target_output = output;
        overview_open(server);
        server->target_output = NULL;
    }
    overview->in_corner = inside;
}

/* The wheel pages through the workspaces. */
bool overview_axis(struct sh_server *server, const struct wlr_pointer_axis_event *event) {
    struct sh_overview *overview = &server->overview;
    if (!overview->open || overview->assist)
        return false;
    if (event->orientation != WL_POINTER_AXIS_VERTICAL_SCROLL)
        return true;
    overview->scroll += event->delta;
    while (overview->scroll >= 10) {
        overview->scroll -= 10;
        overview_view(server, overview->viewed + 1);
    }
    while (overview->scroll <= -10) {
        overview->scroll += 10;
        overview_view(server, overview->viewed - 1);
    }
    return true;
}
