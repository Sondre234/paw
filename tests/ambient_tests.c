// SPDX-License-Identifier: GPL-3.0-or-later
/* Ambient particles' arithmetic and pictures: particles stay in and about their area however
 * long they drift, petals and flakes fall and come in again at the top, fireflies wander and
 * glow, embers rise, cool and go out and come in again at the bottom, rain falls fast along its
 * streaks' slant, what is drawn stays in bounds, and every picture paints something, its corners
 * clear. */
#include "paw/ambient.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum { COUNT = 60 };
static const double WIDTH = 1920, HEIGHT = 1080, TICK = 33;

static void test_names(void) {
    CHECK(!strcmp(sh_ambient_kind_name(SH_AMBIENT_NONE), "none"));
    CHECK(!strcmp(sh_ambient_kind_name(SH_AMBIENT_SAKURA), "sakura"));
    CHECK(!strcmp(sh_ambient_kind_name(SH_AMBIENT_SNOW), "snow"));
    CHECK(!strcmp(sh_ambient_kind_name(SH_AMBIENT_FIREFLIES), "fireflies"));
    CHECK(!strcmp(sh_ambient_kind_name(SH_AMBIENT_EMBERS), "embers"));
    CHECK(!strcmp(sh_ambient_kind_name(SH_AMBIENT_RAIN), "rain"));
    CHECK(!strcmp(sh_ambient_kind_name(SH_AMBIENT_KINDS), "none"));
    CHECK(sh_ambient_sprite_count(SH_AMBIENT_NONE) == 0);
    CHECK(sh_ambient_default_count(SH_AMBIENT_NONE) == 0);
    for (int kind = SH_AMBIENT_SAKURA; kind < SH_AMBIENT_KINDS; ++kind)
        CHECK(sh_ambient_default_count(kind) >= 10 && sh_ambient_default_count(kind) <= 150);
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
            } else if (kind == SH_AMBIENT_EMBERS) {
                double margin = largest + 60;
                CHECK(p->x >= -margin && p->x <= WIDTH + margin, "embers x %g at %g", p->x, t);
                CHECK(p->y >= -margin && p->y <= HEIGHT + margin + 0.1 * HEIGHT, "embers y %g",
                      p->y);
                if (p->y > before[i] + 1)
                    ++respawned; // back below the bottom
                else
                    CHECK(p->y <= before[i], "rises, never falls: %g to %g", before[i], p->y);
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
    // Rain is faint by design.
    bool rain = kind == SH_AMBIENT_RAIN;
    CHECK(moved > COUNT * 100, "%s: they move: %g", name, moved);
    CHECK(highest_opacity > (rain ? 0.45 : 0.6), "%s: they show: %g", name, highest_opacity);
    if (kind == SH_AMBIENT_FIREFLIES) {
        CHECK(fainter > 0 && brighter > 0, "fireflies glow and dim: %d %d", fainter, brighter);
    } else if (kind == SH_AMBIENT_EMBERS) {
        // Each goes out, or rises out of the top, and comes in again from below; some glow
        // bright while others are going out.
        CHECK(respawned >= COUNT, "embers come in again at the bottom: %d", respawned);
        CHECK(fainter > 0 && brighter > 0, "embers glow and go out: %d %d", fainter, brighter);
    } else {
        // Each falls the height of the area in well under a few minutes and comes in again.
        CHECK(respawned >= COUNT, "%s: they fall through and come in at the top: %d", name,
              respawned);
        CHECK(lowest_opacity > (rain ? 0.2 : 0.3), "%s: never fade out falling: %g", name,
              lowest_opacity);
    }
    (void)start_y;
}

static void test_motion(void) {
    drift(SH_AMBIENT_SAKURA, 10);
    drift(SH_AMBIENT_SNOW, 10);
    drift(SH_AMBIENT_FIREFLIES, 10);
    drift(SH_AMBIENT_EMBERS, 10);
    drift(SH_AMBIENT_RAIN, 10);
}

/* An ember comes in below the bottom at its hottest, rises a third of the area or more as it
 * cools through its heats, never warming again, and has gone out by the time it comes in again;
 * a flake of ash among them keeps its picture. */
