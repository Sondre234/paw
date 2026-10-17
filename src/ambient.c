/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "paw/ambient.h"
#include <math.h>
#include <stddef.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* A particle that turns up anywhere fades in over this long. */
static const double FADE_IN_MS = 1500;

/* The pictures: four pinks of petal at three lengths, each turned SH_AMBIENT_TURNS ways; dots of
 * snow at six sizes in two whites; a firefly's glow at two sizes in three colours; an ember's
 * glow at three sizes in six heats, then flakes of ash at two sizes in two greys. */
enum { PETAL_TINTS = SH_AMBIENT_PETAL_TINTS, PETAL_SIZES = 3, SNOW_TINTS = 2, SNOW_SIZES = 6, GLOW_TINTS = 3,
       GLOW_SIZES = 2 };
enum { EMBER_SIZES = 3, EMBER_HEATS = 6, EMBER_GLOWS = EMBER_SIZES * EMBER_HEATS, ASH_TINTS = 2,
       ASH_SIZES = 2 };
/* A petal's length, from its stem to the notch at its tip. */
static const double PETAL_LENGTH[PETAL_SIZES] = {16, 22, 30};
/* A flake's radius; the two largest are soft and out of focus, near the eye. */
static const double FLAKE_RADIUS[SNOW_SIZES] = {2.0, 2.6, 3.2, 4.0, 6.5, 9.5};
enum { SOFT_FLAKES = 4 };
/* How far a firefly's glow reaches, and its bright core. */
static const double GLOW_REACH[GLOW_SIZES] = {20, 28};
static const double GLOW_CORE[GLOW_SIZES] = {1.8, 2.4};
/* How far an ember's glow reaches, and its spark; the nearest is soft, out of focus. */
static const double EMBER_REACH[EMBER_SIZES] = {10, 14, 22};
static const double EMBER_CORE[EMBER_SIZES] = {1.4, 1.9, 3.4};
/* A flake of ash's radius, give or take its ragged edge. */
static const double ASH_RADIUS[ASH_SIZES] = {2.0, 3.2};

/* Colours as 0-255 sRGB. A petal is deeper at its stem, paler toward its tip, and a little
 * deeper again along its edge, which sets it apart on pale paper. */
struct petal_tint {
    double stem[3], tip[3], edge[3];
};
static const struct petal_tint PETALS[PETAL_TINTS] = {
    {{240, 150, 182}, {253, 222, 232}, {228, 124, 162}}, /* sakura */
    {{233, 118, 160}, {248, 188, 209}, {214, 96, 142}},  /* a deeper pink */
    {{246, 194, 212}, {255, 248, 251}, {236, 166, 192}}, /* nearly white, blushing */
    {{244, 170, 197}, {252, 234, 241}, {234, 146, 180}}, /* pale */
};
/* Each pink's share of the petals, in hundredths. */
static const int PETAL_SHARE[PETAL_TINTS] = {40, 30, 15, 15};
static const double SNOW_MIDDLE[SNOW_TINTS][3] = {{255, 255, 255}, {255, 252, 255}};
static const double SNOW_EDGE[SNOW_TINTS][3] = {{218, 232, 255}, {230, 220, 255}};
struct glow_tint {
    double core[3], halo[3];
};
static const struct glow_tint GLOWS[GLOW_TINTS] = {
    {{255, 250, 222}, {255, 204, 96}},  /* gold */
    {{250, 255, 214}, {204, 236, 104}}, /* yellow-green */
    {{255, 242, 226}, {255, 168, 98}},  /* amber */
};
/* An ember cooling as it rises: hot amber, ember orange, a blood red, and a dull red going out;
 * and how strongly each glows. */
static const struct glow_tint EMBER_TINTS[EMBER_HEATS] = {
    {{255, 232, 190}, {255, 168, 100}}, /* hottest, in hot amber (#ffb070) */
    {{255, 210, 156}, {255, 140, 70}},
    {{255, 184, 124}, {255, 122, 51}},  /* ember orange (#ff7a33) */
    {{255, 146, 98}, {228, 90, 52}},
    {{232, 106, 82}, {200, 68, 60}},    /* blood red (#c8443c) */
    {{172, 74, 62}, {126, 44, 40}},     /* nearly out */
};
static const double EMBER_STRENGTH[EMBER_HEATS] = {1, 0.95, 0.86, 0.74, 0.6, 0.45};
/* Ash: pale, and charcoal with a rim still smouldering; each its middle and its edge. */
static const double ASH_MIDDLE[ASH_TINTS][3] = {{176, 172, 168}, {74, 70, 68}};
static const double ASH_EDGE[ASH_TINTS][3] = {{128, 124, 120}, {44, 42, 42}};
static const double ASH_RIM[3] = {255, 122, 51};

