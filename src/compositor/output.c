/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* Outputs: adding, configuring and laying out monitors from outputs.monitors and the
 * wlr-output-management protocol, and committing each frame with zoom and night light. */
#include "server.h"
#include <drm_fourcc.h>

static void output_frame(struct wl_listener *listener, void *data) {
    struct sh_output *output = wl_container_of(listener, output, frame);
    struct wlr_scene *scene = output->server->scene;
    if (output->mirror) { // out of the layout, it has no scene output
        mirror_frame(output);
        return;
    }

    struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(scene, output->wlr_output);
    struct sh_stats *stats = &output->server->stats;
    uint64_t started = now_ns();

    sh_animator_tick(output->server->animator);
    overview_touch(output->server, false); // windows that move or fade move their thumbnails
    if (tick_effects(output->server))
        wlr_output_schedule_frame(output->wlr_output);
    if (tick_idle(output->server)) // the screens dimming
        wlr_output_schedule_frame(output->wlr_output);
    // Night light's colours do not go with an HDR output's, which the scene converts to itself.
    struct wlr_scene_output_state_options night = {
        .color_transform = output_is_hdr(output) ? NULL : output->server->night_transform};
    double level = zoom_level(output->server, now_ms());
    bool zoomed = false;
    struct wlr_output *pointed = pointer_output(output->server);
    if (level > 1.0005 && pointed == output->wlr_output && !output->zoom_failed) {
        zoomed = output_commit_zoomed(output, scene_output, &night, level);
        if (!zoomed) {
            wlr_log(WLR_ERROR, "Cannot magnify %s; showing it at 1x", output->wlr_output->name);
            output->zoom_failed = true;
        }
    }
    if (!zoomed) {
        if (output->zoomed) {
            wlr_damage_ring_add_whole(&scene_output->damage_ring);
            output->zoomed = false;
        }
        if (!(level > 1.0005)) {
            output_release_zoom(output);
            output->zoom_failed = false; // the next zoom tries again
        }
        // A fullscreen game may have its frames shown at once (tearing.c).
        if (!output_commit_tearing(output, scene_output, &night))
            wlr_scene_output_commit(scene_output, &night);
    }
    lock_output_presented(output);
    uint64_t spent = now_ns() - started;
    ++stats->frames;
    stats->frame_ns += spent;
    if (spent > stats->frame_max_ns)
        stats->frame_max_ns = spent;
    size_t slot = (stats->frames - 1) % SH_FRAME_RING;
    stats->frame_us[slot] = (uint32_t)(spent / 1000);
    stats->interval_us[slot] = stats->last_frame_ns ? (uint32_t)((started - stats->last_frame_ns) / 1000) : 0;
    stats->last_frame_ns = started;

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);
}

static void update_backgrounds(struct sh_server *server) {
    const struct sh_settings *settings = server_settings(server);
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        struct wlr_box box;
        wlr_output_layout_get_box(server->output_layout, output->wlr_output, &box);
        wlr_scene_node_set_position(&output->background->node, box.x, box.y);
        wlr_scene_rect_set_size(output->background, box.width, box.height);
        wlr_scene_rect_set_color(output->background, settings->background);
        wlr_scene_node_set_position(&output->lock_blank->node, box.x, box.y);
        wlr_scene_rect_set_size(output->lock_blank, box.width, box.height);
    }
}

bool output_named(const struct sh_output *output, const char *name) {
    return strcmp(output->wlr_output->name, name) == 0;
}

static bool output_listed(const struct sh_settings *settings, const struct sh_output *output) {
    for (int i = 0; i < settings->output_count; ++i) {
        if (output_named(output, settings->output_order[i]))
            return true;
    }
    return false;
}

/* "make model serial", which outputs.monitors can match with a "desc:" prefix. */
void output_description(const struct wlr_output *output, char *text, size_t size) {
    snprintf(text, size, "%s %s %s", output->make ? output->make : "",
             output->model ? output->model : "", output->serial ? output->serial : "");
}

/* A "desc:" key matches the start of the output's description, as Hyprland's does. */
bool output_key_matches(const char *key, const struct wlr_output *output) {
    if (strncmp(key, "desc:", 5) != 0)
        return strcmp(key, output->name) == 0;
    char description[256];
    output_description(output, description, sizeof(description));
    const char *prefix = key + 5;
    return *prefix && strncmp(description, prefix, strlen(prefix)) == 0;
}

