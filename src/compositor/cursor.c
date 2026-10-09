/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* The pointer: what lies under it, focus on hover, the window controls, tab strips and resize
 * bands it acts on, button bindings, scrolling, and the cursor image. */
#include "server.h"

static void process_pointer_target(struct sh_server *server, uint32_t time);

/* Whether `node` is a window's rounded frame and the layout position is inside its hole. */
static bool in_frame_hole(struct wlr_scene_node *node, double lx, double ly) {
    if (!node || node->type != WLR_SCENE_NODE_RECT)
        return false;
    struct wlr_scene_tree *tree = node->parent;
    while (tree && !tree->node.data)
        tree = tree->node.parent;
    struct sh_node *owner = tree ? tree->node.data : NULL;
    if (!owner || owner->kind != SH_NODE_TOPLEVEL)
        return false;
    struct sh_toplevel *toplevel = owner->owner;
    int hole = toplevel->frame_hole;
    if (hole <= 0 || !toplevel->border[0] || node != &toplevel->border[0]->node)
        return false;
    struct wlr_scene_rect *rect = toplevel->border[0];
    int x, y;
    wlr_scene_node_coords(node, &x, &y);
    return lx >= x + hole && lx < x + rect->width - hole && ly >= y + hole &&
           ly < y + rect->height - hole;
}

/* The topmost node at a layout position, where windows will be once their animations end:
 * input never waits for an animation, nor lands in the middle of one. */
static struct wlr_scene_node *scene_node_at(struct sh_server *server, double lx, double ly,
                                            double *sx, double *sy) {
    // A motion event asks the same question several times (window, controls, tabs, resize
    // band) with nothing moved in between; it answers once.
    if (server->hit.caching && server->hit.valid && server->hit.x == lx && server->hit.y == ly) {
        *sx = server->hit.sx;
        *sy = server->hit.sy;
        return server->hit.node;
    }
    bool animating = sh_animator_running(server->animator) > 0;
    if (animating)
        sh_animator_rest_at(server->animator, lx, ly);
    // A window on the screen only for a peek at it is not there to point at.
    struct wlr_scene_node *lent[8];
    int lent_count = 0;
    if (server->peek_lent > 0) {
        struct sh_toplevel *toplevel;
        wl_list_for_each(toplevel, &server->toplevels, link) {
            if (lent_count < 8 && shown_for_peek(toplevel) && toplevel->scene_tree &&
                toplevel->scene_tree->node.enabled) {
                toplevel->scene_tree->node.enabled = false; // as for the frame's hole below
                lent[lent_count++] = &toplevel->scene_tree->node;
            }
        }
    }
    struct wlr_scene_node *node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
    // A rounded border is one hollow rect over the whole window, which hit testing takes as
    // solid: pointing through its hole reaches what is below, the window itself first.
    struct wlr_scene_node *hidden[8];
    int hidden_count = 0;
    while (hidden_count < 8 && in_frame_hole(node, lx, ly)) {
        node->enabled = false; // only for the next lookup: no damage, nothing redrawn
        hidden[hidden_count++] = node;
        node = wlr_scene_node_at(&server->scene->tree.node, lx, ly, sx, sy);
    }
    while (hidden_count > 0)
        hidden[--hidden_count]->enabled = true;
    while (lent_count > 0)
        lent[--lent_count]->enabled = true;
    if (animating)
        sh_animator_resume(server->animator);
    if (server->hit.caching) {
        server->hit.valid = true;
        server->hit.x = lx;
        server->hit.y = ly;
        server->hit.sx = *sx;
        server->hit.sy = *sy;
        server->hit.node = node;
    }
    return node;
}

static struct sh_node *desktop_node_at(struct sh_server *server, double lx, double ly,
                                       struct wlr_surface **surface, double *sx, double *sy) {
    struct wlr_scene_node *node = scene_node_at(server, lx, ly, sx, sy);
    if (node == NULL || node->type != WLR_SCENE_NODE_BUFFER) {
        return NULL;
    }
    struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
    struct wlr_scene_surface *scene_surface = wlr_scene_surface_try_from_buffer(scene_buffer);
    if (!scene_surface) {
        return NULL;
    }

    *surface = scene_surface->surface;

    struct wlr_scene_tree *tree = node->parent;
    while (tree != NULL && tree->node.data == NULL) {
        tree = tree->node.parent;
    }
    return tree ? tree->node.data : NULL;
}

