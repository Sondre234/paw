// SPDX-License-Identifier: GPL-3.0-or-later
/* The pointer heatmap's arithmetic (src/heatmap.c): the grid's counts and their resizing, the
 * blur, the scale and its colours, resampling, and the legend's text. */
#include "check.h"
#include "paw/heatmap.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <string>
#include <vector>

namespace {

uint32_t alpha(uint32_t pixel) { return pixel >> 24; }

uint64_t total(const sh_heat_grid &grid, sh_heat_kind kind) {
    return std::accumulate(grid.cells[kind], grid.cells[kind] + grid.columns * grid.rows,
                           uint64_t{0});
}

void grid() {
    CHECK(sh_heat_cells(1280) == 160 && sh_heat_cells(1281) == 161 && sh_heat_cells(3) == 1 &&
              sh_heat_cells(0) == 1,
          "cells cover the whole length, partly at the far edge");

    sh_heat_grid grid{};
    sh_heat_add(&grid, SH_HEAT_MOTION, 0, 0, 5); // nothing to count in yet
    CHECK(sh_heat_most(&grid, SH_HEAT_MOTION) == 0);
    CHECK(!sh_heat_grid_resize(&grid, 0, 3), "a grid has a cell at least");
    CHECK(sh_heat_grid_resize(&grid, 4, 3) && grid.columns == 4 && grid.rows == 3);
    sh_heat_add(&grid, SH_HEAT_MOTION, 1, 2, 30);
    sh_heat_add(&grid, SH_HEAT_MOTION, 1, 2, 12);
    sh_heat_add(&grid, SH_HEAT_CLICKS, 3, 0, 1);
    CHECK(grid.cells[SH_HEAT_MOTION][2 * 4 + 1] == 42, "time adds up in the cell");
    CHECK(grid.cells[SH_HEAT_CLICKS][3] == 1 && total(grid, SH_HEAT_CLICKS) == 1,
          "clicks are counted apart from time");
    sh_heat_add(&grid, SH_HEAT_MOTION, 9, -2, 7);
    CHECK(grid.cells[SH_HEAT_MOTION][3] == 7, "a place past the edge counts in the edge's cell");
    sh_heat_add(&grid, SH_HEAT_MOTION, 0, 0, UINT32_MAX - 1);
    sh_heat_add(&grid, SH_HEAT_MOTION, 0, 0, 10);
    CHECK(grid.cells[SH_HEAT_MOTION][0] == UINT32_MAX, "a full cell stays full");
    CHECK(sh_heat_most(&grid, SH_HEAT_MOTION) == UINT32_MAX);
    grid.cells[SH_HEAT_MOTION][0] = 0;

    // Twice the cells each way (a scale halved): each count goes where its cell's middle went.
    CHECK(sh_heat_grid_resize(&grid, 8, 6));
    CHECK(grid.cells[SH_HEAT_MOTION][5 * 8 + 3] == 42, "%u", grid.cells[SH_HEAT_MOTION][5 * 8 + 3]);
    CHECK(total(grid, SH_HEAT_MOTION) == 49 && total(grid, SH_HEAT_CLICKS) == 1,
          "resizing keeps the totals");
    // Fewer cells: counts that land together add up.
    sh_heat_add(&grid, SH_HEAT_MOTION, 2, 4, 8);
    CHECK(sh_heat_grid_resize(&grid, 2, 2));
    CHECK(grid.cells[SH_HEAT_MOTION][2] == 50 && total(grid, SH_HEAT_MOTION) == 57, "%u %u %u %u",
          grid.cells[SH_HEAT_MOTION][0], grid.cells[SH_HEAT_MOTION][1],
          grid.cells[SH_HEAT_MOTION][2], grid.cells[SH_HEAT_MOTION][3]);
    CHECK(sh_heat_grid_resize(&grid, 2, 2) && total(grid, SH_HEAT_MOTION) == 57,
          "the same size changes nothing");

    sh_heat_grid_clear(&grid);
    CHECK(total(grid, SH_HEAT_MOTION) == 0 && total(grid, SH_HEAT_CLICKS) == 0 && grid.columns == 2,
          "clearing keeps the cells");
    sh_heat_grid_free(&grid);
    CHECK(!grid.cells[0] && !grid.columns);
}

void blur() {
    // A point spreads evenly about itself, round, keeping what it held while it stays clear of
    // the edges, where a cell weighs fewer cells.
    int columns = 25, rows = 25;
    std::vector<float> values(columns * rows), scratch(columns);
    values[12 * columns + 12] = 1000;
    sh_heat_blur(values.data(), columns, rows, 1, scratch.data());
    auto at = [&](int column, int row) { return values[row * columns + column]; };
    float sum = std::accumulate(values.begin(), values.end(), 0.0F);
    NEAR(sum, 1000, 0.01);
    CHECK(at(12, 12) == sh_heat_peak(values.data(), values.size()), "the middle stays hottest");
    NEAR(at(10, 12), at(14, 12), 1e-4);
    NEAR(at(12, 9), at(12, 15), 1e-4);
    NEAR(at(10, 12), at(12, 10), 1e-4);
    NEAR(at(12 + 3, 12 + 4), at(12 + 5, 12), 1e-6); // as far, 5 cells, so as warm: round
    CHECK(at(11, 12) < at(12, 12) && at(10, 12) < at(11, 12) && at(6, 12) < at(10, 12),
          "it cools away from the middle");
    CHECK(at(12 + 7, 12) == 0 && at(12, 12 + 7) == 0 && at(12 + 6, 12) > 0,
          "a gaussian of 1 reaches 6 cells");
    NEAR(at(12, 12), 1000 / (2 * M_PI), 1); // a gaussian's middle: 1 / (2 pi sigma squared)

    // An even field stays even, also at the edges.
    std::vector<float> even(columns * rows, 3);
    sh_heat_blur(even.data(), columns, rows, 3, scratch.data());
    bool flat = true;
    for (float value : even)
        flat = flat && std::fabs(value - 3) < 1e-4;
    CHECK(flat, "the edges do not cool");

    // The field is the grid's counts blurred.
    sh_heat_grid grid{};
    sh_heat_grid_resize(&grid, columns, rows);
    sh_heat_add(&grid, SH_HEAT_CLICKS, 12, 12, 1000);
    std::vector<float> field(columns * rows);
    sh_heat_field(&grid, SH_HEAT_CLICKS, field.data(), scratch.data());
    std::fill(values.begin(), values.end(), 0.0F);
    values[12 * columns + 12] = 1000;
    sh_heat_blur(values.data(), columns, rows, SH_HEAT_BLUR, scratch.data());
    CHECK(field == values, "the field blurs by SH_HEAT_BLUR");
    sh_heat_field(&grid, SH_HEAT_MOTION, field.data(), scratch.data());
    CHECK(sh_heat_peak(field.data(), field.size()) == 0);
    sh_heat_grid_free(&grid);
}

void scale() {
    NEAR(sh_heat_level(0, 100), 0, 0);
    NEAR(sh_heat_level(100, 100), 1, 1e-12);
    NEAR(sh_heat_level(500, 100), 1, 0);
    NEAR(sh_heat_level(5, 0), 0, 0);
    double tenth = sh_heat_level(10, 100), hundredth = sh_heat_level(1, 100);
    CHECK(tenth > 0.7 && tenth < 0.8, "%g", tenth);
    CHECK(hundredth > 0.45 && hundredth < 0.55, "%g", hundredth);
    CHECK(sh_heat_level(0.01, 100) > 0.05, "a path passed along still shows: %g",
          sh_heat_level(0.01, 100));

    auto color = [](double level) { return sh_heat_color(level); };
    CHECK(color(0).a == 0 && color(-1).a == 0, "nothing is clear");
    NEAR(color(0.1).a, 0.5, 1e-9);
    CHECK(color(0.1).b == 1 && color(0.1).r == 0, "the cold end is blue");
    auto is = [&](double level, double r, double g, double b) {
        auto c = color(level);
        return std::fabs(c.r - r) < 1e-9 && std::fabs(c.g - g) < 1e-9 &&
               std::fabs(c.b - b) < 1e-9 && c.a == 1;
    };
    CHECK(is(0.2, 0, 0.25, 1), "blue");
    CHECK(is(0.4, 0, 0.9, 1), "cyan");
    CHECK(is(0.55, 0.1, 0.9, 0.2), "green");
    CHECK(is(0.7, 1, 0.9, 0), "yellow");
    CHECK(is(0.85, 1, 0.15, 0), "red");
    CHECK(is(1, 1, 1, 1) && is(2, 1, 1, 1), "white at the top");
    CHECK(is(0.3, 0, 0.575, 1), "between two stops, halfway");

    CHECK(sh_heat_pixel(0, 1) == 0, "nothing paints nothing");
    CHECK(sh_heat_pixel(1, 1) == 0xffffffff);
    CHECK(sh_heat_pixel(1, 0.5) == 0x80808080, "%08x", sh_heat_pixel(1, 0.5));
    CHECK(sh_heat_pixel(0.85, 1) == 0xffff2600, "%08x", sh_heat_pixel(0.85, 1));

    float field[] = {0, 50, 100, 0.01F};
    uint32_t pixels[4];
    sh_heat_paint(field, 2, 2, 100, 0.6, pixels);
    CHECK(pixels[0] == 0 && alpha(pixels[2]) == 153 && pixels[2] == sh_heat_pixel(1, 0.6));
    CHECK(alpha(pixels[1]) == 153 && alpha(pixels[3]) > 0 && alpha(pixels[3]) < 153, "%08x %08x",
          pixels[1], pixels[3]);
}

void resample() {
    // Shrinking averages what each new cell covers, in proportion.
    float row[] = {1, 2, 3, 4, 5, 6};
    float half[3], third[2], fifth[5], scratch[16];
    sh_heat_resample(row, 6, 1, half, 3, 1, scratch);
    NEAR(half[0], 1.5, 1e-6);
    NEAR(half[2], 5.5, 1e-6);
    sh_heat_resample(row, 6, 1, third, 2, 1, scratch);
    NEAR(third[0], 2, 1e-6);
    NEAR(third[1], 5, 1e-6);
    sh_heat_resample(row, 6, 1, fifth, 5, 1, scratch);
    NEAR(fifth[0], 1.4 / 1.2, 1e-5); // 1.2 cells: all of the first, a fifth of the second
    NEAR(std::accumulate(fifth, fifth + 5, 0.0F) * 6 / 5, 21, 1e-4);

    // Growing lies on lines between the middles, the ends held.
    float two[] = {0, 10}, four[4];
    sh_heat_resample(two, 2, 1, four, 4, 1, scratch);
    NEAR(four[0], 0, 1e-6);
    NEAR(four[1], 2.5, 1e-6);
    NEAR(four[2], 7.5, 1e-6);
    NEAR(four[3], 10, 1e-6);

    // Both axes: a 4 by 2 grid to 2 by 4.
    float grid[] = {1, 1, 3, 3, 5, 5, 7, 7}, out[8];
    sh_heat_resample(grid, 4, 2, out, 2, 4, scratch);
    NEAR(out[0], 1, 1e-6);
    NEAR(out[1], 3, 1e-6);
    NEAR(out[2], 2, 1e-6);
    NEAR(out[5], 6, 1e-6);
    NEAR(out[7], 7, 1e-6);
}

void legend() {
    CHECK(sh_heat_text_width("") == 0);
    CHECK(sh_heat_text_width("0") == 5);
    CHECK(sh_heat_text_width("12") == 11, "a dot between glyphs");
    CHECK(sh_heat_text_width("1.5 s") == 5 + 1 + 2 + 1 + 5 + 1 + 3 + 1 + 5);
    CHECK(sh_heat_text_width("1?") == 5, "an unknown character is left out");

    std::vector<uint32_t> pixels(12 * 9);
    sh_heat_text(pixels.data(), 12, 9, 1, 1, 1, "1", 0xffffffff);
    auto lit = [&](int x, int y) { return pixels[y * 12 + x] == 0xffffffff; };
    // '1': ..#.. / .##.. / ..#.. ... / .###.
    CHECK(lit(3, 1) && !lit(2, 1) && lit(2, 2) && lit(3, 2) && lit(3, 6) && lit(2, 7) &&
              lit(4, 7) && !lit(1, 7) && !lit(5, 7),
          "the glyph lands at its corner");
    CHECK(!lit(0, 0) && !lit(3, 8));
    std::vector<uint32_t> big(12 * 18);
    sh_heat_text(big.data(), 12, 18, 0, 0, 2, "1", 0x80000000);
    CHECK(big[0 * 12 + 4] == 0x80000000 && big[1 * 12 + 5] == 0x80000000 && big[0] == 0,
          "a dot of two pixels");
    sh_heat_text(big.data(), 12, 18, 9, 15, 2, "8", 0xffffffff); // cut at the edges

    char label[32];
    auto says = [&](sh_heat_kind kind, double value, const char *text) {
        sh_heat_label(label, sizeof(label), kind, value);
        return std::strcmp(label, text) == 0;
    };
    CHECK(says(SH_HEAT_MOTION, 0, "0 ms"));
    CHECK(says(SH_HEAT_MOTION, 450, "450 ms"));
    CHECK(says(SH_HEAT_MOTION, 999.7, "1.0 s"), "%s", label);
    CHECK(says(SH_HEAT_MOTION, 1500, "1.5 s"));
    CHECK(says(SH_HEAT_MOTION, 9990, "10 s"), "%s", label);
    CHECK(says(SH_HEAT_MOTION, 42000, "42 s"));
    CHECK(says(SH_HEAT_MOTION, 59900, "1.0 min"), "%s", label);
    CHECK(says(SH_HEAT_MOTION, 150e3, "2.5 min"));
    CHECK(says(SH_HEAT_MOTION, 2400e3, "40 min"));
    CHECK(says(SH_HEAT_MOTION, 5400e3, "1.5 h"));
    CHECK(says(SH_HEAT_MOTION, 90000e3, "25 h"));
    CHECK(says(SH_HEAT_CLICKS, 1, "1 click") && says(SH_HEAT_CLICKS, 0, "0 clicks") &&
          says(SH_HEAT_CLICKS, 37, "37 clicks"));
    for (char c : std::string("pointer rest clicks 0123456789. min h ms"))
        CHECK(sh_heat_text_width(std::string(1, c).c_str()) > 0,
              "the legend's own words are all in its font: '%c'", c);

    // The card: see-through dark, round at its corners, the scale clear to white along it.
    for (int scale : {1, 2}) {
        int width = SH_HEAT_LEGEND_WIDTH * scale, height = SH_HEAT_LEGEND_HEIGHT * scale;
        std::vector<uint32_t> card(width * height);
        sh_heat_legend(card.data(), scale, SH_HEAT_MOTION, 12000);
        auto pixel = [&](int x, int y) { return card[y * scale * width + x * scale]; };
        CHECK(alpha(pixel(0, 0)) == 0, "a round corner");
        CHECK(alpha(pixel(SH_HEAT_LEGEND_WIDTH / 2, SH_HEAT_LEGEND_HEIGHT - 1)) == 179, "%08x",
              pixel(SH_HEAT_LEGEND_WIDTH / 2, SH_HEAT_LEGEND_HEIGHT - 1));
        int bar = 12 + 14 + 8 + 5;
        uint32_t cold = pixel(12, bar), hot = pixel(SH_HEAT_LEGEND_WIDTH - 13, bar);
        CHECK(hot == 0xffffffff || (hot & 0xffffff) > 0xf0f0f0, "%08x", hot);
        CHECK((cold & 0xff) < 0x40 && ((cold >> 16) & 0xff) < 0x20, "%08x", cold);
        uint32_t middle = pixel(12 + 104, bar);
        CHECK(((middle >> 8) & 0xff) > 0xa0, "green in the middle: %08x", middle);
    }
}

} // namespace

int main() {
    grid();
    blur();
    scale();
    resample();
    legend();
    return failures != 0;
}
