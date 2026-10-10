/* SPDX-License-Identifier: GPL-3.0-or-later AND MIT */
/* Effects: dimming of inactive windows, peeking at the desktop or at one window, night light,
 * the magnifier, and hot corners. */
#include "server.h"

#define NIGHT_LIGHT_TICK_MS 10000
#define NIGHT_LIGHT_LUT 1024

/* Asks every output for a frame, so that fades keep advancing while nothing else changes. */
static void schedule_frames(struct sh_server *server) {
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) wlr_output_schedule_frame(output->wlr_output);
}

/* Puts the dimming black at its current opacity, or takes it away once it has faded out. */
static void apply_dim(struct sh_toplevel *toplevel, int64_t now) {
    double value = sh_fade_value(&toplevel->dim_fade, now);
    bool wanted = value > 0.001 || sh_fade_active(&toplevel->dim_fade, now) ||
                  toplevel->dim_fade.to > 0;
    if (!wanted || !toplevel->scene_tree) {
        if (toplevel->dim && toplevel->scene_tree)
            wlr_scene_node_destroy(&toplevel->dim->node);
        toplevel->dim = NULL;
        return;
    }
    struct sh_server *server = toplevel->server;
    if (!toplevel->dim) {
        if (!server->black)
            server->black = sh_black_buffer();
        if (!server->black)
            return;
        toplevel->dim = sh_dim_create(toplevel->content, server->black);
        if (!toplevel->dim)
            return;
    }
    // The scene tree's origin is the top-left corner of the window geometry.
    struct wlr_box g = toplevel_geometry(toplevel);
    wlr_scene_node_set_position(&toplevel->dim->node, 0, 0);
    wlr_scene_buffer_set_dest_size(toplevel->dim, g.width, g.height);
    // Peeking clears the dimming with everything else, and a window shown only for a peek at it
    // is dimmed only as far as it shows.
    value *= 1 - sh_fade_value(&server->peek_fade, now);
    if (shown_for_peek(toplevel))
        value *= sh_fade_value(&toplevel->peek_shown, now);
    wlr_scene_buffer_set_opacity(toplevel->dim, (float)value);
    wlr_scene_node_raise_to_top(&toplevel->dim->node);
}

/* windows.dim_inactive: a window without focus fades toward the configured darkness. */
void update_dim(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    const struct sh_settings *settings = server_settings(server);
    bool dimmed = settings->dim_inactive > 0 && toplevel_mapped(toplevel) &&
                  !toplevel->fullscreen && server->focused_toplevel != toplevel;
    double target = dimmed ? settings->dim_inactive : 0;
    int64_t now = now_ms();
    if (target != toplevel->dim_fade.to) {
        bool fades = settings->animations && toplevel_mapped(toplevel);
        // A window that gains focus just as it maps has barely started to dim: no fade back.
        if (target == 0 && sh_fade_value(&toplevel->dim_fade, now) < 0.01)
            fades = false;
        sh_fade_to(&toplevel->dim_fade, target, now, fades ? settings->dim_duration : 0);
        schedule_frames(server);
    }
    apply_dim(toplevel, now);
}

/* Whether the window is on the screen only for a peek at it: hidden (minimized, or on a
 * workspace its output does not show), but shown while it is peeked at and as it fades back
 * out. It takes no input. */
bool shown_for_peek(struct sh_toplevel *toplevel) {
    return toplevel->peek_lent && !toplevel_visible(toplevel);
}

/* What the peeks make of the window's opacity, as a factor: faded toward peek.opacity while one
 * goes on, and back toward full as far as the window shows through a peek at it. A window shown
 * only for the peek shows only that far. */
float peek_scale(struct sh_toplevel *toplevel, int64_t now) {
    struct sh_server *server = toplevel->server;
    double faded = shown_for_peek(toplevel)
                       ? 0
                       : 1 - sh_fade_value(&server->peek_fade, now) *
                                 (1 - server_settings(server)->effects.peek_opacity);
    double shown = sh_fade_value(&toplevel->peek_shown, now);
    return (float)(faded + (1 - faded) * shown);
}