static struct sh_toplevel *desktop_toplevel_at(struct sh_server *server, double x, double y,
                                               struct wlr_surface **surface, double *sx,
                                               double *sy) {
    struct sh_node *node = desktop_node_at(server, x, y, surface, sx, sy);
    return node && node->kind == SH_NODE_TOPLEVEL ? node->owner : NULL;
}

/* The window drawn at a point of the layout, or NULL. */
struct sh_toplevel *toplevel_at(struct sh_server *server, double x, double y) {
    struct wlr_surface *surface;
    double sx, sy;
    return desktop_toplevel_at(server, x, y, &surface, &sx, &sy);
}

/* Whether a panel (a layer surface above the windows) is under the point. It is no bare
 * desktop: pointing at it or clicking it leaves the focused window focused, so a taskbar on
 * another monitor still sees that window as the active one and a click on it minimizes it. */
static bool panel_at(struct sh_server *server, double x, double y) {
    struct wlr_surface *surface;
    double sx, sy;
    struct sh_node *node = desktop_node_at(server, x, y, &surface, &sx, &sy);
    if (!node || node->kind != SH_NODE_LAYER)
        return false;
    struct sh_layer *layer = node->owner;
    return layer->surface->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP;
}

/* Clients report exact corners only in a few pixels; a single-edge grab near the end of that
 * edge is almost always meant as a corner resize. */
uint32_t corner_edges(struct sh_toplevel *toplevel, uint32_t edges) {
    struct wlr_cursor *cursor = toplevel->server->cursor;
    struct wlr_box geo_box = toplevel_geometry(toplevel);
    double left = toplevel->scene_tree->node.x + geo_box.x;
    double top = toplevel->scene_tree->node.y + geo_box.y;
    double margin_x = geo_box.width / 4.0 < 32 ? geo_box.width / 4.0 : 32;
    double margin_y = geo_box.height / 4.0 < 32 ? geo_box.height / 4.0 : 32;
    if ((edges & (WLR_EDGE_LEFT | WLR_EDGE_RIGHT)) == 0) {
        if (cursor->x < left + margin_x)
            edges |= WLR_EDGE_LEFT;
        else if (cursor->x > left + geo_box.width - margin_x)
            edges |= WLR_EDGE_RIGHT;
    }
    if ((edges & (WLR_EDGE_TOP | WLR_EDGE_BOTTOM)) == 0) {
        if (cursor->y < top + margin_y)
            edges |= WLR_EDGE_TOP;
        else if (cursor->y > top + geo_box.height - margin_y)
            edges |= WLR_EDGE_BOTTOM;
    }
    return edges;
}

/* Show a corner cursor wherever an edge grab would become a corner resize, so the pointer
 * matches what dragging will do. */
static void update_resize_cursor(struct sh_server *server, struct sh_toplevel *toplevel) {
    if (!server->shape_edges || !toplevel)
        return;
    uint32_t edges = corner_edges(toplevel, server->shape_edges);
    if (edges == server->shown_edges)
        return;
    server->shown_edges = edges;
    wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, wlr_xcursor_get_resize_name(edges));
}

static uint32_t shape_edges(enum wp_cursor_shape_device_v1_shape shape) {
    switch (shape) {
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_N_RESIZE:
        return WLR_EDGE_TOP;
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_S_RESIZE:
        return WLR_EDGE_BOTTOM;
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_W_RESIZE:
        return WLR_EDGE_LEFT;
    case WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_E_RESIZE:
        return WLR_EDGE_RIGHT;
    default:
        return 0;
    }
}

