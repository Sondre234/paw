// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* The toys as arithmetic: a window shaken, windows falling to the bottom of their monitor and
 * bouncing there, and a party of windows bobbing out of step under rainbow borders and confetti.
 * src/compositor/toys.c draws them. Times are in milliseconds and distances in layout pixels,
 * y growing downward; nothing here touches wlroots or the clock. */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A shake lasts this long: a damped spring's swing to either side, set going by a push. */
enum { SH_SHAKE_MS = 400 };
/* How far to the side a shaken window is drawn `t` into the shake, `amplitude` pixels at most:
 * four swings dying away, 0 at the start and from SH_SHAKE_MS on. */
double sh_shake_offset(double t, double amplitude);

/* A window falling `height` pixels to where it lands under `gravity` (pixels per millisecond
 * squared), then bouncing up again at `restitution` times the speed it came down at, until a
 * bounce would rise less than a pixel or it has bounced SH_FALL_BOUNCES times. */
enum { SH_FALL_BOUNCES = 4 };
struct sh_fall {
    double height, gravity;
    int touches; /* the times it comes down, the first landing included; 0 for no fall */
    double touch_at[SH_FALL_BOUNCES + 1]; /* when it comes down each time */
    double speed[SH_FALL_BOUNCES + 1];    /* how fast it comes down then */
    double landed;                        /* when it comes to rest: its last touch */
};
void sh_fall_plan(struct sh_fall *fall, double height, double gravity, double restitution);
/* How far below its place the window is `t` into the fall: `height` from `landed` on. */
double sh_fall_drop(const struct sh_fall *fall, double t);
/* How squashed the window is `t` into the fall, 0 for not at all: a pulse after each touch,
 * SH_SQUASH_MS long and as deep as the speed it came down at gives, SH_SQUASH at most. The
 * owner draws it shorter by that share of its height, standing on its bottom edge, and wider by
 * half as much. */
enum { SH_SQUASH_MS = 160 };
#define SH_SQUASH 0.07
double sh_fall_squash(const struct sh_fall *fall, double t);

/* Windows falling onto a floor (a y) and onto each other. The window lowest down lands first,
 * then each above it, on the floor or on the highest top of a window landed before it that lies
 * across from it (their spans of x overlap) and below it. One that would land on the floor falls
 * at least `least` all the same, sinking past it, so that windows already resting on it still
 * fall; none moves up. Writes how far each falls to `drops`. */
struct sh_toy_box {
    int x, y, width, height;
};
void sh_pile(const struct sh_toy_box *boxes, int count, double floor, double least,
             double *drops);

/* A party lasts this long. */
enum { SH_PARTY_MS = 4000 };
/* Where a window is drawn at a party `t` in, bobbing up and down and wiggling from side to side
 * out of step with the others by `phase` (radians): an offset that comes in over the first
 * 300 milliseconds and dies down over the last 600, 0 at both ends. */
void sh_party_bob(double t, double phase, double *dx, double *dy);
/* The rainbow's colour `hue` turns round it (0 red, a third green, two thirds blue; any number
 * wraps), bright and a little short of saturated, opaque and premultiplied. */
void sh_rainbow(double hue, float rgba[4]);

/* A piece of confetti: where it is, how fast it goes (pixels per millisecond), its turn about
 * its long side (radians, which narrows it as it spins) and how fast that goes, the phase of
 * its sway from side to side, how old it is and how long it lives, its colour (opaque,
 * premultiplied) and its size face on. */
struct sh_confetto {
    double x, y, vx, vy;
    double spin, spin_rate, sway;
    double age, life;
    float color[4];
    int width, height;
};
/* A piece thrown in at random along the top of a box `width` wide from (x, y), the box's
 * top-left corner, living at most `life` milliseconds. `seed` is a xorshift state, never 0,
 * which each call moves on. */
void sh_confetti_spawn(struct sh_confetto *piece, uint32_t *seed, double x, double y,
                       double width, double life);
/* Moves the piece on by `dt` milliseconds: gravity, the air holding it back (so that it falls
 * no faster than SH_CONFETTI_FALL), its sway and its spin. False once its life is over. */
#define SH_CONFETTI_FALL 0.45
bool sh_confetti_step(struct sh_confetto *piece, double dt);
/* How opaque the piece is: whole until the last third of its life, then fading out. */
float sh_confetti_alpha(const struct sh_confetto *piece);
/* How wide the piece is drawn as it spins: its width times |cos spin|, a pixel at least. */
int sh_confetti_width(const struct sh_confetto *piece);

#ifdef __cplusplus
}
#endif
