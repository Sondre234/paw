// SPDX-License-Identifier: GPL-3.0-or-later
#include "paw/session.h"
#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char magic[] = "paw-session 1";

bool sh_session_valid_name(const char *name) {
    size_t length = name ? strlen(name) : 0;
    /* "NAME.new" is where a session is written before it replaces NAME. */
    if (!length || name[0] == '.' || length >= SH_SESSION_NAME_MAX ||
        (length >= 4 && !strcmp(name + length - 4, ".new")))
        return false;
    for (const char *c = name; *c; ++c) {
        if (!isalnum((unsigned char)*c) && *c != '.' && *c != '_' && *c != '-')
            return false;
    }
    return true;
}

bool sh_session_path(const char *name, char *path, size_t size) {
    if (name && !sh_session_valid_name(name))
        return false;
    const char *state = getenv("XDG_STATE_HOME"), *home = getenv("HOME");
    int length;
    if (state && state[0] == '/')
        length = snprintf(path, size, "%s/paw/sessions%s%s", state, name ? "/" : "",
                          name ? name : "");
    else if (home && home[0] == '/')
        length = snprintf(path, size, "%s/.local/state/paw/sessions%s%s", home,
                          name ? "/" : "", name ? name : "");
    else
        return false;
    return length > 0 && (size_t)length < size;
}

