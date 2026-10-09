/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* XWayland: X11 windows run through the shared toplevel code, and the XWM waker that works
 * around stranded X events. */
#include "server.h"

#if WLR_HAS_XWAYLAND
/* X11 windows: managed ones behave like xdg toplevels; override-redirect ones
 * (menus, tooltips, drag icons) are drawn where they ask and never take part in focus order. */
static void xwayland_map(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, map);
    struct wlr_xwayland_surface *xsurface = toplevel->xsurface;
    struct sh_server *server = toplevel->server;
    toplevel->unmanaged = xsurface->override_redirect;
    toplevel->scene_tree =
        wlr_scene_tree_create(toplevel->unmanaged ? server->unmanaged : server->windows);
    toplevel->content = toplevel->scene_tree ? wlr_scene_tree_create(toplevel->scene_tree) : NULL;
    if (!toplevel->content ||
        !wlr_scene_subsurface_tree_create(toplevel->content, xsurface->surface)) {
        wlr_log(WLR_ERROR, "Cannot create scene for X11 window");
        if (toplevel->scene_tree)
            wlr_scene_node_destroy(&toplevel->scene_tree->node);
        toplevel->scene_tree = toplevel->content = NULL;
        toplevel->dim = NULL;
        return;
    }
    if (toplevel->unmanaged) {
        wlr_scene_node_set_position(&toplevel->scene_tree->node, xsurface->x, xsurface->y);
        if (!server->locked && wlr_xwayland_surface_override_redirect_wants_focus(xsurface))
            keyboard_enter(server->seat, xsurface->surface);
        return;
    }
    toplevel->scene_tree->node.data = &toplevel->node;
    toplevel->content->node.data = &toplevel->node;
    toplevel->fullscreen_cover = xsurface->fullscreen;
    map_toplevel(toplevel, xsurface->fullscreen,
                 xsurface->maximized_horz && xsurface->maximized_vert);
    refresh_decoration(toplevel);
}

static void xwayland_unmap(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, unmap);
    struct sh_server *server = toplevel->server;
    if (!toplevel->scene_tree)
        return;
    if (toplevel->unmanaged) {
        // Return the keyboard from a closed X11 menu to the focused window.
        if (server->seat->keyboard_state.focused_surface == toplevel->xsurface->surface) {
            if (server->focused_toplevel)
                focus_toplevel(server->focused_toplevel);
            else
                wlr_seat_keyboard_clear_focus(server->seat);
        }
    } else {
        unmap_toplevel(toplevel);
    }
    sh_anim_finish(&toplevel->anim);
    wlr_scene_node_destroy(&toplevel->scene_tree->node);
    toplevel->scene_tree = toplevel->content = NULL;
    toplevel->dim = NULL;
}

static void xwayland_associate(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, x_associate);
    struct wlr_surface *surface = toplevel->xsurface->surface;
    toplevel->associated = true;
    add_listener(&surface->events.map, &toplevel->map, xwayland_map);
    add_listener(&surface->events.unmap, &toplevel->unmap, xwayland_unmap);
    // The X11 and Wayland sockets race: Xwayland's first buffer can arrive before the
    // WL_SURFACE_SERIAL message that pairs it, and wlroots only maps on a later commit. An
    // unmapped surface gets no frame callbacks, so Xwayland never sends one (Wine dialogs).
    if (!surface->mapped && wlr_surface_has_buffer(surface))
        wlr_surface_map(surface);
}

static void xwayland_dissociate(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, x_dissociate);
    toplevel->associated = false;
    wl_list_remove(&toplevel->map.link);
    wl_list_remove(&toplevel->unmap.link);
}

static void xwayland_destroy(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, destroy);
    if (toplevel->associated) {
        wl_list_remove(&toplevel->map.link);
        wl_list_remove(&toplevel->unmap.link);
    }
    wl_list_remove(&toplevel->x_associate.link);
    wl_list_remove(&toplevel->x_dissociate.link);
    wl_list_remove(&toplevel->x_configure.link);
    wl_list_remove(&toplevel->x_activate.link);
    wl_list_remove(&toplevel->x_geometry.link);
    wl_list_remove(&toplevel->x_decorations.link);
    wl_list_remove(&toplevel->x_attention.link);
    wl_list_remove(&toplevel->x_hints.link);
    wl_list_remove(&toplevel->x_icon.link);
    free_toplevel(toplevel);
}

static bool xwayland_managed(struct sh_toplevel *toplevel) {
    return toplevel_mapped(toplevel) && !toplevel->unmanaged;
}

