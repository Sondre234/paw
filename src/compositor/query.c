/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* The control socket's queries (`paw msg get ...`): what the windows, outputs, workspaces
 * and effects are doing, for scripts, the shell and the tests. */
#include "server.h"

/* workspace, focused, minimized, tiled, x, y, width, height, app_id, title, output,
 * visible, scratchpad, sticky, group (0 for none), kept above — one line. A window hidden in
 * the scratchpad is minimized. */
static void control_describe_window(struct sh_server *server, int fd,
                                    struct sh_toplevel *toplevel) {
    char line[1024], app_id[256], title[512];
    const char *raw_app_id = toplevel_app_id(toplevel), *raw_title = toplevel_title(toplevel);
    snprintf(app_id, sizeof(app_id), "%s", raw_app_id ? raw_app_id : "");
    snprintf(title, sizeof(title), "%s", raw_title ? raw_title : "");
    // Neither can break the columns.
    for (char *c = app_id; *c; ++c)
        *c = *c == '\t' || *c == '\n' || *c == '\r' ? ' ' : *c;
    for (char *c = title; *c; ++c)
        *c = *c == '\t' || *c == '\n' || *c == '\r' ? ' ' : *c;
    struct wlr_box geometry = toplevel_geometry(toplevel);
    snprintf(line, sizeof(line), "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s\t%s\t%s\t%d\t%d\t%d\t%u\t%d\n",
             toplevel->workspace + 1, server->focused_toplevel == toplevel, toplevel->minimized,
             toplevel->tiled, toplevel->scene_tree->node.x, toplevel->scene_tree->node.y,
             geometry.width, geometry.height, app_id, title, toplevel->output,
             toplevel_visible(toplevel), toplevel->scratchpad, toplevel->sticky,
             toplevel->group, toplevel->above);
    control_reply(fd, line);
}

static void control_describe_windows(struct sh_server *server, int fd) {
    control_reply(fd, "ok\n");
    struct sh_toplevel *toplevel;
    wl_list_for_each_reverse(toplevel, &server->toplevels, link)
        control_describe_window(server, fd, toplevel);
}

/* The urgent windows in the columns of `get windows`, the one that has waited longest first. */
static void control_describe_urgent(struct sh_server *server, int fd) {
    control_reply(fd, "ok\n");
    unsigned last = 0;
    for (;;) {
        struct sh_toplevel *toplevel, *next = NULL;
        wl_list_for_each(toplevel, &server->toplevels, link) {
            if (toplevel->urgent && toplevel->urgent_order > last &&
                (!next || toplevel->urgent_order < next->urgent_order))
                next = toplevel;
        }
        if (!next)
            return;
        last = next->urgent_order;
        control_describe_window(server, fd, next);
    }
}

static void control_describe_layers(struct sh_server *server, int fd) {
    control_reply(fd, "ok\n");
    struct sh_layer *layer;
    // namespace, output, layer (0 background to 3 overlay), shown, holds the keyboard — one per
    // line.
    wl_list_for_each_reverse(layer, &server->layers, link) {
        struct wlr_layer_surface_v1 *surface = layer->surface;
        char namespace[256], line[512];
        snprintf(namespace, sizeof(namespace), "%s", surface->namespace);
        for (char *c = namespace; *c; ++c)
            if (*c == '\n' || *c == '\r' || *c == '\t')
                *c = ' ';
        snprintf(line, sizeof(line), "%s\t%s\t%d\t%d\t%d\n", namespace,
                 surface->output ? surface->output->name : "", surface->current.layer,
                 surface->surface->mapped && layer->scene->tree->node.enabled,
                 server->focused_layer == layer);
        control_reply(fd, line);
    }
}

static void control_describe_output(struct sh_server *server, int fd, struct sh_output *output) {
    struct wlr_output *o = output->wlr_output;
    struct wlr_box box = {0};
    if (!output->disabled)
        wlr_output_layout_get_box(server->output_layout, o, &box);
    char line[512], description[256];
    output_description(o, description, sizeof(description));
    // name, enabled, x, y, logical width, height, scale, transform, mode, description, power:
    // "on" while it shows a picture, "off" while turned off (output_power.c) or disabled, the
    // output it mirrors (mirror.c), "-" for none, the bits per channel it is drawn in, and
    // whether it is driven in HDR or SDR (hdr.c).
    const struct sh_output *source = mirrored_output(output);
    snprintf(line, sizeof(line), "%s\t%d\t%d\t%d\t%d\t%d\t%g\t%d\t%dx%d@%.3f\t%s\t%s\t%s\t%d\t%s\n",
             o->name, !output->disabled, box.x, box.y, box.width, box.height, o->scale,
             o->transform, o->width, o->height, o->refresh / 1000.0, description,
             (!output->disabled || output->mirror) && !output->powered_off && o->enabled ? "on"
                                                                                         : "off",
             source ? source->wlr_output->name : "-", deep_format(o->render_format) ? 10 : 8,
             output_is_hdr(output) ? "hdr" : "sdr");
    control_reply(fd, line);
}

