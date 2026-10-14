// SPDX-License-Identifier: GPL-3.0-or-later
/* The toys' arithmetic: a shake's swing, a fall's bounces and squash, windows piling up, a
 * party's bob and colours, and confetti. */
#include "paw/toys.h"
#include <stdio.h>
#include "check.h"

static void test_shake(void) {
    CHECK(sh_shake_offset(0, 20) == 0, "at rest as it starts");
    CHECK(sh_shake_offset(SH_SHAKE_MS, 20) == 0 && sh_shake_offset(SH_SHAKE_MS + 50, 20) == 0,
          "at rest once over");
    CHECK(sh_shake_offset(-5, 20) == 0);
    double most = 0, left = 0, right = 0, late = 0;
    int crossings = 0;
    double last = 0;
    for (int t = 1; t < SH_SHAKE_MS; ++t) {
        double x = sh_shake_offset(t, 20);
        CHECK(fabs(x) <= 20, "within its amplitude at %d: %g", t, x);
        most = fmax(most, fabs(x));
        left = fmin(left, x), right = fmax(right, x);
        if (t >= SH_SHAKE_MS - 100)
            late = fmax(late, fabs(x));
        if ((last < 0 && x > 0) || (last > 0 && x < 0))
            ++crossings;
        last = x;
    }
    CHECK(most > 14, "a swing you can see: %g", most);
    CHECK(left < -5 && right > 5, "to either side: %g %g", left, right);
    CHECK(crossings >= 6, "it swings back and forth: %d", crossings);
    CHECK(late < most / 4, "dying away: %g of %g", late, most);
    CHECK(fabs(sh_shake_offset(SH_SHAKE_MS - 1, 20)) < 1, "near rest as it ends");
}

static void test_fall(void) {
    const double g = 0.006;
    struct sh_fall fall;
    sh_fall_plan(&fall, 800, g, 0.4);
    NEAR(fall.touch_at[0], sqrt(2 * 800 / g), 1e-9);
    NEAR(fall.speed[0], g * fall.touch_at[0], 1e-9);
    CHECK(fall.touches >= 3 && fall.touches <= SH_FALL_BOUNCES + 1, "%d", fall.touches);
    CHECK(fall.landed == fall.touch_at[fall.touches - 1]);
    CHECK(sh_fall_drop(&fall, 0) == 0, "where it was as it starts");
    NEAR(sh_fall_drop(&fall, fall.touch_at[0]), 800, 1e-6);
    double last = 0;
    for (double t = 1; t < fall.touch_at[0]; t += 1) {
        double drop = sh_fall_drop(&fall, t);
        CHECK(drop > last, "falling faster and faster");
        last = drop;
    }
    // Each bounce rises less than the one before, and it never goes through where it lands.
    double previous = 800;
    for (int k = 1; k < fall.touches; ++k) {
        double highest = 800;
        for (double t = fall.touch_at[k - 1]; t <= fall.touch_at[k]; t += 0.5)
            highest = fmin(highest, sh_fall_drop(&fall, t));
        double rise = 800 - highest;
        CHECK(rise >= 1 && rise < previous, "bounce %d rises %g after %g", k, rise, previous);
        NEAR(rise, 800 * pow(0.4, 2 * k), 1);
        previous = rise;
    }
    for (double t = 0; t < fall.landed + 500; t += 0.5)
        CHECK(sh_fall_drop(&fall, t) <= 800 + 1e-9, "never past where it lands, at %g", t);
    CHECK(sh_fall_drop(&fall, fall.landed) == 800 && sh_fall_drop(&fall, 1e6) == 800);
    // It squashes as it touches down, harder the faster it came.
    CHECK(sh_fall_squash(&fall, fall.touch_at[0] - 1) == 0, "not before the first touch");
    double first = sh_fall_squash(&fall, fall.touch_at[0] + SH_SQUASH_MS / 2.0);
    CHECK(first > 0.05 && first <= SH_SQUASH, "%g", first);
    double second = sh_fall_squash(&fall, fall.touch_at[1] + SH_SQUASH_MS / 2.0);
    CHECK(second > 0 && second < first, "%g after %g", second, first);
    CHECK(sh_fall_squash(&fall, fall.landed + SH_SQUASH_MS) == 0, "over once it rests");
    for (double t = 0; t < fall.landed + 500; t += 0.5) {
        double squash = sh_fall_squash(&fall, t);
        CHECK(squash >= 0 && squash <= SH_SQUASH, "%g at %g", squash, t);
    }
    // A short fall barely bounces; none at all does nothing.
    sh_fall_plan(&fall, 3, g, 0.4);
    CHECK(fall.touches == 1 && fall.landed == fall.touch_at[0], "%d", fall.touches);
    sh_fall_plan(&fall, 0, g, 0.4);
    CHECK(fall.touches == 0 && fall.landed == 0);
    CHECK(sh_fall_drop(&fall, 100) == 0 && sh_fall_squash(&fall, 0) == 0);
    sh_fall_plan(&fall, 500, g, 0);
    CHECK(fall.touches == 1, "no bounce without restitution");
}

