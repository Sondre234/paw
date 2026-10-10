/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* Keyboard focus: which window or panel has it, how it moves (back, by direction, to the
 * desktop of an output), and urgent windows that ask for it. */
#include "server.h"

void deactivate_toplevel(struct sh_server *server) {
    if (!server->focused_toplevel)
        return;
    struct sh_toplevel *old = server->focused_toplevel;
    // A fullscreen window stays in front of its output, over the windows kept above there too,
    // while another output has focus; raising a window over it lowers it.
    toplevel_set_activated(old, false);
    if (old->foreign)
        wlr_foreign_toplevel_handle_v1_set_activated(old->foreign, false);
    server->focused_toplevel = NULL;
    refresh_frame(old);
}

/* Puts the fullscreen windows on `toplevel`'s output down among the others, so that it can come
 * to the front over them: those covering the panels, and the others while still in front. */
static void lower_fullscreen_covers(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    struct wlr_output *output = toplevel_output(toplevel);
    struct sh_toplevel *other;
    wl_list_for_each(other, &server->toplevels, link) {
        if (other != toplevel && other->fullscreen && other->scene_tree &&
            (other->fullscreen_cover || other->scene_tree->node.parent == server->fullscreen) &&
            toplevel_output(other) == output)
            wlr_scene_node_reparent(&other->scene_tree->node, window_layer(other));
    }
}

/* Gives the surface keyboard focus even while the seat has no keyboard (headless, or before a
 * virtual keyboard connects), so the first keyboard to appear types into it. */
void keyboard_enter(struct wlr_seat *seat, struct wlr_surface *surface) {
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
    if (keyboard)
        wlr_seat_keyboard_notify_enter(seat, surface, keyboard->keycodes, keyboard->num_keycodes,
                                       &keyboard->modifiers);
    else
        wlr_seat_keyboard_notify_enter(seat, surface, NULL, 0, NULL);
}

/* Gives the window keyboard focus; `raise` also brings it to the front. */
void focus_toplevel_raise(struct sh_toplevel *toplevel, bool raise) {
    if (!toplevel || toplevel->server->locked)
        return;
    struct sh_server *server = toplevel->server;
    struct wlr_seat *seat = server->seat;
    if (toplevel->swallowed && toplevel->swallow_peer)
        swallow_release(toplevel->swallow_peer); // asked for by hand: it comes back
    if (toplevel->group_hidden)
        group_show(toplevel); // another tab of its group: it takes the group's slot
    // A hidden scratchpad window activated from the taskbar comes to the focused output, as
    // scratchpad_show brings it.
    if (toplevel->scratchpad && toplevel->minimized)
        center_scratchpad(toplevel, focused_output(server));
    if (toplevel->output[0] && !toplevel->sticky &&
        toplevel->workspace != *output_workspace(server, toplevel->output)) {
        if (server->grabbed_toplevel)
            reset_cursor_mode(server);
        show_workspace(server, toplevel->output, toplevel->workspace);
    }
    deactivate_toplevel(server);
    server->focused_layer = NULL;
    server->focused_toplevel = toplevel;
    bool was_urgent = toplevel->urgent;
    toplevel->urgent = false; // it has the user's attention now
    bool was_minimized = toplevel->minimized;
    toplevel->minimized = false;
    if (was_minimized && wants_tiling(toplevel, NULL))
        tile_toplevel(toplevel, NULL, NULL, false);
    wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
    // Panels stay reachable once a fullscreen window loses focus.
    if (raise && !toplevel->fullscreen)
        lower_fullscreen_covers(toplevel);
    if (raise || toplevel->fullscreen) {
        wlr_scene_node_reparent(&toplevel->scene_tree->node,
                                toplevel->fullscreen ? fullscreen_tree(toplevel) : window_layer(toplevel));
        wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
    }
    wl_list_remove(&toplevel->link);
    wl_list_insert(&server->toplevels, &toplevel->link);
    toplevel_set_activated(toplevel, true);
    refresh_frame(toplevel);
    if (toplevel->foreign) {
        wlr_foreign_toplevel_handle_v1_set_minimized(toplevel->foreign, false);
        wlr_foreign_toplevel_handle_v1_set_activated(toplevel->foreign, true);
    }
    if (toplevel_accepts_keyboard(toplevel))
        keyboard_enter(seat, toplevel_surface(toplevel));
    set_active_output(server, toplevel->output);
    // The scrolling view follows focus.
    if (toplevel->tiled && sh_tiling_set_focus(server->tiling, toplevel)) {
        struct wlr_output *output = tiled_output(toplevel);
        if (output)
            reflow_output(server, output);
    }
    if (was_urgent) {
        wlr_log(WLR_INFO, "Urgent window focused");
        notify_subscribers(server);
    }
    // Peeked at, as a taskbar's picture of it is clicked, it is shown for good now, where the
    // peek showed it: the others fade back around it.
    if (server->peek_window == toplevel)
        end_window_peek(server, true);
}

