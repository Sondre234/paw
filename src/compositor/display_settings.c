/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The display settings window's monitors. What it kept is read from
 * $XDG_STATE_HOME/paw/outputs (src/output_state.c) as the configuration loads and laid over
 * outputs.monitors, each line for the monitor it was kept for. `monitors apply` takes every
 * monitor's settings at once: each monitor's are tested first, then applied on trial, and unless
 * `monitors keep` keeps them within 15 seconds (written to that file) they go back by themselves,
 * as on Windows and KDE; at once where a monitor did not take its settings. `get monitors` says
 * what the window shows. */
#include "server.h"
#include "paw/lid.h"

/* How long a trial lasts unless it is kept. Under --headless, PAW_TEST_TRIAL_MS sets it for
 * tests. */
#define TRIAL_MS 15000

static int trial_ms(struct sh_server *server) {
    const char *test = getenv("PAW_TEST_TRIAL_MS");
    int ms = test && headless_backend(server) ? atoi(test) : 0;
    return ms > 0 ? ms : TRIAL_MS;
}

/* Ends a trial without going back: the settings on trial are those in force now. */
static void forget_trial(struct sh_server *server) {
    server->display_settings.trial = false;
    if (server->display_settings.timer)
        wl_event_source_timer_update(server->display_settings.timer, 0);
}

/* Reads the kept settings again: at startup, before any monitor is configured, and as the
 * configuration reloads, which ends a trial, its settings never kept. A missing file is none;
 * one that cannot be read is said and passed over. */
void display_settings_load(struct sh_server *server) {
    if (server->display_settings.trial) {
        forget_trial(server);
        send_shell_line(server, "monitors-reverted reload\n");
    }
    struct sh_output_state *saved = &server->display_settings.saved;
    memset(saved, 0, sizeof(*saved));
    char path[PATH_MAX], error[128];
    if (!sh_output_state_path(path, sizeof(path)))
        return;
    FILE *file = fopen(path, "r");
    if (!file) {
        if (errno != ENOENT)
            wlr_log_errno(WLR_ERROR, "Cannot read %s", path);
        return;
    }
    if (!sh_output_state_read(saved, file, error, sizeof(error)))
        wlr_log(WLR_ERROR, "Passing over %s: %s", path, error);
    else if (saved->count > 0)
        wlr_log(WLR_INFO, "Monitors kept from the display settings window: %d, in %s", saved->count,
                path);
    fclose(file);
}

void display_settings_finish(struct sh_server *server) {
    if (server->display_settings.timer)
        wl_event_source_remove(server->display_settings.timer);
    server->display_settings.timer = NULL;
}

/* What the display settings window kept for the monitor on `output`'s connector, if it is the
 * monitor it was kept for, or what is on trial; NULL otherwise. */
const struct sh_output_saved *saved_output(const struct sh_output *output) {
    char description[256];
    output_description(output->wlr_output, description, sizeof(description));
    return sh_output_state_find(&output->server->display_settings.saved, output->wlr_output->name,
                                description);
}

/* The primary monitor's name: the connected one the window made primary, else outputs.primary
 * (a connector name, or "desc:" and the start of a description); "" for none. */
const char *primary_output_name(struct sh_server *server) {
    struct sh_output *output;
    for_each_connected_output(output, server) {
        const struct sh_output_saved *saved = saved_output(output);
        if (saved && saved->primary)
            return output->wlr_output->name;
    }
    return server_settings(server)->primary_output;
}

/* Whether a monitor's settings, the configuration's or those the window kept, ask for HDR. */
bool hdr_asked(struct sh_server *server) {
    const struct sh_settings *settings = server_settings(server);
    for (int i = 0; i < settings->monitor_count; ++i) {
        if (settings->monitors[i].hdr)
            return true;
    }
    const struct sh_output_state *saved = &server->display_settings.saved;
    for (int i = 0; i < saved->count; ++i) {
        if (saved->outputs[i].monitor.hdr)
            return true;
    }
    return false;
}

/* The monitors connected, in the order of `get outputs`, at most SH_OUTPUT_STATE_MAX. */
static int connected_outputs(struct sh_server *server, struct sh_output **outputs) {
    int count = 0;
    struct sh_output *output;
    wl_list_for_each_reverse(output, &server->outputs, link) {
        if (count < SH_OUTPUT_STATE_MAX)
            outputs[count++] = output;
    }
    wl_list_for_each_reverse(output, &server->disabled_outputs, link) {
        if (count < SH_OUTPUT_STATE_MAX)
            outputs[count++] = output;
    }
    return count;
}

