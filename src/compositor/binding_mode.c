/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Binding modes, as sway's modes and Hyprland's submaps: which set of key bindings is in use. The
 * configuration side keeps the bindings and looks keys up in the mode's (sh_callbacks.set_mode);
 * the compositor keeps the mode's name for subscribers ("mode NAME", which the shell shows) and
 * `paw msg get mode`. A reload and locking the session leave the mode. */
#include "server.h"

/* The binding mode in use, "default" outside any. */
const char *binding_mode(struct sh_server *server) {
    return server->binding_mode[0] ? server->binding_mode : "default";
}

/* Puts mode `mode` in use, 0 being the bindings outside any; a number no mode has changes
 * nothing. */
void set_binding_mode(struct sh_server *server, int mode) {
    const char *name = server->callbacks->set_mode
                           ? server->callbacks->set_mode(server->callbacks->userdata, mode)
                           : NULL;
    if (!name || !strcmp(name, binding_mode(server)))
        return;
    snprintf(server->binding_mode, sizeof(server->binding_mode), "%s", name);
    wlr_log(WLR_INFO, "Binding mode: %s", name);
    notify_subscribers(server);
}