void cursor_request_set_shape(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, request_set_shape);
    struct wlr_cursor_shape_manager_v1_request_set_shape_event *event = data;
    if (event->device_type != WLR_CURSOR_SHAPE_MANAGER_V1_DEVICE_TYPE_POINTER ||
        server->seat->pointer_state.focused_client != event->seat_client)
        return;
    server->shape_edges = shape_edges(event->shape);
    server->shown_edges = 0;
    wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr,
                           wlr_cursor_shape_v1_name(event->shape));
    if (server->cursor_mode == SH_CURSOR_PASSTHROUGH) {
        struct wlr_surface *surface;
        double sx, sy;
        update_resize_cursor(server, desktop_toplevel_at(server, server->cursor->x,
                                                         server->cursor->y, &surface, &sx, &sy));
    }
}

/* The window whose controls are at (x, y), and which part of it. */
static struct sh_toplevel *deco_at(struct sh_server *server, double x, double y,
                                   enum sh_deco_part *part) {
    double sx, sy;
    struct wlr_scene_node *node = scene_node_at(server, x, y, &sx, &sy);
    if (!node || node->type != WLR_SCENE_NODE_BUFFER || !node->parent)
        return NULL;
    struct sh_node *owner = node->parent->node.data;
    if (!owner || owner->kind != SH_NODE_TOPLEVEL)
        return NULL;
    struct sh_toplevel *toplevel = owner->owner;
    if (!toplevel->deco || &toplevel->deco->node != node)
        return NULL;
    *part = sh_decoration_part_at(deco_style(server), sx, sy);
    return *part == SH_DECO_NONE ? NULL : toplevel;
}

/* `toplevel`, if the pointer is on its surface within the strip along its top edge
 * (windows.drag_strip), which moves a window without a title bar as if it had one. */
static struct sh_toplevel *drag_strip_at(struct sh_toplevel *toplevel, struct wlr_surface *surface,
                                         double y) {
    if (!toplevel || !toplevel->deco || toplevel->fullscreen || !surface ||
        wlr_surface_get_root_surface(surface) != toplevel_surface(toplevel))
        return NULL;
    double top = toplevel->scene_tree->node.y + toplevel_geometry(toplevel).y;
    return y >= top && y < top + server_settings(toplevel->server)->drag_strip ? toplevel : NULL;
}

/* Server-decorated windows draw no frame of their own to grab, so a band just outside their
 * edges (and border) resizes them, as a client's frame would. */
enum { SH_RESIZE_BAND = 8 };

/* The window whose resize band is at (x, y), and which of its edges that band is along. The
 * band is only reachable where nothing is drawn above the desktop: not over another window, nor
 * over a panel. */
static struct sh_toplevel *resize_band_at(struct sh_server *server, double x, double y,
                                          uint32_t *edges) {
    double sx, sy;
    struct wlr_surface *surface = NULL;
    struct sh_node *node = desktop_node_at(server, x, y, &surface, &sx, &sy);
    if (node && node->kind == SH_NODE_TOPLEVEL)
        return NULL;
    if (node && node->kind == SH_NODE_LAYER) {
        struct sh_layer *layer = node->owner;
        if (layer->surface->current.layer >= ZWLR_LAYER_SHELL_V1_LAYER_TOP)
            return NULL;
    }
    int reach = server_settings(server)->border_width + SH_RESIZE_BAND;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (!toplevel_mapped(toplevel) || !toplevel_visible(toplevel) || toplevel->fullscreen ||
            !wants_decoration(toplevel))
            continue;
        struct wlr_box g = toplevel_geometry(toplevel);
        double left = toplevel->scene_tree->node.x + g.x;
        double top = toplevel->scene_tree->node.y + g.y;
        double right = left + g.width, bottom = top + g.height;
        if (x < left - reach || x >= right + reach || y < top - reach || y >= bottom + reach)
            continue;
        uint32_t found = (x < left ? WLR_EDGE_LEFT : x >= right ? WLR_EDGE_RIGHT : 0) |
                         (y < top ? WLR_EDGE_TOP : y >= bottom ? WLR_EDGE_BOTTOM : 0);
        if (!found)
            continue;
        *edges = corner_edges(toplevel, found);
        return toplevel;
    }
    return NULL;
}

