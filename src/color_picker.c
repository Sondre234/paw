// SPDX-License-Identifier: GPL-3.0-or-later
/* The colour picker's loupe and its arithmetic (paw/color_picker.h): laid out and painted in
 * whole pixels at the output's scale, with anti-aliased edges, the label in strokes of its own. */
#include "paw/color_picker.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The loupe's sizes, logical pixels: a cell, the card's padding, the room between the grid and
 * the label and the label's height, the corners' radii, the swatch, the label's capitals, how far
 * the shadow reaches past the card, and how far the crosshair's arms reach from its middle, and
 * from where. */
#define CELL 10.0
#define PAD 8.0
#define LABEL_GAP 8.0
#define LABEL_HEIGHT 16.0
#define CARD_RADIUS 14.0
#define SWATCH 14.0
#define SWATCH_RADIUS 4.0
#define SWATCH_GAP 7.0
#define TEXT_CAP 11.0
#define SHADOW_MARGIN 22.0
#define SHADOW_SIGMA 6.0
#define SHADOW_DROP 3.0
#define CROSS_REACH 9.0
#define CROSS_GAP 3.0

static double clamp01(double value) { return value < 0 ? 0 : value > 1 ? 1 : value; }

/* `logical` pixels at `scale`, whole, at least one. */
static int pixels_of(double logical, double scale) {
    long value = lround(logical * scale);
    return value < 1 ? 1 : (int)value;
}

/* How many logical pixels a length must be a multiple of to be whole pixels at `scale`: 1 for a
 * whole scale, 4 for 1.25 or 1.75, 2 for 1.5; 1 too where that would take more than 8, the
 * picture then drawn a fraction of a pixel smaller or larger. */
static int whole_step(double scale) {
    double eighths = scale * 120;
    long n = lround(eighths);
    if (fabs(eighths - (double)n) > 1e-6 || n <= 0)
        return 1;
    long a = n, b = 120;
    while (b) {
        long t = a % b;
        a = b;
        b = t;
    }
    long step = 120 / a;
    return step <= 8 ? (int)step : 1;
}

/* wlroots' hotspot in pixels for a logical one, as it places a cursor. */
static int hotspot_pixels(int logical, double scale) {
    return (int)roundf((float)logical * (float)scale);
}

static void card_pixels(double scale, int *cell, int *pad, int *width, int *height) {
    *cell = (int)lround(CELL * scale);
    if (*cell < 3)
        *cell = 3;
    *pad = pixels_of(PAD, scale);
    int grid = SH_LOUPE_CELLS * *cell;
    *width = grid + 2 * *pad;
    *height = *pad + grid + pixels_of(LABEL_GAP, scale) + pixels_of(LABEL_HEIGHT, scale) + *pad;
}

void sh_loupe_card_size(double scale, double *width, double *height) {
    if (!(scale > 0))
        scale = 1;
    int cell, pad, w, h;
    card_pixels(scale, &cell, &pad, &w, &h);
    *width = w / scale;
    *height = h / scale;
}

/* Along one axis: where the picture starts and how long it is, the hotspot (logical and pixels)
 * and where the card starts, given the card's length and side. */
static void lay_axis(double scale, int card, bool before, int step, int *length, int *logical,
                     int *hot, int *hot_pixel, int *card_at) {
    int offset = pixels_of(SH_LOUPE_OFFSET, scale), margin = pixels_of(SHADOW_MARGIN, scale);
    int reach = pixels_of(CROSS_REACH, scale) + 2; // and its halo
    // From the hotspot pixel's near edge: the card, symmetric about that pixel's middle.
    int card_from = before ? 1 - offset - card : offset;
    int low = card_from - margin < -reach ? card_from - margin : -reach;
    int high = card_from + card + margin > 1 + reach ? card_from + card + margin : 1 + reach;
    *hot = (int)ceil(-low / scale - 1e-9);
    *hot_pixel = hotspot_pixels(*hot, scale);
    int needed = *hot_pixel + high;
    int whole = (int)ceil(needed / scale - 1e-9);
    whole = (whole + step - 1) / step * step;
    *logical = whole;
    *length = (int)lround(whole * scale);
    if (*length < needed)
        *length = needed;
    *card_at = *hot_pixel + card_from;
}

