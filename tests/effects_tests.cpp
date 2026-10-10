// SPDX-License-Identifier: GPL-3.0-or-later
#include "paw/effects.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "check.h"

static void fades() {
    sh_fade fade;
    sh_fade_init(&fade, 0.5);
    NEAR(sh_fade_value(&fade, 12345), 0.5, 1e-12);
    CHECK(!sh_fade_active(&fade, 0));
    sh_fade_to(&fade, 1, 1000, 200);
    NEAR(sh_fade_value(&fade, 1000), 0.5, 1e-12);
    NEAR(sh_fade_value(&fade, 1200), 1, 1e-12);
    NEAR(sh_fade_value(&fade, 5000), 1, 1e-12);
    CHECK(sh_fade_active(&fade, 1100));
    CHECK(!sh_fade_active(&fade, 1200));
    // Ease-out: more than half done at half the time, and never backwards.
    CHECK(sh_fade_value(&fade, 1100) > 0.75);
    double last = 0;
    for (int t = 1000; t <= 1200; t += 10) {
        CHECK(sh_fade_value(&fade, t) >= last);
        last = sh_fade_value(&fade, t);
    }
    // Retargeting mid-fade starts from where the value is, without a jump.
    double before = sh_fade_value(&fade, 1050);
    sh_fade_to(&fade, 0, 1050, 200);
    NEAR(sh_fade_value(&fade, 1050), before, 1e-12);
    NEAR(sh_fade_value(&fade, 1250), 0, 1e-12);
    // No duration: at once.
    sh_fade_to(&fade, 0.3, 2000, 0);
    NEAR(sh_fade_value(&fade, 2000), 0.3, 1e-12);
    CHECK(!sh_fade_active(&fade, 2000));
    // A time before the start counts as the start.
    sh_fade_to(&fade, 1, 3000, 100);
    NEAR(sh_fade_value(&fade, 2000), 0.3, 1e-12);
}

static void temperatures() {
    sh_rgb neutral = sh_kelvin_to_rgb(6500);
    NEAR(neutral.r, 1, 1e-9);
    NEAR(neutral.g, 1, 1e-9);
    NEAR(neutral.b, 1, 1e-9);
    // Warmer: red stays, blue drops fastest, and each step is monotonic.
    double last_blue = 1, last_green = 1;
    for (int k = 6400; k >= 1000; k -= 100) {
        sh_rgb c = sh_kelvin_to_rgb(k);
        NEAR(c.r, 1, 1e-9);
        CHECK(c.b <= last_blue + 1e-9);
        CHECK(c.g <= last_green + 1e-9);
        CHECK(c.b <= c.g + 1e-9 && c.g <= c.r + 1e-9);
        CHECK(c.b > 0 && c.g > 0);
        last_blue = c.b, last_green = c.g;
    }
    sh_rgb evening = sh_kelvin_to_rgb(3500);
    CHECK(evening.g > 0.6 && evening.g < 0.85);
    CHECK(evening.b > 0.3 && evening.b < 0.65);
    // Cooler than neutral: blue stays, red drops.
    sh_rgb cool = sh_kelvin_to_rgb(9000);
    NEAR(cool.b, 1, 1e-9);
    CHECK(cool.r < 1);
    // Out of range is clamped, not undefined.
    sh_rgb low = sh_kelvin_to_rgb(-5), lowest = sh_kelvin_to_rgb(SH_KELVIN_MIN);
    NEAR(low.g, lowest.g, 1e-12);
    sh_rgb high = sh_kelvin_to_rgb(1000000), highest = sh_kelvin_to_rgb(SH_KELVIN_MAX);
    NEAR(high.r, highest.r, 1e-12);
}

static void ramps() {
    std::vector<uint16_t> ramp(3 * 256);
    sh_gamma_ramp({1, 0.5, 0.25}, 256, ramp.data());
    CHECK(ramp[0] == 0 && ramp[256] == 0 && ramp[512] == 0);
    CHECK(ramp[255] == 65535);
    NEAR(ramp[256 + 255], 32768, 1);
    NEAR(ramp[512 + 255], 16384, 1);
    NEAR(ramp[128], 65535 * 128 / 255.0, 1);
    for (size_t i = 1; i < 256; ++i)
        CHECK(ramp[i] >= ramp[i - 1]);
    // A neutral ramp is the identity; a one-entry table is the full-scale value.
    sh_gamma_ramp({1, 1, 1}, 256, ramp.data());
    NEAR(ramp[100], 65535 * 100 / 255.0, 1);
    uint16_t single[3];
    sh_gamma_ramp({1, 0.5, 0.25}, 1, single);
    CHECK(single[0] == 65535);
}