/* The window whose tab strip is at (x, y), and which tab, counting from 0. */
static struct sh_toplevel *tabs_at(struct sh_server *server, double x, double y, int *index) {
    double sx, sy;
    struct wlr_scene_node *node = scene_node_at(server, x, y, &sx, &sy);
    if (!node || node->type != WLR_SCENE_NODE_BUFFER || !node->parent)
        return NULL;
    struct sh_node *owner = node->parent->node.data;
    if (!owner || owner->kind != SH_NODE_TOPLEVEL)
        return NULL;
    struct sh_toplevel *toplevel = owner->owner;
    if (!toplevel->tabs || &toplevel->tabs->node != node)
        return NULL;
    *index = sh_tabs_index_at(toplevel->tabs_width, toplevel->tabs_count, sx);
    return *index < 0 ? NULL : toplevel;
}

/* The client surface a press at (x, y) reaches, at (*sx, *sy) on it, and the window or panel it
 * belongs to in *owner (NULL for a popup or an unmanaged X11 window); NULL where the compositor
 * takes the press itself (window controls and their corner, tab strips, a window's drag strip,
 * resize bands) or there is only the desktop. touch.c gives such a surface a finger and the rest
 * to the pointer. */
struct wlr_surface *press_target_at(struct sh_server *server, double x, double y, double *sx,
                                    double *sy, struct sh_node **owner) {
    enum sh_deco_part part;
    int tab;
    if (deco_at(server, x, y, &part) || tabs_at(server, x, y, &tab))
        return NULL;
    struct wlr_surface *surface = NULL;
    struct sh_node *node = desktop_node_at(server, x, y, &surface, sx, sy);
    struct sh_toplevel *toplevel = node && node->kind == SH_NODE_TOPLEVEL ? node->owner : NULL;
    if (!surface || drag_strip_at(toplevel, surface, y) ||
        (toplevel && toplevel->deco && in_deco_corner(toplevel, x, y)))
        return NULL;
    *owner = node;
    return surface;
}

static void set_tabs_hovered(struct sh_server *server, struct sh_toplevel *toplevel, int index) {
    struct sh_toplevel *old = server->tabs_hovered;
    if (old == toplevel && server->tabs_hovered_index == index)
        return;
    server->tabs_hovered = toplevel;
    server->tabs_hovered_index = index;
    if (old && old != toplevel)
        refresh_tabs(old);
    if (toplevel)
        refresh_tabs(toplevel);
}

/* The member of the group that is tab `index`. */
static struct sh_toplevel *group_tab(struct sh_toplevel *from, int index) {
    struct sh_toplevel *member;
    wl_list_for_each(member, &from->server->toplevels, link) {
        if (member->group == from->group && group_index(member) == index)
            return member;
    }
    return NULL;
}

static void set_deco_hovered(struct sh_server *server, struct sh_toplevel *toplevel,
                             enum sh_deco_part part) {
    struct sh_toplevel *old = server->deco_hovered;
    if (old == toplevel && server->deco_hovered_part == part)
        return;
    server->deco_hovered = toplevel;
    server->deco_hovered_part = part;
    if (old && old != toplevel)
        refresh_decoration(old);
    if (toplevel)
        refresh_decoration(toplevel);
}

static void deco_activate(struct sh_toplevel *toplevel, enum sh_deco_part part) {
    switch (part) {
    case SH_DECO_CLOSE:
        toplevel_close(toplevel);
        break;
    case SH_DECO_MINIMIZE:
        minimize_toplevel(toplevel);
        break;
    case SH_DECO_FULLSCREEN:
        set_fullscreen(toplevel, !toplevel->fullscreen);
        break;
    default:
        break;
    }
}

void process_cursor_motion(struct sh_server *server, uint32_t time) {
    hot_corner_check(server);
    zoom_moved(server);
    if (server->seat->drag)
        wlr_scene_node_set_position(&server->drag_icons->node, server->cursor->x,
                                    server->cursor->y);
    overview_hot_corner(server);
    if (overview_motion(server))
        return;
    if (server->cursor_mode == SH_CURSOR_MOVE) {
        process_cursor_move(server);
        return;
    }
    if (server->cursor_mode == SH_CURSOR_RESIZE) {
        process_cursor_resize(server);
        return;
    }

    server->hit.caching = true;
    server->hit.valid = false;
    process_pointer_target(server, time);
    server->hit.caching = false;
}