static void test_embers(void) {
    struct sh_ambient_field field = {.kind = SH_AMBIENT_EMBERS, .width = WIDTH,
                                     .height = HEIGHT, .seed = 3};
    int sprites = sh_ambient_sprite_count(SH_AMBIENT_EMBERS), embers = 0, ashes = 0;
    for (int n = 0; n < 40; ++n) {
        struct sh_ambient_particle p;
        sh_ambient_spawn(&field, &p, false);
        CHECK(p.y > HEIGHT, "comes in below the bottom: %g", p.y);
        struct sh_ambient_look look;
        sh_ambient_look(&field, &p, &look);
        // The last four pictures are ash (paw/ambient.h).
        bool ash = p.sprite >= sprites - 4;
        ashes += ash;
        embers += !ash;
        CHECK(look.sprite == p.sprite && look.opacity > 0.3, "hot as it comes in: %d %g",
              look.sprite, look.opacity);
        double start = p.y, opacity = look.opacity;
        int first = p.sprite, coolest = look.sprite;
        bool back = false;
        for (int k = 0; k < 200000 && !back; ++k) {
            struct sh_ambient_particle was = p;
            sh_ambient_step(&field, &p, 1, TICK);
            if (p.y > was.y + 1) {
                back = true;
                CHECK(opacity < 0.02, "gone out before it comes in again: %g", opacity);
                CHECK(start - was.y > 0.3 * HEIGHT, "rose %g", start - was.y);
                break;
            }
            sh_ambient_look(&field, &p, &look);
            CHECK(look.sprite >= coolest && (!ash || look.sprite == p.sprite),
                  "cools, never warming: %d after %d", look.sprite, coolest);
            coolest = look.sprite;
            opacity = look.opacity;
        }
        CHECK(back, "comes in again");
        if (!ash)
            CHECK(coolest == first + 5, "through its six heats: %d from %d", coolest, first);
    }
    CHECK(embers > 25 && ashes > 1, "embers among ash: %d, %d", embers, ashes);
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

/* The alpha-weighted spread of a picture `width` pixels wide: along x, along y, and together. */
static void spread(const uint32_t *pixels, int width, int height, double *xx, double *yy,
                   double *xy) {
    double total = 0, mx = 0, my = 0;
    for (int i = 0; i < width * height; ++i) {
        double a = pixels[i] >> 24;
        total += a, mx += a * (i % width), my += a * (i / width);
    }
    mx /= total, my /= total;
    *xx = *yy = *xy = 0;
    for (int i = 0; i < width * height; ++i) {
        double a = pixels[i] >> 24, dx = i % width - mx, dy = i / width - my;
        *xx += a * dx * dx, *yy += a * dy * dy, *xy += a * dx * dy;
    }
}

static void test_paint(void) {
    for (int kind = SH_AMBIENT_SAKURA; kind < SH_AMBIENT_KINDS; ++kind) {
        // A firefly's glow and an ember's add light, brighter than their alpha.
        bool glows = kind == SH_AMBIENT_FIREFLIES || kind == SH_AMBIENT_EMBERS;
        for (int sprite = 0; sprite < sh_ambient_sprite_count(kind); ++sprite) {
            CHECK(sh_ambient_sprite_width(kind, sprite) <= sh_ambient_sprite_side(kind, sprite));
            for (double scale = 1; scale <= 2; scale += 0.5) {
                int side = (int)lround(sh_ambient_sprite_side(kind, sprite) * scale);
                int width = (int)lround(sh_ambient_sprite_width(kind, sprite) * scale);
                CHECK(side >= 4 && side <= 128 && width >= 4, "%d %d", width, side);
                int area = width * side;
                uint32_t *pixels = malloc((size_t)area * sizeof(*pixels));
                for (int i = 0; i < area; ++i)
                    pixels[i] = 0xdeadbeef;
                sh_ambient_paint(kind, sprite, pixels, width, side);
                int shown = 0, solid = 0;
                for (int i = 0; i < area; ++i) {
                    uint32_t a = pixels[i] >> 24, r = pixels[i] >> 16 & 0xff;
                    uint32_t g = pixels[i] >> 8 & 0xff, b = pixels[i] & 0xff;
                    shown += a > 8;
                    solid += a > 200;
                    if (!glows)
                        CHECK(r <= a && g <= a && b <= a, "premultiplied: %08x", pixels[i]);
                    if (!a && !glows)
                        CHECK(!pixels[i], "clear: %08x", pixels[i]);
                }
                CHECK(shown >= 3, "%s %d at %g paints something: %d", sh_ambient_kind_name(kind),
                      sprite, scale, shown);
                CHECK(shown < area * 0.8, "with room around it: %d of %d", shown, area);
                if (kind == SH_AMBIENT_SAKURA)
                    CHECK(solid >= 4, "a petal is nearly opaque: %d", solid);
                for (int corner = 0; corner < 4; ++corner) {
                    int i = (corner & 1 ? width - 1 : 0) + (corner & 2 ? side - 1 : 0) * width;
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
    sh_ambient_paint(SH_AMBIENT_SAKURA, 0, pixels, side, side);
    spread(pixels, side, side, &xx, &yy, &xy);
    CHECK(xx > 1.2 * yy && fabs(xy) < 0.1 * xx, "along x: %g %g %g", xx, yy, xy);
    sh_ambient_paint(SH_AMBIENT_SAKURA, SH_AMBIENT_TURNS / 2, pixels, side, side);
    spread(pixels, side, side, &xx, &yy, &xy);
    CHECK(xy > 0.1 * xx, "turned clockwise: %g %g %g", xx, yy, xy);
    // A square picture asked for at another shape paints nothing rather than past its pixels.
    for (int i = 0; i < 40 * 40; ++i)
        pixels[i] = 0xdeadbeef;
    sh_ambient_paint(SH_AMBIENT_SNOW, 0, pixels, 12, 30);
    CHECK(pixels[0] == 0xdeadbeef && pixels[12 * 30 - 1] == 0xdeadbeef);
    // A firefly's glow adds light: brighter than its alpha around its core.
    int glow = 0;
    sh_ambient_paint(SH_AMBIENT_FIREFLIES, 0, pixels, 24, 24);
    for (int i = 0; i < 24 * 24; ++i)
        glow += (pixels[i] >> 16 & 0xff) > (pixels[i] >> 24) + 10;
    CHECK(glow > 20, "%d", glow);
    // So does an ember's, its hottest picture paler in its middle than its coolest, which is
    // still red; a flake of ash adds none.
    glow = 0;
    sh_ambient_paint(SH_AMBIENT_EMBERS, 0, pixels, 24, 24);
    for (int i = 0; i < 24 * 24; ++i)
        glow += (pixels[i] >> 16 & 0xff) > (pixels[i] >> 24) + 10;
    CHECK(glow > 20, "%d", glow);
    uint32_t hot = pixels[12 * 24 + 12];
    sh_ambient_paint(SH_AMBIENT_EMBERS, 5, pixels, 24, 24);
    uint32_t cool = pixels[12 * 24 + 12];
    CHECK((hot >> 8 & 0xff) > (cool >> 8 & 0xff) + 60 && (cool >> 16 & 0xff) > 2 * (cool >> 8 & 0xff),
          "%08x %08x", hot, cool);
    int ash = sh_ambient_sprite_count(SH_AMBIENT_EMBERS) - 1;
    side = (int)lround(sh_ambient_sprite_side(SH_AMBIENT_EMBERS, ash) * 2);
    CHECK(side <= 40, "%d", side);
    sh_ambient_paint(SH_AMBIENT_EMBERS, ash, pixels, side, side);
    for (int i = 0; i < side * side; ++i) {
        uint32_t a = pixels[i] >> 24;
        CHECK((pixels[i] >> 16 & 0xff) <= a && (pixels[i] >> 8 & 0xff) <= a &&
                  (pixels[i] & 0xff) <= a,
              "ash adds no light: %08x", pixels[i]);
    }
}

/* A petal's turn picks one of its pictures and the quarter turns the screen adds, whichever way
 * and however far it turned. */
static void test_petal_turn(void) {
    const double unit = M_PI / 2 / SH_AMBIENT_TURNS;
    int step, quarters;
    sh_ambient_petal_turn(0, &step, &quarters);
    CHECK(step == 0 && quarters == 0);
    sh_ambient_petal_turn(2 * unit, &step, &quarters);
    CHECK(step == 2 && quarters == 0, "%d %d", step, quarters);
    sh_ambient_petal_turn(M_PI / 2 + unit, &step, &quarters);
    CHECK(step == 1 && quarters == 1, "%d %d", step, quarters);
    sh_ambient_petal_turn(-unit, &step, &quarters);
    CHECK(step == SH_AMBIENT_TURNS - 1 && quarters == 3, "%d %d", step, quarters);
    sh_ambient_petal_turn(9 * M_PI, &step, &quarters); // half a turn, four and a half times over
    CHECK(step == 0 && quarters == 2, "%d %d", step, quarters);
    sh_ambient_petal_turn(NAN, &step, &quarters);
    CHECK(step == 0 && quarters == 0);
    // Each pink's and length's first picture, clamped to those there are.
    CHECK(sh_ambient_petal_sprite(0, 0) == 0);
    CHECK(sh_ambient_petal_sprite(0, 1) == SH_AMBIENT_TURNS);
    CHECK(sh_ambient_petal_sprite(1, 0) == 3 * SH_AMBIENT_TURNS);
    CHECK(sh_ambient_petal_sprite(9, 9) ==
          sh_ambient_sprite_count(SH_AMBIENT_SAKURA) - SH_AMBIENT_TURNS);
    CHECK(sh_ambient_sprite_side(SH_AMBIENT_SAKURA, sh_ambient_petal_sprite(0, 0)) <
          sh_ambient_sprite_side(SH_AMBIENT_SAKURA, sh_ambient_petal_sprite(0, 2)));
}

/* Rain falls fast along its streaks' slant, a little to the right as it goes down, the nearer
 * drops faster and longer; a streak's picture is narrower than it is tall, slants the same way,
 * and is fainter toward its tail at the top than toward its head. */
static void test_rain(void) {
    struct sh_ambient_field field = {.kind = SH_AMBIENT_RAIN, .width = WIDTH, .height = HEIGHT,
                                     .seed = 5};
    struct sh_ambient_particle drops[COUNT];
    double near_fall = 0, far_fall = 0, near_side = 0, far_side = 0;
    int near = 0, far = 0;
    for (int i = 0; i < COUNT; ++i) {
        sh_ambient_spawn(&field, &drops[i], true);
        double side = sh_ambient_sprite_side(SH_AMBIENT_RAIN, drops[i].sprite);
        if (drops[i].depth > 0.8)
            ++near, near_fall += drops[i].fall, near_side += side;
        else if (drops[i].depth < 0.3)
            ++far, far_fall += drops[i].fall, far_side += side;
    }
    CHECK(near > 0 && far > near, "more far than near: %d, %d", far, near);
    if (near > 0 && far > 0) {
        CHECK(near_fall / near > 1.5 * far_fall / far, "%g %g", near_fall / near, far_fall / far);
        CHECK(near_side / near > 2 * far_side / far, "%g %g", near_side / near, far_side / far);
    }
    for (int k = 0; k < 200; ++k) {
        struct sh_ambient_particle was[COUNT];
        memcpy(was, drops, sizeof(was));
        sh_ambient_step(&field, drops, COUNT, TICK);
        for (int i = 0; i < COUNT; ++i) {
            double dx = drops[i].x - was[i].x, dy = drops[i].y - was[i].y;
            if (dy < 0 || fabs(dx) > 100)
                continue; // in again at the top, or at the other side
            CHECK(dy > 0.45 * TICK, "falls fast: %g in a tick", dy);
            CHECK(dx > 0.1 * dy && dx < 0.3 * dy, "along its slant: %g across, %g down", dx, dy);
        }
    }
    int longest = sh_ambient_sprite_count(SH_AMBIENT_RAIN) / 2 - 1;
    int width = (int)lround(2 * sh_ambient_sprite_width(SH_AMBIENT_RAIN, longest));
    int height = (int)lround(2 * sh_ambient_sprite_side(SH_AMBIENT_RAIN, longest));
    CHECK(width < height / 3, "%d by %d", width, height);
    uint32_t *pixels = calloc((size_t)width * height, sizeof(*pixels));
    sh_ambient_paint(SH_AMBIENT_RAIN, longest, pixels, width, height);
    double xx, yy, xy, top = 0, bottom = 0;
    spread(pixels, width, height, &xx, &yy, &xy);
    CHECK(yy > 20 * xx && xy > 0, "tall and slanting to the right as it goes down: %g %g %g", xx,
          yy, xy);
    for (int i = 0; i < width * height; ++i) {
        if (i / width < height / 3)
            top += pixels[i] >> 24;
        else if (i / width >= height * 2 / 3)
            bottom += pixels[i] >> 24;
    }
    CHECK(bottom > 2 * top, "faint toward its tail: %g at the top, %g at the bottom", top, bottom);
    free(pixels);
}

int main(void) {
    test_names();
    test_petal_turn();
    test_motion();
    test_embers();
    test_rain();
    test_repeatable();
    test_paint();
    if (failures)
        fprintf(stderr, "%d checks failed\n", failures);
    return failures != 0;
}
