/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* ext-session-lock-v1 screen locking, and what keeps the machine awake: idle inhibitors, keep
 * awake, and a sleep inhibitor while this VT is in front. */
#include "server.h"

struct sh_lock_surface {
    struct sh_server *server;
    struct wlr_session_lock_surface_v1 *surface;
    struct wlr_scene_tree *tree;
    struct wl_listener map, destroy;
};

/* An idle inhibitor a client holds on a surface, and whether it applies, as last worked out. */
struct sh_inhibitor {
    struct sh_server *server;
    struct wlr_idle_inhibitor_v1 *wlr_inhibitor;
    bool applies;
    struct wl_list link; // sh_server.idle_inhibitors
    struct wl_listener destroy;
};

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
    wrapped_follow(server);
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
    // A peek ends at once, before the switcher, which would end its own with a fade.
    end_window_peek(server, false);
    switcher_close(server, -1);
    overview_dismiss(server);
    xray_dismiss(server);
    crossfade_end(server); // its pictures show the session as it was
    toys_end(server);
    cube_end(server);
    add_listener(&wlr_lock->events.new_surface, &lock->new_surface, lock_new_surface);
    add_listener(&wlr_lock->events.unlock, &lock->unlock, lock_unlock);
    add_listener(&wlr_lock->events.destroy, &lock->destroy, lock_destroy);
    server->lock = lock;
    bool relock = server->locked;
    server->locked = true;
    wrapped_save(server);
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

/* Whether a window is shown to the user: mapped, on a workspace shown, not minimized (nor in
 * the scratchpad, a hidden tab or swallowed), and on a monitor that is on. */
static bool window_shown(struct sh_toplevel *toplevel) {
    if (!toplevel_mapped(toplevel) || !toplevel_visible(toplevel))
        return false;
    struct sh_output *output;
    wl_list_for_each(output, &toplevel->server->outputs, link) {
        if (output_named(output, toplevel->output))
            return !output->powered_off;
    }
    return true; // on no monitor yet
}

/* Whether an idle inhibitor on `surface` applies: as idle-inhibit-unstable-v1 has it, only while
 * the surface is visible. A window's (or its popup's or subsurface's) while the window is shown
 * and the session unlocked, a panel's while it is mapped and the session unlocked; a surface
 * that is neither, of which paw cannot tell whether it shows, always. */
static bool inhibitor_applies(struct sh_server *server, struct wlr_surface *surface) {
    struct wlr_surface *root = wlr_surface_get_root_surface(surface);
    struct wlr_xdg_popup *popup;
    while ((popup = wlr_xdg_popup_try_from_wlr_surface(root)) && popup->parent)
        root = wlr_surface_get_root_surface(popup->parent);
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel_surface(toplevel) == root)
            return !server->locked && window_shown(toplevel);
    }
    if (wlr_xdg_toplevel_try_from_wlr_surface(root))
        return false; // a window not mapped
    if (wlr_layer_surface_v1_try_from_wlr_surface(root))
        return !server->locked && root->mapped;
    return true;
}

/* Works out again which idle inhibitors apply, after anything that may show or hide a surface:
 * notify_subscribers (windows mapping and unmapping, workspaces switching, windows moving, the
 * session locking), minimizing and restoring, monitors turning off and on, panels mapping and
 * unmapping. While any applies the idle steps are held and idle daemons hear the seat is
 * inhibited; once none does, the time without input counts from then. Cheap: a walk over the
 * few inhibitors there are. */
void idle_inhibitors_follow(struct sh_server *server) {
    int applying = 0;
    struct sh_inhibitor *inhibitor;
    wl_list_for_each(inhibitor, &server->idle_inhibitors, link) {
        inhibitor->applies = inhibitor_applies(server, inhibitor->wlr_inhibitor->surface);
        applying += inhibitor->applies;
    }
    bool held = applying > 0, was_held = server->inhibitors_applying > 0;
    server->inhibitors_applying = applying;
    if (held == was_held)
        return;
    wlr_log(WLR_INFO, "%s", held ? "An idle inhibitor applies: the idle steps wait"
                                 : "No idle inhibitor applies: the idle steps count again");
    wlr_idle_notifier_v1_set_inhibited(server->idle_notifier, held || server->keep_awake);
    idle_hold_changed(server);
}

/* Keep awake (the keep_awake actions, Quick Settings' tile): every idle step held off and idle
 * daemons told the seat is inhibited, as by an idle inhibitor that always applies, until turned
 * off; then the time without input counts from then. Subscribers hear "keep-awake on|off". */
void set_keep_awake(struct sh_server *server, bool on) {
    if (on == server->keep_awake)
        return;
    server->keep_awake = on;
    wlr_log(WLR_INFO, "%s", on ? "Keep awake on: the idle steps wait" : "Keep awake off");
    wlr_idle_notifier_v1_set_inhibited(server->idle_notifier,
                                       on || server->inhibitors_applying > 0);
    idle_hold_changed(server);
    notify_subscribers(server); // "keep-awake on|off"
}

static void inhibitor_destroy(struct wl_listener *listener, void *data) {
    struct sh_inhibitor *inhibitor = wl_container_of(listener, inhibitor, destroy);
    struct sh_server *server = inhibitor->server;
    wl_list_remove(&inhibitor->destroy.link);
    wl_list_remove(&inhibitor->link);
    free(inhibitor);
    idle_inhibitors_follow(server);
}

/* Video players and games keep the session awake while their window shows. */
void server_new_inhibitor(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, new_inhibitor);
    struct wlr_idle_inhibitor_v1 *wlr_inhibitor = data;
    struct sh_inhibitor *inhibitor = calloc(1, sizeof(*inhibitor));
    if (!inhibitor)
        return;
    inhibitor->server = server;
    inhibitor->wlr_inhibitor = wlr_inhibitor;
    wl_list_insert(server->idle_inhibitors.prev, &inhibitor->link);
    add_listener(&wlr_inhibitor->events.destroy, &inhibitor->destroy, inhibitor_destroy);
    idle_inhibitors_follow(server);
}

/* A line per idle inhibitor for `get idle`, the oldest first: "inhibitor", whether it applies,
 * and its surface as `surface` (query.c's describe_surface) has it. */
void describe_idle_inhibitors(struct sh_server *server, int fd,
                              void (*surface)(struct sh_server *, int, const char *,
                                              struct wlr_surface *)) {
    struct sh_inhibitor *inhibitor;
    wl_list_for_each(inhibitor, &server->idle_inhibitors, link) {
        char what[32];
        snprintf(what, sizeof(what), "inhibitor\t%d", inhibitor->applies);
        surface(server, fd, what, inhibitor->wlr_inhibitor->surface);
    }
}
