/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* Windows as other clients see them: wlr-foreign-toplevel handles for taskbars, and
 * ext-foreign-toplevel handles with image capture sources for capturing a single window. */
#include "server.h"

static void foreign_activate(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, foreign_activate);
    struct wlr_foreign_toplevel_handle_v1_activated_event *event = data;
    if (event->seat == toplevel->server->seat)
        focus_toplevel(toplevel);
}
static void foreign_close(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, foreign_close);
    toplevel_close(toplevel);
}
static void foreign_maximize(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, foreign_maximize);
    struct wlr_foreign_toplevel_handle_v1_maximized_event *event = data;
    if (toplevel->fullscreen)
        return; // As for the client's own request: fullscreen wins.
    if (!event->maximized) {
        restore_toplevel(toplevel);
        return;
    }
    if (toplevel->tiled) {
        toplevel->floating = toplevel->placed = true;
        untile_toplevel(toplevel, false);
    }
    place_maximized(toplevel);
}
static void foreign_fullscreen(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, foreign_fullscreen);
    struct wlr_foreign_toplevel_handle_v1_fullscreen_event *event = data;
    set_fullscreen(toplevel, event->fullscreen);
}
static void foreign_minimize(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, foreign_minimize);
    struct wlr_foreign_toplevel_handle_v1_minimized_event *event = data;
    if (event->minimized)
        minimize_toplevel(toplevel);
    else
        focus_toplevel(toplevel);
}
static void update_listed_state(struct sh_toplevel *toplevel) {
    if (!toplevel->listed)
        return;
    const char *title = toplevel_title(toplevel), *app_id = toplevel_app_id(toplevel);
    struct wlr_ext_foreign_toplevel_handle_v1_state state = {title ? title : "Untitled",
                                                             app_id ? app_id : ""};
    wlr_ext_foreign_toplevel_handle_v1_update_state(toplevel->listed, &state);
}
void toplevel_title_changed(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, title_changed);
    const char *title = toplevel_title(toplevel);
    if (toplevel->foreign)
        wlr_foreign_toplevel_handle_v1_set_title(toplevel->foreign, title ? title : "Untitled");
    update_listed_state(toplevel);
    refresh_frame(toplevel); // opacity rules may match the title
    follow_dynamic_rules(toplevel);
    if (toplevel->urgent)
        notify_subscribers(toplevel->server); // the shell finds the window by its title
}
void toplevel_app_id_changed(struct wl_listener *listener, void *data) {
    struct sh_toplevel *toplevel = wl_container_of(listener, toplevel, app_id_changed);
    const char *app_id = toplevel_app_id(toplevel);
    if (toplevel->foreign)
        wlr_foreign_toplevel_handle_v1_set_app_id(toplevel->foreign, app_id ? app_id : "");
    update_listed_state(toplevel);
    follow_dynamic_rules(toplevel);
    if (toplevel->urgent)
        notify_subscribers(toplevel->server);
}
static void list_toplevel(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    toplevel->capture_scene = wlr_scene_create();
    if (!toplevel->capture_scene)
        return;
#if WLR_HAS_XWAYLAND
    if (toplevel->xsurface)
        wlr_scene_subsurface_tree_create(&toplevel->capture_scene->tree,
                                         toplevel_surface(toplevel));
    else
#endif
        wlr_scene_xdg_surface_create(&toplevel->capture_scene->tree, toplevel->xdg_toplevel->base);
    struct wlr_ext_foreign_toplevel_handle_v1_state state = {"", ""};
    toplevel->listed = wlr_ext_foreign_toplevel_handle_v1_create(server->toplevel_list, &state);
    if (toplevel->listed) {
        toplevel->listed->data = toplevel;
        update_listed_state(toplevel);
    }
}
static void unlist_toplevel(struct sh_toplevel *toplevel) {
    if (toplevel->listed)
        wlr_ext_foreign_toplevel_handle_v1_destroy(toplevel->listed);
    toplevel->listed = NULL;
    // Destroying the scene also ends any capture source made from it.
    if (toplevel->capture_scene)
        wlr_scene_node_destroy(&toplevel->capture_scene->tree.node);
    toplevel->capture_scene = NULL;
    toplevel->capture_source = NULL;
}
/* The window's capture source, made from its capture scene when first asked for, or NULL when
 * there is none to give: the session is locked, the window has no scene, or making the source
 * failed. Every client capturing the window shares it, and it ends with the scene. */
struct wlr_ext_image_capture_source_v1 *toplevel_capture_source(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    if (server->locked || !toplevel->capture_scene)
        return NULL;
    if (!toplevel->capture_source)
        toplevel->capture_source = wlr_ext_image_capture_source_v1_create_with_scene_node(
            &toplevel->capture_scene->tree.node, wl_display_get_event_loop(server->wl_display),
            server->allocator, server->renderer);
    return toplevel->capture_source;
}
void server_new_capture_request(struct wl_listener *listener, void *data) {
    struct wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request *request = data;
    struct sh_toplevel *toplevel = request->toplevel_handle->data;
    // With none to give, the client still gets the source it asked for, an inert one whose
    // sessions stop at once: refused, its next request would name an object that does not
    // exist, a protocol error that disconnects it.
    wlr_ext_foreign_toplevel_image_capture_source_manager_v1_request_accept(
        request, toplevel ? toplevel_capture_source(toplevel) : NULL);
}
void publish_toplevel(struct sh_toplevel *toplevel) {
    // Its number stays with it when it is published again, as a swallowed terminal is.
    if (!toplevel->id)
        toplevel->id = ++toplevel->server->last_window_id;
    list_toplevel(toplevel);
    toplevel->foreign = wlr_foreign_toplevel_handle_v1_create(toplevel->server->foreign_manager);
    if (!toplevel->foreign)
        return;
    toplevel_title_changed(&toplevel->title_changed, NULL);
    toplevel_app_id_changed(&toplevel->app_id_changed, NULL);
    add_listener(&toplevel->foreign->events.request_activate, &toplevel->foreign_activate,
                 foreign_activate);
    add_listener(&toplevel->foreign->events.request_close, &toplevel->foreign_close, foreign_close);
    add_listener(&toplevel->foreign->events.request_maximize, &toplevel->foreign_maximize,
                 foreign_maximize);
    add_listener(&toplevel->foreign->events.request_minimize, &toplevel->foreign_minimize,
                 foreign_minimize);
    add_listener(&toplevel->foreign->events.request_fullscreen, &toplevel->foreign_fullscreen,
                 foreign_fullscreen);
    wlr_foreign_toplevel_handle_v1_set_fullscreen(toplevel->foreign, toplevel->fullscreen);
    struct wlr_output *output = toplevel_output(toplevel);
    if (output)
        wlr_foreign_toplevel_handle_v1_output_enter(toplevel->foreign, output);
}
void unpublish_toplevel(struct sh_toplevel *toplevel) {
    unlist_toplevel(toplevel);
    if (!toplevel->foreign)
        return;
    window_objects_forget(toplevel);
    wl_list_remove(&toplevel->foreign_activate.link);
    wl_list_remove(&toplevel->foreign_close.link);
    wl_list_remove(&toplevel->foreign_maximize.link);
    wl_list_remove(&toplevel->foreign_minimize.link);
    wl_list_remove(&toplevel->foreign_fullscreen.link);
    wlr_foreign_toplevel_handle_v1_destroy(toplevel->foreign);
    toplevel->foreign = NULL;
}
