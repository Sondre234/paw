/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Power saving without an idle daemon (Lua `idle`): after so long without input the screens
 * dim, the monitors turn off (output_power.c), the screen locks and the machine suspends
 * (power.c), each once until the next input, which undoes the dimming and turns on the monitors
 * this turned off. An idle inhibitor, or the session leaving the front, holds every step off. */
#include "server.h"
#include "paw/power_supply.h"

/* How dark the screens dim: the opacity of the black laid over them, and how long it takes to
 * fade in, and out again at the next input. */
#define DIM_OPACITY 0.5
#define DIM_FADE_IN_MS 1000
#define DIM_FADE_OUT_MS 150

/* The timeout of `step` in `steps` (milliseconds, 0 for never), and its name for `get idle`. */
int idle_step_timeout(const struct sh_idle_steps *steps, enum sh_idle_step step,
                      const char **name) {
    static const char *const names[] = {"dim", "display_off", "lock", "suspend"};
    const int timeouts[] = {steps->dim, steps->display_off, steps->lock, steps->suspend};
    if (name)
        *name = names[step];
    return timeouts[step];
}

/* The steps for the power the machine runs on now, read from $PAW_SYSFS (the tests' fake
 * one), else /sys. */
const struct sh_idle_steps *idle_steps(struct sh_server *server, bool *battery) {
    const char *sysfs = getenv("PAW_SYSFS");
    *battery = sh_on_battery(sysfs && *sysfs ? sysfs : "/sys");
    const struct sh_settings *settings = server_settings(server);
    return *battery ? &settings->idle_battery : &settings->idle;
}

/* Whether no step may be taken: an idle inhibitor (a video playing) exists, or the session is
 * not on screen, where another desktop's user would have the machine suspend under them. */
bool idle_held(struct sh_server *server) {
    if (server->inhibitors > 0)
        return true;
#if WLR_HAS_SESSION
    if (server->session && !server->session->active)
        return true;
#endif
    return false;
}

/* Puts the black over the whole layout at its opacity now, and takes it away once it has faded
 * out. True while it is still fading. */
bool tick_idle(struct sh_server *server) {
    struct sh_idle *idle = &server->idle;
    if (!idle->dim)
        return false;
    int64_t now = now_ms();
    bool fading = sh_fade_active(&idle->fade, now);
    if (!fading && idle->fade.to <= 0) {
        wlr_scene_node_destroy(&idle->dim->node);
        idle->dim = NULL;
        return false;
    }
    struct wlr_box box;
    wlr_output_layout_get_box(server->output_layout, NULL, &box);
    wlr_scene_node_set_position(&idle->dim->node, box.x, box.y);
    wlr_scene_buffer_set_dest_size(idle->dim, box.width, box.height);
    wlr_scene_buffer_set_opacity(idle->dim, (float)sh_fade_value(&idle->fade, now));
    return fading;
}

/* Fades the black over every output toward `target`. */
static void dim_to(struct sh_server *server, double target) {
    struct sh_idle *idle = &server->idle;
    if (target > 0 && !idle->dim) {
        if (!server->black)
            server->black = sh_black_buffer();
        if (!server->black)
            return;
        idle->dim = sh_dim_create(idle->tree, server->black);
        if (!idle->dim)
            return;
        sh_fade_init(&idle->fade, 0);
        // Over everything, the lock and whatever came after this tree too.
        wlr_scene_node_raise_to_top(&idle->tree->node);
    }
    if (!idle->dim)
        return;
    int duration = !server_settings(server)->animations ? 0
                   : target > 0                          ? DIM_FADE_IN_MS
                                                         : DIM_FADE_OUT_MS;
    sh_fade_to(&idle->fade, target, now_ms(), duration);
    tick_idle(server);
    schedule_frames(server);
}

static void take_step(struct sh_server *server, enum sh_idle_step step) {
    struct sh_output *output, *temporary;
    switch (step) {
    case SH_IDLE_DIM:
        wlr_log(WLR_INFO, "Idle: dimming the screens");
        dim_to(server, DIM_OPACITY);
        break;
    case SH_IDLE_DISPLAY_OFF:
        wlr_log(WLR_INFO, "Idle: turning the monitors off");
        wl_list_for_each_safe(output, temporary, &server->outputs, link) {
            if (!output->powered_off && set_output_power(output, false))
                output->idle_off = true;
        }
        break;
    case SH_IDLE_LOCK:
        if (!server->lock) {
            wlr_log(WLR_INFO, "Idle: locking the screen");
            power_run(server, SH_LOCK);
        }
        break;
    case SH_IDLE_SUSPEND:
        wlr_log(WLR_INFO, "Idle: suspending");
        power_run(server, SH_SUSPEND);
        break;
    case SH_IDLE_STEPS:
        break;
    }
}

