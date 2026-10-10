/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Keyboard shortcuts inhibitors (keyboard-shortcuts-inhibit-unstable-v1): a virtual machine, a
 * remote desktop or a game asks for the keys the compositor's bindings take, which then go to its
 * surface while it has the keyboard, the user hearing so the first time. toggle_shortcuts_inhibit
 * turns the focused one off and on again, its binding the one that still runs;
 * keyboard.shortcuts_inhibit and a window rule's shortcuts_inhibit = false refuse them. An X11
 * window's active keyboard grab, which Xwayland asks for through
 * xwayland-keyboard-grab-unstable-v1, counts as one of its surface's. */
#include "server.h"
#if WLR_HAS_XWAYLAND
#include "xwayland-keyboard-grab-unstable-v1-protocol.h"
#endif

struct sh_shortcuts_inhibitor {
    struct wl_list link; // sh_server.shortcuts.inhibitors
    struct sh_server *server;
    /* A client's inhibitor, or for an X11 window's grab NULL and Xwayland's
     * zwp_xwayland_keyboard_grab_v1. */
    struct wlr_keyboard_shortcuts_inhibitor_v1 *wlr_inhibitor;
    struct wl_resource *grab;
    struct wlr_surface *surface;
    /* Honoured; refused by keyboard.shortcuts_inhibit or a window rule, as last worked out;
     * turned off by the user (toggle_shortcuts_inhibit) until they turn it on again. */
    bool active, refused, off;
    bool told; // the user heard of it taking the keys, for a surface that is no window
    struct wl_listener destroy; // the inhibitor's, or the grabbing surface's
};

/* The window a surface belongs to, or NULL for a panel, a lock surface and the like. */
static struct sh_toplevel *surface_toplevel(struct sh_server *server, struct wlr_surface *surface) {
    struct wlr_surface *root = wlr_surface_get_root_surface(surface);
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel_surface(toplevel) == root)
            return toplevel;
    }
    return NULL;
}

/* Whether the settings refuse the inhibitor: keyboard.shortcuts_inhibit, or the window rules for
 * its window as it is now called. */
static bool refused(struct sh_shortcuts_inhibitor *inhibitor) {
    struct sh_server *server = inhibitor->server;
    if (!server_settings(server)->shortcuts_inhibit)
        return true;
    struct sh_toplevel *toplevel = surface_toplevel(server, inhibitor->surface);
    if (!toplevel)
        return false;
    const char *app_id = toplevel_app_id(toplevel), *title = toplevel_title(toplevel);
    struct sh_window_rule rule = {.floating = -1};
    return server->callbacks->window_rule(server->callbacks->userdata, app_id ? app_id : "",
                                          title ? title : "", &rule) &&
           rule.no_shortcuts_inhibit;
}

/* Honours the inhibitor, or stops honouring it, as the settings and the user say; a client's
 * inhibitor hears of each change (active, inactive), a grab nothing. */
static void apply(struct sh_shortcuts_inhibitor *inhibitor) {
    inhibitor->refused = refused(inhibitor);
    bool active = !inhibitor->refused && !inhibitor->off;
    if (active == inhibitor->active)
        return;
    inhibitor->active = active;
    if (inhibitor->wlr_inhibitor && active)
        wlr_keyboard_shortcuts_inhibitor_v1_activate(inhibitor->wlr_inhibitor);
    else if (inhibitor->wlr_inhibitor)
        wlr_keyboard_shortcuts_inhibitor_v1_deactivate(inhibitor->wlr_inhibitor);
}

/* The inhibitor of the surface with the keyboard, honoured or not; NULL for none. */
static struct sh_shortcuts_inhibitor *focused_inhibitor(struct sh_server *server) {
    struct wlr_surface *focus = server->seat->keyboard_state.focused_surface;
    struct sh_shortcuts_inhibitor *inhibitor;
    wl_list_for_each(inhibitor, &server->shortcuts.inhibitors, link) {
        if (focus && inhibitor->surface == focus)
            return inhibitor;
    }
    return NULL;
}

/* Whether the keys go to the focused surface rather than to the bindings: it holds an inhibitor
 * that is honoured. Never while the session is locked, where the `locked` bindings run as
 * ever. */
