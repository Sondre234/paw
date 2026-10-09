// SPDX-License-Identifier: GPL-3.0-or-later
#include "paw/swipe.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

static int failures = 0;
#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
            ++failures;                                                                        \
        }                                                                                      \
    } while (0)
#define NEAR(a, b, tolerance) CHECK(std::fabs((a) - (b)) <= (tolerance))

// The direction is the axis the fingers went most along, taken once they are past the
// threshold, and taken once.
static void directions() {
    sh_swipe swipe;
    sh_swipe_begin(&swipe, 3, 300, false, 1000);
    CHECK(swipe.direction == SH_SWIPE_NONE);
    CHECK(!sh_swipe_update(&swipe, -6, 4, 1010));
    CHECK(!sh_swipe_update(&swipe, -6, 3, 1020));
    CHECK(swipe.direction == SH_SWIPE_NONE);
    NEAR(sh_swipe_progress(&swipe), 0, 1e-12);
    CHECK(sh_swipe_update(&swipe, -6, 2, 1030));
    CHECK(swipe.direction == SH_SWIPE_LEFT);
    CHECK(!sh_swipe_update(&swipe, 0, -80, 1040)); // later movement keeps the direction
    CHECK(swipe.direction == SH_SWIPE_LEFT);
    NEAR(sh_swipe_progress(&swipe), 18.0 / 300, 1e-12);

    struct {
        double dx, dy;
        sh_swipe_direction direction;
    } cases[] = {{20, 1, SH_SWIPE_RIGHT}, {-20, 1, SH_SWIPE_LEFT}, {3, -20, SH_SWIPE_UP},
                 {-3, 20, SH_SWIPE_DOWN}, {12, 12, SH_SWIPE_RIGHT}};
    for (const auto &c : cases) {
        sh_swipe_begin(&swipe, 4, 300, false, 0);
        CHECK(sh_swipe_update(&swipe, c.dx, c.dy, 10));
        CHECK(swipe.direction == c.direction);
        CHECK(swipe.fingers == 4);
        // Inverted, the fingers count the other way.
        sh_swipe_begin(&swipe, 4, 300, true, 0);
        CHECK(sh_swipe_update(&swipe, c.dx, c.dy, 10));
        CHECK(swipe.direction == sh_swipe_opposite(c.direction));
        CHECK(sh_swipe_progress(&swipe) > 0);
    }
    // Numbers that are none are passed over.
    sh_swipe_begin(&swipe, 3, 300, false, 0);
    CHECK(!sh_swipe_update(&swipe, NAN, 40, 10));
    CHECK(!sh_swipe_update(&swipe, INFINITY, 0, 10));
    CHECK(swipe.direction == SH_SWIPE_NONE && swipe.x == 0 && swipe.y == 0);
}

// Progress counts whole steps along the direction, below 0 back past the start.
static void progress() {
    sh_swipe swipe;
    sh_swipe_begin(&swipe, 3, 300, false, 0);
    sh_swipe_update(&swipe, -30, 0, 10);
    sh_swipe_update(&swipe, -120, 5, 20);
    NEAR(sh_swipe_progress(&swipe), 0.5, 1e-12);
    sh_swipe_update(&swipe, -300, 0, 30);
    NEAR(sh_swipe_progress(&swipe), 1.5, 1e-12);
    sh_swipe_update(&swipe, 500, 0, 40);
    NEAR(sh_swipe_progress(&swipe), 50.0 / -300, 1e-12);
    // Up and down count y.
    sh_swipe_begin(&swipe, 3, 200, false, 0);
    sh_swipe_update(&swipe, 2, -100, 10);
    CHECK(swipe.direction == SH_SWIPE_UP);
    NEAR(sh_swipe_progress(&swipe), 0.5, 1e-12);
    // A distance of nothing is a distance of one unit, not a division by zero.
    sh_swipe_begin(&swipe, 3, 0, false, 0);
    sh_swipe_update(&swipe, 0, 20, 10);
    NEAR(sh_swipe_progress(&swipe), 20, 1e-12);
}