static void schedule() {
    sh_night_schedule s{6500, 3500, 7 * 60, 19 * 60 + 30, 60};
    CHECK(sh_night_kelvin(&s, 12 * 60) == 6500);
    CHECK(sh_night_kelvin(&s, 23 * 60) == 3500);
    CHECK(sh_night_kelvin(&s, 3 * 60) == 3500);
    // Centred on the change: halfway at sunrise and sunset.
    CHECK(std::abs(sh_night_kelvin(&s, 7 * 60) - 5000) <= 1);
    CHECK(std::abs(sh_night_kelvin(&s, 19 * 60 + 30) - 5000) <= 1);
    // The change lasts an hour and is monotonic through it.
    CHECK(sh_night_kelvin(&s, 6 * 60 + 29) == 3500);
    CHECK(sh_night_kelvin(&s, 7 * 60 + 31) == 6500);
    int last = 0;
    for (double m = 6 * 60 + 30; m <= 7 * 60 + 30; m += 1) {
        int k = sh_night_kelvin(&s, m);
        CHECK(k >= last);
        last = k;
    }
    last = 6500;
    for (double m = 19 * 60; m <= 20 * 60; m += 1) {
        int k = sh_night_kelvin(&s, m);
        CHECK(k <= last);
        last = k;
    }
    // Instant changes with no transition.
    sh_night_schedule sharp{6500, 3500, 420, 1170, 0};
    CHECK(sh_night_kelvin(&sharp, 419.9) == 3500);
    CHECK(sh_night_kelvin(&sharp, 420) == 6500);
    CHECK(sh_night_kelvin(&sharp, 1169.9) == 6500);
    CHECK(sh_night_kelvin(&sharp, 1170) == 3500);
    // A day through midnight (sunset before sunrise): a night shift worker's schedule.
    sh_night_schedule wrap{6500, 3500, 20 * 60, 6 * 60, 30};
    CHECK(sh_night_kelvin(&wrap, 23 * 60) == 6500);
    CHECK(sh_night_kelvin(&wrap, 0) == 6500);
    CHECK(sh_night_kelvin(&wrap, 12 * 60) == 3500);
    CHECK(std::abs(sh_night_kelvin(&wrap, 6 * 60) - 5000) <= 1);
    // The transition around midnight itself.
    sh_night_schedule midnight{6500, 3500, 0, 12 * 60, 60};
    CHECK(std::abs(sh_night_kelvin(&midnight, 0) - 5000) <= 1);
    CHECK(sh_night_kelvin(&midnight, 1439) < 5100);
    CHECK(sh_night_kelvin(&midnight, 1439) > 4900);
    CHECK(sh_night_kelvin(&midnight, 6 * 60) == 6500);
    // Two transitions never overlap: a short day shortens them.
    sh_night_schedule short_day{6500, 3500, 12 * 60, 12 * 60 + 20, 120};
    CHECK(sh_night_kelvin(&short_day, 12 * 60 + 10) > 3500);
    CHECK(sh_night_kelvin(&short_day, 3 * 60) == 3500);
    // Equal times: always night. Never a crash or NaN.
    sh_night_schedule none{6500, 3500, 600, 600, 30};
    CHECK(sh_night_kelvin(&none, 600) == 3500);
    // The same colour by day and night changes nothing.
    sh_night_schedule flat{5000, 5000, 420, 1170, 60};
    for (double m = 0; m < 1440; m += 37)
        CHECK(sh_night_kelvin(&flat, m) == 5000);
}

