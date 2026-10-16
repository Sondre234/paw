/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
/* Shaking the pointer to find it (mouse.shake_to_find), as macOS does: the pointer turning back
 * sideways SH_SHAKE_TURNS times within SH_SHAKE_MS, each stroke up to a turn at least
 * SH_SHAKE_STROKE pixels wide, wider than it is tall, and quicker than SH_SHAKE_SPEED pixels a
 * second, makes a shake; and how large the cursor is drawn as it grows to SH_SHAKE_GROW times its
 * size, holds while the shaking goes on and for a while after, and shrinks back. The desktop
 * cat's strokes (src/cat.c) are slower than a flick, so far slower than a shake: neither is
 * taken for the other. */
#include <stdbool.h>
#include <stdint.h>

#define SH_SHAKE_TURNS 4
#define SH_SHAKE_MS 700
#define SH_SHAKE_STROKE 60.0
#define SH_SHAKE_SPEED 1600.0
/* How far back from the stroke's furthest point the pointer goes before that point is a turn. */
#define SH_SHAKE_BACK 16.0
/* The pointer still this long starts afresh: its next stroke begins where it rested. */
#define SH_SHAKE_REST_MS 100
#define SH_SHAKE_GROW 3.0
#define SH_SHAKE_GROW_MS 200
#define SH_SHAKE_HOLD_MS 600 /* at full size after the last shake */
#define SH_SHAKE_SHRINK_MS 400

struct sh_shake {
    bool known;      /* a position seen */
    int64_t seen_at; /* when the pointer was last seen */
    int way;    /* the stroke under way: -1 leftward, 1 rightward, 0 not yet either */
    /* Where the stroke under way began (the last turn) and when, and its furthest point yet. */
    double start_x, start_y, far_x, far_y;
    int64_t start_at, far_at;
    int64_t turns[SH_SHAKE_TURNS]; /* when the last good strokes turned, oldest first */
    int count;                     /* how many of `turns` are filled */
    /* The cursor's growth: when it began and from what scale, and when a shake was last seen
     * (0 for never). */
    int64_t grown_at, shaken_at;
    double from;
};

/* Notes the pointer at (x, y), in layout pixels, at `now` milliseconds. True when that makes a
 * shake, or keeps one going: the cursor grows, or stays grown longer. */
bool sh_shake_motion(struct sh_shake *shake, double x, double y, int64_t now);
/* Forgets the strokes under way, as when the pointer is used for something else (a drag). */
void sh_shake_reset(struct sh_shake *shake);
/* The cursor's scale at `now`: 1 at rest, up to SH_SHAKE_GROW. */
double sh_shake_scale(const struct sh_shake *shake, int64_t now);
/* Whether the cursor is still drawn grown at `now`, shrinking included. */
bool sh_shake_grown(const struct sh_shake *shake, int64_t now);
