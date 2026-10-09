// SPDX-License-Identifier: GPL-3.0-or-later
#include "paw/overview.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

void require(bool value, const char *what) {
    if (!value)
        throw std::runtime_error(what);
}
bool disjoint(const sh_rect &a, const sh_rect &b) {
    return a.x + a.width <= b.x || b.x + b.width <= a.x || a.y + a.height <= b.y ||
           b.y + b.height <= a.y;
}
bool inside(const sh_rect &r, const sh_rect &area) {
    return r.x >= area.x && r.y >= area.y && r.x + r.width <= area.x + area.width &&
           r.y + r.height <= area.y + area.height;
}

int main() {
    try {
        const sh_rect area{40, 120, 1840, 900};
        const sh_rect shapes[] = {{0, 0, 1920, 1080}, {0, 0, 800, 600}, {0, 0, 300, 700},
                                  {0, 0, 2560, 1440}, {0, 0, 1, 1},     {0, 0, 640, 200}};
        for (int count = 1; count <= 60; ++count) {
            for (int gap : {0, 16, 40}) {
                std::vector<sh_rect> sizes, out(count);
                for (int i = 0; i < count; ++i)
                    sizes.push_back(shapes[(i * 7 + count) % 6]);
                require(sh_overview_grid(sizes.data(), count, area, gap, 1.0, out.data()),
                        "grid placed");
                for (int i = 0; i < count; ++i) {
                    require(out[i].width >= 1 && out[i].height >= 1, "thumbnail has a size");
                    require(inside(out[i], area), "thumbnail inside the area");
                    require(out[i].width <= sizes[i].width && out[i].height <= sizes[i].height,
                            "never larger than the window");
                    if (sizes[i].width >= 50 && sizes[i].height >= 50 && out[i].width >= 30) {
                        double window = double(sizes[i].width) / sizes[i].height;
                        double thumb = double(out[i].width) / out[i].height;
                        require(std::abs(window - thumb) / window < 0.06, "aspect ratio kept");
                    }
                    for (int j = 0; j < i; ++j)
                        require(disjoint(out[i], out[j]), "thumbnails do not overlap");
                }
            }
        }

        // A lone window keeps its size when it fits, and shrinks to fit when it does not.
        sh_rect one;
        require(sh_overview_grid(&shapes[1], 1, area, 16, 1.0, &one), "one window");
        require(one.width == 800 && one.height == 600, "a window that fits is not enlarged");
        require(one.x == area.x + (area.width - 800) / 2 &&
                    one.y == area.y + (area.height - 600) / 2,
                "a lone thumbnail is centered");
        require(sh_overview_grid(&shapes[3], 1, area, 16, 1.0, &one), "one big window");
        require(one.height == 900 && one.width == 1600, "a big window shrinks to the height");
        // Room to grow only when max_scale allows it.
        require(sh_overview_grid(&shapes[1], 1, area, 16, 2.0, &one) && one.height == 900,
                "max_scale allows enlarging");

        // Four windows of one shape make a 2 x 2 grid, in reading order.
        sh_rect four_in[4] = {shapes[0], shapes[0], shapes[0], shapes[0]}, four[4];
        require(sh_overview_grid(four_in, 4, area, 20, 1.0, four), "four windows");
        require(four[0].y == four[1].y && four[2].y == four[3].y && four[0].y < four[2].y,
                "two rows");
        require(four[0].x == four[2].x && four[1].x == four[3].x && four[0].x < four[1].x,
                "two columns");
        // Three in a 2 x 2 grid: the last row is centered.
        sh_rect three_in[3] = {shapes[0], shapes[0], shapes[0]}, three[3];
        require(sh_overview_grid(three_in, 3, area, 20, 1.0, three), "three windows");
        require(three[0].y == three[1].y && three[2].y > three[0].y, "three windows in two rows");
        int middle = three[2].x + three[2].width / 2;
        require(std::abs(middle - (area.x + area.width / 2)) <= 1, "the short row is centered");
        // A wide strip of windows prefers a single row; tall ones prefer columns.
        sh_rect tall_in[3] = {shapes[2], shapes[2], shapes[2]}, tall[3];
        require(sh_overview_grid(tall_in, 3, area, 20, 1.0, tall), "tall windows");
        require(tall[0].y == tall[1].y && tall[1].y == tall[2].y, "tall windows share a row");

        // Bad input.
        require(!sh_overview_grid(nullptr, 1, area, 0, 1, &one), "no sizes");
        require(!sh_overview_grid(&shapes[0], 0, area, 0, 1, &one), "no windows");
        require(!sh_overview_grid(&shapes[0], 1, {0, 0, 0, 10}, 0, 1, &one), "empty area");
        require(!sh_overview_grid(&shapes[0], 1, area, -1, 1, &one), "negative gap");
        require(!sh_overview_grid(&shapes[0], 1, area, 0, 0, &one), "no scale");
        // Gaps larger than the area are dropped instead of failing.
        sh_rect crowd_in[30], crowd[30];
        for (auto &size : crowd_in)
            size = shapes[0];
        require(sh_overview_grid(crowd_in, 30, {0, 0, 100, 100}, 90, 1.0, crowd), "crowded");
        for (const auto &thumb : crowd)
            require(thumb.width > 0 && thumb.x >= 0 && thumb.y >= 0 && thumb.x + thumb.width <= 100 &&
                        thumb.y + thumb.height <= 100,
                    "a crowded thumbnail outside the area");

        // Directions in a 3 x 2 grid.
        sh_rect grid[5] = {{0, 0, 100, 100},   {120, 0, 100, 100},   {240, 0, 100, 100},
                           {60, 120, 100, 100}, {180, 120, 100, 100}};
        require(sh_overview_neighbour(grid, 5, 0, SH_OVERVIEW_RIGHT) == 1, "right");
        require(sh_overview_neighbour(grid, 5, 1, SH_OVERVIEW_LEFT) == 0, "left");
        require(sh_overview_neighbour(grid, 5, 0, SH_OVERVIEW_DOWN) == 3, "down");
        require(sh_overview_neighbour(grid, 5, 2, SH_OVERVIEW_DOWN) == 4, "down from the right");
        require(sh_overview_neighbour(grid, 5, 4, SH_OVERVIEW_UP) == 2 ||
                    sh_overview_neighbour(grid, 5, 4, SH_OVERVIEW_UP) == 1,
                "up");
        require(sh_overview_neighbour(grid, 5, 0, SH_OVERVIEW_UP) == 0, "no row above");
        require(sh_overview_neighbour(grid, 5, 4, SH_OVERVIEW_DOWN) == 4, "no row below");
        require(sh_overview_neighbour(grid, 5, 2, SH_OVERVIEW_RIGHT) == 3, "wraps forward");
        require(sh_overview_neighbour(grid, 5, 0, SH_OVERVIEW_LEFT) == 4, "wraps backward");
        require(sh_overview_neighbour(grid, 1, 0, SH_OVERVIEW_RIGHT) == 0, "alone");
        require(sh_overview_neighbour(grid, 5, 9, SH_OVERVIEW_RIGHT) == 9, "bad index");

        // The strip.
        sh_rect cells[10];
        const sh_rect band{0, 10, 1000, 200};
        require(sh_overview_strip(4, band, 20, 16.0 / 9, 150, cells), "strip");
        for (int i = 0; i < 4; ++i) {
            require(inside(cells[i], band), "cell inside the band");
            require(cells[i].height <= 150, "cell not taller than allowed");
            require(std::abs(double(cells[i].width) / cells[i].height - 16.0 / 9) < 0.05,
                    "cell aspect");
            if (i)
                require(cells[i].x >= cells[i - 1].x + cells[i - 1].width + 19, "gap kept");
        }
        require(std::abs((cells[0].x) - (band.width - cells[3].x - cells[3].width)) <= 2,
                "strip centered");
        require(sh_overview_strip(10, {0, 0, 300, 100}, 4, 1.6, 0, cells), "narrow strip");
        require(cells[9].x + cells[9].width <= 300, "narrow strip fits");
        require(!sh_overview_strip(0, band, 0, 1, 0, cells), "no cells");
        require(!sh_overview_strip(2, band, 0, 0, 0, cells), "no aspect");

        // Filtering.
        require(sh_overview_matches("Firefox - Mozilla", ""), "empty matches");
        require(sh_overview_matches("Firefox - Mozilla", "FIRE"), "case-insensitive");
        require(sh_overview_matches("Firefox - Mozilla", "moz fire"), "words in any order");
        require(!sh_overview_matches("Firefox - Mozilla", "fire zzz"), "every word must match");
        require(sh_overview_matches("kitty", "  kit  "), "surrounding blanks ignored");
        require(sh_overview_matches(nullptr, "  "), "blank text matches");
        require(!sh_overview_matches(nullptr, "a"), "null haystack");
        std::cout << "Overview grid, navigation, strip, and filter passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
