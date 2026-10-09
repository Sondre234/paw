/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* wlr-layer-shell surfaces (panels, launchers, wallpapers): their stacking and placement, the
 * area they leave free for windows, and their keyboard focus. */
#include "server.h"

/* Reserve exclusive panel regions before positioning nonexclusive layers. */
void arrange_layers(struct sh_server *server) {
    struct sh_output *output;
    struct sh_layer *exclusive = NULL;
    wl_list_for_each(output, &server->outputs, link) {
        struct wlr_box full, usable;
        wlr_output_layout_get_box(server->output_layout, output->wlr_output, &full);
        usable = full;
        for (int pass = 0; pass < 2; ++pass) {
            for (int level = 3; level >= 0; --level) {
                struct sh_layer *layer;
                wl_list_for_each(layer, &server->layers, link) {
                    struct wlr_layer_surface_v1 *surface = layer->surface;
                    if (surface->output != output->wlr_output || !surface->initialized ||
                        (!surface->surface->mapped && !surface->initial_commit) ||
                        surface->current.layer != (unsigned)level ||
                        (surface->current.exclusive_zone > 0) != (pass == 0))
                        continue;
                    wlr_scene_node_reparent(&layer->scene->tree->node, server->layer_trees[level]);
                    wlr_scene_layer_surface_v1_configure(layer->scene, &full, &usable);
                    if (!exclusive && surface->surface->mapped && level >= 2 &&
                        surface->current.keyboard_interactive ==
                            ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE)
                        exclusive = layer;
                }
            }
        }
        if (!wlr_box_equal(&output->usable, &usable)) {
            output->usable = usable;
            reflow_output(server, output->wlr_output);
        }
    }
    if (exclusive)
        focus_layer(exclusive);
    else if (server->focused_layer &&
             (!server->focused_layer->surface->surface->mapped ||
              server->focused_layer->surface->current.keyboard_interactive ==
                  ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE))
        focus_previous(server);
}
static void layer_commit(struct wl_listener *listener, void *data) {
    struct sh_layer *layer = wl_container_of(listener, layer, commit);
    if (layer->surface->initial_commit || layer->surface->current.committed)
        arrange_layers(layer->server);
}
static void layer_map(struct wl_listener *listener, void *data) {
    struct sh_layer *layer = wl_container_of(listener, layer, map);
    arrange_layers(layer->server);
}
static void layer_unmap(struct wl_listener *listener, void *data) {
    struct sh_layer *layer = wl_container_of(listener, layer, unmap);
    if (layer->server->focused_layer == layer)
        focus_previous(layer->server);
    arrange_layers(layer->server);
}
static void layer_destroy(struct wl_listener *listener, void *data) {
    struct sh_layer *layer = wl_container_of(listener, layer, destroy);
    struct sh_server *server = layer->server;
    if (server->focused_layer == layer)
        focus_previous(server);
    wl_list_remove(&layer->commit.link);
    wl_list_remove(&layer->map.link);
    wl_list_remove(&layer->unmap.link);
    wl_list_remove(&layer->destroy.link);
    wl_list_remove(&layer->new_popup.link);
    wl_list_remove(&layer->link);
    free(layer);
    arrange_layers(server);
}
static void layer_new_popup(struct wl_listener *listener, void *data) {
    struct sh_layer *layer = wl_container_of(listener, layer, new_popup);
    create_popup(layer->server, data, layer->scene->tree);
}
void server_new_layer_surface(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, new_layer_surface);
    struct wlr_layer_surface_v1 *surface = data;
    if (!surface->output && !(surface->output = first_output(server))) {
        wlr_layer_surface_v1_destroy(surface);
        return;
    }
    struct sh_layer *layer = calloc(1, sizeof(*layer));
    if (!layer) {
        wlr_layer_surface_v1_destroy(surface);
        return;
    }
    layer->server = server;
    layer->surface = surface;
    layer->node = (struct sh_node){SH_NODE_LAYER, layer};
    layer->scene =
        wlr_scene_layer_surface_v1_create(server->layer_trees[surface->pending.layer], surface);
    if (!layer->scene) {
        free(layer);
        wlr_layer_surface_v1_destroy(surface);
        return;
    }
    layer->scene->tree->node.data = &layer->node;
    surface->data = layer;
    wl_list_insert(&server->layers, &layer->link);
    add_listener(&surface->surface->events.commit, &layer->commit, layer_commit);
    add_listener(&surface->surface->events.map, &layer->map, layer_map);
    add_listener(&surface->surface->events.unmap, &layer->unmap, layer_unmap);
    add_listener(&surface->events.destroy, &layer->destroy, layer_destroy);
    add_listener(&surface->events.new_popup, &layer->new_popup, layer_new_popup);
}