static void test_pile(void) {
    double drops[4];
    // One window falls to the floor; one resting on it sinks into it.
    struct sh_toy_box one[] = {{100, 100, 400, 300}};
    sh_pile(one, 1, 1000, 50, drops);
    NEAR(drops[0], 600, 1e-9);
    struct sh_toy_box resting[] = {{100, 700, 400, 300}};
    sh_pile(resting, 1, 1000, 50, drops);
    NEAR(drops[0], 50, 1e-9);
    // A column: the upper one lands on the lower one, wherever the lower one lands.
    struct sh_toy_box column[] = {{100, 50, 400, 200}, {150, 400, 400, 300}};
    sh_pile(column, 2, 1000, 50, drops);
    NEAR(drops[1], 300, 1e-9);
    NEAR(drops[0], (400 + 300) - 250, 1e-9);
    struct sh_toy_box tiles[] = {{0, 10, 500, 480}, {0, 500, 500, 490}};
    sh_pile(tiles, 2, 990, 200, drops);
    NEAR(drops[1], 200, 1e-9);
    NEAR(drops[0], 200 + 10, 1e-9);
    // Side by side, each falls on its own.
    struct sh_toy_box beside[] = {{0, 100, 500, 300}, {500, 300, 500, 300}};
    sh_pile(beside, 2, 1000, 0, drops);
    NEAR(drops[0], 600, 1e-9);
    NEAR(drops[1], 400, 1e-9);
    // One overlapping another where that one lands lands on nothing above it, and never rises.
    struct sh_toy_box overlapping[] = {{0, 600, 500, 300}, {100, 500, 300, 480}};
    sh_pile(overlapping, 2, 1000, 0, drops);
    NEAR(drops[1], 20, 1e-9);
    CHECK(drops[0] >= 0, "%g", drops[0]);
    // A window reaching past the floor already stays where it is.
    struct sh_toy_box past[] = {{0, 900, 500, 300}};
    sh_pile(past, 1, 1000, 0, drops);
    CHECK(drops[0] == 0, "%g", drops[0]);
    sh_pile(past, 0, 1000, 0, drops); // nothing to do
}

