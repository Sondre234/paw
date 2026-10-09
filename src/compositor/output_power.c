/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Turning monitors off and on without taking them out of the layout: wlr-output-power-management
 * clients (wlopm, idle daemons), the display_off, display_on and display_toggle actions, and
 * input turning them on once every one is off. A monitor that is off keeps its windows,
 * workspaces and panels; its wlr_output is disabled, so it neither scans out nor draws frames. */
#include "server.h"

/* How long after an action turned monitors off input leaves them off: the keys that did it
 * coming back up, or a mouse nudged on the way, would otherwise bring them straight back. */
#define WAKE_GRACE_MS 1000

/* Turns a monitor in the layout off or on. On, it is configured as outputs.monitors says, which
 * a reload while it was off may have changed. False when it is not in the layout or the
 * backend refuses. */
bool set_output_power(struct sh_output *output, bool on) {
    struct sh_server *server = output->server;
    struct wlr_output *wlr_output = output->wlr_output;
    if (output->disabled)
        return false;
    if (on != output->powered_off)
        return true;
    if (!on) {
        struct wlr_output_state state;
        wlr_output_state_init(&state);
        wlr_output_state_set_enabled(&state, false);
        bool done = wlr_output_commit_state(wlr_output, &state);
        wlr_output_state_finish(&state);
        if (!done) {
            wlr_log(WLR_ERROR, "Cannot turn %s off", wlr_output->name);
            return false;
        }
        output->powered_off = true;
        mirrors_follow_power(output, false);
        wlr_log(WLR_INFO, "Turned %s off", wlr_output->name);
        // Nothing shows on it, so a lock waits for it no longer.
        send_locked_if_presented(server);
        return true;
    }
    output->powered_off = false;
    output->idle_off = false;
    configure_output(server, output);
    if (output->disabled)
        return false; // a laptop's panel behind its closed lid, out of the layout now
    if (!wlr_output->enabled) {
        output->powered_off = true;
        wlr_log(WLR_ERROR, "Cannot turn %s on", wlr_output->name);
        return false;
    }
    mirrors_follow_power(output, true);
    arrange_outputs(server); // in case a reload changed its mode or scale meanwhile
    wlr_output_schedule_frame(wlr_output);
    wlr_log(WLR_INFO, "Turned %s on", wlr_output->name);
    return true;
}

bool display_action(enum sh_action action) {
    return action == SH_DISPLAY_OFF || action == SH_DISPLAY_ON || action == SH_DISPLAY_TOGGLE;
}

/* display_off, display_on and display_toggle, on the monitor the action's target names (or the
 * control socket's "output NAME" does), else on every monitor in the layout. Toggling turns them
 * all off while any of them is on. False, with the reason, when no monitor is named so, or one
 * could not be turned off or on. */
bool display_power(struct sh_server *server, enum sh_action action, char *error,
                   size_t error_size) {
    const struct sh_callbacks *callbacks = server->callbacks;
    const char *named = callbacks->action_target ? callbacks->action_target(callbacks->userdata) : "";
    if (!named[0] && server->target_output)
        named = server->target_output->name;
    bool found = false, any_on = false;
    struct sh_output *output, *temporary;
    wl_list_for_each(output, &server->outputs, link) {
        if (named[0] && !output_key_matches(named, output->wlr_output))
            continue;
        found = true;
        any_on |= !output->powered_off;
    }
    if (!found) {
        if (named[0])
            snprintf(error, error_size, "no monitor %s", named);
        else
            snprintf(error, error_size, "no monitor");
        return false;
    }
    bool on = action == SH_DISPLAY_ON || (action == SH_DISPLAY_TOGGLE && !any_on), done = true;
    if (!on)
        server->displays_off_at = now_ms();
    wl_list_for_each_safe(output, temporary, &server->outputs, link) {
        if (named[0] && !output_key_matches(named, output->wlr_output))
            continue;
        if (!set_output_power(output, on)) {
            snprintf(error, error_size, "cannot turn %s %s", output->wlr_output->name,
                     on ? "on" : "off");
            done = false;
        }
    }
    return done;
}

/* Input with every monitor in the layout off turns them all on, whoever turned them off: nobody
 * could see to turn them on otherwise. True when it did. */
bool wake_displays(struct sh_server *server) {
    if (wl_list_empty(&server->outputs) || now_ms() - server->displays_off_at < WAKE_GRACE_MS)
        return false;
    struct sh_output *output, *temporary;
    wl_list_for_each(output, &server->outputs, link) {
        if (!output->powered_off)
            return false;
    }
    wlr_log(WLR_INFO, "Input turned the monitors on");
    wl_list_for_each_safe(output, temporary, &server->outputs, link) set_output_power(output, true);
    return true;
}

/* A wlr-output-power-management client sets a monitor's mode. A monitor out of the layout
 * (outputs.monitors' enabled = false) stays as the configuration has it. */
void output_power_set_mode(struct wl_listener *listener, void *data) {
    struct sh_server *server = wl_container_of(listener, server, output_power_set_mode);
    const struct wlr_output_power_v1_set_mode_event *event = data;
    struct sh_output *output = sh_output_for(server, event->output);
    if (output)
        set_output_power(output, event->mode == ZWLR_OUTPUT_POWER_V1_MODE_ON);
}
