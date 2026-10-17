// SPDX-License-Identifier: GPL-3.0-or-later
/* The cursor trail's arithmetic and pictures: a pointer that stays where it is drops nothing and a
 * moving one drops particles in proportion to the way it went (and the amount), up to a limit,
 * the newest by the pointer; they stay near the way, sparkles and petals drift down and hearts up,
 * fade in, out and away within about a second, whatever their kind; the same seed makes the same
 * trail; and every picture paints something, its corners clear. */
#include "paw/cursor_trail.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"

static const float COLORS[2][3] = {{0.56f, 0.76f, 0.93f}, {0.95f, 0.63f, 0.75f}};
static const double TICK = 16;

static struct sh_trail *make(enum sh_trail_kind kind, double amount) {
    struct sh_trail *trail = calloc(1, sizeof(*trail));
    trail->seed = 11;
    sh_trail_reset(trail, kind, 2, amount);
    return trail;
}

/* Moves the pointer from (x, y) by (dx, dy) `steps` times, a tick apart, stepping the trail;
 * returns how many particles came off it. */
static int sweep(struct sh_trail *trail, double x, double y, double dx, double dy, int steps) {
    int spawned = 0;
    for (int i = 0; i < steps; ++i) {
        spawned += sh_trail_moved(trail, x + dx * i, y + dy * i, x + dx * (i + 1), y + dy * (i + 1));
        sh_trail_step(trail, TICK);
    }
    return spawned;
}

static void test_names(void) {
    CHECK(!strcmp(sh_trail_kind_name(SH_TRAIL_NONE), "none"));
    CHECK(!strcmp(sh_trail_kind_name(SH_TRAIL_SPARKLES), "sparkles"));
    CHECK(!strcmp(sh_trail_kind_name(SH_TRAIL_PETALS), "petals"));
    CHECK(!strcmp(sh_trail_kind_name(SH_TRAIL_HEARTS), "hearts"));
    CHECK(!strcmp(sh_trail_kind_name(SH_TRAIL_KINDS), "none"));
    CHECK(sh_trail_sprite_count(SH_TRAIL_NONE, 2) == 0);
    CHECK(sh_trail_sprite_count(SH_TRAIL_SPARKLES, 2) == 2 * sh_trail_sprite_count(SH_TRAIL_SPARKLES, 1));
    CHECK(sh_trail_sprite_count(SH_TRAIL_HEARTS, 0) == sh_trail_sprite_count(SH_TRAIL_HEARTS, 1));
    CHECK(sh_trail_sprite_count(SH_TRAIL_HEARTS, 99) ==
          sh_trail_sprite_count(SH_TRAIL_HEARTS, SH_TRAIL_TINTS));
    CHECK(sh_trail_sprite_side(SH_TRAIL_SPARKLES, -1) == 0);
    CHECK(sh_trail_sprite_side(SH_TRAIL_NONE, 0) == 0);
}

/* Still, nothing; moving, particles in proportion to the way and the amount; none at all for none. */
static void test_spawn(void) {
    for (int kind = SH_TRAIL_SPARKLES; kind < SH_TRAIL_KINDS; ++kind) {
        struct sh_trail *trail = make(kind, 1);
        const char *name = sh_trail_kind_name(kind);
        CHECK(sh_trail_moved(trail, 500, 500, 500, 500) == 0, "%s: a pointer that stays", name);
        CHECK(sweep(trail, 500, 500, 0, 0, 30) == 0 && trail->count == 0, "%s", name);
        int slow = sweep(trail, 100, 300, 2, 0, 100); // 200 pixels
        sh_trail_clear(trail);
        int fast = sweep(trail, 100, 300, 8, 0, 100); // 800 pixels in the same time
        CHECK(slow >= 2 && fast >= 3 * slow && fast <= 5 * slow, "%s: %d slow, %d fast", name,
              slow, fast);
        sh_trail_clear(trail);
        struct sh_trail *more = make(kind, 2);
        int doubled = sweep(more, 100, 300, 8, 0, 100);
        CHECK(doubled >= fast * 3 / 2 && doubled <= fast * 5 / 2, "%s: amount 2 %d, 1 %d", name,
              doubled, fast);
        free(more);
        // A jump far at once brings a handful, not a line across the screen.
        sh_trail_clear(trail);
        CHECK(sh_trail_moved(trail, 0, 0, 3000, 0) <= 24, "%s", name);
        free(trail);
    }
    struct sh_trail *none = make(SH_TRAIL_NONE, 1);
    CHECK(sweep(none, 100, 100, 9, 9, 50) == 0 && none->count == 0);
    free(none);
}

/* Particles stay near the way the pointer went, drift the way their kind does, and are all gone
 * within a second or so of the pointer stopping. */