void sh_loupe_layout(double scale, bool left, bool above, struct sh_loupe_layout *layout) {
    if (!(scale > 0))
        scale = 1;
    memset(layout, 0, sizeof(*layout));
    layout->scale = scale;
    layout->left = left;
    layout->above = above;
    int pad;
    card_pixels(scale, &layout->cell, &pad, &layout->card_width, &layout->card_height);
    layout->card_radius = pixels_of(CARD_RADIUS, scale);
    layout->grid_radius = layout->card_radius - pad > 1 ? layout->card_radius - pad : 1;
    int step = whole_step(scale);
    lay_axis(scale, layout->card_width, left, step, &layout->width, &layout->logical_width,
             &layout->hot_x, &layout->hot_pixel_x, &layout->card_x);
    lay_axis(scale, layout->card_height, above, step, &layout->height, &layout->logical_height,
             &layout->hot_y, &layout->hot_pixel_y, &layout->card_y);
    layout->grid_x = layout->card_x + pad;
    layout->grid_y = layout->card_y + pad;
    int grid = SH_LOUPE_CELLS * layout->cell;
    double row_top = layout->grid_y + grid + pixels_of(LABEL_GAP, scale);
    double row_middle = row_top + pixels_of(LABEL_HEIGHT, scale) / 2.0;
    layout->swatch_size = pixels_of(SWATCH, scale);
    layout->swatch_radius = pixels_of(SWATCH_RADIUS, scale);
    layout->text_cap = TEXT_CAP * scale;
    double text = sh_loupe_text_width("#000000", layout->text_cap);
    double group = layout->swatch_size + SWATCH_GAP * scale + text;
    layout->swatch_x = (int)lround(layout->card_x + (layout->card_width - group) / 2);
    layout->swatch_y = (int)lround(row_middle - layout->swatch_size / 2.0);
    layout->text_x = layout->swatch_x + layout->swatch_size + SWATCH_GAP * scale;
    layout->text_top = row_middle - layout->text_cap / 2;
    layout->card_logical_width = layout->card_width / scale;
    layout->card_logical_height = layout->card_height / scale;
}

/* The signed distance from (px, py) to the edge of a box with rounded corners: negative inside. */
static double box_distance(double px, double py, double x, double y, double width, double height,
                           double radius) {
    double half_w = width / 2, half_h = height / 2;
    if (radius > half_w)
        radius = half_w;
    if (radius > half_h)
        radius = half_h;
    double qx = fabs(px - (x + half_w)) - (half_w - radius);
    double qy = fabs(py - (y + half_h)) - (half_h - radius);
    double outside = hypot(qx > 0 ? qx : 0, qy > 0 ? qy : 0);
    double inside = qx > qy ? qx : qy;
    return outside + (inside < 0 ? inside : 0) - radius;
}

/* How much of a pixel at distance `d` from an edge lies inside (d negative inside). */
static double coverage(double d) { return clamp01(0.5 - d); }

/* How much of a pixel at distance `d` lies in the band from `from` to `to` along it. */
static double band(double d, double from, double to) {
    double inner = d - from, outer = to - d;
    return clamp01((inner < outer ? inner : outer) + 0.5);
}

/* `rgb` at `alpha` laid over a premultiplied ARGB pixel. */
static void blend(uint32_t *pixel, uint32_t rgb, double alpha) {
    alpha = clamp01(alpha);
    if (alpha <= 0)
        return;
    double keep = 1 - alpha;
    uint32_t out = 0;
    for (int shift = 0; shift < 24; shift += 8) {
        double value = ((rgb >> shift) & 0xff) * alpha + ((*pixel >> shift) & 0xff) * keep;
        out |= (uint32_t)lround(value > 255 ? 255 : value) << shift;
    }
    double a = 255 * alpha + ((*pixel >> 24) & 0xff) * keep;
    *pixel = out | (uint32_t)lround(a > 255 ? 255 : a) << 24;
}