/* Hides a window a peek showed again, unless it has been shown for good meanwhile. */
static void return_node(struct sh_toplevel *toplevel) {
    if (!toplevel->peek_lent)
        return;
    toplevel->peek_lent = false;
    --toplevel->server->peek_lent;
    if (toplevel->scene_tree)
        wlr_scene_node_set_enabled(&toplevel->scene_tree->node, toplevel_visible(toplevel));
}

/* Advances the fades; true while one is still running. */
bool tick_effects(struct sh_server *server) {
    int64_t now = now_ms();
    bool running = sh_fade_active(&server->peek_fade, now) || sh_fade_active(&server->zoom_fade, now);
    struct sh_toplevel *toplevel;
    double peek = sh_fade_value(&server->peek_fade, now);
    bool faded = peek != server->peek_applied;
    server->peek_applied = peek;
    // Once the others are back, a window that showed in full while they came stops showing
    // through, which changes nothing on the screen; a window shown only for a peek is hidden
    // again once it has faded out.
    bool back = peek == 0 && !sh_fade_active(&server->peek_fade, now);
    wl_list_for_each(toplevel, &server->toplevels, link) {
        bool peeked = toplevel == server->peek_window;
        if (back && !peeked && toplevel->peek_shown.to > 0)
            sh_fade_to(&toplevel->peek_shown, 0, now, 0);
        double shown = sh_fade_value(&toplevel->peek_shown, now);
        running = running || sh_fade_active(&toplevel->peek_shown, now);
        bool refresh = faded || shown != toplevel->peek_shown_applied;
        if (toplevel->peek_lent && !peeked && shown == 0) {
            return_node(toplevel);
            refresh = true; // at its own opacity again for when it is shown
        }
        toplevel->peek_shown_applied = shown;
        if (refresh)
            refresh_frame(toplevel);
    }
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (!toplevel->dim)
            continue;
        running = running || sh_fade_active(&toplevel->dim_fade, now);
        apply_dim(toplevel, now);
    }
    return running;
}

/* The temperature the schedule or an override asks for right now. */
static int night_light_target(struct sh_server *server) {
    const struct sh_effect_settings *fx = &server_settings(server)->effects;
    if (server->night_mode == SH_NIGHT_OFF || (server->night_mode == SH_NIGHT_AUTO && !fx->night_light))
        return SH_KELVIN_NEUTRAL;
    if (server->night_mode == SH_NIGHT_ON)
        return fx->night_kelvin;
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    double minute = server->night_clock >= 0
                        ? server->night_clock
                        : local.tm_hour * 60 + local.tm_min + local.tm_sec / 60.0;
    struct sh_night_schedule schedule = {fx->day_kelvin, fx->night_kelvin, fx->sunrise, fx->sunset,
                                         fx->transition};
    if (fx->sunrise < 0) {
        bool polar_day = false;
        if (!sh_solar_times(fx->latitude, fx->longitude, local.tm_year + 1900, local.tm_mon + 1,
                            local.tm_mday, local.tm_gmtoff / 3600.0, &schedule.sunrise,
                            &schedule.sunset, &polar_day))
            return polar_day ? fx->day_kelvin : fx->night_kelvin;
    }
    return sh_night_kelvin(&schedule, minute);
}

/* Applies the temperature for now to every output, and keeps the clock ticking while the
 * schedule is in charge. */