static void get_outputs(struct sh_server *server, int fd, const char *arguments) {
    control_reply(fd, "ok\n");
    struct sh_output *output;
    wl_list_for_each_reverse(output, &server->outputs, link)
        control_describe_output(server, fd, output);
    wl_list_for_each_reverse(output, &server->disabled_outputs, link)
        control_describe_output(server, fd, output);
}

static void get_tearing(struct sh_server *server, int fd, const char *arguments) {
    describe_tearing(server, fd);
}

static void get_display_mode(struct sh_server *server, int fd, const char *arguments) {
    describe_display_mode(server, fd);
}

static void get_monitors(struct sh_server *server, int fd, const char *arguments) {
    describe_monitors(server, fd);
}

static void get_monitors_trial(struct sh_server *server, int fd, const char *arguments) {
    describe_monitors_trial(server, fd);
}

static void get_workspace(struct sh_server *server, int fd, const char *arguments) {
    char reply[32];
    snprintf(reply, sizeof(reply), "ok\n%d\n", focused_workspace(server));
    control_reply(fd, reply);
}

static void get_workspaces(struct sh_server *server, int fd, const char *arguments) {
    control_reply(fd, "ok\n");
    struct wlr_output *focused = focused_output(server);
    struct sh_output *output;
    // name, current workspace, focused, workspaces with windows, tiling — one line per
    // output.
    wl_list_for_each_reverse(output, &server->outputs, link) {
        char line[256], used[128];
        occupied_workspaces(server, output->wlr_output, used, sizeof(used));
        snprintf(line, sizeof(line), "%s\t%d\t%d\t%s\t%s\n", output->wlr_output->name,
                 *output_workspace(server, output->wlr_output->name) + 1,
                 output->wlr_output == focused, used,
                 output_tiles(server, output->wlr_output) ? "on" : "off");
        control_reply(fd, line);
    }
}

static void get_layout(struct sh_server *server, int fd, const char *arguments) {
    // Layout, master ratio and master count of the focused output's current workspace, or
    // of "get layout OUTPUT [WORKSPACE]" (from 1).
    static const char *const names[] = {"dwindle", "master", "spiral", "monocle", "scroll"};
    struct wlr_output *output = focused_output(server);
    int workspace = output ? *output_workspace(server, output->name) : 0;
    if (arguments) {
        char name[64];
        int number = 0, fields = sscanf(arguments, "%63s %d", name, &number);
        output = fields >= 1 ? find_output(server, name) : NULL;
        if (!output || (fields == 2 && (number < 1 || number > server_settings(server)->workspaces))) {
            control_reply(fd, "error: usage: get layout [OUTPUT [WORKSPACE]]\n");
            return;
        }
        workspace = fields == 2 ? number - 1 : *output_workspace(server, output->name);
    }
    char reply[96] = "ok\ndwindle\t0.55\t1\n";
    if (output) {
        snprintf(reply, sizeof(reply), "ok\n%s\t%.2f\t%d\n",
                 names[sh_tiling_layout(server->tiling, output->name, workspace)],
                 sh_tiling_ratio(server->tiling, output->name, workspace),
                 sh_tiling_master_count(server->tiling, output->name, workspace));
    }
    control_reply(fd, reply);
}

static void get_tiling(struct sh_server *server, int fd, const char *arguments) {
    control_reply(fd, output_tiles(server, focused_output(server)) ? "ok\non\n" : "ok\noff\n");
}

static void get_urgent(struct sh_server *server, int fd, const char *arguments) {
    control_describe_urgent(server, fd);
}

static void get_windows(struct sh_server *server, int fd, const char *arguments) {
    control_describe_windows(server, fd);
}

static void get_stacking(struct sh_server *server, int fd, const char *arguments) {
    describe_stacking(server, fd);
}

static void get_dynamic_rules(struct sh_server *server, int fd, const char *arguments) {
    describe_dynamic_rules(server, fd);
}

static void get_pid_at(struct sh_server *server, int fd, const char *arguments) {
    // The process of the window drawn at layout point X Y, or no line when there is none.
    double x, y;
    if (sscanf(arguments, "%lf %lf", &x, &y) != 2) {
        control_reply(fd, "error: usage: get pid_at X Y\n");
        return;
    }
    struct sh_toplevel *toplevel = toplevel_at(server, x, y);
    pid_t pid = toplevel ? toplevel_pid(toplevel) : 0;
    char reply[32] = "ok\n";
    if (pid > 0)
        snprintf(reply, sizeof(reply), "ok\n%d\n", (int)pid);
    control_reply(fd, reply);
}

