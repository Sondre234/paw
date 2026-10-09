// SPDX-License-Identifier: GPL-3.0-or-later
#include "paw/output_state.h"
#include <stdlib.h>
#include <string.h>

static const char magic[] = "paw-outputs 1";

bool sh_output_state_path(char *path, size_t size) {
    const char *state = getenv("XDG_STATE_HOME"), *home = getenv("HOME");
    int length;
    if (state && state[0] == '/')
        length = snprintf(path, size, "%s/paw/outputs", state);
    else if (home && home[0] == '/')
        length = snprintf(path, size, "%s/.local/state/paw/outputs", home);
    else
        return false;
    return length > 0 && (size_t)length < size;
}

/* Tabs and line breaks would split the line; they are written as spaces. */
static char plain(char c) { return c == '\t' || c == '\n' || c == '\r' ? ' ' : c; }

static const char *on_off(bool value) { return value ? "on" : "off"; }

bool sh_output_state_write(const struct sh_output_state *state, FILE *file) {
    fprintf(file,
            "%s\n# The monitors' settings kept from the display settings window, laid over "
            "outputs.monitors.\n# Reset to configuration in the window removes this file.\n",
            magic);
    for (int i = 0; i < state->count; ++i) {
        const struct sh_output_saved *saved = &state->outputs[i];
        const struct sh_monitor *m = &saved->monitor;
        char description[sizeof(saved->description)];
        size_t length = 0;
        for (const char *c = saved->description; *c && length + 1 < sizeof(description); ++c)
            description[length++] = plain(*c);
        description[length] = '\0';
        fprintf(file, "%s\tdescription=%s\tenabled=%s", m->name, description, on_off(m->enabled));
        if (m->width > 0 && m->refresh > 0)
            fprintf(file, "\tmode=%dx%d@%d.%03d", m->width, m->height, m->refresh / 1000,
                    m->refresh % 1000);
        else if (m->width > 0)
            fprintf(file, "\tmode=%dx%d", m->width, m->height);
        if (m->scale > 0)
            fprintf(file, "\tscale=%.9g", m->scale);
        fprintf(file, "\ttransform=%d", m->transform);
        if (m->positioned)
            fprintf(file, "\tposition=%d,%d", m->x, m->y);
        fprintf(file, "\tvrr=%s\tmirror=%s\tbit_depth=%d\thdr=%s\tprimary=%s\n", on_off(m->vrr),
                m->mirror[0] ? m->mirror : "-", m->bit_depth == 10 ? 10 : 8, on_off(m->hdr),
                on_off(saved->primary));
    }
    return !ferror(file);
}

static bool parse_switch(const char *text, bool *value) {
    if (!strcmp(text, "on") || !strcmp(text, "1"))
        *value = true;
    else if (!strcmp(text, "off") || !strcmp(text, "0"))
        *value = false;
    else
        return false;
    return true;
}

/* A whole number from `low` to `high`, and nothing after it. */
static bool parse_number(const char *text, long low, long high, int *value) {
    char *end;
    long number = strtol(text, &end, 10);
    if (end == text || *end || number < low || number > high)
        return false;
    *value = (int)number;
    return true;
}

/* "WIDTHxHEIGHT" or "WIDTHxHEIGHT@HZ", HZ with decimals, as outputs.monitors' mode. */
static bool parse_mode(const char *text, struct sh_monitor *m) {
    int width = 0, height = 0, used = 0;
    if (sscanf(text, "%5dx%5d%n", &width, &height, &used) != 2 || width < 1 || height < 1 ||
        width > 16384 || height > 16384)
        return false;
    int refresh = 0;
    if (text[used] == '@') {
        char *end;
        double hz = strtod(text + used + 1, &end);
        if (end == text + used + 1 || *end || !(hz >= 1 && hz <= 1000))
            return false;
        refresh = (int)(hz * 1000 + 0.5);
    } else if (text[used]) {
        return false;
    }
    m->width = width;
    m->height = height;
    m->refresh = refresh;
    return true;
}

/* One KEY=VALUE field of a monitor's line; false for a value it cannot take, true for a key it
 * does not know too, after setting `unknown`. */
