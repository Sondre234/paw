// SPDX-License-Identifier: GPL-3.0-or-later
/* Ambient particles' arithmetic and pictures: particles stay in and about their area however
 * long they drift, petals and flakes fall and come in again at the top, fireflies wander and
 * glow, what is drawn stays in bounds, and every picture paints something, its corners clear. */
#include "paw/ambient.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"

enum { COUNT = 60 };
static const double WIDTH = 1920, HEIGHT = 1080, TICK = 33;

static void test_names(void) {
    CHECK(!strcmp(sh_ambient_kind_name(SH_AMBIENT_NONE), "none"));
    CHECK(!strcmp(sh_ambient_kind_name(SH_AMBIENT_SAKURA), "sakura"));
    CHECK(!strcmp(sh_ambient_kind_name(SH_AMBIENT_SNOW), "snow"));
    CHECK(!strcmp(sh_ambient_kind_name(SH_AMBIENT_FIREFLIES), "fireflies"));
    CHECK(sh_ambient_sprite_count(SH_AMBIENT_NONE) == 0);
    CHECK(sh_ambient_sprite_side(SH_AMBIENT_SNOW, -1) == 0);
    CHECK(sh_ambient_sprite_side(SH_AMBIENT_SNOW, sh_ambient_sprite_count(SH_AMBIENT_SNOW)) == 0);
}

/* Drifts COUNT particles of `kind` for `minutes`, checking where each is and how it is drawn at
 * every tick. */
static void drift(enum sh_ambient_kind kind, double minutes) {
    struct sh_ambient_field field = {.kind = kind, .width = WIDTH, .height = HEIGHT, .seed = 7};
    struct sh_ambient_particle particles[COUNT];
    double largest = 0;
    for (int i = 0; i < sh_ambient_sprite_count(kind); ++i)
        largest = fmax(largest, sh_ambient_sprite_side(kind, i));
    double start_y = 0;
    for (int i = 0; i < COUNT; ++i) {
        sh_ambient_spawn(&field, &particles[i], true);
        start_y += particles[i].y;
        CHECK(particles[i].x >= -largest - 50 && particles[i].x <= WIDTH + largest + 50);
        CHECK(particles[i].y >= -largest - 50 && particles[i].y <= HEIGHT);
    }
    // Faded out as they turn up anywhere, and in a second and a half later.
    struct sh_ambient_look look;
    sh_ambient_look(&field, &particles[0], &look);
    CHECK(look.opacity == 0, "%s: fading in from nothing: %g", sh_ambient_kind_name(kind),
          look.opacity);
    int respawned = 0, fainter = 0, brighter = 0;
    double moved = 0, lowest_opacity = 1, highest_opacity = 0;
    for (double t = 0; t < minutes * 60000; t += TICK) {
        double before[COUNT];
        for (int i = 0; i < COUNT; ++i)
            before[i] = particles[i].y;
        sh_ambient_step(&field, particles, COUNT, TICK);
        for (int i = 0; i < COUNT; ++i) {
            const struct sh_ambient_particle *p = &particles[i];
            if (kind == SH_AMBIENT_FIREFLIES) {
                CHECK(p->x >= 0 && p->x <= WIDTH && p->y >= 0 && p->y <= HEIGHT,
                      "a firefly stays inside: %g %g", p->x, p->y);
            } else {
                double margin = largest + 60;
                CHECK(p->x >= -margin && p->x <= WIDTH + margin, "%s x %g at %g",
                      sh_ambient_kind_name(kind), p->x, t);
                CHECK(p->y >= -margin - 0.1 * HEIGHT && p->y <= HEIGHT + margin, "%s y %g",
                      sh_ambient_kind_name(kind), p->y);
                if (p->y < before[i] - 1)
                    ++respawned; // back at the top
                else
                    CHECK(p->y >= before[i], "falls, never rises: %g to %g", before[i], p->y);
            }
            moved += fabs(p->y - before[i]);
            sh_ambient_look(&field, p, &look);
            CHECK(look.sprite >= 0 && look.sprite < sh_ambient_sprite_count(kind), "%d",
                  look.sprite);
            CHECK(look.quarters >= 0 && look.quarters < 4, "%d", look.quarters);
            CHECK(look.width > 0 && look.width <= largest * 1.2, "%g", look.width);
            CHECK(look.height > 0 && look.height <= largest * 1.2, "%g", look.height);
            CHECK(look.opacity >= 0 && look.opacity <= 1, "%g", look.opacity);
            // Drawn about where it is.
            CHECK(fabs(look.x + look.width / 2 - p->x) <= 50 &&
                      fabs(look.y + look.height / 2 - p->y) <= 5,
                  "drawn at %g %g for %g %g", look.x, look.y, p->x, p->y);
            if (t > 2000) {
                lowest_opacity = fmin(lowest_opacity, look.opacity);
                highest_opacity = fmax(highest_opacity, look.opacity);
                fainter += look.opacity < 0.3;
                brighter += look.opacity > 0.6;
            }
        }
    }
    const char *name = sh_ambient_kind_name(kind);
    CHECK(moved > COUNT * 100, "%s: they move: %g", name, moved);
    CHECK(highest_opacity > 0.6, "%s: they show: %g", name, highest_opacity);
    if (kind == SH_AMBIENT_FIREFLIES) {
        CHECK(fainter > 0 && brighter > 0, "fireflies glow and dim: %d %d", fainter, brighter);
    } else {
        // Each falls the height of the area in well under a few minutes and comes in again.
        CHECK(respawned >= COUNT, "%s: they fall through and come in at the top: %d", name,
              respawned);
        CHECK(lowest_opacity > 0.3, "%s: never fade out falling: %g", name, lowest_opacity);
    }
    (void)start_y;
}

