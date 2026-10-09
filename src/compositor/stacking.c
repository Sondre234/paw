/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* Stacking among the windows: the layers they are drawn in (tiles, floating windows over them
 * with layout.floating_above_tiles, windows kept above the others) and keeping a window above. */
#include "server.h"

/* The windows not fullscreen in front are drawn in three trees, bottom to top: `windows`, the
 * tiles and every other window but those in the next two; `floating_windows`, the windows out
 * of the tiling while layout.floating_above_tiles is on; and `above_windows`, those kept above
 * the others. Raising a window raises it within its own; fullscreen windows, the peek, the panels
 * and the overlays stay over all three. */
struct wlr_scene_tree *window_layer(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    if (toplevel->above)
        return server->above_windows;
    if (!toplevel->tiled && server_settings(server)->floating_above_tiles)
        return server->floating_windows;
    return server->windows;
}

bool is_window_layer(struct sh_server *server, const struct wlr_scene_tree *tree) {
    return tree && (tree == server->windows || tree == server->floating_windows ||
                    tree == server->above_windows);
}

/* Puts the window into the layer its state calls for, at the top of it, when it is in another
 * one; a window fullscreen in front, an X11 menu or one being peeked at stays where it is (the
 * peek's place for it moves instead, so that it returns into the right layer). */
void restack_toplevel(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    if (!toplevel->scene_tree)
        return;
#if WLR_HAS_XWAYLAND
    if (toplevel->unmanaged)
        return;
#endif
    struct wlr_scene_node *node = &toplevel->scene_tree->node;
    if (node->parent == server->peek_layer && server->peek_window == toplevel && server->peek_place)
        node = &server->peek_place->node;
    struct wlr_scene_tree *layer = window_layer(toplevel);
    if (is_window_layer(server, node->parent) && node->parent != layer)
        wlr_scene_node_reparent(node, layer);
}

/* Every window into its layer, as layout.floating_above_tiles changes: those moving keep their
 * order among themselves, from the bottom up. */
void restack_windows(struct sh_server *server) {
    struct wlr_scene_tree *layers[] = {server->windows, server->floating_windows,
                                       server->above_windows};
    struct sh_toplevel *moving[256];
    size_t count = 0;
    for (size_t i = 0; i < sizeof(layers) / sizeof(*layers); ++i) {
        struct wlr_scene_node *node;
        wl_list_for_each(node, &layers[i]->children, link) {
            struct sh_node *owner = node->data;
            if (!owner || owner->kind != SH_NODE_TOPLEVEL || count == sizeof(moving) / sizeof(*moving))
                continue;
            struct sh_toplevel *toplevel = owner->owner;
            if (&toplevel->scene_tree->node == node && window_layer(toplevel) != layers[i])
                moving[count++] = toplevel;
        }
    }
    for (size_t i = 0; i < count; ++i)
        restack_toplevel(moving[i]);
}

/* Keeps the window above the others, or lets it go back among them; the members of a window
 * group share their slot, and so whether it is kept above. */
void set_above(struct sh_toplevel *toplevel, bool above) {
    struct sh_server *server = toplevel->server;
    if (toplevel->above == above)
        return;
    struct sh_toplevel *member;
    wl_list_for_each(member, &server->toplevels, link) {
        if (member == toplevel || (toplevel->group && member->group == toplevel->group)) {
            member->above = above;
            restack_toplevel(member);
        }
    }
    // Kept above, it comes over those kept above already; let go, over the ones it joins.
    if (toplevel->scene_tree && is_window_layer(server, toplevel->scene_tree->node.parent))
        wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
    wlr_log(WLR_INFO, "Window %s", above ? "kept above" : "no longer kept above");
    notify_subscribers(server);
}

/* The layer a window is drawn in, for `get stacking`, or NULL for a tree that holds no window. */
static const char *layer_name(struct sh_server *server, const struct wlr_scene_tree *tree) {
    return tree == server->fullscreen_cover ? "fullscreen_cover"
           : tree == server->peek_layer     ? "peek"
           : tree == server->fullscreen     ? "fullscreen"
           : tree == server->above_windows  ? "above"
           : tree == server->floating_windows ? "floating"
           : tree == server->windows        ? "normal"
                                            : NULL;
}

/* `get stacking`: every window drawn among the windows, front to back: app_id, title, output and
 * the layer it is in (fullscreen_cover, peek, fullscreen, above, floating or normal). Hidden ones
 * count too, where they would show. */
void describe_stacking(struct sh_server *server, int fd) {
    control_reply(fd, "ok\n");
    struct wlr_scene_tree *trees[] = {server->fullscreen_cover, server->peek_layer,
                                      server->fullscreen,       server->above_windows,
                                      server->floating_windows, server->windows};
    for (size_t i = 0; i < sizeof(trees) / sizeof(*trees); ++i) {
        struct wlr_scene_node *node;
        wl_list_for_each_reverse(node, &trees[i]->children, link) {
            // Closing copies and the node keeping the place of the window peeked at are no
            // window's.
            struct sh_node *owner = node->data;
            if (!owner || owner->kind != SH_NODE_TOPLEVEL)
                continue;
            struct sh_toplevel *toplevel = owner->owner;
            char line[1024], app_id[256], title[512];
            const char *raw_app_id = toplevel_app_id(toplevel), *raw_title = toplevel_title(toplevel);
            snprintf(app_id, sizeof(app_id), "%s", raw_app_id ? raw_app_id : "");
            snprintf(title, sizeof(title), "%s", raw_title ? raw_title : "");
            for (char *c = app_id; *c; ++c)
                *c = *c == '\t' || *c == '\n' || *c == '\r' ? ' ' : *c;
            for (char *c = title; *c; ++c)
                *c = *c == '\t' || *c == '\n' || *c == '\r' ? ' ' : *c;
            snprintf(line, sizeof(line), "%s\t%s\t%s\t%s\n", app_id, title, toplevel->output,
                     layer_name(server, trees[i]));
            control_reply(fd, line);
        }
    }
}