static bool parse_field(struct sh_output_saved *saved, const char *key, const char *value,
                        bool *unknown) {
    struct sh_monitor *m = &saved->monitor;
    if (!strcmp(key, "description")) {
        snprintf(saved->description, sizeof(saved->description), "%s", value);
        return true;
    }
    if (!strcmp(key, "enabled"))
        return parse_switch(value, &m->enabled);
    if (!strcmp(key, "mode"))
        return parse_mode(value, m);
    if (!strcmp(key, "scale")) {
        char *end;
        double scale = strtod(value, &end);
        m->scale = (float)scale;
        return end != value && !*end && scale >= 0.25 && scale <= 10;
    }
    if (!strcmp(key, "transform"))
        return parse_number(value, 0, 7, &m->transform);
    if (!strcmp(key, "position")) {
        const char *comma = strchr(value, ',');
        char x[16];
        if (!comma || comma - value >= (long)sizeof(x))
            return false;
        snprintf(x, sizeof(x), "%.*s", (int)(comma - value), value);
        m->positioned = true;
        return parse_number(x, -65536, 65536, &m->x) &&
               parse_number(comma + 1, -65536, 65536, &m->y);
    }
    if (!strcmp(key, "vrr"))
        return parse_switch(value, &m->vrr);
    if (!strcmp(key, "mirror")) {
        if (strlen(value) >= sizeof(m->mirror) || !strcmp(value, m->name))
            return false;
        snprintf(m->mirror, sizeof(m->mirror), "%s", strcmp(value, "-") ? value : "");
        return true;
    }
    if (!strcmp(key, "bit_depth"))
        return parse_number(value, 8, 10, &m->bit_depth) &&
               (m->bit_depth == 8 || m->bit_depth == 10);
    if (!strcmp(key, "hdr"))
        return parse_switch(value, &m->hdr);
    if (!strcmp(key, "primary"))
        return parse_switch(value, &saved->primary);
    *unknown = true;
    return true;
}

enum sh_output_field sh_output_state_set(struct sh_output_saved *saved, const char *key,
                                         const char *value) {
    bool unknown = false;
    if (!parse_field(saved, key, value, &unknown))
        return SH_OUTPUT_FIELD_BAD;
    return unknown ? SH_OUTPUT_FIELD_UNKNOWN : SH_OUTPUT_FIELD_SET;
}

/* A monitor's line, split at its tabs; false for one that cannot be read whole. */
static bool parse_line(char *line, struct sh_output_saved *saved) {
    memset(saved, 0, sizeof(*saved));
    struct sh_monitor *m = &saved->monitor;
    m->enabled = true;
    m->tiling = -1;
    m->bit_depth = 8;
    char *field = line, *next;
    for (int index = 0; field; ++index, field = next) {
        next = strchr(field, '\t');
        if (next)
            *next++ = '\0';
        if (index == 0) {
            if (!*field || strchr(field, ' ') || strchr(field, '=') ||
                strlen(field) >= sizeof(m->mirror))
                return false;
            snprintf(m->name, sizeof(m->name), "%s", field);
            continue;
        }
        char *equals = strchr(field, '=');
        if (!equals)
            return false;
        *equals = '\0';
        // A key of a newer paw is passed over.
        if (sh_output_state_set(saved, field, equals + 1) == SH_OUTPUT_FIELD_BAD)
            return false;
    }
    return m->name[0] != '\0';
}

bool sh_output_state_read(struct sh_output_state *state, FILE *file, char *error,
                          size_t error_size) {
    memset(state, 0, sizeof(*state));
    char line[2048];
    if (!fgets(line, sizeof(line), file) || strncmp(line, magic, strlen(magic)) != 0) {
        snprintf(error, error_size, "not a file of paw's monitors");
        return false;
    }
    bool primary = false;
    while (fgets(line, sizeof(line), file)) {
        size_t length = strlen(line);
        bool whole = length && line[length - 1] == '\n';
        if (whole)
            line[--length] = '\0';
        // A line too long for the buffer is passed over, the rest of it too.
        if (!whole && !feof(file)) {
            int c;
            while ((c = fgetc(file)) != EOF && c != '\n')
                ;
            continue;
        }
        if (!length || line[0] == '#' || state->count == SH_OUTPUT_STATE_MAX)
            continue;
        struct sh_output_saved *saved = &state->outputs[state->count];
        if (!parse_line(line, saved) ||
            sh_output_state_find(state, saved->monitor.name, saved->description))
            continue;
        // One monitor is the primary one.
        saved->primary = saved->primary && !primary;
        primary |= saved->primary;
        ++state->count;
    }
    return true;
}

static bool same_description(const char *a, const char *b) {
    for (; *a && *b; ++a, ++b) {
        if (plain(*a) != plain(*b))
            return false;
    }
    return *a == *b;
}

const struct sh_output_saved *sh_output_state_find(const struct sh_output_state *state,
                                                   const char *connector, const char *description) {
    for (int i = 0; i < state->count; ++i) {
        const struct sh_output_saved *saved = &state->outputs[i];
        if (!strcmp(saved->monitor.name, connector) &&
            same_description(saved->description, description))
            return saved;
    }
    return NULL;
}
