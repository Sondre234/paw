/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "paw/cursor_trail.h"
#include "paw/ambient.h"
#include <math.h>
#include <stddef.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* The pictures. Sparkles: in each colour, four-pointed stars reaching STAR_REACH up and down from
 * their middle (and STAR_WIDE of that to each side), then a speck of glitter. Hearts: in each
 * colour, a heart HEART_UNIT across its lobes' middles (its full width 1.2 times that). Petals:
 * the ambient particles' smallest petal, drawn PETAL_SHRINK as large. */
enum { STARS = 3, SPARKLE_SHAPES = STARS + 1, HEART_SIZES = 2 };
static const double STAR_REACH[STARS] = {11, 8.5, 6};
static const double STAR_WIDE = 0.8;
static const double SPECK_SIDE = 10;
static const double HEART_UNIT[HEART_SIZES] = {13, 10};
static const double PETAL_SHRINK = 1;

/* How far the pointer goes, at amount 1, for each particle that comes off it; at most this many
 * come off one motion, however far it went. */
static const double SPACING[SH_TRAIL_KINDS] = {0, 13, 24, 32};
enum { MOST_AT_ONCE = 24 };

/* Pulled down (or, a heart, up) in pixels per millisecond squared, and slowed by the air:
 * velocities fall to 1/e over DRAG_MS. */
static const double GRAVITY[SH_TRAIL_KINDS] = {0, 0.00012, 0.00012, -0.00003};
static const double DRAG_MS = 450;
/* Fading in over this long, and out over the last share of a particle's life. */
static const double FADE_IN_MS[SH_TRAIL_KINDS] = {0, 30, 60, 50};
static const double FADE_OUT_FROM[SH_TRAIL_KINDS] = {0, 0.5, 0.55, 0.55};

const char *sh_trail_kind_name(enum sh_trail_kind kind) {
    switch (kind) {
    case SH_TRAIL_SPARKLES:
        return "sparkles";
    case SH_TRAIL_PETALS:
        return "petals";
    case SH_TRAIL_HEARTS:
        return "hearts";
    default:
        return "none";
    }
}

static int clamp_tints(int tints) {
    return tints < 1 ? 1 : tints > SH_TRAIL_TINTS ? SH_TRAIL_TINTS : tints;
}

int sh_trail_sprite_count(enum sh_trail_kind kind, int tints) {
    switch (kind) {
    case SH_TRAIL_SPARKLES:
        return clamp_tints(tints) * SPARKLE_SHAPES;
    case SH_TRAIL_PETALS:
        return sh_ambient_sprite_count(SH_AMBIENT_SAKURA);
    case SH_TRAIL_HEARTS:
        return clamp_tints(tints) * HEART_SIZES;
    default:
        return 0;
    }
}

int sh_trail_family_count(enum sh_trail_kind kind, int tints) {
    switch (kind) {
    case SH_TRAIL_SPARKLES:
    case SH_TRAIL_HEARTS:
        return clamp_tints(tints);
    case SH_TRAIL_PETALS:
        return SH_AMBIENT_PETAL_TINTS;
    default:
        return 0;
    }
}

int sh_trail_family_size(enum sh_trail_kind kind) {
    switch (kind) {
    case SH_TRAIL_SPARKLES:
        return SPARKLE_SHAPES;
    case SH_TRAIL_PETALS:
        return SH_AMBIENT_TURNS;
    case SH_TRAIL_HEARTS:
        return HEART_SIZES;
    default:
        return 0;
    }
}

int sh_trail_family_first(enum sh_trail_kind kind, int family) {
    if (family < 0)
        return 0;
    // A petal's turns are its smallest length's, the ambient pictures' first of that pink.
    return kind == SH_TRAIL_PETALS ? sh_ambient_petal_sprite(family, 0)
                                   : family * sh_trail_family_size(kind);
}

int sh_trail_family_at(const struct sh_trail *trail, int place) {
    int families = sh_trail_family_count(trail->kind, trail->tints);
    if (families < 1 || place < 0)
        return 0;
    // Mixed up, so that the colours along the trail follow no pattern one sees.
    uint32_t mixed = (uint32_t)place * 2654435761u;
    return (int)((mixed >> 16) % (uint32_t)families);
}