static uint32_t mix(uint32_t from, uint32_t to, double amount) {
    uint32_t out = 0;
    for (int shift = 0; shift < 24; shift += 8) {
        double a = (from >> shift) & 0xff, b = (to >> shift) & 0xff;
        out |= (uint32_t)lround(a + (b - a) * amount) << shift;
    }
    return out;
}

/* Rec. 709's luma of a colour as it is encoded, 0 to 1: light or dark enough to tell. */
static double luma(uint32_t rgb) {
    return (0.2126 * ((rgb >> 16) & 0xff) + 0.7152 * ((rgb >> 8) & 0xff) + 0.0722 * (rgb & 0xff)) /
           255;
}

/* The distance from (px, py) to the segment from (x0, y0) to (x1, y1). */
static double segment_distance(double px, double py, double x0, double y0, double x1, double y1) {
    double dx = x1 - x0, dy = y1 - y0, length = dx * dx + dy * dy;
    double t = length > 0 ? ((px - x0) * dx + (py - y0) * dy) / length : 0;
    t = clamp01(t);
    return hypot(px - (x0 + t * dx), py - (y0 + t * dy));
}

/* The crosshair over the pixel picked: four arms, white on a dark halo, leaving the pixel and
 * those around it to be seen. */
static void paint_crosshair(uint32_t *pixels, const struct sh_loupe_layout *layout) {
    double scale = layout->scale;
    double cx = layout->hot_pixel_x + 0.5, cy = layout->hot_pixel_y + 0.5;
    double gap = CROSS_GAP * scale, reach = CROSS_REACH * scale;
    // Lines an odd number of pixels wide, centred on the pixel's middle, so that they stay crisp.
    double core = scale >= 2.5 ? 1.5 : 0.5;
    double halo = core + (scale < 1 ? 1 : floor(scale * 0.75 + 0.5));
    double arms[4][4] = {{cx - reach, cy, cx - gap, cy},
                         {cx + gap, cy, cx + reach, cy},
                         {cx, cy - reach, cx, cy - gap},
                         {cx, cy + gap, cx, cy + reach}};
    int from_x = (int)floor(cx - reach - halo - 1), to_x = (int)ceil(cx + reach + halo + 1);
    int from_y = (int)floor(cy - reach - halo - 1), to_y = (int)ceil(cy + reach + halo + 1);
    for (int pass = 0; pass < 2; ++pass) {
        for (int y = from_y < 0 ? 0 : from_y; y < to_y && y < layout->height; ++y) {
            for (int x = from_x < 0 ? 0 : from_x; x < to_x && x < layout->width; ++x) {
                double d = INFINITY;
                for (int i = 0; i < 4; ++i) {
                    double here = segment_distance(x + 0.5, y + 0.5, arms[i][0], arms[i][1],
                                                   arms[i][2], arms[i][3]);
                    d = here < d ? here : d;
                }
                uint32_t *pixel = &pixels[(size_t)y * layout->width + x];
                if (pass == 0)
                    blend(pixel, 0x000000, 0.5 * clamp01(halo + 0.5 - d));
                else
                    blend(pixel, 0xffffff, clamp01(core + 0.5 - d));
            }
        }
    }
}

void sh_loupe_paint_frame(uint32_t *pixels, const struct sh_loupe_layout *layout,
                          const struct sh_loupe_look *look) {
    memset(pixels, 0, (size_t)layout->width * layout->height * sizeof(*pixels));
    double scale = layout->scale;
    double sigma = SHADOW_SIGMA * scale, drop = SHADOW_DROP * scale;
    double darkest = look->light ? 0.2 : 0.42;
    double hairline = scale < 1 ? 1 : floor(scale + 0.25);
    const double cx = layout->card_x, cy = layout->card_y;
    const double cw = layout->card_width, ch = layout->card_height, cr = layout->card_radius;
    for (int y = 0; y < layout->height; ++y) {
        for (int x = 0; x < layout->width; ++x) {
            uint32_t *pixel = &pixels[(size_t)y * layout->width + x];
            double px = x + 0.5, py = y + 0.5;
            // The shadow: the card blurred and dropped a little, which erfc of the distance to
            // its edge makes well enough for a shape this round.
            double shadow = box_distance(px, py - drop, cx, cy, cw, ch, cr);
            double alpha = darkest * 0.5 * erfc(shadow / (sigma * M_SQRT2));
            if (alpha > 1 / 512.0)
                blend(pixel, 0x000000, alpha);
            double d = box_distance(px, py, cx, cy, cw, ch, cr);
            double inside = coverage(d);
            if (inside <= 0)
                continue;
            blend(pixel, look->surface, inside);
            // A hairline along its edge, as the shell's cards have.
            blend(pixel, look->text, look->text_alpha * 0.14 * band(d, -hairline, 0) * inside);
        }
    }
    paint_crosshair(pixels, layout);
}