static void get_swallow(struct sh_server *server, int fd, const char *arguments) {
    // Per window, oldest first: app_id, whether it is swallowed (hidden), and the app_id of
    // the window it swallowed or was swallowed by ("-" for none).
    control_reply(fd, "ok\n");
    struct sh_toplevel *toplevel;
    wl_list_for_each_reverse(toplevel, &server->toplevels, link) {
        char line[640];
        const char *app_id = toplevel_app_id(toplevel);
        const char *peer = toplevel->swallow_peer ? toplevel_app_id(toplevel->swallow_peer) : NULL;
        snprintf(line, sizeof(line), "%s\t%d\t%d\t%s\n", app_id && *app_id ? app_id : "-",
                 toplevel->swallowed, toplevel_visible(toplevel),
                 toplevel->swallow_peer ? (peer && *peer ? peer : "?") : "-");
        control_reply(fd, line);
    }
}

static void get_guides(struct sh_server *server, int fd, const char *arguments) {
    // The magnet guide lines (vertical, then horizontal): shown, x, y, width, height.
    control_reply(fd, "ok\n");
    for (int i = 0; i < 2; ++i) {
        struct wlr_scene_rect *rect = server->guides[i];
        char line[96];
        snprintf(line, sizeof(line), "%d\t%d\t%d\t%d\t%d\n",
                 rect && rect->node.enabled, rect ? rect->node.x : 0, rect ? rect->node.y : 0,
                 rect ? rect->width : 0, rect ? rect->height : 0);
        control_reply(fd, line);
    }
}

static void get_snap(struct sh_server *server, int fd, const char *arguments) {
    // Snapping by dragging: "zone", the zone the pointer is in while a window is moved (none,
    // left, right, maximize, top_left, ...), the slot a drop there gives (x, y, width, height),
    // and whether a window is being moved with the pointer; "preview", whether the preview is
    // shown, where it is drawn now (x, y, width, height), how far it has faded in (thousandths)
    // and the radius of its corners.
    char line[256];
    struct sh_rect slot = server->snap.slot;
    const float *drawn = server->snap.drawn;
    snprintf(line, sizeof(line),
             "ok\nzone\t%s\t%d\t%d\t%d\t%d\t%d\npreview\t%d\t%ld\t%ld\t%ld\t%ld\t%ld\t%d\n",
             snap_zone_name(server->snap.zone), slot.x, slot.y, slot.width, slot.height,
             server->cursor_mode == SH_CURSOR_MOVE && server->grabbed_toplevel,
             server->snap.preview && server->snap.preview->node.enabled, lround(drawn[0]),
             lround(drawn[1]), lround(drawn[2]), lround(drawn[3]), lround(1000 * drawn[4]),
             server->snap.radius);
    control_reply(fd, line);
}

static void get_animations(struct sh_server *server, int fd, const char *arguments) {
    // Running animations, and the scene trees stacked for windows (with closing copies).
    char reply[64];
    snprintf(reply, sizeof(reply), "ok\n%zu\t%d\t%zu\n", sh_animator_running(server->animator),
             wl_list_length(&server->windows->children) +
                 wl_list_length(&server->floating_windows->children) +
                 wl_list_length(&server->above_windows->children) +
                 wl_list_length(&server->fullscreen->children) +
                 wl_list_length(&server->fullscreen_cover->children),
             sh_animator_tweens(server->animator));
    control_reply(fd, reply);
}

static void get_stats(struct sh_server *server, int fd, const char *arguments) {
    // frames, their time and the worst (ns), window commits and their time (ns), placements.
    const struct sh_stats *stats = &server->stats;
    char reply[480];
    snprintf(reply, sizeof(reply), "ok\n%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\n",
             (unsigned long long)stats->frames, (unsigned long long)stats->frame_ns,
             (unsigned long long)stats->frame_max_ns, (unsigned long long)stats->commits,
             (unsigned long long)stats->commit_ns, (unsigned long long)stats->configures,
             (unsigned long long)stats->opacity_rules, (unsigned long long)stats->motions,
             (unsigned long long)stats->motion_ns, (unsigned long long)stats->reflows,
             (unsigned long long)stats->reflow_ns);
    control_reply(fd, reply);
}

static void get_frame_times(struct sh_server *server, int fd, const char *arguments) {
    // Total frames so far, then the retained ones (oldest first) as "spent\tinterval" in us.
    const struct sh_stats *stats = &server->stats;
    size_t kept = stats->frames < SH_FRAME_RING ? stats->frames : SH_FRAME_RING;
    char *reply = malloc(64 + kept * 24);
    if (!reply) {
        control_reply(fd, "error\nout of memory\n");
        return;
    }
    int used = snprintf(reply, 64, "ok\n%llu\n", (unsigned long long)stats->frames);
    for (size_t i = 0; i < kept; ++i) {
        size_t slot = (stats->frames - kept + i) % SH_FRAME_RING;
        used += snprintf(reply + used, 24, "%u\t%u\n", stats->frame_us[slot], stats->interval_us[slot]);
    }
    control_reply(fd, reply);
    free(reply);
}