static void xwayland_set_decorations(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, x_decorations);
    if (xwayland_managed(toplevel))
        refresh_decoration(toplevel);
}

static void xwayland_request_configure(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, x_configure);
    struct wlr_xwayland_surface_configure_event *event = data;
    if (!xwayland_managed(toplevel)) {
        wlr_xwayland_surface_configure(toplevel->xsurface, event->x, event->y, event->width,
                                       event->height);
        if (toplevel->scene_tree)
            wlr_scene_node_set_position(&toplevel->scene_tree->node, event->x, event->y);
        return;
    }
    // Placement belongs to the compositor; floating windows may still choose their size.
    if (toplevel->fullscreen || toplevel->arranged || toplevel->tiled) {
        toplevel_refresh(toplevel);
        return;
    }
    toplevel_configure(toplevel, toplevel->scene_tree->node.x, toplevel->scene_tree->node.y,
                       event->width, event->height);
}

static void xwayland_set_geometry(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, x_geometry);
    if (toplevel->unmanaged && toplevel->scene_tree)
        wlr_scene_node_set_position(&toplevel->scene_tree->node, toplevel->xsurface->x,
                                    toplevel->xsurface->y);
    else if (xwayland_managed(toplevel))
        refresh_frame(toplevel);
}

static void xwayland_request_activate(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, x_activate);
    if (xwayland_managed(toplevel))
        activation_requested(toplevel);
}

/* _NET_WM_STATE_DEMANDS_ATTENTION and the urgency flag of WM_HINTS ask for attention the way
 * xdg-activation does; a client clears them (or the window is focused) when it is done. */
static void xwayland_attention(struct sh_toplevel *toplevel, bool wanted) {
    if (!xwayland_managed(toplevel))
        return;
    if (!wanted)
        set_urgent(toplevel, false);
    else if (toplevel->server->focused_toplevel != toplevel)
        activation_requested(toplevel);
}

static void xwayland_demands_attention(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, x_attention);
    xwayland_attention(toplevel, toplevel->xsurface->demands_attention);
}

static void xwayland_set_hints(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, x_hints);
    const xcb_icccm_wm_hints_t *hints = toplevel->xsurface->hints;
    bool urgent = hints && (hints->flags & XCB_ICCCM_WM_HINT_X_URGENCY);
    if (urgent == toplevel->x_hint_urgent)
        return;
    toplevel->x_hint_urgent = urgent;
    xwayland_attention(toplevel, urgent);
}

/* _NET_WM_ICON changed, or the window was associated, when wlroots reads its properties: the
 * window's icon is the size icon_size_rank prefers of those it gives, its colours premultiplied
 * as wl_shm's are, where X11 gives them as they are beside their alpha. X11 icons have no name.
 * Menus and tooltips have no place on the taskbar, so theirs are not read. */
static void xwayland_set_icon(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, x_icon);
    struct sh_icon icon = {0};
    xcb_ewmh_get_wm_icon_reply_t reply;
    if (toplevel->xsurface->override_redirect ||
        !wlr_xwayland_surface_fetch_icon(toplevel->xsurface, &reply)) {
        set_toplevel_icon(toplevel, icon);
        return;
    }
    xcb_ewmh_wm_icon_iterator_t best = {0};
    int best_rank = 0;
    for (xcb_ewmh_wm_icon_iterator_t each = xcb_ewmh_get_wm_icon_iterator(&reply); each.rem;
         xcb_ewmh_get_wm_icon_next(&each)) {
        int rank = icon_size_rank(each.width, each.height);
        if (rank > best_rank) {
            best = each;
            best_rank = rank;
        }
    }
    size_t count = (size_t)best.width * best.height;
    icon.pixels = best_rank ? malloc(count * 4) : NULL;
    if (icon.pixels) {
        icon.width = (int)best.width;
        icon.height = (int)best.height;
        for (size_t i = 0; i < count; ++i) {
            uint32_t pixel = best.data[i], alpha = pixel >> 24;
            uint32_t red = (pixel >> 16 & 0xff) * alpha, green = (pixel >> 8 & 0xff) * alpha;
            uint32_t blue = (pixel & 0xff) * alpha;
            icon.pixels[i] = alpha << 24 | (red + 127) / 255 << 16 | (green + 127) / 255 << 8 |
                             (blue + 127) / 255;
        }
    }
    xcb_ewmh_get_wm_icon_reply_wipe(&reply);
    set_toplevel_icon(toplevel, icon);
}

/* X11 grab requests carry no serial; accept them only while a button is held. */
static void xwayland_request_move(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, request_move);
    if (xwayland_managed(toplevel) && toplevel->server->seat->pointer_state.button_count > 0)
        begin_interactive(toplevel, SH_CURSOR_MOVE, 0);
}

