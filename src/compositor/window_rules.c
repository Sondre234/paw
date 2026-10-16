/* SPDX-License-Identifier: GPL-3.0-or-later */
/* What window rules give a window for as long as it matches them, rather than once as it opens:
 * its look (a shadow, a border and its colours, its corners' radius; frame.c draws it) and
 * whether it keeps the machine awake (lock.c). Matched again only as the title, the app ID or the
 * configuration changes, not on every commit. */
#include "server.h"

/* What the rules give the window with its title and app ID now; every field unset when none
 * applies. */
const struct sh_window_rule *window_rule_now(struct sh_toplevel *toplevel) {
    struct sh_server *server = toplevel->server;
    struct sh_rule_now *now = &toplevel->rule_now;
    if (now->generation != server->config_generation) {
        window_rule(toplevel, &now->rule);
        now->generation = server->config_generation;
    }
    return &now->rule;
}

/* The window's title or app ID changed: its rules are matched again as they are next asked, and
 * what keeps the machine awake is looked at again. */
void window_rule_changed(struct sh_toplevel *toplevel) {
    toplevel->rule_now.generation = 0;
    idle_inhibitors_follow(toplevel->server);
}

/* The window's border colours while it has the focus (`active`) or not: its rule's, else
 * windows.border_color or border_inactive_color. */
const struct sh_gradient *window_border(struct sh_toplevel *toplevel, bool active) {
    const struct sh_window_rule *rule = window_rule_now(toplevel);
    const struct sh_settings *settings = server_settings(toplevel->server);
    const struct sh_gradient *own = active ? &rule->border_color : &rule->border_inactive_color;
    if (own->count > 0)
        return own;
    return active ? &settings->border_active_gradient : &settings->border_inactive_gradient;
}