static void get_dim(struct sh_server *server, int fd, const char *arguments) {
    // Per window, front to back: focused, how opaque its dimming is now, and where that is
    // heading (both in thousandths), and whether a dimming node exists.
    control_reply(fd, "ok\n");
    int64_t now = now_ms();
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        char line[128];
        snprintf(line, sizeof(line), "%d\t%ld\t%ld\t%d\n", server->focused_toplevel == toplevel,
                 lround(1000 * sh_fade_value(&toplevel->dim_fade, now)),
                 lround(1000 * toplevel->dim_fade.to), toplevel->dim != NULL);
        control_reply(fd, line);
    }
}

static void get_peek(struct sh_server *server, int fd, const char *arguments) {
    // How far windows have faded toward the desktop (thousandths), whether peeking, and
    // whether a held key keeps it up.
    char reply[64];
    snprintf(reply, sizeof(reply), "ok\n%ld\t%d\t%d\n",
             lround(1000 * sh_fade_value(&server->peek_fade, now_ms())), server->peeking,
             server->peek_keycode != 0);
    control_reply(fd, reply);
}

static void get_opacities(struct sh_server *server, int fd, const char *arguments) {
    // app_id, title, focused, opacity applied to its buffers — one window per line.
    control_reply(fd, "ok\n");
    struct sh_toplevel *toplevel;
    wl_list_for_each_reverse(toplevel, &server->toplevels, link) {
        char line[1024], app_id[256], title[512];
        const char *raw_app_id = toplevel_app_id(toplevel), *raw_title = toplevel_title(toplevel);
        snprintf(app_id, sizeof(app_id), "%s", raw_app_id ? raw_app_id : "");
        snprintf(title, sizeof(title), "%s", raw_title ? raw_title : "");
        for (char *c = app_id; *c; ++c)
            *c = *c == '\t' || *c == '\n' || *c == '\r' ? ' ' : *c;
        for (char *c = title; *c; ++c)
            *c = *c == '\t' || *c == '\n' || *c == '\r' ? ' ' : *c;
        snprintf(line, sizeof(line), "%s\t%s\t%d\t%.3f\n", app_id, title,
                 server->focused_toplevel == toplevel, toplevel->opacity);
        control_reply(fd, line);
    }
}

static void get_frames(struct sh_server *server, int fd, const char *arguments) {
    // What the compositor draws for each window, in the order of `get windows`: app_id, title,
    // focused, its controls ("flat", "traffic_lights", or "none" while the window draws its
    // own frame), whether they show, the radius of its rounded corners (0 for square), its
    // shadow: whether it has one, how dark (thousandths of the alpha where it is darkest) and
    // the box it covers from the window's top-left corner, and its border: "none", "color" in
    // rects, or "gradient" in pieces of a gradient.
    control_reply(fd, "ok\n");
    const char *style = deco_style(server) == SH_DECO_TRAFFIC_LIGHTS ? "traffic_lights" : "flat";
    struct sh_toplevel *toplevel;
    wl_list_for_each_reverse(toplevel, &server->toplevels, link) {
        char line[1024], app_id[256], title[512];
        const char *raw_app_id = toplevel_app_id(toplevel), *raw_title = toplevel_title(toplevel);
        snprintf(app_id, sizeof(app_id), "%s", raw_app_id ? raw_app_id : "");
        snprintf(title, sizeof(title), "%s", raw_title ? raw_title : "");
        for (char *c = app_id; *c; ++c)
            *c = *c == '\t' || *c == '\n' || *c == '\r' ? ' ' : *c;
        for (char *c = title; *c; ++c)
            *c = *c == '\t' || *c == '\n' || *c == '\r' ? ' ' : *c;
        bool revealed = toplevel->deco && toplevel->deco->node.enabled;
        struct wlr_box shadow = toplevel->shadow ? toplevel->shadow_box : (struct wlr_box){0};
        snprintf(line, sizeof(line), "%s\t%s\t%d\t%s\t%d\t%d\t%d\t%ld\t%d\t%d\t%d\t%d\t%s\n",
                 app_id, title, server->focused_toplevel == toplevel,
                 toplevel->deco ? style : "none", revealed, toplevel->corner_radius,
                 toplevel->shadow != NULL,
                 toplevel->shadow ? lround(1000 * toplevel->shadow_alpha) : 0, shadow.x, shadow.y,
                 shadow.width, shadow.height,
                 toplevel->gradient ? "gradient" : toplevel->border[0] ? "color" : "none");
        control_reply(fd, line);
    }
}