/* Takes each step whose time has come since the last input, and sets the timer for the next one
 * on either power source, so that a change of source shows at the next step at the latest. */
static void idle_update(struct sh_server *server) {
    struct sh_idle *idle = &server->idle;
    const struct sh_settings *settings = server_settings(server);
    const struct sh_idle_steps *tables[] = {&settings->idle, &settings->idle_battery};
    bool any = false;
    for (size_t i = 0; i < 2; ++i)
        for (int step = 0; step < SH_IDLE_STEPS; ++step)
            any |= idle_step_timeout(tables[i], step, NULL) > 0;
    bool was_armed = idle->armed;
    idle->armed = false;
    if (!idle->timer)
        return; // not started yet
    if (!any || idle_held(server)) {
        if (was_armed) // input calls this while nothing is armed, as often as it comes
            wl_event_source_timer_update(idle->timer, 0);
        return;
    }
    bool battery;
    const struct sh_idle_steps *steps = idle_steps(server, &battery);
    int64_t idle_for = now_ms() - idle->last_input;
    for (int step = 0; step < SH_IDLE_STEPS; ++step) {
        int timeout = idle_step_timeout(steps, step, NULL);
        if (timeout > 0 && idle_for >= timeout && !(idle->done & (1u << step))) {
            idle->done |= 1u << step;
            take_step(server, step);
        }
    }
    int64_t next = 0;
    for (size_t i = 0; i < 2; ++i)
        for (int step = 0; step < SH_IDLE_STEPS; ++step) {
            int64_t left = idle_step_timeout(tables[i], step, NULL) - idle_for;
            if (!(idle->done & (1u << step)) && left > 0 && (!next || left < next))
                next = left;
        }
    wl_event_source_timer_update(idle->timer, (int)next);
    idle->armed = next > 0;
}

static int idle_timer(void *data) {
    idle_update(data);
    return 0;
}

/* Input, or the machine waking up: the time without input starts again, the screens brighten
 * and the monitors the display_off step turned off come on. True when some did, so that a key
 * that woke them goes no further. Cheap while no step was taken, as it runs on every motion: the
 * timer, set for the next step, sees when it fires that input came meanwhile. */
bool idle_activity(struct sh_server *server) {
    struct sh_idle *idle = &server->idle;
    idle->last_input = now_ms();
    bool woke = false;
    if (idle->done) {
        idle->done = 0;
        dim_to(server, 0);
        struct sh_output *output, *temporary;
        wl_list_for_each_safe(output, temporary, &server->outputs, link) {
            bool off = output->idle_off && output->powered_off;
            output->idle_off = false;
            woke |= off && set_output_power(output, true);
        }
        if (woke)
            wlr_log(WLR_INFO, "Turned on the monitors the idle steps turned off");
        // The steps taken are due again, before the one the timer waits for.
        idle_update(server);
    } else if (!idle->armed) {
        idle_update(server);
    }
    return woke;
}

/* An idle inhibitor came or went, or the session came to the front or left it. While held no
 * step is taken and the screens do not stay dimmed; released, the time without input counts
 * from then. */
void idle_hold_changed(struct sh_server *server) {
    struct sh_idle *idle = &server->idle;
    if (idle_held(server)) {
        dim_to(server, 0);
        idle->done &= ~(1u << SH_IDLE_DIM);
    } else {
        idle->last_input = now_ms();
    }
    idle_update(server);
}

void idle_reload(struct sh_server *server) {
    idle_update(server);
}

void idle_init(struct sh_server *server) {
    struct sh_idle *idle = &server->idle;
    idle->tree = wlr_scene_tree_create(&server->scene->tree);
    idle->timer = wl_event_loop_add_timer(wl_display_get_event_loop(server->wl_display),
                                          idle_timer, server);
    idle->last_input = now_ms();
    idle_update(server);
}

void idle_finish(struct sh_server *server) {
    if (server->idle.timer)
        wl_event_source_remove(server->idle.timer);
    server->idle.timer = NULL;
}