const char *sh_ambient_kind_name(enum sh_ambient_kind kind) {
    switch (kind) {
    case SH_AMBIENT_SAKURA:
        return "sakura";
    case SH_AMBIENT_SNOW:
        return "snow";
    case SH_AMBIENT_FIREFLIES:
        return "fireflies";
    case SH_AMBIENT_EMBERS:
        return "embers";
    default:
        return "none";
    }
}

int sh_ambient_default_count(enum sh_ambient_kind kind) {
    switch (kind) {
    case SH_AMBIENT_SAKURA:
        return 30;
    case SH_AMBIENT_SNOW:
        return 60;
    case SH_AMBIENT_FIREFLIES:
        return 24;
    case SH_AMBIENT_EMBERS:
        return 40;
    default:
        return 0;
    }
}

int sh_ambient_sprite_count(enum sh_ambient_kind kind) {
    switch (kind) {
    case SH_AMBIENT_SAKURA:
        return PETAL_TINTS * PETAL_SIZES * SH_AMBIENT_TURNS;
    case SH_AMBIENT_SNOW:
        return SNOW_TINTS * SNOW_SIZES;
    case SH_AMBIENT_FIREFLIES:
        return GLOW_TINTS * GLOW_SIZES;
    case SH_AMBIENT_EMBERS:
        return EMBER_GLOWS + ASH_TINTS * ASH_SIZES;
    default:
        return 0;
    }
}

int sh_ambient_petal_sprite(int tint, int size) {
    tint = tint < 0 ? 0 : tint >= PETAL_TINTS ? PETAL_TINTS - 1 : tint;
    size = size < 0 ? 0 : size >= PETAL_SIZES ? PETAL_SIZES - 1 : size;
    return (tint * PETAL_SIZES + size) * SH_AMBIENT_TURNS;
}

void sh_ambient_petal_turn(double turn, int *step, int *quarters) {
    double unit = (M_PI / 2) / SH_AMBIENT_TURNS;
    long steps = isfinite(turn) ? lround(turn / unit) % (4 * SH_AMBIENT_TURNS) : 0;
    if (steps < 0)
        steps += 4 * SH_AMBIENT_TURNS;
    *step = (int)(steps % SH_AMBIENT_TURNS);
    *quarters = (int)(steps / SH_AMBIENT_TURNS);
}

double sh_ambient_sprite_side(enum sh_ambient_kind kind, int sprite) {
    if (sprite < 0 || sprite >= sh_ambient_sprite_count(kind))
        return 0;
    switch (kind) {
    case SH_AMBIENT_SAKURA:
        // Turned any way about its middle, with room for the edge's smoothing.
        return ceil(1.12 * PETAL_LENGTH[sprite / SH_AMBIENT_TURNS % PETAL_SIZES] + 2);
    case SH_AMBIENT_SNOW: {
        double radius = FLAKE_RADIUS[sprite % SNOW_SIZES];
        // The small ones' faint halo reaches twice as far; the soft ones end at their edge.
        return ceil(2 * (sprite % SNOW_SIZES >= SOFT_FLAKES ? radius + 1 : 2 * radius + 1.5));
    }
    case SH_AMBIENT_FIREFLIES:
        return 2 * GLOW_REACH[sprite % GLOW_SIZES];
    case SH_AMBIENT_EMBERS:
        // A flake's ragged edge reaches past its radius, a little longer one way.
        return sprite < EMBER_GLOWS
                   ? 2 * EMBER_REACH[sprite / EMBER_HEATS]
                   : ceil(2 * 1.6 * ASH_RADIUS[(sprite - EMBER_GLOWS) % ASH_SIZES] + 2);
    default:
        return 0;
    }
}

double sh_ambient_sprite_width(enum sh_ambient_kind kind, int sprite) {
    return sh_ambient_sprite_side(kind, sprite);
}

static double clamp01(double value) { return value < 0 ? 0 : value > 1 ? 1 : value; }

static double smoothstep(double edge0, double edge1, double x) {
    double t = clamp01((x - edge0) / (edge1 - edge0));
    return t * t * (3 - 2 * t);
}

static double mix(double a, double b, double t) { return a + (b - a) * t; }

/* A pixel from premultiplied channels, 0 to 1 (the colour may outshine the alpha: added light). */
static uint32_t pack(double r, double g, double b, double a) {
    uint32_t channels[4];
    double values[4] = {a, r, g, b};
    for (int i = 0; i < 4; ++i)
        channels[i] = (uint32_t)lround(clamp01(values[i]) * 255);
    return channels[0] << 24 | channels[1] << 16 | channels[2] << 8 | channels[3];
}