static void get_zoom(struct sh_server *server, int fd, const char *arguments) {
    // The magnification now and its target (thousandths), and the number of outputs that
    // drew the last frame magnified.
    int zoomed = 0;
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) zoomed += output->zoomed;
    char reply[64];
    snprintf(reply, sizeof(reply), "ok\n%ld\t%ld\t%d\n",
             lround(1000 * zoom_level(server, now_ms())), lround(1000 * server->zoom_target),
             zoomed);
    control_reply(fd, reply);
}

static void get_night_light(struct sh_server *server, int fd, const char *arguments) {
    // The temperature applied now, the override (0 schedule, 1 forced neutral, 2 forced
    // warm), and whether the schedule is enabled.
    char reply[64];
    snprintf(reply, sizeof(reply), "ok\n%d\t%d\t%d\n", server->night_kelvin,
             server->night_mode, server_settings(server)->effects.night_light);
    control_reply(fd, reply);
}

/* The binding mode in use, "default" outside any. */
static void get_mode(struct sh_server *server, int fd, const char *arguments) {
    char reply[64];
    snprintf(reply, sizeof(reply), "ok\n%s\n", binding_mode(server));
    control_reply(fd, reply);
}

static void get_overview(struct sh_server *server, int fd, const char *arguments) {
    // The state (open, closing or closed) and how far the glide has gone (thousandths),
    // then the overview line and a line per thumbnail and per strip cell, as sent to the
    // shell.
    size_t size = 1024 + (size_t)(OVERVIEW_MAX + OVERVIEW_WORKSPACES) * 512;
    char *text = malloc(size);
    if (!text) {
        control_reply(fd, "error: out of memory\n");
        return;
    }
    control_reply(fd, "ok\n");
    snprintf(text, size, "%s %ld\n",
             server->overview.open ? "open" : server->overview.visible ? "closing" : "closed",
             lround(1000 * server->overview.progress));
    control_reply(fd, text);
    if (server->overview.visible) {
        overview_describe(server, text, size);
        control_reply(fd, text);
    }
    free(text);
}

static void get_layers(struct sh_server *server, int fd, const char *arguments) {
    control_describe_layers(server, fd);
}

/* `mask` (xkb modifiers of `keyboard`'s keymap) as sh_modifier bits: Shift 1, Caps Lock 2,
 * Ctrl 4, Alt 8, Num Lock 16, Super 64, ... */
static uint32_t keyboard_modifier_bits(struct wlr_keyboard *keyboard, xkb_mod_mask_t mask) {
    uint32_t bits = 0;
    for (size_t i = 0; keyboard->keymap && i < WLR_MODIFIER_COUNT; ++i) {
        if (keyboard->mod_indexes[i] < 32 && mask & 1u << keyboard->mod_indexes[i])
            bits |= 1u << i;
    }
    return bits;
}

/* Replaces what would break a tab-separated line. */
static void one_field(char *text) {
    for (char *c = text; *c; ++c)
        *c = *c == '\t' || *c == '\n' || *c == '\r' ? ' ' : *c;
}

static void get_keyboard(struct sh_server *server, int fd, const char *arguments) {
    // Where the keymap comes from, "source rules" or "source file PATH"; a line per layout,
    // "layout N ACTIVE SHORT NAME" (N from 1, ACTIVE 1 or 0, SHORT as the panel shows it);
    // and a line per keyboard, "keyboard LAYOUT LAYOUTS VIRTUAL HELD LOCKED NAME": the layout
    // it types in (from 1), how many its keymap has, whether it is virtual (it brings its own
    // keymap), and the modifiers it holds and has locked as sh_modifier bits (Caps Lock 2, Num
    // Lock 16). Tab-separated.
    char line[1280], name[sizeof(server_settings(server)->keyboard_file)];
    control_reply(fd, "ok\n");
    snprintf(name, sizeof(name), "%s", server_settings(server)->keyboard_file);
    one_field(name);
    snprintf(line, sizeof(line), server->keymap_from_file ? "source\tfile\t%s\n" : "source\trules\n",
             name);
    control_reply(fd, line);
    xkb_layout_index_t count = server->keymap ? xkb_keymap_num_layouts(server->keymap) : 0;
    for (xkb_layout_index_t i = 0; i < count; ++i) {
        char code[32];
        layout_short_name(server, i, code, sizeof(code));
        const char *full = xkb_keymap_layout_get_name(server->keymap, i);
        snprintf(name, sizeof(name), "%s", full ? full : "");
        one_field(name);
        snprintf(line, sizeof(line), "layout\t%u\t%d\t%s\t%s\n", i + 1,
                 i == server->keyboard_layout, code, name);
        control_reply(fd, line);
    }
    struct sh_keyboard *keyboard;
    wl_list_for_each_reverse(keyboard, &server->keyboards, link) {
        struct wlr_keyboard *wlr_keyboard = keyboard->wlr_keyboard;
        xkb_layout_index_t layout =
            wlr_keyboard->xkb_state
                ? xkb_state_serialize_layout(wlr_keyboard->xkb_state, XKB_STATE_LAYOUT_LOCKED)
                : wlr_keyboard->modifiers.group;
        snprintf(name, sizeof(name), "%s", wlr_keyboard->base.name ? wlr_keyboard->base.name : "");
        one_field(name);
        snprintf(line, sizeof(line), "keyboard\t%u\t%u\t%d\t%u\t%u\t%s\n", layout + 1,
                 wlr_keyboard->keymap ? xkb_keymap_num_layouts(wlr_keyboard->keymap) : 0,
                 keyboard->is_virtual, wlr_keyboard_get_modifiers(wlr_keyboard),
                 keyboard_modifier_bits(wlr_keyboard, wlr_keyboard->modifiers.locked), name);
        control_reply(fd, line);
    }
}