void night_light_update(struct sh_server *server) {
    const struct sh_effect_settings *fx = &server_settings(server)->effects;
    int kelvin = night_light_target(server);
    if (kelvin != server->night_kelvin) {
        struct wlr_color_transform *transform = NULL;
        if (kelvin != SH_KELVIN_NEUTRAL) {
            // The same three-table form a wlr-gamma-control client would hand over, which
            // outputs turn into their hardware gamma tables.
            uint16_t ramp[3 * NIGHT_LIGHT_LUT];
            sh_gamma_ramp(sh_kelvin_to_rgb(kelvin), NIGHT_LIGHT_LUT, ramp);
            transform = wlr_color_transform_init_lut_3x1d(
                NIGHT_LIGHT_LUT, ramp, ramp + NIGHT_LIGHT_LUT, ramp + 2 * NIGHT_LIGHT_LUT);
        }
        if (transform || kelvin == SH_KELVIN_NEUTRAL) {
            if (server->night_transform)
                wlr_color_transform_unref(server->night_transform);
            server->night_transform = transform;
            server->night_kelvin = kelvin;
            struct sh_output *output;
            wl_list_for_each(output, &server->outputs, link) {
                wlr_output_schedule_frame(output->wlr_output);
            }
        }
    }
    if (server->night_timer)
        wl_event_source_timer_update(
            server->night_timer,
            server->night_mode == SH_NIGHT_AUTO && fx->night_light && server->night_clock < 0
                ? NIGHT_LIGHT_TICK_MS
                : 0);
    // The shell's Quick Settings show whether it is on and who decides; they hear when either
    // changes, not every step of a sunset.
    int announced = (server->night_kelvin < SH_KELVIN_NEUTRAL ? 4 : 0) + server->night_mode;
    if (announced != server->night_announced) {
        server->night_announced = announced;
        notify_subscribers(server);
    }
}

int night_light_tick(void *data) {
    night_light_update(data);
    return 0;
}

/* Hot corners: runs what a corner is bound to once the pointer has rested in it. */
static bool output_has_fullscreen(struct sh_server *server, struct wlr_output *output) {
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel->fullscreen && toplevel_mapped(toplevel) && toplevel_visible(toplevel) &&
            toplevel_output(toplevel) == output)
            return true;
    }
    return false;
}

void hot_corner_check(struct sh_server *server) {
    const struct sh_effect_settings *fx = &server_settings(server)->effects;
    int corner = -1;
    if (fx->corner_mask && !server->locked && server->cursor_mode == SH_CURSOR_PASSTHROUGH &&
        !server->seat->drag) {
        struct wlr_output *output = pointer_output(server);
        struct wlr_box box;
        if (output && !output_has_fullscreen(server, output)) {
            wlr_output_layout_get_box(server->output_layout, output, &box);
            corner = sh_corner_at(server->cursor->x - box.x, server->cursor->y - box.y, box.width,
                                  box.height, fx->corner_size);
            if (corner >= 0 && !(fx->corner_mask & (1U << corner)))
                corner = -1;
        }
    }
    int64_t now = now_ms();
    int fired = sh_corner_dwell_update(&server->corner_dwell, corner, now, fx->corner_delay);
    if (fired >= 0) {
        int argument = 0;
        enum sh_action action = server->callbacks->hot_corner(server->callbacks->userdata, fired,
                                                              &argument);
        if (action != SH_NONE)
            run_action(server, action, argument);
    }
    if (server->corner_timer) {
        int wait = sh_corner_dwell_wait(&server->corner_dwell, now, fx->corner_delay);
        wl_event_source_timer_update(server->corner_timer, wait > 0 ? wait : 0);
    }
}

int hot_corner_tick(void *data) {
    hot_corner_check(data);
    return 0;
}

/* Magnifier. `zoom_by` moves the target a step (or back to 1x for 0 steps) and the level
 * eases there; outputs draw themselves through `output_commit_zoomed` while it is above 1. */
double zoom_level(struct sh_server *server, int64_t now) {
    return sh_fade_value(&server->zoom_fade, now);
}

void zoom_by(struct sh_server *server, int steps) {
    const struct sh_settings *settings = server_settings(server);
    double target = steps == 0 ? 1
                               : sh_zoom_level(server->zoom_target, settings->effects.zoom_step,
                                               steps, settings->effects.zoom_max);
    if (target == server->zoom_target)
        return;
    server->zoom_target = target;
    int64_t now = now_ms();
    sh_fade_to(&server->zoom_fade, target, now,
               settings->animations ? settings->effects.zoom_duration : 0);
    tick_effects(server);
    schedule_frames(server);
}

/* The view follows the pointer, so a frame is due whenever it moves while magnified. */
void zoom_moved(struct sh_server *server) {
    if (server->zoom_target > 1 || server->zoom_fade.to > 1)
        schedule_frames(server);
}

void output_release_zoom(struct sh_output *output) {
    if (output->zoom_source)
        wlr_buffer_unlock(output->zoom_source);
    output->zoom_source = NULL;
    if (output->zoom_swapchain)
        wlr_swapchain_destroy(output->zoom_swapchain);
    output->zoom_swapchain = NULL;
}

