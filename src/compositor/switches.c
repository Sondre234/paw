/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Switch devices: a laptop's lid and a convertible's tablet mode (libinput's switches). Closing
 * the lid while another monitor shows the desktop turns the built-in panel off, as unplugging it
 * would, and opening it brings the panel back (outputs.lid: the clamshell mode); bindings run as
 * a switch changes. Tests plug in switches of their own under --headless (headless_input.c). */
#include "server.h"
#include "paw/lid.h"

/* Whether the lid holds `output` off: a built-in panel, the lid closed, and a monitor that is not
 * built in in the layout or mirroring (outputs.lid). configure_output asks this of every output.
 * A monitor mirroring the panel joins the layout once the panel is off, as Windows' duplicate
 * display becomes the external one alone. */
bool lid_holds_off(struct sh_server *server, struct sh_output *output) {
    if (!sh_output_built_in(output->wlr_output->name))
        return false;
    bool external = false;
    struct sh_output *other;
    wl_list_for_each(other, &server->outputs, link)
        external |= other != output && !sh_output_built_in(other->wlr_output->name);
    wl_list_for_each(other, &server->disabled_outputs, link)
        external |= other->mirror && !sh_output_built_in(other->wlr_output->name);
    return sh_lid_turns_off(server_settings(server)->lid, server->lid_closed, true, external);
}

/* Configures the built-in panels again after the lid or the other monitors changed: one the lid
 * holds off leaves the layout, its windows moving to another monitor (keeping their workspaces,
 * as when it is unplugged), and one it no longer holds off comes back, its windows returning
 * with outputs.return_windows. */
void apply_lid(struct sh_server *server) {
#if WLR_HAS_SESSION
    if (server->session && !server->session->active)
        return; // nothing can change now; the outputs are configured again as the VT returns
#endif
    bool changed = false;
    struct wl_list *lists[] = {&server->outputs, &server->disabled_outputs};
    for (size_t i = 0; i < 2; ++i) {
        struct sh_output *output, *temporary;
        wl_list_for_each_safe(output, temporary, lists[i], link) {
            if (!sh_output_built_in(output->wlr_output->name))
                continue;
            bool was_disabled = output->disabled;
            configure_output(server, output);
            changed |= output->disabled != was_disabled;
        }
    }
    if (!changed)
        return;
    arrange_outputs(server);
    reconfigure_tiling(server);
    return_home_windows(server);
    rehome_tiles(server);
    show_workspaces(server);
    if (server->focused_toplevel && !toplevel_visible(server->focused_toplevel)) {
        deactivate_toplevel(server);
        focus_previous(server);
    }
    struct sh_toplevel *toplevel;
    wl_list_for_each(toplevel, &server->toplevels, link) refresh_frame(toplevel);
    struct sh_output *output;
    wl_list_for_each(output, &server->outputs, link) reflow_output(server, output->wlr_output);
}

static void update_lid(struct sh_server *server) {
    bool closed = server->logind_lid_closed;
    struct sh_switch_device *device;
    wl_list_for_each(device, &server->switches, link) closed |= device->lid_closed;
    if (closed == server->lid_closed)
        return;
    server->lid_closed = closed;
    wlr_log(WLR_INFO, "The lid is %s", closed ? "closed" : "open");
    apply_lid(server);
}

/* logind says whether the lid is closed: as paw starts, which libinput tells only of a lid
 * it knows to be reliable, and as that changes. It counts as one more lid switch. */
void lid_from_logind(struct sh_server *server, bool closed) {
    server->logind_lid_closed = closed;
    if (server->running)
        update_lid(server);
}

static void switch_toggle(struct wl_listener *listener, void *data) {
    struct sh_switch_device *device = wl_container_of(listener, device, toggle);
    struct sh_server *server = device->server;
    const struct wlr_switch_toggle_event *event = data;
    bool on = event->switch_state == WLR_SWITCH_STATE_ON;
    enum sh_switch type;
    if (event->switch_type == WLR_SWITCH_TYPE_LID) {
        if (on == device->lid_closed)
            return;
        device->lid_closed = on;
        type = SH_SWITCH_LID;
    } else if (event->switch_type == WLR_SWITCH_TYPE_TABLET_MODE) {
        if (on == device->tablet_mode)
            return;
        device->tablet_mode = on;
        type = SH_SWITCH_TABLET;
    } else {
        return; // a keypad sliding out
    }
    // Opening the lid or folding the screen back is the user at the machine: it counts as input.
    input_activity(server, true);
    if (type == SH_SWITCH_LID)
        update_lid(server);
    int argument = 0;
    const struct sh_callbacks *callbacks = server->callbacks;
    enum sh_action action = callbacks->switch_toggled
                                ? callbacks->switch_toggled(callbacks->userdata, type, on, &argument)
                                : SH_NONE;
    if (action == SH_NONE)
        return;
    static const char *const changes[2][2] = {{"the lid opening", "the lid closing"},
                                              {"tablet mode ending", "tablet mode starting"}};
    wlr_log(WLR_INFO, "Running the binding of %s", changes[type][on]);
    run_action(server, action, argument);
}

static void switch_destroy(struct wl_listener *listener, void *data) {
    struct sh_switch_device *device = wl_container_of(listener, device, destroy);
    struct sh_server *server = device->server;
    wl_list_remove(&device->toggle.link);
    wl_list_remove(&device->destroy.link);
    wl_list_remove(&device->link);
    free(device);
    if (server->running)
        update_lid(server); // a lid gone is a lid that holds nothing off
}

/* A switch device appeared. libinput tells its state right after it, as a toggle, when tablet
 * mode is on already, or the lid closed of a switch it knows to be reliable (logind tells of the
 * others: lid_from_logind). */
void server_new_switch(struct sh_server *server, struct wlr_input_device *input) {
    struct sh_switch_device *device = calloc(1, sizeof(*device));
    if (!device)
        return;
    device->server = server;
    device->wlr_switch = wlr_switch_from_input_device(input);
    add_listener(&device->wlr_switch->events.toggle, &device->toggle, switch_toggle);
    add_listener(&input->events.destroy, &device->destroy, switch_destroy);
    wl_list_insert(&server->switches, &device->link);
    wlr_log(WLR_INFO, "Switch %s", input->name ? input->name : "without a name");
}
