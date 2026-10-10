/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Windows' Win+P choices, the display_mode action: every monitor extending the desktop, the
 * others mirroring the main one, the main one alone, or the others alone. A choice replaces the
 * monitors' settings until the configuration is reloaded, as wlr-output-management changes do.
 * Without a choice the action opens a popup the shell draws, and steps through the four; the one
 * shown is taken once the key has rested. The main monitor is the built-in panel, or on a machine
 * without one the primary. */
#include "server.h"
#include "paw/lid.h"

/* How long after the last step the popup takes the choice it shows. */
#define DISPLAY_MODE_SETTLE_MS 1500

static const char *const mode_names[] = {"step", "extend", "duplicate", "internal", "external"};
/* The order the popup lists and steps through them, Windows': "PC screen only", "Duplicate",
 * "Extend", "Second screen only". */
static const enum sh_display_mode popup_order[] = {SH_DISPLAY_MODE_INTERNAL, SH_DISPLAY_MODE_DUPLICATE,
                                                   SH_DISPLAY_MODE_EXTEND, SH_DISPLAY_MODE_EXTERNAL};

static int connected_outputs(struct sh_server *server) {
    return wl_list_length(&server->outputs) + wl_list_length(&server->disabled_outputs);
}

/* The main monitor: a built-in panel (the first by name), else the primary one (outputs.primary,
 * or the display settings window's), else the first of outputs.order connected, else the first
 * connector by name. NULL with none. */
static struct sh_output *main_output(struct sh_server *server) {
    const struct sh_settings *settings = server_settings(server);
    const char *primary = primary_output_name(server);
    struct sh_output *best = NULL, *output;
    for_each_connected_output(output, server) {
        if (sh_output_built_in(output->wlr_output->name) &&
            (!best || strcmp(output->wlr_output->name, best->wlr_output->name) < 0))
            best = output;
    }
    for_each_connected_output(output, server) {
        if (!best && primary[0] && output_key_matches(primary, output->wlr_output))
            best = output;
    }
    for (int n = 0; n < settings->output_count && !best; ++n) {
        for_each_connected_output(output, server) {
            if (!best && output_named(output, settings->output_order[n]))
                best = output;
        }
    }
    if (best)
        return best;
    // The first by name of those in the layout, else of the others: the walk takes them first.
    for_each_connected_output(output, server) {
        if (!best || (output->disabled == best->disabled &&
                      strcmp(output->wlr_output->name, best->wlr_output->name) < 0))
            best = output;
    }
    return best;
}

/* What the internal choice keeps on: the built-in panels, or the main monitor without one. */
static bool internal_output(struct sh_output *output, struct sh_output *main) {
    return sh_output_built_in(output->wlr_output->name) || output == main;
}

/* The choice the monitors are in now: duplicate while one mirrors another, internal or external
 * while only those are in the layout and another is connected, else extend. */
static enum sh_display_mode current_mode(struct sh_server *server) {
    struct sh_output *main = main_output(server), *output;
    int internal_on = 0, internal_off = 0, external_on = 0, external_off = 0;
    for_each_connected_output(output, server) {
        if (output->mirror)
            return SH_DISPLAY_MODE_DUPLICATE;
        if (internal_output(output, main))
            ++*(output->disabled ? &internal_off : &internal_on);
        else
            ++*(output->disabled ? &external_off : &external_on);
    }
    if (internal_on && !external_on && external_off)
        return SH_DISPLAY_MODE_INTERNAL;
    if (external_on && !internal_on && internal_off)
        return SH_DISPLAY_MODE_EXTERNAL;
    return SH_DISPLAY_MODE_EXTEND;
}

/* Puts the monitors in `mode`: each keeps the settings in force but for whether it is on and
 * what it mirrors. False, with the reason, when the mode cannot be had. */
static bool set_mode(struct sh_server *server, enum sh_display_mode mode, char *error,
                     size_t error_size) {
    if (mode < SH_DISPLAY_MODE_EXTEND || mode > SH_DISPLAY_MODE_EXTERNAL) {
        snprintf(error, error_size, "no such display mode");
        return false;
    }
    if (mode != SH_DISPLAY_MODE_EXTEND && connected_outputs(server) < 2) {
        snprintf(error, error_size, "%s needs a second monitor", mode_names[mode]);
        return false;
    }
    struct sh_output *main = main_output(server), *output;
    for_each_connected_output(output, server) {
        const struct sh_monitor *in_force = output_monitor(output);
        struct sh_monitor monitor;
        if (in_force) {
            monitor = *in_force;
        } else {
            memset(&monitor, 0, sizeof(monitor));
            snprintf(monitor.name, sizeof(monitor.name), "%s", output->wlr_output->name);
            monitor.tiling = -1;
        }
        bool internal = internal_output(output, main);
        monitor.enabled = mode == SH_DISPLAY_MODE_INTERNAL   ? internal
                          : mode == SH_DISPLAY_MODE_EXTERNAL ? !internal
                                                             : true;
        monitor.mirror[0] = '\0';
        if (mode == SH_DISPLAY_MODE_DUPLICATE && output != main)
            snprintf(monitor.mirror, sizeof(monitor.mirror), "%s", main->wlr_output->name);
        output->override = monitor;
        output->has_override = true;
    }
    wlr_log(WLR_INFO, "Display mode %s", mode_names[mode]);
    apply_output_settings(server);
    return true;
}