bool shortcuts_inhibited(struct sh_server *server) {
    struct sh_shortcuts_inhibitor *inhibitor = focused_inhibitor(server);
    return !server->locked && inhibitor && inhibitor->active;
}

/* The name the user knows the surface's window by, its title, else its app id, as a piece of a
 * line to the shell: at most `size` - 1 bytes, cut between characters, on one line. */
static void surface_name(struct sh_server *server, struct wlr_surface *surface, char *name,
                         size_t size) {
    struct sh_toplevel *toplevel = surface_toplevel(server, surface);
    const char *title = toplevel ? toplevel_title(toplevel) : NULL;
    const char *app_id = toplevel ? toplevel_app_id(toplevel) : NULL;
    copy_field(name, size, title && *title ? title : app_id && *app_id ? app_id : "the window");
    drop_partial_utf8(name);
}

/* Tells the user, once for each window, that it has the shortcuts now and which keys take them
 * back: the shell hears "notice SUMMARY<tab>BODY" and shows a notification. */
static void announce(struct sh_server *server, struct sh_shortcuts_inhibitor *inhibitor) {
    struct sh_toplevel *toplevel = surface_toplevel(server, inhibitor->surface);
    bool *told = toplevel ? &toplevel->shortcuts_told : &inhibitor->told;
    if (*told)
        return;
    *told = true;
    const struct sh_callbacks *callbacks = server->callbacks;
    const char *keys =
        callbacks->binding_keys
            ? callbacks->binding_keys(callbacks->userdata, SH_TOGGLE_SHORTCUTS_INHIBIT)
            : NULL;
    char name[160], line[384];
    surface_name(server, inhibitor->surface, name, sizeof(name));
    if (keys && *keys)
        snprintf(line, sizeof(line), "notice Shortcuts go to %s\t%.64s gives them back\n", name,
                 keys);
    else
        snprintf(line, sizeof(line),
                 "notice Shortcuts go to %s\tThey come back as another window has the keyboard\n",
                 name);
    send_shell_line(server, line);
}

/* Notes which inhibitor takes the keys now. One that has just started to leaves the binding mode
 * in use, as locking does, so that the bindings outside any mode are the ones it holds, and the
 * user hears of it the first time. */
static void shortcuts_changed(struct sh_server *server) {
    struct sh_shortcuts_inhibitor *now =
        shortcuts_inhibited(server) ? focused_inhibitor(server) : NULL;
    if (now == server->shortcuts.effective)
        return;
    server->shortcuts.effective = now;
    if (!now) {
        wlr_log(WLR_INFO, "Shortcuts are the compositor's again");
        return;
    }
    struct sh_toplevel *toplevel = surface_toplevel(server, now->surface);
    const char *app_id = toplevel ? toplevel_app_id(toplevel) : NULL;
    wlr_log(WLR_INFO, "Shortcuts go to %s",
            app_id && *app_id ? app_id : "a surface without a window");
    set_binding_mode(server, 0);
    announce(server, now);
}

/* toggle_shortcuts_inhibit: the focused surface's inhibitor turned off, which gives the bindings
 * their keys back, or on again, which the on-screen display says. One the settings refuse stays
 * refused. */
void toggle_shortcuts_inhibit(struct sh_server *server) {
    struct sh_shortcuts_inhibitor *inhibitor = focused_inhibitor(server);
    if (!inhibitor) {
        wlr_log(WLR_INFO, "The focused surface asks for no shortcuts");
        return;
    }
    apply(inhibitor);
    if (inhibitor->refused) {
        wlr_log(WLR_INFO, "The focused surface's shortcuts inhibitor is refused "
                          "(keyboard.shortcuts_inhibit or a window rule)");
        return;
    }
    inhibitor->off = !inhibitor->off;
    apply(inhibitor);
    shortcuts_changed(server);
    struct wlr_output *output = focused_output(server);
    char name[160], line[256];
    surface_name(server, inhibitor->surface, name, sizeof(name));
    if (inhibitor->off)
        snprintf(line, sizeof(line), "osd %s -1 Shortcuts back to the desktop\n",
                 output ? output->name : "-");
    else
        snprintf(line, sizeof(line), "osd %s -1 Shortcuts go to %s\n", output ? output->name : "-",
                 name);
    send_shell_line(server, line);
}

