/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Tearing (windows.allow_tearing): a fullscreen window that asks for it through
 * tearing-control-v1, or that a rule's allow_tearing names, has its frames shown as soon as they
 * are drawn rather than at the monitor's next refresh, for the least latency in games, while
 * nothing else shows on its monitor. Its output's page flips are asynchronous then
 * (wlr_output_state.tearing_page_flip), and normal ones where the backend refuses them. */
#include "server.h"

/* Whether the rules let `toplevel` tear, matched again only when the window or the configuration
 * changed: matching every frame would cost more than the frame. */
static bool ruled_to_tear(struct sh_output *output, struct sh_toplevel *toplevel) {
    struct sh_server *server = output->server;
    struct sh_tearing *tearing = &output->tearing;
    if (tearing->rule_window == toplevel && tearing->rule_id == toplevel->id &&
        tearing->rule_generation == server->config_generation)
        return tearing->ruled;
    const struct sh_callbacks *callbacks = server->callbacks;
    const char *app_id = toplevel_app_id(toplevel), *title = toplevel_title(toplevel);
    struct sh_window_rule rule;
    tearing->ruled = callbacks->window_rule &&
                     callbacks->window_rule(callbacks->userdata, app_id ? app_id : "",
                                            title ? title : "", &rule) &&
                     rule.allow_tearing;
    tearing->rule_window = toplevel;
    tearing->rule_id = toplevel->id;
    tearing->rule_generation = server->config_generation;
    return tearing->ruled;
}

/* tearing-control-v1's hint for the window's surface: async asks to tear. */
static bool asks_to_tear(struct sh_server *server, struct sh_toplevel *toplevel) {
    struct wlr_surface *surface = toplevel_surface(toplevel);
    return surface && server->tearing_control &&
           wlr_tearing_control_manager_v1_surface_hint_from_surface(server->tearing_control,
                                                                   surface) ==
               WP_TEARING_CONTROL_V1_PRESENTATION_HINT_ASYNC;
}

static bool in_tree(struct wlr_scene_node *node, struct wlr_scene_tree *tree) {
    for (; node; node = node->parent ? &node->parent->node : NULL) {
        if (node == &tree->node)
            return true;
    }
    return false;
}

struct covering {
    struct wlr_scene_tree *window;
    bool seen, covered;
};

/* The scene's buffers on an output come bottom to top: one of anything else after the window's
 * own is drawn over it. */
static void find_covering(struct wlr_scene_buffer *buffer, int sx, int sy, void *data) {
    struct covering *covering = data;
    if (!buffer->buffer || buffer->opacity <= 0)
        return;
    if (in_tree(&buffer->node, covering->window))
        covering->seen = true;
    else if (covering->seen)
        covering->covered = true;
}

/* The window on `output` that may tear now, or NULL with why not: the session is locked, the
 * output is magnified, the overview, the switcher or a peek is up, no window is fullscreen on it,
 * the one that is neither asks nor is ruled to tear, or something is drawn over it (a panel, a
 * notification, a menu, another window). */
static struct sh_toplevel *tearing_window(struct sh_output *output, const char **why) {
    struct sh_server *server = output->server;
    if (server->locked) {
        *why = "locked";
        return NULL;
    }
    if (output->zoomed) {
        *why = "magnified";
        return NULL;
    }
    if (server->overview.visible || server->switcher.open || server->peeking || server->peek_window) {
        *why = "overlay";
        return NULL;
    }
    struct sh_toplevel *toplevel, *found = NULL;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (!toplevel->unmanaged && toplevel->fullscreen && toplevel_mapped(toplevel) &&
            toplevel_visible(toplevel) && output_named(output, toplevel->output)) {
            found = toplevel;
            break;
        }
    }
    if (!found) {
        *why = "no fullscreen window";
        return NULL;
    }
    if (!asks_to_tear(server, found) && !ruled_to_tear(output, found)) {
        *why = "not asked";
        return NULL;
    }
    struct wlr_scene_output *scene_output =
        wlr_scene_get_scene_output(server->scene, output->wlr_output);
    struct covering covering = {.window = found->scene_tree};
    if (scene_output)
        wlr_scene_output_for_each_buffer(scene_output, find_covering, &covering);
    if (!covering.seen || covering.covered) {
        *why = "covered";
        return NULL;
    }
    return found;
}