void focus_toplevel(struct sh_toplevel *toplevel) { focus_toplevel_raise(toplevel, true); }

/* Urgent windows. An unfocused window that asks for attention (xdg-activation, an X11 urgency
 * hint) is marked urgent under windows.activation = "urgent": its border pulses for a few
 * seconds and stays in the urgent color, the taskbar and workspace indicator show it, and
 * focus_urgent goes to the one that asked first. Focusing it, or unmapping it, ends it. */
enum { URGENT_PULSE_MS = 4000, URGENT_PULSE_PERIOD_MS = 1200, URGENT_TICK_MS = 40 };

/* How bright an urgent window's border is now: it starts at full strength and pulses down and
 * up for URGENT_PULSE_MS, then holds. Without animations it holds at once. */
float urgent_pulse(struct sh_toplevel *toplevel, int64_t now) {
    if (!toplevel->urgent || !server_settings(toplevel->server)->animations)
        return 1;
    int64_t elapsed = now - toplevel->urgent_since;
    if (elapsed < 0 || elapsed >= URGENT_PULSE_MS)
        return 1;
    return 0.65F + 0.35F * cosf((float)(2 * M_PI * (double)elapsed / URGENT_PULSE_PERIOD_MS));
}

static bool urgent_pulsing(struct sh_toplevel *toplevel, int64_t now) {
    return toplevel->urgent && now - toplevel->urgent_since < URGENT_PULSE_MS + URGENT_TICK_MS;
}

/* Redraws the borders of the windows that pulse while any does; the tick after the last one
 * finishes draws it at rest. */
int urgent_tick(void *data) {
    struct sh_server *server = data;
    int64_t now = now_ms();
    bool more = false;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (!urgent_pulsing(toplevel, now))
            continue;
        refresh_frame(toplevel);
        more = true;
    }
    if (more && server->urgent_timer)
        wl_event_source_timer_update(server->urgent_timer, URGENT_TICK_MS);
    return 0;
}

static bool toplevel_can_be_urgent(struct sh_toplevel *toplevel) {
    return !toplevel->unmanaged && toplevel_mapped(toplevel);
}

/* Marks or unmarks `toplevel` as urgent. The focused window never is: it has the attention. */
void set_urgent(struct sh_toplevel *toplevel, bool urgent) {
    struct sh_server *server = toplevel->server;
    if (toplevel->urgent == urgent)
        return;
    if (urgent && (!toplevel_can_be_urgent(toplevel) || server->focused_toplevel == toplevel))
        return;
    toplevel->urgent = urgent;
    if (urgent) {
        toplevel->urgent_order = ++server->urgent_serial;
        toplevel->urgent_since = now_ms();
        wlr_log(WLR_INFO, "Window %s is urgent", toplevel_app_id(toplevel) ? toplevel_app_id(toplevel) : "");
        if (server->urgent_timer)
            wl_event_source_timer_update(server->urgent_timer, URGENT_TICK_MS);
    }
    refresh_frame(toplevel);
    notify_subscribers(server);
}

/* A client asks to be focused (xdg-activation, _NET_ACTIVE_WINDOW): what it gets follows
 * windows.activation. */
void activation_requested(struct sh_toplevel *toplevel) {
    if (!toplevel_can_be_urgent(toplevel))
        return;
    switch (server_settings(toplevel->server)->activation) {
    case SH_ACTIVATION_FOCUS:
        focus_toplevel(toplevel);
        break;
    case SH_ACTIVATION_URGENT:
        set_urgent(toplevel, true);
        break;
    default:
        break;
    }
}

/* The window that has been urgent the longest, or NULL. */
static struct sh_toplevel *oldest_urgent(struct sh_server *server) {
    struct sh_toplevel *toplevel, *oldest = NULL;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel->urgent && toplevel_can_be_urgent(toplevel) &&
            (!oldest || toplevel->urgent_order < oldest->urgent_order))
            oldest = toplevel;
    }
    return oldest;
}

void focus_urgent(struct sh_server *server) {
    if (server->locked)
        return;
    struct sh_toplevel *toplevel = oldest_urgent(server);
    if (!toplevel)
        return;
    focus_toplevel(toplevel);
    pointer_follow(toplevel);
}

/* Whether hovering `toplevel` may focus it: not during a drag, a popup or menu grab, or while
 * a panel or launcher holds the keyboard. */
