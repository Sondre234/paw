// SPDX-License-Identifier: GPL-3.0-or-later
/* Shaking the pointer to find it: which strokes make a shake (quick, wide, sideways, four turns
 * within SH_SHAKE_MS) and which do not (slow ones as the desktop cat's strokes, vertical ones,
 * too few turns, a rest between them), and the cursor's scale as it grows, holds while the
 * shaking goes on, and shrinks back, growing again from where it is. */
#include "paw/shake.h"
#include <stdio.h>
#include <string.h>
#include "check.h"

static int64_t now;

/* Moves the pointer from x0 to x1 (and y0 to y1) over `ms` in steps of 8 ms, as a mouse reports.
 * Returns how many motions made a shake. */
static int stroke(struct sh_shake *shake, double x0, double x1, double y0, double y1, int ms) {
    int shakes = 0, steps = ms / 8 > 0 ? ms / 8 : 1;
    for (int i = 1; i <= steps; ++i) {
        now += ms / steps;
        double t = (double)i / steps;
        shakes += sh_shake_motion(shake, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, now);
    }
    return shakes;
}

/* `strokes` strokes back and forth between x = 500 and 500 + width at y = 400, each over `ms`.
 * Returns how many motions made a shake. */
static int sideways(struct sh_shake *shake, int strokes, double width, int ms) {
    int shakes = 0;
    for (int i = 0; i < strokes; ++i)
        shakes += i % 2 ? stroke(shake, 500 + width, 500, 400, 400, ms)
                        : stroke(shake, 500, 500 + width, 400, 400, ms);
    return shakes;
}

static void fresh(struct sh_shake *shake) {
    memset(shake, 0, sizeof(*shake));
    now += 10000;
    sh_shake_motion(shake, 500, 400, now); // the pointer at rest where the strokes begin
}