size_t sh_session_encode(const char *text, bool arg, char *out, size_t size) {
    static const char hex[] = "0123456789ABCDEF";
    size_t length = 0;
    for (const unsigned char *c = (const unsigned char *)text; *c; ++c) {
        bool escape = *c < 0x20 || *c == 0x7f || *c == '%' || (arg && *c == ' ');
        if (escape) {
            if (length + 3 < size) {
                out[length] = '%';
                out[length + 1] = hex[*c >> 4];
                out[length + 2] = hex[*c & 15];
            }
            length += 3;
        } else {
            if (length + 1 < size)
                out[length] = (char)*c;
            length += 1;
        }
    }
    if (size)
        out[length < size ? length : size - 1] = '\0';
    return length;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

size_t sh_session_decode(const char *text, size_t length, char *out, size_t size) {
    size_t used = 0;
    for (size_t i = 0; i < length; ++i) {
        char c = text[i];
        if (c == '%' && i + 2 < length) {
            int high = hex_value(text[i + 1]), low = hex_value(text[i + 2]);
            if (high >= 0 && low >= 0) {
                c = (char)(high << 4 | low);
                i += 2;
            }
        }
        if (used + 1 < size)
            out[used] = c;
        ++used;
    }
    if (size)
        out[used < size ? used : size - 1] = '\0';
    return used;
}

void sh_session_set_command(struct sh_session_window *window, const char *cmdline, size_t length) {
    window->command[0] = '\0';
    size_t used = 0;
    size_t start = 0;
    while (start < length) {
        size_t end = start;
        while (end < length && cmdline[end] != '\0')
            ++end;
        char arg[1024];
        if (end - start >= sizeof(arg) / 3)
            goto fail;
        memcpy(arg, cmdline + start, end - start);
        arg[end - start] = '\0';
        size_t needed = sh_session_encode(arg, true, window->command + used,
                                          sizeof(window->command) - used);
        if (used + needed + 2 > sizeof(window->command))
            goto fail;
        used += needed;
        window->command[used++] = ' ';
        window->command[used] = '\0';
        start = end + 1;
    }
    if (used)
        window->command[used - 1] = '\0'; // no trailing space
    return;
fail:
    window->command[0] = '\0';
}

char **sh_session_argv(const char *command) {
    if (!command || !command[0])
        return NULL;
    size_t length = strlen(command);
    size_t words = 1;
    for (size_t i = 0; i < length; ++i)
        words += command[i] == ' ';
    /* One block: the pointers, then the decoded text. */
    char **argv = malloc((words + 1) * sizeof(char *) + length + words + 1);
    if (!argv)
        return NULL;
    char *text = (char *)(argv + words + 1);
    size_t count = 0;
    size_t start = 0;
    for (size_t i = 0; i <= length; ++i) {
        if (i < length && command[i] != ' ')
            continue;
        if (i > start) {
            argv[count++] = text;
            text += sh_session_decode(command + start, i - start, text, length + 1) + 1;
        }
        start = i + 1;
    }
    argv[count] = NULL;
    if (!count) {
        free(argv);
        return NULL;
    }
    return argv;
}

static void write_field(FILE *file, const char *text, bool last) {
    char buffer[4 * 1024];
    sh_session_encode(text, false, buffer, sizeof(buffer));
    fputs(buffer, file);
    fputc(last ? '\n' : '\t', file);
}

bool sh_session_write(const struct sh_session *session, FILE *file) {
    fprintf(file, "%s\n", magic);
    for (int i = 0; i < session->output_count; ++i) {
        const struct sh_session_output *o = &session->outputs[i];
        fputs("output\t", file);
        write_field(file, o->name, false);
        fprintf(file, "%d\t%d\n", o->workspace, o->tiling);
    }
    for (int i = 0; i < session->layout_count; ++i) {
        const struct sh_session_layout *l = &session->layouts[i];
        fputs("layout\t", file);
        write_field(file, l->output, false);
        fprintf(file, "%d\t%d\t%.4f\t%d", l->workspace, l->layout, l->ratio, l->master_count);
        for (int c = 0; c < l->width_count; ++c)
            fprintf(file, "%c%.4f", c ? ',' : '\t', l->widths[c]);
        fputc('\n', file);
    }
    for (int i = 0; i < session->window_count; ++i) {
        const struct sh_session_window *w = &session->windows[i];
        fputs("window\t", file);
        write_field(file, w->output, false);
        fprintf(file, "%d\t%u\t%d\t%d\t%d\t%d\t", w->workspace, w->flags, w->x, w->y, w->width,
                w->height);
        write_field(file, w->app_id, false);
        write_field(file, w->title, false);
        fputs(w->command, file); /* already encoded */
        if (w->scroll_column > 0)
            fprintf(file, "\t%d\t%d", w->scroll_column, w->scroll_row);
        fputc('\n', file);
    }
    return !ferror(file);
}

/* Splits a line at tabs, in place; returns the number of fields. */
static int split_tabs(char *line, char **fields, int max) {
    int count = 0;
    char *start = line;
    for (char *c = line;; ++c) {
        if (*c == '\t' || *c == '\0') {
            bool end = *c == '\0';
            *c = '\0';
            if (count < max)
                fields[count] = start;
            ++count;
            start = c + 1;
            if (end)
                break;
        }
    }
    return count;
}

static void decode_into(const char *field, char *out, size_t size) {
    sh_session_decode(field, strlen(field), out, size);
}

static bool parse_int(const char *text, int *value) {
    char *end;
    long number = strtol(text, &end, 10);
    if (end == text || *end || number < -1000000 || number > 1000000)
        return false;
    *value = (int)number;
    return true;
}

bool sh_session_read(struct sh_session *session, FILE *file, char *error, size_t error_size) {
    memset(session, 0, sizeof(*session));
    char line[8192];
    int number = 0;
    if (!fgets(line, sizeof(line), file) || strncmp(line, magic, strlen(magic)) != 0) {
        snprintf(error, error_size, "not a paw session file");
        return false;
    }
    number = 1;
    while (fgets(line, sizeof(line), file)) {
        ++number;
        size_t length = strlen(line);
        if (length && line[length - 1] == '\n')
            line[--length] = '\0';
        else if (length == sizeof(line) - 1) {
            snprintf(error, error_size, "line %d is too long", number);
            return false;
        }
        if (!length)
            continue;
        char *f[16];
        int count = split_tabs(line, f, 16);
        bool ok = false;
        if (!strcmp(f[0], "output") && count == 4 &&
            session->output_count < SH_SESSION_MAX_OUTPUTS) {
            struct sh_session_output *o = &session->outputs[session->output_count];
            decode_into(f[1], o->name, sizeof(o->name));
            ok = parse_int(f[2], &o->workspace) && parse_int(f[3], &o->tiling) &&
                 o->name[0] && o->workspace >= 0;
            session->output_count += ok;
        } else if (!strcmp(f[0], "layout") && (count == 6 || count == 7) &&
                   session->layout_count < SH_SESSION_MAX_LAYOUTS) {
            struct sh_session_layout *l = &session->layouts[session->layout_count];
            decode_into(f[1], l->output, sizeof(l->output));
            char *end;
            l->ratio = strtod(f[4], &end);
            ok = parse_int(f[2], &l->workspace) && parse_int(f[3], &l->layout) && !*end &&
                 parse_int(f[5], &l->master_count) && l->output[0] && l->workspace >= 0 &&
                 l->layout >= 0;
            // The widths of a strip's columns, "0.5000,0.3333".
            for (const char *c = count == 7 ? f[6] : ""; ok && *c;) {
                char *stop;
                double width = strtod(c, &stop);
                if (stop == c || (*stop && *stop != ',') || !(width >= 0.1 && width <= 1) ||
                    l->width_count == SH_SESSION_MAX_COLUMNS) {
                    ok = false;
                    break;
                }
                l->widths[l->width_count++] = width;
                c = *stop ? stop + 1 : stop;
            }
            session->layout_count += ok;
        } else if (!strcmp(f[0], "window") && count >= 10 && count <= 13 && count != 12 &&
                   session->window_count < SH_SESSION_MAX_WINDOWS) {
            struct sh_session_window *w = &session->windows[session->window_count];
            decode_into(f[1], w->output, sizeof(w->output));
            int flags = 0;
            ok = parse_int(f[2], &w->workspace) && parse_int(f[3], &flags) &&
                 parse_int(f[4], &w->x) && parse_int(f[5], &w->y) && parse_int(f[6], &w->width) &&
                 parse_int(f[7], &w->height) && w->workspace >= 0 && flags >= 0;
            w->flags = (unsigned)flags;
            decode_into(f[8], w->app_id, sizeof(w->app_id));
            decode_into(f[9], w->title, sizeof(w->title));
            if (count >= 11) {
                if (strlen(f[10]) >= sizeof(w->command))
                    ok = false;
                else
                    strcpy(w->command, f[10]);
            }
            if (count == 13)
                ok = ok && parse_int(f[11], &w->scroll_column) &&
                     parse_int(f[12], &w->scroll_row) && w->scroll_column >= 1 &&
                     w->scroll_row >= 1;
            session->window_count += ok;
        }
        if (!ok) {
            snprintf(error, error_size, "line %d is not a valid session record", number);
            return false;
        }
    }
    return true;
}

int sh_session_match(const struct sh_session_window *saved, int saved_count,
                     const char *const *live_app_ids, const char *const *live_titles,
                     int live_count, int *assignment) {
    bool taken[SH_SESSION_MAX_WINDOWS * 2] = {false};
    if (live_count > (int)(sizeof(taken) / sizeof(*taken)))
        live_count = (int)(sizeof(taken) / sizeof(*taken));
    int pairs = 0;
    for (int i = 0; i < saved_count; ++i)
        assignment[i] = -1;
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < saved_count; ++i) {
            if (assignment[i] >= 0)
                continue;
            for (int j = 0; j < live_count; ++j) {
                if (taken[j] || strcmp(saved[i].app_id, live_app_ids[j] ? live_app_ids[j] : ""))
                    continue;
                if (pass == 0 && strcmp(saved[i].title, live_titles[j] ? live_titles[j] : ""))
                    continue;
                assignment[i] = j;
                taken[j] = true;
                ++pairs;
                break;
            }
        }
    }
    return pairs;
}