/* The connected monitor a mirror setting names, a connector name before a "desc:" key, whether it
 * shows a picture or not; NULL for none, or `output` itself. */
static struct sh_output *named_source(struct sh_server *server, struct sh_output *output,
                                      const char *key) {
    struct sh_output *outputs[SH_OUTPUT_STATE_MAX], *found = NULL;
    int count = connected_outputs(server, outputs);
    for (int exact = 1; exact >= 0 && !found; --exact) {
        for (int i = 0; i < count && !found; ++i) {
            if (exact ? output_named(outputs[i], key)
                      : output_key_matches(key, outputs[i]->wlr_output))
                found = outputs[i];
        }
    }
    return found == output ? NULL : found;
}

/* A monitor's settings as the window shows them, each one set: those in force, but where it shows
 * a picture (in the layout, or mirroring) its mode, scale, transform and adaptive sync as they
 * are; the monitor its settings mirror by connector name; and its place in the arrangement before
 * the layout is moved to start at 0, 0 (sh_output.x and y), a mirror at its source's. */
static void shown_settings(struct sh_server *server, struct sh_output *output,
                           struct sh_output_saved *shown) {
    const struct sh_monitor *monitor = output_monitor(output);
    struct wlr_output *o = output->wlr_output;
    memset(shown, 0, sizeof(*shown));
    struct sh_monitor *m = &shown->monitor;
    snprintf(m->name, sizeof(m->name), "%s", o->name);
    output_description(o, shown->description, sizeof(shown->description));
    m->enabled = !monitor || monitor->enabled;
    m->tiling = -1;
    struct wlr_output_mode *preferred = wlr_output_preferred_mode(o);
    if (!output->disabled || output->mirror) {
        m->width = o->width, m->height = o->height, m->refresh = o->refresh;
        m->scale = o->scale;
        m->transform = o->transform;
        m->vrr = o->adaptive_sync_status == WLR_OUTPUT_ADAPTIVE_SYNC_ENABLED;
    } else {
        if (monitor && monitor->width > 0)
            m->width = monitor->width, m->height = monitor->height, m->refresh = monitor->refresh;
        else if (preferred)
            m->width = preferred->width, m->height = preferred->height,
            m->refresh = preferred->refresh;
        else
            m->width = o->width, m->height = o->height, m->refresh = o->refresh;
        m->scale = monitor && monitor->scale > 0 ? monitor->scale : 1;
        m->transform = monitor ? monitor->transform : 0;
        m->vrr = monitor && monitor->vrr;
    }
    m->vrr = m->vrr && o->adaptive_sync_supported;
    struct sh_output *source = mirrored_output(output);
    if (!source && monitor && monitor->mirror[0])
        source = named_source(server, output, monitor->mirror);
    if (source)
        snprintf(m->mirror, sizeof(m->mirror), "%s", source->wlr_output->name);
    struct sh_output *placed = source ? source : output;
    const struct sh_monitor *placed_monitor =
        placed == output ? monitor : output_monitor(placed);
    m->positioned = true;
    if (!placed->disabled)
        m->x = placed->x, m->y = placed->y;
    else if (placed_monitor && placed_monitor->positioned)
        m->x = placed_monitor->x, m->y = placed_monitor->y;
    m->bit_depth = monitor && monitor->bit_depth == 10 ? 10 : 8;
    m->hdr = monitor && monitor->hdr;
    const char *primary = primary_output_name(server);
    shown->primary = primary[0] && output_key_matches(primary, o);
}

/* A mode as outputs.monitors writes it, its refresh in Hz to the thousandth: "2560x1440@143.912",
 * or without one "2560x1440". */
static void format_mode(char *text, size_t size, int width, int height, int refresh) {
    if (refresh > 0)
        snprintf(text, size, "%dx%d@%d.%03d", width, height, refresh / 1000, refresh % 1000);
    else
        snprintf(text, size, "%dx%d", width, height);
}

/* One line of `get monitors`: the settings the window shows (shown_settings), where they come
 * from, whether it shows a picture, its depth and HDR as drawn, why it cannot have HDR, and the
 * modes it offers. */
