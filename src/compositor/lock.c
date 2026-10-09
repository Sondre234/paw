/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* ext-session-lock-v1 screen locking, and what keeps the machine awake: idle inhibitors, and a
 * sleep inhibitor while this VT is in front. */
#include "server.h"

/* ext-session-lock-v1: an opaque cover hides the desktop from the moment a lock
 * starts; lock surfaces sit above it, and `locked` is sent once every output has
 * presented a covered frame. A monitor that is off shows nothing to cover, and its first frame
 * once it is on again is drawn with the cover. */
void send_locked_if_presented(struct sh_server *server) {
    struct sh_lock *lock = server->lock;
    if (!lock || lock->locked_sent)
        return;
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        if (!output->lock_presented && !output->powered_off)
            return;
    }
    // A mirror shows the lock a frame after its source (mirror.c).
    wl_list_for_each(output, &server->disabled_outputs, link) {
        if (output->mirror && !output->lock_presented && !output->powered_off)
            return;
    }
    lock->locked_sent = true;
    wlr_session_lock_v1_send_locked(lock->lock);
    power_locked(server);
}

void lock_output_presented(struct sh_output *output) {
    if (!output->server->locked || output->lock_presented)
        return;
    output->lock_presented = true;
    send_locked_if_presented(output->server);
}

static void lock_surface_map(struct wl_listener *listener, void *data) {
    struct sh_lock_surface *lock_surface = wl_container_of(listener, lock_surface, map);
    struct sh_server *server = lock_surface->server;
    if (server->locked && !server->seat->keyboard_state.focused_surface)
        keyboard_enter(server->seat, lock_surface->surface->surface);
    process_cursor_motion(server, 0);
}

static void lock_surface_destroy(struct wl_listener *listener, void *data) {
    struct sh_lock_surface *lock_surface = wl_container_of(listener, lock_surface, destroy);
    struct sh_server *server = lock_surface->server;
    if (server->seat->keyboard_state.focused_surface == lock_surface->surface->surface) {
        wlr_seat_keyboard_clear_focus(server->seat);
        // Hand the keyboard to another lock surface, if one remains.
        struct wlr_session_lock_surface_v1 *other;
        if (server->lock) {
            wl_list_for_each(other, &server->lock->lock->surfaces, link) {
                if (other != lock_surface->surface && other->surface->mapped) {
                    keyboard_enter(server->seat, other->surface);
                    break;
                }
            }
        }
    }
    wl_list_remove(&lock_surface->map.link);
    wl_list_remove(&lock_surface->destroy.link);
    wlr_scene_node_destroy(&lock_surface->tree->node);
    free(lock_surface);
}

static void lock_new_surface(struct wl_listener *listener, void *data) {
    struct sh_lock *lock = wl_container_of(listener, lock, new_surface);
    struct sh_server *server = lock->server;
    struct wlr_session_lock_surface_v1 *surface = data;
    struct sh_lock_surface *lock_surface = calloc(1, sizeof(*lock_surface));
    if (!lock_surface)
        return;
    lock_surface->server = server;
    lock_surface->surface = surface;
    lock_surface->tree = wlr_scene_subsurface_tree_create(server->lock_tree, surface->surface);
    if (!lock_surface->tree) {
        free(lock_surface);
        return;
    }
    struct wlr_box box;
    wlr_output_layout_get_box(server->output_layout, surface->output, &box);
    wlr_scene_node_set_position(&lock_surface->tree->node, box.x, box.y);
    wlr_session_lock_surface_v1_configure(surface, box.width, box.height);
    add_listener(&surface->surface->events.map, &lock_surface->map, lock_surface_map);
    add_listener(&surface->events.destroy, &lock_surface->destroy, lock_surface_destroy);
}