/* Draws the scene into a private buffer and puts the part of it around the pointer on the
 * output, enlarged; false (nothing committed) when that cannot be done. */
bool output_commit_zoomed(struct sh_output *output, struct wlr_scene_output *scene_output,
                          const struct wlr_scene_output_state_options *options,
                          double level) {
    struct sh_server *server = output->server;
    struct wlr_output *wlr_output = output->wlr_output;
    struct wlr_box box;
    wlr_output_layout_get_box(server->output_layout, wlr_output, &box);
    if (!wlr_output->enabled || wlr_output->transform != WL_OUTPUT_TRANSFORM_NORMAL ||
        box.width <= 0 || box.height <= 0)
        return false;
    if (output->zoom_swapchain && (output->zoom_swapchain->width != wlr_output->width ||
                                   output->zoom_swapchain->height != wlr_output->height))
        output_release_zoom(output);
    if (!output->zoom_swapchain &&
        !wlr_output_configure_primary_swapchain(wlr_output, NULL, &output->zoom_swapchain))
        return false;
    if (!output->zoomed)
        wlr_damage_ring_add_whole(&scene_output->damage_ring);
    struct wlr_scene_output_state_options scene_options = *options;
    scene_options.swapchain = output->zoom_swapchain;
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    bool done = false;
    struct wlr_texture *texture = NULL;
    if (!wlr_scene_output_build_state(scene_output, &state, &scene_options))
        goto out;
    if (state.committed & WLR_OUTPUT_STATE_BUFFER) {
        if (output->zoom_source)
            wlr_buffer_unlock(output->zoom_source);
        output->zoom_source = wlr_buffer_lock(state.buffer);
    }
    if (!output->zoom_source)
        goto out;
    texture = wlr_texture_from_buffer(server->renderer, output->zoom_source);
    if (!texture)
        goto out;
    struct sh_view view = sh_zoom_view(level, box.width, box.height, server->cursor->x - box.x,
                                       server->cursor->y - box.y);
    double sx = (double)output->zoom_source->width / box.width;
    double sy = (double)output->zoom_source->height / box.height;
    struct wlr_render_pass *pass = wlr_output_begin_render_pass(wlr_output, &state, NULL);
    if (!pass)
        goto out;
    wlr_render_pass_add_texture(
        pass, &(struct wlr_render_texture_options){
                  .texture = texture,
                  .src_box = {view.x * sx, view.y * sy, view.width * sx, view.height * sy},
                  .dst_box = {0, 0, wlr_output->width, wlr_output->height},
                  .filter_mode = WLR_SCALE_FILTER_BILINEAR,
                  .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
              });
    if (!wlr_render_pass_submit(pass))
        goto out;
    // Everything changed as far as the output is concerned, not just what the scene redrew.
    state.committed &= ~WLR_OUTPUT_STATE_DAMAGE;
    done = wlr_output_commit_state(wlr_output, &state);
out:
    if (texture)
        wlr_texture_destroy(texture);
    wlr_output_state_finish(&state);
    if (done)
        output->zoomed = true;
    return done;
}

/* How long the peeks' fades take: peek.duration, or nothing without animations. */
static int peek_duration(struct sh_server *server) {
    const struct sh_settings *settings = server_settings(server);
    return settings->animations ? settings->effects.peek_duration : 0;
}

/* Points the fades where the peeks now want them, over `duration`: the windows faded while a
 * peek at the desktop or at a window goes on, and back otherwise. A window no longer peeked at
 * fades with the others while a peek goes on, and fades out if it was shown only for the peek;
 * otherwise it stays in full until the others are back (tick_effects), so that it does not dip
 * as they come. */
static void aim_peeks(struct sh_server *server, int duration) {
    bool on = server->peeking || server->peek_window;
    int64_t now = now_ms();
    if (server->peek_fade.to != (on ? 1 : 0))
        sh_fade_to(&server->peek_fade, on ? 1 : 0, now, duration);
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (toplevel != server->peek_window && toplevel->peek_shown.to > 0 &&
            (on || shown_for_peek(toplevel)))
            sh_fade_to(&toplevel->peek_shown, 0, now, duration);
    }
    tick_effects(server);
    schedule_frames(server);
}