static void describe_monitor(struct sh_server *server, int fd, struct sh_output *output) {
    struct wlr_output *o = output->wlr_output;
    const struct sh_monitor *monitor = output_monitor(output);
    struct sh_output_saved shown;
    shown_settings(server, output, &shown);
    const struct sh_monitor *m = &shown.monitor;
    const char *source = output->has_override   ? "override"
                         : saved_output(output) ? "window"
                         : monitor              ? "config"
                                                : "default";
    bool live = !output->disabled || output->mirror;
    // Whether it shows a picture: "off" while disabled or turned off, "lid" while the lid holds
    // it off.
    const char *state = lid_holds_off(server, output) ? "lid" : live && o->enabled ? "on" : "off";
    // Why it cannot have HDR whatever it asks; one that can, asking and still SDR, says so in
    // the log (hdr.c).
    const char *why = hdr_unavailable(output);
    char mode[48], line[1024];
    flatten_field(shown.description); // tabs and line breaks would break the columns
    format_mode(mode, sizeof(mode), m->width, m->height, m->refresh);
    snprintf(line, sizeof(line),
             "%s\t%s\t%d\t%s\t%d\t%s\t%s\t%d\t%d\t%s\t%.9g\t%d\t%s\t%d\t%d\t%s\t%s\t%s\t%d\t",
             o->name, shown.description, sh_output_built_in(o->name), source, m->enabled, state,
             m->mirror[0] ? m->mirror : "-", m->x, m->y, mode, m->scale, m->transform,
             !o->adaptive_sync_supported ? "-"
             : m->vrr                    ? "on"
                                         : "off",
             m->bit_depth, live && deep_format(o->render_format) ? 10 : 8, m->hdr ? "on" : "off",
             output_is_hdr(output) ? "hdr" : "sdr", why ? why : "-", shown.primary);
    control_reply(fd, line);
    // The modes it offers, each once, its preferred one marked; one without any (a nested or
    // headless output) offers the one it has.
    struct wlr_output_mode *each, *before;
    int listed = 0;
    wl_list_for_each(each, &o->modes, link) {
        bool again = false;
        wl_list_for_each(before, &o->modes, link) {
            if (before == each)
                break;
            again |= before->width == each->width && before->height == each->height &&
                     before->refresh == each->refresh;
        }
        if (again)
            continue;
        format_mode(mode, sizeof(mode), each->width, each->height, each->refresh);
        snprintf(line, sizeof(line), "%s%s%s", listed++ ? "," : "", mode,
                 each->preferred ? "*" : "");
        control_reply(fd, line);
    }
    if (!listed) {
        format_mode(mode, sizeof(mode), m->width, m->height, m->refresh);
        control_reply(fd, mode);
    }
    control_reply(fd, "\n");
}

/* `get monitors`: a line per monitor connected, in the order of `get outputs`. */
void describe_monitors(struct sh_server *server, int fd, const char *arguments) {
    control_reply(fd, "ok\n");
    struct sh_output *outputs[SH_OUTPUT_STATE_MAX];
    int count = connected_outputs(server, outputs);
    for (int i = 0; i < count; ++i)
        describe_monitor(server, fd, outputs[i]);
}

/* `get monitors_trial`: the milliseconds left before the settings on trial go back, or "-". */
void describe_monitors_trial(struct sh_server *server, int fd, const char *arguments) {
    char reply[32] = "ok\n-\n";
    if (server->display_settings.trial) {
        int64_t left = server->display_settings.trial_ends - now_ms();
        snprintf(reply, sizeof(reply), "ok\n%lld\n", (long long)(left > 0 ? left : 0));
    }
    control_reply(fd, reply);
}

/* Whether `output` took the settings in force: on, at the mode (where it has one of that size),
 * scale and transform they ask for. One the lid holds off, or one turned off, takes them as it
 * comes on, and turning one off cannot fail. */
