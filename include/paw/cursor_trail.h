// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* A cursor trail as arithmetic and pictures: little sparkles, sakura petals or hearts that fall
 * off the pointer as it moves, twinkle or tumble, and fade within a second.
 * src/compositor/cursor_trail.c draws them over the windows, under the cursor. Times are in
 * milliseconds and distances in logical pixels, y growing downward; nothing here touches wlroots
 * or the clock. */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Lua appearance.cursor_trail.kind: what falls off the pointer. */
enum sh_trail_kind {
    SH_TRAIL_NONE,
    SH_TRAIL_SPARKLES,
    SH_TRAIL_PETALS,
    SH_TRAIL_HEARTS,
    SH_TRAIL_KINDS,
};

/* At most this many particles at once, the oldest giving way to new ones; and at most this many
 * colours the sparkles and hearts come in. */
enum { SH_TRAIL_MAX = 120, SH_TRAIL_TINTS = 6 };

/* "none", "sparkles", "petals" or "hearts". */
const char *sh_trail_kind_name(enum sh_trail_kind kind);

/* The pictures a kind is drawn with, each a square: for sparkles, in each of `tints` colours, a
 * four-pointed star at three sizes and a speck of glitter; for hearts, in each colour, a heart at
 * two sizes; for petals, the ambient particles' sakura petals (paw/ambient.h), whose pinks are
 * their own. */
int sh_trail_sprite_count(enum sh_trail_kind kind, int tints);
/* The side of picture `sprite` in logical pixels, as it is drawn at its full size. */
double sh_trail_sprite_side(enum sh_trail_kind kind, int sprite);
/* Paints picture `sprite` of `kind`, `side` pixels square, into `pixels`: premultiplied ARGB
 * (0xAARRGGBB words), rows without padding, transparent around it. `colors` holds `tints` sRGB
 * colours, each channel 0 to 1, which the sparkles and the hearts come in. */
void sh_trail_paint(enum sh_trail_kind kind, int sprite, const float (*colors)[3], int tints,
                    uint32_t *pixels, int side);

/* One particle, `life` 0 for a free place. Where it is (its middle, in the layout) and how fast it
 * goes (pixels per millisecond); how long it has been there and how long it lasts; its size, a
 * share of its picture's; its twinkle or sway (a phase in radians and its rate in radians per
 * millisecond, and how far a sway reaches); a petal's turn and spin, its tumble and the tumble's
 * rate, and the axis it tumbles about (0 across, 1 up and down); and its picture (a petal's first
 * of its turns). */
struct sh_trail_particle {
    double x, y, vx, vy;
    double age, life;
    double scale;
    double phase, rate, reach;
    double angle, spin, tumble, tumble_rate;
    int sprite, axis;
};

/* The particles behind the pointer: their kind, the colours there are, how many fall (1 the usual
 * number, 2 twice as many), the share of a particle the way so far owes, a xorshift state that
 * spawning moves on (never 0), and how many are there. */
struct sh_trail {
    enum sh_trail_kind kind;
    int tints;
    double amount;
    double owed;
    uint32_t seed;
    int count;
    struct sh_trail_particle particles[SH_TRAIL_MAX];
};

/* Empties the trail and sets what falls next, keeping the seed. */
void sh_trail_reset(struct sh_trail *trail, enum sh_trail_kind kind, int tints, double amount);
/* The pointer moved from (x0, y0) to (x1, y1): particles come off it along the way, as many as
 * that distance asks (a few a hundred pixels at the usual amount), none for a pointer that stayed
 * where it was; the oldest give way once SH_TRAIL_MAX are there. Returns how many came. */
int sh_trail_moved(struct sh_trail *trail, double x0, double y0, double x1, double y1);
/* Moves the particles on by `dt` milliseconds: falling, drifting and slowing, twinkling,
 * tumbling or swaying, and gone once their time is up. Returns how many are left. */
int sh_trail_step(struct sh_trail *trail, double dt);
/* Takes every particle away. */
void sh_trail_clear(struct sh_trail *trail);

/* How a particle is drawn: its picture, turned `quarters` quarter turns clockwise on the screen;
 * its box (top-left corner in the layout, and size); and how opaque it is. */
struct sh_trail_look {
    int sprite, quarters;
    double x, y, width, height;
    double opacity;
};
void sh_trail_look(const struct sh_trail *trail, const struct sh_trail_particle *particle,
                   struct sh_trail_look *look);

#ifdef __cplusplus
}
#endif