/* The checkerboard of a cell off every monitor: two shades of the card. */
static uint32_t nothing_at(const struct sh_loupe_look *look, int x, int y, int square) {
    bool dark = ((x / square) + (y / square)) & 1;
    return mix(look->surface, look->text, dark ? 0.1 : 0.04);
}

void sh_loupe_paint_cells(uint32_t *pixels, const struct sh_loupe_layout *layout,
                          const struct sh_loupe_look *look, const uint32_t *cells) {
    const int cell = layout->cell, grid = SH_LOUPE_CELLS * cell;
    const int square = cell / 2 > 1 ? cell / 2 : 1, round = layout->grid_radius + 1;
    // Painted again as the pointer crosses each pixel: the grid's edge worked out only in its
    // rounded corners, where it is not whole pixels.
    for (int y = 0; y < grid; ++y) {
        int gy = layout->grid_y + y;
        if (gy < 0 || gy >= layout->height)
            continue;
        bool corner_row = y < round || y >= grid - round;
        for (int x = 0; x < grid; ++x) {
            int gx = layout->grid_x + x;
            if (gx < 0 || gx >= layout->width)
                continue;
            double inside = 1;
            if (corner_row && (x < round || x >= grid - round))
                inside = coverage(box_distance(gx + 0.5, gy + 0.5, layout->grid_x, layout->grid_y,
                                               grid, grid, layout->grid_radius));
            if (inside <= 0)
                continue;
            int column = x / cell, row = y / cell;
            uint32_t value = cells[row * SH_LOUPE_CELLS + column], rgb;
            if (!value) {
                rgb = nothing_at(look, x, y, square);
            } else {
                rgb = value & 0xffffff;
                // A faint line between pixels, darker on a light one and lighter on a dark one.
                if ((x % cell == 0 && column > 0) || (y % cell == 0 && row > 0))
                    rgb = mix(rgb, luma(rgb) > 0.5 ? 0x000000 : 0xffffff, 0.12);
            }
            uint32_t *pixel = &pixels[(size_t)gy * layout->width + gx];
            if (inside >= 1)
                *pixel = 0xff000000u | rgb;
            else
                blend(pixel, rgb, inside);
        }
    }
    uint32_t middle = cells[(SH_LOUPE_CELLS / 2) * SH_LOUPE_CELLS + SH_LOUPE_CELLS / 2];
    if (!middle)
        return;
    double scale = layout->scale;
    // The middle cell ringed in the accent, a dark line either side of the ring so that it shows
    // against any colour.
    double ring = scale * 2 < 2 ? 2 : floor(scale * 2 + 0.25), line = scale < 1 ? 1 : scale * 0.75;
    double mx = layout->grid_x + (SH_LOUPE_CELLS / 2) * cell;
    double my = layout->grid_y + (SH_LOUPE_CELLS / 2) * cell;
    int reach = (int)ceil(ring + line + 1);
    for (int y = (int)my - reach; y < (int)my + cell + reach; ++y) {
        for (int x = (int)mx - reach; x < (int)mx + cell + reach; ++x) {
            if (x < 0 || y < 0 || x >= layout->width || y >= layout->height)
                continue;
            double d = box_distance(x + 0.5, y + 0.5, mx, my, cell, cell, scale);
            uint32_t *pixel = &pixels[(size_t)y * layout->width + x];
            blend(pixel, 0x000000, 0.45 * band(d, -line, 0));
            blend(pixel, look->accent, band(d, 0, ring));
            blend(pixel, 0x000000, 0.45 * band(d, ring, ring + line));
        }
    }
    // The label: a swatch of the colour picked, and its hex.
    double sx = layout->swatch_x, sy = layout->swatch_y, size = layout->swatch_size;
    double hairline = scale < 1 ? 1 : floor(scale + 0.25);
    for (int y = layout->swatch_y; y < layout->swatch_y + layout->swatch_size; ++y) {
        for (int x = layout->swatch_x; x < layout->swatch_x + layout->swatch_size; ++x) {
            if (x < 0 || y < 0 || x >= layout->width || y >= layout->height)
                continue;
            double d = box_distance(x + 0.5, y + 0.5, sx, sy, size, size, layout->swatch_radius);
            uint32_t *pixel = &pixels[(size_t)y * layout->width + x];
            blend(pixel, middle & 0xffffff, coverage(d));
            blend(pixel, look->text, look->text_alpha * 0.3 * band(d, -hairline, 0));
        }
    }
    char hex[8];
    sh_color_hex(middle, hex);
    sh_loupe_text(pixels, layout->width, layout->height, layout->text_x, layout->text_top,
                  layout->text_cap, hex, look->text, look->text_alpha);
}