/* The output in the layout that `key` names, as output_key_matches has it, or NULL. */
struct wlr_output *find_output_key(struct sh_server *server, const char *key) {
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        if (output_key_matches(key, output->wlr_output))
            return output->wlr_output;
    }
    return NULL;
}

/* Settings by connector name win over a description match. */
const struct sh_monitor *monitor_settings(const struct sh_settings *settings,
                                          const struct wlr_output *output) {
    const struct sh_monitor *described = NULL;
    for (int i = 0; i < settings->monitor_count; ++i) {
        const struct sh_monitor *monitor = &settings->monitors[i];
        if (!output_key_matches(monitor->name, output))
            continue;
        if (strncmp(monitor->name, "desc:", 5) != 0)
            return monitor;
        described = described ? described : monitor;
    }
    return described;
}

/* The settings configured for `output`: its outputs.monitors entry, or what the display settings
 * window kept for this monitor (display_settings.c) laid over it, all but `tiling`, which the
 * window leaves to the configuration. NULL for neither. */
static const struct sh_monitor *configured_monitor(struct sh_output *output) {
    const struct sh_monitor *monitor =
        monitor_settings(server_settings(output->server), output->wlr_output);
    const struct sh_output_saved *saved = saved_output(output);
    if (!saved)
        return monitor;
    output->configured = saved->monitor;
    output->configured.tiling = monitor ? monitor->tiling : -1;
    return &output->configured;
}

/* The monitor settings in force for `output`: what an output-management client or display_mode
 * applied, else what is configured. */
const struct sh_monitor *output_monitor(struct sh_output *output) {
    return output->has_override ? &output->override : configured_monitor(output);
}

/* Tells output-management clients how the outputs are set up now. */
static void publish_output_configuration(struct sh_server *server) {
    if (!server->output_manager)
        return;
    struct wlr_output_configuration_v1 *config = wlr_output_configuration_v1_create();
    if (!config)
        return;
    struct sh_output *output;
    for_each_connected_output(output, server) {
        struct wlr_output_configuration_head_v1 *head =
            wlr_output_configuration_head_v1_create(config, output->wlr_output);
        if (!head)
            continue;
        // A monitor turned off (output_power.c) is still in the layout, as in sway, and a
        // mirror (mirror.c) is on where its source is.
        struct sh_output *placed = mirrored_output(output) ? mirrored_output(output) : output;
        head->state.enabled = (!output->disabled || output->mirror) &&
                              (output->wlr_output->enabled || output->powered_off);
        struct wlr_box box;
        wlr_output_layout_get_box(server->output_layout, placed->wlr_output, &box);
        head->state.x = box.x;
        head->state.y = box.y;
    }
    wlr_output_manager_v1_set_configuration(server->output_manager, config);
}

/* Adds the output to the layout at x, y, or moves it there. */
static void layout_output(struct sh_server *server, struct wlr_output *wlr_output, int x, int y) {
    bool present = wlr_output_layout_get(server->output_layout, wlr_output) != NULL;
    struct wlr_output_layout_output *l_output =
        wlr_output_layout_add(server->output_layout, wlr_output, x, y);
    if (present || l_output == NULL)
        return;
    // Removing an output from the layout also destroys its scene output.
    struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(server->scene, wlr_output);
    if (scene_output == NULL)
        scene_output = wlr_scene_output_create(server->scene, wlr_output);
    wlr_scene_output_layout_add_output(server->scene_layout, l_output, scene_output);
}

/* Moves the windows on `output` along with it, so they stay where they were on its screen.
 * X11 windows are told their new place too. */
static void follow_moved_output(struct sh_server *server, struct sh_output *output) {
    struct wlr_box now;
    wlr_output_layout_get_box(server->output_layout, output->wlr_output, &now);
    int dx = now.x - output->previous.x, dy = now.y - output->previous.y;
    if (wlr_box_empty(&output->previous) || wlr_box_empty(&now) || (dx == 0 && dy == 0))
        return;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel->unmanaged || strcmp(toplevel->output, output->wlr_output->name) != 0)
            continue;
        if (toplevel->restore_box.width > 0) {
            toplevel->restore_box.x += dx;
            toplevel->restore_box.y += dy;
        }
        if (toplevel->fullscreen_restore.width > 0) {
            toplevel->fullscreen_restore.x += dx;
            toplevel->fullscreen_restore.y += dy;
        }
        if (toplevel_mapped(toplevel))
            toplevel_set_position(toplevel, toplevel->scene_tree->node.x + dx,
                                  toplevel->scene_tree->node.y + dy);
    }
}

