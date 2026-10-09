/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Dynamic window rules: the state the rules with `dynamic = true` hold a window to (floating,
 * sticky, kept above) as its title and app ID change, given back as they stop matching
 * (src/dynamic_rule.c). */
#include "server.h"

/* What the dynamic rules matching the window's title and app ID decide now. */
static struct sh_dynamic_rule dynamic_decision(struct sh_toplevel *toplevel) {
    const struct sh_callbacks *callbacks = toplevel->server->callbacks;
    const char *app_id = toplevel_app_id(toplevel), *title = toplevel_title(toplevel);
    struct sh_dynamic_rule rule = {-1, -1, -1};
    callbacks->dynamic_rule(callbacks->userdata, app_id ? app_id : "", title ? title : "", &rule);
    if (!server_settings(toplevel->server)->sticky)
        rule.sticky = -1;
    return rule;
}

/* As the window opens, the dynamic rules matching it hold it from the start: `rule` (set either
 * way, `ruled` saying whether the window rules gave it anything) gets what they decide, and what
 * the rules acting once and the kind of window gave it is what the window gets back as they
 * stop. Returns whether `rule` gives the window anything now. */
bool open_dynamic_rules(struct sh_toplevel *toplevel, struct sh_window_rule *rule, bool ruled) {
    toplevel->held_floating = toplevel->held_sticky = toplevel->held_above =
        (struct sh_held_value){.decided = -1};
    struct sh_dynamic_rule want = dynamic_decision(toplevel);
    if (want.floating < 0 && want.sticky < 0 && want.above < 0)
        return ruled;
    if (!ruled)
        *rule = (struct sh_window_rule){.floating = -1};
    bool floating = rule->floating >= 0 ? rule->floating : toplevel_is_dialog(toplevel);
    int give = sh_held_value_step(&toplevel->held_floating, want.floating, floating);
    if (give >= 0)
        rule->floating = give;
    if ((give = sh_held_value_step(&toplevel->held_sticky, want.sticky, rule->sticky)) >= 0)
        rule->sticky = give;
    if ((give = sh_held_value_step(&toplevel->held_above, want.above, rule->above)) >= 0)
        rule->above = give;
    return true;
}

/* The window's title or app ID changed, or the rules did: it follows what the dynamic rules
 * matching it decide now. A window in a group is left as its group has it, one hidden while a
 * window started from it takes its place or in the scratchpad as it is. */
void follow_dynamic_rules(struct sh_toplevel *toplevel) {
#if WLR_HAS_XWAYLAND
    if (toplevel->unmanaged)
        return;
#endif
    if (!toplevel_mapped(toplevel) || !toplevel->shown || toplevel->group || toplevel->swallowed ||
        toplevel->scratchpad)
        return;
    struct sh_server *server = toplevel->server;
    struct sh_dynamic_rule want = dynamic_decision(toplevel);
    // A sticky window floats whatever it was; what it was comes back as it is no longer sticky.
    bool floating = toplevel->sticky ? toplevel->sticky_floating : toplevel->floating;
    int give = sh_held_value_step(&toplevel->held_floating, want.floating, floating);
    // Moved or resized with the pointer, it is let go first, as it is for fullscreen.
    bool placing = give >= 0 && !toplevel->sticky;
    if (placing && server->grabbed_toplevel == toplevel)
        reset_cursor_mode(server);
    if (give >= 0 && toplevel->sticky)
        toplevel->sticky_floating = give;
    else if (give >= 0)
        set_floating(toplevel, give, false);
    if ((give = sh_held_value_step(&toplevel->held_sticky, want.sticky, toplevel->sticky)) >= 0) {
        if (server->grabbed_toplevel == toplevel)
            reset_cursor_mode(server);
        set_sticky(toplevel, give, true);
    }
    if ((give = sh_held_value_step(&toplevel->held_above, want.above, toplevel->above)) >= 0)
        set_above(toplevel, give);
}

/* `get dynamic_rules`, a line per window in the order of `get windows`: app_id, title, what the
 * dynamic rules decide of floating, sticky and above ("on", "off", or "-" when none decides it),
 * and which of them they hold the window to ("floating,sticky,above", "-" for none): one changed
 * by hand while held is no longer. */
void describe_dynamic_rules(struct sh_server *server, int fd) {
    control_reply(fd, "ok\n");
    struct sh_toplevel *toplevel;
    wl_list_for_each_reverse(toplevel, &server->toplevels, link) {
        char line[1024], app_id[256], title[512], held[32] = "";
        const char *raw_app_id = toplevel_app_id(toplevel), *raw_title = toplevel_title(toplevel);
        snprintf(app_id, sizeof(app_id), "%s", raw_app_id ? raw_app_id : "");
        snprintf(title, sizeof(title), "%s", raw_title ? raw_title : "");
        for (char *c = app_id; *c; ++c)
            *c = *c == '\t' || *c == '\n' || *c == '\r' ? ' ' : *c;
        for (char *c = title; *c; ++c)
            *c = *c == '\t' || *c == '\n' || *c == '\r' ? ' ' : *c;
        const struct sh_held_value *values[] = {&toplevel->held_floating, &toplevel->held_sticky,
                                                &toplevel->held_above};
        bool current[] = {toplevel->sticky ? toplevel->sticky_floating : toplevel->floating,
                          toplevel->sticky, toplevel->above};
        static const char *const names[] = {"floating", "sticky", "above"};
        const char *decided[3];
        for (int i = 0; i < 3; ++i) {
            decided[i] = values[i]->decided < 0 ? "-" : values[i]->decided ? "on" : "off";
            // Held only while the window has what the rules decided: not once changed by hand.
            if (values[i]->held && values[i]->decided >= 0 && current[i] == (values[i]->decided != 0))
                snprintf(held + strlen(held), sizeof(held) - strlen(held), "%s%s",
                         held[0] ? "," : "", names[i]);
        }
        snprintf(line, sizeof(line), "%s\t%s\t%s\t%s\t%s\t%s\n", app_id, title, decided[0],
                 decided[1], decided[2], held[0] ? held : "-");
        control_reply(fd, line);
    }
}
