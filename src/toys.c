/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "paw/toys.h"
#include <math.h>
#include <stddef.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* A shake swings this many times to either side and back, so that it passes through 0 as it
 * ends, dying away to a twentieth of its first swing meanwhile. */
static const double SHAKE_SWINGS = 4, SHAKE_DECAY_MS = 135;

double sh_shake_offset(double t, double amplitude) {
    if (!(t > 0) || t >= SH_SHAKE_MS)
        return 0;
    double omega = 2 * M_PI * SHAKE_SWINGS / SH_SHAKE_MS;
    return amplitude * sin(omega * t) * exp(-t / SHAKE_DECAY_MS);
}

/* A bounce rising less than this is not drawn: the window rests. */
static const double LEAST_BOUNCE = 1;
/* A touch at this speed (pixels per millisecond) or faster squashes the window all the way. */
static const double SQUASH_SPEED = 2.5;

void sh_fall_plan(struct sh_fall *fall, double height, double gravity, double restitution) {
    *fall = (struct sh_fall){.height = height, .gravity = gravity};
    if (!(height > 0) || !(gravity > 0))
        return;
    double t = sqrt(2 * height / gravity), speed = gravity * t;
    fall->touch_at[0] = t;
    fall->speed[0] = speed;
    fall->touches = 1;
    while (fall->touches <= SH_FALL_BOUNCES) {
        speed *= restitution;
        if (!(speed * speed / (2 * gravity) >= LEAST_BOUNCE))
            break;
        t += 2 * speed / gravity; // up and down again
        fall->touch_at[fall->touches] = t;
        fall->speed[fall->touches] = speed;
        ++fall->touches;
    }
    fall->landed = t;
}

double sh_fall_drop(const struct sh_fall *fall, double t) {
    if (!fall->touches || !(t > 0))
        return 0;
    if (t >= fall->landed)
        return fall->height;
    if (t < fall->touch_at[0])
        return fall->gravity * t * t / 2;
    int k = 1;
    while (k < fall->touches - 1 && t >= fall->touch_at[k])
        ++k;
    // Bouncing up from the touch before, at the speed it comes down at again.
    double since = t - fall->touch_at[k - 1];
    double up = fall->speed[k] * since - fall->gravity * since * since / 2;
    return fall->height - fmax(0, up);
}

double sh_fall_squash(const struct sh_fall *fall, double t) {
    int k = -1;
    for (int i = 0; i < fall->touches; ++i)
        if (t >= fall->touch_at[i])
            k = i;
    if (k < 0)
        return 0;
    double since = t - fall->touch_at[k];
    if (since >= SH_SQUASH_MS)
        return 0;
    return SH_SQUASH * fmin(1, fall->speed[k] / SQUASH_SPEED) * sin(M_PI * since / SH_SQUASH_MS);
}

static bool across(const struct sh_toy_box *a, const struct sh_toy_box *b) {
    return a->x < b->x + b->width && b->x < a->x + a->width;
}

void sh_pile(const struct sh_toy_box *boxes, int count, double floor, double least,
             double *drops) {
    for (int i = 0; i < count; ++i)
        drops[i] = -1; // not landed yet
    for (int placed = 0; placed < count; ++placed) {
        // The lowest still to land (the first of those as low).
        int next = -1;
        for (int i = 0; i < count; ++i) {
            if (drops[i] >= 0)
                continue;
            if (next < 0 || boxes[i].y + boxes[i].height > boxes[next].y + boxes[next].height)
                next = i;
        }
        const struct sh_toy_box *box = &boxes[next];
        double bottom = box->y + box->height;
        double land = fmax(floor, bottom + least); // on the floor, or sunk into it
        for (int j = 0; j < count; ++j) {
            if (drops[j] < 0 || j == next || !across(box, &boxes[j]))
                continue;
            double top = boxes[j].y + drops[j];
            if (top >= bottom && top < land)
                land = top;
        }
        drops[next] = fmax(0, land - bottom);
    }
}

/* A party's bob comes in over this long and dies down over the last stretch. */
static const double BOB_IN_MS = 300, BOB_OUT_MS = 600;
/* Up and down, and from side to side: how far and how long a swing there and back takes. */
static const double BOB_HEIGHT = 9, BOB_PERIOD_MS = 760, WIGGLE_WIDTH = 5,
                    WIGGLE_PERIOD_MS = 1180;

/* 0 to 1 and back to 0 smoothly over [0, 1]: no jump in speed at either end. */
static double smoothstep(double u) {
    u = u < 0 ? 0 : u > 1 ? 1 : u;
    return u * u * (3 - 2 * u);
}