/* Outputs with a configured position go there. The rest sit side by side, top-aligned, to
 * the right of those: configured order first, then the order they appeared. Everything then
 * shifts so the layout starts at 0, 0: X11 has no place left of or above its root window,
 * so X11 windows on a monitor at negative coordinates would get no input. Windows, and
 * once running the pointer, move with their monitor; the pointer is put on the primary
 * output (if named) when it appears. */
void arrange_outputs(struct sh_server *server) {
    const struct sh_settings *settings = server_settings(server);
    struct sh_output *output;
    int x = 0;
    bool positioned = false;
    wl_list_for_each(output, &server->outputs, link) {
        const struct sh_monitor *monitor = output_monitor(output);
        if (monitor == NULL || !monitor->positioned)
            continue;
        int width, height;
        wlr_output_effective_resolution(output->wlr_output, &width, &height);
        output->x = monitor->x;
        output->y = monitor->y;
        x = positioned && x > monitor->x + width ? x : monitor->x + width;
        positioned = true;
    }
    for (int i = 0; i <= settings->output_count; ++i) {
        wl_list_for_each_reverse(output, &server->outputs, link) {
            const struct sh_monitor *monitor = output_monitor(output);
            if ((monitor != NULL && monitor->positioned) ||
                (i < settings->output_count ? !output_named(output, settings->output_order[i])
                                            : output_listed(settings, output)))
                continue;
            output->x = x;
            output->y = 0;
            int width, height;
            wlr_output_effective_resolution(output->wlr_output, &width, &height);
            x += width;
        }
    }
    int origin_x = INT_MAX, origin_y = INT_MAX;
    wl_list_for_each(output, &server->outputs, link) {
        origin_x = output->x < origin_x ? output->x : origin_x;
        origin_y = output->y < origin_y ? output->y : origin_y;
        wlr_output_layout_get_box(server->output_layout, output->wlr_output, &output->previous);
    }
    struct wlr_output *pointed = pointer_output(server);
    struct wlr_box pointed_before = {0};
    if (pointed)
        wlr_output_layout_get_box(server->output_layout, pointed, &pointed_before);
    wl_list_for_each(output, &server->outputs, link)
        layout_output(server, output->wlr_output, output->x - origin_x, output->y - origin_y);
    wl_list_for_each(output, &server->outputs, link) follow_moved_output(server, output);
    const char *primary_name = primary_output_name(server);
    struct wlr_output *primary = find_output(server, primary_name);
    struct wlr_box box;
    if (primary && strcmp(server->placed_primary, primary_name) != 0) {
        wlr_output_layout_get_box(server->output_layout, primary, &box);
        wlr_cursor_warp(server->cursor, NULL, box.x + box.width / 2.0, box.y + box.height / 2.0);
    } else if (server->running && pointed &&
               wlr_output_layout_get(server->output_layout, pointed)) {
        wlr_output_layout_get_box(server->output_layout, pointed, &box);
        wlr_cursor_warp(server->cursor, NULL, server->cursor->x + box.x - pointed_before.x,
                        server->cursor->y + box.y - pointed_before.y);
    }
    snprintf(server->placed_primary, sizeof(server->placed_primary), "%s",
             primary ? primary_name : "");
    update_backgrounds(server);
    arrange_layers(server);
    refit_fullscreen(server);
    map_touchscreens(server);
    map_tablets(server);
    publish_output_configuration(server);
    notify_subscribers(server); // the list of outputs and their workspaces
    // A mirror whose source came or went joins or leaves the layout, laid out again then.
    if (refresh_mirrors(server))
        arrange_outputs(server);
}

/* The mode for `monitor`: its resolution at the refresh closest to the one asked for, or the
 * fastest there. Without settings, the preferred resolution at its fastest refresh, since
 * monitors often mark a 60 Hz mode as preferred. NULL when nothing matches. */