static void test_party(void) {
    double dx, dy;
    sh_party_bob(0, 1, &dx, &dy);
    CHECK(dx == 0 && dy == 0, "at rest as it starts");
    sh_party_bob(SH_PARTY_MS, 1, &dx, &dy);
    CHECK(dx == 0 && dy == 0, "and as it ends");
    double most = 0;
    for (int t = 0; t <= SH_PARTY_MS; t += 5) {
        sh_party_bob(t, 2.5, &dx, &dy);
        CHECK(fabs(dx) <= 10 && fabs(dy) <= 10, "gently: %g %g at %d", dx, dy, t);
        most = fmax(most, fabs(dy));
    }
    CHECK(most > 6, "but you see it: %g", most);
    sh_party_bob(SH_PARTY_MS - 2, 1, &dx, &dy);
    CHECK(fabs(dx) < 0.1 && fabs(dy) < 0.1, "settled as it ends: %g %g", dx, dy);
    double ax, ay, bx, by;
    sh_party_bob(1500, 0, &ax, &ay);
    sh_party_bob(1500, 2.4, &bx, &by);
    CHECK(fabs(ay - by) > 1, "out of step: %g %g", ay, by);

    float c[4];
    sh_rainbow(0, c);
    CHECK(c[0] == 1 && c[1] < 0.3F && c[2] < 0.3F && c[3] == 1, "red: %g %g %g", c[0], c[1], c[2]);
    sh_rainbow(1.0 / 3, c);
    CHECK(c[1] == 1 && c[0] < 0.3F && c[2] < 0.3F, "green: %g %g %g", c[0], c[1], c[2]);
    sh_rainbow(2.0 / 3 + 2, c);
    CHECK(c[2] == 1 && c[0] < 0.3F && c[1] < 0.3F, "blue, round twice: %g %g %g", c[0], c[1],
          c[2]);
    sh_rainbow(-1.0 / 6, c);
    CHECK(c[0] == 1 && c[2] > 0.9F && c[1] < 0.3F, "magenta: %g %g %g", c[0], c[1], c[2]);
    for (double hue = 0; hue < 1; hue += 0.01) {
        sh_rainbow(hue, c);
        for (int i = 0; i < 4; ++i)
            CHECK(c[i] >= 0 && c[i] <= 1);
    }
}

static void test_confetti(void) {
    uint32_t seed = 12345, again = 12345;
    struct sh_confetto a, b;
    sh_confetti_spawn(&a, &seed, 100, 0, 800, 3000);
    sh_confetti_spawn(&b, &again, 100, 0, 800, 3000);
    CHECK(a.x == b.x && a.vy == b.vy && a.color[0] == b.color[0], "the seed decides");
    CHECK(seed != 12345, "and moves on");
    for (int i = 0; i < 200; ++i) {
        sh_confetti_spawn(&a, &seed, 100, 50, 800, 3000);
        CHECK(a.x >= 100 && a.x <= 900 && a.y <= 50 && a.y >= 50 - 24, "%g %g", a.x, a.y);
        CHECK(a.vy > 0, "coming down");
        CHECK(a.life > 0 && a.life <= 3000, "%g", a.life);
        CHECK(a.width >= 7 && a.height >= 4 && a.color[3] == 1);
    }
    sh_confetti_spawn(&a, &seed, 0, 0, 1000, 3000);
    a.life = 3000;
    double y = a.y;
    float alpha = 1;
    bool alive = true;
    for (int t = 0; t < 3000 && alive; t += 16) {
        alive = sh_confetti_step(&a, 16);
        CHECK(a.vy <= SH_CONFETTI_FALL + 1e-9, "no faster than the air lets it: %g", a.vy);
        CHECK(a.y > y, "falling");
        y = a.y;
        CHECK(sh_confetti_width(&a) >= 1 && sh_confetti_width(&a) <= a.width);
        float now = sh_confetti_alpha(&a);
        CHECK(now <= alpha, "fading only");
        if (a.age < a.life * 0.6)
            CHECK(now == 1, "whole at first");
        alpha = now;
    }
    CHECK(!alive && sh_confetti_alpha(&a) == 0, "gone as its life ends");
    CHECK(a.vy > SH_CONFETTI_FALL * 0.9, "near the speed the air allows: %g", a.vy);
}

int main(void) {
    test_shake();
    test_fall();
    test_pile();
    test_party();
    test_confetti();
    if (failures)
        return 1;
    puts("toys passed");
    return 0;
}