static void xwayland_request_resize(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, request_resize);
    struct wlr_xwayland_resize_event *event = data;
    if (xwayland_managed(toplevel) && toplevel->server->seat->pointer_state.button_count > 0)
        begin_interactive(toplevel, SH_CURSOR_RESIZE, event->edges);
}

static void xwayland_request_maximize(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, request_maximize);
    if (!xwayland_managed(toplevel) || toplevel->fullscreen)
        return;
    maximize_toplevel(toplevel,
                      toplevel->xsurface->maximized_horz || toplevel->xsurface->maximized_vert);
}

static void xwayland_request_fullscreen(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, request_fullscreen);
    if (xwayland_managed(toplevel))
        set_client_fullscreen(toplevel, toplevel->xsurface->fullscreen);
}

static void xwayland_request_minimize(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, request_minimize);
    struct wlr_xwayland_minimize_event *event = data;
    if (!xwayland_managed(toplevel))
        return;
    if (event->minimize)
        minimize_toplevel(toplevel);
    else
        focus_toplevel(toplevel);
}

void server_new_xwayland_surface(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, new_xwayland_surface);
    struct wlr_xwayland_surface *xsurface = data;
    struct sh_toplevel *toplevel = calloc(1, sizeof(*toplevel));
    if (!toplevel) {
        wlr_xwayland_surface_close(xsurface);
        return;
    }
    toplevel->server = server;
    toplevel->xsurface = xsurface;
    toplevel->node = (struct sh_node){SH_NODE_TOPLEVEL, toplevel};
    xsurface->data = toplevel;
    add_listener(&xsurface->events.associate, &toplevel->x_associate, xwayland_associate);
    add_listener(&xsurface->events.dissociate, &toplevel->x_dissociate, xwayland_dissociate);
    add_listener(&xsurface->events.destroy, &toplevel->destroy, xwayland_destroy);
    add_listener(&xsurface->events.request_configure, &toplevel->x_configure,
                 xwayland_request_configure);
    add_listener(&xsurface->events.request_activate, &toplevel->x_activate,
                 xwayland_request_activate);
    add_listener(&xsurface->events.set_geometry, &toplevel->x_geometry, xwayland_set_geometry);
    add_listener(&xsurface->events.set_decorations, &toplevel->x_decorations,
                 xwayland_set_decorations);
    add_listener(&xsurface->events.request_demands_attention, &toplevel->x_attention,
                 xwayland_demands_attention);
    add_listener(&xsurface->events.set_hints, &toplevel->x_hints, xwayland_set_hints);
    add_listener(&xsurface->events.set_icon, &toplevel->x_icon, xwayland_set_icon);
    add_listener(&xsurface->events.set_title, &toplevel->title_changed, toplevel_title_changed);
    add_listener(&xsurface->events.set_class, &toplevel->app_id_changed, toplevel_app_id_changed);
    add_listener(&xsurface->events.request_move, &toplevel->request_move, xwayland_request_move);
    add_listener(&xsurface->events.request_resize, &toplevel->request_resize,
                 xwayland_request_resize);
    add_listener(&xsurface->events.request_maximize, &toplevel->request_maximize,
                 xwayland_request_maximize);
    add_listener(&xsurface->events.request_fullscreen, &toplevel->request_fullscreen,
                 xwayland_request_fullscreen);
    add_listener(&xsurface->events.request_minimize, &toplevel->request_minimize,
                 xwayland_request_minimize);
}

#if PAW_XWM_WAKER
/* wlroots' XWM can strand X events: xcb reads them into its queue during flushes
 * and round-trips outside the event handler, and the handler's post-dispatch
 * check ignores that queue (packaging/patches/wlroots-xwm-drain.patch fixes it).
 * That strands the first MapRequest after Xwayland starts, among others. While
 * Xwayland runs, a periodic client message from a separate connection, sent only
 * to the XWM's own window, makes its socket readable so the handler drains the queue. */
enum { XWM_WAKE_INTERVAL_MS = 250 };

void close_xwm_waker(struct sh_server *server) {
    if (server->waker_timer)
        wl_event_source_remove(server->waker_timer);
    if (server->waker_input)
        wl_event_source_remove(server->waker_input);
    if (server->xwm_waker)
        xcb_disconnect(server->xwm_waker);
    server->waker_timer = server->waker_input = NULL;
    server->xwm_waker = NULL;
}