double sh_trail_sprite_side(enum sh_trail_kind kind, int sprite) {
    if (sprite < 0)
        return 0;
    switch (kind) {
    case SH_TRAIL_SPARKLES: {
        int shape = sprite % SPARKLE_SHAPES;
        return sprite >= SH_TRAIL_TINTS * SPARKLE_SHAPES ? 0
               : shape == STARS                          ? SPECK_SIDE
                                                         : ceil(2 * STAR_REACH[shape] + 4);
    }
    case SH_TRAIL_PETALS:
        return ceil(sh_ambient_sprite_side(SH_AMBIENT_SAKURA, sprite) * PETAL_SHRINK);
    case SH_TRAIL_HEARTS:
        return sprite >= SH_TRAIL_TINTS * HEART_SIZES
                   ? 0
                   : ceil(1.3 * HEART_UNIT[sprite % HEART_SIZES] + 3);
    default:
        return 0;
    }
}

static double clamp01(double value) { return value < 0 ? 0 : value > 1 ? 1 : value; }

static double smoothstep(double edge0, double edge1, double x) {
    double t = clamp01((x - edge0) / (edge1 - edge0));
    return t * t * (3 - 2 * t);
}

static double mix(double a, double b, double t) { return a + (b - a) * t; }

/* A pixel from premultiplied channels, 0 to 1. */
static uint32_t pack(double r, double g, double b, double a) {
    uint32_t channels[4];
    double values[4] = {a, r, g, b};
    for (int i = 0; i < 4; ++i)
        channels[i] = (uint32_t)lround(clamp01(values[i]) * 255);
    return channels[0] << 24 | channels[1] << 16 | channels[2] << 8 | channels[3];
}

/* Whether (x, y) is inside a four-pointed star reaching `a` to each side and `b` up and down from
 * its middle, its sides curving in between the points. */
static bool in_star(double x, double y, double a, double b) {
    return pow(fabs(x) / a, 0.6) + pow(fabs(y) / b, 0.6) <= 1;
}

/* A star: the colour with a white glint at its middle and a deeper edge, in a soft glow of the
 * colour; sampled four by four a pixel, for smooth edges at any size. */
static void paint_star(const float tint[3], int shape, uint32_t *pixels, int side) {
    double b = STAR_REACH[shape], a = STAR_WIDE * b;
    double logical = sh_trail_sprite_side(SH_TRAIL_SPARKLES, shape), pixel = logical / side;
    enum { SAMPLES = 4 };
    for (int row = 0; row < side; ++row) {
        for (int column = 0; column < side; ++column) {
            double rgb[3] = {0, 0, 0}, alpha = 0;
            for (int sy = 0; sy < SAMPLES; ++sy) {
                for (int sx = 0; sx < SAMPLES; ++sx) {
                    double x = (column + (sx + 0.5) / SAMPLES) * pixel - logical / 2;
                    double y = (row + (sy + 0.5) / SAMPLES) * pixel - logical / 2;
                    if (!in_star(x, y, a, b))
                        continue;
                    double r = hypot(x / a, y / b);
                    // The edge, where a star 0.7 as large is not, deeper; within it the colour,
                    // a little paler, with a white glint at the middle.
                    bool edge = !in_star(x, y, 0.7 * a, 0.7 * b) && r > 0.18;
                    double white = edge ? 0 : 0.2 + 0.8 * exp(-pow(r / 0.2, 2));
                    for (int i = 0; i < 3; ++i)
                        rgb[i] += edge ? 0.8 * tint[i] : mix(tint[i], 1, white);
                    alpha += 1;
                }
            }
            double cover = alpha / (SAMPLES * SAMPLES);
            for (int i = 0; i < 3; ++i)
                rgb[i] = alpha > 0 ? rgb[i] / alpha * cover : 0;
            // The glow under it, fading out before the picture's edge.
            double x = (column + 0.5) * pixel - logical / 2, y = (row + 0.5) * pixel - logical / 2;
            double d = hypot(x, y);
            double glow = 0.3 * exp(-pow(d / (0.45 * b), 2)) *
                          (1 - smoothstep(logical / 2 - 1.5 * pixel, logical / 2, d));
            for (int i = 0; i < 3; ++i)
                rgb[i] += mix(tint[i], 1, 0.35) * glow * (1 - cover);
            alpha = cover + glow * (1 - cover);
            pixels[row * side + column] = alpha > 0 ? pack(rgb[0], rgb[1], rgb[2], alpha) : 0;
        }
    }
}