static struct wlr_output_mode *pick_mode(struct wlr_output *wlr_output,
                                         const struct sh_monitor *monitor) {
    struct wlr_output_mode *preferred = wlr_output_preferred_mode(wlr_output);
    int width = monitor && monitor->width ? monitor->width : preferred ? preferred->width : 0;
    int height = monitor && monitor->width ? monitor->height : preferred ? preferred->height : 0;
    int refresh = monitor ? monitor->refresh : 0;
    struct wlr_output_mode *best = NULL, *candidate;
    wl_list_for_each(candidate, &wlr_output->modes, link) {
        if (candidate->width != width || candidate->height != height)
            continue;
        if (best == NULL ||
            (refresh ? abs(candidate->refresh - refresh) < abs(best->refresh - refresh)
                     : candidate->refresh > best->refresh))
            best = candidate;
    }
    return best;
}

bool deep_format(uint32_t format) {
    return format == DRM_FORMAT_XRGB2101010 || format == DRM_FORMAT_XBGR2101010;
}

/* Whether the environment variable `variable` names `output` among the outputs it lists,
 * separated by commas, under --headless: how tests make an output act as some hardware does. */
bool test_names_output(struct sh_output *output, const char *variable) {
    const char *names = getenv(variable);
    if (!names || !headless_backend(output->server))
        return false;
    const char *name = output->wlr_output->name;
    size_t length = strlen(name);
    for (const char *at = strstr(names, name); at; at = strstr(at + 1, name)) {
        if ((at == names || at[-1] == ',') && (at[length] == ',' || at[length] == '\0'))
            return true;
    }
    return false;
}

/* wlr_output_test_state, but for tests under --headless, whose outputs take any format: the
 * outputs PAW_TEST_REFUSE_10BIT names refuse 10 bits, as a monitor or a renderer without them
 * does. */
static bool test_render_format(struct sh_output *output, const struct wlr_output_state *state) {
    if (deep_format(state->render_format) && test_names_output(output, "PAW_TEST_REFUSE_10BIT"))
        return false;
    return wlr_output_test_state(output->wlr_output, state);
}

/* Adds a 10-bit render format to `state`: the first of XRGB2101010 and XBGR2101010 (which the
 * GLES renderer may have alone) that passes the test with the rest of it. Without one the output
 * stays at the format it has, and says why. */
static void try_deep_format(struct sh_output *output, struct wlr_output_state *state) {
    static const uint32_t formats[] = {DRM_FORMAT_XRGB2101010, DRM_FORMAT_XBGR2101010};
    for (size_t i = 0; i < sizeof(formats) / sizeof(*formats); ++i) {
        wlr_output_state_set_render_format(state, formats[i]);
        if (test_render_format(output, state))
            return;
    }
    state->committed &= ~WLR_OUTPUT_STATE_RENDER_FORMAT;
    if (state->committed == 0 || wlr_output_test_state(output->wlr_output, state))
        wlr_log(WLR_ERROR, "%s cannot be drawn in 10 bits; it stays at 8",
                output->wlr_output->name);
}

/* Under --headless, whether `state` asks for the size the variable `name` names ("1600x900"),
 * which a test has outputs refuse: PAW_TEST_REFUSE_MODE in the display settings' test
 * (test_monitor), PAW_TEST_REFUSE_COMMIT in configure_output's own, as a monitor that passed
 * the one and fails as its settings are committed. */
static bool refused_size(struct sh_output *output, const struct wlr_output_state *state,
                         const char *name) {
    const char *refused = getenv(name);
    int width, height;
    return refused && headless_backend(output->server) &&
           (state->committed & WLR_OUTPUT_STATE_MODE) &&
           state->mode_type == WLR_OUTPUT_STATE_MODE_CUSTOM &&
           sscanf(refused, "%dx%d", &width, &height) == 2 &&
           state->custom_mode.width == width && state->custom_mode.height == height;
}

/* Whether `output` takes `monitor`'s mode, scale, transform and adaptive sync (the defaults for
 * NULL) or being turned off, as configure_output commits them: the backend's test, as
 * wlr-output-management's test asks it of each head. */