static int xwm_waker_tick(void *data) {
    struct sh_server *server = data;
    xcb_client_message_event_t message = {.response_type = XCB_CLIENT_MESSAGE,
                                          .format = 32,
                                          .window = server->xwm_window,
                                          .type = server->waker_atom};
    // An empty event mask delivers the message only to the window's creator: the XWM.
    xcb_send_event(server->xwm_waker, false, server->xwm_window, XCB_EVENT_MASK_NO_EVENT,
                   (const char *)&message);
    xcb_flush(server->xwm_waker);
    wl_event_source_timer_update(server->waker_timer, XWM_WAKE_INTERVAL_MS);
    return 0;
}

static int xwm_waker_input(int fd, uint32_t mask, void *data) {
    struct sh_server *server = data;
    xcb_generic_event_t *event;
    while ((event = xcb_poll_for_event(server->xwm_waker)))
        free(event); // Only errors can arrive; no events are selected.
    if ((mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) || xcb_connection_has_error(server->xwm_waker))
        close_xwm_waker(server);
    return 0;
}

static void open_xwm_waker(struct sh_server *server) {
    close_xwm_waker(server);
    server->xwm_waker = xcb_connect(server->xwayland->display_name, NULL);
    if (xcb_connection_has_error(server->xwm_waker)) {
        xcb_disconnect(server->xwm_waker);
        server->xwm_waker = NULL;
        wlr_log(WLR_ERROR, "Cannot connect XWM waker; X11 windows may appear late");
        return;
    }
    xcb_atom_t atoms[2] = {XCB_ATOM_NONE, XCB_ATOM_NONE};
    const char *names[2] = {"_PAW_XWM_WAKE", "_NET_SUPPORTING_WM_CHECK"};
    for (int i = 0; i < 2; ++i) {
        xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(
            server->xwm_waker,
            xcb_intern_atom(server->xwm_waker, false, strlen(names[i]), names[i]), NULL);
        if (reply)
            atoms[i] = reply->atom;
        free(reply);
    }
    server->waker_atom = atoms[0];
    // Like the XWM's connection, this one must not keep an idle Xwayland running.
    xcb_xfixes_query_version_reply_t *xfixes = xcb_xfixes_query_version_reply(
        server->xwm_waker, xcb_xfixes_query_version(server->xwm_waker, 6, 0), NULL);
    if (xfixes && xfixes->major_version >= 6)
        xcb_xfixes_set_client_disconnect_mode(server->xwm_waker,
                                              XCB_XFIXES_CLIENT_DISCONNECT_FLAGS_TERMINATE);
    free(xfixes);
    server->xwm_window = XCB_WINDOW_NONE;
    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(server->xwm_waker)).data;
    xcb_get_property_reply_t *check = xcb_get_property_reply(
        server->xwm_waker,
        xcb_get_property(server->xwm_waker, false, screen->root, atoms[1], XCB_ATOM_WINDOW, 0, 1),
        NULL);
    if (check && xcb_get_property_value_length(check) == sizeof(xcb_window_t))
        server->xwm_window = *(xcb_window_t *)xcb_get_property_value(check);
    free(check);
    struct wl_event_loop *loop = wl_display_get_event_loop(server->wl_display);
    server->waker_input = wl_event_loop_add_fd(loop, xcb_get_file_descriptor(server->xwm_waker),
                                               WL_EVENT_READABLE, xwm_waker_input, server);
    server->waker_timer = wl_event_loop_add_timer(loop, xwm_waker_tick, server);
    if (server->waker_atom == XCB_ATOM_NONE || server->xwm_window == XCB_WINDOW_NONE ||
        !server->waker_input || !server->waker_timer) {
        wlr_log(WLR_ERROR, "Cannot set up XWM waker; X11 windows may appear late");
        close_xwm_waker(server);
        return;
    }
    xwm_waker_tick(server); // drain whatever the XWM stranded while attaching
}
#endif

void xwayland_ready(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, xwayland_ready);
    wlr_log(WLR_INFO, "XWayland ready on DISPLAY=%s", server->xwayland->display_name);
    wlr_xwayland_set_seat(server->xwayland, server->seat);
#if PAW_XWM_WAKER
    open_xwm_waker(server);
#endif
    if (wlr_xcursor_manager_load(server->cursor_mgr, 1)) {
        struct wlr_xcursor *xcursor =
            wlr_xcursor_manager_get_xcursor(server->cursor_mgr, "default", 1);
        if (xcursor) {
            struct wlr_xcursor_image *image = xcursor->images[0];
            wlr_xwayland_set_cursor(server->xwayland, wlr_xcursor_image_get_buffer(image),
                                    image->hotspot_x, image->hotspot_y);
        }
    }
}
#endif
