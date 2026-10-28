// SPDX-License-Identifier: GPL-3.0-or-later
/* The colour picker's loupe (src/color_picker.c): hex out, its layout in whole pixels at
 * whole and fractional scales on every side of the crosshair, what it paints where, the label's
 * text, and an HDR monitor's colours taken back to sRGB. */
#include "paw/color_picker.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "check.h"

static const struct sh_loupe_look LOOK = {
    .surface = 0x1e2030, .text = 0xcad3f5, .accent = 0xf5bde6, .text_alpha = 1, .light = false};

static int red(uint32_t pixel) { return (int)(pixel >> 16 & 0xff); }
static int green(uint32_t pixel) { return (int)(pixel >> 8 & 0xff); }
static int blue(uint32_t pixel) { return (int)(pixel & 0xff); }
static int alpha(uint32_t pixel) { return (int)(pixel >> 24); }

static void test_hex(void) {
    char text[8];
    sh_color_hex(0x417bc4, text);
    CHECK(!strcmp(text, "#417bc4"), "%s", text);
    sh_color_hex(0xff00abcd, text);
    CHECK(!strcmp(text, "#00abcd"), "the alpha is no part of it: %s", text);
}

static void test_layout(void) {
    const double scales[] = {1, 1.25, 1.5, 1.75, 2, 2.5, 3};
    for (size_t i = 0; i < sizeof(scales) / sizeof(*scales); ++i) {
        double scale = scales[i];
        for (int side = 0; side < 4; ++side) {
            struct sh_loupe_layout l;
            sh_loupe_layout(scale, side & 1, side & 2, &l);
            // Drawn at its logical size, the picture comes out at its own size exactly, as
            // wlroots sizes a cursor: the logical size truncated, times the scale, rounded.
            CHECK(l.width == (int)lroundf((float)l.logical_width * (float)scale) &&
                      (int)((float)l.width / (float)scale) == l.logical_width,
                  "scale %g side %d: %dx%d at %dx%d", scale, side, l.width, l.height,
                  l.logical_width, l.logical_height);
            CHECK(l.height == (int)lroundf((float)l.logical_height * (float)scale) &&
                      (int)((float)l.height / (float)scale) == l.logical_height,
                  "scale %g side %d", scale, side);
            CHECK(l.hot_pixel_x == (int)roundf((float)l.hot_x * (float)scale) &&
                      l.hot_pixel_y == (int)roundf((float)l.hot_y * (float)scale),
                  "the hotspot where wlroots puts it");
            CHECK(l.cell == (int)lround(10 * scale), "scale %g: cells of %d", scale, l.cell);
            // The card, its grid and its label inside the picture, on the side asked for, clear
            // of the crosshair.
            CHECK(l.card_x >= 0 && l.card_y >= 0 && l.card_x + l.card_width <= l.width &&
                      l.card_y + l.card_height <= l.height,
                  "scale %g side %d: the card inside", scale, side);
            CHECK(l.grid_x > l.card_x && l.grid_x + SH_LOUPE_CELLS * l.cell < l.card_x + l.card_width);
            CHECK(l.swatch_y > l.grid_y + SH_LOUPE_CELLS * l.cell &&
                  l.swatch_y + l.swatch_size < l.card_y + l.card_height);
            CHECK(l.text_x + 7 * 0.74 * l.text_cap < l.card_x + l.card_width, "the hex fits");
            bool left = side & 1, above = side & 2;
            CHECK(left ? l.card_x + l.card_width < l.hot_pixel_x : l.card_x > l.hot_pixel_x + 1);
            CHECK(above ? l.card_y + l.card_height < l.hot_pixel_y : l.card_y > l.hot_pixel_y + 1);
            double width, height;
            sh_loupe_card_size(scale, &width, &height);
            NEAR(width, l.card_logical_width, 1e-9);
            NEAR(height, l.card_logical_height, 1e-9);
            NEAR(l.card_logical_width, 126, 5);
        }
    }
    // A scale that cannot come out whole in a few logical pixels comes within a pixel.
    struct sh_loupe_layout l;
    sh_loupe_layout(1.0 + 1.0 / 120, false, false, &l);
    CHECK(abs(l.width - (int)lround(l.logical_width * (1.0 + 1.0 / 120))) <= 1);
    sh_loupe_layout(0, false, false, &l);
    CHECK(l.scale == 1, "no scale is 1");
}