bool test_monitor(struct sh_output *output, const struct sh_monitor *monitor) {
    struct wlr_output *wlr_output = output->wlr_output;
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    bool enable = !monitor || monitor->enabled;
    wlr_output_state_set_enabled(&state, enable);
    if (enable) {
        struct wlr_output_mode *mode = pick_mode(wlr_output, monitor);
        if (mode)
            wlr_output_state_set_mode(&state, mode);
        else if (monitor && monitor->width > 0 && wl_list_empty(&wlr_output->modes))
            wlr_output_state_set_custom_mode(&state, monitor->width, monitor->height,
                                             monitor->refresh);
        wlr_output_state_set_scale(&state, monitor && monitor->scale > 0 ? monitor->scale : 1);
        wlr_output_state_set_transform(&state, monitor ? monitor->transform : 0);
        if (wlr_output->adaptive_sync_supported)
            wlr_output_state_set_adaptive_sync_enabled(&state, monitor && monitor->vrr);
    }
    bool ok = !refused_size(output, &state, "PAW_TEST_REFUSE_MODE") &&
              wlr_output_test_state(wlr_output, &state);
    wlr_output_state_finish(&state);
    return ok;
}

static void destroy_output_layers(struct sh_server *server, struct wlr_output *wlr_output) {
    struct sh_layer *layer, *temporary;
    wl_list_for_each_safe(layer, temporary, &server->layers, link) {
        if (layer->surface->output == wlr_output)
            wlr_layer_surface_v1_destroy(layer->surface);
    }
}

/* Applies outputs.monitors (or the defaults) to one output and files it under the enabled or
 * disabled list. Only settings that differ are committed, so a reload does not modeset
 * needlessly. The last enabled output stays on. Callers arrange the outputs afterwards. */