/* Starts or ends peeking at the desktop, which ends a peek at a window. Windows fade over
 * peek.duration, or at once without animations. */
void set_peek(struct sh_server *server, bool on) {
    if (server->peeking == on)
        return;
    server->peeking = on;
    if (on)
        end_window_peek(server, true);
    if (!on) {
        server->peek_keycode = 0;
        server->peek_keyboard = NULL;
    }
    aim_peeks(server, peek_duration(server));
}

/* Peeking at one window, as the taskbar does while the pointer rests on its picture. The other
 * windows fade with peek_fade, as they do to show the desktop, and the window shows over them in
 * peek_layer, an empty node keeping its place among them until it goes back there; one
 * fullscreen over the panels stays where it is, over the others already. A hidden window
 * (minimized, or on a workspace its output does not show) is lent its node for the peek, where
 * it was, and fades in and out with its peek_shown; tick_effects hides it again once it has
 * faded out. Nothing else about the window changes. */

/* Shows a hidden window for a peek at it. */
static void lend_node(struct sh_toplevel *toplevel) {
    if (toplevel->peek_lent || toplevel_visible(toplevel) || !toplevel->scene_tree)
        return;
    toplevel->peek_lent = true;
    ++toplevel->server->peek_lent;
    wlr_scene_node_set_enabled(&toplevel->scene_tree->node, true);
}

/* Lifts the window peeked at over the others, leaving an empty node in its place. */
static void lift_for_peek(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    struct wlr_scene_node *node = toplevel->scene_tree ? &toplevel->scene_tree->node : NULL;
    if (!node || (!is_window_layer(server, node->parent) && node->parent != server->fullscreen))
        return;
    server->peek_place = wlr_scene_tree_create(node->parent);
    if (!server->peek_place)
        return;
    wlr_scene_node_place_above(&server->peek_place->node, node);
    wlr_scene_node_reparent(node, server->peek_layer);
}

/* Puts the window peeked at back in its place among the others, unless it was moved there on
 * purpose meanwhile: raised as it was focused, or put among them as it left fullscreen. */
static void drop_after_peek(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    struct wlr_scene_tree *place = server->peek_place;
    server->peek_place = NULL;
    if (!place)
        return;
    struct wlr_scene_node *node = toplevel->scene_tree ? &toplevel->scene_tree->node : NULL;
    if (node && node->parent == server->peek_layer) {
        wlr_scene_node_reparent(node, place->node.parent);
        wlr_scene_node_place_below(node, &place->node);
    }
    wlr_scene_node_destroy(&place->node);
}

/* Peeks at the window, or moves the peek over to it from the one peeked at so far, which goes
 * back among the others as they stay faded. */
void peek_at_window(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    if (server->peek_window == toplevel)
        return;
    if (server->peek_window)
        drop_after_peek(server->peek_window);
    server->peek_window = toplevel;
    lend_node(toplevel);
    lift_for_peek(toplevel);
    // A window on the screen while nothing is faded shows in full already.
    int64_t now = now_ms();
    bool full = !shown_for_peek(toplevel) && sh_fade_value(&server->peek_fade, now) == 0;
    sh_fade_to(&toplevel->peek_shown, 1, now, full ? 0 : peek_duration(server));
    aim_peeks(server, peek_duration(server));
}

/* Ends the peek at a window: the others fade back, or come back at once without `fade`, and the
 * window goes back to how it was. */
void end_window_peek(struct sh_server *server, bool fade) {
    struct sh_toplevel *toplevel = server->peek_window;
    if (!toplevel)
        return;
    server->peek_window = NULL;
    server->peek_object = NULL;
    drop_after_peek(toplevel);
    aim_peeks(server, fade ? peek_duration(server) : 0);
}

/* The window is closing: it leaves a peek at once, the others fading back if it was the one
 * peeked at. */
void forget_window_peek(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    if (server->peek_window == toplevel)
        end_window_peek(server, true);
    sh_fade_init(&toplevel->peek_shown, 0);
    toplevel->peek_shown_applied = 0;
    return_node(toplevel);
}