/* Signed distances, negative inside: a circle, and a capsule along y from 0 to `h` whose radius
 * goes from `r1` to `r2` (Inigo Quilez's uneven capsule). */
static double circle(double x, double y, double cx, double cy, double r) {
    return hypot(x - cx, y - cy) - r;
}

static double uneven_capsule(double x, double y, double r1, double r2, double h) {
    x = fabs(x);
    double b = (r1 - r2) / h, a = sqrt(1 - b * b);
    double k = -b * x + a * y;
    if (k < 0)
        return hypot(x, y) - r1;
    if (k > a * h)
        return hypot(x, y - h) - r2;
    return a * x + b * y - r1;
}

/* An ellipse about the origin with radii `rx` and `ry` (an estimate close to its edge). */
static double ellipse(double x, double y, double rx, double ry) {
    double k0 = hypot(x / rx, y / ry), k1 = hypot(x / (rx * rx), y / (ry * ry));
    return k1 > 0 ? k0 * (k0 - 1) / k1 : -fmin(rx, ry);
}

/* The union of two shapes, rounded where they meet over `k`. */
static double smooth_union(double d1, double d2, double k) {
    double h = clamp01(0.5 + 0.5 * (d2 - d1) / k);
    return mix(d2, d1, h) - k * h * (1 - h);
}

/* A sakura petal in units of its length: its stem at a = 0, the notch of its tip near a = 0.9,
 * b across it. Narrow at the stem, widening into two rounded lobes with a notch between them. */
static double petal_distance(double a, double b) {
    double body = smooth_union(uneven_capsule(b, a - 0.08, 0.05, 0.2, 0.42),
                               ellipse(a - 0.56, b, 0.31, 0.29), 0.12);
    double lobes = smooth_union(circle(a, b, 0.66, 0.125, 0.215),
                                circle(a, b, 0.66, -0.125, 0.215), 0.05);
    double d = smooth_union(body, lobes, 0.08);
    double notch = circle(a, b, 0.925, 0, 0.08);
    return fmax(d, -notch);
}
/* Where a petal's middle is along it, about which it turns. */
static const double PETAL_MIDDLE = 0.46;

static void paint_petal(int sprite, uint32_t *pixels, int side) {
    const struct petal_tint *tint = &PETALS[sprite / (PETAL_SIZES * SH_AMBIENT_TURNS)];
    double length = PETAL_LENGTH[sprite / SH_AMBIENT_TURNS % PETAL_SIZES];
    double logical = sh_ambient_sprite_side(SH_AMBIENT_SAKURA, sprite);
    double pixel = logical / side; // a pixel in logical pixels
    double turn = (sprite % SH_AMBIENT_TURNS) * (M_PI / 2) / SH_AMBIENT_TURNS;
    double c = cos(turn), s = sin(turn);
    for (int row = 0; row < side; ++row) {
        for (int column = 0; column < side; ++column) {
            // The pixel's middle from the picture's, turned back into the petal's own frame
            // (y grows downward, so a turn is clockwise on the screen).
            double x = (column + 0.5) * pixel - logical / 2, y = (row + 0.5) * pixel - logical / 2;
            double u = x * c + y * s, v = -x * s + y * c;
            double a = u / length + PETAL_MIDDLE, b = v / length;
            double d = petal_distance(a, b) * length; // logical pixels
            double cover = clamp01(0.5 - d / pixel);
            if (cover <= 0) {
                pixels[row * side + column] = 0;
                continue;
            }
            double along = clamp01(a / 0.9);
            double rgb[3];
            for (int i = 0; i < 3; ++i)
                rgb[i] = mix(tint->stem[i], tint->tip[i], smoothstep(0.05, 0.75, along));
            // A deeper edge, a faint vein down the middle, and one side catching the light.
            double edge = 1 - smoothstep(0, 0.07 * length + pixel, -d);
            double vein = (1 - smoothstep(0, 0.03 * length + 0.5 * pixel, fabs(v))) *
                          smoothstep(0.08, 0.2, a) * (1 - smoothstep(0.45, 0.7, a));
            double light = 1 + 0.06 * clamp01(-b / 0.3) - 0.03 * clamp01(b / 0.3);
            for (int i = 0; i < 3; ++i) {
                rgb[i] = mix(rgb[i], tint->edge[i], 0.45 * edge);
                rgb[i] = mix(rgb[i], tint->edge[i], 0.25 * vein);
                rgb[i] = fmin(rgb[i] * light / 255, 1);
            }
            // A petal is thin: a little of what is behind shows through, more toward its tip.
            double alpha = cover * (0.94 - 0.14 * smoothstep(0.5, 0.95, a));
            pixels[row * side + column] =
                pack(rgb[0] * alpha, rgb[1] * alpha, rgb[2] * alpha, alpha);
        }
    }
}