static void solar() {
    double rise, set;
    bool polar_day = false;
    // Oslo on the solstices (CEST and CET): timeanddate gives 03:53 and 22:44, 09:18 and 15:12.
    CHECK(sh_solar_times(59.91, 10.75, 2026, 6, 21, 2, &rise, &set, &polar_day));
    NEAR(rise, 3 * 60 + 53, 6);
    NEAR(set, 22 * 60 + 44, 6);
    CHECK(sh_solar_times(59.91, 10.75, 2026, 12, 21, 1, &rise, &set, &polar_day));
    NEAR(rise, 9 * 60 + 18, 6);
    NEAR(set, 15 * 60 + 12, 6);
    // On the equator at the equinox the day is twelve hours, the sun up at about 6:00 solar time.
    CHECK(sh_solar_times(0, 0, 2026, 3, 20, 0, &rise, &set, &polar_day));
    NEAR(set - rise, 12 * 60 + 7, 6);
    NEAR((rise + set) / 2, 12 * 60 + 7, 3); // solar noon, the equation of time is -7 minutes
    // A different time zone moves both by the offset.
    double rise2, set2;
    CHECK(sh_solar_times(0, 0, 2026, 3, 20, 3, &rise2, &set2, &polar_day));
    NEAR(rise2 - rise, 180, 1e-6);
    // Polar night and midnight sun have neither.
    CHECK(!sh_solar_times(78, 15, 2026, 12, 21, 1, &rise, &set, &polar_day));
    CHECK(!polar_day);
    CHECK(!sh_solar_times(78, 15, 2026, 6, 21, 2, &rise, &set, &polar_day));
    CHECK(polar_day);
    // Impossible places and dates are refused.
    CHECK(!sh_solar_times(91, 0, 2026, 6, 21, 0, &rise, &set, &polar_day));
    CHECK(!sh_solar_times(0, 181, 2026, 6, 21, 0, &rise, &set, &polar_day));
    CHECK(!sh_solar_times(0, 0, 2026, 13, 1, 0, &rise, &set, &polar_day));
    // Southern hemisphere: Sydney's June day is short.
    CHECK(sh_solar_times(-33.87, 151.21, 2026, 6, 21, 10, &rise, &set, &polar_day));
    NEAR(set - rise, 9 * 60 + 54, 10);
    // Leap days work.
    CHECK(sh_solar_times(50, 8, 2028, 2, 29, 1, &rise, &set, &polar_day));
    CHECK(set > rise);
}

static void clock_text() {
    double minutes = -1;
    CHECK(sh_parse_clock("07:30", &minutes) && minutes == 450);
    CHECK(sh_parse_clock("00:00", &minutes) && minutes == 0);
    CHECK(sh_parse_clock("23:59", &minutes) && minutes == 1439);
    for (const char *bad : {"", "7:30", "24:00", "12:60", "12-30", "ab:cd", "07:300", "07:3", " 07:30"})
        CHECK(!sh_parse_clock(bad, &minutes));
    CHECK(!sh_parse_clock(nullptr, &minutes));
}

static void corners() {
    CHECK(sh_corner_at(0, 0, 1920, 1080, 2) == SH_CORNER_TOP_LEFT);
    CHECK(sh_corner_at(1919, 0, 1920, 1080, 2) == SH_CORNER_TOP_RIGHT);
    CHECK(sh_corner_at(0, 1079, 1920, 1080, 2) == SH_CORNER_BOTTOM_LEFT);
    CHECK(sh_corner_at(1919.5, 1079.5, 1920, 1080, 2) == SH_CORNER_BOTTOM_RIGHT);
    CHECK(sh_corner_at(1.9, 1.9, 1920, 1080, 2) == SH_CORNER_TOP_LEFT);
    CHECK(sh_corner_at(2, 0, 1920, 1080, 2) == -1);
    CHECK(sh_corner_at(0, 2, 1920, 1080, 2) == -1);
    CHECK(sh_corner_at(960, 0, 1920, 1080, 2) == -1);
    CHECK(sh_corner_at(-1, 0, 1920, 1080, 2) == -1);
    CHECK(sh_corner_at(0, 0, 1920, 1080, 0) == -1);
    CHECK(sh_corner_at(1920, 0, 1920, 1080, 2) == -1);
    // A zone larger than half the output still names one corner.
    CHECK(sh_corner_at(5, 5, 10, 10, 20) == SH_CORNER_TOP_LEFT);

    sh_corner_dwell dwell;
    sh_corner_dwell_init(&dwell);
    CHECK(sh_corner_dwell_update(&dwell, -1, 0, 100) == -1);
    CHECK(sh_corner_dwell_wait(&dwell, 0, 100) == -1);
    // Enter, wait less than the delay: nothing; then it fires once.
    CHECK(sh_corner_dwell_update(&dwell, SH_CORNER_TOP_LEFT, 1000, 100) == -1);
    CHECK(sh_corner_dwell_wait(&dwell, 1040, 100) == 60);
    CHECK(sh_corner_dwell_update(&dwell, SH_CORNER_TOP_LEFT, 1099, 100) == -1);
    CHECK(sh_corner_dwell_update(&dwell, SH_CORNER_TOP_LEFT, 1100, 100) == SH_CORNER_TOP_LEFT);
    CHECK(sh_corner_dwell_update(&dwell, SH_CORNER_TOP_LEFT, 1500, 100) == -1);
    CHECK(sh_corner_dwell_wait(&dwell, 1500, 100) == -1);
    // Leaving and coming back arms it again.
    CHECK(sh_corner_dwell_update(&dwell, -1, 1600, 100) == -1);
    CHECK(sh_corner_dwell_update(&dwell, SH_CORNER_TOP_LEFT, 1700, 100) == -1);
    CHECK(sh_corner_dwell_update(&dwell, SH_CORNER_TOP_LEFT, 1800, 100) == SH_CORNER_TOP_LEFT);
    // Sliding along to another corner restarts the wait.
    CHECK(sh_corner_dwell_update(&dwell, SH_CORNER_TOP_RIGHT, 1810, 100) == -1);
    CHECK(sh_corner_dwell_update(&dwell, SH_CORNER_TOP_RIGHT, 1910, 100) == SH_CORNER_TOP_RIGHT);
    // No delay: at once.
    sh_corner_dwell_init(&dwell);
    CHECK(sh_corner_dwell_update(&dwell, SH_CORNER_BOTTOM_LEFT, 5, 0) == SH_CORNER_BOTTOM_LEFT);
}