int main(void) {
    struct sh_shake shake;
    now = 100000;

    // Four turns of quick strokes, 300 pixels in 80 ms: a shake, as the fifth stroke turns back.
    fresh(&shake);
    CHECK(sideways(&shake, 4, 300, 80) == 0, "no shake before the fourth turn is seen");
    CHECK(sh_shake_scale(&shake, now) == 1 && !sh_shake_grown(&shake, now));
    CHECK(stroke(&shake, 500, 520, 400, 400, 16) == 1, "a shake as the fourth turn is seen");
    int64_t shook = now;
    CHECK(sh_shake_grown(&shake, now));
    NEAR(sh_shake_scale(&shake, shook), 1, 1e-9);
    // It grows smoothly to three times the size, holds, and shrinks back to 1.
    double last = 1;
    for (int64_t t = shook; t <= shook + SH_SHAKE_GROW_MS; t += 10) {
        double scale = sh_shake_scale(&shake, t);
        CHECK(scale >= last && scale - last < 0.4, "growing smoothly: %f after %f", scale, last);
        last = scale;
    }
    NEAR(sh_shake_scale(&shake, shook + SH_SHAKE_GROW_MS), SH_SHAKE_GROW, 1e-9);
    NEAR(sh_shake_scale(&shake, shook + SH_SHAKE_HOLD_MS), SH_SHAKE_GROW, 1e-9);
    for (int64_t t = shook + SH_SHAKE_HOLD_MS; t <= shook + SH_SHAKE_HOLD_MS + SH_SHAKE_SHRINK_MS;
         t += 10) {
        double scale = sh_shake_scale(&shake, t);
        CHECK(scale <= last && last - scale < 0.4, "shrinking smoothly: %f after %f", scale, last);
        last = scale;
    }
    NEAR(sh_shake_scale(&shake, shook + SH_SHAKE_HOLD_MS + SH_SHAKE_SHRINK_MS), 1, 1e-9);
    CHECK(!sh_shake_grown(&shake, shook + SH_SHAKE_HOLD_MS + SH_SHAKE_SHRINK_MS));

    // Shaking on keeps it grown for as long, and it shrinks once the shaking stops.
    fresh(&shake);
    CHECK(sideways(&shake, 30, 300, 80) > 10, "every turn past the fourth keeps the shake");
    CHECK(sh_shake_scale(&shake, now) == SH_SHAKE_GROW);
    CHECK(sh_shake_grown(&shake, now + SH_SHAKE_HOLD_MS));
    CHECK(!sh_shake_grown(&shake, now + SH_SHAKE_HOLD_MS + SH_SHAKE_SHRINK_MS + 100));

    // Shaken again while it shrinks, it grows from the size it has, without a jump.
    fresh(&shake);
    sideways(&shake, 5, 300, 80);
    int64_t first = shake.shaken_at;
    now = first + SH_SHAKE_HOLD_MS + SH_SHAKE_SHRINK_MS / 2;
    double shrunk = sh_shake_scale(&shake, now);
    CHECK(shrunk > 1 && shrunk < SH_SHAKE_GROW, "half shrunk: %f", shrunk);
    sh_shake_reset(&shake);
    sh_shake_motion(&shake, 500, 400, now);
    CHECK(sideways(&shake, 4, 300, 8) == 0);
    struct sh_shake before = shake;
    CHECK(stroke(&shake, 500, 520, 400, 400, 8) == 1, "shaken again");
    double again = sh_shake_scale(&shake, shake.grown_at);
    NEAR(again, sh_shake_scale(&before, shake.grown_at), 1e-9);
    CHECK(again > 1 && again < shrunk, "growing again from its size (%f), not from 1", again);
    NEAR(sh_shake_scale(&shake, shake.grown_at + SH_SHAKE_GROW_MS), SH_SHAKE_GROW, 1e-9);

    // Strokes as slow as stroking the desktop cat, 30 pixels in 150 ms, never make one.
    fresh(&shake);
    CHECK(sideways(&shake, 20, 30, 150) == 0, "the cat's strokes are no shake");
    // Nor do wide ones short of the speed: 200 pixels in 144 ms is 1389 pixels a second.
    fresh(&shake);
    CHECK(sideways(&shake, 8, 200, 144) == 0, "slow strokes are no shake");
    // Nor narrow ones however quick, nor up and down.
    fresh(&shake);
    CHECK(sideways(&shake, 12, 40, 16) == 0, "narrow strokes are no shake");
    fresh(&shake);
    int vertical = 0;
    for (int i = 0; i < 12; ++i)
        vertical += stroke(&shake, 500 + (i % 2) * 100, 500 + ((i + 1) % 2) * 100,
                           i % 2 ? 700 : 300, i % 2 ? 300 : 700, 40);
    CHECK(vertical == 0, "up and down is no shake");
    // Three turns are not enough, and a rest between strokes starts over.
    fresh(&shake);
    CHECK(sideways(&shake, 4, 300, 80) == 0, "three turns");
    fresh(&shake);
    sideways(&shake, 3, 300, 80);
    now += SH_SHAKE_REST_MS + 50;
    CHECK(sideways(&shake, 3, 300, 80) == 0, "a rest between strokes starts over");
    // Turns spread wider than SH_SHAKE_MS are no shake: 300 pixels in 240 ms is quick enough,
    // but four turns take 720 ms.
    fresh(&shake);
    CHECK(sideways(&shake, 8, 400, 240) == 0, "turns too far apart");

    // Ordinary pointing: across the screen and back, with a correction or two.
    fresh(&shake);
    int ordinary = stroke(&shake, 500, 1500, 400, 600, 300) +
                   stroke(&shake, 1500, 1460, 600, 600, 40) +
                   stroke(&shake, 1460, 1480, 600, 610, 40) +
                   stroke(&shake, 1480, 200, 610, 200, 400) + stroke(&shake, 200, 260, 200, 210, 60);
    CHECK(ordinary == 0, "pointing is no shake");

    if (failures)
        fprintf(stderr, "%d shake check(s) failed\n", failures);
    else
        printf("shake tests passed\n");
    return failures != 0;
}