static void get_power(struct sh_server *server, int fd, const char *arguments) {
    // Each power action and whether it may run (see power_describe), then "pending" and the
    // action under way with its step, or "-".
    control_reply(fd, "ok\n");
    char line[128], pending[64];
    const char *name, *status;
    for (size_t i = 0; power_describe(server, i, &name, &status); ++i) {
        snprintf(line, sizeof(line), "%s\t%s\n", name, status);
        control_reply(fd, line);
    }
    snprintf(line, sizeof(line), "pending\t%s\n", power_pending(server, pending, sizeof(pending)));
    control_reply(fd, line);
}

static void get_idle(struct sh_server *server, int fd, const char *arguments) {
    // "state", the milliseconds since the last input, whether an idle inhibitor (or the session
    // being away from the front) holds the steps off, whether the machine runs on battery, and how
    // dark the screens are dimmed (thousandths); then a line per step (idle.c): its name, its
    // timeout in milliseconds on that power (0 for never), and whether it was taken since the
    // last input.
    const struct sh_idle *idle = &server->idle;
    int64_t now = now_ms();
    bool battery;
    const struct sh_idle_steps *steps = idle_steps(server, &battery);
    char line[128];
    snprintf(line, sizeof(line), "ok\nstate\t%lld\t%d\t%d\t%ld\n",
             (long long)(now - idle->last_input), idle_held(server), battery,
             idle->dim ? lround(1000 * sh_fade_value(&idle->fade, now)) : 0);
    control_reply(fd, line);
    for (int step = 0; step < SH_IDLE_STEPS; ++step) {
        const char *name;
        int timeout = idle_step_timeout(steps, step, &name);
        snprintf(line, sizeof(line), "%s\t%d\t%d\n", name, timeout,
                 (idle->done & (1u << step)) != 0);
        control_reply(fd, line);
    }
}

static void get_switches(struct sh_server *server, int fd, const char *arguments) {
    // "lid closed" or "lid open" (whether any switch says it is closed), then a line per switch
    // device (switches.c): its name, whether it says the lid is closed, and whether it says
    // tablet mode is on.
    control_reply(fd, server->lid_closed ? "ok\nlid\tclosed\n" : "ok\nlid\topen\n");
    struct sh_switch_device *device;
    wl_list_for_each_reverse(device, &server->switches, link) {
        char name[128], line[192];
        snprintf(name, sizeof(name), "%s",
                 device->wlr_switch->base.name ? device->wlr_switch->base.name : "");
        one_field(name);
        snprintf(line, sizeof(line), "%s\t%d\t%d\n", name, device->lid_closed,
                 device->tablet_mode);
        control_reply(fd, line);
    }
}

static void get_pictures(struct sh_server *server, int fd, const char *arguments) {
    // Per capture source a client asked for a window's picture (get_scaled_capture_source),
    // oldest window first: the box asked for, the frame's size (0x0 before the first), how many
    // sessions capture it, and the window's title.
    control_reply(fd, "ok\n");
    struct sh_toplevel *toplevel;
    wl_list_for_each_reverse(toplevel, &server->toplevels, link) {
        struct sh_picture_source sources[16];
        size_t count = list_picture_sources(toplevel, sources, 16);
        count = count < 16 ? count : 16;
        char title[256], line[384];
        snprintf(title, sizeof(title), "%s", toplevel_title(toplevel) ? toplevel_title(toplevel) : "");
        one_field(title);
        for (size_t i = 0; i < count; ++i) {
            snprintf(line, sizeof(line), "%dx%d\t%dx%d\t%zu\t%s\n", sources[i].box_width,
                     sources[i].box_height, sources[i].width, sources[i].height,
                     sources[i].sessions, title);
            control_reply(fd, line);
        }
    }
}