static void zoom() {
    // Level 1 shows everything.
    sh_view whole = sh_zoom_view(1, 1920, 1080, 700, 300);
    NEAR(whole.x, 0, 1e-9);
    NEAR(whole.y, 0, 1e-9);
    NEAR(whole.width, 1920, 1e-9);
    NEAR(whole.height, 1080, 1e-9);
    // The pointer is a fixed point: it shows where it is, at any level and place.
    for (double level : {1.5, 2.0, 3.7, 8.0})
        for (auto [px, py] : {std::pair{0.0, 0.0}, {1920.0, 1080.0}, {960.0, 540.0}, {17.0, 1000.0}}) {
            sh_view view = sh_zoom_view(level, 1920, 1080, px, py);
            // Where the pointer shows on the screen, which the view fills.
            NEAR((px - view.x) * 1920 / view.width, px, 1e-6);
            NEAR((py - view.y) * 1080 / view.height, py, 1e-6);
            NEAR(view.width, 1920 / level, 1e-9);
            // The view never leaves the output.
            CHECK(view.x >= -1e-9 && view.y >= -1e-9);
            CHECK(view.x + view.width <= 1920 + 1e-9);
            CHECK(view.y + view.height <= 1080 + 1e-9);
        }
    // In a corner the view is the corner; all four edges of the output stay reachable.
    sh_view corner = sh_zoom_view(4, 1920, 1080, 1920, 1080);
    NEAR(corner.x + corner.width, 1920, 1e-9);
    NEAR(corner.y + corner.height, 1080, 1e-9);
    // A pointer outside is clamped; a level below 1 is 1.
    sh_view outside = sh_zoom_view(2, 100, 100, -50, 500);
    NEAR(outside.x, 0, 1e-9);
    NEAR(outside.y + outside.height, 100, 1e-9);
    NEAR(sh_zoom_view(0.2, 100, 100, 50, 50).width, 100, 1e-9);
    // Levels.
    NEAR(sh_zoom_level(1, 2, 1, 8), 2, 1e-12);
    NEAR(sh_zoom_level(2, 2, 5, 8), 8, 1e-12);
    NEAR(sh_zoom_level(2, 2, -3, 8), 1, 1e-12);
    NEAR(sh_zoom_level(4, 1.25, 0, 8), 4, 1e-12);
    NEAR(sh_zoom_level(1, 1.25, 2, 8), 1.5625, 1e-12);
    NEAR(sh_zoom_level(1, 2, 3, 0.5), 1, 1e-12);
}

int main() {
    fades();
    temperatures();
    ramps();
    schedule();
    solar();
    clock_text();
    corners();
    zoom();
    if (failures)
        std::fprintf(stderr, "%d checks failed\n", failures);
    else
        std::puts("Effects arithmetic passed");
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