/* A speck of glitter: a bright dot in a small glow. */
static void paint_speck(const float tint[3], uint32_t *pixels, int side) {
    double logical = SPECK_SIDE, pixel = logical / side;
    for (int row = 0; row < side; ++row) {
        for (int column = 0; column < side; ++column) {
            double x = (column + 0.5) * pixel - logical / 2, y = (row + 0.5) * pixel - logical / 2;
            double d = hypot(x, y);
            double core = 1 - smoothstep(1.6 - 0.5 * pixel, 1.6 + 0.5 * pixel, d);
            double glow = 0.55 * exp(-pow(d / 2.6, 2)) * (1 - smoothstep(logical / 2 - pixel,
                                                                          logical / 2, d));
            double alpha = clamp01(core + glow * (1 - core));
            double white = core * 0.7;
            double rgb[3];
            for (int i = 0; i < 3; ++i)
                rgb[i] = mix(tint[i], 1, white) * alpha;
            pixels[row * side + column] = alpha > 0 ? pack(rgb[0], rgb[1], rgb[2], alpha) : 0;
        }
    }
}

/* Inigo Quilez's heart: its tip at the origin, its lobes up to y = 1.1 and out to x = +-0.6, y
 * growing upward; a signed distance in its units, negative inside. */
static double heart_distance(double x, double y) {
    x = fabs(x);
    if (y + x > 1)
        return hypot(x - 0.25, y - 0.75) - sqrt(2) / 4;
    double m = 0.5 * fmax(x + y, 0);
    double d = fmin(hypot(x, y - 1), hypot(x - m, y - m));
    return x - y < 0 ? -d : d;
}

/* A heart: the colour, deeper toward its tip and along its edge, with a white gleam on its left
 * lobe. */
static void paint_heart(const float tint[3], int size, uint32_t *pixels, int side) {
    double unit = HEART_UNIT[size];
    double logical = sh_trail_sprite_side(SH_TRAIL_HEARTS, size), pixel = logical / side;
    for (int row = 0; row < side; ++row) {
        for (int column = 0; column < side; ++column) {
            double x = (column + 0.5) * pixel - logical / 2, y = (row + 0.5) * pixel - logical / 2;
            // Into the heart's units, its box's middle (0.55 up from its tip) at the picture's.
            double hx = x / unit, hy = 0.55 - y / unit;
            double d = heart_distance(hx, hy) * unit; // logical pixels
            double cover = clamp01(0.5 - d / pixel);
            if (cover <= 0) {
                pixels[row * side + column] = 0;
                continue;
            }
            double edge = 1 - smoothstep(0, 0.09 * unit + 0.6 * pixel, -d);
            double low = 1 - smoothstep(0.1, 0.8, hy);
            double gleam = 1 - smoothstep(0.7, 1, hypot((hx + 0.3) / 0.13, (hy - 0.78) / 0.09));
            double rgb[3];
            for (int i = 0; i < 3; ++i) {
                double deep = 0.74 * tint[i];
                rgb[i] = mix(tint[i], deep, 0.35 * low);
                rgb[i] = mix(rgb[i], deep, 0.8 * edge);
                rgb[i] = mix(rgb[i], 1, 0.85 * gleam);
                rgb[i] *= cover;
            }
            pixels[row * side + column] = pack(rgb[0], rgb[1], rgb[2], cover);
        }
    }
}

void sh_trail_paint(enum sh_trail_kind kind, int sprite, const float (*colors)[3], int tints,
                    uint32_t *pixels, int side) {
    if (side < 1 || sprite < 0 || sprite >= sh_trail_sprite_count(kind, tints))
        return;
    static const float white[3] = {1, 1, 1};
    switch (kind) {
    case SH_TRAIL_SPARKLES: {
        const float *tint = colors ? colors[sprite / SPARKLE_SHAPES] : white;
        if (sprite % SPARKLE_SHAPES == STARS)
            paint_speck(tint, pixels, side);
        else
            paint_star(tint, sprite % SPARKLE_SHAPES, pixels, side);
        break;
    }
    case SH_TRAIL_PETALS:
        sh_ambient_paint(SH_AMBIENT_SAKURA, sprite, pixels, side);
        break;
    case SH_TRAIL_HEARTS:
        paint_heart(colors ? colors[sprite / HEART_SIZES] : white, sprite % HEART_SIZES, pixels,
                    side);
        break;
    default:
        break;
    }
}

static uint32_t xorshift(uint32_t *seed) {
    uint32_t x = *seed ? *seed : 0x9e3779b9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *seed = x;
}

static double uniform(uint32_t *seed, double low, double high) {
    return low + (high - low) * (xorshift(seed) / 4294967296.0);
}

void sh_trail_clear(struct sh_trail *trail) {
    for (int i = 0; i < SH_TRAIL_MAX; ++i)
        trail->particles[i].life = 0;
    trail->count = 0;
    trail->owed = 0;
}