/* Where a window is stacked among the windows, from 0 at the bottom: counting the windows in the
 * trees they are drawn in from the lowest, the one peeked at over the others; -1 for none. */
static int stacked_at(struct sh_server *server, struct sh_toplevel *toplevel) {
    struct wlr_scene_tree *trees[] = {server->windows,       server->floating_windows,
                                      server->above_windows, server->fullscreen,
                                      server->peek_layer,    server->fullscreen_cover};
    int index = 0;
    for (size_t i = 0; i < sizeof(trees) / sizeof(*trees); ++i) {
        struct wlr_scene_node *node;
        wl_list_for_each(node, &trees[i]->children, link) {
            // Closing copies and the node keeping the place of the window peeked at are no
            // window's.
            struct sh_node *owner = node->data;
            if (!owner || owner->kind != SH_NODE_TOPLEVEL)
                continue;
            if (owner->owner == toplevel)
                return index;
            ++index;
        }
    }
    return -1;
}

static void get_window_peek(struct sh_server *server, int fd, const char *arguments) {
    // Per window, in the order of `get windows`: whether it is the one peeked at, whether it is
    // drawn (its node shown), where it is stacked (stacked_at), how far it shows through a peek
    // and the opacity its buffers have (both in thousandths), and its title.
    control_reply(fd, "ok\n");
    int64_t now = now_ms();
    struct sh_toplevel *toplevel;
    wl_list_for_each_reverse(toplevel, &server->toplevels, link) {
        char title[512], line[640];
        snprintf(title, sizeof(title), "%s", toplevel_title(toplevel) ? toplevel_title(toplevel) : "");
        one_field(title);
        snprintf(line, sizeof(line), "%d\t%d\t%d\t%ld\t%ld\t%s\n", server->peek_window == toplevel,
                 toplevel->scene_tree && toplevel->scene_tree->node.enabled,
                 stacked_at(server, toplevel),
                 lround(1000 * sh_fade_value(&toplevel->peek_shown, now)),
                 lround(1000 * toplevel->opacity), title);
        control_reply(fd, line);
    }
}

static void get_window_icons(struct sh_server *server, int fd, const char *arguments) {
    // Per window, in the order of `get windows`: its number (0 before it is published), app_id,
    // and the icon it supplies itself (window_icon.c): its name and the size of its pixels as
    // WIDTHxHEIGHT, each "-" for none.
    control_reply(fd, "ok\n");
    struct sh_toplevel *toplevel;
    wl_list_for_each_reverse(toplevel, &server->toplevels, link) {
        const struct sh_icon *icon = &toplevel->icon;
        const char *raw_app_id = toplevel_app_id(toplevel);
        char app_id[256], name[256], size[32], line[640];
        snprintf(app_id, sizeof(app_id), "%s", raw_app_id ? raw_app_id : "");
        one_field(app_id);
        snprintf(name, sizeof(name), "%s", icon->name ? icon->name : "-");
        one_field(name);
        if (icon->pixels)
            snprintf(size, sizeof(size), "%dx%d", icon->width, icon->height);
        else
            snprintf(size, sizeof(size), "-");
        snprintf(line, sizeof(line), "%u\t%s\t%s\t%s\n", toplevel->id, app_id, name, size);
        control_reply(fd, line);
    }
}

/* `what`, then what `surface` belongs to as "KIND NAME", tab-separated, as one line: "window"
 * and its title, "layer" and its namespace, "other" (a popup, a lock surface) and "-", or "-"
 * and "-" for no surface. */
static void describe_surface(struct sh_server *server, int fd, const char *what,
                             struct wlr_surface *surface) {
    const char *kind = surface ? "other" : "-", *raw = "-";
    struct wlr_surface *root = surface ? wlr_surface_get_root_surface(surface) : NULL;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (root && toplevel_surface(toplevel) == root) {
            kind = "window";
            raw = toplevel_title(toplevel) ? toplevel_title(toplevel) : "";
        }
    }
    struct sh_layer *layer;
    wl_list_for_each(layer, &server->layers, link) {
        if (root && layer->surface->surface == root) {
            kind = "layer";
            raw = layer->surface->namespace;
        }
    }
    char name[256], line[320];
    snprintf(name, sizeof(name), "%s", raw);
    one_field(name);
    snprintf(line, sizeof(line), "%s\t%s\t%s\n", what, kind, name);
    control_reply(fd, line);
}

/* What XDG autostart started or skipped as the session started, and why (main.cpp). */
static void get_autostart(struct sh_server *server, int fd, const char *arguments) {
    const struct sh_callbacks *callbacks = server->callbacks;
    control_reply(fd, "ok\n");
    if (callbacks->autostart)
        control_reply(fd, callbacks->autostart(callbacks->userdata));
}