static void test_motion(void) {
    drift(SH_AMBIENT_SAKURA, 10);
    drift(SH_AMBIENT_SNOW, 10);
    drift(SH_AMBIENT_FIREFLIES, 10);
}

/* The same seed, the same snowfall; a step of nothing changes nothing. */
static void test_repeatable(void) {
    struct sh_ambient_field a = {.kind = SH_AMBIENT_SAKURA, .width = 800, .height = 600,
                                 .seed = 42};
    struct sh_ambient_field b = a;
    struct sh_ambient_particle pa[8], pb[8];
    for (int i = 0; i < 8; ++i) {
        sh_ambient_spawn(&a, &pa[i], true);
        sh_ambient_spawn(&b, &pb[i], true);
    }
    for (int k = 0; k < 500; ++k) {
        sh_ambient_step(&a, pa, 8, TICK);
        sh_ambient_step(&b, pb, 8, TICK);
    }
    for (int i = 0; i < 8; ++i)
        CHECK(pa[i].x == pb[i].x && pa[i].y == pb[i].y);
    struct sh_ambient_particle still = pa[0];
    sh_ambient_step(&a, pa, 1, 0);
    CHECK(still.x == pa[0].x && still.y == pa[0].y);
    // The wind blows gently, mostly to the right.
    double sum = 0;
    for (double t = 0; t < 600000; t += 100) {
        double wind = sh_ambient_wind(t);
        CHECK(fabs(wind) < 0.03, "%g", wind);
        sum += wind;
    }
    CHECK(sum > 0);
}

/* The alpha-weighted spread of a picture: along x, along y, and together. */
static void spread(const uint32_t *pixels, int side, double *xx, double *yy, double *xy) {
    double total = 0, mx = 0, my = 0;
    for (int i = 0; i < side * side; ++i) {
        double a = pixels[i] >> 24;
        total += a, mx += a * (i % side), my += a * (i / side);
    }
    mx /= total, my /= total;
    *xx = *yy = *xy = 0;
    for (int i = 0; i < side * side; ++i) {
        double a = pixels[i] >> 24, dx = i % side - mx, dy = i / side - my;
        *xx += a * dx * dx, *yy += a * dy * dy, *xy += a * dx * dy;
    }
}

static void test_paint(void) {
    for (int kind = SH_AMBIENT_SAKURA; kind < SH_AMBIENT_KINDS; ++kind) {
        for (int sprite = 0; sprite < sh_ambient_sprite_count(kind); ++sprite) {
            for (double scale = 1; scale <= 2; scale += 0.5) {
                int side = (int)lround(sh_ambient_sprite_side(kind, sprite) * scale);
                CHECK(side >= 4 && side <= 80, "%d", side);
                uint32_t *pixels = malloc((size_t)side * side * sizeof(*pixels));
                for (int i = 0; i < side * side; ++i)
                    pixels[i] = 0xdeadbeef;
                sh_ambient_paint(kind, sprite, pixels, side);
                int shown = 0, solid = 0;
                for (int i = 0; i < side * side; ++i) {
                    uint32_t a = pixels[i] >> 24, r = pixels[i] >> 16 & 0xff;
                    uint32_t g = pixels[i] >> 8 & 0xff, b = pixels[i] & 0xff;
                    shown += a > 8;
                    solid += a > 200;
                    if (kind != SH_AMBIENT_FIREFLIES)
                        CHECK(r <= a && g <= a && b <= a, "premultiplied: %08x", pixels[i]);
                    if (!a && kind != SH_AMBIENT_FIREFLIES)
                        CHECK(!pixels[i], "clear: %08x", pixels[i]);
                }
                CHECK(shown >= 3, "%s %d at %g paints something: %d", sh_ambient_kind_name(kind),
                      sprite, scale, shown);
                CHECK(shown < side * side * 0.8, "with room around it: %d of %d", shown,
                      side * side);
                if (kind == SH_AMBIENT_SAKURA)
                    CHECK(solid >= 4, "a petal is nearly opaque: %d", solid);
                for (int corner = 0; corner < 4; ++corner) {
                    int i = (corner & 1 ? side - 1 : 0) + (corner & 2 ? side - 1 : 0) * side;
                    CHECK(pixels[i] >> 24 < 4, "corners clear: %08x", pixels[i]);
                }
                free(pixels);
            }
        }
    }
    // A petal of the first turn lies along x; half way through a quarter turn, clockwise on
    // the screen (y down), it points to the bottom right.
    int side = 40;
    uint32_t pixels[40 * 40];
    double xx, yy, xy;
    sh_ambient_paint(SH_AMBIENT_SAKURA, 0, pixels, side);
    spread(pixels, side, &xx, &yy, &xy);
    CHECK(xx > 1.2 * yy && fabs(xy) < 0.1 * xx, "along x: %g %g %g", xx, yy, xy);
    sh_ambient_paint(SH_AMBIENT_SAKURA, SH_AMBIENT_TURNS / 2, pixels, side);
    spread(pixels, side, &xx, &yy, &xy);
    CHECK(xy > 0.1 * xx, "turned clockwise: %g %g %g", xx, yy, xy);
    // A firefly's glow adds light: brighter than its alpha around its core.
    int glow = 0;
    sh_ambient_paint(SH_AMBIENT_FIREFLIES, 0, pixels, 24);
    for (int i = 0; i < 24 * 24; ++i)
        glow += (pixels[i] >> 16 & 0xff) > (pixels[i] >> 24) + 10;
    CHECK(glow > 20, "%d", glow);
}

int main(void) {
    test_names();
    test_motion();
    test_repeatable();
    test_paint();
    if (failures)
        fprintf(stderr, "%d checks failed\n", failures);
    return failures != 0;
}