static void test_decay(void) {
    for (int kind = SH_TRAIL_SPARKLES; kind < SH_TRAIL_KINDS; ++kind) {
        const char *name = sh_trail_kind_name(kind);
        struct sh_trail *trail = make(kind, 1);
        sweep(trail, 200, 400, 10, 0, 40); // to (600, 400)
        CHECK(trail->count > 5, "%s: %d", name, trail->count);
        // Where each was as the pointer stopped, to see where the same ones went 300 ms later.
        double was[SH_TRAIL_MAX], drift = 0;
        for (int i = 0; i < SH_TRAIL_MAX; ++i)
            was[i] = trail->particles[i].y;
        int last = trail->count;
        double gone = 0;
        bool shrank = true;
        for (double t = 0; t < 2000 && trail->count; t += TICK) {
            sh_trail_step(trail, TICK);
            shrank &= trail->count <= last;
            last = trail->count;
            gone = t + TICK;
            if (gone >= 300 && gone < 300 + TICK) {
                int n = 0;
                for (int i = 0; i < SH_TRAIL_MAX; ++i)
                    if (trail->particles[i].life > 0) {
                        drift += trail->particles[i].y - was[i];
                        ++n;
                    }
                drift /= n > 0 ? n : 1;
            }
            for (int i = 0; i < SH_TRAIL_MAX; ++i) {
                const struct sh_trail_particle *p = &trail->particles[i];
                if (p->life <= 0)
                    continue;
                // Sparks, flung hard, fall further below it in their arcs.
                double below = kind == SH_TRAIL_SPARKS ? 200 : 80;
                CHECK(p->x > 120 && p->x < 680 && p->y > 320 && p->y < 400 + below,
                      "%s: near the way: %g %g", name, p->x, p->y);
                struct sh_trail_look look;
                sh_trail_look(trail, p, &look);
                CHECK(look.opacity >= 0 && look.opacity <= 1);
                CHECK(look.width >= 0 && look.width <= 36 && look.height >= 0 &&
                          look.height <= 36,
                      "%s: %g x %g", name, look.width, look.height);
                CHECK(look.sprite >= 0 && look.sprite < sh_trail_sprite_count(kind, 2), "%s: %d",
                      name, look.sprite);
                CHECK(look.quarters >= 0 && look.quarters < 4);
            }
        }
        CHECK(trail->count == 0 && gone <= 1100, "%s: gone after %g ms (%d left)", name, gone,
              trail->count);
        CHECK(shrank, "%s: only fewer once the pointer stops", name);
        if (kind == SH_TRAIL_HEARTS)
            CHECK(drift < -3, "hearts float up: %g", drift);
        else if (kind == SH_TRAIL_SPARKS)
            CHECK(drift > 20 && drift < 160, "sparks fall in an arc: %g", drift);
        else
            CHECK(drift > 3 && drift < 40, "%s drift down a little: %g", name, drift);
        free(trail);
    }
}

/* A particle fades in from nothing, shows in full, and fades out toward its end. */
static void test_fade(void) {
    for (int kind = SH_TRAIL_SPARKLES; kind < SH_TRAIL_KINDS; ++kind) {
        struct sh_trail *trail = make(kind, 1);
        sh_trail_moved(trail, 0, 0, 200, 0);
        const struct sh_trail_particle *p = NULL;
        for (int i = 0; i < SH_TRAIL_MAX && !p; ++i)
            if (trail->particles[i].life > 0)
                p = &trail->particles[i];
        CHECK(p != NULL);
        if (!p) {
            free(trail);
            continue;
        }
        struct sh_trail_look look;
        sh_trail_look(trail, p, &look);
        CHECK(look.opacity == 0, "%s: from nothing: %g", sh_trail_kind_name(kind), look.opacity);
        double life = p->life, peak = 0, late = 1;
        for (double t = 0; t < life; t += TICK) {
            sh_trail_step(trail, TICK);
            if (p->life <= 0)
                break;
            sh_trail_look(trail, p, &look);
            if (p->age < 0.4 * life)
                peak = fmax(peak, look.opacity);
            if (p->age > 0.95 * life)
                late = fmin(late, look.opacity);
        }
        CHECK(peak > 0.6, "%s: shows: %g", sh_trail_kind_name(kind), peak);
        CHECK(late < 0.1, "%s: fades out: %g", sh_trail_kind_name(kind), late);
        free(trail);
    }
}

/* However far it goes, at most SH_TRAIL_MAX at once, and the newest are by the pointer. */
static void test_limit(void) {
    struct sh_trail *trail = make(SH_TRAIL_SPARKLES, 4);
    for (int i = 0; i < 400; ++i) {
        sh_trail_moved(trail, 10.0 * i, 300, 10.0 * (i + 1), 300);
        sh_trail_step(trail, 2); // a fast mouse's motions, far quicker than the particles go
    }
    CHECK(trail->count == SH_TRAIL_MAX, "%d", trail->count);
    int near = 0;
    for (int i = 0; i < SH_TRAIL_MAX; ++i)
        near += trail->particles[i].x > 4000 - 600;
    CHECK(near == SH_TRAIL_MAX, "the oldest gave way: %d near the pointer", near);
    sh_trail_clear(trail);
    CHECK(trail->count == 0 && sh_trail_step(trail, TICK) == 0);
    free(trail);
}