static void forget_inhibitor(struct sh_shortcuts_inhibitor *inhibitor) {
    struct sh_server *server = inhibitor->server;
    wl_list_remove(&inhibitor->destroy.link);
    wl_list_remove(&inhibitor->link);
    if (server->shortcuts.effective == inhibitor)
        server->shortcuts.effective = NULL;
    if (inhibitor->grab)
        wl_resource_set_user_data(inhibitor->grab, NULL);
    free(inhibitor);
    shortcuts_changed(server);
}

static void inhibitor_destroy(struct wl_listener *listener, void *data) {
    struct sh_shortcuts_inhibitor *inhibitor = wl_container_of(listener, inhibitor, destroy);
    forget_inhibitor(inhibitor);
}

/* Starts following an inhibitor or a grab of `surface`, honouring it unless refused. */
static struct sh_shortcuts_inhibitor *add_inhibitor(struct sh_server *server,
                                                    struct wlr_surface *surface) {
    struct sh_shortcuts_inhibitor *inhibitor = calloc(1, sizeof(*inhibitor));
    if (!inhibitor)
        return NULL;
    inhibitor->server = server;
    inhibitor->surface = surface;
    wl_list_insert(&server->shortcuts.inhibitors, &inhibitor->link);
    return inhibitor;
}

/* Honours a new inhibitor or grab unless refused, and has it take the keys if its surface has the
 * keyboard. */
static void start_inhibitor(struct sh_shortcuts_inhibitor *inhibitor) {
    apply(inhibitor);
    if (inhibitor->refused)
        wlr_log(WLR_INFO, "Refused a keyboard %s (keyboard.shortcuts_inhibit or a window rule)",
                inhibitor->grab ? "grab" : "shortcuts inhibitor");
    shortcuts_changed(inhibitor->server);
}

static void new_inhibitor(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, shortcuts.new_inhibitor);
    struct wlr_keyboard_shortcuts_inhibitor_v1 *wlr_inhibitor = data;
    struct sh_shortcuts_inhibitor *inhibitor = add_inhibitor(server, wlr_inhibitor->surface);
    if (!inhibitor)
        return; // never honoured: its client hears nothing
    inhibitor->wlr_inhibitor = wlr_inhibitor;
    add_listener(&wlr_inhibitor->events.destroy, &inhibitor->destroy, inhibitor_destroy);
    start_inhibitor(inhibitor);
}

#if WLR_HAS_XWAYLAND
/* xwayland-keyboard-grab-unstable-v1: Xwayland asks for the keyboard to go to an X11 window as an
 * X11 client takes an active grab of it (XGrabKeyboard), such as a virtual machine's or a remote
 * desktop's window, and lets go as the grab ends. While its window has the keyboard, the grab
 * holds the bindings' keys as an inhibitor does; it never takes the keyboard from another window.
 * The global is offered to Xwayland alone. */
static void grab_handle_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
}
static const struct zwp_xwayland_keyboard_grab_v1_interface grab_implementation = {
    .destroy = grab_handle_destroy,
};

static void grab_resource_destroy(struct wl_resource *resource) {
    struct sh_shortcuts_inhibitor *inhibitor = wl_resource_get_user_data(resource);
    if (inhibitor) {
        inhibitor->grab = NULL;
        forget_inhibitor(inhibitor);
    }
}

static void manager_handle_destroy(struct wl_client *client, struct wl_resource *resource) {
    wl_resource_destroy(resource);
}

static void manager_grab_keyboard(struct wl_client *client, struct wl_resource *resource,
                                  uint32_t id, struct wl_resource *surface_resource,
                                  struct wl_resource *seat_resource) {
    struct sh_server *server = wl_resource_get_user_data(resource);
    struct wl_resource *grab = wl_resource_create(client, &zwp_xwayland_keyboard_grab_v1_interface,
                                                  wl_resource_get_version(resource), id);
    if (!grab) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(grab, &grab_implementation, NULL, grab_resource_destroy);
    struct wlr_seat_client *seat = wlr_seat_client_from_resource(seat_resource);
    if (!seat || seat->seat != server->seat)
        return; // another seat's, or one that is gone: the grab does nothing
    struct sh_shortcuts_inhibitor *inhibitor =
        add_inhibitor(server, wlr_surface_from_resource(surface_resource));
    if (!inhibitor)
        return;
    inhibitor->grab = grab;
    wl_resource_set_user_data(grab, inhibitor);
    // A grab outlives its surface as an object that does nothing.
    add_listener(&inhibitor->surface->events.destroy, &inhibitor->destroy, inhibitor_destroy);
    start_inhibitor(inhibitor);
}

