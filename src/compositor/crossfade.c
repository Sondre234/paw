/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Crossfading to a new appearance profile: a picture of what each monitor showed as the profile
 * changed lies over everything while the compositor and the shell take up the new look, until the
 * shell says it has drawn it (or a moment has passed), then fades out to show it. */
#include "server.h"

/* How long the picture takes to fade before animations.speed, and how long it waits at most for
 * the shell to draw the new look; under --headless, PAW_TEST_CROSSFADE_HOLD_MS sets that for
 * tests. */
#define CROSSFADE_MS 400
#define HOLD_MS 1000

static int hold_ms(struct sh_server *server) {
    const char *test = getenv("PAW_TEST_CROSSFADE_HOLD_MS");
    int ms = test && headless_backend(server) ? atoi(test) : 0;
    return ms > 0 ? ms : HOLD_MS;
}

/* The picture's opacity while it covers the screen: just below 1, which looks the same, so that
 * the scene does not take it for opaque. Over an opaque node the scene finds the surfaces under
 * it hidden and sends them no frame callbacks, and the shell could not draw the new look. */
#define COVER 0.9999f

/* A monitor's picture over its part of the scene, and the output's place, size and transform as
 * it was taken: it goes as soon as one of them changes. */
struct sh_crossfade_picture {
    struct wlr_scene_buffer *node;
    struct wlr_box box;
    int width, height;
    enum wl_output_transform transform;
};

struct sh_crossfade {
    struct wlr_scene_tree *tree; // the pictures, under the drag icons and the lock
    /* Waiting for the shell to draw `profile` (`profile_drawn`), or for the timer. */
    bool holding;
    char profile[sizeof(((struct sh_settings *)0)->profile)];
    struct wl_event_source *timer;
    int64_t start; // when the fade began, in milliseconds; 0 until its first frame
    int duration;
    float opacity; // the pictures' opacity now
};

static bool no_input(struct wlr_scene_buffer *buffer, double *sx, double *sy) {
    return false;
}

/* Whether `output` shows a fullscreen window and nothing of the desktop over it: nothing there
 * changes with the profile, and a picture would only stop a video or a game for a moment. */
static bool shows_fullscreen(struct sh_output *output) {
    struct sh_server *server = output->server;
    if (server->overview.visible || server->switcher.open)
        return false;
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) {
        if (!toplevel->unmanaged && toplevel->fullscreen && toplevel_mapped(toplevel) &&
            toplevel_visible(toplevel) && output_named(output, toplevel->output))
            return true;
    }
    return false;
}

/* Draws what the scene shows on `output` into a buffer of its own, as its next frame would be
 * drawn, but for what goes on over the picture once it lies in the scene: night light and the
 * magnifier, which the output's frames apply to the scene, and what follows the pointer (drag
 * icons, which are over the picture, and a software cursor). Always drawn, never a client's
 * buffer scanned out. NULL when it cannot be. */
static struct wlr_buffer *take_picture(struct sh_output *output) {
    struct sh_server *server = output->server;
    struct wlr_output *wlr_output = output->wlr_output;
    struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(server->scene, wlr_output);
    struct wlr_swapchain *swapchain = NULL;
    if (!scene_output || !wlr_output_configure_primary_swapchain(wlr_output, NULL, &swapchain))
        return NULL;
    // Hidden for this drawing only, as hit testing hides nodes: no damage, nothing redrawn.
    bool drag_icons = server->drag_icons->node.enabled;
    server->drag_icons->node.enabled = false;
    struct wlr_output_cursor *cursor, *hidden[8];
    size_t hidden_count = 0;
    wl_list_for_each(cursor, &wlr_output->cursors, link) {
        if (cursor->enabled && cursor != wlr_output->hardware_cursor && hidden_count < 8) {
            cursor->enabled = false;
            hidden[hidden_count++] = cursor;
        }
    }
    wlr_output_lock_attach_render(wlr_output, true);
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    struct wlr_buffer *picture = NULL;
    if (wlr_scene_output_build_state(scene_output, &state,
                                     &(struct wlr_scene_output_state_options){
                                         .swapchain = swapchain}) &&
        (state.committed & WLR_OUTPUT_STATE_BUFFER) && state.buffer)
        picture = wlr_buffer_lock(state.buffer);
    wlr_output_state_finish(&state);
    wlr_output_lock_attach_render(wlr_output, false);
    while (hidden_count > 0)
        hidden[--hidden_count]->enabled = true;
    server->drag_icons->node.enabled = drag_icons;
    // The buffer outlives its swapchain while the picture shows.
    wlr_swapchain_destroy(swapchain);
    return picture;
}