void sh_color_hex(uint32_t rgb, char text[8]) {
    snprintf(text, 8, "#%06x", (unsigned)(rgb & 0xffffff));
}

bool sh_color_parse(const char *text, uint32_t *rgb) {
    if (!text || text[0] != '#' || strlen(text) != 7)
        return false;
    uint32_t value = 0;
    for (int i = 1; i < 7; ++i) {
        char c = text[i];
        int digit = c >= '0' && c <= '9'   ? c - '0'
                    : c >= 'a' && c <= 'f' ? c - 'a' + 10
                    : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                           : -1;
        if (digit < 0)
            return false;
        value = value << 4 | (uint32_t)digit;
    }
    *rgb = value;
    return true;
}

/* ST 2084's PQ: an encoded value to light, 1 for 10000 cd/m². */
static double pq_decode(double encoded) {
    const double m1 = 2610.0 / 16384, m2 = 2523.0 / 4096 * 128;
    const double c1 = 3424.0 / 4096, c2 = 2413.0 / 4096 * 32, c3 = 2392.0 / 4096 * 32;
    double e = pow(clamp01(encoded), 1 / m2);
    double top = e - c1 > 0 ? e - c1 : 0;
    return pow(top / (c2 - c3 * e), 1 / m1);
}

uint32_t sh_color_from_pq(uint32_t pixel) {
    // The scene puts SDR white at 203 cd/m² of PQ's 10000 (wlroots' reference luminances).
    const double white = 203.0 / 10000;
    double c[3];
    for (int i = 0; i < 3; ++i)
        c[i] = pq_decode(((pixel >> (16 - 8 * i)) & 0xff) / 255.0) / white;
    // BT.2020's primaries to BT.709's (sRGB's), both with D65 white.
    static const double to_709[3][3] = {{1.660491, -0.587641, -0.072850},
                                        {-0.124550, 1.132900, -0.008349},
                                        {-0.018151, -0.100579, 1.118730}};
    uint32_t out = 0;
    for (int i = 0; i < 3; ++i) {
        double linear = to_709[i][0] * c[0] + to_709[i][1] * c[1] + to_709[i][2] * c[2];
        double encoded = pow(clamp01(linear), 1 / 2.2);
        out |= (uint32_t)lround(encoded * 255) << (16 - 8 * i);
    }
    return out;
}

/* The label's strokes, in units of the capitals' height: a glyph is GLYPH_WIDTH wide, from 0 at
 * the top of the capitals to 1 on the baseline, lowercase from 0.3, each ADVANCE from the last. */
#define GLYPH_WIDTH 0.56
#define ADVANCE 0.74
#define STROKE 0.15
#define SEGMENTS 96

struct path {
    double segments[SEGMENTS][4];
    int count;
    double x, y;
};

static void path_move(struct path *path, double x, double y) {
    path->x = x;
    path->y = y;
}

static void path_line(struct path *path, double x, double y) {
    if (path->count < SEGMENTS) {
        double *segment = path->segments[path->count++];
        segment[0] = path->x;
        segment[1] = path->y;
        segment[2] = x;
        segment[3] = y;
    }
    path->x = x;
    path->y = y;
}