static const struct zwp_xwayland_keyboard_grab_manager_v1_interface manager_implementation = {
    .destroy = manager_handle_destroy,
    .grab_keyboard = manager_grab_keyboard,
};

static void bind_grab_manager(struct wl_client *client, void *data, uint32_t version, uint32_t id) {
    struct wl_resource *resource = wl_resource_create(
        client, &zwp_xwayland_keyboard_grab_manager_v1_interface, (int)version, id);
    if (!resource) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(resource, &manager_implementation, data, NULL);
}

/* Which client sees which global: the grab manager is Xwayland's alone, as the protocol asks. */
static bool global_filter(const struct wl_client *client, const struct wl_global *global,
                          void *data) {
    struct sh_server *server = data;
    if (global != server->shortcuts.grab_manager)
        return true;
    // Xwayland is destroyed before the clients as the compositor quits.
    return server->running && server->xwayland && server->xwayland->server &&
           client == server->xwayland->server->client;
}
#endif

static void keyboard_focus_change(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, shortcuts.keyboard_focus_change);
    // A window's rules are matched again as it gets the keyboard, by the name it has now.
    struct sh_shortcuts_inhibitor *inhibitor = focused_inhibitor(server);
    if (inhibitor)
        apply(inhibitor);
    shortcuts_changed(server);
}

/* A reload may have changed keyboard.shortcuts_inhibit and the window rules. */
void shortcuts_reload(struct sh_server *server) {
    struct sh_shortcuts_inhibitor *inhibitor;
    wl_list_for_each(inhibitor, &server->shortcuts.inhibitors, link) apply(inhibitor);
    shortcuts_changed(server);
}

/* For `get shortcuts`: "inhibited 1" while the keys go to the focused surface (else 0), then a
 * line per inhibitor: "inhibitor" (or "grab" for an X11 window's), whether it is honoured
 * ("active"), turned off by the user ("off") or refused ("refused"), whether its surface has the
 * keyboard, and the surface as `surface` describes it. */
void describe_shortcuts_inhibitors(struct sh_server *server, int fd,
                                   void (*surface)(struct sh_server *, int, const char *,
                                                   struct wlr_surface *)) {
    char line[64];
    snprintf(line, sizeof(line), "inhibited\t%d\n", shortcuts_inhibited(server));
    control_reply(fd, line);
    struct wlr_surface *focus = server->seat->keyboard_state.focused_surface;
    struct sh_shortcuts_inhibitor *inhibitor;
    wl_list_for_each_reverse(inhibitor, &server->shortcuts.inhibitors, link) {
        snprintf(line, sizeof(line), "%s\t%s\t%d", inhibitor->grab ? "grab" : "inhibitor",
                 inhibitor->active    ? "active"
                 : inhibitor->refused ? "refused"
                                      : "off",
                 focus && inhibitor->surface == focus);
        surface(server, fd, line, inhibitor->surface);
    }
}

void shortcuts_inhibit_init(struct sh_server *server) {
    wl_list_init(&server->shortcuts.inhibitors);
    server->shortcuts.manager = wlr_keyboard_shortcuts_inhibit_v1_create(server->wl_display);
    add_listener(&server->shortcuts.manager->events.new_inhibitor, &server->shortcuts.new_inhibitor,
                 new_inhibitor);
    add_listener(&server->seat->keyboard_state.events.focus_change,
                 &server->shortcuts.keyboard_focus_change, keyboard_focus_change);
#if WLR_HAS_XWAYLAND
    server->shortcuts.grab_manager =
        wl_global_create(server->wl_display, &zwp_xwayland_keyboard_grab_manager_v1_interface, 1,
                         server, bind_grab_manager);
    wl_display_set_global_filter(server->wl_display, global_filter, server);
#endif
}

void shortcuts_inhibit_finish(struct sh_server *server) {
    wl_list_remove(&server->shortcuts.new_inhibitor.link);
    wl_list_remove(&server->shortcuts.keyboard_focus_change.link);
}