/* What the pointer is over after moving: window, controls, tabs, resize band, focus. */
static void process_pointer_target(struct sh_server *server, uint32_t time) {
    double sx, sy;
    struct wlr_seat *seat = server->seat;
    struct sh_toplevel *revealed = server->deco_revealed;
    if (revealed && !in_deco_corner(revealed, server->cursor->x, server->cursor->y)) {
        server->deco_revealed = NULL;
        refresh_decoration(revealed);
        server->hit.valid = false; // the controls came out of the scene
    }
    struct wlr_surface *surface = NULL;
    struct sh_toplevel *toplevel =
        desktop_toplevel_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
    if (toplevel && toplevel->deco && !server->deco_revealed &&
        in_deco_corner(toplevel, server->cursor->x, server->cursor->y)) {
        server->deco_revealed = toplevel;
        refresh_decoration(toplevel);
        server->hit.valid = false; // the controls went into the scene
    }
    enum sh_deco_part part;
    struct sh_toplevel *decorated = deco_at(server, server->cursor->x, server->cursor->y, &part);
    set_deco_hovered(server, decorated, decorated ? part : SH_DECO_NONE);
    if (decorated) {
        set_default_cursor(server);
        wlr_seat_pointer_clear_focus(seat);
        return;
    }
    int tab;
    struct sh_toplevel *tabbed = tabs_at(server, server->cursor->x, server->cursor->y, &tab);
    set_tabs_hovered(server, tabbed, tabbed ? tab : -1);
    if (tabbed) {
        set_default_cursor(server);
        wlr_seat_pointer_clear_focus(seat);
        return;
    }
    uint32_t band_edges;
    if (!surface && resize_band_at(server, server->cursor->x, server->cursor->y, &band_edges)) {
        if (server->shape_edges || server->shown_edges != band_edges) {
            server->shape_edges = 0;
            server->shown_edges = band_edges;
            wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr,
                                   wlr_xcursor_get_resize_name(band_edges));
        }
        wlr_seat_pointer_clear_focus(seat);
        return;
    }
    if (toplevel && hover_focuses(server, toplevel))
        focus_toplevel_raise(toplevel, false);
    else if (!toplevel && server_settings(server)->focus_follows_mouse &&
             seat->pointer_state.button_count == 0 && !wlr_seat_pointer_has_grab(seat) &&
             !wlr_seat_keyboard_has_grab(seat) &&
             !panel_at(server, server->cursor->x, server->cursor->y))
        focus_desktop(server, wlr_output_layout_output_at(server->output_layout,
                                                          server->cursor->x, server->cursor->y));
    // A drag's text or file goes to the window there: the strip is for a press that moves it.
    if (!seat->drag && drag_strip_at(toplevel, surface, server->cursor->y)) {
        set_default_cursor(server);
        wlr_seat_pointer_clear_focus(seat);
        return;
    }
    if (surface) {
        wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(seat, time, sx, sy);
        update_resize_cursor(server, toplevel);
    } else {
        set_default_cursor(server);
        wlr_seat_pointer_clear_focus(seat);
    }
}

