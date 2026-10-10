// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* The arithmetic behind the desktop effects: fades, colour temperature and its daily
 * schedule, hot corner dwell, and the magnifier's viewport. Nothing here touches wlroots or
 * the clock, so every function takes the time it should work at and can be tested on its own. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A value moving to a target over a duration. A new target starts from wherever the value is
 * at that moment, so retargeting mid-fade never jumps. */
struct sh_fade {
    double from, to;
    int64_t start; /* milliseconds */
    int duration;
};
void sh_fade_init(struct sh_fade *fade, double value);
double sh_fade_value(const struct sh_fade *fade, int64_t now);
/* A duration of 0 or less jumps to the target. */
void sh_fade_to(struct sh_fade *fade, double target, int64_t now, int duration);
bool sh_fade_active(const struct sh_fade *fade, int64_t now);

/* Night light. Colour temperatures are in kelvin; 6500 is neutral. */
enum { SH_KELVIN_MIN = 1000, SH_KELVIN_MAX = 10000, SH_KELVIN_NEUTRAL = 6500 };
struct sh_rgb {
    double r, g, b;
};
/* Per-channel factors in (0, 1] that give a white pixel the colour of a black body at
 * `kelvin`, the strongest channel at 1 so the screen only gets dimmer where it gets warmer.
 * 6500 gives 1, 1, 1. The temperature is clamped to the supported range. */
struct sh_rgb sh_kelvin_to_rgb(int kelvin);
/* Fills `out` with `3 * size` entries: `size` red, then green, then blue, each a ramp from 0 to
 * the channel's factor, as wlr-gamma-control and DRM expect. */
void sh_gamma_ramp(struct sh_rgb factors, size_t size, uint16_t *out);

/* When the temperature changes, in minutes after local midnight, both in [0, 1440). The
 * change is centred on the time and lasts `transition` minutes. `sunset` before `sunrise`
 * means the day runs through midnight. */
struct sh_night_schedule {
    int day_kelvin, night_kelvin;
    double sunrise, sunset;
    double transition;
};
/* The day's temperature in full day, the night's in full night, smooth in between. */
int sh_night_kelvin(const struct sh_night_schedule *schedule, double minute);

/* Sunrise and sunset in minutes after local midnight for a place (degrees, north and east
 * positive) on a date, `utc_offset` hours from UTC, with the NOAA approximation (good to a
 * few minutes). False in polar day or night, when there is neither; then *polar_day says which. */
bool sh_solar_times(double latitude, double longitude, int year, int month, int day,
                    double utc_offset, double *sunrise, double *sunset, bool *polar_day);
/* "HH:MM" (24 hours) to minutes after midnight; false for anything else. */
bool sh_parse_clock(const char *text, double *minutes);

/* Hot corners. */
enum sh_corner {
    SH_CORNER_TOP_LEFT,
    SH_CORNER_TOP_RIGHT,
    SH_CORNER_BOTTOM_LEFT,
    SH_CORNER_BOTTOM_RIGHT,
    SH_CORNER_COUNT,
};
/* The corner of a width x height box whose size x size square holds (x, y), or -1. */
int sh_corner_at(double x, double y, int width, int height, int size);
/* A corner fires once the pointer has stayed in it for `delay` milliseconds, then not again
 * until the pointer has left it. */
struct sh_corner_dwell {
    int corner; /* -1: outside every corner */
    int64_t entered;
    bool fired;
};
void sh_corner_dwell_init(struct sh_corner_dwell *dwell);
/* The pointer is now in `corner` (or -1). Returns the corner to run, or -1. */
int sh_corner_dwell_update(struct sh_corner_dwell *dwell, int corner, int64_t now, int delay);
/* Milliseconds until the corner in `dwell` fires if the pointer stays, or -1 for never. */
int sh_corner_dwell_wait(const struct sh_corner_dwell *dwell, int64_t now, int delay);

/* Magnifier. The viewport is the part of an output, in output coordinates, that fills it. */
struct sh_view {
    double x, y, width, height;
};
/* At `level` (1 and up) about the point (px, py) of a width x height output. The point keeps
 * its relative place on screen (it is the fixed point of the zoom), so the pointer stays
 * under the magnified pointer; the view stays inside the output. */
struct sh_view sh_zoom_view(double level, double width, double height, double px, double py);
/* Where the logical point (x, y) appears on screen in `view`, and the reverse. */
void sh_view_to_screen(const struct sh_view *view, double width, double height, double x,
                       double y, double *sx, double *sy);
void sh_view_to_logical(const struct sh_view *view, double width, double height, double sx,
                        double sy, double *x, double *y);
/* The level after `steps` zoom steps of a factor `step` each, kept within [1, maximum]. */
double sh_zoom_level(double level, double step, int steps, double maximum);

#ifdef __cplusplus
}
#endif