// The speed at the end is the travel over the last moments before the fingers lifted.
static void speed() {
    sh_swipe swipe;
    sh_swipe_begin(&swipe, 3, 300, false, 1000);
    NEAR(sh_swipe_speed(&swipe, 1000), 0, 1e-12); // no direction yet
    for (int i = 1; i <= 20; ++i)
        sh_swipe_update(&swipe, -10, 0, 1000 + 10 * i);
    // 10 units every 10 ms toward the left: 1 a millisecond, lifting as the last arrived.
    NEAR(sh_swipe_speed(&swipe, 1200), 1, 1e-9);
    // Lifting 20 ms later, slower over the span.
    NEAR(sh_swipe_speed(&swipe, 1220), 60.0 / 80, 1e-9);
    // Resting longer than the span before lifting: no speed at all.
    NEAR(sh_swipe_speed(&swipe, 1200 + SH_SWIPE_SPEED_SPAN + 1), 0, 1e-12);
    // Turning back at the end gives a speed away from the direction.
    sh_swipe_update(&swipe, 40, 0, 1210);
    sh_swipe_update(&swipe, 40, 0, 1220);
    CHECK(sh_swipe_speed(&swipe, 1220) < 0);
    // A single movement within the span counts from where the fingers were before it.
    sh_swipe_begin(&swipe, 3, 300, false, 0);
    sh_swipe_update(&swipe, 0, 30, 500);
    NEAR(sh_swipe_speed(&swipe, 500), 30.0 / 500, 1e-9);
    // libinput's times wrap at 32 bits.
    sh_swipe_begin(&swipe, 3, 300, false, UINT32_MAX - 15);
    for (uint32_t i = 1; i <= 4; ++i)
        sh_swipe_update(&swipe, 8, 0, UINT32_MAX - 15 + 10 * i);
    NEAR(sh_swipe_speed(&swipe, UINT32_MAX - 15 + 40), 0.8, 1e-9);
}

// Letting go finishes the step from half way, or from anywhere on its side with a flick toward
// it; a flick away goes back from anywhere.
static void finishing() {
    CHECK(sh_swipe_finishes(0.5, 0));
    CHECK(sh_swipe_finishes(0.9, 0.2));
    CHECK(!sh_swipe_finishes(0.49, 0));
    CHECK(!sh_swipe_finishes(0.3, SH_SWIPE_FLICK - 0.01));
    CHECK(sh_swipe_finishes(0.1, SH_SWIPE_FLICK));
    CHECK(sh_swipe_finishes(0.05, 3));
    CHECK(!sh_swipe_finishes(0.9, -SH_SWIPE_FLICK));
    CHECK(sh_swipe_finishes(0.9, -SH_SWIPE_FLICK + 0.01));
    CHECK(!sh_swipe_finishes(-0.2, 2)); // heading for the step, but not past the start yet
    CHECK(!sh_swipe_finishes(0, 2));
    CHECK(!sh_swipe_finishes(-0.7, 0));
}

static void names() {
    CHECK(!std::strcmp(sh_swipe_direction_name(SH_SWIPE_LEFT), "left"));
    CHECK(!std::strcmp(sh_swipe_direction_name(SH_SWIPE_DOWN), "down"));
    CHECK(!std::strcmp(sh_swipe_direction_name(SH_SWIPE_NONE), "none"));
    CHECK(!std::strcmp(sh_swipe_direction_name(static_cast<sh_swipe_direction>(42)), "none"));
    CHECK(sh_swipe_opposite(SH_SWIPE_UP) == SH_SWIPE_DOWN);
    CHECK(sh_swipe_opposite(SH_SWIPE_LEFT) == SH_SWIPE_RIGHT);
    CHECK(sh_swipe_opposite(SH_SWIPE_NONE) == SH_SWIPE_NONE);
}

int main() {
    directions();
    progress();
    speed();
    finishing();
    names();
    if (failures)
        std::fprintf(stderr, "%d swipe checks failed\n", failures);
    else
        std::puts("swipe tests passed");
    return failures ? 1 : 0;
}
