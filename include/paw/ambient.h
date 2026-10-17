// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* Ambient particles as arithmetic and pictures: sakura petals tumbling down, soft snow,
 * fireflies wandering and glowing, and embers rising and cooling among flakes of ash, over an
 * area as big as a monitor. src/compositor/ambient.c draws them over the wallpaper, under the
 * windows. Times are in milliseconds and distances in logical pixels, y growing downward; nothing
 * here touches wlroots or the clock. */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Lua appearance.ambient.kind: what drifts over the wallpaper. */
enum sh_ambient_kind {
    SH_AMBIENT_NONE,
    SH_AMBIENT_SAKURA,
    SH_AMBIENT_SNOW,
    SH_AMBIENT_FIREFLIES,
    SH_AMBIENT_EMBERS,
    SH_AMBIENT_KINDS,
};

/* "none", "sakura", "snow", "fireflies" or "embers". */
const char *sh_ambient_kind_name(enum sh_ambient_kind kind);
/* How many of a kind drift on a monitor unless appearance.ambient.count says: a few dozen petals,
 * twice as many flakes, which are smaller, fewer fireflies, and a few dozen embers. */
int sh_ambient_default_count(enum sh_ambient_kind kind);

/* The pictures a kind is drawn with, each a square (see sh_ambient_sprite_width): petals of four pinks at three sizes, turned
 * by each of SH_AMBIENT_TURNS steps of a quarter turn (a quarter turn more is the same picture
 * turned on the screen); dots of snow at six sizes, the larger ones softer, in two whites; a
 * firefly's glow at two sizes in three warm colours; and an ember's glow at three sizes, the
 * nearest out of focus, each in six heats from hot amber to a dull red going out, then flakes
 * of ash at two sizes in two greys. */
enum { SH_AMBIENT_TURNS = 6, SH_AMBIENT_PETAL_TINTS = 4 };
int sh_ambient_sprite_count(enum sh_ambient_kind kind);
/* The first of the SH_AMBIENT_TURNS pictures of a sakura petal in pink `tint` (0 to
 * SH_AMBIENT_PETAL_TINTS - 1, the commonest first) and of length `size` (0 to 2, the shortest first), which the cursor trail
 * (paw/cursor_trail.h) draws too. */
int sh_ambient_petal_sprite(int tint, int size);
/* A petal drawn turned `turn` radians clockwise: the step to add to its first picture, and the
 * quarter turns more the screen gives that picture. */
void sh_ambient_petal_turn(double turn, int *step, int *quarters);
/* The side of picture `sprite` in logical pixels, as it is drawn face on: its height, and its
 * width too for a square one. */
double sh_ambient_sprite_side(enum sh_ambient_kind kind, int sprite);
/* The width of picture `sprite` in logical pixels, as it is drawn face on, no more than its
 * side. */
double sh_ambient_sprite_width(enum sh_ambient_kind kind, int sprite);
/* Paints picture `sprite` of `kind`, `width` by `height` pixels (its width and side at some
 * scale, equal for a square picture, which paints nothing otherwise), into `pixels`:
 * premultiplied ARGB (0xAARRGGBB words), rows without padding, transparent around it. A
 * firefly's glow and an ember's are brighter than their alpha, so that they add light to what is
 * under them. */
void sh_ambient_paint(enum sh_ambient_kind kind, int sprite, uint32_t *pixels, int width,
                      int height);

/* One particle. Where it is (its middle) and how far off (`depth`, 0 far to 1 near, which makes
 * it larger, faster and more opaque); how fast it falls (pixels per millisecond), or for an
 * ember how fast it rises and for a firefly how fast it wanders, and the way it heads; its sway
 * from side to side (a phase in radians, its rate in radians per millisecond, and how far it
 * reaches in pixels); a petal's turn and spin, its tumble (a phase, which narrows it along
 * `axis`, 0 across and 1 up and down) and the tumble's rate, which a flake of ash has too, while
 * an ember flickers on the tumble's and the spin's waves; a firefly's glow (the share of a cycle
 * gone) and that cycle's length, or an ember's (the share of its life gone, as it cools) and its
 * life; how long it has been there; and the picture it is drawn with face on (an ember's when it
 * is hottest), at `scale` times that picture's size. */
struct sh_ambient_particle {
    double x, y, depth;
    double fall, heading;
    double phase, rate, reach;
    double angle, spin, tumble, tumble_rate;
    double glow, glow_ms;
    double age;
    double scale;
    int sprite, axis;
};

/* The particles of one area: its kind and size, the time gone (which the wind follows) and a
 * xorshift state, never 0, which spawning moves on. */
struct sh_ambient_field {
    enum sh_ambient_kind kind;
    double width, height;
    double time;
    uint32_t seed;
};

/* A particle anywhere in the area (as the particles first show, fading in), or else coming in
 * above its top (a petal or a flake taking the place of one that fell out of it), or below its
 * bottom for an ember, hot. */
void sh_ambient_spawn(struct sh_ambient_field *field, struct sh_ambient_particle *particle,
                      bool anywhere);
/* Moves the particles on by `dt` milliseconds (at the speed asked for: twice as fast is twice
 * the time): falling or rising, drifting with the wind and swaying, turning and tumbling,
 * wandering and glowing, cooling. One that fell out of the bottom comes in again above the top,
 * an ember that rose out of the top or went out comes in again below the bottom, one that
 * drifted out at a side comes in at the other, and a firefly keeps inside the area. */
void sh_ambient_step(struct sh_ambient_field *field, struct sh_ambient_particle *particles,
                     int count, double dt);
/* The wind `time` into a field: pixels per millisecond to the right, gusting gently. */
double sh_ambient_wind(double time);

/* How a particle is drawn: its picture, turned `quarters` quarter turns clockwise on the screen;
 * its box (top-left corner in the area and size, which a petal's or a flake of ash's tumble
 * narrows); and how opaque it is. */
struct sh_ambient_look {
    int sprite, quarters;
    double x, y, width, height;
    double opacity;
};
void sh_ambient_look(const struct sh_ambient_field *field,
                     const struct sh_ambient_particle *particle, struct sh_ambient_look *look);

#ifdef __cplusplus
}
#endif
