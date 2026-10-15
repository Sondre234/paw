// SPDX-License-Identifier: GPL-3.0-or-later
/* The scrolling layout's cube: at rest the strip is drawn as it is, a move starts from the view as
 * it was and turns a quarter to the view as it will be, the sides facing the eye fill the view,
 * further off is smaller and edge on fainter, and a move under way goes on from where it is
 * drawn, turning the way the view moves. */
#include "paw/cube.h"
#include "check.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const double W = 1000;
static const struct sh_cube REST = {1000, 0, 0};

static struct sh_cube_span span(const struct sh_cube *cube, double left, double right) {
    struct sh_cube_span out;
    sh_cube_span(cube, left, right, &out);
    return out;
}

/* At rest the strip is drawn where it lies, as it is; past the view it lies on sides turned away. */
static void test_rest(void) {
    struct sh_cube_span s = span(&REST, 100, 400);
    CHECK(s.shown);
    NEAR(s.left, 100, 1e-9);
    NEAR(s.right, 400, 1e-9);
    NEAR(s.scale, 1, 1e-9);
    NEAR(s.fade, 0, 1e-9);
    CHECK(!span(&REST, 1100, 1400).shown, "past the view's right edge");
    CHECK(!span(&REST, -400, -100).shown, "past its left edge");
    CHECK(span(&REST, 800, 1200).shown, "across the edge, it shows");
    NEAR(span(&REST, 800, 1200).right, 1200, 1e-9);
}

/* A move starts from the view as it was and ends at rest, the side with the view as it will be
 * coming round from the side the view moves to. */
static void test_move(void) {
    for (int way = -1; way <= 1; way += 2) {
        double distance = way * 450;
        struct sh_cube from = sh_cube_move(REST, distance);
        NEAR(from.angle, -way * M_PI / 2, 1e-9);
        // Where a column was, before the view moved: it lay `distance` further right.
        struct sh_cube_span was = span(&from, 100 - distance, 400 - distance);
        CHECK(was.shown, "way %d", way);
        NEAR(was.left, 100, 1e-6);
        NEAR(was.right, 400, 1e-6);
        NEAR(was.scale, 1, 1e-9);
        NEAR(was.fade, 0, 1e-9);
        // What the view shows at the end is edge on at first, on the side it moves to.
        CHECK(!span(&from, 500 + 450 * way, 520 + 450 * way).shown, "way %d", way);
        struct sh_cube end = sh_cube_at(&from, 1);
        struct sh_cube_span rest = span(&end, 100, 400);
        NEAR(rest.left, 100, 1e-9);
        NEAR(rest.right, 400, 1e-9);
        NEAR(rest.scale, 1, 1e-9);
    }
    // No move, no turn.
    struct sh_cube still = sh_cube_move(REST, 0);
    NEAR(still.angle, 0, 1e-12);
    NEAR(still.start, 0, 1e-12);
}

/* Half way through a move a whole view wide, the corner between the two sides is nearest the eye,
 * in the middle of the view, and the far edges of the sides are at the view's edges, smaller. */
static void test_half(void) {
    struct sh_cube from = sh_cube_move(REST, W);
    struct sh_cube half = sh_cube_at(&from, 0.5);
    NEAR(half.angle, -M_PI / 4, 1e-9);
    NEAR(half.start, 0, 1e-9);
    struct sh_cube_span before = span(&half, -300, -100), after = span(&half, 100, 300);
    NEAR(before.left + after.right, W, 1e-6); // mirrored about the corner
    NEAR(before.right + after.left, W, 1e-6);
    NEAR(before.scale, after.scale, 1e-9);
    NEAR(before.fade, after.fade, 1e-9);
    NEAR(span(&half, -1, 1).left, W / 2, 1);
    NEAR(span(&half, -W, -W + 1).left, 0, 1e-6);
    NEAR(span(&half, W - 1, W).right, W, 1e-6);
    CHECK(span(&half, -W, -W + 10).scale < span(&half, -10, 0).scale, "further off, smaller");
    NEAR(span(&half, -10, 0).scale, 1, 0.01);
    // Each side is turned an eighth: as faint as that.
    NEAR(after.fade, SH_CUBE_FADE * (1 - cos(M_PI / 4)), 1e-9);
    // Past the sides facing the eye, it goes on past the view's edges, turned away.
    CHECK(!span(&half, W + 10, W + 300).shown);
    CHECK(span(&half, W + 10, W + 300).left > W);
    CHECK(span(&half, -W - 300, -W - 10).right < 0);
}