static void paint_flake(int sprite, uint32_t *pixels, int side) {
    int size = sprite % SNOW_SIZES, tint = sprite / SNOW_SIZES;
    double radius = FLAKE_RADIUS[size];
    double logical = sh_ambient_sprite_side(SH_AMBIENT_SNOW, sprite);
    double pixel = logical / side;
    bool soft = size >= SOFT_FLAKES;
    for (int row = 0; row < side; ++row) {
        for (int column = 0; column < side; ++column) {
            double x = (column + 0.5) * pixel - logical / 2, y = (row + 0.5) * pixel - logical / 2;
            double d = hypot(x, y);
            double alpha;
            if (soft) {
                // Out of focus: a flat disc fading out over its outer half, its rim a touch
                // brighter, as a lens draws a light out of focus.
                alpha = 0.5 * (1 - smoothstep(0.45 * radius, radius, d)) +
                        0.1 * exp(-pow((d - 0.8 * radius) / (0.15 * radius), 2));
            } else {
                // A soft round dot, its edge smoothed over most of its radius, in a faint
                // halo.
                double soften = fmax(pixel, 0.7 * radius);
                alpha = 0.95 * (1 - smoothstep(radius - soften / 2, radius + soften / 2, d)) +
                        0.28 * exp(-pow(fmax(0, d - radius) / (0.8 * radius + 0.6), 2));
            }
            alpha = clamp01(alpha) * (1 - smoothstep(logical / 2 - pixel, logical / 2, d));
            double whiteness = 1 - clamp01(d / radius);
            double rgb[3];
            for (int i = 0; i < 3; ++i)
                rgb[i] = mix(SNOW_EDGE[tint][i], SNOW_MIDDLE[tint][i], whiteness) / 255;
            pixels[row * side + column] =
                alpha > 0 ? pack(rgb[0] * alpha, rgb[1] * alpha, rgb[2] * alpha, alpha) : 0;
        }
    }
}

static void paint_glow(int sprite, uint32_t *pixels, int side) {
    const struct glow_tint *tint = &GLOWS[sprite / GLOW_SIZES];
    double reach = GLOW_REACH[sprite % GLOW_SIZES], core = GLOW_CORE[sprite % GLOW_SIZES];
    double logical = sh_ambient_sprite_side(SH_AMBIENT_FIREFLIES, sprite);
    double pixel = logical / side;
    for (int row = 0; row < side; ++row) {
        for (int column = 0; column < side; ++column) {
            double x = (column + 0.5) * pixel - logical / 2, y = (row + 0.5) * pixel - logical / 2;
            double d = hypot(x, y);
            // A bright core in a warm glow, and a wider, fainter one fading out before the edge.
            double bright = 1 - smoothstep(core - 0.5 * pixel, core + pixel, d);
            double inner = exp(-pow(d / (0.2 * reach), 2));
            double outer = 0.6 * exp(-pow(d / (0.45 * reach), 2));
            double fade = 1 - smoothstep(0.75 * reach, reach, d);
            double light = clamp01(bright + 0.8 * inner + outer) * fade;
            double whiteness = clamp01(bright + 0.35 * inner);
            double rgb[3];
            for (int i = 0; i < 3; ++i)
                rgb[i] = mix(tint->halo[i], tint->core[i], whiteness) / 255 * light;
            // Less alpha than light: the glow adds to what is under it, as light does.
            double alpha = clamp01(bright + 0.55 * (light - bright));
            pixels[row * side + column] = light > 0 ? pack(rgb[0], rgb[1], rgb[2], alpha) : 0;
        }
    }
}