/* An arc of the ellipse about (cx, cy) from angle `from` to `to` in degrees, clockwise on the
 * screen for a growing angle (0 to the right, 90 down), joined to where the path is if `join`. */
static void path_arc(struct path *path, double cx, double cy, double rx, double ry, double from,
                     double to, bool join) {
    int steps = (int)ceil(fabs(to - from) / 12);
    for (int i = 0; i <= steps; ++i) {
        double angle = (from + (to - from) * i / (steps ? steps : 1)) * M_PI / 180;
        double x = cx + rx * cos(angle), y = cy + ry * sin(angle);
        if (i == 0 && !join)
            path_move(path, x, y);
        else
            path_line(path, x, y);
    }
}

static void path_ellipse(struct path *path, double cx, double cy, double rx, double ry) {
    path_arc(path, cx, cy, rx, ry, 0, 360, false);
}

/* `c`'s strokes; false for a character the label has no glyph for. */
static bool glyph_path(char c, struct path *path) {
    path->count = 0;
    switch (c) {
    case '#':
        path_move(path, 0.23, 0.08), path_line(path, 0.17, 0.92);
        path_move(path, 0.43, 0.08), path_line(path, 0.37, 0.92);
        path_move(path, 0.05, 0.34), path_line(path, 0.54, 0.34);
        path_move(path, 0.02, 0.66), path_line(path, 0.51, 0.66);
        return true;
    case '0':
        path_ellipse(path, 0.28, 0.5, 0.24, 0.5);
        return true;
    case '1':
        path_move(path, 0.1, 0.2), path_line(path, 0.3, 0), path_line(path, 0.3, 1);
        path_move(path, 0.08, 1), path_line(path, 0.5, 1);
        return true;
    case '2':
        path_arc(path, 0.28, 0.28, 0.23, 0.27, 200, 385, false);
        path_line(path, 0.04, 1), path_line(path, 0.53, 1);
        return true;
    case '3':
        path_arc(path, 0.27, 0.255, 0.22, 0.235, 205, 450, false);
        path_arc(path, 0.27, 0.745, 0.25, 0.255, 270, 515, true);
        return true;
    case '4':
        path_move(path, 0.4, 1), path_line(path, 0.4, 0), path_line(path, 0.02, 0.68);
        path_line(path, 0.55, 0.68);
        return true;
    case '5':
        path_move(path, 0.5, 0), path_line(path, 0.11, 0), path_line(path, 0.08, 0.45);
        path_arc(path, 0.28, 0.67, 0.25, 0.33, 220, 505, true);
        return true;
    case '6':
    case '9': {
        path_ellipse(path, 0.28, 0.68, 0.24, 0.32);
        path_arc(path, 0.52, 0.66, 0.48, 0.66, 180, 258, false);
        if (c == '9') { // a 6 turned about its middle
            for (int i = 0; i < path->count; ++i) {
                double *s = path->segments[i];
                s[0] = GLYPH_WIDTH - s[0], s[1] = 1 - s[1];
                s[2] = GLYPH_WIDTH - s[2], s[3] = 1 - s[3];
            }
        }
        return true;
    }
    case '7':
        path_move(path, 0.03, 0), path_line(path, 0.53, 0), path_line(path, 0.18, 1);
        return true;
    case '8':
        path_ellipse(path, 0.28, 0.25, 0.2, 0.25);
        path_ellipse(path, 0.28, 0.735, 0.245, 0.265);
        return true;
    case 'a':
        path_ellipse(path, 0.26, 0.65, 0.23, 0.35);
        path_move(path, 0.49, 0.3), path_line(path, 0.49, 1);
        return true;
    case 'b':
        path_move(path, 0.05, 0), path_line(path, 0.05, 1);
        path_ellipse(path, 0.29, 0.65, 0.24, 0.35);
        return true;
    case 'c':
        path_arc(path, 0.29, 0.65, 0.24, 0.35, 35, 325, false);
        return true;
    case 'd':
        path_move(path, 0.51, 0), path_line(path, 0.51, 1);
        path_ellipse(path, 0.27, 0.65, 0.24, 0.35);
        return true;
    case 'e':
        path_move(path, 0.05, 0.65), path_line(path, 0.52, 0.65);
        path_arc(path, 0.285, 0.65, 0.235, 0.35, 360, 40, true);
        return true;
    case 'f':
        path_move(path, 0.2, 1), path_line(path, 0.2, 0.2);
        path_arc(path, 0.4, 0.2, 0.2, 0.2, 180, 300, true);
        path_move(path, 0.04, 0.32), path_line(path, 0.46, 0.32);
        return true;
    default:
        return false;
    }
}