void server_cursor_motion(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, cursor_motion);
    struct wlr_pointer_motion_event *event = data;
    input_activity(server, true);
    wlr_relative_pointer_manager_v1_send_relative_motion(
        server->relative_pointer, server->seat, (uint64_t)event->time_msec * 1000, event->delta_x,
        event->delta_y, event->unaccel_dx, event->unaccel_dy);
    double dx = event->delta_x, dy = event->delta_y;
    struct wlr_pointer_constraint_v1 *constraint = server->active_constraint;
    if (constraint && server->cursor_mode == SH_CURSOR_PASSTHROUGH &&
        server->seat->pointer_state.focused_surface == constraint->surface) {
        if (constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED)
            return; // The client only wants the relative motion sent above.
        double sx = server->seat->pointer_state.sx, sy = server->seat->pointer_state.sy;
        double confined_x, confined_y;
        if (wlr_region_confine(&constraint->region, sx, sy, sx + dx, sy + dy, &confined_x,
                               &confined_y)) {
            dx = confined_x - sx;
            dy = confined_y - sy;
        }
    }
    uint64_t started = now_ns();
    wlr_cursor_move(server->cursor, &event->pointer->base, dx, dy);
    process_cursor_motion(server, event->time_msec);
    ++server->stats.motions;
    server->stats.motion_ns += now_ns() - started;
}

void server_cursor_motion_absolute(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;
    input_activity(server, true);
    uint64_t started = now_ns();
    wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
    process_cursor_motion(server, event->time_msec);
    ++server->stats.motions;
    server->stats.motion_ns += now_ns() - started;
}

/* Actions that work on the focused window; over the bare desktop, a button binding skips them
 * rather than act on a window the pointer is not on. */
static bool action_targets_window(enum sh_action action) {
    switch (action) {
    case SH_CLOSE:
    case SH_FULLSCREEN:
    case SH_TOGGLE_FLOATING:
    case SH_TOGGLE_STICKY:
    case SH_TOGGLE_ABOVE:
    case SH_SWALLOW_TOGGLE:
    case SH_GROUP_TOGGLE:
    case SH_GROUP_NEXT:
    case SH_GROUP_PREV:
    case SH_UNGROUP:
    case SH_MOVE_TO_WORKSPACE:
    case SH_SNAP_LEFT:
    case SH_SNAP_RIGHT:
    case SH_SNAP_TOP_LEFT:
    case SH_SNAP_TOP_RIGHT:
    case SH_SNAP_BOTTOM_LEFT:
    case SH_SNAP_BOTTOM_RIGHT:
    case SH_SNAP_CYCLE_LEFT:
    case SH_SNAP_CYCLE_RIGHT:
    case SH_SNAP_CYCLE_UP:
    case SH_SNAP_CYCLE_DOWN:
    case SH_MAXIMIZE:
    case SH_RESTORE:
    case SH_MOVE_LEFT:
    case SH_MOVE_RIGHT:
    case SH_MOVE_UP:
    case SH_MOVE_DOWN:
    case SH_MOVE_TO_SCRATCHPAD:
    case SH_PROMOTE:
    case SH_SWAP_NEXT:
    case SH_SWAP_PREV:
    case SH_RESIZE_LEFT:
    case SH_RESIZE_RIGHT:
    case SH_RESIZE_UP:
    case SH_RESIZE_DOWN:
        return true;
    default:
        return false;
    }
}

/* Runs the binding for a pressed button, if one matches what lies under the pointer, and
 * swallows both the press and its release. Returns false when the click belongs to a client. */
static bool handle_button_binding(struct sh_server *server,
                                  const struct wlr_pointer_button_event *event) {
    if (event->button < BTN_MOUSE || event->button >= BTN_MOUSE + 32)
        return false;
    uint32_t bit = 1u << (event->button - BTN_MOUSE);
    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        if (!(server->bound_buttons & bit))
            return false;
        server->bound_buttons &= ~bit;
        return true;
    }
    if (server->locked || server->grab_button || server->deco_pressed)
        return false;
    double sx, sy;
    struct wlr_surface *surface = NULL;
    struct sh_node *node =
        desktop_node_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
    struct sh_toplevel *toplevel = NULL;
    enum sh_pointer_target target = SH_POINTER_DESKTOP;
    if (node && node->kind == SH_NODE_TOPLEVEL) {
        toplevel = node->owner;
        target = SH_POINTER_WINDOW;
    } else if (node && node->kind == SH_NODE_LAYER) {
        struct sh_layer *layer = node->owner;
        if (layer->surface->current.layer != ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND)
            target = SH_POINTER_OTHER;
    } else if (surface) {
        target = SH_POINTER_OTHER; // a popup, or an unmanaged X11 window
    }
    const char *app_id = toplevel ? toplevel_app_id(toplevel) : NULL;
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
    uint32_t mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
    int argument = 0;
    enum sh_action action = server->callbacks->button(
        server->callbacks->userdata, mods, event->button, target, app_id ? app_id : "", &argument);
    if (action == SH_NONE)
        return false;
    server->bound_buttons |= bit;
    if (action == SH_SPAWN || action == SH_TERMINAL) {
        // It starts a program, whose window opens where the click was.
        server->spawn_x = server->cursor->x;
        server->spawn_y = server->cursor->y;
        server->spawn_until = now_ms() + 5000;
    }
    if (toplevel)
        focus_toplevel(toplevel);
    if (toplevel || !action_targets_window(action))
        run_action(server, action, argument);
    return true;
}

