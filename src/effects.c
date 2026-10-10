// SPDX-License-Identifier: GPL-3.0-or-later
#include "paw/effects.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static double clamp(double value, double low, double high) {
    return value < low ? low : value > high ? high : value;
}

/* Ease-out cubic, t clamped to [0, 1]: fast at first, settling gently. */
static double ease_out(double t) {
    t = clamp(t, 0, 1);
    return 1 - (1 - t) * (1 - t) * (1 - t);
}

void sh_fade_init(struct sh_fade *fade, double value) {
    *fade = (struct sh_fade){.from = value, .to = value};
}

double sh_fade_value(const struct sh_fade *fade, int64_t now) {
    if (fade->duration <= 0)
        return fade->to;
    return fade->from +
           (fade->to - fade->from) * ease_out((double)(now - fade->start) / fade->duration);
}

void sh_fade_to(struct sh_fade *fade, double target, int64_t now, int duration) {
    double current = sh_fade_value(fade, now);
    if (duration <= 0) {
        sh_fade_init(fade, target);
        return;
    }
    fade->from = current;
    fade->to = target;
    fade->start = now;
    fade->duration = duration;
}

bool sh_fade_active(const struct sh_fade *fade, int64_t now) {
    return fade->duration > 0 && now - fade->start < fade->duration && fade->from != fade->to;
}

/* Tanner Helland's fit of the black body locus, in [0, 255] per channel. */
static struct sh_rgb blackbody(double kelvin) {
    double t = kelvin / 100;
    double r = t <= 66 ? 255 : 329.698727446 * pow(t - 60, -0.1332047592);
    double g = t <= 66 ? 99.4708025861 * log(t) - 161.1195681661
                       : 288.1221695283 * pow(t - 60, -0.0755148492);
    double b = t >= 66 ? 255 : t <= 19 ? 0 : 138.5177312231 * log(t - 10) - 305.0447927307;
    return (struct sh_rgb){clamp(r, 1, 255), clamp(g, 1, 255), clamp(b, 1, 255)};
}

struct sh_rgb sh_kelvin_to_rgb(int kelvin) {
    struct sh_rgb white = blackbody(SH_KELVIN_NEUTRAL);
    struct sh_rgb c = blackbody(clamp(kelvin, SH_KELVIN_MIN, SH_KELVIN_MAX));
    struct sh_rgb f = {c.r / white.r, c.g / white.g, c.b / white.b};
    double strongest = fmax(f.r, fmax(f.g, f.b));
    return (struct sh_rgb){f.r / strongest, f.g / strongest, f.b / strongest};
}

void sh_gamma_ramp(struct sh_rgb factors, size_t size, uint16_t *out) {
    const double channel[3] = {factors.r, factors.g, factors.b};
    for (size_t c = 0; c < 3; ++c)
        for (size_t i = 0; i < size; ++i) {
            double ramp = size > 1 ? (double)i / (double)(size - 1) : 1;
            out[c * size + i] = (uint16_t)lround(clamp(ramp * channel[c], 0, 1) * UINT16_MAX);
        }
}

static double smoothstep(double t) {
    t = clamp(t, 0, 1);
    return t * t * (3 - 2 * t);
}

/* `minutes` folded into [-720, 720). */
static double around(double minutes) {
    minutes = fmod(minutes, 1440);
    if (minutes < -720)
        minutes += 1440;
    else if (minutes >= 720)
        minutes -= 1440;
    return minutes;
}

/* 1 in full day, 0 in full night, smooth in between. */
static double daylight(const struct sh_night_schedule *schedule, double minute) {
    double day = fmod(schedule->sunset - schedule->sunrise + 1440, 1440);
    if (day == 0)
        return 0;
    // The two changes must not overlap.
    double transition = clamp(schedule->transition, 0, fmin(day, 1440 - day));
    double since_sunrise = fmod(minute - schedule->sunrise + 1440, 1440);
    double rising = around(since_sunrise), falling = around(since_sunrise - day);
    if (transition > 0 && fabs(rising) < transition / 2)
        return smoothstep((rising + transition / 2) / transition);
    if (transition > 0 && fabs(falling) < transition / 2)
        return 1 - smoothstep((falling + transition / 2) / transition);
    return since_sunrise < day ? 1 : 0;
}

int sh_night_kelvin(const struct sh_night_schedule *schedule, double minute) {
    double day = daylight(schedule, minute);
    return (int)lround(schedule->night_kelvin +
                       (schedule->day_kelvin - schedule->night_kelvin) * day);
}