void sh_party_bob(double t, double phase, double *dx, double *dy) {
    *dx = *dy = 0;
    if (!(t > 0) || t >= SH_PARTY_MS)
        return;
    double envelope = smoothstep(t / BOB_IN_MS) * smoothstep((SH_PARTY_MS - t) / BOB_OUT_MS);
    *dy = envelope * BOB_HEIGHT * sin(2 * M_PI * t / BOB_PERIOD_MS + phase);
    *dx = envelope * WIGGLE_WIDTH * sin(2 * M_PI * t / WIGGLE_PERIOD_MS + 1.7 * phase);
}

void sh_rainbow(double hue, float rgba[4]) {
    const double saturation = 0.75;
    double h = 6 * (hue - floor(hue)); // which sixth of the way round, and how far into it
    double f = h - floor(h);
    double low = 1 - saturation, falling = 1 - saturation * f,
           rising = 1 - saturation * (1 - f);
    double rgb[6][3] = {{1, rising, low},  {falling, 1, low}, {low, 1, rising},
                        {low, falling, 1}, {rising, low, 1},  {1, low, falling}};
    int sixth = (int)h % 6;
    for (int i = 0; i < 3; ++i)
        rgba[i] = (float)rgb[sixth][i];
    rgba[3] = 1;
}

/* Confetti falls under this gravity (pixels per millisecond squared), held back by the air in
 * proportion to its speed, so that it falls at most SH_CONFETTI_FALL; it sways from side to side
 * as paper does. */
static const double CONFETTI_GRAVITY = 0.0009, CONFETTI_DRAG = CONFETTI_GRAVITY / SH_CONFETTI_FALL;
static const double SWAY_SPEED = 0.06, SWAY_RATE = 0.005;

/* Bright, festive, opaque. */
static const float PALETTE[][3] = {
    {1, 0.30F, 0.43F},    {1, 0.65F, 0.17F},    {1, 0.85F, 0.24F}, {0.42F, 0.80F, 0.47F},
    {0.30F, 0.59F, 1},    {0.61F, 0.37F, 0.90F}, {0.95F, 0.36F, 0.71F},
};

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

void sh_confetti_spawn(struct sh_confetto *piece, uint32_t *seed, double x, double y,
                       double width, double life) {
    *piece = (struct sh_confetto){0};
    piece->x = x + uniform(seed, 0, width);
    piece->y = y - uniform(seed, 0, 24); // just above the top, coming in
    piece->vx = uniform(seed, -0.18, 0.18);
    piece->vy = uniform(seed, 0.02, 0.3);
    piece->spin = uniform(seed, 0, 2 * M_PI);
    piece->spin_rate = uniform(seed, 0.006, 0.02) * (xorshift(seed) & 1 ? 1 : -1);
    piece->sway = uniform(seed, 0, 2 * M_PI);
    piece->life = life * uniform(seed, 0.7, 1);
    const float *color = PALETTE[xorshift(seed) % (sizeof(PALETTE) / sizeof(*PALETTE))];
    for (int i = 0; i < 3; ++i)
        piece->color[i] = color[i];
    piece->color[3] = 1;
    piece->width = 7 + (int)(xorshift(seed) % 5);
    piece->height = 4 + (int)(xorshift(seed) % 3);
}

bool sh_confetti_step(struct sh_confetto *piece, double dt) {
    if (!(dt > 0))
        return piece->age < piece->life;
    // The air takes away a share of the speed, gravity adds to it: the speed tends to what
    // gravity and the air leave, SH_CONFETTI_FALL, from either side.
    double keep = exp(-CONFETTI_DRAG * dt);
    piece->vy = (piece->vy + CONFETTI_GRAVITY * dt) * keep;
    piece->vx *= keep;
    piece->x += (piece->vx + SWAY_SPEED * sin(piece->sway + piece->age * SWAY_RATE)) * dt;
    piece->y += piece->vy * dt;
    piece->spin += piece->spin_rate * dt;
    piece->age += dt;
    return piece->age < piece->life;
}

float sh_confetti_alpha(const struct sh_confetto *piece) {
    double fading = piece->life / 3;
    if (!(fading > 0))
        return 0;
    double left = piece->life - piece->age;
    return (float)(left >= fading ? 1 : left <= 0 ? 0 : left / fading);
}

int sh_confetti_width(const struct sh_confetto *piece) {
    int width = (int)lround(piece->width * fabs(cos(piece->spin)));
    return width > 1 ? width : 1;
}
