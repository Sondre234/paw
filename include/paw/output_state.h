// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* The monitor settings kept from the display settings window, laid over outputs.monitors: plain
 * text in $XDG_STATE_HOME/paw/outputs, a line per monitor with its connector name and then
 * KEY=VALUE fields separated by tabs. Each line holds for the monitor on that connector whose
 * "make model serial" is the one it was kept for, so that another monitor plugged in there keeps
 * the configuration's settings. */
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "paw/backend.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { SH_OUTPUT_STATE_MAX = 16 };

/* One monitor's settings: an outputs.monitors entry named by its connector (`tiling` left at -1,
 * as the window leaves it to the configuration), the description it was kept for, and whether it
 * is the primary monitor, as outputs.primary would name it. */
struct sh_output_saved {
    struct sh_monitor monitor;
    char description[256];
    bool primary;
};

struct sh_output_state {
    struct sh_output_saved outputs[SH_OUTPUT_STATE_MAX];
    int count;
};

/* Fills `path` with $XDG_STATE_HOME/paw/outputs, or the same under ~/.local/state, creating
 * nothing. False when neither can be determined. */
bool sh_output_state_path(char *path, size_t size);
/* Writes `state`, a header line first. */
bool sh_output_state_write(const struct sh_output_state *state, FILE *file);
/* Reads what sh_output_state_write wrote. A line it cannot read is passed over whole, and a key
 * it does not know, so that a file edited by hand or written by a newer paw gives what it
 * can. False, with the reason in `error`, for a file that is not one. */
bool sh_output_state_read(struct sh_output_state *state, FILE *file, char *error,
                          size_t error_size);
/* What sh_output_state_set made of a KEY=VALUE field. */
enum sh_output_field {
    SH_OUTPUT_FIELD_SET,
    SH_OUTPUT_FIELD_BAD,     /* a value the key cannot take */
    SH_OUTPUT_FIELD_UNKNOWN, /* a key it does not know */
};
/* Sets one KEY=VALUE field of a monitor's line, as the file has them and `monitors apply` takes
 * them: description, enabled (on or off), mode (as outputs.monitors' mode), scale, transform (0 to
 * 7), position (X,Y), vrr, mirror (a connector name, or - for none), bit_depth (8 or 10), hdr and
 * primary. */
enum sh_output_field sh_output_state_set(struct sh_output_saved *saved, const char *key,
                                         const char *value);
/* The entry for the monitor on `connector` described as `description` (tabs and line breaks
 * counting as spaces, as they are written); NULL for none. */
const struct sh_output_saved *sh_output_state_find(const struct sh_output_state *state,
                                                   const char *connector, const char *description);

#ifdef __cplusplus
}
#endif