void sh_trail_reset(struct sh_trail *trail, enum sh_trail_kind kind, int tints, double amount) {
    sh_trail_clear(trail);
    trail->kind = kind > SH_TRAIL_NONE && kind < SH_TRAIL_KINDS ? kind : SH_TRAIL_NONE;
    trail->tints = clamp_tints(tints);
    trail->amount = amount > 0 ? amount : 1;
}

/* A free place, else the one nearest the end of its life. */
static struct sh_trail_particle *place_for(struct sh_trail *trail) {
    struct sh_trail_particle *oldest = &trail->particles[0];
    for (int i = 0; i < SH_TRAIL_MAX; ++i) {
        struct sh_trail_particle *particle = &trail->particles[i];
        if (particle->life <= 0) {
            ++trail->count;
            return particle;
        }
        if (particle->age / particle->life > oldest->age / oldest->life)
            oldest = particle;
    }
    return oldest;
}

/* A particle coming off the pointer at (x, y) as it heads along (ux, uy), a unit vector. */
static void spawn(struct sh_trail *trail, double x, double y, double ux, double uy) {
    uint32_t *seed = &trail->seed;
    if (trail->kind <= SH_TRAIL_NONE || trail->kind >= SH_TRAIL_KINDS)
        return;
    struct sh_trail_particle *particle = place_for(trail);
    int family = sh_trail_family_at(trail, (int)(particle - trail->particles));
    *particle = (struct sh_trail_particle){.scale = 1};
    // Off to the side of the way a little, left behind and spreading out.
    double aside = uniform(seed, -9, 9);
    particle->x = x - uy * aside;
    particle->y = y + ux * aside;
    double spread = uniform(seed, 0, 2 * M_PI), back = 0, out = 0, fall = 0;
    particle->phase = uniform(seed, 0, 2 * M_PI);
    switch (trail->kind) {
    case SH_TRAIL_SPARKLES: {
        int pick = (int)(xorshift(seed) % 100);
        int shape = pick < 20 ? 0 : pick < 55 ? 1 : pick < 75 ? 2 : STARS;
        particle->sprite = sh_trail_family_first(trail->kind, family) + shape;
        particle->scale = uniform(seed, 0.85, 1.15);
        back = uniform(seed, 0, 0.02);
        out = uniform(seed, 0.01, 0.05);
        fall = uniform(seed, 0, 0.02);
        particle->rate = 2 * M_PI / uniform(seed, 160, 300);
        particle->life = shape == STARS ? uniform(seed, 400, 750) : uniform(seed, 550, 950);
        break;
    }
    case SH_TRAIL_PETALS: {
        particle->sprite = sh_trail_family_first(trail->kind, family);
        particle->scale = uniform(seed, 0.85, 1.15);
        back = uniform(seed, 0.01, 0.03);
        out = uniform(seed, 0.01, 0.03);
        fall = uniform(seed, 0.01, 0.03);
        particle->rate = 2 * M_PI / uniform(seed, 450, 800);
        particle->reach = uniform(seed, 3, 7);
        particle->angle = uniform(seed, 0, 2 * M_PI);
        particle->spin = uniform(seed, 0.004, 0.01) * (xorshift(seed) & 1 ? 1 : -1);
        particle->tumble = uniform(seed, 0, 2 * M_PI);
        particle->tumble_rate = 2 * M_PI / uniform(seed, 350, 700);
        particle->axis = (int)(xorshift(seed) & 1);
        particle->life = uniform(seed, 750, 1050);
        break;
    }
    case SH_TRAIL_HEARTS: {
        int size = xorshift(seed) % 100 < 45 ? 0 : 1;
        particle->sprite = sh_trail_family_first(trail->kind, family) + size;
        particle->scale = uniform(seed, 0.85, 1.1);
        back = uniform(seed, 0.005, 0.015);
        out = uniform(seed, 0.005, 0.02);
        fall = -uniform(seed, 0.01, 0.03); // hearts float up
        particle->rate = 2 * M_PI / uniform(seed, 500, 900);
        particle->reach = uniform(seed, 2, 4);
        particle->life = uniform(seed, 700, 1000);
        break;
    }
    default:
        break;
    }
    particle->vx = -ux * back + out * cos(spread);
    particle->vy = -uy * back + out * sin(spread) + fall;
}