/* The pixels the loupe shows: a gradient, with the column right of the middle off the screen. */
static void fill_cells(uint32_t *cells) {
    for (int row = 0; row < SH_LOUPE_CELLS; ++row) {
        for (int column = 0; column < SH_LOUPE_CELLS; ++column) {
            cells[row * SH_LOUPE_CELLS + column] =
                column == SH_LOUPE_CELLS - 1
                    ? 0
                    : 0xff000000u | (uint32_t)(column * 20) << 16 | (uint32_t)(row * 20) << 8 | 0x80;
        }
    }
    cells[(SH_LOUPE_CELLS / 2) * SH_LOUPE_CELLS + SH_LOUPE_CELLS / 2] = 0xff417bc4;
}

static void test_paint(double scale) {
    struct sh_loupe_layout l;
    sh_loupe_layout(scale, false, false, &l);
    uint32_t *pixels = malloc((size_t)l.width * l.height * sizeof(*pixels));
    sh_loupe_paint_frame(pixels, &l, &LOOK);
    uint32_t cells[SH_LOUPE_CELLS * SH_LOUPE_CELLS];
    fill_cells(cells);
    sh_loupe_paint_cells(pixels, &l, &LOOK, cells);
#define AT(x, y) pixels[(size_t)(y) * l.width + (x)]
    // Each cell's middle shows its pixel as it is, opaque.
    for (int row = 0; row < SH_LOUPE_CELLS; ++row) {
        for (int column = 0; column < SH_LOUPE_CELLS - 1; ++column) {
            uint32_t want = cells[row * SH_LOUPE_CELLS + column];
            uint32_t got = AT(l.grid_x + column * l.cell + l.cell / 2,
                              l.grid_y + row * l.cell + l.cell / 2);
            CHECK(got == want, "scale %g cell %d,%d: %08x, not %08x", scale, column, row, got,
                  want);
        }
    }
    // One off the screen is a checkerboard of the card's shades, not a colour.
    uint32_t off = AT(l.grid_x + (SH_LOUPE_CELLS - 1) * l.cell + 1, l.grid_y + 5 * l.cell + 1);
    CHECK(alpha(off) == 255 && abs(red(off) - 0x1e) < 0x20 && abs(blue(off) - 0x30) < 0x20,
          "%08x", off);
    // The middle is ringed in the accent just outside it.
    int ring = (int)(scale * 2 < 2 ? 2 : floor(scale * 2 + 0.25));
    uint32_t ringed = AT(l.grid_x + 5 * l.cell + l.cell / 2, l.grid_y + 5 * l.cell - ring / 2 - 1);
    CHECK(abs(red(ringed) - 0xf5) <= 24 && abs(green(ringed) - 0xbd) <= 24, "%08x", ringed);
    // The swatch shows the middle's colour.
    uint32_t swatch = AT(l.swatch_x + l.swatch_size / 2, l.swatch_y + l.swatch_size / 2);
    CHECK(swatch == 0xff417bc4, "%08x", swatch);
    // The hex is written beside it in the text's colour: some pixels near it, none elsewhere
    // in its row.
    int text_pixels = 0;
    for (int y = (int)l.text_top; y < (int)(l.text_top + l.text_cap); ++y) {
        for (int x = (int)l.text_x; x < (int)(l.text_x + 6 * l.text_cap); ++x)
            text_pixels += abs(red(AT(x, y)) - 0xca) < 12 && abs(blue(AT(x, y)) - 0xf5) < 12;
    }
    CHECK(text_pixels > 20 * scale * scale, "scale %g: %d", scale, text_pixels);
    // The card: its surface between the grid and the label, opaque.
    uint32_t card = AT(l.card_x + l.card_width / 2, l.grid_y + SH_LOUPE_CELLS * l.cell + 2);
    CHECK(card == 0xff1e2030, "%08x", card);
    // The shadow falls under it, and nothing is drawn in the picture's far corner.
    uint32_t under = AT(l.card_x + l.card_width / 2, l.card_y + l.card_height + (int)(4 * scale));
    CHECK(alpha(under) > 20 && red(under) == 0, "%08x", under);
    CHECK(AT(l.width - 1, 0) == 0 && AT(0, l.height - 1) == 0);
    // The crosshair leaves the pixel picked to be seen, its arms white around it.
    CHECK(AT(l.hot_pixel_x, l.hot_pixel_y) == 0, "%08x", AT(l.hot_pixel_x, l.hot_pixel_y));
    uint32_t arm = AT(l.hot_pixel_x + (int)(6 * scale), l.hot_pixel_y);
    CHECK(arm == 0xffffffff, "scale %g: %08x", scale, arm);
    uint32_t beside = AT(l.hot_pixel_x + (int)(6 * scale), l.hot_pixel_y - 1);
    CHECK(alpha(beside) > 0 && red(beside) == 0, "a dark halo: %08x", beside);
    // Nothing of the middle off the screen: no ring and no label.
    sh_loupe_paint_frame(pixels, &l, &LOOK);
    cells[(SH_LOUPE_CELLS / 2) * SH_LOUPE_CELLS + SH_LOUPE_CELLS / 2] = 0;
    sh_loupe_paint_cells(pixels, &l, &LOOK, cells);
    swatch = AT(l.swatch_x + l.swatch_size / 2, l.swatch_y + l.swatch_size / 2);
    CHECK(swatch == 0xff1e2030, "%08x", swatch);
#undef AT
    free(pixels);
}