bool hover_focuses(struct sh_server *server, struct sh_toplevel *toplevel) {
    struct wlr_seat *seat = server->seat;
    if (!server_settings(server)->focus_follows_mouse || server->locked ||
        toplevel == server->focused_toplevel || server->focused_layer ||
        !toplevel_visible(toplevel) || seat->pointer_state.button_count > 0 ||
        wlr_seat_pointer_has_grab(seat) || wlr_seat_keyboard_has_grab(seat))
        return false;
#if WLR_HAS_XWAYLAND
    // X11 menus are override-redirect windows that grab inside the X server, unseen here.
    if (toplevel->unmanaged || !wl_list_empty(&server->unmanaged->children))
        return false;
#endif
    return true;
}

void focus_previous(struct sh_server *server) {
    if (server->locked)
        return;
    server->focused_layer = NULL;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel_visible(toplevel)) {
            focus_toplevel(toplevel);
            return;
        }
    }
    deactivate_toplevel(server);
    wlr_seat_keyboard_clear_focus(server->seat);
}

/* Focuses the window focused before the current one, wherever it is (its output switches to
 * its workspace). Windows hidden in the scratchpad or minimized are not in the history. */
void focus_last(struct sh_server *server) {
    if (server->locked)
        return;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel->unmanaged || toplevel == server->focused_toplevel || toplevel->minimized ||
            toplevel->swallowed || !toplevel_mapped(toplevel))
            continue;
        focus_toplevel(toplevel);
        pointer_follow(toplevel);
        return;
    }
}

/* Focuses the topmost visible window on `output`, else nothing. */
void focus_top_on(struct sh_server *server, struct wlr_output *output) {
    server->focused_layer = NULL;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel_visible(toplevel) && find_output(server, toplevel->output) == output) {
            focus_toplevel(toplevel);
            return;
        }
    }
    deactivate_toplevel(server);
    wlr_seat_keyboard_clear_focus(server->seat);
    notify_subscribers(server);
}

/* The bare desktop of an output counts as a window there: pointing at it (with focus following
 * the mouse) or clicking it on another output than the focused window's makes that output the
 * focused one and takes the keyboard from the window, as sway focuses an empty workspace. */
void focus_desktop(struct sh_server *server, struct wlr_output *output) {
    if (!output || server->locked || server->focused_layer)
        return;
    struct sh_toplevel *focused = server->focused_toplevel;
    if (focused ? find_output(server, focused->output) == output
                : !strcmp(server->active_output, output->name))
        return;
    set_active_output(server, output->name);
    deactivate_toplevel(server);
    wlr_seat_keyboard_clear_focus(server->seat);
    notify_subscribers(server);
}

void focus_layer(struct sh_layer *layer) {
    if (layer->server->locked || layer->surface->current.keyboard_interactive ==
                                     ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE)
        return;
    struct sh_server *server = layer->server;
    deactivate_toplevel(server);
    server->focused_layer = layer;
    keyboard_enter(server->seat, layer->surface->surface);
}

/* The window keyboard actions apply to: the focused one, else the topmost visible. */
struct sh_toplevel *current_toplevel(struct sh_server *server) {
    if (server->focused_toplevel)
        return server->focused_toplevel;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel_visible(toplevel))
            return toplevel;
    }
    return NULL;
}

/* The nearest visible window from `from` in a direction (`sign` -1 is left or up): first those
 * level with it (overlapping across the direction), then by distance between centres.
 * `tiles_only` looks only at tiles sharing its tiling. */
struct sh_toplevel *toplevel_toward(struct sh_toplevel *from_toplevel, bool horizontal,
                                    int sign, bool tiles_only) {
    struct sh_server *server = from_toplevel->server;
    struct wlr_output *tiling = tiles_only ? tiled_output(from_toplevel) : NULL;
    struct wlr_box from = toplevel_box(from_toplevel);
    double from_x = from.x + from.width / 2.0, from_y = from.y + from.height / 2.0;
    struct sh_toplevel *best = NULL, *toplevel;
    bool best_level = false;
    double best_distance = 0;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel == from_toplevel || !toplevel_visible(toplevel))
            continue;
        if (tiles_only && (!toplevel->tiled || toplevel->fullscreen ||
                           tiled_output(toplevel) != tiling))
            continue;
        struct wlr_box box = toplevel_box(toplevel);
        double x = box.x + box.width / 2.0, y = box.y + box.height / 2.0;
        double along = horizontal ? x - from_x : y - from_y;
        if (along * sign <= 0)
            continue;
        bool level = horizontal ? box.y < from.y + from.height && from.y < box.y + box.height
                                : box.x < from.x + from.width && from.x < box.x + box.width;
        double distance = hypot(x - from_x, y - from_y);
        if (!best || (level && !best_level) || (level == best_level && distance < best_distance)) {
            best = toplevel;
            best_level = level;
            best_distance = distance;
        }
    }
    return best;
}

/* Focus follows the mouse on its next move, so keyboard actions take the pointer along to the
 * window they focus or move: just inside its bottom-right corner, out of the way of what is
 * being read or typed. Windows still opening or gliding are not yet drawn where they are, so
 * they land first; otherwise the pointer could hover, and focus, whichever window is passing
 * its destination. */