static void drop_picture(struct sh_output *output) {
    struct sh_crossfade_picture *picture = output->crossfade;
    if (!picture)
        return;
    wlr_scene_node_destroy(&picture->node->node);
    free(picture);
    output->crossfade = NULL;
}

static bool any_picture(struct sh_server *server) {
    struct sh_output *output;
    for_each_connected_output(output, server) {
        if (output->crossfade)
            return true;
    }
    return false;
}

/* Lays a picture of what `output` shows now over it, or a new one in place of the one there (the
 * scene's picture holds that one as it shows). An output that is off or shows a fullscreen window
 * gets none. */
static void lay_picture(struct sh_crossfade *fade, struct sh_output *output) {
    struct sh_server *server = output->server;
    struct wlr_output *wlr_output = output->wlr_output;
    struct wlr_box box;
    wlr_output_layout_get_box(server->output_layout, wlr_output, &box);
    struct wlr_buffer *buffer = NULL;
    if (wlr_output->enabled && !wlr_box_empty(&box) && !shows_fullscreen(output))
        buffer = take_picture(output);
    if (!buffer) {
        drop_picture(output);
        return;
    }
    struct sh_crossfade_picture *picture = output->crossfade;
    if (!picture) {
        picture = calloc(1, sizeof(*picture));
        if (picture)
            picture->node = wlr_scene_buffer_create(fade->tree, NULL);
        if (!picture || !picture->node) {
            free(picture);
            wlr_buffer_unlock(buffer);
            return;
        }
        picture->node->point_accepts_input = no_input;
        output->crossfade = picture;
    }
    wlr_scene_buffer_set_buffer(picture->node, buffer);
    wlr_buffer_unlock(buffer);
    // The buffer is the output's own frame: turned as the output is, in its pixels, which fill its
    // box in the layout.
    wlr_scene_buffer_set_transform(picture->node, wlr_output->transform);
    wlr_scene_buffer_set_dest_size(picture->node, box.width, box.height);
    wlr_scene_node_set_position(&picture->node->node, box.x, box.y);
    // An HDR monitor's frame is in its own colours (hdr.c), which the scene converts back into
    // themselves.
    const struct wlr_output_image_description *colours = wlr_output->image_description;
    wlr_scene_buffer_set_transfer_function(picture->node, colours ? colours->transfer_function : 0);
    wlr_scene_buffer_set_primaries(picture->node, colours ? colours->primaries : 0);
    wlr_scene_buffer_set_opacity(picture->node, COVER);
    picture->box = box;
    picture->width = wlr_output->width;
    picture->height = wlr_output->height;
    picture->transform = wlr_output->transform;
}

/* The pictures start fading at their next frame. */
static void release(struct sh_server *server) {
    struct sh_crossfade *fade = server->crossfade;
    fade->holding = false;
    fade->start = 0;
    if (fade->timer)
        wl_event_source_timer_update(fade->timer, 0);
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) {
        if (output->crossfade)
            wlr_output_schedule_frame(output->wlr_output);
    }
}

static int hold_expired(void *data) {
    struct sh_server *server = data;
    if (server->crossfade && server->crossfade->holding) {
        wlr_log(WLR_INFO, "The shell did not draw profile %s in time; crossfading anyway",
                server->crossfade->profile);
        release(server);
    }
    return 0;
}

/* The appearance profile changed: every monitor in the layout keeps showing what it showed until
 * the new look is drawn under it, then crossfades to it. Called before anything changes, so the
 * scene still shows the old look. A crossfade under way starts again from what it shows. Nothing
 * crossfades without animations or while the session is locked. */
void crossfade_start(struct sh_server *server) {
    const struct sh_settings *settings = server_settings(server);
    if (!settings->animations || server->locked || !settings->profile[0])
        return;
    struct sh_crossfade *fade = server->crossfade;
    if (!fade) {
        fade = calloc(1, sizeof(*fade));
        if (fade)
            fade->tree = wlr_scene_tree_create(&server->scene->tree);
        if (!fade || !fade->tree) {
            free(fade);
            return;
        }
        wlr_scene_node_place_below(&fade->tree->node, &server->drag_icons->node);
        server->crossfade = fade;
    }
    // Held, the pictures show the look before the last change still, and stay; fading, new ones
    // are taken of what shows, the old pictures part of it.
    if (!fade->holding) {
        struct sh_output *output;
        wl_list_for_each(output, &server->outputs, link) lay_picture(fade, output);
    }
    if (!any_picture(server)) {
        crossfade_end(server);
        return;
    }
    snprintf(fade->profile, sizeof(fade->profile), "%s", settings->profile);
    double speed = settings->animation_speed > 0 ? settings->animation_speed : 1;
    fade->duration = (int)fmax(1, lround(CROSSFADE_MS / speed));
    fade->opacity = COVER;
    if (!shell_listening(server)) {
        release(server);
        return;
    }
    if (!fade->timer)
        fade->timer = wl_event_loop_add_timer(wl_display_get_event_loop(server->wl_display),
                                              hold_expired, server);
    if (!fade->timer) {
        release(server);
        return;
    }
    fade->holding = true;
    wl_event_source_timer_update(fade->timer, hold_ms(server));
}

