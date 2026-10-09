// SPDX-License-Identifier: GPL-3.0-or-later
// windows.placement: cascade, center and smart placement of new floating windows.
#include "paw/backend.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

static void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
static bool apart(const sh_rect &a, const sh_rect &b) {
    return a.x + a.width <= b.x || b.x + b.width <= a.x || a.y + a.height <= b.y ||
           b.y + b.height <= a.y;
}
static bool inside(const sh_rect &r, const sh_rect &area) {
    return r.x >= area.x && r.y >= area.y && r.x + r.width <= area.x + area.width &&
           r.y + r.height <= area.y + area.height;
}
static sh_rect place(sh_place_mode mode, sh_rect area, const std::vector<sh_rect> &others, int w,
                     int h, int index = 0) {
    sh_rect result{};
    require(sh_place_window(mode, area, others.data(), static_cast<int>(others.size()), w, h,
                            index, &result),
            "placement failed");
    require(result.width == w && result.height == h, "the size changed");
    return result;
}

int main() {
    try {
        const sh_rect area{0, 0, 1280, 672};
        // Cascade: 40 pixels in, 32 more for each window, wrapping after 8, as it always was.
        auto first = place(SH_PLACE_CASCADE, area, {}, 320, 240, 0);
        require(first.x == 40 && first.y == 40, "cascade start");
        auto third = place(SH_PLACE_CASCADE, area, {}, 320, 240, 2);
        require(third.x == 104 && third.y == 104, "cascade step");
        auto ninth = place(SH_PLACE_CASCADE, area, {}, 320, 240, 8);
        require(ninth.x == 40 && ninth.y == 40, "cascade wraps");
        auto offset = place(SH_PLACE_CASCADE, {100, 50, 1280, 672}, {}, 320, 240, 1);
        require(offset.x == 172 && offset.y == 122, "cascade follows the area's origin");
        auto tight = place(SH_PLACE_CASCADE, area, {}, 1200, 600, 7); // 40 + 224 + 1200 > 1280
        require(tight.x == 40 && tight.y == 40, "a window too big to step falls back to the margin");
        auto tiny = place(SH_PLACE_CASCADE, {0, 0, 60, 60}, {}, 50, 50, 3);
        require(tiny.x == 0 && tiny.y == 0, "no margin in a tiny output");
        // Center, whatever is there.
        auto middle = place(SH_PLACE_CENTER, {10, 20, 1000, 500}, {{0, 0, 2000, 2000}}, 400, 200);
        require(middle.x == 310 && middle.y == 170, "center");
        auto big = place(SH_PLACE_CENTER, {10, 20, 300, 200}, {}, 400, 250);
        require(big.x == 10 && big.y == 20, "a window bigger than the area starts at its corner");

        // Smart: alone it is centered.
        auto alone = place(SH_PLACE_SMART, area, {}, 320, 240);
        require(alone.x == 480 && alone.y == 216, "smart with nothing there centers");
        // Beside a window on the left it takes the middle of what is free on the right.
        std::vector<sh_rect> left{{0, 0, 640, 672}};
        auto right = place(SH_PLACE_SMART, area, left, 320, 240);
        require(apart(right, left[0]) && inside(right, area), "smart avoids the window");
        require(right.x == 800 && right.y == 216, "smart centers in the free half");
        // Below a window across the top.
        std::vector<sh_rect> top{{0, 0, 1280, 400}};
        auto below = place(SH_PLACE_SMART, area, top, 320, 240);
        require(apart(below, top[0]) && inside(below, area), "smart avoids the window above");
        require(below.x == 480, "smart centers across the free strip");
        // A hole between two windows that fits it is found.
        std::vector<sh_rect> around{{0, 0, 400, 672}, {880, 0, 400, 672}};
        auto hole = place(SH_PLACE_SMART, area, around, 480, 300);
        require(hole.x == 400 && apart(hole, around[0]) && apart(hole, around[1]), "the hole is used");
        // Windows on other outputs or far outside the area do not count.
        auto elsewhere = place(SH_PLACE_SMART, area, {{2000, 0, 500, 500}, {-900, 0, 500, 500}}, 320, 240);
        require(elsewhere.x == 480 && elsewhere.y == 216, "windows outside the area are ignored");
        // Filling the area with tiles: none is free, so it cascades by index.
        std::vector<sh_rect> full{{0, 0, 640, 672}, {640, 0, 640, 672}};
        auto crowded = place(SH_PLACE_SMART, area, full, 320, 240, 2);
        require(crowded.x == 104 && crowded.y == 104, "smart cascades when nothing is free");
        // Many windows in a grid with one gap: the gap is found.
        std::vector<sh_rect> grid;
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 4; ++column)
                if (row != 1 || column != 2)
                    grid.push_back({column * 320, row * 224, 320, 224});
        auto gap = place(SH_PLACE_SMART, area, grid, 300, 200);
        require(gap.x >= 640 && gap.x + 300 <= 960 && gap.y >= 224 && gap.y + 200 <= 448,
                "the single free cell is used");
        // Invariants over many configurations: inside the area, and clear when there is room.
        unsigned seed = 7;
        auto random = [&](int limit) {
            seed = seed * 1103515245U + 12345U;
            return static_cast<int>((seed >> 8) % static_cast<unsigned>(limit));
        };
        for (int round = 0; round < 500; ++round) {
            std::vector<sh_rect> windows;
            int count = random(6);
            for (int i = 0; i < count; ++i)
                windows.push_back({random(1000), random(500), 100 + random(400), 100 + random(300)});
            int w = 100 + random(300), h = 100 + random(250);
            auto spot = place(SH_PLACE_SMART, area, windows, w, h, random(20));
            require(inside(spot, area), "smart left the area");
            bool room = false;
            for (int y = 0; y + h <= area.height && !room; y += 4)
                for (int x = 0; x + w <= area.width && !room; x += 4) {
                    bool clear = true;
                    for (const auto &other : windows)
                        clear = clear && apart({x, y, w, h}, other);
                    room = clear;
                }
            bool clear = true;
            for (const auto &other : windows)
                clear = clear && apart(spot, other);
            // A free spot found on a 4 pixel grid is one the candidates reach as well.
            require(!room || clear, "smart covered a window although there was room");
        }
        // Bad input is refused.
        sh_rect result;
        require(!sh_place_window(SH_PLACE_SMART, {0, 0, 0, 10}, nullptr, 0, 10, 10, 0, &result),
                "an empty area was accepted");
        require(!sh_place_window(SH_PLACE_SMART, area, nullptr, 1, 10, 10, 0, &result),
                "null windows were accepted");
        require(!sh_place_window(SH_PLACE_CENTER, area, nullptr, 0, 0, 10, 0, &result),
                "a zero width was accepted");
        require(!sh_place_window(SH_PLACE_CENTER, area, nullptr, 0, 10, 10, -1, nullptr),
                "no result was accepted");
        std::cout << "Window placement passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