/* At every point of a move, the strip is drawn left to right in order and what faces the eye fills
 * the view. */
static void test_order(void) {
    const double distances[] = {200, 500, 1000, 1700, -300, -1000};
    for (size_t d = 0; d < sizeof(distances) / sizeof(*distances); ++d) {
        struct sh_cube from = sh_cube_move(REST, distances[d]);
        for (int step = 0; step <= 20; ++step) {
            struct sh_cube cube = sh_cube_at(&from, step / 20.0);
            double last = -1e9;
            for (double at = -3 * W; at <= 3 * W; at += 37) {
                struct sh_cube_span s = span(&cube, at, at + 37);
                CHECK(s.left >= last - 1e-6 && s.right >= s.left - 1e-6,
                      "distance %g, step %d, at %g: %g after %g", distances[d], step, at, s.left,
                      last);
                if (s.shown)
                    CHECK(s.scale > 0 && s.scale <= 1 + 1e-9 && s.fade >= 0 &&
                          s.fade <= SH_CUBE_FADE + 1e-9);
                last = s.right;
            }
            // The two outer edges of what faces the eye are the view's.
            int first = (int)floor(cube.angle / (M_PI / 2) - 1) + 1;
            int count = fmod(fabs(cube.angle), M_PI / 2) < 1e-12 ? 1 : 2;
            double low = cube.start + first * W, high = low + count * W;
            NEAR(span(&cube, low, low + 1).left, 0, 1e-6);
            NEAR(span(&cube, high - 1, high).right, W, 1e-6);
        }
    }
}

/* A move under way moved again draws everything where it was, in the new view, and turns on the
 * way the view moves now: further the same way, or back. */
static void test_again(void) {
    struct sh_cube from = sh_cube_move(REST, 500);
    const double progress[] = {0.1, 0.5, 0.9, 1};
    const double again[] = {500, -500, 1500, -20};
    for (size_t p = 0; p < 4; ++p) {
        struct sh_cube now = sh_cube_at(&from, progress[p]);
        for (size_t a = 0; a < 4; ++a) {
            struct sh_cube next = sh_cube_move(now, again[a]);
            CHECK(again[a] > 0 ? next.angle <= -M_PI / 4 + 1e-9 : next.angle >= M_PI / 4 - 1e-9,
                  "progress %g, again %g: angle %g", progress[p], again[a], next.angle);
            CHECK(fabs(next.angle) <= 3 * M_PI / 4 + 1e-9);
            for (double at = -W; at <= 2 * W; at += 125) {
                struct sh_cube_span was = span(&now, at, at + 100);
                struct sh_cube_span is = span(&next, at - again[a], at + 100 - again[a]);
                NEAR(is.left, was.left, 1e-6);
                NEAR(is.right, was.right, 1e-6);
                NEAR(is.scale, was.scale, 1e-9);
                NEAR(is.fade, was.fade, 1e-9);
                CHECK(is.shown == was.shown, "progress %g, again %g, at %g", progress[p],
                      again[a], at);
            }
        }
    }
}

int main(void) {
    test_rest();
    test_move();
    test_half();
    test_order();
    test_again();
    if (failures)
        fprintf(stderr, "%d check(s) failed\n", failures);
    return failures != 0;
}