int sh_trail_moved(struct sh_trail *trail, double x0, double y0, double x1, double y1) {
    double dx = x1 - x0, dy = y1 - y0, distance = hypot(dx, dy);
    if (trail->kind <= SH_TRAIL_NONE || trail->kind >= SH_TRAIL_KINDS || !(distance > 0) ||
        !isfinite(distance))
        return 0;
    trail->tints = clamp_tints(trail->tints);
    double amount = trail->amount > 0 ? trail->amount : 1;
    trail->owed += distance * amount / SPACING[trail->kind];
    int due = (int)floor(trail->owed);
    trail->owed -= due;
    if (due > MOST_AT_ONCE) {
        due = MOST_AT_ONCE;
        trail->owed = 0;
    }
    double ux = dx / distance, uy = dy / distance;
    for (int i = 0; i < due; ++i) {
        // Spread along the way, each somewhere in its own share of it.
        double t = (i + uniform(&trail->seed, 0, 1)) / due;
        spawn(trail, x0 + dx * t, y0 + dy * t, ux, uy);
    }
    return due;
}

int sh_trail_step(struct sh_trail *trail, double dt) {
    if (!(dt > 0))
        return trail->count;
    double slow = exp(-dt / DRAG_MS);
    double gravity = trail->kind > SH_TRAIL_NONE && trail->kind < SH_TRAIL_KINDS
                         ? GRAVITY[trail->kind]
                         : 0;
    int count = 0;
    for (int i = 0; i < SH_TRAIL_MAX; ++i) {
        struct sh_trail_particle *particle = &trail->particles[i];
        if (particle->life <= 0)
            continue;
        particle->age += dt;
        if (particle->age >= particle->life) {
            particle->life = 0;
            continue;
        }
        ++count;
        particle->vx *= slow;
        particle->vy = particle->vy * slow + gravity * dt;
        particle->x += particle->vx * dt;
        particle->y += particle->vy * dt;
        particle->phase = remainder(particle->phase + particle->rate * dt, 2 * M_PI);
        particle->angle = remainder(particle->angle + particle->spin * dt, 2 * M_PI);
        particle->tumble = remainder(particle->tumble + particle->tumble_rate * dt, 2 * M_PI);
    }
    return trail->count = count;
}

/* easeOutBack: from 0 to 1, overshooting a little before it settles, as a pop. */
static double pop(double t) {
    t = clamp01(t) - 1;
    return 1 + 2.70158 * t * t * t + 1.70158 * t * t;
}

void sh_trail_look(const struct sh_trail *trail, const struct sh_trail_particle *particle,
                   struct sh_trail_look *look) {
    *look = (struct sh_trail_look){.sprite = particle->sprite};
    enum sh_trail_kind kind = trail->kind;
    if (particle->life <= 0 || kind <= SH_TRAIL_NONE || kind >= SH_TRAIL_KINDS)
        return;
    double side = sh_trail_sprite_side(kind, particle->sprite) * particle->scale;
    double t = particle->age / particle->life;
    double fade = smoothstep(0, FADE_IN_MS[kind], particle->age) *
                  (1 - smoothstep(FADE_OUT_FROM[kind], 1, t));
    double x = particle->x, y = particle->y, width = side, height = side, opacity = 1;
    switch (kind) {
    case SH_TRAIL_SPARKLES: {
        double twinkle = 0.5 + 0.5 * cos(particle->phase);
        if (particle->sprite % SPARKLE_SHAPES == STARS) {
            opacity = 0.45 + 0.55 * twinkle;
        } else {
            // Popping out to its size, twinkling, and shrinking away as it fades.
            double grow = 0.55 + 0.45 * smoothstep(0, 70, particle->age);
            width = height =
                side * grow * (0.8 + 0.2 * twinkle) * (1 - 0.5 * smoothstep(0.55, 1, t));
            opacity = 0.7 + 0.3 * twinkle;
        }
        break;
    }
    case SH_TRAIL_PETALS: {
        // As the ambient petals: leaning into its sway and narrowed as it tumbles, its other
        // face showing past edge on.
        x += particle->reach * sin(particle->phase);
        double turn = particle->angle + 0.45 * cos(particle->phase);
        double face = cos(particle->tumble), squash = 0.2 + 0.8 * pow(fabs(face), 0.7);
        if (face < 0)
            turn = particle->axis == 0 ? M_PI - turn : -turn;
        int step;
        sh_ambient_petal_turn(turn, &step, &look->quarters);
        look->sprite = particle->sprite + step;
        if (particle->axis == 0)
            width *= squash;
        else
            height *= squash;
        opacity = 0.95 * (0.85 + 0.15 * squash);
        break;
    }
    case SH_TRAIL_HEARTS:
        x += particle->reach * sin(particle->phase);
        width = height = side * (0.3 + 0.7 * pop(particle->age / 220)) *
                         (1 - 0.25 * smoothstep(0.6, 1, t));
        break;
    default:
        break;
    }
    look->opacity = clamp01(opacity * fade);
    look->width = width;
    look->height = height;
    look->x = x - width / 2;
    look->y = y - height / 2;
}