void configure_output(struct sh_server *server, struct sh_output *output) {
    struct wlr_output *wlr_output = output->wlr_output;
    const struct sh_monitor *monitor = output_monitor(output);
    // A laptop's panel with its lid closed stays dark while another monitor shows the desktop.
    bool by_lid = lid_holds_off(server, output);
    bool enable = (monitor == NULL || monitor->enabled) && !by_lid;
    output->kept_on = false;
    if (!enable) {
        bool others = false;
        struct sh_output *candidate;
        wl_list_for_each(candidate, &server->outputs, link) others |= candidate != output;
        if (!others) {
            wlr_log(WLR_ERROR, "Keeping %s on: it is the only output", wlr_output->name);
            enable = output->kept_on = true;
        }
    }
    // A monitor that is off stays off as it is until it is turned on, which configures it.
    if (enable && output->powered_off)
        return;
    // One that mirrors another in the layout leaves the layout, showing that one (mirror.c).
    struct sh_output *source = enable ? mirror_source(server, output) : NULL;
    bool in_layout = enable && !source;

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    if (!enable) {
        wlr_output_state_set_enabled(&state, false);
    } else {
        if (!wlr_output->enabled)
            wlr_output_state_set_enabled(&state, true);
        struct wlr_output_mode *mode = pick_mode(wlr_output, monitor);
        if (mode != NULL && mode != wlr_output->current_mode) {
            wlr_output_state_set_mode(&state, mode);
        } else if (mode == NULL && monitor != NULL && monitor->width != 0) {
            if (wl_list_empty(&wlr_output->modes))
                wlr_output_state_set_custom_mode(&state, monitor->width, monitor->height,
                                                 monitor->refresh);
            else
                wlr_log(WLR_ERROR, "%s has no %dx%d mode", wlr_output->name, monitor->width,
                        monitor->height);
        }
        float scale = monitor && monitor->scale > 0 ? monitor->scale : 1;
        if (scale != wlr_output->scale)
            wlr_output_state_set_scale(&state, scale);
        enum wl_output_transform transform = monitor ? monitor->transform : 0;
        if (transform != wlr_output->transform)
            wlr_output_state_set_transform(&state, transform);
        // Outputs that cannot switch it (nested ones report it on) are left alone.
        bool vrr = monitor && monitor->vrr;
        if (wlr_output->adaptive_sync_supported &&
            vrr != (wlr_output->adaptive_sync_status == WLR_OUTPUT_ADAPTIVE_SYNC_ENABLED))
            wlr_output_state_set_adaptive_sync_enabled(&state, vrr);
        // HDR where asked for and possible (hdr.c), which wants 10 bits too.
        bool hdr = output_want_hdr(output, monitor, source != NULL, &state);
        if (hdr && !wlr_output_test_state(wlr_output, &state)) {
            output_drop_hdr(output, &state);
            hdr = false;
        }
        // 10 bits per channel where asked for and taken; else the default 8.
        bool deep = monitor && (monitor->bit_depth == 10 || hdr);
        if (deep && !deep_format(wlr_output->render_format))
            try_deep_format(output, &state);
        else if (!deep && wlr_output->render_format != DRM_FORMAT_XRGB8888)
            wlr_output_state_set_render_format(&state, DRM_FORMAT_XRGB8888);
    }
    if (state.committed != 0 && (refused_size(output, &state, "PAW_TEST_REFUSE_COMMIT") ||
                                 !wlr_output_test_state(wlr_output, &state))) {
        // Fall back to the defaults; a new monitor must still light up.
        wlr_log(WLR_ERROR, "%s rejected its configured settings", wlr_output->name);
        wlr_output_state_finish(&state);
        wlr_output_state_init(&state);
        wlr_output_state_set_enabled(&state, true);
        struct wlr_output_mode *mode = pick_mode(wlr_output, NULL);
        if (mode != NULL)
            wlr_output_state_set_mode(&state, mode);
        wlr_output_state_set_scale(&state, 1);
        wlr_output_state_set_transform(&state, WL_OUTPUT_TRANSFORM_NORMAL);
        if (!wlr_output_test_state(wlr_output, &state))
            wlr_output_state_set_mode(&state, wlr_output_preferred_mode(wlr_output));
        enable = true;
    }
    if (state.committed != 0)
        wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    // The windows of an output that is turned off go to another, as if it were unplugged.
    bool turned_off = !in_layout && !wl_list_empty(&output->link) && !output->disabled;
    struct wlr_box gone = {0};
    if (turned_off) {
        gone = output->usable;
        if (wlr_box_empty(&gone))
            wlr_output_layout_get_box(server->output_layout, wlr_output, &gone);
    }
    if (wl_list_empty(&output->link) || in_layout == output->disabled) {
        wl_list_remove(&output->link);
        wl_list_insert(in_layout ? &server->outputs : &server->disabled_outputs, &output->link);
        output->disabled = !in_layout;
    }
    if (source)
        mirror_start(output, source);
    else
        mirror_stop(output);
    if (in_layout) {
        wlr_scene_node_set_enabled(&output->background->node, true);
        wlr_scene_node_set_enabled(&output->lock_blank->node, true);
    } else {
        output->powered_off = false; // out of the layout, it is simply off
        destroy_output_layers(server, wlr_output);
        wlr_output_layout_remove(server->output_layout, wlr_output);
        wlr_scene_node_set_enabled(&output->background->node, false);
        wlr_scene_node_set_enabled(&output->lock_blank->node, false);
        // The lid or a mirror: as if unplugged, so that the windows come back.
        if (turned_off && server->running)
            evacuate_output(server, wlr_output->name, gone, by_lid || source);
    }
}

/* The connected output after `output` (the first for NULL): those in the layout, then the
 * others; NULL after the last. for_each_connected_output walks them. */
struct sh_output *next_connected(struct sh_server *server, struct sh_output *output) {
    struct wl_list *link = output ? output->link.next : server->outputs.next;
    if (link == &server->outputs)
        link = server->disabled_outputs.next;
    return link == &server->disabled_outputs ? NULL : wl_container_of(link, output, link);
}

struct sh_output *sh_output_for(struct sh_server *server, struct wlr_output *wlr_output) {
    struct sh_output *output;
    for_each_connected_output(output, server) {
        if (output->wlr_output == wlr_output)
            return output;
    }
    return NULL;
}