double sh_loupe_text_width(const char *text, double cap) {
    int glyphs = 0;
    struct path path;
    for (; *text; ++text)
        glyphs += glyph_path(*text, &path);
    return glyphs ? (glyphs * ADVANCE - (ADVANCE - GLYPH_WIDTH)) * cap : 0;
}

/* A glyph's coverage, 0 to 255, in a box of whole pixels from (`x`, `y`) of the picture, for
 * one size and place within a pixel. The label is drawn again for each pixel the pointer crosses,
 * the same few glyphs at the same few places, so the last MASKS painted are kept. */
#define MASKS 64
struct mask {
    char c;
    double cap, fraction_x, fraction_y;
    int x, y, width, height; /* from the glyph's whole-pixel origin */
    uint8_t *coverage;
};
static struct mask masks[MASKS];
static int next_mask;

static const struct mask *mask_of(char c, double cap, double fraction_x, double fraction_y) {
    for (int i = 0; i < MASKS; ++i) {
        const struct mask *m = &masks[i];
        if (m->coverage && m->c == c && m->cap == cap && m->fraction_x == fraction_x &&
            m->fraction_y == fraction_y)
            return m;
    }
    struct path path;
    if (!glyph_path(c, &path))
        return NULL;
    double half = STROKE * cap / 2;
    struct mask made = {.c = c, .cap = cap, .fraction_x = fraction_x, .fraction_y = fraction_y};
    made.x = (int)floor(fraction_x - half - 1);
    made.y = (int)floor(fraction_y - half - 1);
    made.width = (int)ceil(fraction_x + GLYPH_WIDTH * cap + half + 1) - made.x;
    made.height = (int)ceil(fraction_y + cap + half + 1) - made.y;
    made.coverage = malloc((size_t)made.width * made.height);
    if (!made.coverage)
        return NULL;
    for (int row = 0; row < made.height; ++row) {
        for (int column = 0; column < made.width; ++column) {
            double px = made.x + column + 0.5, py = made.y + row + 0.5, d = INFINITY;
            for (int i = 0; i < path.count; ++i) {
                const double *seg = path.segments[i];
                double here =
                    segment_distance(px, py, fraction_x + seg[0] * cap, fraction_y + seg[1] * cap,
                                     fraction_x + seg[2] * cap, fraction_y + seg[3] * cap);
                d = here < d ? here : d;
            }
            made.coverage[row * made.width + column] =
                (uint8_t)lround(255 * clamp01(half + 0.5 - d));
        }
    }
    struct mask *slot = &masks[next_mask];
    next_mask = (next_mask + 1) % MASKS;
    free(slot->coverage);
    *slot = made;
    return slot;
}

void sh_loupe_text(uint32_t *pixels, int width, int height, double x, double top, double cap,
                   const char *text, uint32_t rgb, double alpha) {
    int whole_y = (int)floor(top);
    for (; *text; ++text) {
        int whole_x = (int)floor(x);
        const struct mask *m = mask_of(*text, cap, x - whole_x, top - whole_y);
        if (!m)
            continue;
        for (int row = 0; row < m->height; ++row) {
            int py = whole_y + m->y + row;
            if (py < 0 || py >= height)
                continue;
            for (int column = 0; column < m->width; ++column) {
                int px = whole_x + m->x + column;
                uint8_t c = m->coverage[row * m->width + column];
                if (c && px >= 0 && px < width)
                    blend(&pixels[(size_t)py * width + px], rgb, alpha * c / 255);
            }
        }
        x += ADVANCE * cap;
    }
}