void server_cursor_button(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, cursor_button);
    struct wlr_pointer_button_event *event = data;
    input_activity(server, event->state == WL_POINTER_BUTTON_STATE_PRESSED);
    if ((server->overview.open || server->overview.pressed) && overview_button(server, event))
        return;
    if (server->deco_pressed && event->button == BTN_LEFT &&
        event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        // A dot acts on release, and only if the pointer is still on it.
        struct sh_toplevel *pressed = server->deco_pressed;
        server->deco_pressed = NULL;
        refresh_decoration(pressed); // no longer held
        enum sh_deco_part part;
        if (deco_at(server, server->cursor->x, server->cursor->y, &part) == pressed &&
            part == server->deco_pressed_part)
            deco_activate(pressed, part);
        process_cursor_motion(server, event->time_msec);
        return;
    }
    if (server->grab_button == event->button && event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        server->grab_button = 0;
        finish_grab(server);
        reset_cursor_mode(server);
        process_cursor_motion(server, event->time_msec);
        return;
    }
    if (handle_button_binding(server, event))
        return;
    if (!server->locked && !server->deco_pressed) {
        int tab;
        struct sh_toplevel *tabbed = tabs_at(server, server->cursor->x, server->cursor->y, &tab);
        if (tabbed) {
            // A press on a tab brings that window forward; the release is ignored.
            if (event->state == WL_POINTER_BUTTON_STATE_PRESSED && event->button == BTN_LEFT) {
                struct sh_toplevel *member = group_tab(tabbed, tab);
                focus_toplevel(member ? member : tabbed);
            }
            return;
        }
    }
    enum sh_deco_part part;
    struct sh_toplevel *decorated =
        event->state == WL_POINTER_BUTTON_STATE_PRESSED && !server->locked && !server->deco_pressed
            ? deco_at(server, server->cursor->x, server->cursor->y, &part)
            : NULL;
    if (!decorated && event->state == WL_POINTER_BUTTON_STATE_PRESSED && !server->locked &&
        !server->deco_pressed && event->button == BTN_LEFT) {
        double sx, sy;
        struct wlr_surface *surface = NULL;
        struct sh_toplevel *toplevel =
            desktop_toplevel_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
        toplevel = drag_strip_at(toplevel, surface, server->cursor->y);
        if (toplevel) {
            focus_toplevel(toplevel);
            server->grab_button = BTN_LEFT;
            begin_interactive(toplevel, SH_CURSOR_MOVE, 0);
            return;
        }
        uint32_t edges;
        toplevel = surface ? NULL
                           : resize_band_at(server, server->cursor->x, server->cursor->y, &edges);
        if (toplevel) {
            focus_toplevel(toplevel);
            server->grab_button = BTN_LEFT;
            begin_interactive(toplevel, SH_CURSOR_RESIZE, edges);
            return;
        }
    }
    if (decorated) {
        focus_toplevel(decorated);
        if (event->button != BTN_LEFT)
            return;
        server->deco_pressed = decorated;
        server->deco_pressed_part = part;
        refresh_decoration(decorated);
        return;
    }
    if (event->state == WL_POINTER_BUTTON_STATE_PRESSED) {
        struct wlr_output *clicked = wlr_output_layout_output_at(
            server->output_layout, server->cursor->x, server->cursor->y);
        if (clicked)
            set_active_output(server, clicked->name);
        double sx, sy;
        struct wlr_surface *surface = NULL;
        struct sh_node *node =
            desktop_node_at(server, server->cursor->x, server->cursor->y, &surface, &sx, &sy);
        struct sh_toplevel *toplevel = node && node->kind == SH_NODE_TOPLEVEL ? node->owner : NULL;
        if (server->locked) {
            node = NULL; // Only lock surfaces, which have no desktop node, are reachable.
            toplevel = NULL;
        }
        if (!toplevel && !server->locked && !panel_at(server, server->cursor->x, server->cursor->y))
            focus_desktop(server, clicked);
        if (toplevel)
            focus_toplevel(toplevel);
        else if (node && node->kind == SH_NODE_LAYER)
            focus_layer(node->owner);
        struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat);
        uint32_t mods = keyboard ? wlr_keyboard_get_modifiers(keyboard) : 0;
        const struct sh_settings *settings = server_settings(server);
        if (toplevel && (mods & settings->mouse_modifier) &&
            (event->button == BTN_LEFT || event->button == BTN_RIGHT)) {
            server->grab_button = event->button;
            uint32_t edges = WLR_EDGE_BOTTOM | WLR_EDGE_RIGHT;
            if (toplevel->tiled) {
                // A tile's outer edges cannot move, so resize from the corner nearest the pointer.
                struct wlr_box geometry = toplevel_geometry(toplevel);
                double center_x = toplevel->scene_tree->node.x + geometry.x + geometry.width / 2.0;
                double center_y = toplevel->scene_tree->node.y + geometry.y + geometry.height / 2.0;
                edges = (server->cursor->x < center_x ? WLR_EDGE_LEFT : WLR_EDGE_RIGHT) |
                        (server->cursor->y < center_y ? WLR_EDGE_TOP : WLR_EDGE_BOTTOM);
            }
            begin_interactive(toplevel,
                              event->button == BTN_LEFT ? SH_CURSOR_MOVE : SH_CURSOR_RESIZE, edges);
            return;
        }
    }
    wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        finish_grab(server);
        reset_cursor_mode(server);
    }
}