static void test_text(void) {
    NEAR(sh_loupe_text_width("", 10), 0, 1e-9);
    NEAR(sh_loupe_text_width("#417bc4", 10), sh_loupe_text_width("#000000", 10), 1e-9);
    NEAR(sh_loupe_text_width("#41 7?bc4", 10), sh_loupe_text_width("#417bc4", 10), 1e-9);
    NEAR(sh_loupe_text_width("ab", 20), 2 * sh_loupe_text_width("ab", 10), 1e-9);
    // Every glyph draws something, inside its own box.
    const char *glyphs = "#0123456789abcdef";
    for (const char *c = glyphs; *c; ++c) {
        uint32_t pixels[40 * 40] = {0};
        char one[2] = {*c, 0};
        sh_loupe_text(pixels, 40, 40, 10, 10, 20, one, 0xffffff, 1);
        int drawn = 0, outside = 0;
        for (int y = 0; y < 40; ++y) {
            for (int x = 0; x < 40; ++x) {
                if (!pixels[y * 40 + x])
                    continue;
                ++drawn;
                outside += x < 8 || x > 24 || y < 8 || y > 32;
            }
        }
        CHECK(drawn > 30 && !outside, "'%c': %d drawn, %d outside", *c, drawn, outside);
    }
    // Cut at the picture's edges.
    uint32_t small[4 * 4] = {0};
    sh_loupe_text(small, 4, 4, -5, -5, 20, "#888", 0xffffff, 1);
}

/* sRGB to what an HDR monitor's frame holds, as the scene draws it: gamma 2.2 undone, BT.709's
 * primaries to BT.2020's, SDR white at 203 cd/m², PQ, 8 bits. */
static uint32_t to_pq(uint32_t rgb) {
    static const double to_2020[3][3] = {{0.627404, 0.329283, 0.043313},
                                         {0.069097, 0.919540, 0.011362},
                                         {0.016391, 0.088013, 0.895595}};
    double linear[3];
    for (int i = 0; i < 3; ++i)
        linear[i] = pow(((rgb >> (16 - 8 * i)) & 0xff) / 255.0, 2.2);
    const double m1 = 2610.0 / 16384, m2 = 2523.0 / 4096 * 128;
    const double c1 = 3424.0 / 4096, c2 = 2413.0 / 4096 * 32, c3 = 2392.0 / 4096 * 32;
    uint32_t out = 0;
    for (int i = 0; i < 3; ++i) {
        double y = (to_2020[i][0] * linear[0] + to_2020[i][1] * linear[1] +
                    to_2020[i][2] * linear[2]) * 203 / 10000;
        double p = pow(y, m1);
        double e = pow((c1 + c2 * p) / (1 + c3 * p), m2);
        out |= (uint32_t)lround(e * 255) << (16 - 8 * i);
    }
    return out;
}

static void test_pq(void) {
    CHECK(sh_color_from_pq(to_pq(0xffffff)) == 0xffffff, "%06x", sh_color_from_pq(to_pq(0xffffff)));
    CHECK(sh_color_from_pq(0) == 0);
    // PQ's 8 bits are coarse below SDR white: within a few steps of where it started, but for
    // what nearly cancels out going back from BT.2020's wider primaries, a little of the others
    // in a primary.
    const uint32_t colours[] = {0x417bc4, 0x23314a, 0xf5bde6, 0x808080, 0xc0ffee, 0xff0000,
                                0x00ff00, 0x0000ff};
    for (size_t i = 0; i < sizeof(colours) / sizeof(*colours); ++i) {
        uint32_t back = sh_color_from_pq(to_pq(colours[i]));
        int worst = abs(red(back) - red(colours[i]));
        worst = fmax(worst, abs(green(back) - green(colours[i])));
        worst = fmax(worst, abs(blue(back) - blue(colours[i])));
        CHECK(worst <= (i < 5 ? 6 : 16), "%06x came back %06x", colours[i], back);
    }
}

int main(void) {
    test_hex();
    test_layout();
    test_paint(1);
    test_paint(1.25);
    test_paint(2);
    test_text();
    test_pq();
    return failures ? 1 : 0;
}
