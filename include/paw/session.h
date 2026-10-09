// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A saved arrangement of windows (`paw msg session save NAME`): which workspace, layout and
 * tiling state each output was in, and for every window its output, workspace, place and
 * state, with the matchers (app ID and title) and the command that restore it later. Kept as
 * plain text under $XDG_STATE_HOME/paw/sessions/NAME, one record per line. */
enum {
    SH_SESSION_MAX_WINDOWS = 128,
    SH_SESSION_MAX_OUTPUTS = 16,
    SH_SESSION_MAX_LAYOUTS = 160,
    SH_SESSION_MAX_COLUMNS = 16, /* columns of a scrolling strip that are saved */
    SH_SESSION_NAME_MAX = 64,
};

enum sh_session_flag {
    SH_SESSION_TILED = 1,      /* was in the tiling */
    SH_SESSION_FLOATING = 2,   /* kept out of the tiling by hand */
    SH_SESSION_MINIMIZED = 4,
    SH_SESSION_STICKY = 8,
    SH_SESSION_FULLSCREEN = 16,
    SH_SESSION_MAXIMIZED = 32,
    SH_SESSION_SCRATCHPAD = 64,
    SH_SESSION_FOCUSED = 128,  /* had the keyboard focus */
    SH_SESSION_ABOVE = 256,    /* kept above the other windows */
};

struct sh_session_output {
    char name[64];
    int workspace; /* the workspace it showed, from 0 */
    int tiling;    /* -1 unknown, 0 floating, 1 tiling */
};

struct sh_session_layout {
    char output[64];
    int workspace; /* from 0 */
    int layout;    /* enum sh_tile_layout */
    double ratio;
    int master_count;
    /* The widths of the columns of the scrolling layout, left to right (shares of the area);
     * none for other layouts. */
    double widths[SH_SESSION_MAX_COLUMNS];
    int width_count;
};

struct sh_session_window {
    char output[64];
    int workspace; /* from 0 */
    unsigned flags;
    int x, y, width, height; /* the place it floats at; a tile's floating place when known */
    char app_id[128];
    char title[256];
    /* The command line, each argument written with sh_session_encode_arg and separated by
     * spaces; empty when unknown. */
    char command[1024];
    /* Where a tile of the scrolling layout sat: its column and its place in the stack, both
     * from 1; 0 when it was not in one. */
    int scroll_column, scroll_row;
};

struct sh_session {
    struct sh_session_output outputs[SH_SESSION_MAX_OUTPUTS];
    int output_count;
    struct sh_session_layout layouts[SH_SESSION_MAX_LAYOUTS];
    int layout_count;
    struct sh_session_window windows[SH_SESSION_MAX_WINDOWS];
    int window_count;
};

/* Session names are 1 to 63 characters of letters, digits, '.', '_' and '-', not starting with
 * a dot: they name a file. */
bool sh_session_valid_name(const char *name);
/* Fills `path` with the file of a session, creating no directories: $XDG_STATE_HOME/paw/
 * sessions/NAME, or under ~/.local/state. False when the name is invalid or no directory can be
 * determined. With a NULL `name` gives the directory itself. */
bool sh_session_path(const char *name, char *path, size_t size);

/* Writes `text` percent-encoded, so it holds no tab, newline, other control character, or
 * '%'; with `arg` also no space. Returns the length that would have been written, as
 * snprintf. */
size_t sh_session_encode(const char *text, bool arg, char *out, size_t size);
/* The reverse; returns the decoded length. */
size_t sh_session_decode(const char *text, size_t length, char *out, size_t size);
/* Encodes a NUL-separated command line (as in /proc/PID/cmdline, `length` bytes) as the
 * `command` field: each argument encoded, joined by spaces. Nothing is written for an empty
 * or over-long command line, leaving "". */
void sh_session_set_command(struct sh_session_window *window, const char *cmdline, size_t length);
/* Splits a `command` field into a NULL-terminated argument vector, one allocation freed with
 * free(); NULL for an empty command or on failure. */
char **sh_session_argv(const char *command);

bool sh_session_write(const struct sh_session *session, FILE *file);
/* Reads what sh_session_write wrote. On failure returns false with a message in `error`. */
bool sh_session_read(struct sh_session *session, FILE *file, char *error, size_t error_size);

/* Pairs saved windows with live ones by app ID and title, without pairing one window twice:
 * first those whose app ID and title both match, then those with the same app ID, in order.
 * assignment[i] receives the index of the live window for saved window i, or -1. Returns the
 * number of pairs. */
int sh_session_match(const struct sh_session_window *saved, int saved_count,
                     const char *const *live_app_ids, const char *const *live_titles,
                     int live_count, int *assignment);

#ifdef __cplusplus
}
#endif