void server_cursor_axis(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, cursor_axis);
    struct wlr_pointer_axis_event *event = data;
    input_activity(server, true);
    if (overview_axis(server, event))
        return;
    struct wlr_keyboard *held = wlr_seat_get_keyboard(server->seat);
    unsigned zoom_mask = server_settings(server)->effects.zoom_scroll_modifier;
    if (zoom_mask && held && event->orientation == WL_POINTER_AXIS_VERTICAL_SCROLL &&
        (wlr_keyboard_get_modifiers(held) & zoom_mask) == zoom_mask) {
        // A wheel notch is about 15 units; touchpads send small amounts that add up.
        server->zoom_scroll += event->delta;
        while (server->zoom_scroll <= -10) {
            server->zoom_scroll += 10;
            zoom_by(server, 1);
        }
        while (server->zoom_scroll >= 10) {
            server->zoom_scroll -= 10;
            zoom_by(server, -1);
        }
        return;
    }
    wlr_seat_pointer_notify_axis(server->seat, event->time_msec, event->orientation, event->delta,
                                 event->delta_discrete, event->source, event->relative_direction);
}

void server_cursor_frame(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, cursor_frame);

    wlr_seat_pointer_notify_frame(server->seat);
}

void seat_request_cursor(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, request_cursor);

    struct wlr_seat_pointer_request_set_cursor_event *event = data;
    struct wlr_seat_client *focused_client = server->seat->pointer_state.focused_client;

    if (focused_client == event->seat_client) {
        server->shape_edges = 0;
        server->shown_edges = 0;
        wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
    }
}

void set_default_cursor(struct sh_server *server) {
    server->shape_edges = 0;
    server->shown_edges = 0;
    wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
}

void seat_pointer_focus_change(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, pointer_focus_change);
    struct wlr_seat_pointer_focus_change_event *event = data;
    if (!event->new_surface)
        set_default_cursor(server);
}