static void paint_ember(int sprite, uint32_t *pixels, int side) {
    int size = sprite / EMBER_HEATS, heat = sprite % EMBER_HEATS;
    const struct glow_tint *tint = &EMBER_TINTS[heat];
    double reach = EMBER_REACH[size], core = EMBER_CORE[size], strength = EMBER_STRENGTH[heat];
    double logical = sh_ambient_sprite_side(SH_AMBIENT_EMBERS, sprite);
    double pixel = logical / side;
    bool soft = size == EMBER_SIZES - 1;
    for (int row = 0; row < side; ++row) {
        for (int column = 0; column < side; ++column) {
            double x = (column + 0.5) * pixel - logical / 2, y = (row + 0.5) * pixel - logical / 2;
            // A spark a little taller than it is wide, drawn out as it rises; near the eye, a
            // soft disc instead, out of focus.
            double d = hypot(x, y / (soft ? 1.08 : 1.25));
            double bright = soft ? 0.85 * (1 - smoothstep(0.35 * core, 1.15 * core, d))
                                 : 1 - smoothstep(core - 0.5 * pixel, core + pixel, d);
            // Its glow, trailing a little below it, fading out before the edge.
            double r = hypot(x, y > 0 ? y / 1.25 : y);
            double inner = 0.9 * exp(-pow(r / (0.2 * reach), 2));
            double outer = 0.6 * exp(-pow(r / (0.42 * reach), 2));
            double fade = 1 - smoothstep(0.55 * reach, 0.8 * reach, r);
            double light = clamp01(bright + strength * (0.8 * inner + outer)) * fade;
            double whiteness = clamp01(bright * (1 - 0.1 * heat) + 0.3 * inner * strength);
            double rgb[3];
            for (int i = 0; i < 3; ++i)
                rgb[i] = mix(tint->halo[i], tint->core[i], whiteness) / 255 * light;
            // Less alpha than light: the glow adds to what is under it, as light does.
            double alpha = clamp01(bright + 0.5 * (light - bright));
            pixels[row * side + column] = light > 0 ? pack(rgb[0], rgb[1], rgb[2], alpha) : 0;
        }
    }
}

static void paint_ash(int sprite, uint32_t *pixels, int side) {
    int tint = (sprite - EMBER_GLOWS) / ASH_SIZES, size = (sprite - EMBER_GLOWS) % ASH_SIZES;
    double radius = ASH_RADIUS[size];
    double logical = sh_ambient_sprite_side(SH_AMBIENT_EMBERS, sprite);
    double pixel = logical / side;
    for (int row = 0; row < side; ++row) {
        for (int column = 0; column < side; ++column) {
            double x = (column + 0.5) * pixel - logical / 2, y = (row + 0.5) * pixel - logical / 2;
            // A torn flake: longer one way, its edge ragged, each grey torn its own way.
            double a = atan2(y, x), r = hypot(x / 1.3, y);
            double edge = radius * (1 + 0.16 * sin(3 * a + 0.7 + 2.1 * tint) +
                                    0.09 * sin(5 * a + 2.3) + 0.05 * sin(9 * a + 1.1 * tint));
            double d = r - edge;
            double cover = clamp01(0.5 - d / (0.7 * pixel + 0.25));
            if (cover <= 0) {
                pixels[row * side + column] = 0;
                continue;
            }
            double inside = clamp01(-d / (0.6 * radius));
            double rgb[3];
            for (int i = 0; i < 3; ++i)
                rgb[i] = mix(ASH_EDGE[tint][i], ASH_MIDDLE[tint][i], inside);
            // Charcoal still smoulders along one side of its edge.
            if (tint == 1) {
                double rim = (1 - smoothstep(0, 0.45 * radius + pixel, -d)) *
                             smoothstep(-0.2, 0.6, cos(a - 2.2));
                for (int i = 0; i < 3; ++i)
                    rgb[i] = mix(rgb[i], ASH_RIM[i], 0.75 * rim);
            }
            double alpha = 0.92 * cover;
            pixels[row * side + column] =
                pack(rgb[0] / 255 * alpha, rgb[1] / 255 * alpha, rgb[2] / 255 * alpha, alpha);
        }
    }
}