/* Tells the shell about the popup: "display-mode OUTPUT SHOWN CURRENT CHOICES", the choices it
 * offers in its order separated by commas, or "display-mode-close". */
static void send_popup(struct sh_server *server) {
    char line[256];
    if (!server->display_mode.open) {
        send_shell_line(server, "display-mode-close\n");
        return;
    }
    snprintf(line, sizeof(line), "display-mode %s %s %s %s\n", server->display_mode.output,
             mode_names[server->display_mode.shown], mode_names[current_mode(server)],
             connected_outputs(server) < 2 ? "extend" : "internal,duplicate,extend,external");
    send_shell_line(server, line);
}

/* Closes the popup, taking the choice it shows with `take`. */
static void close_popup(struct sh_server *server, bool take) {
    if (!server->display_mode.open)
        return;
    server->display_mode.open = false;
    if (server->display_mode.timer)
        wl_event_source_timer_update(server->display_mode.timer, 0);
    send_popup(server);
    enum sh_display_mode chosen = server->display_mode.shown;
    char error[128];
    if (take && chosen != current_mode(server) && !set_mode(server, chosen, error, sizeof(error)))
        wlr_log(WLR_ERROR, "%s", error);
}

/* Whether the pointer is on the shell's popup: someone about to click a choice. */
static bool pointer_on_popup(struct sh_server *server) {
    struct wlr_surface *surface = server->seat->pointer_state.focused_surface;
    struct wlr_layer_surface_v1 *layer =
        surface ? wlr_layer_surface_v1_try_from_wlr_surface(surface) : NULL;
    return layer && layer->namespace && !strcmp(layer->namespace, "paw-display-mode");
}

/* The key has rested: the choice shown is taken, unless the pointer is on the popup, which then
 * stays open as long as it is. */
static int popup_settled(void *data) {
    struct sh_server *server = data;
    if (pointer_on_popup(server)) {
        wl_event_source_timer_update(server->display_mode.timer, DISPLAY_MODE_SETTLE_MS);
        return 0;
    }
    close_popup(server, true);
    return 0;
}

/* Opens the popup on the focused monitor showing the choice in force, or moves it `step` on
 * through the four in the popup's order, wrapping, while it is open; the time to take it starts
 * again. With one monitor it shows extend alone. */
static void step_popup(struct sh_server *server, int step) {
    struct wlr_output *output = focused_output(server);
    if (!server->display_mode.open) {
        if (!output)
            return;
        server->display_mode.open = true;
        server->display_mode.shown = current_mode(server);
        snprintf(server->display_mode.output, sizeof(server->display_mode.output), "%s",
                 output->name);
    } else if (connected_outputs(server) >= 2) {
        int at = 0;
        while (at < 3 && (int)popup_order[at] != server->display_mode.shown)
            ++at;
        server->display_mode.shown = popup_order[(at + step + 4) % 4];
    }
    if (!server->display_mode.timer)
        server->display_mode.timer = wl_event_loop_add_timer(
            wl_display_get_event_loop(server->wl_display), popup_settled, server);
    if (server->display_mode.timer)
        wl_event_source_timer_update(server->display_mode.timer, DISPLAY_MODE_SETTLE_MS);
    send_popup(server);
}

/* The display_mode action: `mode` at once, closing the popup, or the popup's next step. False,
 * with the reason, when the mode cannot be had. */
bool display_mode_choose(struct sh_server *server, int mode, char *error, size_t error_size) {
    if (mode == SH_DISPLAY_MODE_STEP) {
        step_popup(server, 1);
        return true;
    }
    close_popup(server, false);
    return set_mode(server, (enum sh_display_mode)mode, error, error_size);
}

/* Keys while the popup is open: the arrows step through the choices, Return takes the one shown
 * at once and Escape closes it without. True for a key it took; the others are the focused
 * window's. */
bool display_mode_key(struct sh_server *server, xkb_keysym_t sym) {
    if (!server->display_mode.open)
        return false;
    switch (sym) {
    case XKB_KEY_Escape:
        close_popup(server, false);
        return true;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter:
        close_popup(server, true);
        return true;
    case XKB_KEY_Right:
    case XKB_KEY_Down:
        step_popup(server, 1);
        return true;
    case XKB_KEY_Left:
    case XKB_KEY_Up:
        step_popup(server, -1);
        return true;
    default:
        return false;
    }
}

/* For `get display_mode`: the choice in force, and while the popup is open the one it shows and
 * its monitor. */
void describe_display_mode(struct sh_server *server, int fd, const char *arguments) {
    char line[160];
    snprintf(line, sizeof(line), "ok\n%s\t%s\t%s\n", mode_names[current_mode(server)],
             server->display_mode.open ? mode_names[server->display_mode.shown] : "-",
             server->display_mode.open ? server->display_mode.output : "-");
    control_reply(fd, line);
}

void display_mode_finish(struct sh_server *server) {
    if (server->display_mode.timer)
        wl_event_source_remove(server->display_mode.timer);
    server->display_mode.timer = NULL;
}