/* Settings a wlr-output-management head asks for, as the monitor entry they amount to. */
static void head_monitor(struct sh_server *server, struct sh_output *output,
                         const struct wlr_output_head_v1_state *head, struct sh_monitor *monitor) {
    const struct sh_monitor *configured = configured_monitor(output);
    memset(monitor, 0, sizeof(*monitor));
    monitor->tiling = configured ? configured->tiling : -1;
    monitor->bit_depth = configured ? configured->bit_depth : 8; // no head state says it
    monitor->hdr = configured && configured->hdr;
    snprintf(monitor->name, sizeof(monitor->name), "%s", output->wlr_output->name);
    monitor->enabled = head->enabled;
    if (!head->enabled)
        return;
    if (head->mode) {
        monitor->width = head->mode->width;
        monitor->height = head->mode->height;
        monitor->refresh = head->mode->refresh;
    } else {
        monitor->width = head->custom_mode.width;
        monitor->height = head->custom_mode.height;
        monitor->refresh = head->custom_mode.refresh;
    }
    monitor->scale = head->scale;
    monitor->transform = head->transform;
    monitor->vrr = head->adaptive_sync_enabled;
    monitor->positioned = true;
    monitor->x = head->x;
    monitor->y = head->y;
    // A mirror left where it was said to be, on its source, goes on mirroring it.
    const struct sh_output *source = mirrored_output(output);
    struct wlr_box box;
    if (source) {
        wlr_output_layout_get_box(server->output_layout, source->wlr_output, &box);
        if (head->x == box.x && head->y == box.y)
            snprintf(monitor->mirror, sizeof(monitor->mirror), "%s", source->wlr_output->name);
    }
}

static bool test_head(const struct wlr_output_head_v1_state *head) {
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, head->enabled);
    if (head->enabled) {
        if (head->mode)
            wlr_output_state_set_mode(&state, head->mode);
        else if (head->custom_mode.width > 0 && head->custom_mode.height > 0)
            wlr_output_state_set_custom_mode(&state, head->custom_mode.width,
                                             head->custom_mode.height, head->custom_mode.refresh);
        wlr_output_state_set_scale(&state, head->scale);
        wlr_output_state_set_transform(&state, head->transform);
    }
    bool ok = head->scale >= 0 && wlr_output_test_state(head->output, &state);
    wlr_output_state_finish(&state);
    return ok;
}

/* A request is acceptable when every head is known and its settings pass the backend's test,
 * and an output stays on. */
static bool output_configuration_ok(struct sh_server *server,
                                    struct wlr_output_configuration_v1 *config) {
    bool any_on = false;
    struct wlr_output_configuration_head_v1 *head;
    wl_list_for_each(head, &config->heads, link) {
        if (!sh_output_for(server, head->state.output) || !test_head(&head->state))
            return false;
        any_on |= head->state.enabled;
    }
    // Outputs the request leaves out keep their state.
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        bool listed = false;
        wl_list_for_each(head, &config->heads, link) listed |= head->state.output == output->wlr_output;
        any_on |= !listed;
    }
    return any_on;
}

void output_config_test(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, output_test);
    struct wlr_output_configuration_v1 *config = data;
    if (output_configuration_ok(server, config))
        wlr_output_configuration_v1_send_succeeded(config);
    else
        wlr_output_configuration_v1_send_failed(config);
    wlr_output_configuration_v1_destroy(config);
}

void output_config_apply(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, output_apply);
    struct wlr_output_configuration_v1 *config = data;
    if (!output_configuration_ok(server, config)) {
        wlr_output_configuration_v1_send_failed(config);
        wlr_output_configuration_v1_destroy(config);
        return;
    }
    struct wlr_output_configuration_head_v1 *head;
    wl_list_for_each(head, &config->heads, link) {
        struct sh_output *output = sh_output_for(server, head->state.output);
        head_monitor(server, output, &head->state, &output->override);
        output->has_override = true;
    }
    apply_output_settings(server);
    wlr_output_configuration_v1_send_succeeded(config);
    wlr_output_configuration_v1_destroy(config);
}

/* Configures every output again after their settings changed while running (through
 * wlr-output-management, or display_mode.c), and puts windows, workspaces and focus in order. */
void apply_output_settings(struct sh_server *server) {
    // Enable outputs before disabling others, so a swap never leaves none on.
    struct sh_output *output, *temporary;
    wl_list_for_each_safe(output, temporary, &server->disabled_outputs, link)
        configure_output(server, output);
    wl_list_for_each_safe(output, temporary, &server->outputs, link)
        configure_output(server, output);
    arrange_outputs(server);
    reconfigure_tiling(server);
    return_home_windows(server);
    rehome_tiles(server);
    show_workspaces(server);
    struct sh_toplevel *toplevel;
    refocus_if_hidden(server);
    wl_list_for_each(toplevel, &server->toplevels, link) refresh_frame(toplevel);
    wl_list_for_each(output, &server->outputs, link) reflow_output(server, output->wlr_output);
}