static void get_gesture(struct sh_server *server, int fd, const char *arguments) {
    // The touchpad swipe under way, and the workspaces following it (see describe_gesture).
    control_reply(fd, "ok\n");
    describe_gesture(server, fd);
}

static void get_touch(struct sh_server *server, int fd, const char *arguments) {
    // The touchscreens and the finger standing in for the pointer (see describe_touch), then
    // each other finger down, `point ID` with the surface it went to as describe_surface has it.
    control_reply(fd, "ok\n");
    describe_touch(server, fd);
    struct wlr_touch_point *point;
    wl_list_for_each(point, &server->seat->touch_state.touch_points, link) {
        char what[32];
        snprintf(what, sizeof(what), "point %d", point->touch_id);
        describe_surface(server, fd, what, point->surface);
    }
}

static void get_tablet(struct sh_server *server, int fd, const char *arguments) {
    // The drawing tablets, their pads and the tools that came near them (see describe_tablets).
    control_reply(fd, "ok\n");
    describe_tablets(server, fd, describe_surface);
}

static void get_shortcuts(struct sh_server *server, int fd, const char *arguments) {
    // Whether the keys go to the focused surface, and the keyboard shortcuts inhibitors (see
    // describe_shortcuts_inhibitors).
    control_reply(fd, "ok\n");
    describe_shortcuts_inhibitors(server, fd, describe_surface);
}

static void get_input_method(struct sh_server *server, int fd, const char *arguments) {
    // The input method, the text inputs and the input method's popups (see
    // describe_input_method).
    control_reply(fd, "ok\n");
    describe_input_method(server, fd, describe_surface);
}

static void get_seat(struct sh_server *server, int fd, const char *arguments) {
    // What has the keyboard, what the pointer is on, and, while a drag is under way, what it is
    // over (each as describe_surface has it). During a drag the pointer is on nothing: its events
    // go to the drag.
    struct wlr_seat *seat = server->seat;
    control_reply(fd, "ok\n");
    describe_surface(server, fd, "keyboard", seat->keyboard_state.focused_surface);
    describe_surface(server, fd, "pointer", seat->pointer_state.focused_surface);
    if (seat->drag)
        describe_surface(server, fd, "drag", seat->drag->focus);
}

/* The queries, "get NAME" (or "get NAME ARGUMENTS" for one that takes them), which answer
 * even while the session is locked. A handler gets NULL for no arguments. */
static const struct {
    const char *name;
    void (*run)(struct sh_server *server, int fd, const char *arguments);
    bool arguments;
} queries[] = {
    {"outputs", get_outputs, false},
    {"display_mode", get_display_mode, false},
    {"monitors", get_monitors, false},
    {"monitors_trial", get_monitors_trial, false},
    {"tearing", get_tearing, false},
    {"workspace", get_workspace, false},
    {"workspaces", get_workspaces, false},
    {"layout", get_layout, true},
    {"tiling", get_tiling, false},
    {"urgent", get_urgent, false},
    {"windows", get_windows, false},
    {"stacking", get_stacking, false},
    {"dynamic_rules", get_dynamic_rules, false},
    {"pid_at", get_pid_at, true},
    {"swallow", get_swallow, false},
    {"guides", get_guides, false},
    {"snap", get_snap, false},
    {"animations", get_animations, false},
    {"stats", get_stats, false},
    {"frame_times", get_frame_times, false},
    {"dim", get_dim, false},
    {"peek", get_peek, false},
    {"window_peek", get_window_peek, false},
    {"window_icons", get_window_icons, false},
    {"opacities", get_opacities, false},
    {"frames", get_frames, false},
    {"zoom", get_zoom, false},
    {"night_light", get_night_light, false},
    {"overview", get_overview, false},
    {"layers", get_layers, false},
    {"keyboard", get_keyboard, false},
    {"power", get_power, false},
    {"idle", get_idle, false},
    {"switches", get_switches, false},
    {"pictures", get_pictures, false},
    {"seat", get_seat, false},
    {"autostart", get_autostart, false},
    {"gesture", get_gesture, false},
    {"touch", get_touch, false},
    {"tablet", get_tablet, false},
    {"mode", get_mode, false},
    {"shortcuts", get_shortcuts, false},
    {"input_method", get_input_method, false},
};

/* Answers `request` if it is a query; false if it is not one. */
bool run_query(struct sh_server *server, int fd, const char *request) {
    if (strncmp(request, "get ", 4))
        return false;
    const char *name = request + 4;
    for (size_t i = 0; i < sizeof(queries) / sizeof(*queries); ++i) {
        size_t length = strlen(queries[i].name);
        if (strncmp(name, queries[i].name, length))
            continue;
        if (!name[length] || (queries[i].arguments && name[length] == ' ')) {
            queries[i].run(server, fd, name[length] ? name + length + 1 : NULL);
            return true;
        }
    }
    return false;
}