static bool took_settings(struct sh_server *server, struct sh_output *output) {
    const struct sh_monitor *m = output_monitor(output);
    struct wlr_output *o = output->wlr_output;
    if ((m && !m->enabled) || lid_holds_off(server, output) || output->powered_off)
        return true;
    if (!o->enabled)
        return false;
    if (m && m->width > 0) {
        bool offered = wl_list_empty(&o->modes);
        struct wlr_output_mode *mode;
        wl_list_for_each(mode, &o->modes, link) offered |=
            mode->width == m->width && mode->height == m->height;
        if (offered && (o->width != m->width || o->height != m->height))
            return false;
    }
    float scale = m && m->scale > 0 ? m->scale : 1;
    return fabsf(o->scale - scale) < 0.001f && (int)o->transform == (m ? m->transform : 0);
}

/* Puts `next` in force, the kept settings with the monitors' runtime settings noted first as a
 * trial starts; the monitors' runtime settings then give way to them. */
static void apply_saved(struct sh_server *server, const struct sh_output_state *next) {
    struct sh_output *outputs[SH_OUTPUT_STATE_MAX];
    int count = connected_outputs(server, outputs);
    if (!server->display_settings.trial) {
        server->display_settings.kept = server->display_settings.saved;
        for (int i = 0; i < count; ++i) {
            char *name = server->display_settings.before[i].name;
            snprintf(name, sizeof(server->display_settings.before[i].name), "%s",
                     outputs[i]->wlr_output->name);
            server->display_settings.before[i].has_override = outputs[i]->has_override;
            server->display_settings.before[i].override = outputs[i]->override;
        }
        server->display_settings.before_count = count;
        server->display_settings.trial = true;
    }
    server->display_settings.saved = *next;
    for (int i = 0; i < count; ++i)
        outputs[i]->has_override = false;
    // The pointer stays where it is rather than going to a new primary monitor.
    snprintf(server->placed_primary, sizeof(server->placed_primary), "%s",
             primary_output_name(server));
    apply_output_settings(server);
    color_management_update(server);
}

/* Ends the trial, putting back what was in force before it, and tells the shell why:
 * "monitors-reverted REASON". */
static void revert_trial(struct sh_server *server, const char *reason) {
    if (!server->display_settings.trial)
        return;
    forget_trial(server);
    server->display_settings.saved = server->display_settings.kept;
    struct sh_output *outputs[SH_OUTPUT_STATE_MAX];
    int count = connected_outputs(server, outputs);
    for (int i = 0; i < count; ++i) {
        outputs[i]->has_override = false;
        for (int j = 0; j < server->display_settings.before_count; ++j) {
            if (output_named(outputs[i], server->display_settings.before[j].name)) {
                outputs[i]->has_override = server->display_settings.before[j].has_override;
                outputs[i]->override = server->display_settings.before[j].override;
            }
        }
    }
    snprintf(server->placed_primary, sizeof(server->placed_primary), "%s",
             primary_output_name(server));
    apply_output_settings(server);
    color_management_update(server);
    wlr_log(WLR_INFO, "Monitors back as they were before the trial: %s", reason);
    char line[160];
    snprintf(line, sizeof(line), "monitors-reverted %s\n", reason);
    send_shell_line(server, line);
}

static int trial_over(void *data) {
    revert_trial(data, "timeout");
    return 0;
}

/* Puts `next` on trial: tested first, each monitor's settings as wlr-output-management's test
 * tests a head's, then applied, and taken back at once where a monitor did not take them; else
 * the time to keep them starts. False, with why, when nothing is on trial. */
static bool start_trial(struct sh_server *server, const struct sh_output_state *next, char *error,
                        size_t error_size) {
    // Nothing changes that could not go back by itself.
    if (!server->display_settings.timer)
        server->display_settings.timer = wl_event_loop_add_timer(
            wl_display_get_event_loop(server->wl_display), trial_over, server);
    if (!server->display_settings.timer) {
        snprintf(error, error_size, "no timer to take the settings back; nothing changed");
        return false;
    }
    struct sh_output *outputs[SH_OUTPUT_STATE_MAX];
    int count = connected_outputs(server, outputs);
    for (int i = 0; i < count; ++i) {
        char description[256];
        output_description(outputs[i]->wlr_output, description, sizeof(description));
        const struct sh_output_saved *settings =
            sh_output_state_find(next, outputs[i]->wlr_output->name, description);
        // Those it leaves out are the configuration's (`monitors reset`).
        const struct sh_monitor *monitor =
            settings ? &settings->monitor
                     : monitor_settings(server_settings(server), outputs[i]->wlr_output);
        if (!test_monitor(outputs[i], monitor)) {
            snprintf(error, error_size, "%s refused these settings; nothing changed",
                     outputs[i]->wlr_output->name);
            return false;
        }
    }
    apply_saved(server, next);
    count = connected_outputs(server, outputs);
    for (int i = 0; i < count; ++i) {
        if (took_settings(server, outputs[i]))
            continue;
        char reason[96];
        snprintf(reason, sizeof(reason), "refused %s", outputs[i]->wlr_output->name);
        snprintf(error, error_size,
                 "%s did not take its settings; the monitors are back as they were",
                 outputs[i]->wlr_output->name);
        revert_trial(server, reason);
        return false;
    }
    int ms = trial_ms(server);
    wl_event_source_timer_update(server->display_settings.timer, ms);
    server->display_settings.trial_ends = now_ms() + ms;
    char line[64];
    snprintf(line, sizeof(line), "monitors-trial %d\n", ms);
    send_shell_line(server, line);
    return true;
}