/* The backend's test of a tearing page flip; under --headless, whose outputs take one, the
 * outputs PAW_TEST_REFUSE_TEARING names refuse it, as a GPU or driver without asynchronous flips
 * does. */
static bool test_tearing(struct sh_output *output, const struct wlr_output_state *state) {
    return !test_names_output(output, "PAW_TEST_REFUSE_TEARING") &&
           wlr_output_test_state(output->wlr_output, state);
}

/* Commits `output`'s next frame with an asynchronous page flip while its fullscreen window may
 * tear, or a normal one where the backend refuses that. False, having done nothing, when it may
 * not tear (always with windows.allow_tearing off), for the caller to commit as usual. */
bool output_commit_tearing(struct sh_output *output, struct wlr_scene_output *scene_output,
                           const struct wlr_scene_output_state_options *options) {
    struct sh_tearing *tearing = &output->tearing;
    const char *why = "off";
    struct sh_toplevel *window = server_settings(output->server)->allow_tearing
                                     ? tearing_window(output, &why)
                                     : NULL;
    if (!window) {
        tearing->why = why;
        tearing->title[0] = '\0';
        return false;
    }
    const char *title = toplevel_title(window);
    snprintf(tearing->title, sizeof(tearing->title), "%s", title ? title : "");
    if (!wlr_scene_output_needs_frame(scene_output))
        return true;
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    bool done = wlr_scene_output_build_state(scene_output, &state, options);
    if (done && (state.committed & WLR_OUTPUT_STATE_BUFFER)) {
        state.tearing_page_flip = true;
        if (!test_tearing(output, &state))
            state.tearing_page_flip = false;
    }
    if (done)
        done = wlr_output_commit_state(output->wlr_output, &state);
    if (!done && state.tearing_page_flip) {
        // Refused late: the frame goes out at the next refresh as any other.
        state.tearing_page_flip = false;
        done = wlr_output_commit_state(output->wlr_output, &state);
    }
    if (done && (state.committed & WLR_OUTPUT_STATE_BUFFER)) {
        ++*(state.tearing_page_flip ? &tearing->flips : &tearing->refused);
        tearing->why = state.tearing_page_flip ? "tearing" : "refused";
        if (!state.tearing_page_flip && !tearing->refusal_logged) {
            wlr_log(WLR_INFO, "%s refuses asynchronous page flips; its frames wait for the refresh",
                    output->wlr_output->name);
            tearing->refusal_logged = true;
        }
    }
    wlr_output_state_finish(&state);
    return true;
}

/* For `get tearing`: a line per output in the layout with what its frames do (`tearing`,
 * `refused` where the backend took no asynchronous flip, or why the window may not tear), the
 * frames flipped at once and refused, and the window's title. */
void describe_tearing(struct sh_server *server, int fd, const char *arguments) {
    control_reply(fd, "ok\n");
    struct sh_output *output;
    wl_list_for_each_reverse(output, &server->outputs, link) {
        struct sh_tearing *tearing = &output->tearing;
        char line[512], name[256];
        copy_field(name, sizeof(name), tearing->title[0] ? tearing->title : "-");
        snprintf(line, sizeof(line), "%s\t%s\t%llu\t%llu\t%s\n", output->wlr_output->name,
                 tearing->why ? tearing->why : "off", (unsigned long long)tearing->flips,
                 (unsigned long long)tearing->refused, name);
        control_reply(fd, line);
    }
}