static double radians(double degrees) { return degrees * M_PI / 180; }

static int day_of_year(int year, int month, int day) {
    static const int before[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return before[month - 1] + day + (leap && month > 2);
}

bool sh_solar_times(double latitude, double longitude, int year, int month, int day,
                    double utc_offset, double *sunrise, double *sunset, bool *polar_day) {
    if (month < 1 || month > 12 || day < 1 || day > 31 || fabs(latitude) > 90 ||
        fabs(longitude) > 180)
        return false;
    bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    double gamma = 2 * M_PI / (leap ? 366 : 365) * (day_of_year(year, month, day) - 1);
    double equation = 229.18 * (0.000075 + 0.001868 * cos(gamma) - 0.032077 * sin(gamma) -
                                0.014615 * cos(2 * gamma) - 0.040849 * sin(2 * gamma));
    double declination = 0.006918 - 0.399912 * cos(gamma) + 0.070257 * sin(gamma) -
                         0.006758 * cos(2 * gamma) + 0.000907 * sin(2 * gamma) -
                         0.002697 * cos(3 * gamma) + 0.00148 * sin(3 * gamma);
    double lat = radians(latitude);
    // The sun's centre 0.833 degrees below the horizon: its radius and the refraction.
    double cosine = cos(radians(90.833)) / (cos(lat) * cos(declination)) -
                    tan(lat) * tan(declination);
    if (cosine < -1 || cosine > 1) {
        if (polar_day)
            *polar_day = cosine < -1;
        return false;
    }
    double angle = acos(cosine) * 180 / M_PI;
    double rise = 720 - 4 * (longitude + angle) - equation + utc_offset * 60;
    double set = 720 - 4 * (longitude - angle) - equation + utc_offset * 60;
    *sunrise = fmod(fmod(rise, 1440) + 1440, 1440);
    *sunset = fmod(fmod(set, 1440) + 1440, 1440);
    return true;
}

bool sh_parse_clock(const char *text, double *minutes) {
    if (!text || strlen(text) != 5 || text[2] != ':')
        return false;
    for (int i = 0; i < 5; ++i)
        if (i != 2 && (text[i] < '0' || text[i] > '9'))
            return false;
    int hours = (text[0] - '0') * 10 + text[1] - '0', mins = (text[3] - '0') * 10 + text[4] - '0';
    if (hours > 23 || mins > 59)
        return false;
    *minutes = hours * 60 + mins;
    return true;
}

int sh_corner_at(double x, double y, int width, int height, int size) {
    if (size <= 0 || x < 0 || y < 0 || x >= width || y >= height)
        return -1;
    bool left = x < size, right = x >= width - size, top = y < size, bottom = y >= height - size;
    if (top && left)
        return SH_CORNER_TOP_LEFT;
    if (top && right)
        return SH_CORNER_TOP_RIGHT;
    if (bottom && left)
        return SH_CORNER_BOTTOM_LEFT;
    if (bottom && right)
        return SH_CORNER_BOTTOM_RIGHT;
    return -1;
}

void sh_corner_dwell_init(struct sh_corner_dwell *dwell) {
    *dwell = (struct sh_corner_dwell){.corner = -1};
}

int sh_corner_dwell_update(struct sh_corner_dwell *dwell, int corner, int64_t now, int delay) {
    if (corner != dwell->corner) {
        dwell->corner = corner;
        dwell->entered = now;
        dwell->fired = false;
    }
    if (corner < 0 || dwell->fired || now - dwell->entered < delay)
        return -1;
    dwell->fired = true;
    return corner;
}

int sh_corner_dwell_wait(const struct sh_corner_dwell *dwell, int64_t now, int delay) {
    if (dwell->corner < 0 || dwell->fired)
        return -1;
    int64_t left = dwell->entered + delay - now;
    return left < 0 ? 0 : (int)left;
}

struct sh_view sh_zoom_view(double level, double width, double height, double px, double py) {
    level = fmax(level, 1);
    px = clamp(px, 0, width);
    py = clamp(py, 0, height);
    return (struct sh_view){px * (1 - 1 / level), py * (1 - 1 / level), width / level,
                            height / level};
}

double sh_zoom_level(double level, double step, int steps, double maximum) {
    return clamp(level * pow(step, steps), 1, fmax(maximum, 1));
}