/* `monitors apply`'s words into `next`, which starts from every connected monitor's settings as
 * the window shows them: a monitor's connector name, then KEY=VALUE words for its settings (as the
 * state file has them), then the next monitor's. */
static bool parse_apply(struct sh_server *server, char *words, struct sh_output_state *next,
                        char *error, size_t error_size) {
    struct sh_output *outputs[SH_OUTPUT_STATE_MAX];
    next->count = connected_outputs(server, outputs);
    for (int i = 0; i < next->count; ++i)
        shown_settings(server, outputs[i], &next->outputs[i]);
    struct sh_output_saved *current = NULL, *primary = NULL;
    char *rest = NULL;
    for (char *word = strtok_r(words, " ", &rest); word; word = strtok_r(NULL, " ", &rest)) {
        char *equals = strchr(word, '=');
        if (!equals) {
            current = NULL;
            for (int i = 0; i < next->count; ++i) {
                if (!strcmp(next->outputs[i].monitor.name, word))
                    current = &next->outputs[i];
            }
            if (!current) {
                snprintf(error, error_size, "no monitor %s is connected", word);
                return false;
            }
            continue;
        }
        if (!current) {
            snprintf(error, error_size, "name a monitor before its settings");
            return false;
        }
        *equals = '\0';
        enum sh_output_field set = strcmp(word, "description")
                                       ? sh_output_state_set(current, word, equals + 1)
                                       : SH_OUTPUT_FIELD_UNKNOWN;
        if (set != SH_OUTPUT_FIELD_SET) {
            snprintf(error, error_size,
                     set == SH_OUTPUT_FIELD_BAD ? "%s cannot take %s=%s"
                                                : "%s has no setting %s%.0s",
                     current->monitor.name, word, equals + 1);
            return false;
        }
        if (!strcmp(word, "primary") && current->primary)
            primary = current;
    }
    // One monitor is the primary one: the last one named so.
    for (int i = 0; i < next->count && primary; ++i)
        next->outputs[i].primary = &next->outputs[i] == primary;
    return true;
}

/* Whether `next` can be put on trial: some monitor shows the desktop (on, and mirroring none), a
 * mirror's source is connected and mirrors none itself, and a mode is one the monitor offers. */
static bool check_apply(struct sh_server *server, const struct sh_output_state *next, char *error,
                        size_t error_size) {
    struct sh_output *outputs[SH_OUTPUT_STATE_MAX];
    connected_outputs(server, outputs);
    bool desktop = false;
    for (int i = 0; i < next->count; ++i) {
        const struct sh_monitor *m = &next->outputs[i].monitor;
        desktop |= m->enabled && !m->mirror[0];
        if (m->mirror[0]) {
            const struct sh_monitor *source = NULL;
            for (int j = 0; j < next->count; ++j) {
                if (!strcmp(next->outputs[j].monitor.name, m->mirror))
                    source = &next->outputs[j].monitor;
            }
            if (!source) {
                snprintf(error, error_size, "%s cannot mirror %s, which is not connected", m->name,
                         m->mirror);
                return false;
            }
            if (source->mirror[0]) {
                snprintf(error, error_size, "%s cannot mirror %s, which mirrors %s", m->name,
                         m->mirror, source->mirror);
                return false;
            }
        }
        struct wlr_output *o = outputs[i]->wlr_output;
        bool offered = wl_list_empty(&o->modes);
        struct wlr_output_mode *mode;
        wl_list_for_each(mode, &o->modes, link) offered |=
            mode->width == m->width && mode->height == m->height;
        if (m->width > 0 && !offered) {
            snprintf(error, error_size, "%s has no %dx%d mode", m->name, m->width, m->height);
            return false;
        }
    }
    if (!desktop) {
        snprintf(error, error_size, "a monitor must show the desktop: on, and mirroring none");
        return false;
    }
    return true;
}