/* Each place keeps to its family of pictures, whatever particle comes to it and however it
 * turns, and the families hold every picture the trail draws. */
static void test_families(void) {
    for (int kind = SH_TRAIL_SPARKLES; kind < SH_TRAIL_KINDS; ++kind) {
        const char *name = sh_trail_kind_name(kind);
        struct sh_trail *trail = make(kind, 2);
        int families = sh_trail_family_count(kind, trail->tints);
        int size = sh_trail_family_size(kind);
        CHECK(families >= 2 && size >= 2, "%s: %d of %d", name, families, size);
        for (int f = 0; f < families; ++f)
            CHECK(sh_trail_family_first(kind, f) + size <= sh_trail_sprite_count(kind, 2), "%s %d",
                  name, f);
        int seen[SH_TRAIL_TINTS] = {0};
        for (int round = 0; round < 6; ++round) {
            sweep(trail, 100, 200 + 10 * round, 9, 2, 60);
            for (int i = 0; i < SH_TRAIL_MAX; ++i) {
                const struct sh_trail_particle *p = &trail->particles[i];
                if (p->life <= 0)
                    continue;
                int family = sh_trail_family_at(trail, i);
                int first = sh_trail_family_first(kind, family);
                struct sh_trail_look look;
                sh_trail_look(trail, p, &look);
                CHECK(look.sprite >= first && look.sprite < first + size,
                      "%s: place %d of family %d shows %d", name, i, family, look.sprite);
                ++seen[family];
            }
        }
        for (int f = 0; f < families; ++f)
            CHECK(seen[f] > 0, "%s: family %d seen", name, f);
        free(trail);
    }
}

static void test_repeatable(void) {
    struct sh_trail *a = make(SH_TRAIL_PETALS, 1), *b = make(SH_TRAIL_PETALS, 1);
    sweep(a, 0, 0, 7, 3, 60);
    sweep(b, 0, 0, 7, 3, 60);
    CHECK(!memcmp(a->particles, b->particles, sizeof(a->particles)));
    free(a);
    free(b);
}

/* Every picture paints something inside it, its corners clear; a star is whiter at its middle
 * than at its points, and a heart is solid in its middle. */
static void test_paint(void) {
    for (int kind = SH_TRAIL_SPARKLES; kind < SH_TRAIL_KINDS; ++kind) {
        for (int sprite = 0; sprite < sh_trail_sprite_count(kind, 2); ++sprite) {
            for (int scale = 1; scale <= 2; ++scale) {
                double logical = sh_trail_sprite_side(kind, sprite);
                CHECK(logical >= 6 && logical <= 36, "%s %d: %g", sh_trail_kind_name(kind),
                      sprite, logical);
                int side = (int)lround(logical * scale);
                uint32_t *pixels = calloc((size_t)side * side, sizeof(*pixels));
                sh_trail_paint(kind, sprite, COLORS, 2, pixels, side);
                int shown = 0;
                for (int i = 0; i < side * side; ++i) {
                    uint32_t a = pixels[i] >> 24;
                    shown += a > 64;
                    CHECK((pixels[i] >> 16 & 0xff) <= a && (pixels[i] >> 8 & 0xff) <= a &&
                              (pixels[i] & 0xff) <= a,
                          "premultiplied: %08x", pixels[i]);
                }
                CHECK(shown >= 4, "%s %d: %d pixels", sh_trail_kind_name(kind), sprite, shown);
                for (int corner = 0; corner < 4; ++corner) {
                    int i = (corner & 1 ? side - 1 : 0) + (corner & 2 ? side - 1 : 0) * side;
                    CHECK(pixels[i] >> 24 < 8, "%s %d: corners clear: %08x",
                          sh_trail_kind_name(kind), sprite, pixels[i]);
                }
                free(pixels);
            }
        }
    }
    // The first star in the second colour (pink): white in its middle, pink toward its points.
    int side = 42;
    uint32_t *pixels = calloc((size_t)side * side, sizeof(*pixels));
    sh_trail_paint(SH_TRAIL_SPARKLES, 4, COLORS, 2, pixels, side);
    uint32_t middle = pixels[side / 2 * side + side / 2];
    CHECK(middle >> 24 == 255 && (middle & 0xff) > 230, "%08x", middle);
    uint32_t point = pixels[(side / 2 - 13) * side + side / 2]; // up toward its top point
    CHECK(point >> 24 > 128 && (point >> 16 & 0xff) > (point & 0xff) + 10, "%08x", point);
    sh_trail_paint(SH_TRAIL_HEARTS, 0, COLORS, 2, pixels, side);
    uint32_t heart = pixels[side / 2 * side + side / 2];
    CHECK(heart >> 24 == 255, "%08x", heart);
    free(pixels);
}

int main(void) {
    test_names();
    test_spawn();
    test_decay();
    test_fade();
    test_limit();
    test_families();
    test_repeatable();
    test_paint();
    if (failures)
        fprintf(stderr, "%d checks failed\n", failures);
    return failures != 0;
}