static void output_request_state(struct wl_listener *listener, void *data) {
    struct sh_output *output = wl_container_of(listener, output, request_state);
    const struct wlr_output_event_request_state *event = data;
    if (wlr_output_commit_state(output->wlr_output, event->state))
        arrange_outputs(output->server);
}

static void output_destroy(struct wl_listener *listener, void *data) {
    struct sh_output *output = wl_container_of(listener, output, destroy);

    wl_list_remove(&output->frame.link);
    wl_list_remove(&output->request_state.link);
    wl_list_remove(&output->destroy.link);
    wl_list_remove(&output->link);
    mirror_stop(output);
    mirrors_forget_source(output);
    output_release_zoom(output);
    destroy_output_layers(output->server, output->wlr_output);
    wlr_scene_node_destroy(&output->background->node);
    wlr_scene_node_destroy(&output->lock_blank->node);
    struct sh_server *server = output->server;
    if (!strcmp(server->overview.output, output->wlr_output->name))
        overview_dismiss(server);
    if (server->running)
        schedule_evacuation(server, output);
    // A laptop's panel the lid held off comes back when the last other monitor goes.
    if (server->running)
        apply_lid(server);
    // Closing the host window ends a nested session. A standalone session loses every output
    // on VT switch (wlroots recreates them on return) or when the last monitor is unplugged.
    bool standalone = false;
#if WLR_HAS_SESSION
    standalone = server->session != NULL;
#endif
    if (server->running && !standalone && wl_list_empty(&server->outputs))
        wl_display_terminate(server->wl_display);
    free(output);
    if (server->running)
        arrange_outputs(server);
    send_locked_if_presented(server);
}

void server_new_output(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, new_output);
    struct wlr_output *wlr_output = data;

    wlr_output_init_render(wlr_output, server->allocator, server->renderer);

    struct sh_output *output = calloc(1, sizeof(*output));
    output->wlr_output = wlr_output;
    output->server = server;
    output->background =
        wlr_scene_rect_create(server->backgrounds, 1, 1, server_settings(server)->background);
    static const float lock_color[4] = {0, 0, 0, 1};
    // lock_presented starts false: an output added while locked must not show the desktop.
    output->lock_blank = wlr_scene_rect_create(server->lock_blanks, 1, 1, lock_color);
    add_listener(&wlr_output->events.frame, &output->frame, output_frame);
    add_listener(&wlr_output->events.request_state, &output->request_state, output_request_state);
    add_listener(&wlr_output->events.destroy, &output->destroy, output_destroy);

    if (server->pending_output_name[0]) {
        wlr_output_set_name(wlr_output, server->pending_output_name);
        server->pending_output_name[0] = '\0';
    }
    wl_list_init(&output->link);
    char description[256];
    output_description(wlr_output, description, sizeof(description));
    wlr_log(WLR_INFO, "Output %s: %s", wlr_output->name, description);
    apply_output_layout(server, wlr_output);
    configure_output(server, output);
    // One kept on only as the first to appear, such as one turned off at startup, goes off now.
    struct sh_output *other, *temporary;
    wl_list_for_each_safe(other, temporary, &server->outputs, link) {
        if (other != output && other->kept_on)
            configure_output(server, other);
    }
    if (wlr_output_is_wl(wlr_output))
        wlr_wl_output_set_title(wlr_output, "paw — nested desktop");
    arrange_outputs(server);
    return_home_windows(server);
    apply_lid(server); // a monitor joining a laptop with its lid closed turns the panel off
}

struct wlr_output *find_output(struct sh_server *server, const char *name) {
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        if (output_named(output, name))
            return output->wlr_output;
    }
    return NULL;
}

struct wlr_output *first_output(struct sh_server *server) {
    if (wl_list_empty(&server->outputs))
        return NULL;
    struct sh_output *first = wl_container_of(server->outputs.next, first, link);
    return first->wlr_output;
}

struct sh_rect usable_area(struct sh_server *server, struct wlr_output *output) {
    struct wlr_box box;
    wlr_output_layout_get_box(server->output_layout, output, &box);
    struct sh_output *candidate;
    wl_list_for_each(candidate, &server->outputs, link) {
        if (candidate->wlr_output == output)
            box = candidate->usable;
    }
    return (struct sh_rect){box.x, box.y, box.width, box.height};
}