/* Writes the settings in force to the state file, or removes it when they are the
 * configuration's. */
static bool save_kept(struct sh_server *server, char *error, size_t error_size) {
    char path[PATH_MAX], directory[PATH_MAX], temporary[PATH_MAX + 8];
    if (!sh_output_state_path(path, sizeof(path))) {
        snprintf(error, error_size, "neither XDG_STATE_HOME nor HOME is set");
        return false;
    }
    const struct sh_output_state *saved = &server->display_settings.saved;
    if (saved->count == 0) {
        if (unlink(path) == 0 || errno == ENOENT)
            return true;
        snprintf(error, error_size, "cannot remove %s: %s", path, strerror(errno));
        return false;
    }
    snprintf(directory, sizeof(directory), "%s", path);
    *strrchr(directory, '/') = '\0';
    if (!make_directories(directory)) {
        snprintf(error, error_size, "cannot create %s: %s", directory, strerror(errno));
        return false;
    }
    snprintf(temporary, sizeof(temporary), "%s.new", path);
    FILE *file = fopen(temporary, "w");
    bool ok = file && sh_output_state_write(saved, file);
    if (file && fclose(file) != 0)
        ok = false;
    if (ok && rename(temporary, path) != 0)
        ok = false;
    if (!ok) {
        snprintf(error, error_size, "cannot write %s: %s", path, strerror(errno));
        unlink(temporary);
    }
    return ok;
}

/* `monitors apply NAME KEY=VALUE ... [NAME KEY=VALUE ...]` puts every monitor's settings on
 * trial at once, those not named as the window shows them, replying with the milliseconds before
 * they go back; `monitors keep` keeps the settings on trial, `monitors revert` takes them back,
 * and `monitors reset` puts the configuration's settings alone on trial, kept by removing the
 * state file. */
void control_monitors(struct sh_server *server, int fd, const char *arguments) {
    char words[4096], verb[16] = "", error[PATH_MAX + 128] = "", reply[PATH_MAX + 160];
    snprintf(words, sizeof(words), "%s", arguments);
    char *rest = words;
    while (*rest == ' ')
        ++rest;
    size_t length = strcspn(rest, " ");
    snprintf(verb, sizeof(verb), "%.*s", (int)length, rest);
    rest += length;
    bool done = false;
    if (!strcmp(verb, "apply") || (!strcmp(verb, "reset") && !*rest)) {
        struct sh_output_state *next = calloc(1, sizeof(*next));
        done = next &&
               (!strcmp(verb, "reset") || (parse_apply(server, rest, next, error, sizeof(error)) &&
                                           check_apply(server, next, error, sizeof(error)))) &&
               start_trial(server, next, error, sizeof(error));
        if (!next)
            snprintf(error, sizeof(error), "out of memory");
        free(next);
        if (done) {
            snprintf(reply, sizeof(reply), "ok\n%d\n", trial_ms(server));
            control_reply(fd, reply);
            return;
        }
    } else if ((!strcmp(verb, "keep") || !strcmp(verb, "revert")) && !*rest) {
        if (!server->display_settings.trial) {
            snprintf(error, sizeof(error), "no monitor settings are on trial");
        } else if (!strcmp(verb, "revert")) {
            revert_trial(server, "asked");
            done = true;
        } else {
            // Kept for this session at least, whether or not the file can be written.
            forget_trial(server);
            send_shell_line(server, "monitors-kept\n");
            done = save_kept(server, error, sizeof(error));
        }
    } else {
        snprintf(error, sizeof(error),
                 "usage: monitors apply NAME KEY=VALUE... | keep | revert | reset");
    }
    snprintf(reply, sizeof(reply), done ? "ok\n" : "error: %s\n", error);
    control_reply(fd, reply);
}