static void lock_unlock(struct wl_listener *listener, void *data) {
    struct sh_lock *lock = wl_container_of(listener, lock, unlock);
    struct sh_server *server = lock->server;
    server->locked = false;
    wlr_scene_node_set_enabled(&server->lock_tree->node, false);
    wlr_seat_keyboard_clear_focus(server->seat);
    if (server->focused_toplevel && !server->focused_toplevel->minimized)
        focus_toplevel(server->focused_toplevel);
    else
        focus_previous(server);
    process_cursor_motion(server, 0);
    wlr_log(WLR_INFO, "Session unlocked");
    notify_subscribers(server); // "locked off"
}

static void lock_destroy(struct wl_listener *listener, void *data) {
    struct sh_lock *lock = wl_container_of(listener, lock, destroy);
    struct sh_server *server = lock->server;
    if (server->locked)
        wlr_log(WLR_ERROR, "Lock client vanished; the session stays locked until a new lock");
    wl_list_remove(&lock->new_surface.link);
    wl_list_remove(&lock->unlock.link);
    wl_list_remove(&lock->destroy.link);
    server->lock = NULL;
    free(lock);
}

void server_new_lock(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, new_lock);
    struct wlr_session_lock_v1 *wlr_lock = data;
    struct sh_lock *lock = server->lock ? NULL : calloc(1, sizeof(*lock));
    if (!lock) {
        wlr_session_lock_v1_destroy(wlr_lock); // Another locker is active: `finished`.
        return;
    }
    lock->server = server;
    lock->lock = wlr_lock;
    switcher_close(server, -1);
    overview_dismiss(server);
    end_window_peek(server, false);
    add_listener(&wlr_lock->events.new_surface, &lock->new_surface, lock_new_surface);
    add_listener(&wlr_lock->events.unlock, &lock->unlock, lock_unlock);
    add_listener(&wlr_lock->events.destroy, &lock->destroy, lock_destroy);
    server->lock = lock;
    bool relock = server->locked;
    server->locked = true;
    set_binding_mode(server, 0); // the volume keys and the like are bound outside any mode
    if (server->grabbed_toplevel)
        reset_cursor_mode(server);
    wlr_seat_keyboard_clear_focus(server->seat);
    wlr_seat_pointer_clear_focus(server->seat);
    wlr_scene_node_set_enabled(&server->lock_tree->node, true);
    wlr_log(WLR_INFO, "Session locked");
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        // A replacement locker for an abandoned lock finds the cover already shown.
        output->lock_presented = relock;
        wlr_output_schedule_frame(output->wlr_output);
    }
    send_locked_if_presented(server);
    notify_subscribers(server); // "locked on"
}

static void inhibitor_destroy(struct wl_listener *listener, void *data) {
    struct sh_inhibitor *inhibitor = wl_container_of(listener, inhibitor, destroy);
    struct sh_server *server = inhibitor->server;
    wl_list_remove(&inhibitor->destroy.link);
    free(inhibitor);
    wlr_idle_notifier_v1_set_inhibited(server->idle_notifier, --server->inhibitors > 0);
    idle_hold_changed(server);
}

#if WLR_HAS_SESSION
/* The machine stays awake while this VT is in front; switching away lets it sleep again. */
void session_active(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, session_active);
    if (server->session->active && server->sleep_inhibitor < 0) {
        server->sleep_inhibitor = sh_sleep_inhibit();
    } else if (!server->session->active && server->sleep_inhibitor >= 0) {
        close(server->sleep_inhibitor);
        server->sleep_inhibitor = -1;
    }
    idle_hold_changed(server); // no idle step while another VT is in front
}
#endif

/* Video players and games keep the session awake while any inhibitor exists. */
void server_new_inhibitor(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, new_inhibitor);
    struct wlr_idle_inhibitor_v1 *wlr_inhibitor = data;
    struct sh_inhibitor *inhibitor = calloc(1, sizeof(*inhibitor));
    if (!inhibitor)
        return;
    inhibitor->server = server;
    add_listener(&wlr_inhibitor->events.destroy, &inhibitor->destroy, inhibitor_destroy);
    wlr_idle_notifier_v1_set_inhibited(server->idle_notifier, ++server->inhibitors > 0);
    idle_hold_changed(server);
}