/* Takes the pictures away at once, as the session locks or the compositor stops. */
void crossfade_end(struct sh_server *server) {
    struct sh_crossfade *fade = server->crossfade;
    if (!fade)
        return;
    struct sh_output *output;
    for_each_connected_output(output, server) drop_picture(output);
    wlr_scene_node_destroy(&fade->tree->node);
    if (fade->timer)
        wl_event_source_remove(fade->timer);
    free(fade);
    server->crossfade = NULL;
}

/* `output` goes away, and its picture with it. */
void crossfade_forget_output(struct sh_output *output) {
    drop_picture(output);
    if (output->server->crossfade && !any_picture(output->server))
        crossfade_end(output->server);
}

/* Before an output's frame: a picture goes where its output moved, turned, or changed size or
 * scale since it was taken, and once released the pictures fade with the time. */
void crossfade_tick(struct sh_server *server) {
    struct sh_crossfade *fade = server->crossfade;
    if (!fade)
        return;
    struct sh_output *output;
    for_each_connected_output(output, server) {
        struct sh_crossfade_picture *picture = output->crossfade;
        if (!picture)
            continue;
        struct wlr_box box;
        wlr_output_layout_get_box(server->output_layout, output->wlr_output, &box);
        if (!wlr_box_equal(&box, &picture->box) || output->wlr_output->width != picture->width ||
            output->wlr_output->height != picture->height ||
            output->wlr_output->transform != picture->transform)
            drop_picture(output);
    }
    if (!any_picture(server)) {
        crossfade_end(server);
        return;
    }
    if (fade->holding)
        return;
    int64_t now = now_ms();
    if (!fade->start)
        fade->start = now;
    double progress = (double)(now - fade->start) / fade->duration;
    if (progress >= 1) {
        crossfade_end(server);
        return;
    }
    static const struct sh_curve ease = {.kind = SH_CURVE_EASE_IN_OUT};
    fade->opacity = (float)(COVER * (1 - sh_curve_eval(&ease, progress)));
    // Each frame asks for the next while the fade lasts, also the first, whose opacity is the
    // one the pictures have, which damages nothing.
    for_each_connected_output(output, server) {
        if (output->crossfade) {
            wlr_scene_buffer_set_opacity(output->crossfade->node, fade->opacity);
            wlr_output_schedule_frame(output->wlr_output);
        }
    }
}

/* "profile_drawn NAME": the shell has drawn the look of profile NAME, which a crossfade waiting
 * for it then shows. Any other time it changes nothing. */
void control_profile_drawn(struct sh_server *server, int fd, const char *arguments) {
    char name[64] = "", extra[2] = "";
    if (sscanf(arguments, " %63s %1s", name, extra) != 1) {
        control_reply(fd, "error: usage: profile_drawn NAME\n");
        return;
    }
    struct sh_crossfade *fade = server->crossfade;
    if (fade && fade->holding && !strcmp(name, fade->profile))
        release(server);
    control_reply(fd, "ok\n");
}

/* `get crossfade`: for each output with a picture over it, its name, whether the picture is held
 * for the shell ("held") or fading ("fading"), the profile it crossfades to, and its opacity in
 * thousandths. Nothing while no crossfade runs. */
void describe_crossfade(struct sh_server *server, int fd, const char *arguments) {
    control_reply(fd, "ok\n");
    struct sh_crossfade *fade = server->crossfade;
    if (!fade)
        return;
    struct sh_output *output;
    for_each_connected_output(output, server) {
        if (!output->crossfade)
            continue;
        char line[192];
        snprintf(line, sizeof(line), "%s\t%s\t%s\t%ld\n", output->wlr_output->name,
                 fade->holding ? "held" : "fading", fade->profile, lround(fade->opacity * 1000));
        control_reply(fd, line);
    }
}