void sh_ambient_paint(enum sh_ambient_kind kind, int sprite, uint32_t *pixels, int width,
                      int height) {
    if (width < 1 || height < 1 || sprite < 0 || sprite >= sh_ambient_sprite_count(kind))
        return;
    // The rest are square.
    if (width != height)
        return;
    int side = height;
    if (kind == SH_AMBIENT_SAKURA)
        paint_petal(sprite, pixels, side);
    else if (kind == SH_AMBIENT_SNOW)
        paint_flake(sprite, pixels, side);
    else if (kind == SH_AMBIENT_EMBERS && sprite < EMBER_GLOWS)
        paint_ember(sprite, pixels, side);
    else if (kind == SH_AMBIENT_EMBERS)
        paint_ash(sprite, pixels, side);
    else
        paint_glow(sprite, pixels, side);
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

double sh_ambient_wind(double time) {
    // Mostly a breath to the right, now and then still or turning back a little.
    return 0.006 + 0.008 * sin(2 * M_PI * time / 23000) +
           0.004 * sin(2 * M_PI * time / 9100 + 1.3);
}

/* How far a particle may go past a side before it comes in at the other: out of sight, its
 * sway included. */
static double side_margin(const struct sh_ambient_field *field,
                          const struct sh_ambient_particle *particle) {
    return sh_ambient_sprite_side(field->kind, particle->sprite) * particle->scale / 2 +
           particle->reach;
}

void sh_ambient_spawn(struct sh_ambient_field *field, struct sh_ambient_particle *particle,
                      bool anywhere) {
    uint32_t *seed = &field->seed;
    double width = field->width > 1 ? field->width : 1, height = field->height > 1 ? field->height : 1;
    *particle = (struct sh_ambient_particle){.scale = 1};
    particle->phase = uniform(seed, 0, 2 * M_PI);
    particle->age = anywhere ? 0 : FADE_IN_MS;
    switch (field->kind) {
    case SH_AMBIENT_SAKURA: {
        // More far off than near, as a tree's petals come from all around.
        double depth = particle->depth = pow(uniform(seed, 0, 1), 1.3);
        int size = depth < 0.35 ? 0 : depth < 0.75 ? 1 : 2;
        int pick = (int)(xorshift(seed) % 100), tint = 0;
        while (tint < PETAL_TINTS - 1 && pick >= PETAL_SHARE[tint])
            pick -= PETAL_SHARE[tint++];
        particle->sprite = sh_ambient_petal_sprite(tint, size);
        particle->scale = uniform(seed, 0.88, 1.12);
        particle->fall = (0.016 + 0.022 * depth) * uniform(seed, 0.85, 1.15);
        particle->reach = uniform(seed, 14, 44) * (0.6 + 0.4 * depth);
        particle->rate = 2 * M_PI / uniform(seed, 2600, 5200);
        particle->angle = uniform(seed, 0, 2 * M_PI);
        particle->spin = uniform(seed, 0.00015, 0.0006) * (xorshift(seed) & 1 ? 1 : -1);
        particle->tumble = uniform(seed, 0, 2 * M_PI);
        particle->tumble_rate = 2 * M_PI / uniform(seed, 1400, 3200);
        particle->axis = (int)(xorshift(seed) & 1);
        break;
    }
    case SH_AMBIENT_SNOW: {
        double depth = particle->depth = pow(uniform(seed, 0, 1), 1.5);
        int size = depth < 0.25 ? 0 : depth < 0.45 ? 1 : depth < 0.62 ? 2 : depth < 0.8 ? 3
                   : depth < 0.92 ? 4 : 5;
        particle->sprite = (xorshift(seed) % 4 == 0 ? SNOW_SIZES : 0) + size;
        particle->scale = uniform(seed, 0.9, 1.1);
        particle->fall = (0.012 + 0.038 * depth) * uniform(seed, 0.85, 1.15);
        particle->reach = uniform(seed, 3, 13);
        particle->rate = 2 * M_PI / uniform(seed, 2500, 6000);
        break;
    }
    case SH_AMBIENT_FIREFLIES: {
        double depth = particle->depth = uniform(seed, 0, 1);
        int pick = (int)(xorshift(seed) % 10);
        int tint = pick < 5 ? 0 : pick < 8 ? 1 : 2;
        particle->sprite = tint * GLOW_SIZES + (depth > 0.6 ? 1 : 0);
        particle->scale = uniform(seed, 0.85, 1.15);
        particle->fall = uniform(seed, 0.008, 0.022); // how fast it wanders
        particle->heading = uniform(seed, 0, 2 * M_PI);
        particle->rate = 2 * M_PI / uniform(seed, 1800, 4200);
        particle->reach = uniform(seed, 0.0006, 0.0012); // how sharply it turns, radians/ms
        particle->glow = uniform(seed, 0, 1);
        particle->glow_ms = uniform(seed, 2600, 6000);
        // Anywhere, but not right at an edge.
        particle->x = uniform(seed, 0.05, 0.95) * width;
        particle->y = uniform(seed, 0.08, 0.95) * height;
        return;
    }
    case SH_AMBIENT_EMBERS: {
        // More far off than near; one in seven is a flake of ash, rising slower and swaying
        // wider.
        double depth = particle->depth = pow(uniform(seed, 0, 1), 1.3);
        bool ash = xorshift(seed) % 7 == 0;
        int size = depth < 0.45 ? 0 : depth < 0.82 ? 1 : 2;
        particle->sprite = ash ? EMBER_GLOWS + (int)(xorshift(seed) % ASH_TINTS) * ASH_SIZES +
                                     (depth < 0.6 ? 0 : 1)
                               : size * EMBER_HEATS;
        particle->scale = uniform(seed, 0.85, 1.15);
        particle->fall = (0.022 + 0.04 * depth) * uniform(seed, 0.8, 1.2) * (ash ? 0.6 : 1);
        particle->reach = uniform(seed, 4, 14) * (0.6 + 0.4 * depth) * (ash ? 1.6 : 1);
        particle->rate = 2 * M_PI / uniform(seed, 2200, 4800);
        particle->tumble = uniform(seed, 0, 2 * M_PI);
        particle->angle = uniform(seed, 0, 2 * M_PI);
        if (ash) {
            particle->tumble_rate = 2 * M_PI / uniform(seed, 900, 2200);
            particle->axis = (int)(xorshift(seed) & 1);
        } else {
            // Two quick waves it flickers on.
            particle->tumble_rate = 2 * M_PI / uniform(seed, 140, 380);
            particle->spin = 2 * M_PI / uniform(seed, 300, 900);
        }
        // It burns while it rises through a third of the area to all of it, give or take,
        // slowing as it cools to half the pace it started at.
        particle->glow_ms = uniform(seed, 0.35, 1.05) * height / (0.75 * particle->fall);
        particle->glow = anywhere ? uniform(seed, 0, 0.75) : 0;
        double margin = side_margin(field, particle);
        particle->x = uniform(seed, -margin, width + margin);
        particle->y = anywhere ? uniform(seed, 0.05 * height, height + margin)
                               : height + margin + uniform(seed, 0, 0.08 * height);
        return;
    }
    default:
        return;
    }
    double margin = side_margin(field, particle);
    particle->x = uniform(seed, -margin, width + margin);
    particle->y = anywhere ? uniform(seed, -margin, height)
                           : -margin - uniform(seed, 0, 0.08 * height);
}

/* Turns `heading` toward `goal` by at most `most` radians. */
static double turn_toward(double heading, double goal, double most) {
    double off = remainder(goal - heading, 2 * M_PI);
    return heading + (off > most ? most : off < -most ? -most : off);
}

static void step_firefly(struct sh_ambient_field *field, struct sh_ambient_particle *particle,
                         double dt) {
    double width = field->width, height = field->height;
    particle->phase += particle->rate * dt;
    particle->heading += particle->reach * sin(particle->phase) * dt;
    // Kept off the edges, turning back toward the middle as it nears one.
    double margin = fmin(width, height) * 0.08, wx = 0, wy = 0;
    wx += particle->x < margin ? 1 : particle->x > width - margin ? -1 : 0;
    wy += particle->y < margin ? 1 : particle->y > height - margin ? -1 : 0;
    if (wx || wy)
        particle->heading = turn_toward(particle->heading, atan2(wy, wx), 0.002 * dt);
    particle->heading = remainder(particle->heading, 2 * M_PI);
    particle->x += particle->fall * cos(particle->heading) * dt;
    particle->y += particle->fall * sin(particle->heading) * dt;
    particle->x = fmin(fmax(particle->x, 0), width);
    particle->y = fmin(fmax(particle->y, 0), height);
    particle->glow += dt / particle->glow_ms;
    particle->glow -= floor(particle->glow);
}

/* Moves an ember on: rising on the heat, slower as it cools, and carried further by the wind
 * than a petal is; one gone out or risen out of the top comes in again below the bottom. */
static void step_ember(struct sh_ambient_field *field, struct sh_ambient_particle *particle,
                       double dt, double wind) {
    particle->phase = remainder(particle->phase + particle->rate * dt, 2 * M_PI);
    particle->tumble = remainder(particle->tumble + particle->tumble_rate * dt, 2 * M_PI);
    particle->angle = remainder(particle->angle + particle->spin * dt, 2 * M_PI);
    particle->glow += dt / particle->glow_ms;
    particle->y -= particle->fall * (1 - 0.5 * clamp01(particle->glow)) * dt;
    particle->x += wind * (0.8 + 0.8 * particle->depth) * dt;
    double margin = side_margin(field, particle);
    if (particle->glow >= 1 || particle->y < -margin) {
        sh_ambient_spawn(field, particle, false);
        return;
    }
    double span = field->width + 2 * margin;
    if (particle->x < -margin)
        particle->x += span;
    else if (particle->x > field->width + margin)
        particle->x -= span;
}

void sh_ambient_step(struct sh_ambient_field *field, struct sh_ambient_particle *particles,
                     int count, double dt) {
    if (!(dt > 0))
        return;
    field->time += dt;
    double wind = sh_ambient_wind(field->time);
    for (int i = 0; i < count; ++i) {
        struct sh_ambient_particle *particle = &particles[i];
        particle->age += dt;
        if (field->kind == SH_AMBIENT_FIREFLIES) {
            step_firefly(field, particle, dt);
            continue;
        }
        if (field->kind == SH_AMBIENT_EMBERS) {
            step_ember(field, particle, dt, wind);
            continue;
        }
        particle->phase = remainder(particle->phase + particle->rate * dt, 2 * M_PI);
        particle->y += particle->fall * dt;
        double drift = field->kind == SH_AMBIENT_SAKURA ? 0.5 + 0.7 * particle->depth
                                                        : 0.3 + 0.6 * particle->depth;
        particle->x += wind * drift * dt;
        if (field->kind == SH_AMBIENT_SAKURA) {
            particle->angle = remainder(particle->angle + particle->spin * dt, 2 * M_PI);
            particle->tumble = remainder(particle->tumble + particle->tumble_rate * dt, 2 * M_PI);
        }
        double margin = side_margin(field, particle);
        if (particle->y > field->height + margin) {
            sh_ambient_spawn(field, particle, false);
            continue;
        }
        double span = field->width + 2 * margin;
        if (particle->x < -margin)
            particle->x += span;
        else if (particle->x > field->width + margin)
            particle->x -= span;
    }
}

/* How bright a firefly is the share `glow` into its cycle: lighting up, holding, dimming, then
 * resting dark a while. */
static double firefly_brightness(double glow) {
    if (glow < 0.2)
        return smoothstep(0, 0.2, glow);
    if (glow < 0.45)
        return 1;
    return 1 - smoothstep(0.45, 0.75, glow);
}

void sh_ambient_look(const struct sh_ambient_field *field,
                     const struct sh_ambient_particle *particle, struct sh_ambient_look *look) {
    double side = sh_ambient_sprite_side(field->kind, particle->sprite) * particle->scale;
    double fade = smoothstep(0, FADE_IN_MS, particle->age);
    double x = particle->x, y = particle->y, height = side;
    double width = sh_ambient_sprite_width(field->kind, particle->sprite) * particle->scale;
    *look = (struct sh_ambient_look){.sprite = particle->sprite};
    switch (field->kind) {
    case SH_AMBIENT_SAKURA: {
        x += particle->reach * sin(particle->phase);
        // Leaning into its swing, and narrowed as it tumbles; past edge on, its other face
        // shows, which is the petal mirrored across the axis it tumbles about.
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
        look->opacity = (0.75 + 0.25 * particle->depth) * (0.85 + 0.15 * squash);
        break;
    }
    case SH_AMBIENT_SNOW:
        x += particle->reach * sin(particle->phase);
        look->opacity = (0.6 + 0.4 * particle->depth) *
                        (particle->sprite % SNOW_SIZES >= SOFT_FLAKES ? 0.8 : 1);
        break;
    case SH_AMBIENT_FIREFLIES: {
        double bright = firefly_brightness(particle->glow);
        y += 3 * sin(2 * particle->phase); // bobbing as it flies
        width = height = side * (0.75 + 0.35 * bright);
        look->opacity = (0.15 + 0.85 * bright) * (0.75 + 0.25 * particle->depth);
        break;
    }
    case SH_AMBIENT_EMBERS: {
        x += particle->reach * sin(particle->phase);
        double life = clamp01(particle->glow);
        // Going out over the last of its life.
        double out = 1 - smoothstep(0.55, 1, life);
        if (particle->sprite >= EMBER_GLOWS) {
            // A flake of ash tumbles, narrowed as it turns edge on; it keeps its quarter turn.
            double squash = 0.25 + 0.75 * pow(fabs(cos(particle->tumble)), 0.7);
            look->quarters = (int)floor(particle->angle / (M_PI / 2)) & 3;
            if (particle->axis == 0)
                width *= squash;
            else
                height *= squash;
            look->opacity = (0.55 + 0.35 * particle->depth) * out;
            break;
        }
        // Cooling through its heats as its life goes, shrinking a little, and flickering on two
        // quick waves, more while it is hot.
        int heat = (int)(life * EMBER_HEATS);
        look->sprite = particle->sprite + (heat < EMBER_HEATS ? heat : EMBER_HEATS - 1);
        double flicker = 0.5 + 0.3 * sin(particle->tumble) + 0.2 * sin(particle->angle);
        double lively = 0.25 + 0.2 * (1 - life);
        width = height = side * (1 - 0.3 * life) * (0.85 + 0.15 * flicker);
        look->opacity = (0.75 + 0.25 * particle->depth) * (1 - lively + lively * flicker) * out;
        break;
    }
    default:
        break;
    }
    look->opacity = clamp01(look->opacity * fade);
    look->width = width;
    look->height = height;
    look->x = x - width / 2;
    look->y = y - height / 2;
}