void pointer_follow(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    if (!server_settings(server)->focus_follows_mouse)
        return;
    struct sh_toplevel *other;
    wl_list_for_each(other, &server->toplevels, link) sh_anim_finish(&other->anim);
    // The corner of the smaller of its drawn and requested sizes, so the pointer is inside the
    // window both before and after a pending resize.
    struct wlr_box box = toplevel_box(toplevel), drawn = toplevel_geometry(toplevel);
    int width = drawn.width > 0 && drawn.width < box.width ? drawn.width : box.width;
    int height = drawn.height > 0 && drawn.height < box.height ? drawn.height : box.height;
    const int inset = 8; // clear of client-side resize edges
    double x = box.x + (width > 2 * inset ? width - inset : width / 2.0);
    double y = box.y + (height > 2 * inset ? height - inset : height / 2.0);
    wlr_cursor_warp(server->cursor, NULL, x, y);
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    process_cursor_motion(server, now.tv_sec * 1000 + now.tv_nsec / 1000000);
}

/* Focus moves to the nearest window that way, from the focused window or else the topmost on
 * the focused output. With none there, it moves to the next output that way, as sway does, so
 * an empty output can be reached from the keyboard: its topmost window, else its bare desktop
 * with the pointer at its centre, where new windows open. */
void focus_direction(struct sh_server *server, enum sh_action action) {
    if (server->locked)
        return;
    struct wlr_output *output = focused_output(server);
    struct sh_toplevel *current = server->focused_toplevel, *toplevel;
    if (!current) {
        wl_list_for_each(toplevel, &server->toplevels, link) {
            if (toplevel_visible(toplevel) && find_output(server, toplevel->output) == output) {
                current = toplevel;
                break;
            }
        }
    }
    bool horizontal = action == SH_FOCUS_LEFT || action == SH_FOCUS_RIGHT;
    int sign = action == SH_FOCUS_LEFT || action == SH_FOCUS_UP ? -1 : 1;
    struct sh_toplevel *best;
    if (current && current->tiled && toplevel_layout(current) == SH_LAYOUT_SCROLL)
        // Columns off the output count too: step through the strip.
        best = sh_tiling_scroll_step(server->tiling, current, horizontal ? sign : 0,
                                     horizontal ? 0 : sign);
    else
        best = current ? toplevel_toward(current, horizontal, sign, false) : NULL;
    if (current && current->tiled && toplevel_layout(current) == SH_LAYOUT_MONOCLE) {
        // Every tile covers the same area, so the arrows step through them.
        best = sh_tiling_neighbour(server->tiling, current, sign);
        if (best) {
            focus_toplevel(best);
            pointer_follow(best);
        }
        return;
    }
    if (best) {
        focus_toplevel(best);
        pointer_follow(best);
        return;
    }
    if (!output)
        return;
    static const enum wlr_direction directions[] = {WLR_DIRECTION_LEFT, WLR_DIRECTION_RIGHT,
                                                    WLR_DIRECTION_UP, WLR_DIRECTION_DOWN};
    struct wlr_box from;
    if (current)
        from = toplevel_box(current);
    else
        wlr_output_layout_get_box(server->output_layout, output, &from);
    struct wlr_output *next = wlr_output_layout_adjacent_output(
        server->output_layout, directions[action - SH_FOCUS_LEFT], output,
        from.x + from.width / 2.0, from.y + from.height / 2.0);
    if (!next)
        return;
    set_active_output(server, next->name);
    focus_top_on(server, next);
    if (server->focused_toplevel) {
        pointer_follow(server->focused_toplevel);
        return;
    }
    struct wlr_box box;
    wlr_output_layout_get_box(server->output_layout, next, &box);
    wlr_cursor_warp(server->cursor, NULL, box.x + box.width / 2.0, box.y + box.height / 2.0);
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    process_cursor_motion(server, now.tv_sec * 1000 + now.tv_nsec / 1000000);
}

static struct sh_toplevel *toplevel_for_surface(struct sh_server *server,
                                                struct wlr_surface *surface) {
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel_surface(toplevel) == surface)
            return toplevel;
    }
    return NULL;
}

/* xdg-activation: an application asks to be raised, e.g. a browser opening a link from chat.
 * wlroots expires and validates tokens. Tokens made without an input serial are honoured too:
 * a browser handed a link by another process often has nothing better. What the window then
 * gets depends on windows.activation. */
void request_activate(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, request_activate);
    struct wlr_xdg_activation_v1_request_activate_event *event = data;
    struct sh_toplevel *toplevel = toplevel_for_surface(server, event->surface);
    if (toplevel)
        activation_requested(toplevel);
}
